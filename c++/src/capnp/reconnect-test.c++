// Copyright (c) 2020 Cloudflare, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "reconnect.h"
#include "test-util.h"
#include <kj/debug.h>
#include <kj/test.h>
#include <kj/async-io.h>
#include "rpc-twoparty.h"

namespace capnp {
namespace _ {
namespace {

class TestInterfaceImpl final: public test::TestInterface::Server {
public:
  TestInterfaceImpl(uint generation): generation(generation) {}

  void setError(kj::Exception e) {
    error = kj::mv(e);
  }

  kj::Own<kj::PromiseFulfiller<void>> block() {
    auto paf = kj::newPromiseAndFulfiller<void>();
    blocker = paf.promise.fork();
    return kj::mv(paf.fulfiller);
  }

protected:
  kj::Promise<void> foo(FooContext context) override {
    KJ_IF_SOME(e, error) {
      return kj::cp(e);
    }
    auto params = context.getParams();
    context.initResults().setX(kj::str(params.getI(), ' ', params.getJ(), ' ', generation));
    return blocker.addBranch();
  }

private:
  uint generation;
  kj::Maybe<kj::Exception> error;
  kj::ForkedPromise<void> blocker = kj::Promise<void>(kj::READY_NOW).fork();
};

using WrapClientFn = kj::Function<test::TestInterface::Client(test::TestInterface::Client)>;

enum class AutoReconnectFn {
  AUTO_RECONNECT,
  LAZY_AUTO_RECONNECT,
  LAZY_AUTO_RECONNECT_WITH_CONTROLLER,
};

void doAutoReconnectTestImpl(kj::WaitScope& ws, WrapClientFn wrapClient, AutoReconnectFn autoReconnectFn) {
  TestInterfaceImpl* currentServer = nullptr;
  uint connectCount = 0;

  auto connect = [&]() {
    auto server = kj::heap<TestInterfaceImpl>(++connectCount);
    currentServer = server;
    return test::TestInterface::Client(kj::mv(server));
  };
  test::TestInterface::Client client = nullptr;
  kj::Own<AutoReconnectController> controller;
  switch (autoReconnectFn) {
    case AutoReconnectFn::AUTO_RECONNECT:
      client = autoReconnect(kj::mv(connect));
      KJ_EXPECT(connectCount == 1);
      break;
    case AutoReconnectFn::LAZY_AUTO_RECONNECT:
      client = lazyAutoReconnect(kj::mv(connect));
      KJ_EXPECT(connectCount == 0);
      break;
    case AutoReconnectFn::LAZY_AUTO_RECONNECT_WITH_CONTROLLER:
      auto result = lazyAutoReconnectWithController(kj::mv(connect));
      client = kj::mv(result.cap);
      controller = kj::mv(result.controller);
      KJ_EXPECT(connectCount == 0);
      break;
  }
  client = wrapClient(kj::mv(client));

  auto testPromise = [&](uint i, bool j) {
    auto req = client.fooRequest();
    req.setI(i);
    req.setJ(j);
    return req.send();
  };

  auto test = [&](uint i, bool j) {
    return kj::str(testPromise(i, j).wait(ws).getX());
  };

  KJ_EXPECT(test(123, true) == "123 true 1");
  KJ_EXPECT(connectCount == 1);

  currentServer->setError(KJ_EXCEPTION(DISCONNECTED, "test1 disconnect"));
  if (autoReconnectFn == AutoReconnectFn::LAZY_AUTO_RECONNECT_WITH_CONTROLLER) {
    controller->reset();
    KJ_EXPECT(connectCount == 1);
    KJ_EXPECT(test(456, true) == "456 true 2");
  } else {
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("test1 disconnect",
        testPromise(456, true).ignoreResult().wait(ws));
  }
  KJ_EXPECT(connectCount == 2);

  KJ_EXPECT(test(789, false) == "789 false 2");
  KJ_EXPECT(test(21, true) == "21 true 2");

  {
    // We cause two disconnect promises to be thrown concurrently. This should only cause the
    // reconnector to reconnect once, not twice.
    auto fulfiller = currentServer->block();
    auto promise1 = testPromise(32, false);
    auto promise2 = testPromise(43, true);
    KJ_EXPECT(!promise1.poll(ws));
    KJ_EXPECT(!promise2.poll(ws));
    fulfiller->reject(KJ_EXCEPTION(DISCONNECTED, "test2 disconnect"));
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("test2 disconnect", promise1.ignoreResult().wait(ws));
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("test2 disconnect", promise2.ignoreResult().wait(ws));
  }
  KJ_EXPECT(connectCount == 3);

  KJ_EXPECT(test(43, false) == "43 false 3");

  // Start a couple calls that will block at the server end, plus an unsent request.
  auto fulfiller = currentServer->block();

  auto promise1 = testPromise(1212, true);
  auto promise2 = testPromise(3434, false);
  auto req3 = client.fooRequest();
  req3.setI(5656);
  req3.setJ(true);
  KJ_EXPECT(!promise1.poll(ws));
  KJ_EXPECT(!promise2.poll(ws));

  // Now force a reconnect.
  currentServer->setError(KJ_EXCEPTION(DISCONNECTED, "test3 disconnect"));

  // Initiate a request that will fail with DISCONNECTED.
  auto promise4 = testPromise(7878, false);

  // And throw away our capability entirely, just to make sure that anyone who needs it is holding
  // onto their own ref.
  client = nullptr;

  KJ_EXPECT(connectCount == 3);
  // Everything we initiated should still finish.
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("test3 disconnect", promise4.ignoreResult().wait(ws));
  KJ_EXPECT(connectCount == 4);

  // Send the request which we created before the disconnect. There are two behaviors we accept
  // as correct here: it may throw the disconnect exception, or it may automatically redirect to
  // the newly-reconnected destination.
  req3.send().then([](Response<test::TestInterface::FooResults> resp) {
    KJ_EXPECT(resp.getX() == "5656 true 4");
  }, [](kj::Exception e) {
    KJ_EXPECT(e.getDescription().endsWith("test3 disconnect"));
  }).wait(ws);

  KJ_EXPECT(!promise1.poll(ws));
  KJ_EXPECT(!promise2.poll(ws));
  fulfiller->fulfill();
  KJ_EXPECT(promise1.wait(ws).getX() == "1212 true 3");
  KJ_EXPECT(promise2.wait(ws).getX() == "3434 false 3");
}

void doAutoReconnectTest(kj::WaitScope& ws, WrapClientFn wrapClient) {
  doAutoReconnectTestImpl(ws, kj::mv(wrapClient), AutoReconnectFn::AUTO_RECONNECT);
}

void doLazyAutoReconnectTest(kj::WaitScope& ws, WrapClientFn wrapClient) {
  doAutoReconnectTestImpl(ws, kj::mv(wrapClient), AutoReconnectFn::LAZY_AUTO_RECONNECT);
}

void doLazyAutoReconnectWithControllerTest(kj::WaitScope& ws, WrapClientFn wrapClient) {
  doAutoReconnectTestImpl(ws, kj::mv(wrapClient), AutoReconnectFn::LAZY_AUTO_RECONNECT_WITH_CONTROLLER);
}

KJ_TEST("autoReconnect() direct call (exercises newCall() / RequestHook)") {
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  doAutoReconnectTest(ws, [](auto c) {return kj::mv(c);});
}

KJ_TEST("autoReconnect() through RPC (exercises call() / CallContextHook)") {
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  auto paf = kj::newPromiseAndFulfiller<test::TestInterface::Client>();

  auto pipe = kj::newTwoWayPipe();
  TwoPartyClient client(*pipe.ends[0]);
  TwoPartyClient server(*pipe.ends[1], kj::mv(paf.promise), rpc::twoparty::Side::SERVER);

  doAutoReconnectTest(ws, [&](test::TestInterface::Client c) {
    paf.fulfiller->fulfill(kj::mv(c));
    return client.bootstrap().castAs<test::TestInterface>();
  });
}

KJ_TEST("lazyAutoReconnect() direct call (exercises newCall() / RequestHook)") {
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  doLazyAutoReconnectTest(ws, [](auto c) {return kj::mv(c);});
}

KJ_TEST("lazyAutoReconnect() through RPC (exercises call() / CallContextHook)") {
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  auto paf = kj::newPromiseAndFulfiller<test::TestInterface::Client>();

  auto pipe = kj::newTwoWayPipe();
  TwoPartyClient client(*pipe.ends[0]);
  TwoPartyClient server(*pipe.ends[1], kj::mv(paf.promise), rpc::twoparty::Side::SERVER);

  doLazyAutoReconnectTest(ws, [&](test::TestInterface::Client c) {
    paf.fulfiller->fulfill(kj::mv(c));
    return client.bootstrap().castAs<test::TestInterface>();
  });
}

KJ_TEST("lazyAutoReconnectWithController() direct call (exercises newCall() / RequestHook)") {
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  doLazyAutoReconnectWithControllerTest(ws, [](auto c) {return kj::mv(c);});
}

KJ_TEST("lazyAutoReconnectWithController() through RPC (exercises call() / CallContextHook)") {
  kj::EventLoop loop;
  kj::WaitScope ws(loop);

  auto paf = kj::newPromiseAndFulfiller<test::TestInterface::Client>();

  auto pipe = kj::newTwoWayPipe();
  TwoPartyClient client(*pipe.ends[0]);
  TwoPartyClient server(*pipe.ends[1], kj::mv(paf.promise), rpc::twoparty::Side::SERVER);

  doLazyAutoReconnectWithControllerTest(ws, [&](test::TestInterface::Client c) {
    paf.fulfiller->fulfill(kj::mv(c));
    return client.bootstrap().castAs<test::TestInterface>();
  });
}

}  // namespace
}  // namespace _
}  // namespace capnp
