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

#include "endian.h"
#include <kj/compat/gtest.h>

namespace capnp {
namespace _ {  // private
namespace {

#if CAPNP_DISABLE_ENDIAN_DETECTION
#define Endian EndianUnoptimized
#endif

TEST(Endian, Byte) {
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

TEST(Endian, TwoBytes) {
  byte bytes[] = {0x12, 0x34, 0x56, 0x78};

  WireValue<uint16_t>* vals = reinterpret_cast<WireValue<uint16_t>*>(bytes);

  EXPECT_EQ(0x3412, vals[0].get());
  EXPECT_EQ(0x7856, vals[1].get());

  vals[0].set(0x2345);
  vals[1].set(0x6789);

  EXPECT_EQ(0x45, bytes[0]);
  EXPECT_EQ(0x23, bytes[1]);
  EXPECT_EQ(0x89, bytes[2]);
  EXPECT_EQ(0x67, bytes[3]);
}

TEST(Endian, FourBytes) {
  byte bytes[] = {0x12, 0x34, 0x56, 0x78};

  WireValue<uint32_t>* vals = reinterpret_cast<WireValue<uint32_t>*>(bytes);

  EXPECT_EQ(0x78563412u, vals[0].get());

  vals[0].set(0x23456789);

  EXPECT_EQ(0x89, bytes[0]);
  EXPECT_EQ(0x67, bytes[1]);
  EXPECT_EQ(0x45, bytes[2]);
  EXPECT_EQ(0x23, bytes[3]);
}

TEST(Endian, EightBytes) {
  byte bytes[] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};

  WireValue<uint64_t>* vals = reinterpret_cast<WireValue<uint64_t>*>(bytes);

  EXPECT_EQ(0xf0debc9a78563412, vals[0].get());

  vals[0].set(0x23456789abcdef01);

  EXPECT_EQ(0x01, bytes[0]);
  EXPECT_EQ(0xef, bytes[1]);
  EXPECT_EQ(0xcd, bytes[2]);
  EXPECT_EQ(0xab, bytes[3]);
  EXPECT_EQ(0x89, bytes[4]);
  EXPECT_EQ(0x67, bytes[5]);
  EXPECT_EQ(0x45, bytes[6]);
  EXPECT_EQ(0x23, bytes[7]);
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
