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

TEST(UnitMeasure, GuardedConst) {
  // TODO(someday): Some script should attempt to compile this test once with each "COMPILE ERROR"
  //   line restored to verify that they actually error out.

  KJ_EXPECT((guarded<456>() + guarded<123>()).unwrap() == 456 + 123);
  KJ_EXPECT((guarded<456>() - guarded<123>()).unwrap() == 456 - 123);
  KJ_EXPECT((guarded<456>() * guarded<123>()).unwrap() == 456 * 123);
  KJ_EXPECT((guarded<456>() / guarded<123>()).unwrap() == 456 / 123);
  KJ_EXPECT((guarded<456>() % guarded<123>()).unwrap() == 456 % 123);
  KJ_EXPECT((guarded<456>() << guarded<5>()).unwrap() == 456 << 5);
  KJ_EXPECT((guarded<456>() >> guarded<2>()).unwrap() == 456 >> 2);

  KJ_EXPECT(guarded<123>() == guarded<123>());
  KJ_EXPECT(guarded<123>() != guarded<456>());
  KJ_EXPECT(guarded<123>() <  guarded<456>());
  KJ_EXPECT(guarded<456>() >  guarded<123>());
  KJ_EXPECT(guarded<123>() <= guarded<456>());
  KJ_EXPECT(guarded<456>() >= guarded<123>());

  KJ_EXPECT(!(guarded<123>() == guarded<456>()));
  KJ_EXPECT(!(guarded<123>() != guarded<123>()));
  KJ_EXPECT(!(guarded<456>() <  guarded<123>()));
  KJ_EXPECT(!(guarded<123>() >  guarded<456>()));
  KJ_EXPECT(!(guarded<456>() <= guarded<123>()));
  KJ_EXPECT(!(guarded<123>() >= guarded<456>()));

  {
    uint16_t succ = unguard(guarded<12345>());
    KJ_EXPECT(succ == 12345);

    // COMPILE ERROR: uint8_t err KJ_UNUSED = unguard(guarded<12345>());
  }

  // COMPILE ERROR: auto err1 KJ_UNUSED = guarded<(0xffffffffffffffffull)>() + guarded<1>();
  // COMPILE ERROR: auto err2 KJ_UNUSED = guarded<1>() - guarded<2>();
  // COMPILE ERROR: auto err3 KJ_UNUSED = guarded<(1ull << 60)>() * guarded<(1ull << 60)>();
  // COMPILE ERROR: auto err4 KJ_UNUSED = guarded<1>() / guarded<0>();
  // COMPILE ERROR: auto err5 KJ_UNUSED = guarded<1>() % guarded<0>();
  // COMPILE ERROR: auto err6 KJ_UNUSED = guarded<1>() << guarded<64>();
  // COMPILE ERROR: auto err7 KJ_UNUSED = guarded<(1ull << 60)>() << guarded<4>();
  // COMPILE ERROR: auto err8 KJ_UNUSED = guarded<1>() >> guarded<64>();

  // COMPILE ERROR: guardedAdd<0xffffffffffffffffull, 1>();
  // COMPILE ERROR: guardedSub<1, 2>();
  // COMPILE ERROR: guardedMul<0x100000000, 0x100000000>();
  // COMPILE ERROR: guardedLShift<0x10, 60>();
}

template <uint value>
constexpr Guarded<value, uint> guardedValue() {
  return Guarded<value, uint>(value, unsafe);
}

TEST(UnitMeasure, Guarded) {
  // TODO(someday): Some script should attempt to compile this test once with each "COMPILE ERROR"
  //   line restored to verify that they actually error out.

  KJ_EXPECT((guardedValue<456>() + guardedValue<123>()).unwrap() == 456 + 123);
  KJ_EXPECT(guardedValue<456>().subtractChecked(guardedValue<123>(), [](){}).unwrap() == 456 - 123);
  KJ_EXPECT((guardedValue<456>() * guardedValue<123>()).unwrap() == 456 * 123);
  KJ_EXPECT((guardedValue<456>() / guardedValue<123>()).unwrap() == 456 / 123);
  KJ_EXPECT((guardedValue<456>() % guardedValue<123>()).unwrap() == 456 % 123);


  {
    Guarded<123, uint8_t> succ KJ_UNUSED;
    // COMPILE ERROR: Guarded<1234, uint8_t> err KJ_UNUSED;
    // COMPILE ERROR: auto err KJ_UNUSED = guardedValue<0xffffffffull>() + guardedValue<1>();
  }

  {
    Guarded<123, uint8_t> succ1 KJ_UNUSED = guardedValue<123>();
    Guarded<123, uint8_t> succ2 KJ_UNUSED = guardedValue<122>();
    Guarded<123, uint8_t> succ3 KJ_UNUSED = guardedValue<0>();
    // COMPILE ERROR: Guarded<123, uint8_t> err KJ_UNUSED = guardedValue<124>();
    // COMPILE ERROR: Guarded<123, uint8_t> err KJ_UNUSED = guardedValue<125>();
    // COMPILE ERROR: Guarded<123, uint8_t> err KJ_UNUSED = guardedValue<123456>();
  }

  Guarded<123, uint8_t> foo;
  foo = guardedValue<123>();
  foo = guardedValue<122>();
  foo = guardedValue<0>();
  // COMPILE ERROR: foo = guardedValue<124>();
  // COMPILE ERROR: foo = guardedValue<125>();
  // COMPILE ERROR: foo = guardedValue<123456>();

  assertMax<122>(foo, []() {});
  // COMPILE ERROR: assertMax<123>(foo, []() {});
  // COMPILE ERROR: assertMax<124>(foo, []() {});

  assertMaxBits<6>(foo, []() {});
  // COMPILE ERROR: assertMaxBits<7>(foo, []() {});
  // COMPILE ERROR: assertMaxBits<8>(foo, []() {});

  Guarded<12, uint8_t> bar;
  // COMPILE ERROR: bar = foo;
  // COMPILE ERROR: bar = foo.assertMax<13>([]() {});
  bool caught = false;
  foo = guardedValue<13>();
  bar = foo.assertMax<12>([&]() { caught = true; });
  KJ_EXPECT(caught);

  foo = guardedValue<100>() + guardedValue<23>();
  // COMPILE ERROR: foo = guardedValue<100>() + guardedValue<24>();

  bar = guardedValue<3>() * guardedValue<4>();
  // COMPILE ERROR: bar = guardedValue<2>() * guardedValue<7>();

  foo.subtractChecked(guardedValue<122>(), []() { KJ_FAIL_EXPECT(); });
  foo.subtractChecked(guardedValue<123>(), []() { KJ_FAIL_EXPECT(); });
  caught = false;
  foo.subtractChecked(guardedValue<124>(), [&]() { caught = true; });
  KJ_EXPECT(caught);

  {
    Guarded<65535, uint16_t> succ1 KJ_UNUSED = guarded((uint16_t)123);
    // COMPILE ERROR: Guarded<65534, uint16_t> err KJ_UNUSED = guarded((uint16_t)123);
  }

  uint old = foo.unwrap();
  foo = foo * unit<decltype(foo)>();
  KJ_EXPECT(foo.unwrap() == old);

  {
    Guarded<1234, uint16_t> x = guarded<123>();
    uint16_t succ = unguard(x);
    KJ_EXPECT(succ == 123);

    // COMPILE ERROR: uint8_t err KJ_UNUSED = unguard(x);
  }
}

TEST(UnitMeasure, GuardedVsGuardedConst) {
  // TODO(someday): Some script should attempt to compile this test once with each "COMPILE ERROR"
  //   line restored to verify that they actually error out.

  KJ_EXPECT((guardedValue<456>() + guarded<123>()).unwrap() == 456 + 123);
  KJ_EXPECT(guardedValue<456>().subtractChecked(guarded<123>(), [](){}).unwrap() == 456 - 123);
  KJ_EXPECT((guardedValue<456>() * guarded<123>()).unwrap() == 456 * 123);
  KJ_EXPECT((guardedValue<456>() / guarded<123>()).unwrap() == 456 / 123);
  KJ_EXPECT((guardedValue<456>() % guarded<123>()).unwrap() == 456 % 123);

  {
    Guarded<123, uint8_t> succ1 KJ_UNUSED = guarded<123>();
    Guarded<123, uint8_t> succ2 KJ_UNUSED = guarded<122>();
    Guarded<123, uint8_t> succ3 KJ_UNUSED = guarded<0>();
    // COMPILE ERROR: Guarded<123, uint8_t> err KJ_UNUSED = guarded<124>();
    // COMPILE ERROR: Guarded<123, uint8_t> err KJ_UNUSED = guarded<125>();
    // COMPILE ERROR: Guarded<123, uint8_t> err KJ_UNUSED = guarded<123456>();
  }

  Guarded<123, uint8_t> foo;
  foo = guarded<123>();
  foo = guarded<122>();
  foo = guarded<0>();
  // COMPILE ERROR: foo = guarded<124>();
  // COMPILE ERROR: foo = guarded<125>();
  // COMPILE ERROR: foo = guarded<123456>();

  Guarded<16, uint8_t> bar;
  // COMPILE ERROR: bar = foo >> guarded<2>();
  bar = foo >> guarded<3>();

  // COMPILE ERROR: foo = bar << guarded<3>();
  foo = bar << guarded<2>();
}

TEST(UnitMeasure, GuardedRange) {
  uint expected = 0;
  for (auto i: zeroTo(guarded<10>())) {
    Guarded<10, uint8_t> value = i;
    KJ_EXPECT(unguard(value) == expected++);
  }
  KJ_EXPECT(expected == 10);

  expected = 0;
  for (auto i: zeroTo(guarded((uint8_t)10))) {
    Guarded<255, uint8_t> value = i;
    KJ_EXPECT(unguard(value) == expected++);
  }
  KJ_EXPECT(expected == 10);

  expected = 3;
  for (auto i: range(guarded((uint8_t)3), guarded((uint8_t)10))) {
    Guarded<255, uint8_t> value = i;
    KJ_EXPECT(unguard(value) == expected++);
  }
  KJ_EXPECT(expected == 10);
}

TEST(UnitMeasure, GuardedQuantity) {
  auto BYTES = unit<Quantity<Guarded<12345, uint16_t>, byte>>();

  uint expected = 0;
  for (auto i: zeroTo(guarded<10>() * BYTES)) {
    Quantity<Guarded<10, uint8_t>, byte> value = i;
    KJ_EXPECT(unguard(value / BYTES) == expected++);
  }
  KJ_EXPECT(expected == 10);

  expected = 0;
  for (auto i: zeroTo(guarded((uint8_t)10) * BYTES)) {
    Quantity<Guarded<255, uint8_t>, byte> value = i;
    KJ_EXPECT(unguard(value / BYTES) == expected++);
  }
  KJ_EXPECT(expected == 10);

  expected = 3;
  for (auto i: range(guarded((uint8_t)3) * BYTES, guarded((uint8_t)10) * BYTES)) {
    Quantity<Guarded<255, uint8_t>, byte> value = i;
    KJ_EXPECT(unguard(value / BYTES) == expected++);
  }
  KJ_EXPECT(expected == 10);
}

}  // namespace
}  // namespace kj
