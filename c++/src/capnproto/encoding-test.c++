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

#define CAPNPROTO_PRIVATE
#include "test-import.capnp.h"
#include "message.h"
#include "logging.h"
#include <gtest/gtest.h>
#include "test-util.h"

namespace capnproto {
namespace internal {
namespace {

TEST(Encoding, AllTypes) {
  MallocMessageBuilder builder;

  initTestMessage(builder.initRoot<TestAllTypes>());
  checkTestMessage(builder.getRoot<TestAllTypes>());
  checkTestMessage(builder.getRoot<TestAllTypes>().asReader());

  SegmentArrayMessageReader reader(builder.getSegmentsForOutput());

  checkTestMessage(reader.getRoot<TestAllTypes>());

  ASSERT_EQ(1u, builder.getSegmentsForOutput().size());

  checkTestMessage(readMessageTrusted<TestAllTypes>(builder.getSegmentsForOutput()[0].begin()));
}

TEST(Encoding, AllTypesMultiSegment) {
  MallocMessageBuilder builder(0, AllocationStrategy::FIXED_SIZE);

  initTestMessage(builder.initRoot<TestAllTypes>());
  checkTestMessage(builder.getRoot<TestAllTypes>());
  checkTestMessage(builder.getRoot<TestAllTypes>().asReader());

  SegmentArrayMessageReader reader(builder.getSegmentsForOutput());

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Encoding, Defaults) {
  AlignedData<1> nullRoot = {{0, 0, 0, 0, 0, 0, 0, 0}};
  ArrayPtr<const word> segments[1] = {arrayPtr(nullRoot.words, 1)};
  SegmentArrayMessageReader reader(arrayPtr(segments, 1));

  checkTestMessage(reader.getRoot<TestDefaults>());
  checkTestMessage(readMessageTrusted<TestDefaults>(nullRoot.words));
}

TEST(Encoding, DefaultInitialization) {
  MallocMessageBuilder builder;

  checkTestMessage(builder.getRoot<TestDefaults>());  // first pass initializes to defaults
  checkTestMessage(builder.getRoot<TestDefaults>().asReader());

  checkTestMessage(builder.getRoot<TestDefaults>());  // second pass just reads the initialized structure
  checkTestMessage(builder.getRoot<TestDefaults>().asReader());

  SegmentArrayMessageReader reader(builder.getSegmentsForOutput());

  checkTestMessage(reader.getRoot<TestDefaults>());
}

TEST(Encoding, DefaultInitializationMultiSegment) {
  MallocMessageBuilder builder(0, AllocationStrategy::FIXED_SIZE);

  // first pass initializes to defaults
  checkTestMessage(builder.getRoot<TestDefaults>());
  checkTestMessage(builder.getRoot<TestDefaults>().asReader());

  // second pass just reads the initialized structure
  checkTestMessage(builder.getRoot<TestDefaults>());
  checkTestMessage(builder.getRoot<TestDefaults>().asReader());

  SegmentArrayMessageReader reader(builder.getSegmentsForOutput());

  checkTestMessage(reader.getRoot<TestDefaults>());
}

TEST(Encoding, DefaultsFromEmptyMessage) {
  AlignedData<1> emptyMessage = {{0, 0, 0, 0, 0, 0, 0, 0}};

  ArrayPtr<const word> segments[1] = {arrayPtr(emptyMessage.words, 1)};
  SegmentArrayMessageReader reader(arrayPtr(segments, 1));

  checkTestMessage(reader.getRoot<TestDefaults>());
  checkTestMessage(readMessageTrusted<TestDefaults>(emptyMessage.words));
}

#ifdef NDEBUG
#define EXPECT_DEBUG_ANY_THROW(EXP)
#else
#define EXPECT_DEBUG_ANY_THROW EXPECT_ANY_THROW
#endif

TEST(Encoding, Unions) {
  MallocMessageBuilder builder;
  TestUnion::Builder root = builder.getRoot<TestUnion>();

  EXPECT_EQ(TestUnion::Union0::U0F0S0, root.getUnion0().which());
  EXPECT_EQ(Void::VOID, root.getUnion0().getU0f0s0());
  EXPECT_DEBUG_ANY_THROW(root.getUnion0().getU0f0s1());

  root.getUnion0().setU0f0s1(true);
  EXPECT_EQ(TestUnion::Union0::U0F0S1, root.getUnion0().which());
  EXPECT_TRUE(root.getUnion0().getU0f0s1());
  EXPECT_DEBUG_ANY_THROW(root.getUnion0().getU0f0s0());

  root.getUnion0().setU0f0s8(123);
  EXPECT_EQ(TestUnion::Union0::U0F0S8, root.getUnion0().which());
  EXPECT_EQ(123, root.getUnion0().getU0f0s8());
  EXPECT_DEBUG_ANY_THROW(root.getUnion0().getU0f0s1());
}

struct UnionState {
  uint discriminants[4];
  int dataOffset;

  UnionState(std::initializer_list<uint> discriminants, int dataOffset)
      : dataOffset(dataOffset) {
    memcpy(this->discriminants, discriminants.begin(), sizeof(discriminants));
  }

  bool operator==(const UnionState& other) const {
    for (uint i = 0; i < 4; i++) {
      if (discriminants[i] != other.discriminants[i]) {
        return false;
      }
    }

    return dataOffset == other.dataOffset;
  }
};

std::ostream& operator<<(std::ostream& os, const UnionState& us) {
  os << "UnionState({";

  for (uint i = 0; i < 4; i++) {
    if (i > 0) os << ", ";
    os << us.discriminants[i];
  }

  return os << "}, " << us.dataOffset << ")";
}

template <typename T> T one() { return static_cast<T>(1); }
template <> Text::Reader one() { return "1"; }
template <> Void one() { return Void::VOID; }

template <typename T, typename U>
UnionState initUnion(U (TestUnion::Builder::*unionGetter)(),
                     void (U::Builder::*setter)(T value)) {
  // Use the given setter to initialize the given union field and then return a struct indicating
  // the location of the data that was written as well as the values of the four union
  // discriminants.

  MallocMessageBuilder builder;
  ((builder.getRoot<TestUnion>().*unionGetter)().*setter)(one<T>());
  ArrayPtr<const word> segment = builder.getSegmentsForOutput()[0];

  CHECK(segment.size() > 2, segment.size());

  // Find the offset of the first set bit after the union discriminants.
  int offset = 0;
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(segment.begin() + 2);
       p < reinterpret_cast<const uint8_t*>(segment.end()); p++) {
    if (*p != 0) {
      uint8_t bits = *p;
      while ((bits & 1) == 0) {
        ++offset;
        bits >>= 1;
      }
      goto found;
    }
    offset += 8;
  }
  offset = -1;

found:
  const uint8_t* discriminants = reinterpret_cast<const uint8_t*>(segment.begin() + 1);
  return UnionState({discriminants[0], discriminants[2], discriminants[4], discriminants[6]},
                    offset);
}

#define INIT_UNION(unionName, fieldName) \
  initUnion(&TestUnion::Builder::get##unionName, &TestUnion::unionName::Builder::set##fieldName)

TEST(Encoding, UnionLayout) {
  EXPECT_EQ(UnionState({ 0,0,0,0},  -1), INIT_UNION(Union0, U0f0s0));
  EXPECT_EQ(UnionState({ 1,0,0,0},   0), INIT_UNION(Union0, U0f0s1));
  EXPECT_EQ(UnionState({ 2,0,0,0},   0), INIT_UNION(Union0, U0f0s8));
  EXPECT_EQ(UnionState({ 3,0,0,0},   0), INIT_UNION(Union0, U0f0s16));
  EXPECT_EQ(UnionState({ 4,0,0,0},   0), INIT_UNION(Union0, U0f0s32));
  EXPECT_EQ(UnionState({ 5,0,0,0},   0), INIT_UNION(Union0, U0f0s64));
  EXPECT_EQ(UnionState({ 6,0,0,0}, 448), INIT_UNION(Union0, U0f0sp));

  EXPECT_EQ(UnionState({ 7,0,0,0},  -1), INIT_UNION(Union0, U0f1s0));
  EXPECT_EQ(UnionState({ 8,0,0,0},   0), INIT_UNION(Union0, U0f1s1));
  EXPECT_EQ(UnionState({ 9,0,0,0},   0), INIT_UNION(Union0, U0f1s8));
  EXPECT_EQ(UnionState({10,0,0,0},   0), INIT_UNION(Union0, U0f1s16));
  EXPECT_EQ(UnionState({11,0,0,0},   0), INIT_UNION(Union0, U0f1s32));
  EXPECT_EQ(UnionState({12,0,0,0},   0), INIT_UNION(Union0, U0f1s64));
  EXPECT_EQ(UnionState({13,0,0,0}, 448), INIT_UNION(Union0, U0f1sp));

  EXPECT_EQ(UnionState({0, 0,0,0},  -1), INIT_UNION(Union1, U1f0s0));
  EXPECT_EQ(UnionState({0, 1,0,0},  65), INIT_UNION(Union1, U1f0s1));
  EXPECT_EQ(UnionState({0, 2,0,0},  65), INIT_UNION(Union1, U1f1s1));
  EXPECT_EQ(UnionState({0, 3,0,0},  72), INIT_UNION(Union1, U1f0s8));
  EXPECT_EQ(UnionState({0, 4,0,0},  72), INIT_UNION(Union1, U1f1s8));
  EXPECT_EQ(UnionState({0, 5,0,0},  80), INIT_UNION(Union1, U1f0s16));
  EXPECT_EQ(UnionState({0, 6,0,0},  80), INIT_UNION(Union1, U1f1s16));
  EXPECT_EQ(UnionState({0, 7,0,0},  96), INIT_UNION(Union1, U1f0s32));
  EXPECT_EQ(UnionState({0, 8,0,0},  96), INIT_UNION(Union1, U1f1s32));
  EXPECT_EQ(UnionState({0, 9,0,0}, 128), INIT_UNION(Union1, U1f0s64));
  EXPECT_EQ(UnionState({0,10,0,0}, 128), INIT_UNION(Union1, U1f1s64));
  EXPECT_EQ(UnionState({0,11,0,0}, 512), INIT_UNION(Union1, U1f0sp));
  EXPECT_EQ(UnionState({0,12,0,0}, 512), INIT_UNION(Union1, U1f1sp));

  EXPECT_EQ(UnionState({0,13,0,0},  -1), INIT_UNION(Union1, U1f2s0));
  EXPECT_EQ(UnionState({0,14,0,0}, 128), INIT_UNION(Union1, U1f2s1));
  EXPECT_EQ(UnionState({0,15,0,0}, 128), INIT_UNION(Union1, U1f2s8));
  EXPECT_EQ(UnionState({0,16,0,0}, 128), INIT_UNION(Union1, U1f2s16));
  EXPECT_EQ(UnionState({0,17,0,0}, 128), INIT_UNION(Union1, U1f2s32));
  EXPECT_EQ(UnionState({0,18,0,0}, 128), INIT_UNION(Union1, U1f2s64));
  EXPECT_EQ(UnionState({0,19,0,0}, 512), INIT_UNION(Union1, U1f2sp));

  EXPECT_EQ(UnionState({0,0,0,0}, 192), INIT_UNION(Union2, U2f0s1));
  EXPECT_EQ(UnionState({0,0,0,0}, 193), INIT_UNION(Union3, U3f0s1));
  EXPECT_EQ(UnionState({0,0,1,0}, 200), INIT_UNION(Union2, U2f0s8));
  EXPECT_EQ(UnionState({0,0,0,1}, 208), INIT_UNION(Union3, U3f0s8));
  EXPECT_EQ(UnionState({0,0,2,0}, 224), INIT_UNION(Union2, U2f0s16));
  EXPECT_EQ(UnionState({0,0,0,2}, 240), INIT_UNION(Union3, U3f0s16));
  EXPECT_EQ(UnionState({0,0,3,0}, 256), INIT_UNION(Union2, U2f0s32));
  EXPECT_EQ(UnionState({0,0,0,3}, 288), INIT_UNION(Union3, U3f0s32));
  EXPECT_EQ(UnionState({0,0,4,0}, 320), INIT_UNION(Union2, U2f0s64));
  EXPECT_EQ(UnionState({0,0,0,4}, 384), INIT_UNION(Union3, U3f0s64));
}

TEST(Encoding, UnionDefault) {
  MallocMessageBuilder builder;
  TestUnionDefaults::Reader reader = builder.getRoot<TestUnionDefaults>().asReader();

  {
    auto field = reader.getS16s8s64s8Set();
    EXPECT_EQ(TestUnion::Union0::U0F0S16, field.getUnion0().which());
    EXPECT_EQ(TestUnion::Union1::U1F0S8 , field.getUnion1().which());
    EXPECT_EQ(TestUnion::Union2::U2F0S64, field.getUnion2().which());
    EXPECT_EQ(TestUnion::Union3::U3F0S8 , field.getUnion3().which());
    EXPECT_EQ(321, field.getUnion0().getU0f0s16());
    EXPECT_EQ(123, field.getUnion1().getU1f0s8());
    EXPECT_EQ(12345678901234567ll, field.getUnion2().getU2f0s64());
    EXPECT_EQ(55, field.getUnion3().getU3f0s8());
  }

  {
    auto field = reader.getS0sps1s32Set();
    EXPECT_EQ(TestUnion::Union0::U0F1S0 , field.getUnion0().which());
    EXPECT_EQ(TestUnion::Union1::U1F0SP , field.getUnion1().which());
    EXPECT_EQ(TestUnion::Union2::U2F0S1 , field.getUnion2().which());
    EXPECT_EQ(TestUnion::Union3::U3F0S32, field.getUnion3().which());
    EXPECT_EQ(Void::VOID, field.getUnion0().getU0f1s0());
    EXPECT_EQ("foo", field.getUnion1().getU1f0sp());
    EXPECT_EQ(true, field.getUnion2().getU2f0s1());
    EXPECT_EQ(12345678, field.getUnion3().getU3f0s32());
  }
}

TEST(Encoding, NestedTypes) {
  // This is more of a test of the generated code than the encoding.

  MallocMessageBuilder builder;
  TestNestedTypes::Reader reader = builder.getRoot<TestNestedTypes>().asReader();

  EXPECT_EQ(TestNestedTypes::NestedEnum::BAR, reader.getOuterNestedEnum());
  EXPECT_EQ(TestNestedTypes::NestedStruct::NestedEnum::QUUX, reader.getInnerNestedEnum());

  TestNestedTypes::NestedStruct::Reader nested = reader.getNestedStruct();
  EXPECT_EQ(TestNestedTypes::NestedEnum::BAR, nested.getOuterNestedEnum());
  EXPECT_EQ(TestNestedTypes::NestedStruct::NestedEnum::QUUX, nested.getInnerNestedEnum());
}

TEST(Encoding, Imports) {
  // Also just testing the generated code.

  MallocMessageBuilder builder;
  TestImport::Builder root = builder.getRoot<TestImport>();
  initTestMessage(root.initField());
  checkTestMessage(root.asReader().getField());
}

}  // namespace
}  // namespace internal
}  // namespace capnproto
