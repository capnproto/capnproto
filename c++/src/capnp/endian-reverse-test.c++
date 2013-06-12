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

// Test that the code for the opposite endianness of our CPU works.  E.g. on x86 this will test
// the bswap-based code.
#define CAPNP_REVERSE_ENDIAN 1
#include "endian.h"
#include <gtest/gtest.h>

namespace capnp {
namespace _ {  // private
namespace {

TEST(EndianReverse, Byte) {
  byte bytes[] = {123, 45, 67, 89};

  WireValue<uint8_t>* vals = reinterpret_cast<WireValue<uint8_t>*>(bytes);

  EXPECT_EQ(123, vals[0].get());
  EXPECT_EQ(45, vals[1].get());
  EXPECT_EQ(67, vals[2].get());
  EXPECT_EQ(89, vals[3].get());

  vals[0].set(21);
  vals[1].set(43);
  vals[2].set(65);
  vals[3].set(87);

  EXPECT_EQ(21, bytes[0]);
  EXPECT_EQ(43, bytes[1]);
  EXPECT_EQ(65, bytes[2]);
  EXPECT_EQ(87, bytes[3]);
}

TEST(EndianReverse, TwoBytes) {
  byte bytes[] = {0x12, 0x34, 0x56, 0x78};

  WireValue<uint16_t>* vals = reinterpret_cast<WireValue<uint16_t>*>(bytes);

  EXPECT_EQ(0x1234, vals[0].get());
  EXPECT_EQ(0x5678, vals[1].get());

  vals[0].set(0x2345);
  vals[1].set(0x6789);

  EXPECT_EQ(0x23, bytes[0]);
  EXPECT_EQ(0x45, bytes[1]);
  EXPECT_EQ(0x67, bytes[2]);
  EXPECT_EQ(0x89, bytes[3]);
}

TEST(EndianReverse, FourBytes) {
  byte bytes[] = {0x12, 0x34, 0x56, 0x78};

  WireValue<uint32_t>* vals = reinterpret_cast<WireValue<uint32_t>*>(bytes);

  EXPECT_EQ(0x12345678u, vals[0].get());

  vals[0].set(0x23456789);

  EXPECT_EQ(0x23, bytes[0]);
  EXPECT_EQ(0x45, bytes[1]);
  EXPECT_EQ(0x67, bytes[2]);
  EXPECT_EQ(0x89, bytes[3]);
}

TEST(EndianReverse, EightBytes) {
  byte bytes[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};

  WireValue<uint64_t>* vals = reinterpret_cast<WireValue<uint64_t>*>(bytes);

  EXPECT_EQ(0x123456789abcdef0ull, vals[0].get());

  vals[0].set(0x23456789abcdef01ull);

  EXPECT_EQ(0x23, bytes[0]);
  EXPECT_EQ(0x45, bytes[1]);
  EXPECT_EQ(0x67, bytes[2]);
  EXPECT_EQ(0x89, bytes[3]);
  EXPECT_EQ(0xab, bytes[4]);
  EXPECT_EQ(0xcd, bytes[5]);
  EXPECT_EQ(0xef, bytes[6]);
  EXPECT_EQ(0x01, bytes[7]);
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
