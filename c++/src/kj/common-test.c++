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
    // Verify orDefault() works with move-only types.
    Maybe<kj::String> m = nullptr;
    kj::String s = kj::mv(m).orDefault(kj::str("foo"));
    EXPECT_EQ("foo", s);
  }

  {
    Maybe<int> m = &i;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_NE(v, &i);
      EXPECT_EQ(234, *v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_NE(v, &i);
      EXPECT_EQ(234, *v);
    } else {
      ADD_FAILURE();
    }
  }

  {
    Maybe<int> m = implicitCast<int*>(nullptr);
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

class Foo {
public:
  KJ_DISALLOW_COPY(Foo);
  virtual ~Foo() {}
protected:
  Foo() = default;
};

class Bar: public Foo {
public:
  Bar() = default;
  KJ_DISALLOW_COPY(Bar);
  virtual ~Bar() {}
};

class Baz: public Foo {
public:
  Baz() = delete;
  KJ_DISALLOW_COPY(Baz);
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

}  // namespace
}  // namespace kj
