// Copyright (c) 2015 Sandstorm Development Group, Inc. and contributors
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

#include "membrane.h"
#include <kj/test.h>
#include "test-util.h"
#include <kj/function.h>
#include <kj/async-io.h>
#include "rpc-twoparty.h"

namespace capnp {
namespace _ {
namespace {

using Thing = test::TestMembrane::Thing;

class ThingImpl: public Thing::Server {
public:
  ThingImpl(kj::StringPtr text): text(text) {}

protected:
  kj::Promise<void> passThrough(PassThroughContext context) override {
    context.getResults().setText(text);
    return kj::READY_NOW;
  }

  kj::Promise<void> intercept(InterceptContext context) override {
    context.getResults().setText(text);
    return kj::READY_NOW;
  }

private:
  kj::StringPtr text;
};

class TestMembraneImpl: public test::TestMembrane::Server {
protected:
  kj::Promise<void> makeThing(MakeThingContext context) override {
    context.getResults().setThing(kj::heap<ThingImpl>("inside"));
    return kj::READY_NOW;
  }

  kj::Promise<void> callPassThrough(CallPassThroughContext context) override {
    auto params = context.getParams();
    auto req = params.getThing().passThroughRequest();
    if (params.getTailCall()) {
      return context.tailCall(kj::mv(req));
    } else {
      return req.send().then([context](Response<test::TestMembrane::Result>&& result) mutable {
        context.setResults(result);
      });
    }
  }

  kj::Promise<void> callIntercept(CallInterceptContext context) override {
    auto params = context.getParams();
    auto req = params.getThing().interceptRequest();
    if (params.getTailCall()) {
      return context.tailCall(kj::mv(req));
    } else {
      return req.send().then([context](Response<test::TestMembrane::Result>&& result) mutable {
        context.setResults(result);
      });
    }
  }

  kj::Promise<void> loopback(LoopbackContext context) override {
    context.getResults().setThing(context.getParams().getThing());
    return kj::READY_NOW;
  }
};

class MembranePolicyImpl: public MembranePolicy, public kj::Refcounted {
public:
  kj::Maybe<Capability::Client> inboundCall(uint64_t interfaceId, uint16_t methodId,
                                            Capability::Client target) override {
    if (interfaceId == capnp::typeId<Thing>() && methodId == 1) {
      return Capability::Client(kj::heap<ThingImpl>("inbound"));
    } else {
      return nullptr;
    }
  }

  kj::Maybe<Capability::Client> outboundCall(uint64_t interfaceId, uint16_t methodId,
                                             Capability::Client target) override {
    if (interfaceId == capnp::typeId<Thing>() && methodId == 1) {
      return Capability::Client(kj::heap<ThingImpl>("outbound"));
    } else {
      return nullptr;
    }
  }

  kj::Own<MembranePolicy> addRef() override {
    return kj::addRef(*this);
  }
};

void testThingImpl(kj::WaitScope& waitScope, test::TestMembrane::Client membraned,
                   kj::Function<Thing::Client()> makeThing,
                   kj::StringPtr localPassThrough, kj::StringPtr localIntercept,
                   kj::StringPtr remotePassThrough, kj::StringPtr remoteIntercept) {
  KJ_EXPECT(makeThing().passThroughRequest().send().wait(waitScope).getText() == localPassThrough);
  KJ_EXPECT(makeThing().interceptRequest().send().wait(waitScope).getText() == localIntercept);

  {
    auto req = membraned.callPassThroughRequest();
    req.setThing(makeThing());
    req.setTailCall(false);
    KJ_EXPECT(req.send().wait(waitScope).getText() == remotePassThrough);
  }
  {
    auto req = membraned.callInterceptRequest();
    req.setThing(makeThing());
    req.setTailCall(false);
    KJ_EXPECT(req.send().wait(waitScope).getText() == remoteIntercept);
  }
  {
    auto req = membraned.callPassThroughRequest();
    req.setThing(makeThing());
    req.setTailCall(true);
    KJ_EXPECT(req.send().wait(waitScope).getText() == remotePassThrough);
  }
  {
    auto req = membraned.callInterceptRequest();
    req.setThing(makeThing());
    req.setTailCall(true);
    KJ_EXPECT(req.send().wait(waitScope).getText() == remoteIntercept);
  }
}

struct TestEnv {
  kj::EventLoop loop;
  kj::WaitScope waitScope;
  kj::Own<MembranePolicyImpl> policy;
  test::TestMembrane::Client membraned;

  TestEnv()
      : waitScope(loop),
        policy(kj::refcounted<MembranePolicyImpl>()),
        membraned(membrane(kj::heap<TestMembraneImpl>(), policy->addRef())) {}

  void testThing(kj::Function<Thing::Client()> makeThing,
                 kj::StringPtr localPassThrough, kj::StringPtr localIntercept,
                 kj::StringPtr remotePassThrough, kj::StringPtr remoteIntercept) {
    testThingImpl(waitScope, membraned, kj::mv(makeThing),
                  localPassThrough, localIntercept, remotePassThrough, remoteIntercept);
  }
};

KJ_TEST("call local object inside membrane") {
  TestEnv env;
  env.testThing([&]() {
    return env.membraned.makeThingRequest().send().wait(env.waitScope).getThing();
  }, "inside", "inbound", "inside", "inside");
}

KJ_TEST("call local promise inside membrane") {
  TestEnv env;
  env.testThing([&]() {
    return env.membraned.makeThingRequest().send().getThing();
  }, "inside", "inbound", "inside", "inside");
}

KJ_TEST("call local resolved promise inside membrane") {
  TestEnv env;
  env.testThing([&]() {
    auto thing = env.membraned.makeThingRequest().send().getThing();
    thing.whenResolved().wait(env.waitScope);
    return thing;
  }, "inside", "inbound", "inside", "inside");
}

KJ_TEST("call local object outside membrane") {
  TestEnv env;
  env.testThing([&]() {
    return kj::heap<ThingImpl>("outside");
  }, "outside", "outside", "outside", "outbound");
}

KJ_TEST("call local capability that has passed into and back out of membrane") {
  TestEnv env;
  env.testThing([&]() {
    auto req = env.membraned.loopbackRequest();
    req.setThing(kj::heap<ThingImpl>("outside"));
    return req.send().wait(env.waitScope).getThing();
  }, "outside", "outside", "outside", "outbound");
}

KJ_TEST("call local promise pointing into membrane that eventually resolves to outside") {
  TestEnv env;
  env.testThing([&]() {
    auto req = env.membraned.loopbackRequest();
    req.setThing(kj::heap<ThingImpl>("outside"));
    return req.send().getThing();
  }, "outside", "outside", "outside", "outbound");
}

KJ_TEST("apply membrane using copyOutOfMembrane() on struct") {
  TestEnv env;

  env.testThing([&]() {
    MallocMessageBuilder outsideBuilder;
    auto root = outsideBuilder.initRoot<test::TestContainMembrane>();
    root.setCap(kj::heap<ThingImpl>("inside"));
    MallocMessageBuilder insideBuilder;
    insideBuilder.adoptRoot(copyOutOfMembrane(
        root.asReader(), insideBuilder.getOrphanage(), env.policy->addRef()));
    return insideBuilder.getRoot<test::TestContainMembrane>().getCap();
  }, "inside", "inbound", "inside", "inside");
}

KJ_TEST("apply membrane using copyOutOfMembrane() on list") {
  TestEnv env;

  env.testThing([&]() {
    MallocMessageBuilder outsideBuilder;
    auto list = outsideBuilder.initRoot<test::TestContainMembrane>().initList(1);
    list.set(0, kj::heap<ThingImpl>("inside"));
    MallocMessageBuilder insideBuilder;
    insideBuilder.initRoot<test::TestContainMembrane>().adoptList(copyOutOfMembrane(
        list.asReader(), insideBuilder.getOrphanage(), env.policy->addRef()));
    return insideBuilder.getRoot<test::TestContainMembrane>().getList()[0];
  }, "inside", "inbound", "inside", "inside");
}

KJ_TEST("apply membrane using copyOutOfMembrane() on AnyPointer") {
  TestEnv env;

  env.testThing([&]() {
    MallocMessageBuilder outsideBuilder;
    auto ptr = outsideBuilder.initRoot<test::TestAnyPointer>().getAnyPointerField();
    ptr.setAs<test::TestMembrane::Thing>(kj::heap<ThingImpl>("inside"));
    MallocMessageBuilder insideBuilder;
    insideBuilder.initRoot<test::TestAnyPointer>().getAnyPointerField().adopt(copyOutOfMembrane(
        ptr.asReader(), insideBuilder.getOrphanage(), env.policy->addRef()));
    return insideBuilder.getRoot<test::TestAnyPointer>().getAnyPointerField()
        .getAs<test::TestMembrane::Thing>();
  }, "inside", "inbound", "inside", "inside");
}

struct TestRpcEnv {
  kj::AsyncIoContext io;
  kj::TwoWayPipe pipe;
  TwoPartyClient client;
  TwoPartyClient server;
  test::TestMembrane::Client membraned;

  TestRpcEnv()
      : io(kj::setupAsyncIo()),
        pipe(io.provider->newTwoWayPipe()),
        client(*pipe.ends[0]),
        server(*pipe.ends[1],
               membrane(kj::heap<TestMembraneImpl>(), kj::refcounted<MembranePolicyImpl>()),
               rpc::twoparty::Side::SERVER),
        membraned(client.bootstrap().castAs<test::TestMembrane>()) {}

  void testThing(kj::Function<Thing::Client()> makeThing,
                 kj::StringPtr localPassThrough, kj::StringPtr localIntercept,
                 kj::StringPtr remotePassThrough, kj::StringPtr remoteIntercept) {
    testThingImpl(io.waitScope, membraned, kj::mv(makeThing),
                  localPassThrough, localIntercept, remotePassThrough, remoteIntercept);
  }
};

KJ_TEST("call remote object inside membrane") {
  TestRpcEnv env;
  env.testThing([&]() {
    return env.membraned.makeThingRequest().send().wait(env.io.waitScope).getThing();
  }, "inside", "inbound", "inside", "inside");
}

KJ_TEST("call remote promise inside membrane") {
  TestRpcEnv env;
  env.testThing([&]() {
    return env.membraned.makeThingRequest().send().getThing();
  }, "inside", "inbound", "inside", "inside");
}

KJ_TEST("call remote resolved promise inside membrane") {
  TestEnv env;
  env.testThing([&]() {
    auto thing = env.membraned.makeThingRequest().send().getThing();
    thing.whenResolved().wait(env.waitScope);
    return thing;
  }, "inside", "inbound", "inside", "inside");
}

KJ_TEST("call remote object outside membrane") {
  TestRpcEnv env;
  env.testThing([&]() {
    return kj::heap<ThingImpl>("outside");
  }, "outside", "outside", "outside", "outbound");
}

KJ_TEST("call remote capability that has passed into and back out of membrane") {
  TestRpcEnv env;
  env.testThing([&]() {
    auto req = env.membraned.loopbackRequest();
    req.setThing(kj::heap<ThingImpl>("outside"));
    return req.send().wait(env.io.waitScope).getThing();
  }, "outside", "outside", "outside", "outbound");
}

KJ_TEST("call remote promise pointing into membrane that eventually resolves to outside") {
  TestRpcEnv env;
  env.testThing([&]() {
    auto req = env.membraned.loopbackRequest();
    req.setThing(kj::heap<ThingImpl>("outside"));
    return req.send().getThing();
  }, "outside", "outside", "outside", "outbound");
}

}  // namespace
}  // namespace _
}  // namespace capnp
