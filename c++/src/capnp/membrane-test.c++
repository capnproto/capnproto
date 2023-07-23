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

class ThingImpl final: public Thing::Server {
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

class TestMembraneImpl final: public test::TestMembrane::Server {
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
      return req.send().then(
          [KJ_CPCAP(context)](Response<test::TestMembrane::Result>&& result) mutable {
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
      return req.send().then(
          [KJ_CPCAP(context)](Response<test::TestMembrane::Result>&& result) mutable {
        context.setResults(result);
      });
    }
  }

  kj::Promise<void> loopback(LoopbackContext context) override {
    context.getResults().setThing(context.getParams().getThing());
    return kj::READY_NOW;
  }

  kj::Promise<void> waitForever(WaitForeverContext context) override {
    return kj::NEVER_DONE;
  }
};

class MembranePolicyImpl: public MembranePolicy, public kj::Refcounted {
public:
  MembranePolicyImpl() = default;
  MembranePolicyImpl(kj::Maybe<kj::Promise<void>> revokePromise)
      : revokePromise(revokePromise.map([](kj::Promise<void>& p) { return p.fork(); })) {}

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

  kj::Maybe<kj::Promise<void>> onRevoked() override {
    return revokePromise.map([](kj::ForkedPromise<void>& fork) {
      return fork.addBranch();
    });
  }

  bool shouldResolveBeforeRedirecting() override { return true; }

private:
  kj::Maybe<kj::ForkedPromise<void>> revokePromise;
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

KJ_TEST("MembraneHook::whenMoreResolved returns same value even when called concurrently.") {
  TestEnv env;

  auto paf = kj::newPromiseAndFulfiller<test::TestMembrane::Client>();
  test::TestMembrane::Client promCap(kj::mv(paf.promise));

  auto prom = promCap.whenResolved();
  prom = prom.then([promCap = kj::mv(promCap), &env]() mutable {
    auto membraned = membrane(kj::mv(promCap), env.policy->addRef());
    auto hook = ClientHook::from(membraned);

    auto arr = kj::heapArrayBuilder<kj::Promise<kj::Own<ClientHook>>>(2);
    arr.add(KJ_ASSERT_NONNULL(hook->whenMoreResolved()));
    arr.add(KJ_ASSERT_NONNULL(hook->whenMoreResolved()));

    return kj::joinPromises(arr.finish()).attach(kj::mv(hook));
  }).then([](kj::Vector<kj::Own<ClientHook>> hooks) {
    auto first = hooks[0].get();
    auto second = hooks[1].get();
    KJ_ASSERT(first == second);
  }).eagerlyEvaluate(nullptr);

  auto newClient = kj::heap<TestMembraneImpl>();
  paf.fulfiller->fulfill(kj::mv(newClient));
  prom.wait(env.waitScope);
}

struct TestRpcEnv {
  kj::EventLoop loop;
  kj::WaitScope waitScope;
  kj::TwoWayPipe pipe;
  TwoPartyClient client;
  TwoPartyClient server;
  test::TestMembrane::Client membraned;

  TestRpcEnv(kj::Maybe<kj::Promise<void>> revokePromise = nullptr)
      : waitScope(loop),
        pipe(kj::newTwoWayPipe()),
        client(*pipe.ends[0]),
        server(*pipe.ends[1],
               membrane(kj::heap<TestMembraneImpl>(),
                        kj::refcounted<MembranePolicyImpl>(kj::mv(revokePromise))),
               rpc::twoparty::Side::SERVER),
        membraned(client.bootstrap().castAs<test::TestMembrane>()) {}

  void testThing(kj::Function<Thing::Client()> makeThing,
                 kj::StringPtr localPassThrough, kj::StringPtr localIntercept,
                 kj::StringPtr remotePassThrough, kj::StringPtr remoteIntercept) {
    testThingImpl(waitScope, membraned, kj::mv(makeThing),
                  localPassThrough, localIntercept, remotePassThrough, remoteIntercept);
  }
};

KJ_TEST("call remote object inside membrane") {
  TestRpcEnv env;
  env.testThing([&]() {
    return env.membraned.makeThingRequest().send().wait(env.waitScope).getThing();
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
    return req.send().wait(env.waitScope).getThing();
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

KJ_TEST("revoke membrane") {
  auto paf = kj::newPromiseAndFulfiller<void>();

  TestRpcEnv env(kj::mv(paf.promise));

  auto thing = env.membraned.makeThingRequest().send().wait(env.waitScope).getThing();

  auto callPromise = env.membraned.waitForeverRequest().send();

  KJ_EXPECT(!callPromise.poll(env.waitScope));

  paf.fulfiller->reject(KJ_EXCEPTION(DISCONNECTED, "foobar"));

  // TRICKY: We need to use .ignoreResult().wait() below because when compiling with
  //   -fno-exceptions, void waits throw recoverable exceptions while non-void waits necessarily
  //   throw fatal exceptions... but testing for fatal exceptions when exceptions are disabled
  //   involves fork()ing the process to run the code so if it has side effects on file descriptors
  //   then we'll get in a bad state...

  KJ_ASSERT(callPromise.poll(env.waitScope));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("foobar", callPromise.ignoreResult().wait(env.waitScope));

  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("foobar",
      env.membraned.makeThingRequest().send().ignoreResult().wait(env.waitScope));

  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("foobar",
      thing.passThroughRequest().send().ignoreResult().wait(env.waitScope));
}

}  // namespace
}  // namespace _
}  // namespace capnp
