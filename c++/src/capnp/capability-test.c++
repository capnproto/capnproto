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

class TestInterfaceImpl final: public test::TestInterface::Server {
public:
  TestInterfaceImpl(int& callCount): callCount(callCount) {}

  int& callCount;

  ::kj::Promise<void> foo(
      test::TestInterface::FooParams::Reader params,
      test::TestInterface::FooResults::Builder result) override {
    ++callCount;
    EXPECT_EQ(123, params.getI());
    EXPECT_TRUE(params.getJ());
    result.setX("foo");
    return kj::READY_NOW;
  }

  ::kj::Promise<void> bazAdvanced(
      ::capnp::CallContext<test::TestInterface::BazParams,
                           test::TestInterface::BazResults> context) override {
    ++callCount;
    auto params = context.getParams();
    checkTestMessage(params.getS());
    context.releaseParams();
#if !KJ_NO_EXCEPTIONS
    EXPECT_ANY_THROW(context.getParams());
#endif

    return kj::READY_NOW;
  }
};

TEST(Capability, Basic) {
  kj::SimpleEventLoop loop;

  int callCount = 0;
  test::TestInterface::Client client(makeLocalClient(kj::heap<TestInterfaceImpl>(callCount), loop));

  auto request1 = client.fooRequest();
  request1.setI(123);
  request1.setJ(true);
  auto promise1 = request1.send();

  auto request2 = client.bazRequest();
  initTestMessage(request2.initS());
  auto promise2 = request2.send();

  auto request3 = client.barRequest();
  auto promise3 = loop.there(request3.send(),
      [](Response<test::TestInterface::BarResults>&& response) {
        ADD_FAILURE() << "Expected bar() call to fail.";
      }, [](kj::Exception&& e) {
        // success
      });

  EXPECT_EQ(0, callCount);

  auto response1 = loop.wait(kj::mv(promise1));

  EXPECT_EQ("foo", response1.getX());

  auto response2 = loop.wait(kj::mv(promise2));

  loop.wait(kj::mv(promise3));

  EXPECT_EQ(2, callCount);
}

class TestExtendsImpl final: public test::TestExtends::Server {
public:
  TestExtendsImpl(int& callCount): callCount(callCount) {}

  int& callCount;

  ::kj::Promise<void> foo(
      test::TestInterface::FooParams::Reader params,
      test::TestInterface::FooResults::Builder result) override {
    ++callCount;
    EXPECT_EQ(321, params.getI());
    EXPECT_FALSE(params.getJ());
    result.setX("bar");
    return kj::READY_NOW;
  }

  ::kj::Promise<void> graultAdvanced(
      ::capnp::CallContext<test::TestExtends::GraultParams, test::TestAllTypes> context) override {
    ++callCount;
    context.releaseParams();

    initTestMessage(context.getResults());

    return kj::READY_NOW;
  }
};

TEST(Capability, Inheritance) {
  kj::SimpleEventLoop loop;

  int callCount = 0;
  test::TestExtends::Client client(makeLocalClient(kj::heap<TestExtendsImpl>(callCount), loop));

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

class TestPipelineImpl final: public test::TestPipeline::Server {
public:
  TestPipelineImpl(int& callCount): callCount(callCount) {}

  int& callCount;

  ::kj::Promise<void> getCapAdvanced(
      capnp::CallContext<test::TestPipeline::GetCapParams,
                         test::TestPipeline::GetCapResults> context) override {
    ++callCount;

    auto params = context.getParams();
    EXPECT_EQ("foo", params.getS());

    auto cap = params.getInCap();
    context.releaseParams();

    auto request = cap.fooRequest();
    request.setI(123);
    request.setJ(true);

    return request.send().then(
        [this,context](capnp::Response<test::TestInterface::FooResults>&& response) mutable {
          EXPECT_EQ("foo", response.getX());

          auto result = context.getResults();
          result.setN(234);
          result.setOutCap(test::TestExtends::Client(
              makeLocalClient(kj::heap<TestExtendsImpl>(callCount))));
        });
  }
};

TEST(Capability, Pipelining) {
  kj::SimpleEventLoop loop;

  int callCount = 0;
  int chainedCallCount = 0;
  test::TestPipeline::Client client(makeLocalClient(kj::heap<TestPipelineImpl>(callCount), loop));

  auto request = client.getCapRequest();
  request.setS("foo");
  request.setInCap(test::TestInterface::Client(
      makeLocalClient(kj::heap<TestInterfaceImpl>(chainedCallCount), loop)));

  auto promise = request.send();

  auto pipelineRequest = promise.getOutCap().fooRequest();
  pipelineRequest.setI(321);
  auto pipelinePromise = pipelineRequest.send();

  EXPECT_EQ(0, callCount);
  EXPECT_EQ(0, chainedCallCount);

  auto response = loop.wait(kj::mv(pipelinePromise));
  EXPECT_EQ("bar", response.getX());

  EXPECT_EQ(2, callCount);
  EXPECT_EQ(1, chainedCallCount);
}

}  // namespace
}  // namespace _
}  // namespace capnp
