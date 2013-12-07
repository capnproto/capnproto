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

#ifndef CAPNP_TEST_UTIL_H_
#define CAPNP_TEST_UTIL_H_

#include <capnp/test.capnp.h>
#include <iostream>
#include "blob.h"
#include "dynamic.h"
#include <gtest/gtest.h>

#if KJ_NO_EXCEPTIONS
#undef EXPECT_ANY_THROW
#define EXPECT_ANY_THROW(code) EXPECT_DEATH(code, ".")
#endif

#define EXPECT_NONFATAL_FAILURE(code) \
  EXPECT_TRUE(kj::runCatchingExceptions([&]() { code; }) != nullptr);

#ifdef KJ_DEBUG
#define EXPECT_DEBUG_ANY_THROW EXPECT_ANY_THROW
#else
#define EXPECT_DEBUG_ANY_THROW(EXP)
#endif

namespace capnp {

inline std::ostream& operator<<(std::ostream& os, const Data::Reader& value) {
  return os.write(reinterpret_cast<const char*>(value.begin()), value.size());
}
inline std::ostream& operator<<(std::ostream& os, const Data::Builder& value) {
  return os.write(reinterpret_cast<const char*>(value.begin()), value.size());
}
inline std::ostream& operator<<(std::ostream& os, const Text::Reader& value) {
  return os.write(value.begin(), value.size());
}
inline std::ostream& operator<<(std::ostream& os, const Text::Builder& value) {
  return os.write(value.begin(), value.size());
}
inline std::ostream& operator<<(std::ostream& os, Void) {
  return os << "void";
}

namespace _ {  // private

inline Data::Reader data(const char* str) {
  return Data::Reader(reinterpret_cast<const byte*>(str), strlen(str));
}

namespace test = capnproto_test::capnp::test;

// We don't use "using namespace" to pull these in because then things would still compile
// correctly if they were generated in the global namespace.
using ::capnproto_test::capnp::test::TestAllTypes;
using ::capnproto_test::capnp::test::TestDefaults;
using ::capnproto_test::capnp::test::TestEnum;
using ::capnproto_test::capnp::test::TestUnion;
using ::capnproto_test::capnp::test::TestUnionDefaults;
using ::capnproto_test::capnp::test::TestNestedTypes;
using ::capnproto_test::capnp::test::TestUsing;
using ::capnproto_test::capnp::test::TestListDefaults;

void initTestMessage(TestAllTypes::Builder builder);
void initTestMessage(TestDefaults::Builder builder);
void initTestMessage(TestListDefaults::Builder builder);

void checkTestMessage(TestAllTypes::Builder builder);
void checkTestMessage(TestDefaults::Builder builder);
void checkTestMessage(TestListDefaults::Builder builder);

void checkTestMessage(TestAllTypes::Reader reader);
void checkTestMessage(TestDefaults::Reader reader);
void checkTestMessage(TestListDefaults::Reader reader);

void checkTestMessageAllZero(TestAllTypes::Builder builder);
void checkTestMessageAllZero(TestAllTypes::Reader reader);

void initDynamicTestMessage(DynamicStruct::Builder builder);
void initDynamicTestLists(DynamicStruct::Builder builder);
void checkDynamicTestMessage(DynamicStruct::Builder builder);
void checkDynamicTestLists(DynamicStruct::Builder builder);
void checkDynamicTestMessage(DynamicStruct::Reader reader);
void checkDynamicTestLists(DynamicStruct::Reader reader);
void checkDynamicTestMessageAllZero(DynamicStruct::Builder builder);
void checkDynamicTestMessageAllZero(DynamicStruct::Reader reader);

template <typename T, typename U>
void checkList(T reader, std::initializer_list<U> expected) {
  ASSERT_EQ(expected.size(), reader.size());
  for (uint i = 0; i < expected.size(); i++) {
    EXPECT_EQ(expected.begin()[i], reader[i]);
  }
}

template <typename T>
void checkList(T reader, std::initializer_list<float> expected) {
  ASSERT_EQ(expected.size(), reader.size());
  for (uint i = 0; i < expected.size(); i++) {
    EXPECT_FLOAT_EQ(expected.begin()[i], reader[i]);
  }
}

template <typename T>
void checkList(T reader, std::initializer_list<double> expected) {
  ASSERT_EQ(expected.size(), reader.size());
  for (uint i = 0; i < expected.size(); i++) {
    EXPECT_DOUBLE_EQ(expected.begin()[i], reader[i]);
  }
}

// Hack because as<>() is a template-parameter-dependent lookup everywhere below...
#define as template as

template <typename T> void expectPrimitiveEq(T a, T b) { EXPECT_EQ(a, b); }
inline void expectPrimitiveEq(float a, float b) { EXPECT_FLOAT_EQ(a, b); }
inline void expectPrimitiveEq(double a, double b) { EXPECT_DOUBLE_EQ(a, b); }
inline void expectPrimitiveEq(Text::Reader a, Text::Builder b) { EXPECT_EQ(a, b); }
inline void expectPrimitiveEq(Data::Reader a, Data::Builder b) { EXPECT_EQ(a, b); }

template <typename Element, typename T>
void checkList(T reader, std::initializer_list<ReaderFor<Element>> expected) {
  auto list = reader.as<DynamicList>();
  ASSERT_EQ(expected.size(), list.size());
  for (uint i = 0; i < expected.size(); i++) {
    expectPrimitiveEq(expected.begin()[i], list[i].as<Element>());
  }

  auto typed = reader.as<List<Element>>();
  ASSERT_EQ(expected.size(), typed.size());
  for (uint i = 0; i < expected.size(); i++) {
    expectPrimitiveEq(expected.begin()[i], typed[i]);
  }
}

#undef as

// =======================================================================================
// Interface implementations.

class TestInterfaceImpl final: public test::TestInterface::Server {
public:
  TestInterfaceImpl(int& callCount);

  kj::Promise<void> foo(FooContext context) override;

  kj::Promise<void> baz(BazContext context) override;

private:
  int& callCount;
};

class TestExtendsImpl final: public test::TestExtends::Server {
public:
  TestExtendsImpl(int& callCount);

  kj::Promise<void> foo(FooContext context) override;

  kj::Promise<void> grault(GraultContext context) override;

private:
  int& callCount;
};

class TestPipelineImpl final: public test::TestPipeline::Server {
public:
  TestPipelineImpl(int& callCount);

  kj::Promise<void> getCap(GetCapContext context) override;

private:
  int& callCount;
};

class TestCallOrderImpl final: public test::TestCallOrder::Server {
public:
  kj::Promise<void> getCallSequence(GetCallSequenceContext context) override;

private:
  uint count = 0;
};

class TestTailCallerImpl final: public test::TestTailCaller::Server {
public:
  TestTailCallerImpl(int& callCount);

  kj::Promise<void> foo(FooContext context) override;

private:
  int& callCount;
};

class TestTailCalleeImpl final: public test::TestTailCallee::Server {
public:
  TestTailCalleeImpl(int& callCount);

  kj::Promise<void> foo(FooContext context) override;

private:
  int& callCount;
};

class TestMoreStuffImpl final: public test::TestMoreStuff::Server {
public:
  TestMoreStuffImpl(int& callCount);

  kj::Promise<void> getCallSequence(GetCallSequenceContext context) override;

  kj::Promise<void> callFoo(CallFooContext context) override;

  kj::Promise<void> callFooWhenResolved(CallFooWhenResolvedContext context) override;

  kj::Promise<void> neverReturn(NeverReturnContext context) override;

  kj::Promise<void> hold(HoldContext context) override;

  kj::Promise<void> callHeld(CallHeldContext context) override;

  kj::Promise<void> getHeld(GetHeldContext context) override;

  kj::Promise<void> echo(EchoContext context) override;

  kj::Promise<void> expectCancel(ExpectCancelContext context) override;

private:
  int& callCount;
  test::TestInterface::Client clientToHold = nullptr;

  kj::Promise<void> loop(uint depth, test::TestInterface::Client cap, ExpectCancelContext context);
};

class TestCapDestructor final: public test::TestInterface::Server {
  // Implementation of TestInterface that notifies when it is destroyed.

public:
  TestCapDestructor(kj::Own<kj::PromiseFulfiller<void>>&& fulfiller)
      : fulfiller(kj::mv(fulfiller)), impl(dummy) {}

  ~TestCapDestructor() {
    fulfiller->fulfill();
  }

  kj::Promise<void> foo(FooContext context) {
    return impl.foo(context);
  }

private:
  kj::Own<kj::PromiseFulfiller<void>> fulfiller;
  int dummy = 0;
  TestInterfaceImpl impl;
};

}  // namespace _ (private)
}  // namespace capnp

#endif  // TEST_UTIL_H_
