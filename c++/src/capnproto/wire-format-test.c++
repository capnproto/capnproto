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

#include "wire-format.h"
#include "descriptor.h"
#include <gtest/gtest.h>

namespace capnproto {
namespace internal {
namespace {

TEST(StructReader, RawData) {
  AlignedData<2> data = {
    {
      // Struct ref, offset = 1, fieldCount = 1, dataSize = 1, referenceCount = 0
      0x08, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
      // Content for the data segment.
      0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef
    }
  };

  StructReader reader = StructReader::readRootTrusted(data.words, data.words);

  EXPECT_EQ(0xefcdab8967452301ull, reader.getDataField<uint64_t>(0 * ELEMENTS, 321u));
  EXPECT_EQ(321u, reader.getDataField<uint64_t>(1 * ELEMENTS, 321u));
  EXPECT_EQ(0x67452301u, reader.getDataField<uint32_t>(0 * ELEMENTS, 321u));
  EXPECT_EQ(0xefcdab89u, reader.getDataField<uint32_t>(1 * ELEMENTS, 321u));
  EXPECT_EQ(321u, reader.getDataField<uint32_t>(2 * ELEMENTS, 321u));
  EXPECT_EQ(0x2301u, reader.getDataField<uint16_t>(0 * ELEMENTS, 321u));
  EXPECT_EQ(0x6745u, reader.getDataField<uint16_t>(1 * ELEMENTS, 321u));
  EXPECT_EQ(0xab89u, reader.getDataField<uint16_t>(2 * ELEMENTS, 321u));
  EXPECT_EQ(0xefcdu, reader.getDataField<uint16_t>(3 * ELEMENTS, 321u));
  EXPECT_EQ(321u, reader.getDataField<uint16_t>(4 * ELEMENTS, 321u));

  // Bits
  EXPECT_TRUE (reader.getDataField<bool>(0 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(1 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(2 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(3 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(4 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(5 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(6 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(7 * ELEMENTS, false));

  EXPECT_TRUE (reader.getDataField<bool>( 8 * ELEMENTS, false));
  EXPECT_TRUE (reader.getDataField<bool>( 9 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(10 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(11 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(12 * ELEMENTS, false));
  EXPECT_TRUE (reader.getDataField<bool>(13 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(14 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(15 * ELEMENTS, false));

  EXPECT_FALSE(reader.getDataField<bool>(64 * ELEMENTS, false));
  EXPECT_TRUE (reader.getDataField<bool>(64 * ELEMENTS, true ));

  // Field number guards.
  EXPECT_EQ(0xefcdab89u,
      reader.getDataFieldCheckingNumber<uint32_t>(FieldNumber(0), 1 * ELEMENTS, 321u));
  EXPECT_EQ(321u,
      reader.getDataFieldCheckingNumber<uint32_t>(FieldNumber(1), 1 * ELEMENTS, 321u));

  EXPECT_TRUE (reader.getDataFieldCheckingNumber<bool>(FieldNumber(0), 0 * ELEMENTS, false));
  EXPECT_TRUE (reader.getDataFieldCheckingNumber<bool>(FieldNumber(0), 0 * ELEMENTS, true ));
  EXPECT_FALSE(reader.getDataFieldCheckingNumber<bool>(FieldNumber(0), 1 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataFieldCheckingNumber<bool>(FieldNumber(0), 1 * ELEMENTS, true ));
  EXPECT_FALSE(reader.getDataFieldCheckingNumber<bool>(FieldNumber(1), 0 * ELEMENTS, false));
  EXPECT_TRUE (reader.getDataFieldCheckingNumber<bool>(FieldNumber(1), 0 * ELEMENTS, true ));
}

}  // namespace
}  // namespace internal
}  // namespace capnproto
