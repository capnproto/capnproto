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

#include "descriptor.h"
#include <gtest/gtest.h>

namespace capnproto {
namespace internal {
namespace {

const int READONLY_SEGMENT_START = 123;

const FieldDescriptor TEST_FIELDS[2] = {
    { 1 * WORDS, 0, 0, FieldSize::FOUR_BYTES, 1, 0, 0, 0 },
    { 1 * WORDS, 1, 0, FieldSize::REFERENCE, 1, 0, 0, 0 }
};

extern const StructDescriptor TEST_STRUCT;

extern const Descriptor* const TEST_STRUCT_DEFAULT_REFS[1] = {
    &TEST_STRUCT.base
};

const AlignedData<1> TEST_STRUCT_DEFAULT_DATA = {
    { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 }
};

const StructDescriptor TEST_STRUCT = {
    { Descriptor::Kind::STRUCT },
    2,
    WordCount8(1 * WORDS),
    1,
    TEST_FIELDS,
    TEST_STRUCT_DEFAULT_DATA.bytes,
    TEST_STRUCT_DEFAULT_REFS
};

const int READONLY_SEGMENT_END = 321;

TEST(Descriptors, InReadOnlySegment) {
  // It's extremely important that statically-initialized descriptors end up in the read-only
  // segment, proving that they will not require any dynamic initialization at startup.  We hackily
  // assume that variables will be placed in each segment in the order that they appear, therefore
  // if our test declarations above do in fact land in the read-only segment, they should appear
  // between READONLY_SEGMENT_START and READONLY_SEGMENT_END.

  EXPECT_LE((const void*)&READONLY_SEGMENT_START, (const void*)&TEST_FIELDS);
  EXPECT_GE((const void*)&READONLY_SEGMENT_END  , (const void*)&TEST_FIELDS);
  EXPECT_LE((const void*)&READONLY_SEGMENT_START, (const void*)&TEST_STRUCT_DEFAULT_DATA);
  EXPECT_GE((const void*)&READONLY_SEGMENT_END  , (const void*)&TEST_STRUCT_DEFAULT_DATA);
  EXPECT_LE((const void*)&READONLY_SEGMENT_START, (const void*)&TEST_STRUCT_DEFAULT_REFS);
  EXPECT_GE((const void*)&READONLY_SEGMENT_END  , (const void*)&TEST_STRUCT_DEFAULT_REFS);
  EXPECT_LE((const void*)&READONLY_SEGMENT_START, (const void*)&TEST_STRUCT);
  EXPECT_GE((const void*)&READONLY_SEGMENT_END  , (const void*)&TEST_STRUCT);
}

}  // namespace
}  // namespace internal
}  // namespace capnproto
