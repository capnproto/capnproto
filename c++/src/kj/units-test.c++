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
#include <gtest/gtest.h>
#include <iostream>

namespace kj {
namespace {

class Bytes;
class KiB;
class MiB;

typedef Quantity<int, Bytes> ByteCount;
typedef Quantity<int, KiB> KiBCount;
typedef Quantity<int, MiB> MiBCount;

constexpr ByteCount BYTE = unit<ByteCount>();
constexpr KiBCount KIB = unit<KiBCount>();
constexpr MiBCount MIB = unit<MiBCount>();

constexpr UnitRatio<int, Bytes, KiB> BYTES_PER_KIB = 1024 * BYTE / KIB;
constexpr UnitRatio<int, Bytes, MiB> BYTES_PER_MIB = 1024 * 1024 * BYTE / MIB;
constexpr auto KIB_PER_MIB = 1024 * KIB / MIB;

template <typename T, typename U>
std::ostream& operator<<(std::ostream& os, Quantity<T, U> value) {
  return os << (value / unit<Quantity<T, U>>());
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

}  // namespace
}  // namespace kj
