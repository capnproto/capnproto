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

#include "unit.h"
#include <gtest/gtest.h>
#include <iostream>

namespace capnproto {
namespace {

class Bytes;
class KiB;
class MiB;

typedef UnitMeasure<int, Bytes> ByteCount;
typedef UnitMeasure<int, KiB> KiBCount;
typedef UnitMeasure<int, MiB> MiBCount;

constexpr ByteCount BYTE = ByteCount::ONE;
constexpr KiBCount KIB = KiBCount::ONE;
constexpr MiBCount MIB = MiBCount::ONE;

constexpr UnitRatio<int, Bytes, KiB> BYTES_PER_KIB(1024);
constexpr UnitRatio<int, Bytes, MiB> BYTES_PER_MIB(1024*1024);
constexpr UnitRatio<int, KiB, MiB> KIB_PER_MIB(1024);

template <typename T, typename U>
std::ostream& operator<<(std::ostream& os, UnitMeasure<T, U> value) {
  return os << (value / UnitMeasure<T, U>::ONE);
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
}  // namespace capnproto
