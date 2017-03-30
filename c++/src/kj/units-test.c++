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

#include "units.h"
#include <kj/compat/gtest.h>
#include <iostream>

namespace kj {
namespace {

class Bytes;
class KiB;
class MiB;

typedef Quantity<int, Bytes> ByteCount;
typedef Quantity<int, KiB> KiBCount;
typedef Quantity<int, MiB> MiBCount;

constexpr ByteCount BYTE KJ_UNUSED = unit<ByteCount>();
constexpr KiBCount KIB = unit<KiBCount>();
constexpr MiBCount MIB = unit<MiBCount>();

constexpr UnitRatio<int, Bytes, KiB> BYTES_PER_KIB KJ_UNUSED = 1024 * BYTE / KIB;
constexpr UnitRatio<int, Bytes, MiB> BYTES_PER_MIB KJ_UNUSED = 1024 * 1024 * BYTE / MIB;
constexpr auto KIB_PER_MIB = 1024 * KIB / MIB;

template <typename T, typename U>
kj::String KJ_STRINGIFY(Quantity<T, U> value) {
  return kj::str(value / unit<Quantity<T, U>>());
}

TEST(UnitMeasure, Basics) {
  KiBCount k = 15 * KIB;
  EXPECT_EQ(15, k / KIB);
  EXPECT_EQ(16 * KIB, k + KIB);

  k += KIB;
  k *= 2048;

  EXPECT_EQ(32 * MIB, k / KIB_PER_MIB);

  EXPECT_TRUE(2 * KIB < 4 * KIB);
  EXPECT_FALSE(8 * KIB < 4 * KIB);
}

template <typename T, typename U>
static void assertSameType() {
  U u;
  T* t = &u;
  *t = 0;
}

TEST(UnitMeasure, AtLeastUInt) {
  assertSameType<uint8_t , AtLeastUInt< 2>>();
  assertSameType<uint8_t , AtLeastUInt< 3>>();
  assertSameType<uint8_t , AtLeastUInt< 4>>();
  assertSameType<uint8_t , AtLeastUInt< 5>>();
  assertSameType<uint8_t , AtLeastUInt< 6>>();
  assertSameType<uint8_t , AtLeastUInt< 7>>();
  assertSameType<uint8_t , AtLeastUInt< 8>>();
  assertSameType<uint16_t, AtLeastUInt< 9>>();
  assertSameType<uint16_t, AtLeastUInt<10>>();
  assertSameType<uint16_t, AtLeastUInt<13>>();
  assertSameType<uint16_t, AtLeastUInt<16>>();
  assertSameType<uint32_t, AtLeastUInt<17>>();
  assertSameType<uint32_t, AtLeastUInt<23>>();
  assertSameType<uint32_t, AtLeastUInt<24>>();
  assertSameType<uint32_t, AtLeastUInt<25>>();
  assertSameType<uint32_t, AtLeastUInt<32>>();
  assertSameType<uint64_t, AtLeastUInt<33>>();
  assertSameType<uint64_t, AtLeastUInt<40>>();
  assertSameType<uint64_t, AtLeastUInt<41>>();
  assertSameType<uint64_t, AtLeastUInt<47>>();
  assertSameType<uint64_t, AtLeastUInt<48>>();
  assertSameType<uint64_t, AtLeastUInt<52>>();
  assertSameType<uint64_t, AtLeastUInt<64>>();

  // COMPILE ERROR: assertSameType<uint64_t, AtLeastUInt<65>>();
}

TEST(UnitMeasure, BoundedConst) {
  // TODO(someday): Some script should attempt to compile this test once with each "COMPILE ERROR"
  //   line restored to verify that they actually error out.

  KJ_EXPECT((bounded<456>() + bounded<123>()).unwrap() == 456 + 123);
  KJ_EXPECT((bounded<456>() - bounded<123>()).unwrap() == 456 - 123);
  KJ_EXPECT((bounded<456>() * bounded<123>()).unwrap() == 456 * 123);
  KJ_EXPECT((bounded<456>() / bounded<123>()).unwrap() == 456 / 123);
  KJ_EXPECT((bounded<456>() % bounded<123>()).unwrap() == 456 % 123);
  KJ_EXPECT((bounded<456>() << bounded<5>()).unwrap() == 456 << 5);
  KJ_EXPECT((bounded<456>() >> bounded<2>()).unwrap() == 456 >> 2);

  KJ_EXPECT(bounded<123>() == bounded<123>());
  KJ_EXPECT(bounded<123>() != bounded<456>());
  KJ_EXPECT(bounded<123>() <  bounded<456>());
  KJ_EXPECT(bounded<456>() >  bounded<123>());
  KJ_EXPECT(bounded<123>() <= bounded<456>());
  KJ_EXPECT(bounded<456>() >= bounded<123>());

  KJ_EXPECT(!(bounded<123>() == bounded<456>()));
  KJ_EXPECT(!(bounded<123>() != bounded<123>()));
  KJ_EXPECT(!(bounded<456>() <  bounded<123>()));
  KJ_EXPECT(!(bounded<123>() >  bounded<456>()));
  KJ_EXPECT(!(bounded<456>() <= bounded<123>()));
  KJ_EXPECT(!(bounded<123>() >= bounded<456>()));

  {
    uint16_t succ = unbound(bounded<12345>());
    KJ_EXPECT(succ == 12345);

    // COMPILE ERROR: uint8_t err KJ_UNUSED = unbound(bounded<12345>());
  }

  // COMPILE ERROR: auto err1 KJ_UNUSED = bounded<(0xffffffffffffffffull)>() + bounded<1>();
  // COMPILE ERROR: auto err2 KJ_UNUSED = bounded<1>() - bounded<2>();
  // COMPILE ERROR: auto err3 KJ_UNUSED = bounded<(1ull << 60)>() * bounded<(1ull << 60)>();
  // COMPILE ERROR: auto err4 KJ_UNUSED = bounded<1>() / bounded<0>();
  // COMPILE ERROR: auto err5 KJ_UNUSED = bounded<1>() % bounded<0>();
  // COMPILE ERROR: auto err6 KJ_UNUSED = bounded<1>() << bounded<64>();
  // COMPILE ERROR: auto err7 KJ_UNUSED = bounded<(1ull << 60)>() << bounded<4>();
  // COMPILE ERROR: auto err8 KJ_UNUSED = bounded<1>() >> bounded<64>();

  // COMPILE ERROR: boundedAdd<0xffffffffffffffffull, 1>();
  // COMPILE ERROR: boundedSub<1, 2>();
  // COMPILE ERROR: boundedMul<0x100000000, 0x100000000>();
  // COMPILE ERROR: boundedLShift<0x10, 60>();
}

template <uint value, typename T = uint>
constexpr Bounded<value, T> boundedValue(NoInfer<T> runtimeValue = value) {
  return Bounded<value, T>(runtimeValue, unsafe);
}

TEST(UnitMeasure, Bounded) {
  // TODO(someday): Some script should attempt to compile this test once with each "COMPILE ERROR"
  //   line restored to verify that they actually error out.

  KJ_EXPECT((boundedValue<456>() + boundedValue<123>()).unwrap() == 456 + 123);
  KJ_EXPECT(boundedValue<456>().subtractChecked(boundedValue<123>(), [](){}).unwrap() == 456 - 123);
  KJ_EXPECT((boundedValue<456>() * boundedValue<123>()).unwrap() == 456 * 123);
  KJ_EXPECT((boundedValue<456>() / boundedValue<123>()).unwrap() == 456 / 123);
  KJ_EXPECT((boundedValue<456>() % boundedValue<123>()).unwrap() == 456 % 123);


  {
    Bounded<123, uint8_t> succ KJ_UNUSED;
    // COMPILE ERROR: Bounded<1234, uint8_t> err KJ_UNUSED;
    // COMPILE ERROR: auto err KJ_UNUSED = boundedValue<0xffffffffull>() + boundedValue<1>();
  }

  {
    Bounded<123, uint8_t> succ1 KJ_UNUSED = boundedValue<123>();
    Bounded<123, uint8_t> succ2 KJ_UNUSED = boundedValue<122>();
    Bounded<123, uint8_t> succ3 KJ_UNUSED = boundedValue<0>();
    // COMPILE ERROR: Bounded<123, uint8_t> err KJ_UNUSED = boundedValue<124>();
    // COMPILE ERROR: Bounded<123, uint8_t> err KJ_UNUSED = boundedValue<125>();
    // COMPILE ERROR: Bounded<123, uint8_t> err KJ_UNUSED = boundedValue<123456>();
  }

  Bounded<123, uint8_t> foo;
  foo = boundedValue<123>();
  foo = boundedValue<122>();
  foo = boundedValue<0>();
  // COMPILE ERROR: foo = boundedValue<124>();
  // COMPILE ERROR: foo = boundedValue<125>();
  // COMPILE ERROR: foo = boundedValue<123456>();

  assertMax<122>(foo, []() {});
  // COMPILE ERROR: assertMax<123>(foo, []() {});
  // COMPILE ERROR: assertMax<124>(foo, []() {});

  assertMaxBits<6>(foo, []() {});
  // COMPILE ERROR: assertMaxBits<7>(foo, []() {});
  // COMPILE ERROR: assertMaxBits<8>(foo, []() {});

  Bounded<12, uint8_t> bar;
  // COMPILE ERROR: bar = foo;
  // COMPILE ERROR: bar = foo.assertMax<13>([]() {});
  bool caught = false;
  foo = boundedValue<13>();
  bar = foo.assertMax<12>([&]() { caught = true; });
  KJ_EXPECT(caught);

  foo = boundedValue<100>() + boundedValue<23>();
  // COMPILE ERROR: foo = boundedValue<100>() + boundedValue<24>();

  bar = boundedValue<3>() * boundedValue<4>();
  // COMPILE ERROR: bar = boundedValue<2>() * boundedValue<7>();

  foo.subtractChecked(boundedValue<122>(), []() { KJ_FAIL_EXPECT(""); });
  foo.subtractChecked(boundedValue<123>(), []() { KJ_FAIL_EXPECT(""); });
  caught = false;
  foo.subtractChecked(boundedValue<124>(), [&]() { caught = true; });
  KJ_EXPECT(caught);

  {
    Bounded<65535, uint16_t> succ1 KJ_UNUSED = bounded((uint16_t)123);
    // COMPILE ERROR: Bounded<65534, uint16_t> err KJ_UNUSED = bounded((uint16_t)123);
  }

  uint old = foo.unwrap();
  foo = foo * unit<decltype(foo)>();
  KJ_EXPECT(foo.unwrap() == old);

  {
    Bounded<1234, uint16_t> x = bounded<123>();
    uint16_t succ = unbound(x);
    KJ_EXPECT(succ == 123);

    // COMPILE ERROR: uint8_t err KJ_UNUSED = unbound(x);
  }
}

TEST(UnitMeasure, BoundedVsGuardedConst) {
  // TODO(someday): Some script should attempt to compile this test once with each "COMPILE ERROR"
  //   line restored to verify that they actually error out.

  KJ_EXPECT((boundedValue<456>() + bounded<123>()).unwrap() == 456 + 123);
  KJ_EXPECT(boundedValue<456>().subtractChecked(bounded<123>(), [](){}).unwrap() == 456 - 123);
  KJ_EXPECT((boundedValue<456>() * bounded<123>()).unwrap() == 456 * 123);
  KJ_EXPECT((boundedValue<456>() / bounded<123>()).unwrap() == 456 / 123);
  KJ_EXPECT((boundedValue<456>() % bounded<123>()).unwrap() == 456 % 123);

  {
    Bounded<123, uint8_t> succ1 KJ_UNUSED = bounded<123>();
    Bounded<123, uint8_t> succ2 KJ_UNUSED = bounded<122>();
    Bounded<123, uint8_t> succ3 KJ_UNUSED = bounded<0>();
    // COMPILE ERROR: Bounded<123, uint8_t> err KJ_UNUSED = bounded<124>();
    // COMPILE ERROR: Bounded<123, uint8_t> err KJ_UNUSED = bounded<125>();
    // COMPILE ERROR: Bounded<123, uint8_t> err KJ_UNUSED = bounded<123456>();
  }

  Bounded<123, uint8_t> foo;
  foo = bounded<123>();
  foo = bounded<122>();
  foo = bounded<0>();
  // COMPILE ERROR: foo = bounded<124>();
  // COMPILE ERROR: foo = bounded<125>();
  // COMPILE ERROR: foo = bounded<123456>();

  Bounded<16, uint8_t> bar;
  // COMPILE ERROR: bar = foo >> bounded<2>();
  bar = foo >> bounded<3>();

  // COMPILE ERROR: foo = bar << bounded<3>();
  foo = bar << bounded<2>();
}

TEST(UnitMeasure, BoundedRange) {
  uint expected = 0;
  for (auto i: zeroTo(bounded<10>())) {
    Bounded<10, uint8_t> value = i;
    KJ_EXPECT(unbound(value) == expected++);
  }
  KJ_EXPECT(expected == 10);

  expected = 0;
  for (auto i: zeroTo(bounded((uint8_t)10))) {
    Bounded<255, uint8_t> value = i;
    KJ_EXPECT(unbound(value) == expected++);
  }
  KJ_EXPECT(expected == 10);

  expected = 3;
  for (auto i: range(bounded((uint8_t)3), bounded((uint8_t)10))) {
    Bounded<255, uint8_t> value = i;
    KJ_EXPECT(unbound(value) == expected++);
  }
  KJ_EXPECT(expected == 10);
}

TEST(UnitMeasure, BoundedQuantity) {
  auto BYTES = unit<Quantity<Bounded<12345, uint16_t>, byte>>();

  uint expected = 0;
  for (auto i: zeroTo(bounded<10>() * BYTES)) {
    Quantity<Bounded<10, uint8_t>, byte> value = i;
    KJ_EXPECT(unbound(value / BYTES) == expected++);
  }
  KJ_EXPECT(expected == 10);

  expected = 0;
  for (auto i: zeroTo(bounded((uint8_t)10) * BYTES)) {
    Quantity<Bounded<255, uint8_t>, byte> value = i;
    KJ_EXPECT(unbound(value / BYTES) == expected++);
  }
  KJ_EXPECT(expected == 10);

  expected = 3;
  for (auto i: range(bounded((uint8_t)3) * BYTES, bounded((uint8_t)10) * BYTES)) {
    Quantity<Bounded<255, uint8_t>, byte> value = i;
    KJ_EXPECT(unbound(value / BYTES) == expected++);
  }
  KJ_EXPECT(expected == 10);
}

template <typename T>
void assertTypeAndValue(T a, T b) { KJ_EXPECT(a == b); }

TEST(UnitMeasure, BoundedMinMax) {
  assertTypeAndValue(bounded<5>(), kj::max(bounded<4>(), bounded<5>()));
  assertTypeAndValue(bounded<5>(), kj::max(bounded<5>(), bounded<4>()));
  assertTypeAndValue(bounded<4>(), kj::max(bounded<4>(), bounded<4>()));

  assertTypeAndValue(bounded<4>(), kj::min(bounded<4>(), bounded<5>()));
  assertTypeAndValue(bounded<4>(), kj::min(bounded<5>(), bounded<4>()));
  assertTypeAndValue(bounded<4>(), kj::min(bounded<4>(), bounded<4>()));

  typedef uint8_t t1;
  typedef uint16_t t2;

  assertTypeAndValue(boundedValue<5,t2>(3), kj::max(boundedValue<4,t2>(3), boundedValue<5,t1>(2)));
  assertTypeAndValue(boundedValue<5,t2>(3), kj::max(boundedValue<5,t1>(2), boundedValue<4,t2>(3)));
  assertTypeAndValue(boundedValue<4,t2>(3), kj::max(boundedValue<4,t2>(3), boundedValue<4,t2>(3)));

  assertTypeAndValue(boundedValue<4,t2>(2), kj::min(boundedValue<4,t2>(3), boundedValue<5,t1>(2)));
  assertTypeAndValue(boundedValue<4,t2>(2), kj::min(boundedValue<5,t1>(2), boundedValue<4,t2>(3)));
  assertTypeAndValue(boundedValue<4,t2>(3), kj::min(boundedValue<4,t2>(3), boundedValue<4,t2>(3)));

  assertTypeAndValue(boundedValue<5,t1>(4), kj::max(bounded<4>(), boundedValue<5,t1>(2)));
  assertTypeAndValue(boundedValue<5,t1>(4), kj::max(boundedValue<5,t1>(2), bounded<4>()));

  assertTypeAndValue(boundedValue<4,t1>(2), kj::min(bounded<4>(), boundedValue<5,t1>(2)));
  assertTypeAndValue(boundedValue<4,t1>(2), kj::min(boundedValue<5,t1>(2), bounded<4>()));

  // These two are degenerate cases. Currently they fail to compile but meybe they shouldn't?
//  assertTypeAndValue(bounded<5>(), kj::max(boundedValue<4,t2>(3), bounded<5>()));
//  assertTypeAndValue(bounded<5>(), kj::max(bounded<5>(), boundedValue<4,t2>(3)));

  assertTypeAndValue(boundedValue<4,t2>(3), kj::min(boundedValue<4,t2>(3), bounded<5>()));
  assertTypeAndValue(boundedValue<4,t2>(3), kj::min(bounded<5>(), boundedValue<4,t2>(3)));
}

}  // namespace
}  // namespace kj
