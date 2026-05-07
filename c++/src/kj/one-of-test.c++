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
#include "refcount.h"
#include "string.h"
#include <kj/compat/gtest.h>

namespace kj {

TEST(OneOf, Basic) {
  OneOf<int, float, String> var;

  EXPECT_FALSE(var.is<int>());
  EXPECT_FALSE(var.is<float>());
  EXPECT_FALSE(var.is<String>());
  EXPECT_TRUE(var.tryGet<int>() == kj::none);
  EXPECT_TRUE(var.tryGet<float>() == kj::none);
  EXPECT_TRUE(var.tryGet<String>() == kj::none);

  var.init<int>(123);

  EXPECT_TRUE(var.is<int>());
  EXPECT_FALSE(var.is<float>());
  EXPECT_FALSE(var.is<String>());

  EXPECT_EQ(123, var.get<int>());
#ifdef KJ_DEBUG
  EXPECT_ANY_THROW(var.get<float>());
  EXPECT_ANY_THROW(var.get<String>());
#endif

  EXPECT_EQ(123, KJ_ASSERT_NONNULL(var.tryGet<int>()));
  EXPECT_TRUE(var.tryGet<float>() == kj::none);
  EXPECT_TRUE(var.tryGet<String>() == kj::none);

  var.init<String>(kj::str("foo"));

  EXPECT_FALSE(var.is<int>());
  EXPECT_FALSE(var.is<float>());
  EXPECT_TRUE(var.is<String>());

  EXPECT_EQ("foo", var.get<String>());

  EXPECT_TRUE(var.tryGet<int>() == kj::none);
  EXPECT_TRUE(var.tryGet<float>() == kj::none);
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

  KJ_IF_SOME(v, var) {
    // At one time this failed to compile. Note that a Maybe<OneOf<...>> isn't necessarily great
    // style -- you might be better off with an explicit OneOf<Empty, ...>. Nevertheless, it should
    // compile.
    KJ_SWITCH_ONEOF(v) {
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

KJ_TEST("OneOf equality") {
  {
    OneOf<int, bool> a = 5;
    OneOf<int, bool> b = 5;
    KJ_EXPECT(a == b);
  }
  {
    OneOf<int, bool> a = false;
    OneOf<int, bool> b = false;
    KJ_EXPECT(a == b);
  }
  {
    OneOf<int, bool> a = true;
    OneOf<int, bool> b = false;
    KJ_EXPECT(a != b);
  }
  {
    OneOf<int, bool> a = 0;
    OneOf<int, bool> b = false;
    KJ_EXPECT(a != b);
  }
  {
    OneOf<int, bool> a = 1;
    OneOf<int, bool> b = true;
    KJ_EXPECT(a != b);
  }
  {
    OneOf<int, bool> a = 5;
    OneOf<int, bool> b = 6;
    KJ_EXPECT(a != b);
  }
  {
    OneOf<int, bool> a = 5;
    OneOf<int, bool> b = true;
    KJ_EXPECT(a != b);
  }
}

KJ_TEST("OneOf stringification") {
  {
    OneOf<int, bool> a = 0;
    OneOf<int, bool> b = false;
    OneOf<int, bool> uninit;
    KJ_EXPECT(kj::str(a) == kj::str(0));
    KJ_EXPECT(kj::str(b) == kj::str(false));
    KJ_EXPECT(kj::str(uninit) == kj::str("(null OneOf)"));
  }
}

template<unsigned int N>
struct T {
  unsigned int n = N;
};

struct RcCloneable: public Refcounted {
  int value;
  RcCloneable(int v): value(v) {}
};

KJ_TEST("OneOf clone: mixed Cloneable + Copyable variants") {
  // OneOf<int, String> is the canonical mixed-trait case. The OneOf itself is non-copyable
  // (String is move-only), but per-variant `Cloneable<T> || Copyable<T>` lets clone() work:
  // String is cloned, int is copied. Exercise both arms by cloning each active variant.

  OneOf<int, String> textVar = kj::str("foo");
  auto textCloned = textVar.clone();
  static_assert(isSameType<decltype(textCloned), OneOf<int, String>>());
  KJ_EXPECT(textCloned.get<String>() == "foo");
  // Independent storage — mutating original doesn't disturb the clone.
  textVar.get<String>() = kj::str("bar");
  KJ_EXPECT(textCloned.get<String>() == "foo");

  OneOf<int, String> intVar = 42;
  auto intCloned = intVar.clone();
  KJ_EXPECT(intCloned.get<int>() == 42);
  intVar.get<int>() = 99;
  KJ_EXPECT(intCloned.get<int>() == 42);
}

KJ_TEST("OneOf clone: nested Cloneable composites (Array, Maybe)") {
  // Variants whose own types are clone()-aware composites: Array<String> deep-clones each
  // element, Maybe<String> clones the held value if present. Verifies the per-variant
  // clone() dispatch propagates through composite types correctly.

  OneOf<Array<String>, Maybe<String>> arrayVar = arr<String>(kj::str("a"), kj::str("b"));
  auto arrayCloned = arrayVar.clone();
  KJ_EXPECT(arrayCloned.get<Array<String>>().size() == 2);
  KJ_EXPECT(arrayCloned.get<Array<String>>()[0] == "a");
  // Distinct backing storage.
  KJ_EXPECT(arrayCloned.get<Array<String>>()[0].begin() !=
      arrayVar.get<Array<String>>()[0].begin());

  OneOf<Array<String>, Maybe<String>> maybeVar = Maybe<String>(kj::str("baz"));
  auto maybeCloned = maybeVar.clone();
  KJ_EXPECT(KJ_ASSERT_NONNULL(maybeCloned.get<Maybe<String>>()) == "baz");
}

KJ_TEST("OneOf clone: type-changing variants (ArrayPtr -> Array)") {
  // ArrayPtr<T>::clone() returns Array<T>, so OneOf<ArrayPtr<int>, String>::clone() returns
  // OneOf<Array<int>, String>. Mirrors Maybe<ArrayPtr<T>>::clone() returning Maybe<Array<T>>.

  int storage[] = {1, 2, 3};
  OneOf<ArrayPtr<int>, String> var = ArrayPtr<int>(storage);

  auto cloned = var.clone();
  static_assert(isSameType<decltype(cloned), OneOf<Array<int>, String>>());
  KJ_EXPECT(cloned.get<Array<int>>().size() == 3);
  KJ_EXPECT(cloned.get<Array<int>>()[0] == 1);
}

KJ_TEST("OneOf clone: non-const overload supports Rc<T>") {
  // kj::Rc<T>::clone() is non-const (it mutates the refcount), so Cloneable<const Rc<T>>
  // is false and the const clone() overload is SFINAE'd out for OneOf<Rc<T>, ...>. The
  // non-const overload exists precisely to support this case; verify it works and that the
  // refcount semantics carry through (both originals share the same underlying object).

  OneOf<int, Rc<RcCloneable>> var = kj::rc<RcCloneable>(42);
  auto cloned = var.clone();
  KJ_EXPECT(cloned.get<Rc<RcCloneable>>()->value == 42);
  KJ_EXPECT(var.get<Rc<RcCloneable>>().get() == cloned.get<Rc<RcCloneable>>().get());
}

KJ_TEST("OneOf clone: default-constructed source produces default-constructed clone") {
  // A default-constructed OneOf has no active variant (tag == 0). Cloning skips every
  // variant arm of the fold expression and returns a fresh default-constructed result.
  // Mirrors the behavior already documented for the copy ctor at the top of TEST(OneOf,
  // Copy).
  OneOf<int, String> var;
  KJ_EXPECT(!var.is<int>());
  KJ_EXPECT(!var.is<String>());

  auto cloned = var.clone();
  KJ_EXPECT(!cloned.is<int>());
  KJ_EXPECT(!cloned.is<String>());
}

KJ_TEST("OneOf clone: moved-from source still clonable, produces moved-from-equivalent clone") {
  // OneOf doesn't reset its tag during move construction (see TEST(OneOf, Basic) above:
  // `kj::mv(var); var.get<String>() == ""` is the existing documented behavior). So a
  // moved-from OneOf still has tag == String with a moved-from String inside. clone() is
  // therefore well-defined on a moved-from source: it clones whatever the moved-from
  // variant currently holds — which for kj::String is empty content.
  OneOf<int, String> var = kj::str("payload");
  OneOf<int, String> moved KJ_UNUSED = kj::mv(var);

  KJ_EXPECT(var.is<String>());
  KJ_EXPECT(var.get<String>() == "");

  auto cloned = var.clone();
  KJ_EXPECT(cloned.is<String>());
  KJ_EXPECT(cloned.get<String>() == "");
}

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
