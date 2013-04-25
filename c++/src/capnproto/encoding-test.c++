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

template <typename StructType, typename Func>
UnionState initUnion(Func&& initializer) {
  // Use the given setter to initialize the given union field and then return a struct indicating
  // the location of the data that was written as well as the values of the four union
  // discriminants.

  MallocMessageBuilder builder;
  initializer(builder.getRoot<StructType>());
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

TEST(Encoding, UnionLayout) {
#define INIT_UNION(setter) \
  initUnion<TestUnion>([](TestUnion::Builder b) {b.setter;})

  EXPECT_EQ(UnionState({ 0,0,0,0},  -1), INIT_UNION(getUnion0().setU0f0s0(Void::VOID)));
  EXPECT_EQ(UnionState({ 1,0,0,0},   0), INIT_UNION(getUnion0().setU0f0s1(1)));
  EXPECT_EQ(UnionState({ 2,0,0,0},   0), INIT_UNION(getUnion0().setU0f0s8(1)));
  EXPECT_EQ(UnionState({ 3,0,0,0},   0), INIT_UNION(getUnion0().setU0f0s16(1)));
  EXPECT_EQ(UnionState({ 4,0,0,0},   0), INIT_UNION(getUnion0().setU0f0s32(1)));
  EXPECT_EQ(UnionState({ 5,0,0,0},   0), INIT_UNION(getUnion0().setU0f0s64(1)));
  EXPECT_EQ(UnionState({ 6,0,0,0}, 448), INIT_UNION(getUnion0().setU0f0sp("1")));

  EXPECT_EQ(UnionState({ 7,0,0,0},  -1), INIT_UNION(getUnion0().setU0f1s0(Void::VOID)));
  EXPECT_EQ(UnionState({ 8,0,0,0},   0), INIT_UNION(getUnion0().setU0f1s1(1)));
  EXPECT_EQ(UnionState({ 9,0,0,0},   0), INIT_UNION(getUnion0().setU0f1s8(1)));
  EXPECT_EQ(UnionState({10,0,0,0},   0), INIT_UNION(getUnion0().setU0f1s16(1)));
  EXPECT_EQ(UnionState({11,0,0,0},   0), INIT_UNION(getUnion0().setU0f1s32(1)));
  EXPECT_EQ(UnionState({12,0,0,0},   0), INIT_UNION(getUnion0().setU0f1s64(1)));
  EXPECT_EQ(UnionState({13,0,0,0}, 448), INIT_UNION(getUnion0().setU0f1sp("1")));

  EXPECT_EQ(UnionState({0, 0,0,0},  -1), INIT_UNION(getUnion1().setU1f0s0(Void::VOID)));
  EXPECT_EQ(UnionState({0, 1,0,0},  65), INIT_UNION(getUnion1().setU1f0s1(1)));
  EXPECT_EQ(UnionState({0, 2,0,0},  65), INIT_UNION(getUnion1().setU1f1s1(1)));
  EXPECT_EQ(UnionState({0, 3,0,0},  72), INIT_UNION(getUnion1().setU1f0s8(1)));
  EXPECT_EQ(UnionState({0, 4,0,0},  72), INIT_UNION(getUnion1().setU1f1s8(1)));
  EXPECT_EQ(UnionState({0, 5,0,0},  80), INIT_UNION(getUnion1().setU1f0s16(1)));
  EXPECT_EQ(UnionState({0, 6,0,0},  80), INIT_UNION(getUnion1().setU1f1s16(1)));
  EXPECT_EQ(UnionState({0, 7,0,0},  96), INIT_UNION(getUnion1().setU1f0s32(1)));
  EXPECT_EQ(UnionState({0, 8,0,0},  96), INIT_UNION(getUnion1().setU1f1s32(1)));
  EXPECT_EQ(UnionState({0, 9,0,0}, 128), INIT_UNION(getUnion1().setU1f0s64(1)));
  EXPECT_EQ(UnionState({0,10,0,0}, 128), INIT_UNION(getUnion1().setU1f1s64(1)));
  EXPECT_EQ(UnionState({0,11,0,0}, 512), INIT_UNION(getUnion1().setU1f0sp("1")));
  EXPECT_EQ(UnionState({0,12,0,0}, 512), INIT_UNION(getUnion1().setU1f1sp("1")));

  EXPECT_EQ(UnionState({0,13,0,0},  -1), INIT_UNION(getUnion1().setU1f2s0(Void::VOID)));
  EXPECT_EQ(UnionState({0,14,0,0}, 128), INIT_UNION(getUnion1().setU1f2s1(1)));
  EXPECT_EQ(UnionState({0,15,0,0}, 128), INIT_UNION(getUnion1().setU1f2s8(1)));
  EXPECT_EQ(UnionState({0,16,0,0}, 128), INIT_UNION(getUnion1().setU1f2s16(1)));
  EXPECT_EQ(UnionState({0,17,0,0}, 128), INIT_UNION(getUnion1().setU1f2s32(1)));
  EXPECT_EQ(UnionState({0,18,0,0}, 128), INIT_UNION(getUnion1().setU1f2s64(1)));
  EXPECT_EQ(UnionState({0,19,0,0}, 512), INIT_UNION(getUnion1().setU1f2sp("1")));

  EXPECT_EQ(UnionState({0,0,0,0}, 192), INIT_UNION(getUnion2().setU2f0s1(1)));
  EXPECT_EQ(UnionState({0,0,0,0}, 193), INIT_UNION(getUnion3().setU3f0s1(1)));
  EXPECT_EQ(UnionState({0,0,1,0}, 200), INIT_UNION(getUnion2().setU2f0s8(1)));
  EXPECT_EQ(UnionState({0,0,0,1}, 208), INIT_UNION(getUnion3().setU3f0s8(1)));
  EXPECT_EQ(UnionState({0,0,2,0}, 224), INIT_UNION(getUnion2().setU2f0s16(1)));
  EXPECT_EQ(UnionState({0,0,0,2}, 240), INIT_UNION(getUnion3().setU3f0s16(1)));
  EXPECT_EQ(UnionState({0,0,3,0}, 256), INIT_UNION(getUnion2().setU2f0s32(1)));
  EXPECT_EQ(UnionState({0,0,0,3}, 288), INIT_UNION(getUnion3().setU3f0s32(1)));
  EXPECT_EQ(UnionState({0,0,4,0}, 320), INIT_UNION(getUnion2().setU2f0s64(1)));
  EXPECT_EQ(UnionState({0,0,0,4}, 384), INIT_UNION(getUnion3().setU3f0s64(1)));

#undef INIT_UNION
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

// =======================================================================================

TEST(Encoding, InlineStructUnionLayout) {
  uint ptrOffset = TestInlineUnions::STRUCT_SIZE.data * BITS_PER_WORD / BITS - 64;
  auto ptr = [=](uint i) { return ptrOffset + i * 64; };

#define INIT_UNION(setter) \
  initUnion<TestInlineUnions>([](TestInlineUnions::Builder b) {b.setter;})

  EXPECT_EQ(UnionState({ 0,0,0,0},  -1), INIT_UNION(getUnion0().initF0()));
  EXPECT_EQ(UnionState({ 1,0,0,0},   0), INIT_UNION(getUnion0().initF1().setF(1)));
  EXPECT_EQ(UnionState({ 2,0,0,0},   0), INIT_UNION(getUnion0().initF8().setF0(true)));
  EXPECT_EQ(UnionState({ 3,0,0,0},   0), INIT_UNION(getUnion0().initF16().setF0(1)));
  EXPECT_EQ(UnionState({ 4,0,0,0},   0), INIT_UNION(getUnion0().initF32().setF0(1)));
  EXPECT_EQ(UnionState({ 5,0,0,0},   0), INIT_UNION(getUnion0().initF64().setF0(1)));
  EXPECT_EQ(UnionState({ 6,0,0,0},   0), INIT_UNION(getUnion0().initF128().setF0(1)));
  EXPECT_EQ(UnionState({ 7,0,0,0},   0), INIT_UNION(getUnion0().initF192().setF0(1)));

  EXPECT_EQ(UnionState({ 8,0,0,0},  -1), INIT_UNION(getUnion0().initF0p().initF()));
  EXPECT_EQ(UnionState({ 9,0,0,0},   0), INIT_UNION(getUnion0().initF1p().initF().setF(1)));
  EXPECT_EQ(UnionState({10,0,0,0},   0), INIT_UNION(getUnion0().initF8p().initF().setF0(true)));
  EXPECT_EQ(UnionState({11,0,0,0},   0), INIT_UNION(getUnion0().initF16p().initF().setF0(1)));
  EXPECT_EQ(UnionState({12,0,0,0},   0), INIT_UNION(getUnion0().initF32p().initF().setF0(1)));
  EXPECT_EQ(UnionState({13,0,0,0},   0), INIT_UNION(getUnion0().initF64p().initF().setF0(1)));
  EXPECT_EQ(UnionState({14,0,0,0},   0), INIT_UNION(getUnion0().initF128p().initF().setF0(1)));
  EXPECT_EQ(UnionState({15,0,0,0},   0), INIT_UNION(getUnion0().initF192p().initF().setF0(1)));

  EXPECT_EQ(UnionState({ 8,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF0p().setP0("1")));
  EXPECT_EQ(UnionState({ 9,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF1p().setP0("1")));
  EXPECT_EQ(UnionState({10,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF8p().setP0("1")));
  EXPECT_EQ(UnionState({11,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF16p().setP0("1")));
  EXPECT_EQ(UnionState({12,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF32p().setP0("1")));
  EXPECT_EQ(UnionState({13,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF64p().setP0("1")));
  EXPECT_EQ(UnionState({14,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF128p().setP0("1")));
  EXPECT_EQ(UnionState({15,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF192p().setP0("1")));

  EXPECT_EQ(UnionState({0, 0,0,0},  -1), INIT_UNION(getUnion1().initF0()));
  EXPECT_EQ(UnionState({0, 1,0,0}, 193), INIT_UNION(getUnion1().initF1().setF(1)));
  EXPECT_EQ(UnionState({0, 2,0,0}, 200), INIT_UNION(getUnion1().initF8().setF0(true)));
  EXPECT_EQ(UnionState({0, 3,0,0}, 208), INIT_UNION(getUnion1().initF16().setF0(1)));
  EXPECT_EQ(UnionState({0, 4,0,0}, 224), INIT_UNION(getUnion1().initF32().setF0(1)));
  EXPECT_EQ(UnionState({0, 5,0,0}, 256), INIT_UNION(getUnion1().initF64().setF0(1)));
  EXPECT_EQ(UnionState({0, 6,0,0}, 256), INIT_UNION(getUnion1().initF128().setF0(1)));
  EXPECT_EQ(UnionState({0, 7,0,0}, 256), INIT_UNION(getUnion1().initF192().setF0(1)));

  EXPECT_EQ(UnionState({0,0, 0,0}, 448), INIT_UNION(getUnion2().initF1p().initF().setF(1)));
  EXPECT_EQ(UnionState({0,0,0, 0}, 449), INIT_UNION(getUnion3().initF1p().initF().setF(1)));
  EXPECT_EQ(UnionState({0,0, 1,0}, 456), INIT_UNION(getUnion2().initF8p().initF().setF0(true)));
  EXPECT_EQ(UnionState({0,0,0, 1}, 464), INIT_UNION(getUnion3().initF8p().initF().setF0(true)));
  EXPECT_EQ(UnionState({0,0, 2,0}, 480), INIT_UNION(getUnion2().initF16p().initF().setF0(1)));
  EXPECT_EQ(UnionState({0,0,0, 2}, 496), INIT_UNION(getUnion3().initF16p().initF().setF0(1)));
  EXPECT_EQ(UnionState({0,0, 3,0}, 512), INIT_UNION(getUnion2().initF32p().initF().setF0(1)));
  EXPECT_EQ(UnionState({0,0,0, 3}, 544), INIT_UNION(getUnion3().initF32p().initF().setF0(1)));
  EXPECT_EQ(UnionState({0,0, 4,0}, 576), INIT_UNION(getUnion2().initF64p().initF().setF0(1)));
  EXPECT_EQ(UnionState({0,0,0, 4}, 640), INIT_UNION(getUnion3().initF64p().initF().setF0(1)));
  EXPECT_EQ(UnionState({0,0, 5,0}, 704), INIT_UNION(getUnion2().initF128p().initF().setF0(1)));
  EXPECT_EQ(UnionState({0,0,0, 5}, 832), INIT_UNION(getUnion3().initF128p().initF().setF0(1)));
  EXPECT_EQ(UnionState({0,0, 6,0}, 960), INIT_UNION(getUnion2().initF192p().initF().setF0(1)));
  EXPECT_EQ(UnionState({0,0,0, 6},1152), INIT_UNION(getUnion3().initF192p().initF().setF0(1)));

  EXPECT_EQ(UnionState({0,0, 0,0}, ptr( 3)), INIT_UNION(getUnion2().initF1p().setP0("1")));
  EXPECT_EQ(UnionState({0,0,0, 0}, ptr( 4)), INIT_UNION(getUnion3().initF1p().setP0("1")));
  EXPECT_EQ(UnionState({0,0, 1,0}, ptr( 3)), INIT_UNION(getUnion2().initF8p().setP0("1")));
  EXPECT_EQ(UnionState({0,0,0, 1}, ptr( 4)), INIT_UNION(getUnion3().initF8p().setP0("1")));
  EXPECT_EQ(UnionState({0,0, 2,0}, ptr( 5)), INIT_UNION(getUnion2().initF16p().setP0("1")));
  EXPECT_EQ(UnionState({0,0,0, 2}, ptr( 7)), INIT_UNION(getUnion3().initF16p().setP0("1")));
  EXPECT_EQ(UnionState({0,0, 3,0}, ptr( 5)), INIT_UNION(getUnion2().initF32p().setP0("1")));
  EXPECT_EQ(UnionState({0,0,0, 3}, ptr( 7)), INIT_UNION(getUnion3().initF32p().setP0("1")));
  EXPECT_EQ(UnionState({0,0, 4,0}, ptr( 5)), INIT_UNION(getUnion2().initF64p().setP0("1")));
  EXPECT_EQ(UnionState({0,0,0, 4}, ptr( 7)), INIT_UNION(getUnion3().initF64p().setP0("1")));
  EXPECT_EQ(UnionState({0,0, 5,0}, ptr( 9)), INIT_UNION(getUnion2().initF128p().setP0("1")));
  EXPECT_EQ(UnionState({0,0,0, 5}, ptr(12)), INIT_UNION(getUnion3().initF128p().setP0("1")));
  EXPECT_EQ(UnionState({0,0, 6,0}, ptr( 9)), INIT_UNION(getUnion2().initF192p().setP0("1")));
  EXPECT_EQ(UnionState({0,0,0, 6}, ptr(12)), INIT_UNION(getUnion3().initF192p().setP0("1")));

#undef INIT_UNION
}

TEST(Encoding, InitInlineStruct) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<TestInlineLayout>();

  // Set as many bits as we can.
  root.initF1().setF(true);
  root.initF1Offset().setF(true);
  root.setBit(true);
  root.initF8().setF0(true);
  root.getF8().setF1(true);
  root.getF8().setF2(true);
  root.initF16().setF0(0xffu);
  root.getF16().setF1(0xffu);
  root.initF32().setF0(0xffu);
  root.getF32().setF1(0xffffu);
  root.initF64().setF0(0xffu);
  root.getF64().setF1(0xffffffffu);
  root.initF128().setF0(0xffffffffffffffffull);
  root.getF128().setF1(0xffffffffffffffffull);
  root.initF192().setF0(0xffffffffffffffffull);
  root.getF192().setF1(0xffffffffffffffffull);
  root.getF192().setF2(0xffffffffffffffffull);

  root.initF0p().setP0("foo");
  root.initF1p().setP0("foo");
  root.getF1p().initF().setF(true);
  root.initF8p().setP0("foo");
  root.initF16p().setP0("foo");
  root.getF16p().setP1("foo");
  root.initF32p().setP0("foo");
  root.getF32p().setP1("foo");
  root.initF64p().setP0("foo");
  root.getF64p().setP1("foo");
  root.initF128p().setP0("foo");
  root.getF128p().setP1("foo");
  root.getF128p().setP2("foo");
  root.initF192p().setP0("foo");
  root.getF192p().setP1("foo");
  root.getF192p().setP2("foo");

  // Now try re-initializing each thing and making sure the surrounding things aren't modified.
  EXPECT_FALSE(root.initF1().getF());
  EXPECT_TRUE(root.getF1Offset().getF());
  root.getF1().setF(true);

  EXPECT_FALSE(root.initF1Offset().getF());
  EXPECT_TRUE(root.getF1().getF());
  EXPECT_TRUE(root.getBit());
  EXPECT_TRUE(root.getF8().getF0());
  root.getF1Offset().setF(true);

  EXPECT_FALSE(root.initF8().getF0());
  EXPECT_FALSE(root.getF8().getF1());
  EXPECT_FALSE(root.getF8().getF2());
  EXPECT_TRUE(root.getF1().getF());
  EXPECT_TRUE(root.getBit());
  EXPECT_EQ(0xffu, root.getF16().getF0());
  root.initF8().setF0(true);
  root.getF8().setF1(true);
  root.getF8().setF2(true);

  EXPECT_EQ(0u, root.initF16().getF0());
  EXPECT_EQ(0u, root.getF16().getF1());
  EXPECT_TRUE(root.getF8().getF0());
  EXPECT_TRUE(root.getF8().getF1());
  EXPECT_TRUE(root.getF8().getF2());
  EXPECT_EQ(0xffu, root.getF32().getF0());
  root.getF16().setF0(0xffu);
  root.getF16().setF1(0xffu);

  EXPECT_EQ(0u, root.initF32().getF0());
  EXPECT_EQ(0u, root.getF32().getF1());
  EXPECT_EQ(0xffu, root.getF16().getF0());
  EXPECT_EQ(0xffu, root.getF16().getF1());
  EXPECT_EQ(0xffu, root.getF64().getF0());
  root.getF32().setF0(0xffu);
  root.getF32().setF1(0xffffu);

  EXPECT_EQ(0u, root.initF64().getF0());
  EXPECT_EQ(0u, root.getF64().getF1());
  EXPECT_EQ(0xffu, root.getF32().getF0());
  EXPECT_EQ(0xffffu, root.getF32().getF1());
  EXPECT_EQ(0xffffffffffffffffull, root.getF128().getF0());
  root.getF64().setF0(0xffu);
  root.getF64().setF1(0xffffffffu);

  EXPECT_EQ(0u, root.initF128().getF0());
  EXPECT_EQ(0u, root.getF128().getF1());
  EXPECT_EQ(0xffu, root.getF64().getF0());
  EXPECT_EQ(0xffffffffu, root.getF64().getF1());
  EXPECT_EQ(0xffffffffffffffffull, root.getF192().getF0());
  root.getF128().setF0(0xffffffffffffffffull);
  root.getF128().setF1(0xffffffffffffffffull);

  EXPECT_EQ(0u, root.initF192().getF0());
  EXPECT_EQ(0u, root.getF192().getF1());
  EXPECT_EQ(0u, root.getF192().getF2());
  EXPECT_EQ(0xffffffffffffffffull, root.getF128().getF0());
  EXPECT_EQ(0xffffffffffffffffull, root.getF128().getF1());
  EXPECT_TRUE(root.getF1p().getF().getF());
  root.getF192().setF0(0xffffffffffffffffull);
  root.getF192().setF1(0xffffffffffffffffull);
  root.getF192().setF2(0xffffffffffffffffull);

  EXPECT_EQ("", root.initF0p().getP0());
  EXPECT_EQ("foo", root.getF1p().getP0());
  root.getF0p().setP0("foo");

  EXPECT_EQ("", root.initF1p().getP0());
  EXPECT_EQ("foo", root.getF0p().getP0());
  EXPECT_EQ("foo", root.getF8p().getP0());
  root.getF1p().setP0("foo");

  EXPECT_EQ("", root.initF8p().getP0());
  EXPECT_EQ("foo", root.getF1p().getP0());
  EXPECT_EQ("foo", root.getF16p().getP0());
  root.initF8p().setP0("foo");

  EXPECT_EQ("", root.initF16p().getP0());
  EXPECT_EQ("", root.getF16p().getP1());
  EXPECT_EQ("foo", root.getF8p().getP0());
  EXPECT_EQ("foo", root.getF32p().getP0());
  root.initF16p().setP0("foo");
  root.getF16p().setP1("foo");

  EXPECT_EQ("", root.initF32p().getP0());
  EXPECT_EQ("", root.getF32p().getP1());
  EXPECT_EQ("foo", root.getF16p().getP1());
  EXPECT_EQ("foo", root.getF64p().getP0());
  root.initF32p().setP0("foo");
  root.getF32p().setP1("foo");

  EXPECT_EQ("", root.initF64p().getP0());
  EXPECT_EQ("", root.getF64p().getP1());
  EXPECT_EQ("foo", root.getF32p().getP1());
  EXPECT_EQ("foo", root.getF128p().getP0());
  root.initF64p().setP0("foo");
  root.getF64p().setP1("foo");

  EXPECT_EQ("", root.initF128p().getP0());
  EXPECT_EQ("", root.getF128p().getP1());
  EXPECT_EQ("", root.getF128p().getP2());
  EXPECT_EQ("foo", root.getF64p().getP1());
  EXPECT_EQ("foo", root.getF192p().getP0());
  root.initF128p().setP0("foo");
  root.getF128p().setP1("foo");
  root.getF128p().setP2("foo");

  EXPECT_EQ("", root.initF192p().getP0());
  EXPECT_EQ("", root.getF192p().getP1());
  EXPECT_EQ("", root.getF192p().getP2());
  EXPECT_EQ("foo", root.getF128p().getP2());
  root.initF192p().setP0("foo");
  root.getF192p().setP1("foo");
  root.getF192p().setP2("foo");
}

TEST(Encoding, InlineDefaults) {
  MallocMessageBuilder builder;
  TestInlineDefaults::Reader reader = builder.getRoot<TestInlineDefaults>().asReader();

  {
    auto normal = reader.getNormal();
    EXPECT_TRUE(normal.getF1().getF());
    EXPECT_TRUE(normal.getF8().getF0());
    EXPECT_FALSE(normal.getF8().getF1());
    EXPECT_TRUE(normal.getF8().getF2());
    EXPECT_EQ(123u, normal.getF16().getF0());
    EXPECT_EQ(45u, normal.getF16().getF1());
    EXPECT_EQ(67u, normal.getF32().getF0());
    EXPECT_EQ(8901u, normal.getF32().getF1());
    EXPECT_EQ(234u, normal.getF64().getF0());
    EXPECT_EQ(567890123u, normal.getF64().getF1());
    EXPECT_EQ(1234567890123ull, normal.getF128().getF0());
    EXPECT_EQ(4567890123456ull, normal.getF128().getF1());
    EXPECT_EQ(7890123456789ull, normal.getF192().getF0());
    EXPECT_EQ(2345678901234ull, normal.getF192().getF1());
    EXPECT_EQ(5678901234567ull, normal.getF192().getF2());

    EXPECT_FALSE(normal.getF1p().getF().getF());
    EXPECT_TRUE(normal.getF8p().getF().getF0());
    EXPECT_TRUE(normal.getF8p().getF().getF1());
    EXPECT_FALSE(normal.getF8p().getF().getF2());
    EXPECT_EQ(98u, normal.getF16p().getF().getF0());
    EXPECT_EQ(76u, normal.getF16p().getF().getF1());
    EXPECT_EQ(54u, normal.getF32p().getF().getF0());
    EXPECT_EQ(32109u, normal.getF32p().getF().getF1());
    EXPECT_EQ(87u, normal.getF64p().getF().getF0());
    EXPECT_EQ(654321098u, normal.getF64p().getF().getF1());
    EXPECT_EQ(7654321098765ull, normal.getF128p().getF().getF0());
    EXPECT_EQ(4321098765432ull, normal.getF128p().getF().getF1());
    EXPECT_EQ(1098765432109ull, normal.getF192p().getF().getF0());
    EXPECT_EQ(8765432109876ull, normal.getF192p().getF().getF1());
    EXPECT_EQ(5432109876543ull, normal.getF192p().getF().getF2());

    EXPECT_EQ("foo", normal.getF0p().getP0());
    EXPECT_EQ("bar", normal.getF1p().getP0());
    EXPECT_EQ("baz", normal.getF8p().getP0());
    EXPECT_EQ("qux", normal.getF16p().getP0());
    EXPECT_EQ("quux", normal.getF16p().getP1());
    EXPECT_EQ("corge", normal.getF32p().getP0());
    EXPECT_EQ("grault", normal.getF32p().getP1());
    EXPECT_EQ("garply", normal.getF64p().getP0());
    EXPECT_EQ("waldo", normal.getF64p().getP1());
    EXPECT_EQ("fred", normal.getF128p().getP0());
    EXPECT_EQ("plugh", normal.getF128p().getP1());
    EXPECT_EQ("xyzzy", normal.getF128p().getP2());
    EXPECT_EQ("thud", normal.getF192p().getP0());
    EXPECT_EQ("foobar", normal.getF192p().getP1());
    EXPECT_EQ("barbaz", normal.getF192p().getP2());
  }

  {
    auto unions = reader.getUnions();

    ASSERT_EQ(TestInlineUnions::Union0::F32, unions.getUnion0().which());
    EXPECT_EQ(67u, unions.getUnion0().getF32().getF0());
    EXPECT_EQ(8901u, unions.getUnion0().getF32().getF1());

    ASSERT_EQ(TestInlineUnions::Union1::F128, unions.getUnion1().which());
    EXPECT_EQ(1234567890123ull, unions.getUnion1().getF128().getF0());
    EXPECT_EQ(4567890123456ull, unions.getUnion1().getF128().getF1());

    ASSERT_EQ(TestInlineUnions::Union2::F1P, unions.getUnion2().which());
    EXPECT_EQ("foo", unions.getUnion2().getF1p().getP0());

    ASSERT_EQ(TestInlineUnions::Union3::F16P, unions.getUnion3().which());
    EXPECT_EQ(98u, unions.getUnion3().getF16p().getF().getF0());
    EXPECT_EQ(76u, unions.getUnion3().getF16p().getF().getF1());
    EXPECT_EQ("qux", unions.getUnion3().getF16p().getP0());
    EXPECT_EQ("quux", unions.getUnion3().getF16p().getP1());
  }
}

// =======================================================================================
// Tests of generated code, not really of the encoding.
// TODO(cleanup):  Move to a different test?

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

TEST(Encoding, Using) {
  MallocMessageBuilder builder;
  TestUsing::Reader reader = builder.getRoot<TestUsing>().asReader();
  EXPECT_EQ(TestNestedTypes::NestedEnum::BAR, reader.getOuterNestedEnum());
  EXPECT_EQ(TestNestedTypes::NestedStruct::NestedEnum::QUUX, reader.getInnerNestedEnum());
}

}  // namespace
}  // namespace internal
}  // namespace capnproto
