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

#include "common.h"
#include "test.h"
#include <inttypes.h>
#include <kj/compat/gtest.h>

namespace kj {
namespace {

KJ_TEST("kj::size() on native arrays") {
  int arr[] = {12, 34, 56, 78};

  size_t expected = 0;
  for (size_t i: indices(arr)) {
    KJ_EXPECT(i == expected++);
  }
  KJ_EXPECT(expected == 4u);
}

struct ImplicitToInt {
  int i;

  operator int() const {
    return i;
  }
};

struct Immovable {
  Immovable() = default;
  KJ_DISALLOW_COPY_AND_MOVE(Immovable);
};

struct CopyOrMove {
  // Type that detects the difference between copy and move.
  CopyOrMove(int i): i(i) {}
  CopyOrMove(CopyOrMove&& other): i(other.i) { other.i = -1; }
  CopyOrMove(const CopyOrMove&) = default;

  int i;
};

TEST(Common, Maybe) {
  {
    Maybe<int> m = 123;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(123, *v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(123, *v);
    } else {
      ADD_FAILURE();
    }
    EXPECT_EQ(123, m.orDefault(456));
    bool ranLazy = false;
    EXPECT_EQ(123, m.orDefault([&] {
      ranLazy = true;
      return 456;
    }));
    EXPECT_FALSE(ranLazy);

    KJ_IF_MAYBE(v, m) {
      int notUsedForRef = 5;
      const int& ref = m.orDefault([&]() -> int& { return notUsedForRef; });

      EXPECT_EQ(ref, *v);
      EXPECT_EQ(&ref, v);

      const int& ref2 = m.orDefault([notUsed = 5]() -> int { return notUsed; });
      EXPECT_NE(&ref, &ref2);
      EXPECT_EQ(ref2, 123);
    } else {
      ADD_FAILURE();
    }
  }

  {
    Maybe<Own<CopyOrMove>> m = kj::heap<CopyOrMove>(123);
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(123, (*v)->i);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(123, (*v)->i);
    } else {
      ADD_FAILURE();
    }
    // We have moved the kj::Own away, so this should give us the default and leave the Maybe empty.
    EXPECT_EQ(456, m.orDefault(heap<CopyOrMove>(456))->i);
    EXPECT_TRUE(m == nullptr);

    bool ranLazy = false;
    EXPECT_EQ(123, mv(m).orDefault([&] {
      ranLazy = true;
      return heap<CopyOrMove>(123);
    })->i);
    EXPECT_TRUE(ranLazy);
    EXPECT_TRUE(m == nullptr);

    m = heap<CopyOrMove>(123);
    EXPECT_TRUE(m != nullptr);
    ranLazy = false;
    EXPECT_EQ(123, mv(m).orDefault([&] {
      ranLazy = true;
      return heap<CopyOrMove>(456);
    })->i);
    EXPECT_FALSE(ranLazy);
    EXPECT_TRUE(m == nullptr);
  }

  {
    Maybe<int> empty;
    int defaultValue = 5;
    auto& ref1 = empty.orDefault([&defaultValue]() -> int& {
      return defaultValue;
    });
    EXPECT_EQ(&ref1, &defaultValue);

    auto ref2 = empty.orDefault([&]() -> int { return defaultValue; });
    EXPECT_NE(&ref2, &defaultValue);
  }

  {
    Maybe<int> m = 0;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(0, *v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(0, *v);
    } else {
      ADD_FAILURE();
    }
    EXPECT_EQ(0, m.orDefault(456));
    bool ranLazy = false;
    EXPECT_EQ(0, m.orDefault([&] {
      ranLazy = true;
      return 456;
    }));
    EXPECT_FALSE(ranLazy);
  }

  {
    Maybe<int> m = nullptr;
    EXPECT_TRUE(m == nullptr);
    EXPECT_FALSE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    KJ_IF_MAYBE(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    EXPECT_EQ(456, m.orDefault(456));
    bool ranLazy = false;
    EXPECT_EQ(456, m.orDefault([&] {
      ranLazy = true;
      return 456;
    }));
    EXPECT_TRUE(ranLazy);
  }

  int i = 234;
  {
    Maybe<int&> m = i;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
    EXPECT_EQ(234, m.orDefault(456));
  }

  {
    Maybe<int&> m = nullptr;
    EXPECT_TRUE(m == nullptr);
    EXPECT_FALSE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    KJ_IF_MAYBE(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    EXPECT_EQ(456, m.orDefault(456));
  }

  {
    Maybe<int&> m = &i;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
    EXPECT_EQ(234, m.orDefault(456));
  }

  {
    const Maybe<int&> m2 = &i;
    Maybe<const int&> m = m2;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
    EXPECT_EQ(234, m.orDefault(456));
  }

  {
    Maybe<int&> m = implicitCast<int*>(nullptr);
    EXPECT_TRUE(m == nullptr);
    EXPECT_FALSE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    KJ_IF_MAYBE(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    EXPECT_EQ(456, m.orDefault(456));
  }

  {
    Maybe<int> mi = i;
    Maybe<int&> m = mi;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(&KJ_ASSERT_NONNULL(mi), v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(&KJ_ASSERT_NONNULL(mi), v);
    } else {
      ADD_FAILURE();
    }
    EXPECT_EQ(234, m.orDefault(456));
  }

  {
    Maybe<int> mi = nullptr;
    Maybe<int&> m = mi;
    EXPECT_TRUE(m == nullptr);
    KJ_IF_MAYBE(v, m) {
      KJ_FAIL_EXPECT(*v);
    }
  }

  {
    const Maybe<int> mi = i;
    Maybe<const int&> m = mi;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(&KJ_ASSERT_NONNULL(mi), v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(&KJ_ASSERT_NONNULL(mi), v);
    } else {
      ADD_FAILURE();
    }
    EXPECT_EQ(234, m.orDefault(456));
  }

  {
    const Maybe<int> mi = nullptr;
    Maybe<const int&> m = mi;
    EXPECT_TRUE(m == nullptr);
    KJ_IF_MAYBE(v, m) {
      KJ_FAIL_EXPECT(*v);
    }
  }

  {
    // Verify orDefault() works with move-only types.
    Maybe<kj::String> m = nullptr;
    kj::String s = kj::mv(m).orDefault(kj::str("foo"));
    EXPECT_EQ("foo", s);
    EXPECT_EQ("foo", kj::mv(m).orDefault([] {
      return kj::str("foo");
    }));
  }

  {
    // Test a case where an implicit conversion didn't used to happen correctly.
    Maybe<ImplicitToInt> m(ImplicitToInt { 123 });
    Maybe<uint> m2(m);
    Maybe<uint> m3(kj::mv(m));
    KJ_IF_MAYBE(v, m2) {
      EXPECT_EQ(123, *v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, m3) {
      EXPECT_EQ(123, *v);
    } else {
      ADD_FAILURE();
    }
  }

  {
    // Test usage of immovable types.
    Maybe<Immovable> m;
    KJ_EXPECT(m == nullptr);
    m.emplace();
    KJ_EXPECT(m != nullptr);
    m = nullptr;
    KJ_EXPECT(m == nullptr);
  }

  {
    // Test that initializing Maybe<T> from Maybe<T&>&& does a copy, not a move.
    CopyOrMove x(123);
    Maybe<CopyOrMove&> m(x);
    Maybe<CopyOrMove> m2 = kj::mv(m);
    KJ_EXPECT(m == nullptr);  // m is moved out of and cleared
    KJ_EXPECT(x.i == 123);  // but what m *referenced* was not moved out of
    KJ_EXPECT(KJ_ASSERT_NONNULL(m2).i == 123);  // m2 is a copy of what m referenced
  }

  {
    // Test that a moved-out-of Maybe<T> is left empty after move constructor.
    Maybe<int> m = 123;
    KJ_EXPECT(m != nullptr);

    Maybe<int> n(kj::mv(m));
    KJ_EXPECT(m == nullptr);
    KJ_EXPECT(n != nullptr);
  }

  {
    // Test that a moved-out-of Maybe<T> is left empty after move constructor.
    Maybe<int> m = 123;
    KJ_EXPECT(m != nullptr);

    Maybe<int> n = kj::mv(m);
    KJ_EXPECT(m == nullptr);
    KJ_EXPECT(n != nullptr);
  }

  {
    // Test that a moved-out-of Maybe<T&> is left empty when moved to a Maybe<T>.
    int x = 123;
    Maybe<int&> m = x;
    KJ_EXPECT(m != nullptr);

    Maybe<int> n(kj::mv(m));
    KJ_EXPECT(m == nullptr);
    KJ_EXPECT(n != nullptr);
  }

  {
    // Test that a moved-out-of Maybe<T&> is left empty when moved to another Maybe<T&>.
    int x = 123;
    Maybe<int&> m = x;
    KJ_EXPECT(m != nullptr);

    Maybe<int&> n(kj::mv(m));
    KJ_EXPECT(m == nullptr);
    KJ_EXPECT(n != nullptr);
  }

  {
    Maybe<int> m1 = 123;
    Maybe<int> m2 = 123;
    Maybe<int> m3 = 456;
    Maybe<int> m4 = nullptr;
    Maybe<int> m5 = nullptr;

    KJ_EXPECT(m1 == m2);
    KJ_EXPECT(m1 != m3);
    KJ_EXPECT(m1 != m4);
    KJ_EXPECT(m4 == m5);
    KJ_EXPECT(m4 != m1);
  }
}

TEST(Common, MaybeConstness) {
  int i;

  Maybe<int&> mi = i;
  const Maybe<int&> cmi = mi;
//  const Maybe<int&> cmi2 = cmi;    // shouldn't compile!  Transitive const violation.

  KJ_IF_MAYBE(i2, cmi) {
    EXPECT_EQ(&i, i2);
  } else {
    ADD_FAILURE();
  }

  Maybe<const int&> mci = mi;
  const Maybe<const int&> cmci = mci;
  const Maybe<const int&> cmci2 = cmci;

  KJ_IF_MAYBE(i2, cmci2) {
    EXPECT_EQ(&i, i2);
  } else {
    ADD_FAILURE();
  }
}

#if __GNUC__
TEST(Common, MaybeUnwrapOrReturn) {
  {
    auto func = [](Maybe<int> i) -> int {
      int& j = KJ_UNWRAP_OR_RETURN(i, -1);
      KJ_EXPECT(&j == &KJ_ASSERT_NONNULL(i));
      return j + 2;
    };

    KJ_EXPECT(func(123) == 125);
    KJ_EXPECT(func(nullptr) == -1);
  }

  {
    auto func = [&](Maybe<String> maybe) -> int {
      String str = KJ_UNWRAP_OR_RETURN(kj::mv(maybe), -1);
      return str.parseAs<int>();
    };

    KJ_EXPECT(func(kj::str("123")) == 123);
    KJ_EXPECT(func(nullptr) == -1);
  }

  // Test void return.
  {
    int val = 0;
    auto func = [&](Maybe<int> i) {
      val = KJ_UNWRAP_OR_RETURN(i);
    };

    func(123);
    KJ_EXPECT(val == 123);
    val = 321;
    func(nullptr);
    KJ_EXPECT(val == 321);
  }

  // Test KJ_UNWRAP_OR
  {
    bool wasNull = false;
    auto func = [&](Maybe<int> i) -> int {
      int& j = KJ_UNWRAP_OR(i, {
        wasNull = true;
        return -1;
      });
      KJ_EXPECT(&j == &KJ_ASSERT_NONNULL(i));
      return j + 2;
    };

    KJ_EXPECT(func(123) == 125);
    KJ_EXPECT(!wasNull);
    KJ_EXPECT(func(nullptr) == -1);
    KJ_EXPECT(wasNull);
  }

  {
    bool wasNull = false;
    auto func = [&](Maybe<String> maybe) -> int {
      String str = KJ_UNWRAP_OR(kj::mv(maybe), {
        wasNull = true;
        return -1;
      });
      return str.parseAs<int>();
    };

    KJ_EXPECT(func(kj::str("123")) == 123);
    KJ_EXPECT(!wasNull);
    KJ_EXPECT(func(nullptr) == -1);
    KJ_EXPECT(wasNull);
  }

  // Test void return.
  {
    int val = 0;
    auto func = [&](Maybe<int> i) {
      val = KJ_UNWRAP_OR(i, {
        return;
      });
    };

    func(123);
    KJ_EXPECT(val == 123);
    val = 321;
    func(nullptr);
    KJ_EXPECT(val == 321);
  }

}
#endif

class Foo {
public:
  KJ_DISALLOW_COPY_AND_MOVE(Foo);
  virtual ~Foo() {}
protected:
  Foo() = default;
};

class Bar: public Foo {
public:
  Bar() = default;
  KJ_DISALLOW_COPY_AND_MOVE(Bar);
  virtual ~Bar() {}
};

class Baz: public Foo {
public:
  Baz() = delete;
  KJ_DISALLOW_COPY_AND_MOVE(Baz);
  virtual ~Baz() {}
};

TEST(Common, Downcast) {
  Bar bar;
  Foo& foo = bar;

  EXPECT_EQ(&bar, &downcast<Bar>(foo));
#if defined(KJ_DEBUG) && !KJ_NO_RTTI
  KJ_EXPECT_THROW_MESSAGE("Value cannot be downcast", downcast<Baz>(foo));
#endif

#if KJ_NO_RTTI
  EXPECT_TRUE(dynamicDowncastIfAvailable<Bar>(foo) == nullptr);
  EXPECT_TRUE(dynamicDowncastIfAvailable<Baz>(foo) == nullptr);
#else
  KJ_IF_MAYBE(m, dynamicDowncastIfAvailable<Bar>(foo)) {
    EXPECT_EQ(&bar, m);
  } else {
    KJ_FAIL_ASSERT("Dynamic downcast returned null.");
  }
  EXPECT_TRUE(dynamicDowncastIfAvailable<Baz>(foo) == nullptr);
#endif
}

TEST(Common, MinMax) {
  EXPECT_EQ(5, kj::min(5, 9));
  EXPECT_EQ(5, kj::min(9, 5));
  EXPECT_EQ(5, kj::min(5, 5));
  EXPECT_EQ(9, kj::max(5, 9));
  EXPECT_EQ(9, kj::max(9, 5));
  EXPECT_EQ(5, kj::min(5, 5));

  // Hey look, we can handle the types mismatching.  Eat your heart out, std.
  EXPECT_EQ(5, kj::min(5, 'a'));
  EXPECT_EQ(5, kj::min('a', 5));
  EXPECT_EQ('a', kj::max(5, 'a'));
  EXPECT_EQ('a', kj::max('a', 5));

  EXPECT_EQ('a', kj::min(1234567890123456789ll, 'a'));
  EXPECT_EQ('a', kj::min('a', 1234567890123456789ll));
  EXPECT_EQ(1234567890123456789ll, kj::max(1234567890123456789ll, 'a'));
  EXPECT_EQ(1234567890123456789ll, kj::max('a', 1234567890123456789ll));
}

TEST(Common, MinMaxValue) {
  EXPECT_EQ(0x7f, int8_t(maxValue));
  EXPECT_EQ(0xffu, uint8_t(maxValue));
  EXPECT_EQ(0x7fff, int16_t(maxValue));
  EXPECT_EQ(0xffffu, uint16_t(maxValue));
  EXPECT_EQ(0x7fffffff, int32_t(maxValue));
  EXPECT_EQ(0xffffffffu, uint32_t(maxValue));
  EXPECT_EQ(0x7fffffffffffffffll, int64_t(maxValue));
  EXPECT_EQ(0xffffffffffffffffull, uint64_t(maxValue));

  EXPECT_EQ(-0x80, int8_t(minValue));
  EXPECT_EQ(0, uint8_t(minValue));
  EXPECT_EQ(-0x8000, int16_t(minValue));
  EXPECT_EQ(0, uint16_t(minValue));
  EXPECT_EQ(-0x80000000, int32_t(minValue));
  EXPECT_EQ(0, uint32_t(minValue));
  EXPECT_EQ(-0x8000000000000000ll, int64_t(minValue));
  EXPECT_EQ(0, uint64_t(minValue));

  double f = inf();
  EXPECT_TRUE(f * 2 == f);

  f = nan();
  EXPECT_FALSE(f == f);

  // `char`'s signedness is platform-specific.
  EXPECT_LE(char(minValue), '\0');
  EXPECT_GE(char(maxValue), '\x7f');
}

TEST(Common, Defer) {
  uint i = 0;
  uint j = 1;
  bool k = false;

  {
    KJ_DEFER(++i);
    KJ_DEFER(j += 3; k = true);
    EXPECT_EQ(0u, i);
    EXPECT_EQ(1u, j);
    EXPECT_FALSE(k);
  }

  EXPECT_EQ(1u, i);
  EXPECT_EQ(4u, j);
  EXPECT_TRUE(k);
}

TEST(Common, CanConvert) {
  static_assert(canConvert<long, int>(), "failure");
  static_assert(!canConvert<long, void*>(), "failure");

  struct Super {};
  struct Sub: public Super {};

  static_assert(canConvert<Sub, Super>(), "failure");
  static_assert(!canConvert<Super, Sub>(), "failure");
  static_assert(canConvert<Sub*, Super*>(), "failure");
  static_assert(!canConvert<Super*, Sub*>(), "failure");

  static_assert(canConvert<void*, const void*>(), "failure");
  static_assert(!canConvert<const void*, void*>(), "failure");
}

TEST(Common, ArrayAsBytes) {
  uint32_t raw[] = { 0x12345678u, 0x9abcdef0u };

  ArrayPtr<uint32_t> array = raw;
  ASSERT_EQ(2, array.size());
  EXPECT_EQ(0x12345678u, array[0]);
  EXPECT_EQ(0x9abcdef0u, array[1]);

  {
    ArrayPtr<byte> bytes = array.asBytes();
    ASSERT_EQ(8, bytes.size());

    if (bytes[0] == '\x12') {
      // big-endian
      EXPECT_EQ(0x12u, bytes[0]);
      EXPECT_EQ(0x34u, bytes[1]);
      EXPECT_EQ(0x56u, bytes[2]);
      EXPECT_EQ(0x78u, bytes[3]);
      EXPECT_EQ(0x9au, bytes[4]);
      EXPECT_EQ(0xbcu, bytes[5]);
      EXPECT_EQ(0xdeu, bytes[6]);
      EXPECT_EQ(0xf0u, bytes[7]);
    } else {
      // little-endian
      EXPECT_EQ(0x12u, bytes[3]);
      EXPECT_EQ(0x34u, bytes[2]);
      EXPECT_EQ(0x56u, bytes[1]);
      EXPECT_EQ(0x78u, bytes[0]);
      EXPECT_EQ(0x9au, bytes[7]);
      EXPECT_EQ(0xbcu, bytes[6]);
      EXPECT_EQ(0xdeu, bytes[5]);
      EXPECT_EQ(0xf0u, bytes[4]);
    }
  }

  {
    ArrayPtr<char> chars = array.asChars();
    ASSERT_EQ(8, chars.size());

    if (chars[0] == '\x12') {
      // big-endian
      EXPECT_EQ('\x12', chars[0]);
      EXPECT_EQ('\x34', chars[1]);
      EXPECT_EQ('\x56', chars[2]);
      EXPECT_EQ('\x78', chars[3]);
      EXPECT_EQ('\x9a', chars[4]);
      EXPECT_EQ('\xbc', chars[5]);
      EXPECT_EQ('\xde', chars[6]);
      EXPECT_EQ('\xf0', chars[7]);
    } else {
      // little-endian
      EXPECT_EQ('\x12', chars[3]);
      EXPECT_EQ('\x34', chars[2]);
      EXPECT_EQ('\x56', chars[1]);
      EXPECT_EQ('\x78', chars[0]);
      EXPECT_EQ('\x9a', chars[7]);
      EXPECT_EQ('\xbc', chars[6]);
      EXPECT_EQ('\xde', chars[5]);
      EXPECT_EQ('\xf0', chars[4]);
    }
  }

  ArrayPtr<const uint32_t> constArray = array;

  {
    ArrayPtr<const byte> bytes = constArray.asBytes();
    ASSERT_EQ(8, bytes.size());

    if (bytes[0] == '\x12') {
      // big-endian
      EXPECT_EQ(0x12u, bytes[0]);
      EXPECT_EQ(0x34u, bytes[1]);
      EXPECT_EQ(0x56u, bytes[2]);
      EXPECT_EQ(0x78u, bytes[3]);
      EXPECT_EQ(0x9au, bytes[4]);
      EXPECT_EQ(0xbcu, bytes[5]);
      EXPECT_EQ(0xdeu, bytes[6]);
      EXPECT_EQ(0xf0u, bytes[7]);
    } else {
      // little-endian
      EXPECT_EQ(0x12u, bytes[3]);
      EXPECT_EQ(0x34u, bytes[2]);
      EXPECT_EQ(0x56u, bytes[1]);
      EXPECT_EQ(0x78u, bytes[0]);
      EXPECT_EQ(0x9au, bytes[7]);
      EXPECT_EQ(0xbcu, bytes[6]);
      EXPECT_EQ(0xdeu, bytes[5]);
      EXPECT_EQ(0xf0u, bytes[4]);
    }
  }

  {
    ArrayPtr<const char> chars = constArray.asChars();
    ASSERT_EQ(8, chars.size());

    if (chars[0] == '\x12') {
      // big-endian
      EXPECT_EQ('\x12', chars[0]);
      EXPECT_EQ('\x34', chars[1]);
      EXPECT_EQ('\x56', chars[2]);
      EXPECT_EQ('\x78', chars[3]);
      EXPECT_EQ('\x9a', chars[4]);
      EXPECT_EQ('\xbc', chars[5]);
      EXPECT_EQ('\xde', chars[6]);
      EXPECT_EQ('\xf0', chars[7]);
    } else {
      // little-endian
      EXPECT_EQ('\x12', chars[3]);
      EXPECT_EQ('\x34', chars[2]);
      EXPECT_EQ('\x56', chars[1]);
      EXPECT_EQ('\x78', chars[0]);
      EXPECT_EQ('\x9a', chars[7]);
      EXPECT_EQ('\xbc', chars[6]);
      EXPECT_EQ('\xde', chars[5]);
      EXPECT_EQ('\xf0', chars[4]);
    }
  }
}

KJ_TEST("ArrayPtr operator ==") {
  KJ_EXPECT(ArrayPtr<const int>({123, 456}) == ArrayPtr<const int>({123, 456}));
  KJ_EXPECT(!(ArrayPtr<const int>({123, 456}) != ArrayPtr<const int>({123, 456})));
  KJ_EXPECT(ArrayPtr<const int>({123, 456}) != ArrayPtr<const int>({123, 321}));
  KJ_EXPECT(ArrayPtr<const int>({123, 456}) != ArrayPtr<const int>({123}));

  KJ_EXPECT(ArrayPtr<const int>({123, 456}) == ArrayPtr<const short>({123, 456}));
  KJ_EXPECT(!(ArrayPtr<const int>({123, 456}) != ArrayPtr<const short>({123, 456})));
  KJ_EXPECT(ArrayPtr<const int>({123, 456}) != ArrayPtr<const short>({123, 321}));
  KJ_EXPECT(ArrayPtr<const int>({123, 456}) != ArrayPtr<const short>({123}));

  KJ_EXPECT((ArrayPtr<const StringPtr>({"foo", "bar"}) ==
             ArrayPtr<const char* const>({"foo", "bar"})));
  KJ_EXPECT(!(ArrayPtr<const StringPtr>({"foo", "bar"}) !=
             ArrayPtr<const char* const>({"foo", "bar"})));
  KJ_EXPECT((ArrayPtr<const StringPtr>({"foo", "bar"}) !=
             ArrayPtr<const char* const>({"foo", "baz"})));
  KJ_EXPECT((ArrayPtr<const StringPtr>({"foo", "bar"}) !=
             ArrayPtr<const char* const>({"foo"})));

  // operator== should not use memcmp for double elements.
  double d[1] = { nan() };
  KJ_EXPECT(ArrayPtr<double>(d, 1) != ArrayPtr<double>(d, 1));
}

KJ_TEST("kj::range()") {
  uint expected = 5;
  for (uint i: range(5, 10)) {
    KJ_EXPECT(i == expected++);
  }
  KJ_EXPECT(expected == 10);

  expected = 0;
  for (uint i: range(0, 8)) {
    KJ_EXPECT(i == expected++);
  }
  KJ_EXPECT(expected == 8);
}

KJ_TEST("kj::defer()") {
  {
    // rvalue reference
    bool executed = false;
    {
      auto deferred = kj::defer([&executed]() {
        executed = true;
      });
      KJ_EXPECT(!executed);
    }

    KJ_EXPECT(executed);
  }

  {
    // lvalue reference
    bool executed = false;
    auto executor = [&executed]() {
      executed = true;
    };

    {
      auto deferred = kj::defer(executor);
      KJ_EXPECT(!executed);
    }

    KJ_EXPECT(executed);
  }

  {
    // Cancellation via `cancel()`.
    bool executed = false;
    {
      auto deferred = kj::defer([&executed]() {
        executed = true;
      });
      KJ_EXPECT(!executed);

      // Cancel and release the functor.
      deferred.cancel();
      KJ_EXPECT(!executed);
    }

    KJ_EXPECT(!executed);
  }

  {
    // Execution via `run()`.
    size_t runCount = 0;
    {
      auto deferred = kj::defer([&runCount](){
        ++runCount;
      });

      // Run and release the functor.
      deferred.run();
      KJ_EXPECT(runCount == 1);
    }

    // `deferred` is already been run, so nothing is run when we destruct it.
    KJ_EXPECT(runCount == 1);
  }

}

KJ_TEST("kj::ArrayPtr startsWith / endsWith / findFirst / findLast") {
  // Note: char-/byte- optimized versions are covered by string-test.c++.

  int rawArray[] = {12, 34, 56, 34, 12};
  ArrayPtr<int> arr(rawArray);

  KJ_EXPECT(arr.startsWith({12, 34}));
  KJ_EXPECT(arr.startsWith({12, 34, 56}));
  KJ_EXPECT(!arr.startsWith({12, 34, 56, 78}));
  KJ_EXPECT(arr.startsWith({12, 34, 56, 34, 12}));
  KJ_EXPECT(!arr.startsWith({12, 34, 56, 34, 12, 12}));

  KJ_EXPECT(arr.endsWith({34, 12}));
  KJ_EXPECT(arr.endsWith({56, 34, 12}));
  KJ_EXPECT(!arr.endsWith({78, 56, 34, 12}));
  KJ_EXPECT(arr.endsWith({12, 34, 56, 34, 12}));
  KJ_EXPECT(!arr.endsWith({12, 12, 34, 56, 34, 12}));

  KJ_EXPECT(arr.findFirst(12).orDefault(100) == 0);
  KJ_EXPECT(arr.findFirst(34).orDefault(100) == 1);
  KJ_EXPECT(arr.findFirst(56).orDefault(100) == 2);
  KJ_EXPECT(arr.findFirst(78).orDefault(100) == 100);

  KJ_EXPECT(arr.findLast(12).orDefault(100) == 4);
  KJ_EXPECT(arr.findLast(34).orDefault(100) == 3);
  KJ_EXPECT(arr.findLast(56).orDefault(100) == 2);
  KJ_EXPECT(arr.findLast(78).orDefault(100) == 100);
}

}  // namespace
}  // namespace kj
