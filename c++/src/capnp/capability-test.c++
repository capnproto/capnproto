// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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

#include "schema.capnp.h"

#ifdef CAPNP_CAPABILITY_H_INCLUDED
#error "schema.capnp should not depend on capability.h, because it contains no interfaces."
#endif

#include <capnp/test.capnp.h>

#ifndef CAPNP_CAPABILITY_H_INCLUDED
#error "test.capnp did not include capability.h."
#endif

#include "capability.h"
#include "test-util.h"
#include <kj/debug.h>
#include <kj/compat/gtest.h>

namespace capnp {
namespace _ {
namespace {

TEST(Capability, Basic) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0;
  test::TestInterface::Client client(kj::heap<TestInterfaceImpl>(callCount));

  auto request1 = client.fooRequest();
  request1.setI(123);
  request1.setJ(true);
  auto promise1 = request1.send();

  auto request2 = client.bazRequest();
  initTestMessage(request2.initS());
  auto promise2 = request2.send();

  bool barFailed = false;
  auto request3 = client.barRequest();
  auto promise3 = request3.send().then(
      [](Response<test::TestInterface::BarResults>&& response) {
        ADD_FAILURE() << "Expected bar() call to fail.";
      }, [&](kj::Exception&& e) {
        EXPECT_EQ(kj::Exception::Type::UNIMPLEMENTED, e.getType());
        barFailed = true;
      });

  EXPECT_EQ(0, callCount);

  auto response1 = promise1.wait(waitScope);

  EXPECT_EQ("foo", response1.getX());

  auto response2 = promise2.wait(waitScope);

  promise3.wait(waitScope);

  EXPECT_EQ(2, callCount);
  EXPECT_TRUE(barFailed);
}

TEST(Capability, Inheritance) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0;
  test::TestExtends::Client client1(kj::heap<TestExtendsImpl>(callCount));
  test::TestInterface::Client client2 = client1;
  auto client = client2.castAs<test::TestExtends>();

  auto request1 = client.fooRequest();
  request1.setI(321);
  auto promise1 = request1.send();

  auto request2 = client.graultRequest();
  auto promise2 = request2.send();

  EXPECT_EQ(0, callCount);

  auto response2 = promise2.wait(waitScope);

  checkTestMessage(response2);

  auto response1 = promise1.wait(waitScope);

  EXPECT_EQ("bar", response1.getX());

  EXPECT_EQ(2, callCount);
}

TEST(Capability, Pipelining) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0;
  int chainedCallCount = 0;
  test::TestPipeline::Client client(kj::heap<TestPipelineImpl>(callCount));

  auto request = client.getCapRequest();
  request.setN(234);
  request.setInCap(test::TestInterface::Client(kj::heap<TestInterfaceImpl>(chainedCallCount)));

  auto promise = request.send();

  auto pipelineRequest = promise.getOutBox().getCap().fooRequest();
  pipelineRequest.setI(321);
  auto pipelinePromise = pipelineRequest.send();

  auto pipelineRequest2 = promise.getOutBox().getCap().castAs<test::TestExtends>().graultRequest();
  auto pipelinePromise2 = pipelineRequest2.send();

  promise = nullptr;  // Just to be annoying, drop the original promise.

  EXPECT_EQ(0, callCount);
  EXPECT_EQ(0, chainedCallCount);

  auto response = pipelinePromise.wait(waitScope);
  EXPECT_EQ("bar", response.getX());

  auto response2 = pipelinePromise2.wait(waitScope);
  checkTestMessage(response2);

  EXPECT_EQ(3, callCount);
  EXPECT_EQ(1, chainedCallCount);
}

TEST(Capability, TailCall) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int calleeCallCount = 0;
  int callerCallCount = 0;

  test::TestTailCallee::Client callee(kj::heap<TestTailCalleeImpl>(calleeCallCount));
  test::TestTailCaller::Client caller(kj::heap<TestTailCallerImpl>(callerCallCount));

  auto request = caller.fooRequest();
  request.setI(456);
  request.setCallee(callee);

  auto promise = request.send();

  auto dependentCall0 = promise.getC().getCallSequenceRequest().send();

  auto response = promise.wait(waitScope);
  EXPECT_EQ(456, response.getI());
  EXPECT_EQ(456, response.getI());

  auto dependentCall1 = promise.getC().getCallSequenceRequest().send();

  auto dependentCall2 = response.getC().getCallSequenceRequest().send();

  EXPECT_EQ(0, dependentCall0.wait(waitScope).getN());
  EXPECT_EQ(1, dependentCall1.wait(waitScope).getN());
  EXPECT_EQ(2, dependentCall2.wait(waitScope).getN());

  EXPECT_EQ(1, calleeCallCount);
  EXPECT_EQ(1, callerCallCount);
}

TEST(Capability, AsyncCancelation) {
  // Tests allowCancellation().

  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = paf.promise.then([&]() { destroyed = true; }).eagerlyEvaluate(nullptr);

  int callCount = 0;
  int handleCount = 0;

  test::TestMoreStuff::Client client(kj::heap<TestMoreStuffImpl>(callCount, handleCount));

  kj::Promise<void> promise = nullptr;

  bool returned = false;
  {
    auto request = client.expectCancelRequest();
    request.setCap(test::TestInterface::Client(kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller))));
    promise = request.send().then(
        [&](Response<test::TestMoreStuff::ExpectCancelResults>&& response) {
      returned = true;
    }).eagerlyEvaluate(nullptr);
  }
  kj::evalLater([]() {}).wait(waitScope);
  kj::evalLater([]() {}).wait(waitScope);

  // We can detect that the method was canceled because it will drop the cap.
  EXPECT_FALSE(destroyed);
  EXPECT_FALSE(returned);

  promise = nullptr;  // request cancellation
  destructionPromise.wait(waitScope);

  EXPECT_TRUE(destroyed);
  EXPECT_FALSE(returned);
}

// =======================================================================================

TEST(Capability, DynamicClient) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0;
  DynamicCapability::Client client =
      test::TestInterface::Client(kj::heap<TestInterfaceImpl>(callCount));

  auto request1 = client.newRequest("foo");
  request1.set("i", 123);
  request1.set("j", true);
  auto promise1 = request1.send();

  auto request2 = client.newRequest("baz");
  initDynamicTestMessage(request2.init("s").as<DynamicStruct>());
  auto promise2 = request2.send();

  bool barFailed = false;
  auto request3 = client.newRequest("bar");
  auto promise3 = request3.send().then(
      [](Response<DynamicStruct>&& response) {
        ADD_FAILURE() << "Expected bar() call to fail.";
      }, [&](kj::Exception&& e) {
        EXPECT_EQ(kj::Exception::Type::UNIMPLEMENTED, e.getType());
        barFailed = true;
      });

  EXPECT_EQ(0, callCount);

  auto response1 = promise1.wait(waitScope);

  EXPECT_EQ("foo", response1.get("x").as<Text>());

  auto response2 = promise2.wait(waitScope);

  promise3.wait(waitScope);

  EXPECT_EQ(2, callCount);
  EXPECT_TRUE(barFailed);
}

TEST(Capability, DynamicClientInheritance) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0;

  DynamicCapability::Client client1 =
      test::TestExtends::Client(kj::heap<TestExtendsImpl>(callCount));
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

  auto response2 = promise2.wait(waitScope);

  checkDynamicTestMessage(response2.as<DynamicStruct>());

  auto response1 = promise1.wait(waitScope);

  EXPECT_EQ("bar", response1.get("x").as<Text>());

  EXPECT_EQ(2, callCount);
}

TEST(Capability, DynamicClientPipelining) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0;
  int chainedCallCount = 0;
  DynamicCapability::Client client =
      test::TestPipeline::Client(kj::heap<TestPipelineImpl>(callCount));

  auto request = client.newRequest("getCap");
  request.set("n", 234);
  request.set("inCap", test::TestInterface::Client(kj::heap<TestInterfaceImpl>(chainedCallCount)));

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

  auto response = pipelinePromise.wait(waitScope);
  EXPECT_EQ("bar", response.get("x").as<Text>());

  auto response2 = pipelinePromise2.wait(waitScope);
  checkTestMessage(response2);

  EXPECT_EQ(3, callCount);
  EXPECT_EQ(1, chainedCallCount);
}

TEST(Capability, DynamicClientPipelineAnyCap) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0;
  int chainedCallCount = 0;
  DynamicCapability::Client client =
      test::TestPipeline::Client(kj::heap<TestPipelineImpl>(callCount));

  auto request = client.newRequest("getAnyCap");
  request.set("n", 234);
  request.set("inCap", test::TestInterface::Client(kj::heap<TestInterfaceImpl>(chainedCallCount)));

  auto promise = request.send();

  auto outAnyCap = promise.get("outBox").releaseAs<DynamicStruct>()
                          .get("cap").releaseAs<DynamicCapability>();

  EXPECT_EQ(Schema::from<Capability>(), outAnyCap.getSchema());
  auto outCap = outAnyCap.castAs<DynamicCapability>(Schema::from<test::TestInterface>());

  auto pipelineRequest = outCap.newRequest("foo");
  pipelineRequest.set("i", 321);
  auto pipelinePromise = pipelineRequest.send();

  auto pipelineRequest2 = outCap.castAs<test::TestExtends>().graultRequest();
  auto pipelinePromise2 = pipelineRequest2.send();

  promise = nullptr;  // Just to be annoying, drop the original promise.

  EXPECT_EQ(0, callCount);
  EXPECT_EQ(0, chainedCallCount);

  auto response = pipelinePromise.wait(waitScope);
  EXPECT_EQ("bar", response.get("x").as<Text>());

  auto response2 = pipelinePromise2.wait(waitScope);
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
      KJ_UNIMPLEMENTED("Method not implemented", methodName) { break; }
      return kj::READY_NOW;
    }
  }
};

TEST(Capability, DynamicServer) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0;
  test::TestInterface::Client client =
      DynamicCapability::Client(kj::heap<TestInterfaceDynamicImpl>(callCount))
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
  auto promise3 = request3.send().then(
      [](Response<test::TestInterface::BarResults>&& response) {
        ADD_FAILURE() << "Expected bar() call to fail.";
      }, [&](kj::Exception&& e) {
        EXPECT_EQ(kj::Exception::Type::UNIMPLEMENTED, e.getType());
        barFailed = true;
      });

  EXPECT_EQ(0, callCount);

  auto response1 = promise1.wait(waitScope);

  EXPECT_EQ("foo", response1.getX());

  auto response2 = promise2.wait(waitScope);

  promise3.wait(waitScope);

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
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0;
  test::TestExtends::Client client1 =
      DynamicCapability::Client(kj::heap<TestExtendsDynamicImpl>(callCount))
          .castAs<test::TestExtends>();
  test::TestInterface::Client client2 = client1;
  auto client = client2.castAs<test::TestExtends>();

  auto request1 = client.fooRequest();
  request1.setI(321);
  auto promise1 = request1.send();

  auto request2 = client.graultRequest();
  auto promise2 = request2.send();

  EXPECT_EQ(0, callCount);

  auto response2 = promise2.wait(waitScope);

  checkTestMessage(response2);

  auto response1 = promise1.wait(waitScope);

  EXPECT_EQ("bar", response1.getX());

  EXPECT_EQ(2, callCount);
}

class TestPipelineDynamicImpl final: public DynamicCapability::Server {
public:
  TestPipelineDynamicImpl(int& callCount)
      : DynamicCapability::Server(Schema::from<test::TestPipeline>()),
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
          [this,KJ_CPCAP(context)](capnp::Response<DynamicStruct>&& response) mutable {
            EXPECT_EQ("foo", response.get("x").as<Text>());

            auto result = context.getResults();
            result.set("s", "bar");

            auto box = result.init("outBox").as<DynamicStruct>();

            // Too lazy to write a whole separate test for each of these cases...  so just make
            // sure they both compile here, and only actually test the latter.
            box.set("cap", kj::heap<TestExtendsDynamicImpl>(callCount));
#if __GNUG__ && !__clang__ && __GNUG__ == 4 && __GNUC_MINOR__ == 9
            // The last line in this block tickles a bug in Debian G++ 4.9.2 that is not present
            // in 4.8.x nor in 4.9.4:
            //     https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=781060
            //
            // Unfortunatley 4.9.2 is present on many Debian Jessie systems..
            //
            // For the moment, we can get away with skipping the last line as the previous line
            // will set things up in a way that allows the test to complete successfully.
            return;
#endif
            box.set("cap", kj::heap<TestExtendsImpl>(callCount));
          });
    } else {
      KJ_FAIL_ASSERT("Method not implemented", methodName);
    }
  }
};

TEST(Capability, DynamicServerPipelining) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0;
  int chainedCallCount = 0;
  test::TestPipeline::Client client =
      DynamicCapability::Client(kj::heap<TestPipelineDynamicImpl>(callCount))
          .castAs<test::TestPipeline>();

  auto request = client.getCapRequest();
  request.setN(234);
  request.setInCap(test::TestInterface::Client(kj::heap<TestInterfaceImpl>(chainedCallCount)));

  auto promise = request.send();

  auto pipelineRequest = promise.getOutBox().getCap().fooRequest();
  pipelineRequest.setI(321);
  auto pipelinePromise = pipelineRequest.send();

  auto pipelineRequest2 = promise.getOutBox().getCap().castAs<test::TestExtends>().graultRequest();
  auto pipelinePromise2 = pipelineRequest2.send();

  promise = nullptr;  // Just to be annoying, drop the original promise.

  EXPECT_EQ(0, callCount);
  EXPECT_EQ(0, chainedCallCount);

  auto response = pipelinePromise.wait(waitScope);
  EXPECT_EQ("bar", response.getX());

  auto response2 = pipelinePromise2.wait(waitScope);
  checkTestMessage(response2);

  EXPECT_EQ(3, callCount);
  EXPECT_EQ(1, chainedCallCount);
}

class TestTailCallerDynamicImpl final: public DynamicCapability::Server {
public:
  TestTailCallerDynamicImpl(int& callCount)
      : DynamicCapability::Server(Schema::from<test::TestTailCaller>()),
        callCount(callCount) {}

  int& callCount;

  kj::Promise<void> call(InterfaceSchema::Method method,
                         CallContext<DynamicStruct, DynamicStruct> context) {
    auto methodName = method.getProto().getName();
    if (methodName == "foo") {
      ++callCount;

      auto params = context.getParams();
      auto tailRequest = params.get("callee").as<DynamicCapability>().newRequest("foo");
      tailRequest.set("i", params.get("i"));
      tailRequest.set("t", "from TestTailCaller");
      return context.tailCall(kj::mv(tailRequest));
    } else {
      KJ_FAIL_ASSERT("Method not implemented", methodName);
    }
  }
};

TEST(Capability, DynamicServerTailCall) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int calleeCallCount = 0;
  int callerCallCount = 0;

  test::TestTailCallee::Client callee(kj::heap<TestTailCalleeImpl>(calleeCallCount));
  test::TestTailCaller::Client caller =
      DynamicCapability::Client(kj::heap<TestTailCallerDynamicImpl>(callerCallCount))
          .castAs<test::TestTailCaller>();

  auto request = caller.fooRequest();
  request.setI(456);
  request.setCallee(callee);

  auto promise = request.send();

  auto dependentCall0 = promise.getC().getCallSequenceRequest().send();

  auto response = promise.wait(waitScope);
  EXPECT_EQ(456, response.getI());
  EXPECT_EQ(456, response.getI());

  auto dependentCall1 = promise.getC().getCallSequenceRequest().send();

  auto dependentCall2 = response.getC().getCallSequenceRequest().send();

  EXPECT_EQ(0, dependentCall0.wait(waitScope).getN());
  EXPECT_EQ(1, dependentCall1.wait(waitScope).getN());
  EXPECT_EQ(2, dependentCall2.wait(waitScope).getN());

  EXPECT_EQ(1, calleeCallCount);
  EXPECT_EQ(1, callerCallCount);
}

// =======================================================================================

void verifyClient(test::TestInterface::Client client, const int& callCount,
                  kj::WaitScope& waitScope) {
  int origCount = callCount;
  auto request = client.fooRequest();
  request.setI(123);
  request.setJ(true);
  auto response = request.send().wait(waitScope);
  EXPECT_EQ("foo", response.getX());
  EXPECT_EQ(origCount + 1, callCount);
}

void verifyClient(DynamicCapability::Client client, const int& callCount,
                  kj::WaitScope& waitScope) {
  int origCount = callCount;
  auto request = client.newRequest("foo");
  request.set("i", 123);
  request.set("j", true);
  auto response = request.send().wait(waitScope);
  EXPECT_EQ("foo", response.get("x").as<Text>());
  EXPECT_EQ(origCount + 1, callCount);
}

TEST(Capability, AnyPointersAndOrphans) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount1 = 0;
  int callCount2 = 0;

  // We use a TestPipeline instance here merely as a way to conveniently obtain an imbued message
  // instance.
  test::TestPipeline::Client baseClient(nullptr);
  test::TestInterface::Client client1(kj::heap<TestInterfaceImpl>(callCount1));
  test::TestInterface::Client client2(kj::heap<TestInterfaceImpl>(callCount2));

  auto request = baseClient.testPointersRequest();
  request.setCap(client1);

  EXPECT_TRUE(request.hasCap());

  Orphan<test::TestInterface> orphan = request.disownCap();
  EXPECT_FALSE(orphan == nullptr);

  EXPECT_FALSE(request.hasCap());

  verifyClient(orphan.get(), callCount1, waitScope);
  verifyClient(orphan.getReader(), callCount1, waitScope);

  request.getObj().adopt(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);

  verifyClient(request.getObj().getAs<test::TestInterface>(), callCount1, waitScope);
  verifyClient(request.asReader().getObj().getAs<test::TestInterface>(), callCount1, waitScope);
  verifyClient(request.getObj().getAs<DynamicCapability>(
      Schema::from<test::TestInterface>()), callCount1, waitScope);
  verifyClient(request.asReader().getObj().getAs<DynamicCapability>(
      Schema::from<test::TestInterface>()), callCount1, waitScope);

  request.getObj().clear();
  EXPECT_FALSE(request.hasObj());

  request.getObj().setAs<test::TestInterface>(client2);
  verifyClient(request.getObj().getAs<test::TestInterface>(), callCount2, waitScope);

  Orphan<DynamicCapability> dynamicOrphan = request.getObj().disownAs<DynamicCapability>(
      Schema::from<test::TestInterface>());
  verifyClient(dynamicOrphan.get(), callCount2, waitScope);
  verifyClient(dynamicOrphan.getReader(), callCount2, waitScope);

  Orphan<DynamicValue> dynamicValueOrphan = kj::mv(dynamicOrphan);
  verifyClient(dynamicValueOrphan.get().as<DynamicCapability>(), callCount2, waitScope);

  orphan = dynamicValueOrphan.releaseAs<test::TestInterface>();
  EXPECT_FALSE(orphan == nullptr);
  verifyClient(orphan.get(), callCount2, waitScope);

  request.adoptCap(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);

  verifyClient(request.getCap(), callCount2, waitScope);

  Orphan<DynamicCapability> dynamicOrphan2 = request.disownCap();
  verifyClient(dynamicOrphan2.get(), callCount2, waitScope);
  verifyClient(dynamicOrphan2.getReader(), callCount2, waitScope);
}

TEST(Capability, Lists) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount1 = 0;
  int callCount2 = 0;
  int callCount3 = 0;
  test::TestPipeline::Client baseClient(kj::heap<TestPipelineImpl>(callCount1));
  test::TestInterface::Client client1(kj::heap<TestInterfaceImpl>(callCount1));
  test::TestInterface::Client client2(kj::heap<TestInterfaceImpl>(callCount2));
  test::TestInterface::Client client3(kj::heap<TestInterfaceImpl>(callCount3));

  auto request = baseClient.testPointersRequest();

  auto list = request.initList(3);
  list.set(0, client1);
  list.set(1, client2);
  list.set(2, client3);

  verifyClient(list[0], callCount1, waitScope);
  verifyClient(list[1], callCount2, waitScope);
  verifyClient(list[2], callCount3, waitScope);

  auto listReader = request.asReader().getList();
  verifyClient(listReader[0], callCount1, waitScope);
  verifyClient(listReader[1], callCount2, waitScope);
  verifyClient(listReader[2], callCount3, waitScope);

  auto dynamicList = toDynamic(list);
  verifyClient(dynamicList[0].as<DynamicCapability>(), callCount1, waitScope);
  verifyClient(dynamicList[1].as<DynamicCapability>(), callCount2, waitScope);
  verifyClient(dynamicList[2].as<DynamicCapability>(), callCount3, waitScope);

  auto dynamicListReader = toDynamic(listReader);
  verifyClient(dynamicListReader[0].as<DynamicCapability>(), callCount1, waitScope);
  verifyClient(dynamicListReader[1].as<DynamicCapability>(), callCount2, waitScope);
  verifyClient(dynamicListReader[2].as<DynamicCapability>(), callCount3, waitScope);
}

TEST(Capability, KeywordMethods) {
  // Verify that keywords are only munged where necessary.

  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  bool called = false;

  class TestKeywordMethodsImpl final: public test::TestKeywordMethods::Server {
  public:
    TestKeywordMethodsImpl(bool& called): called(called) {}

    kj::Promise<void> delete_(DeleteContext context) override {
      called = true;
      return kj::READY_NOW;
    }

  private:
    bool& called;
  };

  test::TestKeywordMethods::Client client = kj::heap<TestKeywordMethodsImpl>(called);
  client.deleteRequest().send().wait(waitScope);

  EXPECT_TRUE(called);
}

TEST(Capability, Generics) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  typedef test::TestGenerics<TestAllTypes>::Interface<List<uint32_t>> Interface;
  Interface::Client client = nullptr;

  auto request = client.callRequest();
  request.setBaz("hello");
  initTestMessage(request.initInnerBound().initFoo());
  initTestMessage(request.initInnerUnbound().getFoo().initAs<TestAllTypes>());

  auto promise = request.send().then([](capnp::Response<Interface::CallResults>&& response) {
    // This doesn't actually execute; we're just checking that it compiles.
    List<uint32_t>::Reader qux = response.getQux();
    qux.size();
    checkTestMessage(response.getGen().getFoo());
  }, [](kj::Exception&& e) {
    // Ignore exception (which we'll always get because we're calling a null capability).
  });

  promise.wait(waitScope);

  // Check that asGeneric<>() compiles.
  test::TestGenerics<TestAllTypes>::Interface<>::Client castClient = client.asGeneric<>();
  test::TestGenerics<TestAllTypes>::Interface<TestAllTypes>::Client castClient2 =
      client.asGeneric<TestAllTypes>();
  test::TestGenerics<>::Interface<List<uint32_t>>::Client castClient3 = client.asTestGenericsGeneric<>();
}

TEST(Capability, Generics2) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<test::TestUseGenerics>();

  root.initCap().setFoo(test::TestInterface::Client(nullptr));
}

TEST(Capability, ImplicitParams) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  typedef test::TestImplicitMethodParams Interface;
  Interface::Client client = nullptr;

  capnp::Request<Interface::CallParams<Text, TestAllTypes>,
                 test::TestGenerics<Text, TestAllTypes>> request =
      client.callRequest<Text, TestAllTypes>();
  request.setFoo("hello");
  initTestMessage(request.initBar());

  auto promise = request.send()
      .then([](capnp::Response<test::TestGenerics<Text, TestAllTypes>>&& response) {
    // This doesn't actually execute; we're just checking that it compiles.
    Text::Reader text = response.getFoo();
    text.size();
    checkTestMessage(response.getRev().getFoo());
  }, [](kj::Exception&& e) {});

  promise.wait(waitScope);
}

TEST(Capability, CapabilityServerSet) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  CapabilityServerSet<test::TestInterface> set1, set2;

  int callCount = 0;
  test::TestInterface::Client clientStandalone(kj::heap<TestInterfaceImpl>(callCount));
  test::TestInterface::Client clientNull = nullptr;

  auto ownServer1 = kj::heap<TestInterfaceImpl>(callCount);
  auto& server1 = *ownServer1;
  test::TestInterface::Client client1 = set1.add(kj::mv(ownServer1));

  auto ownServer2 = kj::heap<TestInterfaceImpl>(callCount);
  auto& server2 = *ownServer2;
  test::TestInterface::Client client2 = set2.add(kj::mv(ownServer2));

  // Getting the local server using the correct set works.
  EXPECT_EQ(&server1, &KJ_ASSERT_NONNULL(set1.getLocalServer(client1).wait(waitScope)));
  EXPECT_EQ(&server2, &KJ_ASSERT_NONNULL(set2.getLocalServer(client2).wait(waitScope)));

  // Getting the local server using the wrong set doesn't work.
  EXPECT_TRUE(set1.getLocalServer(client2).wait(waitScope) == nullptr);
  EXPECT_TRUE(set2.getLocalServer(client1).wait(waitScope) == nullptr);
  EXPECT_TRUE(set1.getLocalServer(clientStandalone).wait(waitScope) == nullptr);
  EXPECT_TRUE(set1.getLocalServer(clientNull).wait(waitScope) == nullptr);

  // A promise client waits to be resolved.
  auto paf = kj::newPromiseAndFulfiller<test::TestInterface::Client>();
  test::TestInterface::Client clientPromise = kj::mv(paf.promise);

  auto errorPaf = kj::newPromiseAndFulfiller<test::TestInterface::Client>();
  test::TestInterface::Client errorPromise = kj::mv(errorPaf.promise);

  bool resolved1 = false, resolved2 = false, resolved3 = false;
  auto promise1 = set1.getLocalServer(clientPromise)
      .then([&](kj::Maybe<test::TestInterface::Server&> server) {
    resolved1 = true;
    EXPECT_EQ(&server1, &KJ_ASSERT_NONNULL(server));
  });
  auto promise2 = set2.getLocalServer(clientPromise)
      .then([&](kj::Maybe<test::TestInterface::Server&> server) {
    resolved2 = true;
    EXPECT_TRUE(server == nullptr);
  });
  auto promise3 = set1.getLocalServer(errorPromise)
      .then([&](kj::Maybe<test::TestInterface::Server&> server) {
    KJ_FAIL_EXPECT("getLocalServer() on error promise should have thrown");
  }, [&](kj::Exception&& e) {
    resolved3 = true;
    KJ_EXPECT(e.getDescription().endsWith("foo"), e.getDescription());
  });

  kj::evalLater([](){}).wait(waitScope);
  kj::evalLater([](){}).wait(waitScope);
  kj::evalLater([](){}).wait(waitScope);
  kj::evalLater([](){}).wait(waitScope);

  EXPECT_FALSE(resolved1);
  EXPECT_FALSE(resolved2);
  EXPECT_FALSE(resolved3);

  paf.fulfiller->fulfill(kj::cp(client1));
  errorPaf.fulfiller->reject(KJ_EXCEPTION(FAILED, "foo"));

  promise1.wait(waitScope);
  promise2.wait(waitScope);
  promise3.wait(waitScope);

  EXPECT_TRUE(resolved1);
  EXPECT_TRUE(resolved2);
  EXPECT_TRUE(resolved3);
}

class TestThisCap final: public test::TestInterface::Server {
public:
  TestThisCap(int& callCount): callCount(callCount) {}
  ~TestThisCap() noexcept(false) { callCount = -1; }

  test::TestInterface::Client getSelf() {
    return thisCap();
  }

protected:
  kj::Promise<void> bar(BarContext context) {
    ++callCount;
    return kj::READY_NOW;
  }

private:
  int& callCount;
};

TEST(Capability, ThisCap) {
  int callCount = 0;
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto server = kj::heap<TestThisCap>(callCount);
  TestThisCap* serverPtr = server;

  test::TestInterface::Client client = kj::mv(server);
  client.barRequest().send().wait(waitScope);
  EXPECT_EQ(1, callCount);

  test::TestInterface::Client client2 = serverPtr->getSelf();
  EXPECT_EQ(1, callCount);
  client2.barRequest().send().wait(waitScope);
  EXPECT_EQ(2, callCount);

  client = nullptr;

  EXPECT_EQ(2, callCount);
  client2.barRequest().send().wait(waitScope);
  EXPECT_EQ(3, callCount);

  client2 = nullptr;

  EXPECT_EQ(-1, callCount);
}

TEST(Capability, TransferCap) {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  MallocMessageBuilder message;
  auto root = message.initRoot<test::TestTransferCap>();

  auto orphan = message.getOrphanage().newOrphan<test::TestTransferCap::Element>();
  auto e = orphan.get();
  e.setText("foo");
  e.setCap(KJ_EXCEPTION(FAILED, "whatever"));

  root.initList(1).adoptWithCaveats(0, kj::mv(orphan));

  // This line used to throw due to capability pointers being incorrectly transferred.
  auto cap = root.getList()[0].getCap();

  cap.whenResolved().then([]() {
    KJ_FAIL_EXPECT("didn't throw?");
  }, [](kj::Exception&&) {
    // success
  }).wait(waitScope);
}

KJ_TEST("Promise<RemotePromise<T>> automatically reduces to RemotePromise<T>") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0;
  test::TestInterface::Client client(kj::heap<TestInterfaceImpl>(callCount));

  RemotePromise<test::TestInterface::FooResults> promise = kj::evalLater([&]() {
    auto request = client.fooRequest();
    request.setI(123);
    request.setJ(true);
    return request.send();
  });

  EXPECT_EQ(0, callCount);
  auto response = promise.wait(waitScope);
  EXPECT_EQ("foo", response.getX());
  EXPECT_EQ(1, callCount);
}

KJ_TEST("Promise<RemotePromise<T>> automatically reduces to RemotePromise<T> with pipelining") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0;
  int chainedCallCount = 0;
  test::TestPipeline::Client client(kj::heap<TestPipelineImpl>(callCount));

  auto promise = kj::evalLater([&]() {
    auto request = client.getCapRequest();
    request.setN(234);
    request.setInCap(test::TestInterface::Client(kj::heap<TestInterfaceImpl>(chainedCallCount)));
    return request.send();
  });

  auto pipelineRequest = promise.getOutBox().getCap().fooRequest();
  pipelineRequest.setI(321);
  auto pipelinePromise = pipelineRequest.send();

  EXPECT_EQ(0, callCount);
  EXPECT_EQ(0, chainedCallCount);

  auto response = pipelinePromise.wait(waitScope);
  EXPECT_EQ("bar", response.getX());

  EXPECT_EQ(2, callCount);
  EXPECT_EQ(1, chainedCallCount);
}

KJ_TEST("clone() with caps") {
  int dummy = 0;
  MallocMessageBuilder builder(2048);
  auto root = builder.getRoot<AnyPointer>().initAs<List<test::TestInterface>>(3);
  root.set(0, kj::heap<TestInterfaceImpl>(dummy));
  root.set(1, kj::heap<TestInterfaceImpl>(dummy));
  root.set(2, kj::heap<TestInterfaceImpl>(dummy));

  auto copyPtr = clone(root.asReader());
  auto& copy = *copyPtr;

  KJ_ASSERT(copy.size() == 3);
  KJ_EXPECT(ClientHook::from(copy[0]).get() == ClientHook::from(root[0]).get());
  KJ_EXPECT(ClientHook::from(copy[1]).get() == ClientHook::from(root[1]).get());
  KJ_EXPECT(ClientHook::from(copy[2]).get() == ClientHook::from(root[2]).get());

  KJ_EXPECT(ClientHook::from(copy[0]).get() != ClientHook::from(root[1]).get());
  KJ_EXPECT(ClientHook::from(copy[1]).get() != ClientHook::from(root[2]).get());
  KJ_EXPECT(ClientHook::from(copy[2]).get() != ClientHook::from(root[0]).get());
}

KJ_TEST("Streaming calls block subsequent calls") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto ownServer = kj::heap<TestStreamingImpl>();
  auto& server = *ownServer;
  test::TestStreaming::Client cap = kj::mv(ownServer);

  kj::Promise<void> promise1 = nullptr, promise2 = nullptr, promise3 = nullptr;

  {
    auto req = cap.doStreamIRequest();
    req.setI(123);
    promise1 = req.send();
  }

  {
    auto req = cap.doStreamJRequest();
    req.setJ(321);
    promise2 = req.send();
  }

  {
    auto req = cap.doStreamIRequest();
    req.setI(456);
    promise3 = req.send();
  }

  auto promise4 = cap.finishStreamRequest().send();

  KJ_EXPECT(server.iSum == 0);
  KJ_EXPECT(server.jSum == 0);

  KJ_EXPECT(!promise1.poll(waitScope));
  KJ_EXPECT(!promise2.poll(waitScope));
  KJ_EXPECT(!promise3.poll(waitScope));
  KJ_EXPECT(!promise4.poll(waitScope));

  KJ_EXPECT(server.iSum == 123);
  KJ_EXPECT(server.jSum == 0);

  KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();

  KJ_EXPECT(promise1.poll(waitScope));
  KJ_EXPECT(!promise2.poll(waitScope));
  KJ_EXPECT(!promise3.poll(waitScope));
  KJ_EXPECT(!promise4.poll(waitScope));

  KJ_EXPECT(server.iSum == 123);
  KJ_EXPECT(server.jSum == 321);

  KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();

  KJ_EXPECT(promise1.poll(waitScope));
  KJ_EXPECT(promise2.poll(waitScope));
  KJ_EXPECT(!promise3.poll(waitScope));
  KJ_EXPECT(!promise4.poll(waitScope));

  KJ_EXPECT(server.iSum == 579);
  KJ_EXPECT(server.jSum == 321);

  KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();

  KJ_EXPECT(promise1.poll(waitScope));
  KJ_EXPECT(promise2.poll(waitScope));
  KJ_EXPECT(promise3.poll(waitScope));
  KJ_EXPECT(promise4.poll(waitScope));

  auto result = promise4.wait(waitScope);
  KJ_EXPECT(result.getTotalI() == 579);
  KJ_EXPECT(result.getTotalJ() == 321);
}

KJ_TEST("Streaming calls can be canceled") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto ownServer = kj::heap<TestStreamingImpl>();
  auto& server = *ownServer;
  test::TestStreaming::Client cap = kj::mv(ownServer);

  kj::Promise<void> promise1 = nullptr, promise2 = nullptr, promise3 = nullptr;

  {
    auto req = cap.doStreamIRequest();
    req.setI(123);
    promise1 = req.send();
  }

  {
    auto req = cap.doStreamJRequest();
    req.setJ(321);
    promise2 = req.send();
  }

  {
    auto req = cap.doStreamIRequest();
    req.setI(456);
    promise3 = req.send();
  }

  auto promise4 = cap.finishStreamRequest().send();

  // Cancel the streaming calls.
  promise1 = nullptr;
  promise2 = nullptr;
  promise3 = nullptr;

  KJ_EXPECT(server.iSum == 0);
  KJ_EXPECT(server.jSum == 0);

  KJ_EXPECT(!promise4.poll(waitScope));

  KJ_EXPECT(server.iSum == 123);
  KJ_EXPECT(server.jSum == 0);

  KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();

  KJ_EXPECT(!promise4.poll(waitScope));

  // The call to doStreamJ() opted into cancellation so the next call to doStreamI() happens
  // immediately.
  KJ_EXPECT(server.iSum == 579);
  KJ_EXPECT(server.jSum == 321);

  KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();

  KJ_EXPECT(promise4.poll(waitScope));

  auto result = promise4.wait(waitScope);
  KJ_EXPECT(result.getTotalI() == 579);
  KJ_EXPECT(result.getTotalJ() == 321);
}

KJ_TEST("Streaming call throwing cascades to following calls") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto ownServer = kj::heap<TestStreamingImpl>();
  auto& server = *ownServer;
  test::TestStreaming::Client cap = kj::mv(ownServer);

  server.jShouldThrow = true;

  kj::Promise<void> promise1 = nullptr, promise2 = nullptr, promise3 = nullptr;

  {
    auto req = cap.doStreamIRequest();
    req.setI(123);
    promise1 = req.send();
  }

  {
    auto req = cap.doStreamJRequest();
    req.setJ(321);
    promise2 = req.send();
  }

  {
    auto req = cap.doStreamIRequest();
    req.setI(456);
    promise3 = req.send();
  }

  auto promise4 = cap.finishStreamRequest().send();

  KJ_EXPECT(server.iSum == 0);
  KJ_EXPECT(server.jSum == 0);

  KJ_EXPECT(!promise1.poll(waitScope));
  KJ_EXPECT(!promise2.poll(waitScope));
  KJ_EXPECT(!promise3.poll(waitScope));
  KJ_EXPECT(!promise4.poll(waitScope));

  KJ_EXPECT(server.iSum == 123);
  KJ_EXPECT(server.jSum == 0);

  KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();

  KJ_EXPECT(promise1.poll(waitScope));
  KJ_EXPECT(promise2.poll(waitScope));
  KJ_EXPECT(promise3.poll(waitScope));
  KJ_EXPECT(promise4.poll(waitScope));

  KJ_EXPECT(server.iSum == 123);
  KJ_EXPECT(server.jSum == 321);

  KJ_EXPECT_THROW_MESSAGE("throw requested", promise2.wait(waitScope));
  KJ_EXPECT_THROW_MESSAGE("throw requested", promise3.wait(waitScope));
  KJ_EXPECT_THROW_MESSAGE("throw requested", promise4.wait(waitScope));
}

}  // namespace
}  // namespace _
}  // namespace capnp
