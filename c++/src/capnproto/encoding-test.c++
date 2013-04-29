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

template <typename T, typename U>
void checkList(T reader, std::initializer_list<U> expected) {
  ASSERT_EQ(expected.size(), reader.size());
  for (uint i = 0; i < expected.size(); i++) {
    EXPECT_EQ(expected.begin()[i], reader[i]);
  }
}

template <typename T>
void checkList(T reader, std::initializer_list<float> expected) {
  ASSERT_EQ(expected.size(), reader.size());
  for (uint i = 0; i < expected.size(); i++) {
    EXPECT_FLOAT_EQ(expected.begin()[i], reader[i]);
  }
}

template <typename T>
void checkList(T reader, std::initializer_list<double> expected) {
  ASSERT_EQ(expected.size(), reader.size());
  for (uint i = 0; i < expected.size(); i++) {
    EXPECT_DOUBLE_EQ(expected.begin()[i], reader[i]);
  }
}

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
  EXPECT_EQ(UnionState({ 2,0,0,0},   0), INIT_UNION(getUnion0().initF8().setF0(true)));
  EXPECT_EQ(UnionState({ 3,0,0,0},   0), INIT_UNION(getUnion0().initF16().setF0(1)));
  EXPECT_EQ(UnionState({ 4,0,0,0},   0), INIT_UNION(getUnion0().initF32().setF0(1)));
  EXPECT_EQ(UnionState({ 5,0,0,0},   0), INIT_UNION(getUnion0().initF64().setF0(1)));
  EXPECT_EQ(UnionState({ 6,0,0,0},   0), INIT_UNION(getUnion0().initF128().setF0(1)));
  EXPECT_EQ(UnionState({ 7,0,0,0},   0), INIT_UNION(getUnion0().initF192().setF0(1)));

  EXPECT_EQ(UnionState({ 8,0,0,0},  -1), INIT_UNION(getUnion0().initF0p().initF()));
  EXPECT_EQ(UnionState({10,0,0,0},   0), INIT_UNION(getUnion0().initF8p().initF().setF0(true)));
  EXPECT_EQ(UnionState({11,0,0,0},   0), INIT_UNION(getUnion0().initF16p().initF().setF0(1)));
  EXPECT_EQ(UnionState({12,0,0,0},   0), INIT_UNION(getUnion0().initF32p().initF().setF0(1)));
  EXPECT_EQ(UnionState({13,0,0,0},   0), INIT_UNION(getUnion0().initF64p().initF().setF0(1)));
  EXPECT_EQ(UnionState({14,0,0,0},   0), INIT_UNION(getUnion0().initF128p().initF().setF0(1)));
  EXPECT_EQ(UnionState({15,0,0,0},   0), INIT_UNION(getUnion0().initF192p().initF().setF0(1)));

  EXPECT_EQ(UnionState({ 8,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF0p().setP0("1")));
  EXPECT_EQ(UnionState({10,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF8p().setP0("1")));
  EXPECT_EQ(UnionState({11,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF16p().setP0("1")));
  EXPECT_EQ(UnionState({12,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF32p().setP0("1")));
  EXPECT_EQ(UnionState({13,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF64p().setP0("1")));
  EXPECT_EQ(UnionState({14,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF128p().setP0("1")));
  EXPECT_EQ(UnionState({15,0,0,0}, ptr(0)), INIT_UNION(getUnion0().initF192p().setP0("1")));

  EXPECT_EQ(UnionState({0, 0,0,0},  -1), INIT_UNION(getUnion1().initF0()));
  EXPECT_EQ(UnionState({0, 2,0,0}, 200), INIT_UNION(getUnion1().initF8().setF0(true)));
  EXPECT_EQ(UnionState({0, 3,0,0}, 208), INIT_UNION(getUnion1().initF16().setF0(1)));
  EXPECT_EQ(UnionState({0, 4,0,0}, 224), INIT_UNION(getUnion1().initF32().setF0(1)));
  EXPECT_EQ(UnionState({0, 5,0,0}, 256), INIT_UNION(getUnion1().initF64().setF0(1)));
  EXPECT_EQ(UnionState({0, 6,0,0}, 256), INIT_UNION(getUnion1().initF128().setF0(1)));
  EXPECT_EQ(UnionState({0, 7,0,0}, 256), INIT_UNION(getUnion1().initF192().setF0(1)));

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
  root.setPad1(0xffu);
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
  root.setPad2(0xffu);
  root.setPadP("foo");
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
  EXPECT_FALSE(root.initF8().getF0());
  EXPECT_FALSE(root.getF8().getF1());
  EXPECT_FALSE(root.getF8().getF2());
  EXPECT_EQ(0xffu, root.getPad1());
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
  EXPECT_EQ(0xffu, root.getPad2());
  root.getF192().setF0(0xffffffffffffffffull);
  root.getF192().setF1(0xffffffffffffffffull);
  root.getF192().setF2(0xffffffffffffffffull);

  EXPECT_EQ("", root.initF0p().getP0());
  EXPECT_EQ("foo", root.getPadP());
  root.getF0p().setP0("foo");

  EXPECT_EQ("", root.initF8p().getP0());
  EXPECT_EQ("foo", root.getPadP());
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
  TestInlineDefaults::Builder root = builder.getRoot<TestInlineDefaults>();

  checkTestMessage(root.asReader());
  checkTestMessage(root);
  checkTestMessage(root.asReader());
}

TEST(Encoding, BuildInlineDefaults) {
  MallocMessageBuilder builder;
  TestInlineDefaults::Builder root = builder.getRoot<TestInlineDefaults>();

  initTestMessage(root);
  checkTestMessage(root.asReader());
  checkTestMessage(root);
  checkTestMessage(root.asReader());
}

TEST(Encoding, InlineNotPresent) {
  // Test that getting an inline field of a zero-sized struct works.  This simulates what happens
  // when a new inline field is added to a struct, then old data lacking that field is received.

  AlignedData<1> emptyMessage = {{0, 0, 0, 0, 0, 0, 0, 0}};
  ArrayPtr<const word> segments[1] = {arrayPtr(emptyMessage.words, 1)};
  SegmentArrayMessageReader reader(arrayPtr(segments, 1));

  {
    auto normal = reader.getRoot<test::TestInlineLayout>();

    EXPECT_FALSE(normal.getF8().getF0());
    EXPECT_FALSE(normal.getF8().getF1());
    EXPECT_FALSE(normal.getF8().getF2());
    EXPECT_EQ(0u, normal.getF16().getF0());
    EXPECT_EQ(0u, normal.getF16().getF1());
    EXPECT_EQ(0u, normal.getF32().getF0());
    EXPECT_EQ(0u, normal.getF32().getF1());
    EXPECT_EQ(0u, normal.getF64().getF0());
    EXPECT_EQ(0u, normal.getF64().getF1());
    EXPECT_EQ(0u, normal.getF128().getF0());
    EXPECT_EQ(0u, normal.getF128().getF1());
    EXPECT_EQ(0u, normal.getF192().getF0());
    EXPECT_EQ(0u, normal.getF192().getF1());
    EXPECT_EQ(0u, normal.getF192().getF2());

    EXPECT_FALSE(normal.getF8p().getF().getF0());
    EXPECT_FALSE(normal.getF8p().getF().getF1());
    EXPECT_FALSE(normal.getF8p().getF().getF2());
    EXPECT_EQ(0u, normal.getF16p().getF().getF0());
    EXPECT_EQ(0u, normal.getF16p().getF().getF1());
    EXPECT_EQ(0u, normal.getF32p().getF().getF0());
    EXPECT_EQ(0u, normal.getF32p().getF().getF1());
    EXPECT_EQ(0u, normal.getF64p().getF().getF0());
    EXPECT_EQ(0u, normal.getF64p().getF().getF1());
    EXPECT_EQ(0u, normal.getF128p().getF().getF0());
    EXPECT_EQ(0u, normal.getF128p().getF().getF1());
    EXPECT_EQ(0u, normal.getF192p().getF().getF0());
    EXPECT_EQ(0u, normal.getF192p().getF().getF1());
    EXPECT_EQ(0u, normal.getF192p().getF().getF2());

    EXPECT_EQ("", normal.getF0p().getP0());
    EXPECT_EQ("", normal.getF8p().getP0());
    EXPECT_EQ("", normal.getF16p().getP0());
    EXPECT_EQ("", normal.getF16p().getP1());
    EXPECT_EQ("", normal.getF32p().getP0());
    EXPECT_EQ("", normal.getF32p().getP1());
    EXPECT_EQ("", normal.getF64p().getP0());
    EXPECT_EQ("", normal.getF64p().getP1());
    EXPECT_EQ("", normal.getF128p().getP0());
    EXPECT_EQ("", normal.getF128p().getP1());
    EXPECT_EQ("", normal.getF128p().getP2());
    EXPECT_EQ("", normal.getF192p().getP0());
    EXPECT_EQ("", normal.getF192p().getP1());
    EXPECT_EQ("", normal.getF192p().getP2());
  }

  {
    auto lists = reader.getRoot<test::TestInlineLists>();

    // Inline lists have fixed size even when "empty".
    ASSERT_EQ(2u, lists.getVoidList().size());
    ASSERT_EQ(3u, lists.getBoolList().size());
    ASSERT_EQ(4u, lists.getUInt8List().size());
    ASSERT_EQ(5u, lists.getUInt16List().size());
    ASSERT_EQ(6u, lists.getUInt32List().size());
    ASSERT_EQ(7u, lists.getUInt64List().size());
    ASSERT_EQ(8u, lists.getTextList().size());

    ASSERT_EQ(2u, lists.getStructList0().size());
    ASSERT_EQ(3u, lists.getStructList1().size());
    ASSERT_EQ(4u, lists.getStructList8().size());
    ASSERT_EQ(2u, lists.getStructList16().size());
    ASSERT_EQ(3u, lists.getStructList32().size());
    ASSERT_EQ(4u, lists.getStructList64().size());
    ASSERT_EQ(2u, lists.getStructList128().size());
    ASSERT_EQ(3u, lists.getStructList192().size());

    ASSERT_EQ(4u, lists.getStructList0p().size());
    ASSERT_EQ(2u, lists.getStructList1p().size());
    ASSERT_EQ(3u, lists.getStructList8p().size());
    ASSERT_EQ(4u, lists.getStructList16p().size());
    ASSERT_EQ(2u, lists.getStructList32p().size());
    ASSERT_EQ(3u, lists.getStructList64p().size());
    ASSERT_EQ(4u, lists.getStructList128p().size());
    ASSERT_EQ(2u, lists.getStructList192p().size());

    EXPECT_EQ(Void::VOID, lists.getVoidList()[0]);
    EXPECT_EQ(Void::VOID, lists.getVoidList()[1]);
    EXPECT_FALSE(lists.getBoolList()[0]);
    EXPECT_FALSE(lists.getBoolList()[1]);
    EXPECT_FALSE(lists.getBoolList()[2]);
    EXPECT_EQ(0u, lists.getUInt8List()[0]);
    EXPECT_EQ(0u, lists.getUInt8List()[1]);
    EXPECT_EQ(0u, lists.getUInt8List()[2]);
    EXPECT_EQ(0u, lists.getUInt8List()[3]);
    EXPECT_EQ(0u, lists.getUInt16List()[0]);
    EXPECT_EQ(0u, lists.getUInt16List()[1]);
    EXPECT_EQ(0u, lists.getUInt16List()[2]);
    EXPECT_EQ(0u, lists.getUInt16List()[3]);
    EXPECT_EQ(0u, lists.getUInt16List()[4]);
    EXPECT_EQ(0u, lists.getUInt32List()[0]);
    EXPECT_EQ(0u, lists.getUInt32List()[1]);
    EXPECT_EQ(0u, lists.getUInt32List()[2]);
    EXPECT_EQ(0u, lists.getUInt32List()[3]);
    EXPECT_EQ(0u, lists.getUInt32List()[4]);
    EXPECT_EQ(0u, lists.getUInt32List()[5]);
    for (uint i = 0; i < 7; i++) {
      EXPECT_EQ(0u, lists.getUInt64List()[i]);
    }
    EXPECT_EQ("", lists.getTextList()[0]);
    EXPECT_EQ("", lists.getTextList()[1]);
    EXPECT_EQ("", lists.getTextList()[2]);
    EXPECT_EQ("", lists.getTextList()[3]);
    EXPECT_EQ("", lists.getTextList()[4]);
    EXPECT_EQ("", lists.getTextList()[5]);
    EXPECT_EQ("", lists.getTextList()[6]);
    EXPECT_EQ("", lists.getTextList()[7]);

    EXPECT_EQ(Void::VOID, lists.getStructList0()[0].getF());
    EXPECT_EQ(Void::VOID, lists.getStructList0()[1].getF());

    EXPECT_FALSE(lists.getStructList8()[0].getF0());
    EXPECT_FALSE(lists.getStructList8()[0].getF1());
    EXPECT_FALSE(lists.getStructList8()[0].getF2());
    EXPECT_FALSE(lists.getStructList8()[1].getF0());
    EXPECT_FALSE(lists.getStructList8()[1].getF1());
    EXPECT_FALSE(lists.getStructList8()[1].getF2());
    EXPECT_FALSE(lists.getStructList8()[2].getF0());
    EXPECT_FALSE(lists.getStructList8()[2].getF1());
    EXPECT_FALSE(lists.getStructList8()[2].getF2());
    EXPECT_FALSE(lists.getStructList8()[3].getF0());
    EXPECT_FALSE(lists.getStructList8()[3].getF1());
    EXPECT_FALSE(lists.getStructList8()[3].getF2());

    EXPECT_EQ(0u, lists.getStructList16()[0].getF0());
    EXPECT_EQ(0u, lists.getStructList16()[0].getF1());
    EXPECT_EQ(0u, lists.getStructList16()[1].getF0());
    EXPECT_EQ(0u, lists.getStructList16()[1].getF1());

    EXPECT_EQ(0u, lists.getStructList32()[0].getF0());
    EXPECT_EQ(0u, lists.getStructList32()[0].getF1());
    EXPECT_EQ(0u, lists.getStructList32()[1].getF0());
    EXPECT_EQ(0u, lists.getStructList32()[1].getF1());
    EXPECT_EQ(0u, lists.getStructList32()[2].getF0());
    EXPECT_EQ(0u, lists.getStructList32()[2].getF1());

    EXPECT_EQ(0u, lists.getStructList64()[0].getF0());
    EXPECT_EQ(0u, lists.getStructList64()[0].getF1());
    EXPECT_EQ(0u, lists.getStructList64()[1].getF0());
    EXPECT_EQ(0u, lists.getStructList64()[1].getF1());
    EXPECT_EQ(0u, lists.getStructList64()[2].getF0());
    EXPECT_EQ(0u, lists.getStructList64()[2].getF1());
    EXPECT_EQ(0u, lists.getStructList64()[3].getF0());
    EXPECT_EQ(0u, lists.getStructList64()[3].getF1());

    EXPECT_EQ(0ull, lists.getStructList128()[0].getF0());
    EXPECT_EQ(0ull, lists.getStructList128()[0].getF1());
    EXPECT_EQ(0ull, lists.getStructList128()[1].getF0());
    EXPECT_EQ(0ull, lists.getStructList128()[1].getF1());

    EXPECT_EQ(0ull, lists.getStructList192()[0].getF0());
    EXPECT_EQ(0ull, lists.getStructList192()[0].getF1());
    EXPECT_EQ(0ull, lists.getStructList192()[0].getF2());
    EXPECT_EQ(0ull, lists.getStructList192()[1].getF0());
    EXPECT_EQ(0ull, lists.getStructList192()[1].getF1());
    EXPECT_EQ(0ull, lists.getStructList192()[1].getF2());
    EXPECT_EQ(0ull, lists.getStructList192()[2].getF0());
    EXPECT_EQ(0ull, lists.getStructList192()[2].getF1());
    EXPECT_EQ(0ull, lists.getStructList192()[2].getF2());

    EXPECT_EQ("", lists.getStructList0p()[0].getP0());
    EXPECT_EQ("", lists.getStructList0p()[1].getP0());
    EXPECT_EQ("", lists.getStructList0p()[2].getP0());
    EXPECT_EQ("", lists.getStructList0p()[3].getP0());

    EXPECT_FALSE(lists.getStructList8p()[0].getF().getF0());
    EXPECT_EQ("", lists.getStructList8p()[0].getP0());
    EXPECT_EQ("", lists.getStructList8p()[1].getP0());
    EXPECT_EQ("", lists.getStructList8p()[2].getP0());

    EXPECT_EQ(0u, lists.getStructList16p()[0].getF().getF0());
    EXPECT_EQ("", lists.getStructList16p()[0].getP0());
    EXPECT_EQ("", lists.getStructList16p()[0].getP1());
    EXPECT_EQ("", lists.getStructList16p()[1].getP0());
    EXPECT_EQ("", lists.getStructList16p()[1].getP1());
    EXPECT_EQ("", lists.getStructList16p()[2].getP0());
    EXPECT_EQ("", lists.getStructList16p()[2].getP1());
    EXPECT_EQ("", lists.getStructList16p()[3].getP0());
    EXPECT_EQ("", lists.getStructList16p()[3].getP1());

    EXPECT_EQ(0u, lists.getStructList32p()[0].getF().getF1());
    EXPECT_EQ("", lists.getStructList32p()[0].getP0());
    EXPECT_EQ("", lists.getStructList32p()[0].getP1());
    EXPECT_EQ("", lists.getStructList32p()[1].getP0());
    EXPECT_EQ("", lists.getStructList32p()[1].getP1());

    EXPECT_EQ(0u, lists.getStructList64p()[0].getF().getF1());
    EXPECT_EQ("", lists.getStructList64p()[0].getP0());
    EXPECT_EQ("", lists.getStructList64p()[0].getP1());
    EXPECT_EQ("", lists.getStructList64p()[1].getP0());
    EXPECT_EQ("", lists.getStructList64p()[1].getP1());
    EXPECT_EQ("", lists.getStructList64p()[2].getP0());
    EXPECT_EQ("", lists.getStructList64p()[2].getP1());

    EXPECT_EQ(0ull, lists.getStructList128p()[0].getF().getF1());
    EXPECT_EQ("", lists.getStructList128p()[0].getP0());
    EXPECT_EQ("", lists.getStructList128p()[0].getP1());
    EXPECT_EQ("", lists.getStructList128p()[0].getP2());
    EXPECT_EQ("", lists.getStructList128p()[1].getP0());
    EXPECT_EQ("", lists.getStructList128p()[1].getP1());
    EXPECT_EQ("", lists.getStructList128p()[1].getP2());
    EXPECT_EQ("", lists.getStructList128p()[2].getP0());
    EXPECT_EQ("", lists.getStructList128p()[2].getP1());
    EXPECT_EQ("", lists.getStructList128p()[2].getP2());
    EXPECT_EQ("", lists.getStructList128p()[3].getP0());
    EXPECT_EQ("", lists.getStructList128p()[3].getP1());
    EXPECT_EQ("", lists.getStructList128p()[3].getP2());

    EXPECT_EQ(0ull, lists.getStructList192p()[0].getF().getF2());
    EXPECT_EQ("", lists.getStructList192p()[0].getP0());
    EXPECT_EQ("", lists.getStructList192p()[0].getP1());
    EXPECT_EQ("", lists.getStructList192p()[0].getP2());
    EXPECT_EQ("", lists.getStructList192p()[1].getP0());
    EXPECT_EQ("", lists.getStructList192p()[1].getP1());
    EXPECT_EQ("", lists.getStructList192p()[1].getP2());

    EXPECT_EQ(Data::Reader(std::string(5, '\0')), lists.getData());
  }
}

TEST(Encoding, SmallStructLists) {
  // In this test, we will manually initialize TestInlineDefaults.structLists to match the default
  // value and verify that we end up with the same encoding that the compiler produces.

  MallocMessageBuilder builder;
  auto root = builder.getRoot<TestInlineDefaults>();
  auto sl = root.initStructLists();

  // Verify that all the lists are actually empty.
  EXPECT_EQ(0u, sl.getList0 ().size());
  EXPECT_EQ(0u, sl.getList1 ().size());
  EXPECT_EQ(0u, sl.getList8 ().size());
  EXPECT_EQ(0u, sl.getList16().size());
  EXPECT_EQ(0u, sl.getList32().size());
  EXPECT_EQ(0u, sl.getList64().size());
  EXPECT_EQ(0u, sl.getListP ().size());

  { auto l = sl.initList0 (2); l[0].setF(Void::VOID);        l[1].setF(Void::VOID); }
  { auto l = sl.initList1 (2); l[0].setF(true);              l[1].setF(false); }
  { auto l = sl.initList8 (2); l[0].setF(123u);              l[1].setF(45u); }
  { auto l = sl.initList16(2); l[0].setF(12345u);            l[1].setF(6789u); }
  { auto l = sl.initList32(2); l[0].setF(123456789u);        l[1].setF(234567890u); }
  { auto l = sl.initList64(2); l[0].setF(1234567890123456u); l[1].setF(2345678901234567u); }
  { auto l = sl.initListP (2); l[0].setF("foo");             l[1].setF("bar"); }

  ArrayPtr<const word> segment = builder.getSegmentsForOutput()[0];

  // Initialize another message such that it copies the default value for that field.
  MallocMessageBuilder defaultBuilder;
  defaultBuilder.getRoot<TestInlineDefaults>().getStructLists();
  ArrayPtr<const word> defaultSegment = defaultBuilder.getSegmentsForOutput()[0];

  // Should match...
  EXPECT_EQ(defaultSegment.size(), segment.size());

  for (size_t i = 0; i < std::min(segment.size(), defaultSegment.size()); i++) {
    EXPECT_EQ(reinterpret_cast<const uint64_t*>(defaultSegment.begin())[i],
              reinterpret_cast<const uint64_t*>(segment.begin())[i]);
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
