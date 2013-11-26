// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "schema.capnp.h"

#ifdef CAPNP_CAPABILITY_H_
#error "schema.capnp should not depend on capability.h, because it contains no interfaces."
#endif

#include "test.capnp.h"

#ifndef CAPNP_CAPABILITY_H_
#error "test.capnp did not include capability.h."
#endif

#include "capability.h"
#include "test-util.h"
#include <kj/debug.h>
#include <gtest/gtest.h>

namespace capnp {
namespace _ {
namespace {

#if KJ_NO_EXCEPTIONS
#undef EXPECT_ANY_THROW
#define EXPECT_ANY_THROW(code) EXPECT_DEATH(code, ".")
#define EXPECT_NONFATAL_FAILURE(code) code
#else
#define EXPECT_NONFATAL_FAILURE EXPECT_ANY_THROW
#endif

TEST(Capability, Basic) {
  kj::SimpleEventLoop loop;

  int callCount = 0;
  test::TestInterface::Client client(kj::heap<TestInterfaceImpl>(callCount), loop);

  auto request1 = client.fooRequest();
  request1.setI(123);
  request1.setJ(true);
  auto promise1 = request1.send();

  auto request2 = client.bazRequest();
  initTestMessage(request2.initS());
  auto promise2 = request2.send();

  bool barFailed = false;
  auto request3 = client.barRequest();
  auto promise3 = loop.there(request3.send(),
      [](Response<test::TestInterface::BarResults>&& response) {
        ADD_FAILURE() << "Expected bar() call to fail.";
      }, [&](kj::Exception&& e) {
        barFailed = true;
      });

  EXPECT_EQ(0, callCount);

  auto response1 = loop.wait(kj::mv(promise1));

  EXPECT_EQ("foo", response1.getX());

  auto response2 = loop.wait(kj::mv(promise2));

  loop.wait(kj::mv(promise3));

  EXPECT_EQ(2, callCount);
  EXPECT_TRUE(barFailed);
}

TEST(Capability, Inheritance) {
  kj::SimpleEventLoop loop;

  int callCount = 0;
  test::TestExtends::Client client1(kj::heap<TestExtendsImpl>(callCount), loop);
  test::TestInterface::Client client2 = client1;
  auto client = client2.castAs<test::TestExtends>();

  auto request1 = client.fooRequest();
  request1.setI(321);
  auto promise1 = request1.send();

  auto request2 = client.graultRequest();
  auto promise2 = request2.send();

  EXPECT_EQ(0, callCount);

  auto response2 = loop.wait(kj::mv(promise2));

  checkTestMessage(response2);

  auto response1 = loop.wait(kj::mv(promise1));

  EXPECT_EQ("bar", response1.getX());

  EXPECT_EQ(2, callCount);
}

TEST(Capability, Pipelining) {
  kj::SimpleEventLoop loop;

  int callCount = 0;
  int chainedCallCount = 0;
  test::TestPipeline::Client client(kj::heap<TestPipelineImpl>(callCount), loop);

  auto request = client.getCapRequest();
  request.setN(234);
  request.setInCap(test::TestInterface::Client(
      kj::heap<TestInterfaceImpl>(chainedCallCount), loop));

  auto promise = request.send();

  auto pipelineRequest = promise.getOutBox().getCap().fooRequest();
  pipelineRequest.setI(321);
  auto pipelinePromise = pipelineRequest.send();

  auto pipelineRequest2 = promise.getOutBox().getCap().castAs<test::TestExtends>().graultRequest();
  auto pipelinePromise2 = pipelineRequest2.send();

  promise = nullptr;  // Just to be annoying, drop the original promise.

  EXPECT_EQ(0, callCount);
  EXPECT_EQ(0, chainedCallCount);

  auto response = loop.wait(kj::mv(pipelinePromise));
  EXPECT_EQ("bar", response.getX());

  auto response2 = loop.wait(kj::mv(pipelinePromise2));
  checkTestMessage(response2);

  EXPECT_EQ(3, callCount);
  EXPECT_EQ(1, chainedCallCount);
}

TEST(Capability, TailCall) {
  kj::SimpleEventLoop loop;

  int calleeCallCount = 0;
  int callerCallCount = 0;

  test::TestTailCallee::Client callee(kj::heap<TestTailCalleeImpl>(calleeCallCount), loop);
  test::TestTailCaller::Client caller(kj::heap<TestTailCallerImpl>(callerCallCount), loop);

  auto request = caller.fooRequest();
  request.setI(456);
  request.setCallee(callee);

  auto promise = request.send();

  auto dependentCall0 = promise.getC().getCallSequenceRequest().send();

  auto response = loop.wait(kj::mv(promise));
  EXPECT_EQ(456, response.getI());
  EXPECT_EQ(456, response.getI());

  auto dependentCall1 = promise.getC().getCallSequenceRequest().send();

  auto dependentCall2 = response.getC().getCallSequenceRequest().send();

  EXPECT_EQ(0, loop.wait(kj::mv(dependentCall0)).getN());
  EXPECT_EQ(1, loop.wait(kj::mv(dependentCall1)).getN());
  EXPECT_EQ(2, loop.wait(kj::mv(dependentCall2)).getN());

  EXPECT_EQ(1, calleeCallCount);
  EXPECT_EQ(1, callerCallCount);
}

TEST(Capability, AsyncCancelation) {
  // Tests allowAsyncCancellation().

  kj::SimpleEventLoop loop;

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = loop.there(kj::mv(paf.promise), [&]() { destroyed = true; });
  destructionPromise.eagerlyEvaluate(loop);

  int callCount = 0;

  test::TestMoreStuff::Client client(kj::heap<TestMoreStuffImpl>(callCount), loop);

  kj::Promise<void> promise = nullptr;

  bool returned = false;
  {
    auto request = client.expectAsyncCancelRequest();
    request.setCap(test::TestInterface::Client(
        kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller)), loop));
    promise = loop.there(request.send(),
        [&](Response<test::TestMoreStuff::ExpectAsyncCancelResults>&& response) {
      returned = true;
    });
    promise.eagerlyEvaluate(loop);
  }
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));

  // We can detect that the method was canceled because it will drop the cap.
  EXPECT_FALSE(destroyed);
  EXPECT_FALSE(returned);

  promise = nullptr;  // request cancellation
  loop.wait(kj::mv(destructionPromise));

  EXPECT_TRUE(destroyed);
  EXPECT_FALSE(returned);
}

TEST(Capability, SyncCancelation) {
  // Tests isCanceled() without allowAsyncCancellation().

  kj::SimpleEventLoop loop;

  int callCount = 0;
  int innerCallCount = 0;

  test::TestMoreStuff::Client client(kj::heap<TestMoreStuffImpl>(callCount), loop);

  kj::Promise<void> promise = nullptr;

  bool returned = false;
  {
    auto request = client.expectSyncCancelRequest();
    request.setCap(test::TestInterface::Client(
        kj::heap<TestInterfaceImpl>(innerCallCount), loop));
    promise = loop.there(request.send(),
        [&](Response<test::TestMoreStuff::ExpectSyncCancelResults>&& response) {
      returned = true;
    });
    promise.eagerlyEvaluate(loop);
  }
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));

  // expectSyncCancel() will make a call to the TestInterfaceImpl only once it noticed isCanceled()
  // is true.
  EXPECT_EQ(0, innerCallCount);
  EXPECT_FALSE(returned);

  promise = nullptr;  // request cancellation
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));

  EXPECT_EQ(1, innerCallCount);
  EXPECT_FALSE(returned);
}

// =======================================================================================

TEST(Capability, DynamicClient) {
  kj::SimpleEventLoop loop;

  int callCount = 0;
  DynamicCapability::Client client =
      test::TestInterface::Client(kj::heap<TestInterfaceImpl>(callCount), loop);

  auto request1 = client.newRequest("foo");
  request1.set("i", 123);
  request1.set("j", true);
  auto promise1 = request1.send();

  auto request2 = client.newRequest("baz");
  initDynamicTestMessage(request2.init("s").as<DynamicStruct>());
  auto promise2 = request2.send();

  bool barFailed = false;
  auto request3 = client.newRequest("bar");
  auto promise3 = loop.there(request3.send(),
      [](Response<DynamicStruct>&& response) {
        ADD_FAILURE() << "Expected bar() call to fail.";
      }, [&](kj::Exception&& e) {
        barFailed = true;
      });

  EXPECT_EQ(0, callCount);

  auto response1 = loop.wait(kj::mv(promise1));

  EXPECT_EQ("foo", response1.get("x").as<Text>());

  auto response2 = loop.wait(kj::mv(promise2));

  loop.wait(kj::mv(promise3));

  EXPECT_EQ(2, callCount);
  EXPECT_TRUE(barFailed);
}

TEST(Capability, DynamicClientInheritance) {
  kj::SimpleEventLoop loop;

  int callCount = 0;

  DynamicCapability::Client client1 =
      test::TestExtends::Client(kj::heap<TestExtendsImpl>(callCount), loop);
  EXPECT_EQ(Schema::from<test::TestExtends>(), client1.getSchema());
  EXPECT_NE(Schema::from<test::TestInterface>(), client1.getSchema());

  DynamicCapability::Client client2 = client1.upcast(Schema::from<test::TestInterface>());
  EXPECT_EQ(Schema::from<test::TestInterface>(), client2.getSchema());

  EXPECT_ANY_THROW(client2.upcast(Schema::from<test::TestExtends>()));
  auto client = client2.castAs<DynamicCapability>(Schema::from<test::TestExtends>());

  auto request1 = client.newRequest("foo");
  request1.set("i", 321);
  auto promise1 = request1.send();

  auto request2 = client.newRequest("grault");
  auto promise2 = request2.send();

  EXPECT_EQ(0, callCount);

  auto response2 = loop.wait(kj::mv(promise2));

  checkDynamicTestMessage(response2.as<DynamicStruct>());

  auto response1 = loop.wait(kj::mv(promise1));

  EXPECT_EQ("bar", response1.get("x").as<Text>());

  EXPECT_EQ(2, callCount);
}

TEST(Capability, DynamicClientPipelining) {
  kj::SimpleEventLoop loop;

  int callCount = 0;
  int chainedCallCount = 0;
  DynamicCapability::Client client =
      test::TestPipeline::Client(kj::heap<TestPipelineImpl>(callCount), loop);

  auto request = client.newRequest("getCap");
  request.set("n", 234);
  request.set("inCap", test::TestInterface::Client(
      kj::heap<TestInterfaceImpl>(chainedCallCount), loop));

  auto promise = request.send();

  auto outCap = promise.get("outBox").releaseAs<DynamicStruct>()
                       .get("cap").releaseAs<DynamicCapability>();
  auto pipelineRequest = outCap.newRequest("foo");
  pipelineRequest.set("i", 321);
  auto pipelinePromise = pipelineRequest.send();

  auto pipelineRequest2 = outCap.castAs<test::TestExtends>().graultRequest();
  auto pipelinePromise2 = pipelineRequest2.send();

  promise = nullptr;  // Just to be annoying, drop the original promise.

  EXPECT_EQ(0, callCount);
  EXPECT_EQ(0, chainedCallCount);

  auto response = loop.wait(kj::mv(pipelinePromise));
  EXPECT_EQ("bar", response.get("x").as<Text>());

  auto response2 = loop.wait(kj::mv(pipelinePromise2));
  checkTestMessage(response2);

  EXPECT_EQ(3, callCount);
  EXPECT_EQ(1, chainedCallCount);
}

// =======================================================================================

class TestInterfaceDynamicImpl final: public DynamicCapability::Server {
public:
  TestInterfaceDynamicImpl(int& callCount)
      : DynamicCapability::Server(Schema::from<test::TestInterface>()),
        callCount(callCount) {}

  int& callCount;

  kj::Promise<void> call(InterfaceSchema::Method method,
                         CallContext<DynamicStruct, DynamicStruct> context) {
    auto methodName = method.getProto().getName();
    if (methodName == "foo") {
      ++callCount;
      auto params = context.getParams();
      EXPECT_EQ(123, params.get("i").as<int>());
      EXPECT_TRUE(params.get("j").as<bool>());
      context.getResults().set("x", "foo");
      return kj::READY_NOW;
    } else if (methodName == "baz") {
      ++callCount;
      auto params = context.getParams();
      checkDynamicTestMessage(params.get("s").as<DynamicStruct>());
      context.releaseParams();
      EXPECT_ANY_THROW(context.getParams());
      return kj::READY_NOW;
    } else {
      KJ_FAIL_ASSERT("Method not implemented", methodName);
    }
  }
};

TEST(Capability, DynamicServer) {
  kj::SimpleEventLoop loop;

  int callCount = 0;
  test::TestInterface::Client client =
      DynamicCapability::Client(kj::heap<TestInterfaceDynamicImpl>(callCount), loop)
          .castAs<test::TestInterface>();

  auto request1 = client.fooRequest();
  request1.setI(123);
  request1.setJ(true);
  auto promise1 = request1.send();

  auto request2 = client.bazRequest();
  initTestMessage(request2.initS());
  auto promise2 = request2.send();

  bool barFailed = false;
  auto request3 = client.barRequest();
  auto promise3 = loop.there(request3.send(),
      [](Response<test::TestInterface::BarResults>&& response) {
        ADD_FAILURE() << "Expected bar() call to fail.";
      }, [&](kj::Exception&& e) {
        barFailed = true;
      });

  EXPECT_EQ(0, callCount);

  auto response1 = loop.wait(kj::mv(promise1));

  EXPECT_EQ("foo", response1.getX());

  auto response2 = loop.wait(kj::mv(promise2));

  loop.wait(kj::mv(promise3));

  EXPECT_EQ(2, callCount);
  EXPECT_TRUE(barFailed);
}

class TestExtendsDynamicImpl final: public DynamicCapability::Server {
public:
  TestExtendsDynamicImpl(int& callCount)
      : DynamicCapability::Server(Schema::from<test::TestExtends>()),
        callCount(callCount) {}

  int& callCount;

  kj::Promise<void> call(InterfaceSchema::Method method,
                         CallContext<DynamicStruct, DynamicStruct> context) {
    auto methodName = method.getProto().getName();
    if (methodName == "foo") {
      ++callCount;
      auto params = context.getParams();
      EXPECT_EQ(321, params.get("i").as<int>());
      EXPECT_FALSE(params.get("j").as<bool>());
      context.getResults().set("x", "bar");
      return kj::READY_NOW;
    } else if (methodName == "grault") {
      ++callCount;
      context.releaseParams();
      initDynamicTestMessage(context.getResults());
      return kj::READY_NOW;
    } else {
      KJ_FAIL_ASSERT("Method not implemented", methodName);
    }
  }
};

TEST(Capability, DynamicServerInheritance) {
  kj::SimpleEventLoop loop;

  int callCount = 0;
  test::TestExtends::Client client1 =
      DynamicCapability::Client(kj::heap<TestExtendsDynamicImpl>(callCount), loop)
          .castAs<test::TestExtends>();
  test::TestInterface::Client client2 = client1;
  auto client = client2.castAs<test::TestExtends>();

  auto request1 = client.fooRequest();
  request1.setI(321);
  auto promise1 = request1.send();

  auto request2 = client.graultRequest();
  auto promise2 = request2.send();

  EXPECT_EQ(0, callCount);

  auto response2 = loop.wait(kj::mv(promise2));

  checkTestMessage(response2);

  auto response1 = loop.wait(kj::mv(promise1));

  EXPECT_EQ("bar", response1.getX());

  EXPECT_EQ(2, callCount);
}

class TestPipelineDynamicImpl final: public DynamicCapability::Server {
public:
  TestPipelineDynamicImpl(int& callCount)
      : DynamicCapability::Server(Schema::from<test::TestExtends>()),
        callCount(callCount) {}

  int& callCount;

  kj::Promise<void> call(InterfaceSchema::Method method,
                         CallContext<DynamicStruct, DynamicStruct> context) {
    auto methodName = method.getProto().getName();
    if (methodName == "getCap") {
      ++callCount;

      auto params = context.getParams();
      EXPECT_EQ(234, params.get("n").as<uint32_t>());

      auto cap = params.get("inCap").as<DynamicCapability>();
      context.releaseParams();

      auto request = cap.newRequest("foo");
      request.set("i", 123);
      request.set("j", true);

      return request.send().then(
          [this,context](capnp::Response<DynamicStruct>&& response) mutable {
            EXPECT_EQ("foo", response.get("x").as<Text>());

            auto result = context.getResults();
            result.set("s", "bar");

            auto box = result.init("outBox").as<DynamicStruct>();

            // Too lazy to write a whole separate test for each of these cases...  so just make
            // sure they both compile here, and only actually test the latter.
            box.set("outCap", kj::heap<TestExtendsDynamicImpl>(callCount));
            box.set("outCap", kj::heap<TestExtendsImpl>(callCount));
          });
    } else {
      KJ_FAIL_ASSERT("Method not implemented", methodName);
    }
  }
};

TEST(Capability, DynamicServerPipelining) {
  kj::SimpleEventLoop loop;

  int callCount = 0;
  int chainedCallCount = 0;
  test::TestPipeline::Client client(kj::heap<TestPipelineImpl>(callCount), loop);

  auto request = client.getCapRequest();
  request.setN(234);
  request.setInCap(test::TestInterface::Client(
      kj::heap<TestInterfaceImpl>(chainedCallCount), loop));

  auto promise = request.send();

  auto pipelineRequest = promise.getOutBox().getCap().fooRequest();
  pipelineRequest.setI(321);
  auto pipelinePromise = pipelineRequest.send();

  auto pipelineRequest2 = promise.getOutBox().getCap().castAs<test::TestExtends>().graultRequest();
  auto pipelinePromise2 = pipelineRequest2.send();

  promise = nullptr;  // Just to be annoying, drop the original promise.

  EXPECT_EQ(0, callCount);
  EXPECT_EQ(0, chainedCallCount);

  auto response = loop.wait(kj::mv(pipelinePromise));
  EXPECT_EQ("bar", response.getX());

  auto response2 = loop.wait(kj::mv(pipelinePromise2));
  checkTestMessage(response2);

  EXPECT_EQ(3, callCount);
  EXPECT_EQ(1, chainedCallCount);
}

// =======================================================================================

void verifyClient(const test::TestInterface::Client& client, kj::SimpleEventLoop& loop,
                  const int& callCount) {
  int origCount = callCount;
  auto request = client.fooRequest();
  request.setI(123);
  request.setJ(true);
  auto response = loop.wait(request.send());
  EXPECT_EQ("foo", response.getX());
  EXPECT_EQ(origCount + 1, callCount);
}

void verifyClient(const DynamicCapability::Client& client, kj::SimpleEventLoop& loop,
                  const int& callCount) {
  int origCount = callCount;
  auto request = client.newRequest("foo");
  request.set("i", 123);
  request.set("j", true);
  auto response = loop.wait(request.send());
  EXPECT_EQ("foo", response.get("x").as<Text>());
  EXPECT_EQ(origCount + 1, callCount);
}

TEST(Capability, ObjectsAndOrphans) {
  kj::SimpleEventLoop loop;

  int callCount1 = 0;
  int callCount2 = 0;

  // We use a TestPipeline instance here merely as a way to conveniently obtain an imbued message
  // instance.
  test::TestPipeline::Client baseClient(nullptr);
  test::TestInterface::Client client1(kj::heap<TestInterfaceImpl>(callCount1), loop);
  test::TestInterface::Client client2(kj::heap<TestInterfaceImpl>(callCount2), loop);

  auto request = baseClient.testPointersRequest();
  request.setCap(client1);

  EXPECT_TRUE(request.hasCap());

  Orphan<test::TestInterface> orphan = request.disownCap();
  EXPECT_FALSE(orphan == nullptr);

  EXPECT_FALSE(request.hasCap());

  verifyClient(orphan.get(), loop, callCount1);
  verifyClient(orphan.getReader(), loop, callCount1);

  request.getObj().adopt(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);

  verifyClient(request.getObj().getAs<test::TestInterface>(), loop, callCount1);
  verifyClient(request.asReader().getObj().getAs<test::TestInterface>(), loop, callCount1);
  verifyClient(request.getObj().getAs<DynamicCapability>(
      Schema::from<test::TestInterface>()), loop, callCount1);
  verifyClient(request.asReader().getObj().getAs<DynamicCapability>(
      Schema::from<test::TestInterface>()), loop, callCount1);

  request.getObj().clear();
  EXPECT_FALSE(request.hasObj());

  request.getObj().setAs<test::TestInterface>(client2);
  verifyClient(request.getObj().getAs<test::TestInterface>(), loop, callCount2);

  Orphan<DynamicCapability> dynamicOrphan = request.getObj().disownAs<DynamicCapability>(
      Schema::from<test::TestInterface>());
  verifyClient(dynamicOrphan.get(), loop, callCount2);
  verifyClient(dynamicOrphan.getReader(), loop, callCount2);

  Orphan<DynamicValue> dynamicValueOrphan = kj::mv(dynamicOrphan);
  verifyClient(dynamicValueOrphan.get().as<DynamicCapability>(), loop, callCount2);

  orphan = dynamicValueOrphan.releaseAs<test::TestInterface>();
  EXPECT_FALSE(orphan == nullptr);
  verifyClient(orphan.get(), loop, callCount2);

  request.adoptCap(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);

  verifyClient(request.getCap(), loop, callCount2);

  Orphan<DynamicCapability> dynamicOrphan2 = request.disownCap();
  verifyClient(dynamicOrphan2.get(), loop, callCount2);
  verifyClient(dynamicOrphan2.getReader(), loop, callCount2);
}

TEST(Capability, Lists) {
  kj::SimpleEventLoop loop;

  int callCount1 = 0;
  int callCount2 = 0;
  int callCount3 = 0;
  test::TestPipeline::Client baseClient(kj::heap<TestPipelineImpl>(callCount1), loop);
  test::TestInterface::Client client1(kj::heap<TestInterfaceImpl>(callCount1), loop);
  test::TestInterface::Client client2(kj::heap<TestInterfaceImpl>(callCount2), loop);
  test::TestInterface::Client client3(kj::heap<TestInterfaceImpl>(callCount3), loop);

  auto request = baseClient.testPointersRequest();

  auto list = request.initList(3);
  list.set(0, client1);
  list.set(1, client2);
  list.set(2, client3);

  verifyClient(list[0], loop, callCount1);
  verifyClient(list[1], loop, callCount2);
  verifyClient(list[2], loop, callCount3);

  auto listReader = request.asReader().getList();
  verifyClient(listReader[0], loop, callCount1);
  verifyClient(listReader[1], loop, callCount2);
  verifyClient(listReader[2], loop, callCount3);

  auto dynamicList = toDynamic(list);
  verifyClient(dynamicList[0].as<DynamicCapability>(), loop, callCount1);
  verifyClient(dynamicList[1].as<DynamicCapability>(), loop, callCount2);
  verifyClient(dynamicList[2].as<DynamicCapability>(), loop, callCount3);

  auto dynamicListReader = toDynamic(listReader);
  verifyClient(dynamicListReader[0].as<DynamicCapability>(), loop, callCount1);
  verifyClient(dynamicListReader[1].as<DynamicCapability>(), loop, callCount2);
  verifyClient(dynamicListReader[2].as<DynamicCapability>(), loop, callCount3);
}

}  // namespace
}  // namespace _
}  // namespace capnp
