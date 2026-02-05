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
#include <type_traits>

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

KJ_TEST("OneOf with reference variant") {
  int value = 42;
  // Use int& with a copyable type (float) to test copy/move semantics
  OneOf<int&, float> var;

  // Test init<T&>()
  var.init<int&>(value);
  KJ_EXPECT(var.is<int&>());
  KJ_EXPECT(var.get<int&>() == 42);

  // Verify it's really a reference (modifying through the reference changes original)
  var.get<int&>() = 100;
  KJ_EXPECT(value == 100);

  // Test tryGet
  KJ_IF_SOME(ref, var.tryGet<int&>()) {
    KJ_EXPECT(ref == 100);
    ref = 200;
  }
  KJ_EXPECT(value == 200);

  // Reset for further tests
  value = 42;
  var.init<int&>(value);

  // Test copy (copies pointer, both point to same object)
  auto copy = var;
  KJ_EXPECT(copy.is<int&>());
  KJ_EXPECT(&copy.get<int&>() == &value);

  // Modifying through copy affects original value
  copy.get<int&>() = 300;
  KJ_EXPECT(value == 300);

  // Test move (copies pointer, nullifies source)
  value = 42;
  var.init<int&>(value);
  auto moved = kj::mv(var);
  KJ_EXPECT(moved.is<int&>());
  KJ_EXPECT(moved.get<int&>() == 42);
  KJ_EXPECT(var == nullptr);  // Source is now empty after move
}

KJ_TEST("OneOf implicit construction from reference") {
  int value = 42;

  // When only T& is a variant (not T), implicit construction should work
  OneOf<int&, float> var = value;
  KJ_EXPECT(var.is<int&>());
  KJ_EXPECT(&var.get<int&>() == &value);

  // Modifying through var affects original
  var.get<int&>() = 100;
  KJ_EXPECT(value == 100);
}

KJ_TEST("OneOf with both T and T& prefers value") {
  int value = 42;

  // When both T and T& are variants, implicit construction from lvalue prefers T (copy)
  OneOf<int, int&> var = value;
  KJ_EXPECT(var.is<int>());  // Should be value, not reference
  KJ_EXPECT(var.get<int>() == 42);

  // Modifying var doesn't affect original
  var.get<int>() = 100;
  KJ_EXPECT(value == 42);

  // Explicit init<T&> can still be used to get reference
  var.init<int&>(value);
  KJ_EXPECT(var.is<int&>());
  KJ_EXPECT(&var.get<int&>() == &value);
}

KJ_TEST("OneOf reference in switch") {
  int value = 42;
  OneOf<int&, float, const char*> var;
  var.init<int&>(value);

  KJ_SWITCH_ONEOF(var) {
    KJ_CASE_ONEOF(i, int&) {
      KJ_EXPECT(i == 42);
      KJ_EXPECT(&i == &value);
      i = 100;
    }
    KJ_CASE_ONEOF(f, float) {
      KJ_FAIL_EXPECT("expected int&, got float");
    }
    KJ_CASE_ONEOF(s, const char*) {
      KJ_FAIL_EXPECT("expected int&, got const char*");
    }
  }

  KJ_EXPECT(value == 100);
}

KJ_TEST("OneOf reference subset construction") {
  int value = 42;
  OneOf<int&> src;
  src.init<int&>(value);

  // Copy from subset
  OneOf<int&, float> dst = src;
  KJ_EXPECT(dst.is<int&>());
  KJ_EXPECT(&dst.get<int&>() == &value);

  // Move from subset
  src.init<int&>(value);
  OneOf<int&, float, const char*> dst2 = kj::mv(src);
  KJ_EXPECT(dst2.is<int&>());
  KJ_EXPECT(&dst2.get<int&>() == &value);
  KJ_EXPECT(src == nullptr);  // Source nullified after move
}

// Test immobile types
struct Immobile {
  int value;
  KJ_DISALLOW_COPY_AND_MOVE(Immobile);
  explicit Immobile(int v): value(v) {}
};

KJ_TEST("OneOf with immobile type") {
  OneOf<Immobile, int> var;

  // Can use init() to construct in-place
  var.init<Immobile>(42);
  KJ_EXPECT(var.is<Immobile>());
  KJ_EXPECT(var.get<Immobile>().value == 42);

  // Can switch to a different variant
  var.init<int>(123);
  KJ_EXPECT(var.is<int>());
  KJ_EXPECT(var.get<int>() == 123);

  // Can switch back
  var.init<Immobile>(999);
  KJ_EXPECT(var.get<Immobile>().value == 999);
}

// Compile-time checks for copy/move availability
static_assert(!std::is_copy_constructible_v<OneOf<Immobile, int>>,
    "OneOf with immobile type should not be copyable");
static_assert(!std::is_move_constructible_v<OneOf<Immobile, int>>,
    "OneOf with immobile type should not be movable");
static_assert(!std::is_copy_assignable_v<OneOf<Immobile, int>>,
    "OneOf with immobile type should not be copy-assignable");
static_assert(!std::is_move_assignable_v<OneOf<Immobile, int>>,
    "OneOf with immobile type should not be move-assignable");

// Move-only type
struct MoveOnly {
  int value;
  KJ_DISALLOW_COPY(MoveOnly);
  MoveOnly(int v): value(v) {}
  MoveOnly(MoveOnly&& other): value(other.value) { other.value = -1; }
  MoveOnly& operator=(MoveOnly&& other) { value = other.value; other.value = -1; return *this; }
};

static_assert(!std::is_copy_constructible_v<OneOf<MoveOnly, int>>,
    "OneOf with move-only type should not be copyable");
static_assert(std::is_move_constructible_v<OneOf<MoveOnly, int>>,
    "OneOf with move-only type should be movable");

KJ_TEST("OneOf with move-only type") {
  OneOf<MoveOnly, int> var;
  var.init<MoveOnly>(42);
  KJ_EXPECT(var.is<MoveOnly>());
  KJ_EXPECT(var.get<MoveOnly>().value == 42);

  // Move construction works
  auto moved = kj::mv(var);
  KJ_EXPECT(moved.is<MoveOnly>());
  KJ_EXPECT(moved.get<MoveOnly>().value == 42);
  KJ_EXPECT(var.is<MoveOnly>());  // Still has the type, but value was moved
  KJ_EXPECT(var.get<MoveOnly>().value == -1);  // Moved-from state
}

// Reference variants are always copyable (copy the pointer)
static_assert(std::is_copy_constructible_v<OneOf<int&, float>>,
    "OneOf with reference variant should be copyable");
static_assert(std::is_move_constructible_v<OneOf<int&, float>>,
    "OneOf with reference variant should be movable");

// Even with immobile types, if there's a reference variant, the reference itself is copyable
static_assert(std::is_copy_constructible_v<OneOf<Immobile&, int>>,
    "OneOf with reference to immobile should be copyable");

}  // namespace kj
