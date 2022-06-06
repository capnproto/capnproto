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

#include "one-of.h"
#include "string.h"
#include <kj/compat/gtest.h>

namespace kj {

TEST(OneOf, Basic) {
  OneOf<int, float, String> var;

  EXPECT_FALSE(var.is<int>());
  EXPECT_FALSE(var.is<float>());
  EXPECT_FALSE(var.is<String>());
  EXPECT_TRUE(var.tryGet<int>() == nullptr);
  EXPECT_TRUE(var.tryGet<float>() == nullptr);
  EXPECT_TRUE(var.tryGet<String>() == nullptr);

  var.init<int>(123);

  EXPECT_TRUE(var.is<int>());
  EXPECT_FALSE(var.is<float>());
  EXPECT_FALSE(var.is<String>());

  EXPECT_EQ(123, var.get<int>());
#if !KJ_NO_EXCEPTIONS && defined(KJ_DEBUG)
  EXPECT_ANY_THROW(var.get<float>());
  EXPECT_ANY_THROW(var.get<String>());
#endif

  EXPECT_EQ(123, KJ_ASSERT_NONNULL(var.tryGet<int>()));
  EXPECT_TRUE(var.tryGet<float>() == nullptr);
  EXPECT_TRUE(var.tryGet<String>() == nullptr);

  var.init<String>(kj::str("foo"));

  EXPECT_FALSE(var.is<int>());
  EXPECT_FALSE(var.is<float>());
  EXPECT_TRUE(var.is<String>());

  EXPECT_EQ("foo", var.get<String>());

  EXPECT_TRUE(var.tryGet<int>() == nullptr);
  EXPECT_TRUE(var.tryGet<float>() == nullptr);
  EXPECT_EQ("foo", KJ_ASSERT_NONNULL(var.tryGet<String>()));

  OneOf<int, float, String> var2 = kj::mv(var);
  EXPECT_EQ("", var.get<String>());
  EXPECT_EQ("foo", var2.get<String>());

  var = kj::mv(var2);
  EXPECT_EQ("foo", var.get<String>());
  EXPECT_EQ("", var2.get<String>());

  auto canCompile KJ_UNUSED = [&]() {
    var.allHandled<3>();
    // var.allHandled<2>();  // doesn't compile
  };
}

TEST(OneOf, Copy) {
  OneOf<int, float, const char*> var;

  OneOf<int, float, const char*> var2 = var;
  EXPECT_FALSE(var2.is<int>());
  EXPECT_FALSE(var2.is<float>());
  EXPECT_FALSE(var2.is<const char*>());

  var.init<int>(123);

  var2 = var;
  EXPECT_TRUE(var2.is<int>());
  EXPECT_EQ(123, var2.get<int>());

  var.init<const char*>("foo");

  var2 = var;
  EXPECT_TRUE(var2.is<const char*>());
  EXPECT_STREQ("foo", var2.get<const char*>());
}

TEST(OneOf, Switch) {
  OneOf<int, float, const char*> var;
  var = "foo";
  uint count = 0;

  {
    KJ_SWITCH_ONEOF(var) {
      KJ_CASE_ONEOF(i, int) {
        KJ_FAIL_ASSERT("expected char*, got int", i);
      }
      KJ_CASE_ONEOF(s, const char*) {
        KJ_EXPECT(kj::StringPtr(s) == "foo");
        ++count;
      }
      KJ_CASE_ONEOF(n, float) {
        KJ_FAIL_ASSERT("expected char*, got float", n);
      }
    }
  }

  KJ_EXPECT(count == 1);

  {
    KJ_SWITCH_ONEOF(kj::cp(var)) {
      KJ_CASE_ONEOF(i, int) {
        KJ_FAIL_ASSERT("expected char*, got int", i);
      }
      KJ_CASE_ONEOF(s, const char*) {
        KJ_EXPECT(kj::StringPtr(s) == "foo");
      }
      KJ_CASE_ONEOF(n, float) {
        KJ_FAIL_ASSERT("expected char*, got float", n);
      }
    }
  }

  {
    // At one time this failed to compile.
    const auto& constVar = var;
    KJ_SWITCH_ONEOF(constVar) {
      KJ_CASE_ONEOF(i, int) {
        KJ_FAIL_ASSERT("expected char*, got int", i);
      }
      KJ_CASE_ONEOF(s, const char*) {
        KJ_EXPECT(kj::StringPtr(s) == "foo");
      }
      KJ_CASE_ONEOF(n, float) {
        KJ_FAIL_ASSERT("expected char*, got float", n);
      }
    }
  }
}

TEST(OneOf, Maybe) {
  Maybe<OneOf<int, float>> var;
  var = OneOf<int, float>(123);

  KJ_IF_MAYBE(v, var) {
    // At one time this failed to compile. Note that a Maybe<OneOf<...>> isn't necessarily great
    // style -- you might be better off with an explicit OneOf<Empty, ...>. Nevertheless, it should
    // compile.
    KJ_SWITCH_ONEOF(*v) {
      KJ_CASE_ONEOF(i, int) {
        KJ_EXPECT(i == 123);
      }
      KJ_CASE_ONEOF(n, float) {
        KJ_FAIL_ASSERT("expected int, got float", n);
      }
    }
  }
}

KJ_TEST("OneOf copy/move from alternative variants") {
  {
    // Test const copy.
    const OneOf<int, float> src = 23.5f;
    OneOf<int, bool, float> dst = src;
    KJ_ASSERT(dst.is<float>());
    KJ_EXPECT(dst.get<float>() == 23.5);
  }

  {
    // Test case that requires non-const copy.
    int arr[3] = {1, 2, 3};
    OneOf<int, ArrayPtr<int>> src = ArrayPtr<int>(arr);
    OneOf<int, bool, ArrayPtr<int>> dst = src;
    KJ_ASSERT(dst.is<ArrayPtr<int>>());
    KJ_EXPECT(dst.get<ArrayPtr<int>>().begin() == arr);
    KJ_EXPECT(dst.get<ArrayPtr<int>>().size() == kj::size(arr));
  }

  {
    // Test move.
    OneOf<int, String> src = kj::str("foo");
    OneOf<int, bool, String> dst = kj::mv(src);
    KJ_ASSERT(dst.is<String>());
    KJ_EXPECT(dst.get<String>() == "foo");

    String s = kj::mv(dst).get<String>();
    KJ_EXPECT(s == "foo");
  }

  {
    // We can still have nested OneOfs.
    OneOf<int, float> src = 23.5f;
    OneOf<bool, OneOf<int, float>> dst = src;
    KJ_ASSERT((dst.is<OneOf<int, float>>()));
    KJ_ASSERT((dst.get<OneOf<int, float>>().is<float>()));
    KJ_EXPECT((dst.get<OneOf<int, float>>().get<float>() == 23.5));
  }
}

template<unsigned int N>
struct T {
  unsigned int n = N;
};

TEST(OneOf, MaxVariants) {
  kj::OneOf<
      T<1>, T<2>, T<3>, T<4>, T<5>, T<6>, T<7>, T<8>, T<9>, T<10>,
      T<11>, T<12>, T<13>, T<14>, T<15>, T<16>, T<17>, T<18>, T<19>, T<20>,
      T<21>, T<22>, T<23>, T<24>, T<25>, T<26>, T<27>, T<28>, T<29>, T<30>,
      T<31>, T<32>, T<33>, T<34>, T<35>, T<36>, T<37>, T<38>, T<39>, T<40>,
      T<41>, T<42>, T<43>, T<44>, T<45>, T<46>, T<47>, T<48>, T<49>, T<50>
  > v;

  v = T<1>();
  EXPECT_TRUE(v.is<T<1>>());

  v = T<50>();
  EXPECT_TRUE(v.is<T<50>>());
}

}  // namespace kj
