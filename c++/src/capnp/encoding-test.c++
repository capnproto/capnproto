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

#include <capnp/test-import.capnp.h>
#include <capnp/test-import2.capnp.h>
#include "message.h"
#include <kj/debug.h>
#include <kj/compat/gtest.h>
#include "test-util.h"
#include "schema-lite.h"
#include "serialize-packed.h"

namespace capnp {
namespace _ {  // private
namespace {

TEST(Encoding, AllTypes) {
  MallocMessageBuilder builder;

  initTestMessage(builder.initRoot<TestAllTypes>());
  checkTestMessage(builder.getRoot<TestAllTypes>());
  checkTestMessage(builder.getRoot<TestAllTypes>().asReader());

  SegmentArrayMessageReader reader(builder.getSegmentsForOutput());

  checkTestMessage(reader.getRoot<TestAllTypes>());

  ASSERT_EQ(1u, builder.getSegmentsForOutput().size());

  checkTestMessage(readMessageUnchecked<TestAllTypes>(builder.getSegmentsForOutput()[0].begin()));

  EXPECT_EQ(builder.getSegmentsForOutput()[0].size() - 1,  // -1 for root pointer
            reader.getRoot<TestAllTypes>().totalSize().wordCount);
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
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(nullRoot.words, 1)};
  SegmentArrayMessageReader reader(kj::arrayPtr(segments, 1));

  checkTestMessage(reader.getRoot<TestDefaults>());
  checkTestMessage(readMessageUnchecked<TestDefaults>(nullRoot.words));

  checkTestMessage(TestDefaults::Reader());
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

  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(emptyMessage.words, 1)};
  SegmentArrayMessageReader reader(kj::arrayPtr(segments, 1));

  checkTestMessage(reader.getRoot<TestDefaults>());
  checkTestMessage(readMessageUnchecked<TestDefaults>(emptyMessage.words));
}

TEST(Encoding, Unions) {
  MallocMessageBuilder builder;
  TestUnion::Builder root = builder.getRoot<TestUnion>();

  EXPECT_EQ(TestUnion::Union0::U0F0S0, root.getUnion0().which());
  EXPECT_EQ(VOID, root.getUnion0().getU0f0s0());
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
    memcpy(this->discriminants, discriminants.begin(), sizeof(this->discriminants));
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

kj::String KJ_STRINGIFY(const UnionState& us) {
  return kj::str("UnionState({", kj::strArray(us.discriminants, ", "), "}, ", us.dataOffset, ")");
}

template <typename StructType, typename Func>
UnionState initUnion(Func&& initializer) {
  // Use the given setter to initialize the given union field and then return a struct indicating
  // the location of the data that was written as well as the values of the four union
  // discriminants.

  MallocMessageBuilder builder;
  initializer(builder.getRoot<StructType>());
  kj::ArrayPtr<const word> segment = builder.getSegmentsForOutput()[0];

  KJ_ASSERT(segment.size() > 2, segment.size());

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

  EXPECT_EQ(UnionState({ 0,0,0,0},  -1), INIT_UNION(getUnion0().setU0f0s0(VOID)));
  EXPECT_EQ(UnionState({ 1,0,0,0},   0), INIT_UNION(getUnion0().setU0f0s1(1)));
  EXPECT_EQ(UnionState({ 2,0,0,0},   0), INIT_UNION(getUnion0().setU0f0s8(1)));
  EXPECT_EQ(UnionState({ 3,0,0,0},   0), INIT_UNION(getUnion0().setU0f0s16(1)));
  EXPECT_EQ(UnionState({ 4,0,0,0},   0), INIT_UNION(getUnion0().setU0f0s32(1)));
  EXPECT_EQ(UnionState({ 5,0,0,0},   0), INIT_UNION(getUnion0().setU0f0s64(1)));
  EXPECT_EQ(UnionState({ 6,0,0,0}, 448), INIT_UNION(getUnion0().setU0f0sp("1")));

  EXPECT_EQ(UnionState({ 7,0,0,0},  -1), INIT_UNION(getUnion0().setU0f1s0(VOID)));
  EXPECT_EQ(UnionState({ 8,0,0,0},   0), INIT_UNION(getUnion0().setU0f1s1(1)));
  EXPECT_EQ(UnionState({ 9,0,0,0},   0), INIT_UNION(getUnion0().setU0f1s8(1)));
  EXPECT_EQ(UnionState({10,0,0,0},   0), INIT_UNION(getUnion0().setU0f1s16(1)));
  EXPECT_EQ(UnionState({11,0,0,0},   0), INIT_UNION(getUnion0().setU0f1s32(1)));
  EXPECT_EQ(UnionState({12,0,0,0},   0), INIT_UNION(getUnion0().setU0f1s64(1)));
  EXPECT_EQ(UnionState({13,0,0,0}, 448), INIT_UNION(getUnion0().setU0f1sp("1")));

  EXPECT_EQ(UnionState({0, 0,0,0},  -1), INIT_UNION(getUnion1().setU1f0s0(VOID)));
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

  EXPECT_EQ(UnionState({0,13,0,0},  -1), INIT_UNION(getUnion1().setU1f2s0(VOID)));
  EXPECT_EQ(UnionState({0,14,0,0}, 65), INIT_UNION(getUnion1().setU1f2s1(1)));
  EXPECT_EQ(UnionState({0,15,0,0}, 72), INIT_UNION(getUnion1().setU1f2s8(1)));
  EXPECT_EQ(UnionState({0,16,0,0}, 80), INIT_UNION(getUnion1().setU1f2s16(1)));
  EXPECT_EQ(UnionState({0,17,0,0}, 96), INIT_UNION(getUnion1().setU1f2s32(1)));
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

TEST(Encoding, UnnamedUnion) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestUnnamedUnion>();
  EXPECT_EQ(test::TestUnnamedUnion::FOO, root.which());

  root.setBar(321);
  EXPECT_EQ(test::TestUnnamedUnion::BAR, root.which());
  EXPECT_EQ(test::TestUnnamedUnion::BAR, root.asReader().which());
  EXPECT_EQ(321u, root.getBar());
  EXPECT_EQ(321u, root.asReader().getBar());
  EXPECT_DEBUG_ANY_THROW(root.getFoo());
  EXPECT_DEBUG_ANY_THROW(root.asReader().getFoo());

  root.setFoo(123);
  EXPECT_EQ(test::TestUnnamedUnion::FOO, root.which());
  EXPECT_EQ(test::TestUnnamedUnion::FOO, root.asReader().which());
  EXPECT_EQ(123u, root.getFoo());
  EXPECT_EQ(123u, root.asReader().getFoo());
  EXPECT_DEBUG_ANY_THROW(root.getBar());
  EXPECT_DEBUG_ANY_THROW(root.asReader().getBar());

#if !CAPNP_LITE
  StructSchema schema = Schema::from<test::TestUnnamedUnion>();

  // The discriminant is allocated just before allocating "bar".
  EXPECT_EQ(2u, schema.getProto().getStruct().getDiscriminantOffset());
  EXPECT_EQ(0u, schema.getFieldByName("foo").getProto().getSlot().getOffset());
  EXPECT_EQ(2u, schema.getFieldByName("bar").getProto().getSlot().getOffset());
#endif  // !CAPNP_LITE
}

TEST(Encoding, Groups) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestGroups>();

  {
    auto foo = root.getGroups().initFoo();
    foo.setCorge(12345678);
    foo.setGrault(123456789012345ll);
    foo.setGarply("foobar");

    EXPECT_EQ(12345678, foo.getCorge());
    EXPECT_EQ(123456789012345ll, foo.getGrault());
    EXPECT_EQ("foobar", foo.getGarply());
  }

  {
    auto bar = root.getGroups().initBar();
    bar.setCorge(23456789);
    bar.setGrault("barbaz");
    bar.setGarply(234567890123456ll);

    EXPECT_EQ(23456789, bar.getCorge());
    EXPECT_EQ("barbaz", bar.getGrault());
    EXPECT_EQ(234567890123456ll, bar.getGarply());
  }

  {
    auto baz = root.getGroups().initBaz();
    baz.setCorge(34567890);
    baz.setGrault("bazqux");
    baz.setGarply("quxquux");

    EXPECT_EQ(34567890, baz.getCorge());
    EXPECT_EQ("bazqux", baz.getGrault());
    EXPECT_EQ("quxquux", baz.getGarply());
  }
}

TEST(Encoding, InterleavedGroups) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestInterleavedGroups>();

  // Init both groups to different values.
  {
    auto group = root.getGroup1();
    group.setFoo(12345678u);
    group.setBar(123456789012345llu);
    auto corge = group.initCorge();
    corge.setGrault(987654321098765llu);
    corge.setGarply(12345u);
    corge.setPlugh("plugh");
    corge.setXyzzy("xyzzy");
    group.setWaldo("waldo");
  }

  {
    auto group = root.getGroup2();
    group.setFoo(23456789u);
    group.setBar(234567890123456llu);
    auto corge = group.initCorge();
    corge.setGrault(876543210987654llu);
    corge.setGarply(23456u);
    corge.setPlugh("hgulp");
    corge.setXyzzy("yzzyx");
    group.setWaldo("odlaw");
  }

  // Check group1 is still set correctly.
  {
    auto group = root.asReader().getGroup1();
    EXPECT_EQ(12345678u, group.getFoo());
    EXPECT_EQ(123456789012345llu, group.getBar());
    auto corge = group.getCorge();
    EXPECT_EQ(987654321098765llu, corge.getGrault());
    EXPECT_EQ(12345u, corge.getGarply());
    EXPECT_EQ("plugh", corge.getPlugh());
    EXPECT_EQ("xyzzy", corge.getXyzzy());
    EXPECT_EQ("waldo", group.getWaldo());
  }

  // Zero out group 1 and see if it is zero'd.
  {
    auto group = root.initGroup1().asReader();
    EXPECT_EQ(0u, group.getFoo());
    EXPECT_EQ(0u, group.getBar());
    EXPECT_EQ(test::TestInterleavedGroups::Group1::QUX, group.which());
    EXPECT_EQ(0u, group.getQux());
    EXPECT_FALSE(group.hasWaldo());
  }

  // Group 2 should not have been touched.
  {
    auto group = root.asReader().getGroup2();
    EXPECT_EQ(23456789u, group.getFoo());
    EXPECT_EQ(234567890123456llu, group.getBar());
    auto corge = group.getCorge();
    EXPECT_EQ(876543210987654llu, corge.getGrault());
    EXPECT_EQ(23456u, corge.getGarply());
    EXPECT_EQ("hgulp", corge.getPlugh());
    EXPECT_EQ("yzzyx", corge.getXyzzy());
    EXPECT_EQ("odlaw", group.getWaldo());
  }
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
    EXPECT_EQ(VOID, field.getUnion0().getU0f1s0());
    EXPECT_EQ("foo", field.getUnion1().getU1f0sp());
    EXPECT_EQ(true, field.getUnion2().getU2f0s1());
    EXPECT_EQ(12345678, field.getUnion3().getU3f0s32());
  }

  {
    auto field = reader.getUnnamed1();
    EXPECT_EQ(test::TestUnnamedUnion::FOO, field.which());
    EXPECT_EQ(123u, field.getFoo());
    EXPECT_FALSE(field.hasBefore());
    EXPECT_FALSE(field.hasAfter());
  }

  {
    auto field = reader.getUnnamed2();
    EXPECT_EQ(test::TestUnnamedUnion::BAR, field.which());
    EXPECT_EQ(321u, field.getBar());
    EXPECT_EQ("foo", field.getBefore());
    EXPECT_EQ("bar", field.getAfter());
  }
}

// =======================================================================================

TEST(Encoding, ListDefaults) {
  MallocMessageBuilder builder;
  TestListDefaults::Builder root = builder.getRoot<TestListDefaults>();

  checkTestMessage(root.asReader());
  checkTestMessage(root);
  checkTestMessage(root.asReader());
}

TEST(Encoding, BuildListDefaults) {
  MallocMessageBuilder builder;
  TestListDefaults::Builder root = builder.getRoot<TestListDefaults>();

  initTestMessage(root);
  checkTestMessage(root.asReader());
  checkTestMessage(root);
  checkTestMessage(root.asReader());
}

TEST(Encoding, SmallStructLists) {
  // In this test, we will manually initialize TestListDefaults.lists to match the default
  // value and verify that we end up with the same encoding that the compiler produces.

  MallocMessageBuilder builder;
  auto root = builder.getRoot<TestListDefaults>();
  auto sl = root.initLists();

  // Verify that all the lists are actually empty.
  EXPECT_EQ(0u, sl.getList0 ().size());
  EXPECT_EQ(0u, sl.getList1 ().size());
  EXPECT_EQ(0u, sl.getList8 ().size());
  EXPECT_EQ(0u, sl.getList16().size());
  EXPECT_EQ(0u, sl.getList32().size());
  EXPECT_EQ(0u, sl.getList64().size());
  EXPECT_EQ(0u, sl.getListP ().size());
  EXPECT_EQ(0u, sl.getInt32ListList().size());
  EXPECT_EQ(0u, sl.getTextListList().size());
  EXPECT_EQ(0u, sl.getStructListList().size());

  { auto l = sl.initList0 (2); l[0].setF(VOID);              l[1].setF(VOID); }
  { auto l = sl.initList1 (4); l[0].setF(true);              l[1].setF(false);
                               l[2].setF(true);              l[3].setF(true); }
  { auto l = sl.initList8 (2); l[0].setF(123u);              l[1].setF(45u); }
  { auto l = sl.initList16(2); l[0].setF(12345u);            l[1].setF(6789u); }
  { auto l = sl.initList32(2); l[0].setF(123456789u);        l[1].setF(234567890u); }
  { auto l = sl.initList64(2); l[0].setF(1234567890123456u); l[1].setF(2345678901234567u); }
  { auto l = sl.initListP (2); l[0].setF("foo");             l[1].setF("bar"); }

  {
    auto l = sl.initInt32ListList(3);
    l.set(0, {1, 2, 3});
    l.set(1, {4, 5});
    l.set(2, {12341234});
  }

  {
    auto l = sl.initTextListList(3);
    l.set(0, {"foo", "bar"});
    l.set(1, {"baz"});
    l.set(2, {"qux", "corge"});
  }

  {
    auto l = sl.initStructListList(2);
    l.init(0, 2);
    l.init(1, 1);

    l[0][0].setInt32Field(123);
    l[0][1].setInt32Field(456);
    l[1][0].setInt32Field(789);
  }

  kj::ArrayPtr<const word> segment = builder.getSegmentsForOutput()[0];

  // Initialize another message such that it copies the default value for that field.
  MallocMessageBuilder defaultBuilder;
  defaultBuilder.getRoot<TestListDefaults>().getLists();
  kj::ArrayPtr<const word> defaultSegment = defaultBuilder.getSegmentsForOutput()[0];

  // Should match...
  EXPECT_EQ(defaultSegment.size(), segment.size());

  for (size_t i = 0; i < kj::min(segment.size(), defaultSegment.size()); i++) {
    EXPECT_EQ(reinterpret_cast<const uint64_t*>(defaultSegment.begin())[i],
              reinterpret_cast<const uint64_t*>(segment.begin())[i]);
  }
}

TEST(Encoding, SetListToEmpty) {
  // Test initializing list fields from various ways of constructing zero-sized lists.
  // At one point this would often fail because the lists would have ElementSize::VOID which is
  // incompatible with other list sizes.

#define ALL_LIST_TYPES(MACRO) \
  MACRO(Void, Void) \
  MACRO(Bool, bool) \
  MACRO(UInt8, uint8_t) \
  MACRO(UInt16, uint16_t) \
  MACRO(UInt32, uint32_t) \
  MACRO(UInt64, uint64_t) \
  MACRO(Int8, int8_t) \
  MACRO(Int16, int16_t) \
  MACRO(Int32, int32_t) \
  MACRO(Int64, int64_t) \
  MACRO(Float32, float) \
  MACRO(Float64, double) \
  MACRO(Text, Text) \
  MACRO(Data, Data) \
  MACRO(Struct, TestAllTypes)

#define SET_FROM_READER_ACCESSOR(name, type) \
  root.set##name##List(reader.get##name##List());

#define SET_FROM_BUILDER_ACCESSOR(name, type) \
  root.set##name##List(root.get##name##List());

#define SET_FROM_READER_CONSTRUCTOR(name, type) \
  root.set##name##List(List<type>::Reader());

#define SET_FROM_BUILDER_CONSTRUCTOR(name, type) \
  root.set##name##List(List<type>::Builder());

#define CHECK_EMPTY_NONNULL(name, type) \
  EXPECT_TRUE(root.has##name##List()); \
  EXPECT_EQ(0, root.get##name##List().size());

  {
    MallocMessageBuilder builder;
    auto root = builder.initRoot<test::TestAllTypes>();
    auto reader = root.asReader();
    ALL_LIST_TYPES(SET_FROM_READER_ACCESSOR)
    ALL_LIST_TYPES(CHECK_EMPTY_NONNULL)
  }

  {
    MallocMessageBuilder builder;
    auto root = builder.initRoot<test::TestAllTypes>();
    ALL_LIST_TYPES(SET_FROM_BUILDER_ACCESSOR)
    ALL_LIST_TYPES(CHECK_EMPTY_NONNULL)
  }

  {
    MallocMessageBuilder builder;
    auto root = builder.initRoot<test::TestAllTypes>();
    ALL_LIST_TYPES(SET_FROM_READER_CONSTRUCTOR)
    ALL_LIST_TYPES(CHECK_EMPTY_NONNULL)
  }

  {
    MallocMessageBuilder builder;
    auto root = builder.initRoot<test::TestAllTypes>();
    ALL_LIST_TYPES(SET_FROM_BUILDER_CONSTRUCTOR)
    ALL_LIST_TYPES(CHECK_EMPTY_NONNULL)
  }

#undef SET_FROM_READER_ACCESSOR
#undef SET_FROM_BUILDER_ACCESSOR
#undef SET_FROM_READER_CONSTRUCTOR
#undef SET_FROM_BUILDER_CONSTRUCTOR
#undef CHECK_EMPTY_NONNULL
}

#if CAPNP_EXPENSIVE_TESTS
TEST(Encoding, LongList) {
  // This test allocates 512MB of contiguous memory and takes several seconds, so we usually don't
  // run it. It is run before release, though.

  MallocMessageBuilder builder;

  auto root = builder.initRoot<TestAllTypes>();
  uint length = 1 << 27;
  auto list = root.initUInt64List(length);
  for (uint ii = 0; ii < length; ++ii) {
    list.set(ii, ii);
  }
  for (uint ii = 0; ii < length; ++ii) {
    ASSERT_EQ(list[ii], ii);
  }
}
#endif

// =======================================================================================

TEST(Encoding, ListUpgrade) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  root.getAnyPointerField().setAs<List<uint16_t>>({12, 34, 56});

  checkList(root.getAnyPointerField().getAs<List<uint8_t>>(), {12, 34, 56});

  {
    auto l = root.getAnyPointerField().getAs<List<test::TestLists::Struct8>>();
    ASSERT_EQ(3u, l.size());
    EXPECT_EQ(12u, l[0].getF());
    EXPECT_EQ(34u, l[1].getF());
    EXPECT_EQ(56u, l[2].getF());
  }

  checkList(root.getAnyPointerField().getAs<List<uint16_t>>(), {12, 34, 56});

  auto reader = root.asReader();

  checkList(reader.getAnyPointerField().getAs<List<uint8_t>>(), {12, 34, 56});

  {
    auto l = reader.getAnyPointerField().getAs<List<test::TestLists::Struct8>>();
    ASSERT_EQ(3u, l.size());
    EXPECT_EQ(12u, l[0].getF());
    EXPECT_EQ(34u, l[1].getF());
    EXPECT_EQ(56u, l[2].getF());
  }

  root.getAnyPointerField().setAs<List<uint16_t>>({12, 34, 56});

  {
    kj::Maybe<kj::Exception> e = kj::runCatchingExceptions([&]() {
      reader.getAnyPointerField().getAs<List<uint32_t>>();
#if !KJ_NO_EXCEPTIONS
      ADD_FAILURE() << "Should have thrown an exception.";
#endif
    });

    KJ_EXPECT(e != nullptr, "Should have thrown an exception.");
  }

  {
    auto l = reader.getAnyPointerField().getAs<List<test::TestLists::Struct32>>();
    ASSERT_EQ(3u, l.size());

    // These should return default values because the structs aren't big enough.
    EXPECT_EQ(0u, l[0].getF());
    EXPECT_EQ(0u, l[1].getF());
    EXPECT_EQ(0u, l[2].getF());
  }

  checkList(reader.getAnyPointerField().getAs<List<uint16_t>>(), {12, 34, 56});
}

TEST(Encoding, BitListDowngrade) {
  // NO LONGER SUPPORTED -- We check for exceptions thrown.

  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  root.getAnyPointerField().setAs<List<uint16_t>>({0x1201u, 0x3400u, 0x5601u, 0x7801u});

  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());

  {
    auto l = root.getAnyPointerField().getAs<List<test::TestLists::Struct1>>();
    ASSERT_EQ(4u, l.size());
    EXPECT_TRUE(l[0].getF());
    EXPECT_FALSE(l[1].getF());
    EXPECT_TRUE(l[2].getF());
    EXPECT_TRUE(l[3].getF());
  }

  checkList(root.getAnyPointerField().getAs<List<uint16_t>>(),
            {0x1201u, 0x3400u, 0x5601u, 0x7801u});

  auto reader = root.asReader();

  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());

  {
    auto l = reader.getAnyPointerField().getAs<List<test::TestLists::Struct1>>();
    ASSERT_EQ(4u, l.size());
    EXPECT_TRUE(l[0].getF());
    EXPECT_FALSE(l[1].getF());
    EXPECT_TRUE(l[2].getF());
    EXPECT_TRUE(l[3].getF());
  }

  checkList(reader.getAnyPointerField().getAs<List<uint16_t>>(),
            {0x1201u, 0x3400u, 0x5601u, 0x7801u});
}

TEST(Encoding, BitListDowngradeFromStruct) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  {
    auto list = root.getAnyPointerField().initAs<List<test::TestLists::Struct1c>>(4);
    list[0].setF(true);
    list[1].setF(false);
    list[2].setF(true);
    list[3].setF(true);
  }

  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());

  {
    auto l = root.getAnyPointerField().getAs<List<test::TestLists::Struct1>>();
    ASSERT_EQ(4u, l.size());
    EXPECT_TRUE(l[0].getF());
    EXPECT_FALSE(l[1].getF());
    EXPECT_TRUE(l[2].getF());
    EXPECT_TRUE(l[3].getF());
  }

  auto reader = root.asReader();

  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());

  {
    auto l = reader.getAnyPointerField().getAs<List<test::TestLists::Struct1>>();
    ASSERT_EQ(4u, l.size());
    EXPECT_TRUE(l[0].getF());
    EXPECT_FALSE(l[1].getF());
    EXPECT_TRUE(l[2].getF());
    EXPECT_TRUE(l[3].getF());
  }
}

TEST(Encoding, BitListUpgrade) {
  // No longer supported!

  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  root.getAnyPointerField().setAs<List<bool>>({true, false, true, true});

  {
    kj::Maybe<kj::Exception> e = kj::runCatchingExceptions([&]() {
      root.getAnyPointerField().getAs<List<test::TestLists::Struct1>>();
#if !KJ_NO_EXCEPTIONS
      ADD_FAILURE() << "Should have thrown an exception.";
#endif
    });

    KJ_EXPECT(e != nullptr, "Should have thrown an exception.");
  }

  auto reader = root.asReader();

  {
    kj::Maybe<kj::Exception> e = kj::runCatchingExceptions([&]() {
      reader.getAnyPointerField().getAs<List<test::TestLists::Struct1>>();
#if !KJ_NO_EXCEPTIONS
      ADD_FAILURE() << "Should have thrown an exception.";
#endif
    });

    KJ_EXPECT(e != nullptr, "Should have thrown an exception.");
  }
}

TEST(Encoding, UpgradeStructInBuilder) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  test::TestOldVersion::Reader oldReader;

  {
    auto oldVersion = root.getAnyPointerField().initAs<test::TestOldVersion>();
    oldVersion.setOld1(123);
    oldVersion.setOld2("foo");
    auto sub = oldVersion.initOld3();
    sub.setOld1(456);
    sub.setOld2("bar");

    oldReader = oldVersion;
  }

  size_t size = builder.getSegmentsForOutput()[0].size();
  size_t size2;

  {
    auto newVersion = root.getAnyPointerField().getAs<test::TestNewVersion>();

    // The old instance should have been zero'd.
    EXPECT_EQ(0, oldReader.getOld1());
    EXPECT_EQ("", oldReader.getOld2());
    EXPECT_EQ(0, oldReader.getOld3().getOld1());
    EXPECT_EQ("", oldReader.getOld3().getOld2());

    // Size should have increased due to re-allocating the struct.
    size_t size1 = builder.getSegmentsForOutput()[0].size();
    EXPECT_GT(size1, size);

    auto sub = newVersion.getOld3();

    // Size should have increased due to re-allocating the sub-struct.
    size2 = builder.getSegmentsForOutput()[0].size();
    EXPECT_GT(size2, size1);

    // Check contents.
    EXPECT_EQ(123, newVersion.getOld1());
    EXPECT_EQ("foo", newVersion.getOld2());
    EXPECT_EQ(987, newVersion.getNew1());
    EXPECT_EQ("baz", newVersion.getNew2());

    EXPECT_EQ(456, sub.getOld1());
    EXPECT_EQ("bar", sub.getOld2());
    EXPECT_EQ(987, sub.getNew1());
    EXPECT_EQ("baz", sub.getNew2());

    newVersion.setOld1(234);
    newVersion.setOld2("qux");
    newVersion.setNew1(321);
    newVersion.setNew2("quux");

    sub.setOld1(567);
    sub.setOld2("corge");
    sub.setNew1(654);
    sub.setNew2("grault");
  }

  // We set four small text fields and implicitly initialized two to defaults, so the size should
  // have raised by six words.
  size_t size3 = builder.getSegmentsForOutput()[0].size();
  EXPECT_EQ(size2 + 6, size3);

  {
    // Go back to old version.  It should have the values set on the new version.
    auto oldVersion = root.getAnyPointerField().getAs<test::TestOldVersion>();
    EXPECT_EQ(234, oldVersion.getOld1());
    EXPECT_EQ("qux", oldVersion.getOld2());

    auto sub = oldVersion.getOld3();
    EXPECT_EQ(567, sub.getOld1());
    EXPECT_EQ("corge", sub.getOld2());

    // Overwrite the old fields.  The new fields should remain intact.
    oldVersion.setOld1(345);
    oldVersion.setOld2("garply");
    sub.setOld1(678);
    sub.setOld2("waldo");
  }

  // We set two small text fields, so the size should have raised by two words.
  size_t size4 = builder.getSegmentsForOutput()[0].size();
  EXPECT_EQ(size3 + 2, size4);

  {
    // Back to the new version again.
    auto newVersion = root.getAnyPointerField().getAs<test::TestNewVersion>();
    EXPECT_EQ(345, newVersion.getOld1());
    EXPECT_EQ("garply", newVersion.getOld2());
    EXPECT_EQ(321, newVersion.getNew1());
    EXPECT_EQ("quux", newVersion.getNew2());

    auto sub = newVersion.getOld3();
    EXPECT_EQ(678, sub.getOld1());
    EXPECT_EQ("waldo", sub.getOld2());
    EXPECT_EQ(654, sub.getNew1());
    EXPECT_EQ("grault", sub.getNew2());
  }

  // Size should not have changed because we didn't write anything and the structs were already
  // the right size.
  EXPECT_EQ(size4, builder.getSegmentsForOutput()[0].size());
}

TEST(Encoding, UpgradeStructInBuilderMultiSegment) {
  // Exactly like the previous test, except that we force multiple segments.  Since we force a
  // separate segment for every object, every pointer is a far pointer, and far pointers are easily
  // transferred, so this is actually not such a complicated case.

  MallocMessageBuilder builder(0, AllocationStrategy::FIXED_SIZE);
  auto root = builder.initRoot<test::TestAnyPointer>();

  // Start with a 1-word first segment and the root object in the second segment.
  size_t size = builder.getSegmentsForOutput().size();
  EXPECT_EQ(2u, size);

  {
    auto oldVersion = root.getAnyPointerField().initAs<test::TestOldVersion>();
    oldVersion.setOld1(123);
    oldVersion.setOld2("foo");
    auto sub = oldVersion.initOld3();
    sub.setOld1(456);
    sub.setOld2("bar");
  }

  // Allocated two structs and two strings.
  size_t size2 = builder.getSegmentsForOutput().size();
  EXPECT_EQ(size + 4, size2);

  size_t size4;

  {
    auto newVersion = root.getAnyPointerField().getAs<test::TestNewVersion>();

    // Allocated a new struct.
    size_t size3 = builder.getSegmentsForOutput().size();
    EXPECT_EQ(size2 + 1, size3);

    auto sub = newVersion.getOld3();

    // Allocated another new struct for its string field.
    size4 = builder.getSegmentsForOutput().size();
    EXPECT_EQ(size3 + 1, size4);

    // Check contents.
    EXPECT_EQ(123, newVersion.getOld1());
    EXPECT_EQ("foo", newVersion.getOld2());
    EXPECT_EQ(987, newVersion.getNew1());
    EXPECT_EQ("baz", newVersion.getNew2());

    EXPECT_EQ(456, sub.getOld1());
    EXPECT_EQ("bar", sub.getOld2());
    EXPECT_EQ(987, sub.getNew1());
    EXPECT_EQ("baz", sub.getNew2());

    newVersion.setOld1(234);
    newVersion.setOld2("qux");
    newVersion.setNew1(321);
    newVersion.setNew2("quux");

    sub.setOld1(567);
    sub.setOld2("corge");
    sub.setNew1(654);
    sub.setNew2("grault");
  }

  // Set four strings and implicitly initialized two.
  size_t size5 = builder.getSegmentsForOutput().size();
  EXPECT_EQ(size4 + 6, size5);

  {
    // Go back to old version.  It should have the values set on the new version.
    auto oldVersion = root.getAnyPointerField().getAs<test::TestOldVersion>();
    EXPECT_EQ(234, oldVersion.getOld1());
    EXPECT_EQ("qux", oldVersion.getOld2());

    auto sub = oldVersion.getOld3();
    EXPECT_EQ(567, sub.getOld1());
    EXPECT_EQ("corge", sub.getOld2());

    // Overwrite the old fields.  The new fields should remain intact.
    oldVersion.setOld1(345);
    oldVersion.setOld2("garply");
    sub.setOld1(678);
    sub.setOld2("waldo");
  }

  // Set two new strings.
  size_t size6 = builder.getSegmentsForOutput().size();
  EXPECT_EQ(size5 + 2, size6);

  {
    // Back to the new version again.
    auto newVersion = root.getAnyPointerField().getAs<test::TestNewVersion>();
    EXPECT_EQ(345, newVersion.getOld1());
    EXPECT_EQ("garply", newVersion.getOld2());
    EXPECT_EQ(321, newVersion.getNew1());
    EXPECT_EQ("quux", newVersion.getNew2());

    auto sub = newVersion.getOld3();
    EXPECT_EQ(678, sub.getOld1());
    EXPECT_EQ("waldo", sub.getOld2());
    EXPECT_EQ(654, sub.getNew1());
    EXPECT_EQ("grault", sub.getNew2());
  }

  // Size should not have changed because we didn't write anything and the structs were already
  // the right size.
  EXPECT_EQ(size6, builder.getSegmentsForOutput().size());
}

TEST(Encoding, UpgradeStructInBuilderFarPointers) {
  // Force allocation of a Far pointer.

  MallocMessageBuilder builder(7, AllocationStrategy::FIXED_SIZE);
  auto root = builder.initRoot<test::TestAnyPointer>();

  root.getAnyPointerField().initAs<test::TestOldVersion>().setOld2("foo");

  // We should have allocated all but one word of the first segment.
  EXPECT_EQ(1u, builder.getSegmentsForOutput().size());
  EXPECT_EQ(6u, builder.getSegmentsForOutput()[0].size());

  // Now if we upgrade...
  EXPECT_EQ("foo", root.getAnyPointerField().getAs<test::TestNewVersion>().getOld2());

  // We should have allocated the new struct in a new segment, but allocated the far pointer
  // landing pad back in the first segment.
  ASSERT_EQ(2u, builder.getSegmentsForOutput().size());
  EXPECT_EQ(7u, builder.getSegmentsForOutput()[0].size());
  EXPECT_EQ(6u, builder.getSegmentsForOutput()[1].size());
}

TEST(Encoding, UpgradeStructInBuilderDoubleFarPointers) {
  // Force allocation of a double-Far pointer.

  MallocMessageBuilder builder(6, AllocationStrategy::FIXED_SIZE);
  auto root = builder.initRoot<test::TestAnyPointer>();

  root.getAnyPointerField().initAs<test::TestOldVersion>().setOld2("foo");

  // We should have allocated all of the first segment.
  EXPECT_EQ(1u, builder.getSegmentsForOutput().size());
  EXPECT_EQ(6u, builder.getSegmentsForOutput()[0].size());

  // Now if we upgrade...
  EXPECT_EQ("foo", root.getAnyPointerField().getAs<test::TestNewVersion>().getOld2());

  // We should have allocated the new struct in a new segment, and also allocated the far pointer
  // landing pad in yet another segment.
  ASSERT_EQ(3u, builder.getSegmentsForOutput().size());
  EXPECT_EQ(6u, builder.getSegmentsForOutput()[0].size());
  EXPECT_EQ(6u, builder.getSegmentsForOutput()[1].size());
  EXPECT_EQ(2u, builder.getSegmentsForOutput()[2].size());
}

void checkUpgradedList(test::TestAnyPointer::Builder root,
                       std::initializer_list<int64_t> expectedData,
                       std::initializer_list<Text::Reader> expectedPointers) {
  {
    auto builder = root.getAnyPointerField().getAs<List<test::TestNewVersion>>();

    ASSERT_EQ(expectedData.size(), builder.size());
    for (uint i = 0; i < expectedData.size(); i++) {
      EXPECT_EQ(expectedData.begin()[i], builder[i].getOld1());
      EXPECT_EQ(expectedPointers.begin()[i], builder[i].getOld2());

      // Other fields shouldn't be set.
      EXPECT_EQ(0, builder[i].asReader().getOld3().getOld1());
      EXPECT_EQ("", builder[i].asReader().getOld3().getOld2());
      EXPECT_EQ(987, builder[i].getNew1());
      EXPECT_EQ("baz", builder[i].getNew2());

      // Write some new data.
      builder[i].setOld1(i * 123);
      builder[i].setOld2(kj::str("qux", i, '\0').begin());
      builder[i].setNew1(i * 456);
      builder[i].setNew2(kj::str("corge", i, '\0').begin());
    }
  }

  // Read the newly-written data as TestOldVersion to ensure it was updated.
  {
    auto builder = root.getAnyPointerField().getAs<List<test::TestOldVersion>>();

    ASSERT_EQ(expectedData.size(), builder.size());
    for (uint i = 0; i < expectedData.size(); i++) {
      EXPECT_EQ(i * 123, builder[i].getOld1());
      EXPECT_EQ(Text::Reader(kj::str("qux", i, "\0").begin()), builder[i].getOld2());
    }
  }

  // Also read back as TestNewVersion again.
  {
    auto builder = root.getAnyPointerField().getAs<List<test::TestNewVersion>>();

    ASSERT_EQ(expectedData.size(), builder.size());
    for (uint i = 0; i < expectedData.size(); i++) {
      EXPECT_EQ(i * 123, builder[i].getOld1());
      EXPECT_EQ(Text::Reader(kj::str("qux", i, '\0').begin()), builder[i].getOld2());
      EXPECT_EQ(i * 456, builder[i].getNew1());
      EXPECT_EQ(Text::Reader(kj::str("corge", i, '\0').begin()), builder[i].getNew2());
    }
  }
}

TEST(Encoding, UpgradeListInBuilder) {
  // Test every damned list upgrade.

  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  // -----------------------------------------------------------------

  root.getAnyPointerField().setAs<List<Void>>({VOID, VOID, VOID, VOID});
  checkList(root.getAnyPointerField().getAs<List<Void>>(), {VOID, VOID, VOID, VOID});
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint8_t>>());
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint16_t>>());
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint32_t>>());
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint64_t>>());
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<Text>>());
  checkUpgradedList(root, {0, 0, 0, 0}, {"", "", "", ""});

  // -----------------------------------------------------------------

  {
    root.getAnyPointerField().setAs<List<bool>>({true, false, true, true});
    auto orig = root.asReader().getAnyPointerField().getAs<List<bool>>();
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<Void>>());
    checkList(root.getAnyPointerField().getAs<List<bool>>(), {true, false, true, true});
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint8_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint16_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint32_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint64_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<Text>>());

    checkList(orig, {true, false, true, true});

    // Can't upgrade bit lists. (This used to be supported.)
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<test::TestNewVersion>>());
  }

  // -----------------------------------------------------------------

  {
    root.getAnyPointerField().setAs<List<uint8_t>>({0x12, 0x23, 0x33, 0x44});
    auto orig = root.asReader().getAnyPointerField().getAs<List<uint8_t>>();
    checkList(root.getAnyPointerField().getAs<List<Void>>(), {VOID, VOID, VOID, VOID});
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());
    checkList(root.getAnyPointerField().getAs<List<uint8_t>>(), {0x12, 0x23, 0x33, 0x44});
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint16_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint32_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint64_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<Text>>());

    checkList(orig, {0x12, 0x23, 0x33, 0x44});
    checkUpgradedList(root, {0x12, 0x23, 0x33, 0x44}, {"", "", "", ""});
    checkList(orig, {0, 0, 0, 0});  // old location zero'd during upgrade
  }

  // -----------------------------------------------------------------

  {
    root.getAnyPointerField().setAs<List<uint16_t>>({0x5612, 0x7823, 0xab33, 0xcd44});
    auto orig = root.asReader().getAnyPointerField().getAs<List<uint16_t>>();
    checkList(root.getAnyPointerField().getAs<List<Void>>(), {VOID, VOID, VOID, VOID});
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());
    checkList(root.getAnyPointerField().getAs<List<uint8_t>>(), {0x12, 0x23, 0x33, 0x44});
    checkList(root.getAnyPointerField().getAs<List<uint16_t>>(), {0x5612, 0x7823, 0xab33, 0xcd44});
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint32_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint64_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<Text>>());

    checkList(orig, {0x5612, 0x7823, 0xab33, 0xcd44});
    checkUpgradedList(root, {0x5612, 0x7823, 0xab33, 0xcd44}, {"", "", "", ""});
    checkList(orig, {0, 0, 0, 0});  // old location zero'd during upgrade
  }

  // -----------------------------------------------------------------

  {
    root.getAnyPointerField().setAs<List<uint32_t>>({0x17595612, 0x29347823, 0x5923ab32, 0x1a39cd45});
    auto orig = root.asReader().getAnyPointerField().getAs<List<uint32_t>>();
    checkList(root.getAnyPointerField().getAs<List<Void>>(), {VOID, VOID, VOID, VOID});
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());
    checkList(root.getAnyPointerField().getAs<List<uint8_t>>(), {0x12, 0x23, 0x32, 0x45});
    checkList(root.getAnyPointerField().getAs<List<uint16_t>>(), {0x5612, 0x7823, 0xab32, 0xcd45});
    checkList(root.getAnyPointerField().getAs<List<uint32_t>>(), {0x17595612u, 0x29347823u, 0x5923ab32u, 0x1a39cd45u});
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint64_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<Text>>());

    checkList(orig, {0x17595612u, 0x29347823u, 0x5923ab32u, 0x1a39cd45u});
    checkUpgradedList(root, {0x17595612, 0x29347823, 0x5923ab32, 0x1a39cd45}, {"", "", "", ""});
    checkList(orig, {0u, 0u, 0u, 0u});  // old location zero'd during upgrade
  }

  // -----------------------------------------------------------------

  {
    root.getAnyPointerField().setAs<List<uint64_t>>({0x1234abcd8735fe21, 0x7173bc0e1923af36});
    auto orig = root.asReader().getAnyPointerField().getAs<List<uint64_t>>();
    checkList(root.getAnyPointerField().getAs<List<Void>>(), {VOID, VOID});
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());
    checkList(root.getAnyPointerField().getAs<List<uint8_t>>(), {0x21, 0x36});
    checkList(root.getAnyPointerField().getAs<List<uint16_t>>(), {0xfe21, 0xaf36});
    checkList(root.getAnyPointerField().getAs<List<uint32_t>>(), {0x8735fe21u, 0x1923af36u});
    checkList(root.getAnyPointerField().getAs<List<uint64_t>>(), {0x1234abcd8735fe21ull, 0x7173bc0e1923af36ull});
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<Text>>());

    checkList(orig, {0x1234abcd8735fe21ull, 0x7173bc0e1923af36ull});
    checkUpgradedList(root, {0x1234abcd8735fe21ull, 0x7173bc0e1923af36ull}, {"", ""});
    checkList(orig, {0u, 0u});  // old location zero'd during upgrade
  }

  // -----------------------------------------------------------------

  {
    root.getAnyPointerField().setAs<List<Text>>({"foo", "bar", "baz"});
    auto orig = root.asReader().getAnyPointerField().getAs<List<Text>>();
    checkList(root.getAnyPointerField().getAs<List<Void>>(), {VOID, VOID, VOID});
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint8_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint16_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint32_t>>());
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint64_t>>());
    checkList(root.getAnyPointerField().getAs<List<Text>>(), {"foo", "bar", "baz"});

    checkList(orig, {"foo", "bar", "baz"});
    checkUpgradedList(root, {0, 0, 0}, {"foo", "bar", "baz"});
    checkList(orig, {"", "", ""});  // old location zero'd during upgrade
  }

  // -----------------------------------------------------------------

  {
    {
      auto l = root.getAnyPointerField().initAs<List<test::TestOldVersion>>(3);
      l[0].setOld1(0x1234567890abcdef);
      l[1].setOld1(0x234567890abcdef1);
      l[2].setOld1(0x34567890abcdef12);
      l[0].setOld2("foo");
      l[1].setOld2("bar");
      l[2].setOld2("baz");
    }
    auto orig = root.asReader().getAnyPointerField().getAs<List<test::TestOldVersion>>();

    checkList(root.getAnyPointerField().getAs<List<Void>>(), {VOID, VOID, VOID});
    EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());
    checkList(root.getAnyPointerField().getAs<List<uint8_t>>(), {0xefu, 0xf1u, 0x12u});
    checkList(root.getAnyPointerField().getAs<List<uint16_t>>(), {0xcdefu, 0xdef1u, 0xef12u});
    checkList(root.getAnyPointerField().getAs<List<uint32_t>>(), {0x90abcdefu, 0x0abcdef1u, 0xabcdef12u});
    checkList(root.getAnyPointerField().getAs<List<uint64_t>>(),
              {0x1234567890abcdefull, 0x234567890abcdef1ull, 0x34567890abcdef12ull});
    checkList(root.getAnyPointerField().getAs<List<Text>>(), {"foo", "bar", "baz"});

    checkList(orig, {0x1234567890abcdefull, 0x234567890abcdef1ull, 0x34567890abcdef12ull},
                    {"foo", "bar", "baz"});
    checkUpgradedList(root, {0x1234567890abcdefull, 0x234567890abcdef1ull, 0x34567890abcdef12ull},
                            {"foo", "bar", "baz"});
    checkList(orig, {0u, 0u, 0u}, {"", "", ""});  // old location zero'd during upgrade
  }

  // -----------------------------------------------------------------
  // OK, now we've tested upgrading every primitive list to every primitive list, every primitive
  // list to a multi-word struct, and a multi-word struct to every primitive list.  But we haven't
  // tried upgrading primitive lists to sub-word structs.

  // Upgrade from multi-byte, sub-word data.
  root.getAnyPointerField().setAs<List<uint16_t>>({12u, 34u, 56u, 78u});
  {
    auto orig = root.asReader().getAnyPointerField().getAs<List<uint16_t>>();
    checkList(orig, {12u, 34u, 56u, 78u});
    auto l = root.getAnyPointerField().getAs<List<test::TestLists::Struct32>>();
    checkList(orig, {0u, 0u, 0u, 0u});  // old location zero'd during upgrade
    ASSERT_EQ(4u, l.size());
    EXPECT_EQ(12u, l[0].getF());
    EXPECT_EQ(34u, l[1].getF());
    EXPECT_EQ(56u, l[2].getF());
    EXPECT_EQ(78u, l[3].getF());
    l[0].setF(0x65ac1235u);
    l[1].setF(0x13f12879u);
    l[2].setF(0x33423082u);
    l[3].setF(0x12988948u);
  }
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());
  checkList(root.getAnyPointerField().getAs<List<uint8_t>>(), {0x35u, 0x79u, 0x82u, 0x48u});
  checkList(root.getAnyPointerField().getAs<List<uint16_t>>(), {0x1235u, 0x2879u, 0x3082u, 0x8948u});
  checkList(root.getAnyPointerField().getAs<List<uint32_t>>(),
            {0x65ac1235u, 0x13f12879u, 0x33423082u, 0x12988948u});
  checkList(root.getAnyPointerField().getAs<List<uint64_t>>(),
            {0x65ac1235u, 0x13f12879u, 0x33423082u, 0x12988948u});
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<Text>>());

  // Upgrade from void -> data struct
  root.getAnyPointerField().setAs<List<Void>>({VOID, VOID, VOID, VOID});
  {
    auto l = root.getAnyPointerField().getAs<List<test::TestLists::Struct16>>();
    ASSERT_EQ(4u, l.size());
    EXPECT_EQ(0u, l[0].getF());
    EXPECT_EQ(0u, l[1].getF());
    EXPECT_EQ(0u, l[2].getF());
    EXPECT_EQ(0u, l[3].getF());
    l[0].setF(12573);
    l[1].setF(3251);
    l[2].setF(9238);
    l[3].setF(5832);
  }
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());
  checkList(root.getAnyPointerField().getAs<List<uint16_t>>(), {12573u, 3251u, 9238u, 5832u});
  checkList(root.getAnyPointerField().getAs<List<uint32_t>>(), {12573u, 3251u, 9238u, 5832u});
  checkList(root.getAnyPointerField().getAs<List<uint64_t>>(), {12573u, 3251u, 9238u, 5832u});
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<Text>>());

  // Upgrade from void -> pointer struct
  root.getAnyPointerField().setAs<List<Void>>({VOID, VOID, VOID, VOID});
  {
    auto l = root.getAnyPointerField().getAs<List<test::TestLists::StructP>>();
    ASSERT_EQ(4u, l.size());
    EXPECT_EQ("", l[0].getF());
    EXPECT_EQ("", l[1].getF());
    EXPECT_EQ("", l[2].getF());
    EXPECT_EQ("", l[3].getF());
    l[0].setF("foo");
    l[1].setF("bar");
    l[2].setF("baz");
    l[3].setF("qux");
  }
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<bool>>());
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint16_t>>());
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint32_t>>());
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint64_t>>());
  checkList(root.getAnyPointerField().getAs<List<Text>>(), {"foo", "bar", "baz", "qux"});

  // Verify that we cannot "side-grade" a pointer list to a data list, or a data list to
  // a pointer struct list.
  root.getAnyPointerField().setAs<List<Text>>({"foo", "bar", "baz", "qux"});
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<uint32_t>>());
  root.getAnyPointerField().setAs<List<uint32_t>>({12, 34, 56, 78});
  EXPECT_NONFATAL_FAILURE(root.getAnyPointerField().getAs<List<Text>>());
}

TEST(Encoding, UpgradeUnion) {
  // This tests for a specific case that was broken originally.
  MallocMessageBuilder builder;

  {
    auto root = builder.getRoot<test::TestOldUnionVersion>();
    root.setB(123);
  }

  {
    auto root = builder.getRoot<test::TestNewUnionVersion>();
    ASSERT_TRUE(root.isB())
    EXPECT_EQ(123, root.getB());
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

  {
    MallocMessageBuilder builder;
    TestImport::Builder root = builder.getRoot<TestImport>();
    initTestMessage(root.initField());
    checkTestMessage(root.asReader().getField());
  }

  {
    MallocMessageBuilder builder;
    TestImport2::Builder root = builder.getRoot<TestImport2>();
    initTestMessage(root.initFoo());
    checkTestMessage(root.asReader().getFoo());
    root.setBar(schemaProto<TestAllTypes>());
    initTestMessage(root.initBaz().initField());
    checkTestMessage(root.asReader().getBaz().getField());
  }
}

TEST(Encoding, Using) {
  MallocMessageBuilder builder;
  TestUsing::Reader reader = builder.getRoot<TestUsing>().asReader();
  EXPECT_EQ(TestNestedTypes::NestedEnum::BAR, reader.getOuterNestedEnum());
  EXPECT_EQ(TestNestedTypes::NestedStruct::NestedEnum::QUUX, reader.getInnerNestedEnum());
}

TEST(Encoding, StructSetters) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<TestAllTypes>();
  initTestMessage(root);

  {
    MallocMessageBuilder builder2;
    builder2.setRoot(root.asReader());
    checkTestMessage(builder2.getRoot<TestAllTypes>());
  }

  {
    MallocMessageBuilder builder2;
    auto root2 = builder2.getRoot<TestAllTypes>();
    root2.setStructField(root);
    checkTestMessage(root2.getStructField());
  }

  {
    MallocMessageBuilder builder2;
    auto root2 = builder2.getRoot<test::TestAnyPointer>();
    root2.getAnyPointerField().setAs<test::TestAllTypes>(root);
    checkTestMessage(root2.getAnyPointerField().getAs<test::TestAllTypes>());
  }
}

TEST(Encoding, OneBitStructSetters) {
  // Test case of setting a 1-bit struct.

  MallocMessageBuilder builder;
  auto root = builder.getRoot<test::TestLists>();
  auto list = root.initList1(8);
  list[0].setF(true);
  list[1].setF(true);
  list[2].setF(false);
  list[3].setF(true);
  list[4].setF(true);
  list[5].setF(false);
  list[6].setF(true);
  list[7].setF(false);

  MallocMessageBuilder builder2;
  builder2.setRoot(list.asReader()[2]);
  EXPECT_FALSE(builder2.getRoot<test::TestLists::Struct1>().getF());
  builder2.setRoot(list.asReader()[6]);
  EXPECT_TRUE(builder2.getRoot<test::TestLists::Struct1>().getF());
}

TEST(Encoding, ListSetters) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<TestListDefaults>();
  initTestMessage(root);

  {
    MallocMessageBuilder builder2;
    auto root2 = builder2.getRoot<TestListDefaults>();

    root2.getLists().setList0(root.getLists().getList0());
    root2.getLists().setList1(root.getLists().getList1());
    root2.getLists().setList8(root.getLists().getList8());
    root2.getLists().setList16(root.getLists().getList16());
    root2.getLists().setList32(root.getLists().getList32());
    root2.getLists().setList64(root.getLists().getList64());
    root2.getLists().setListP(root.getLists().getListP());

    {
      auto dst = root2.getLists().initInt32ListList(3);
      auto src = root.getLists().getInt32ListList();
      dst.set(0, src[0]);
      dst.set(1, src[1]);
      dst.set(2, src[2]);
    }

    {
      auto dst = root2.getLists().initTextListList(3);
      auto src = root.getLists().getTextListList();
      dst.set(0, src[0]);
      dst.set(1, src[1]);
      dst.set(2, src[2]);
    }

    {
      auto dst = root2.getLists().initStructListList(2);
      auto src = root.getLists().getStructListList();
      dst.set(0, src[0]);
      dst.set(1, src[1]);
    }
  }
}

TEST(Encoding, ZeroOldObject) {
  MallocMessageBuilder builder;

  auto root = builder.initRoot<TestAllTypes>();
  initTestMessage(root);

  auto oldRoot = root.asReader();
  checkTestMessage(oldRoot);

  auto oldSub = oldRoot.getStructField();
  auto oldSub2 = oldRoot.getStructList()[0];

  root = builder.initRoot<TestAllTypes>();
  checkTestMessageAllZero(oldRoot);
  checkTestMessageAllZero(oldSub);
  checkTestMessageAllZero(oldSub2);
}

TEST(Encoding, Has) {
  MallocMessageBuilder builder;

  auto root = builder.initRoot<TestAllTypes>();

  EXPECT_FALSE(root.hasTextField());
  EXPECT_FALSE(root.hasDataField());
  EXPECT_FALSE(root.hasStructField());
  EXPECT_FALSE(root.hasInt32List());

  EXPECT_FALSE(root.asReader().hasTextField());
  EXPECT_FALSE(root.asReader().hasDataField());
  EXPECT_FALSE(root.asReader().hasStructField());
  EXPECT_FALSE(root.asReader().hasInt32List());

  initTestMessage(root);

  EXPECT_TRUE(root.hasTextField());
  EXPECT_TRUE(root.hasDataField());
  EXPECT_TRUE(root.hasStructField());
  EXPECT_TRUE(root.hasInt32List());

  EXPECT_TRUE(root.asReader().hasTextField());
  EXPECT_TRUE(root.asReader().hasDataField());
  EXPECT_TRUE(root.asReader().hasStructField());
  EXPECT_TRUE(root.asReader().hasInt32List());
}

TEST(Encoding, VoidListAmplification) {
  MallocMessageBuilder builder;
  builder.initRoot<test::TestAnyPointer>().getAnyPointerField().initAs<List<Void>>(1u << 28);

  auto segments = builder.getSegmentsForOutput();
  EXPECT_EQ(1, segments.size());
  EXPECT_LT(segments[0].size(), 16);  // quite small for such a big list!

  SegmentArrayMessageReader reader(builder.getSegmentsForOutput());
  auto root = reader.getRoot<test::TestAnyPointer>().getAnyPointerField();
  EXPECT_NONFATAL_FAILURE(root.getAs<List<TestAllTypes>>());

  MallocMessageBuilder copy;
  EXPECT_NONFATAL_FAILURE(copy.setRoot(reader.getRoot<AnyPointer>()));
}

TEST(Encoding, EmptyStructListAmplification) {
  MallocMessageBuilder builder(1024);
  auto listList = builder.initRoot<test::TestAnyPointer>().getAnyPointerField()
      .initAs<List<List<test::TestEmptyStruct>>>(500);

  for (uint i = 0; i < listList.size(); i++) {
    listList.init(i, 1u << 28);
  }

  auto segments = builder.getSegmentsForOutput();
  ASSERT_EQ(1, segments.size());

  SegmentArrayMessageReader reader(builder.getSegmentsForOutput());
  auto root = reader.getRoot<test::TestAnyPointer>();
  auto listListReader = root.getAnyPointerField().getAs<List<List<TestAllTypes>>>();
  EXPECT_NONFATAL_FAILURE(listListReader[0]);
  EXPECT_NONFATAL_FAILURE(listListReader[10]);

  EXPECT_EQ(segments[0].size() - 1, root.totalSize().wordCount);
}

TEST(Encoding, Constants) {
  EXPECT_EQ(VOID, test::TestConstants::VOID_CONST);
  EXPECT_EQ(true, test::TestConstants::BOOL_CONST);
  EXPECT_EQ(-123, test::TestConstants::INT8_CONST);
  EXPECT_EQ(-12345, test::TestConstants::INT16_CONST);
  EXPECT_EQ(-12345678, test::TestConstants::INT32_CONST);
  EXPECT_EQ(-123456789012345ll, test::TestConstants::INT64_CONST);
  EXPECT_EQ(234u, test::TestConstants::UINT8_CONST);
  EXPECT_EQ(45678u, test::TestConstants::UINT16_CONST);
  EXPECT_EQ(3456789012u, test::TestConstants::UINT32_CONST);
  EXPECT_EQ(12345678901234567890ull, test::TestConstants::UINT64_CONST);
  EXPECT_FLOAT_EQ(1234.5f, test::TestConstants::FLOAT32_CONST);
  EXPECT_DOUBLE_EQ(-123e45, test::TestConstants::FLOAT64_CONST);
  EXPECT_EQ("foo", *test::TestConstants::TEXT_CONST);
  EXPECT_EQ(data("bar"), test::TestConstants::DATA_CONST);
  {
    TestAllTypes::Reader subReader = test::TestConstants::STRUCT_CONST;
    EXPECT_EQ(VOID, subReader.getVoidField());
    EXPECT_EQ(true, subReader.getBoolField());
    EXPECT_EQ(-12, subReader.getInt8Field());
    EXPECT_EQ(3456, subReader.getInt16Field());
    EXPECT_EQ(-78901234, subReader.getInt32Field());
    EXPECT_EQ(56789012345678ll, subReader.getInt64Field());
    EXPECT_EQ(90u, subReader.getUInt8Field());
    EXPECT_EQ(1234u, subReader.getUInt16Field());
    EXPECT_EQ(56789012u, subReader.getUInt32Field());
    EXPECT_EQ(345678901234567890ull, subReader.getUInt64Field());
    EXPECT_FLOAT_EQ(-1.25e-10f, subReader.getFloat32Field());
    EXPECT_DOUBLE_EQ(345, subReader.getFloat64Field());
    EXPECT_EQ("baz", subReader.getTextField());
    EXPECT_EQ(data("qux"), subReader.getDataField());
    {
      auto subSubReader = subReader.getStructField();
      EXPECT_EQ("nested", subSubReader.getTextField());
      EXPECT_EQ("really nested", subSubReader.getStructField().getTextField());
    }
    EXPECT_EQ(TestEnum::BAZ, subReader.getEnumField());

    checkList(subReader.getVoidList(), {VOID, VOID, VOID});
    checkList(subReader.getBoolList(), {false, true, false, true, true});
    checkList(subReader.getInt8List(), {12, -34, -0x80, 0x7f});
    checkList(subReader.getInt16List(), {1234, -5678, -0x8000, 0x7fff});
    // gcc warns on -0x800... and the only work-around I could find was to do -0x7ff...-1.
    checkList(subReader.getInt32List(), {12345678, -90123456, -0x7fffffff - 1, 0x7fffffff});
    checkList(subReader.getInt64List(), {123456789012345ll, -678901234567890ll, -0x7fffffffffffffffll-1, 0x7fffffffffffffffll});
    checkList(subReader.getUInt8List(), {12u, 34u, 0u, 0xffu});
    checkList(subReader.getUInt16List(), {1234u, 5678u, 0u, 0xffffu});
    checkList(subReader.getUInt32List(), {12345678u, 90123456u, 0u, 0xffffffffu});
    checkList(subReader.getUInt64List(), {123456789012345ull, 678901234567890ull, 0ull, 0xffffffffffffffffull});
    checkList(subReader.getFloat32List(), {0.0f, 1234567.0f, 1e37f, -1e37f, 1e-37f, -1e-37f});
    checkList(subReader.getFloat64List(), {0.0, 123456789012345.0, 1e306, -1e306, 1e-306, -1e-306});
    checkList(subReader.getTextList(), {"quux", "corge", "grault"});
    checkList(subReader.getDataList(), {data("garply"), data("waldo"), data("fred")});
    {
      auto listReader = subReader.getStructList();
      ASSERT_EQ(3u, listReader.size());
      EXPECT_EQ("x structlist 1", listReader[0].getTextField());
      EXPECT_EQ("x structlist 2", listReader[1].getTextField());
      EXPECT_EQ("x structlist 3", listReader[2].getTextField());
    }
    checkList(subReader.getEnumList(), {TestEnum::QUX, TestEnum::BAR, TestEnum::GRAULT});
  }
  EXPECT_EQ(TestEnum::CORGE, test::TestConstants::ENUM_CONST);

  EXPECT_EQ(6u, test::TestConstants::VOID_LIST_CONST->size());
  checkList(*test::TestConstants::BOOL_LIST_CONST, {true, false, false, true});
  checkList(*test::TestConstants::INT8_LIST_CONST, {111, -111});
  checkList(*test::TestConstants::INT16_LIST_CONST, {11111, -11111});
  checkList(*test::TestConstants::INT32_LIST_CONST, {111111111, -111111111});
  checkList(*test::TestConstants::INT64_LIST_CONST, {1111111111111111111ll, -1111111111111111111ll});
  checkList(*test::TestConstants::UINT8_LIST_CONST, {111u, 222u});
  checkList(*test::TestConstants::UINT16_LIST_CONST, {33333u, 44444u});
  checkList(*test::TestConstants::UINT32_LIST_CONST, {3333333333u});
  checkList(*test::TestConstants::UINT64_LIST_CONST, {11111111111111111111ull});
  {
    List<float>::Reader listReader = test::TestConstants::FLOAT32_LIST_CONST;
    ASSERT_EQ(4u, listReader.size());
    EXPECT_EQ(5555.5f, listReader[0]);
    EXPECT_EQ(kj::inf(), listReader[1]);
    EXPECT_EQ(-kj::inf(), listReader[2]);
    EXPECT_TRUE(listReader[3] != listReader[3]);
  }
  {
    List<double>::Reader listReader = test::TestConstants::FLOAT64_LIST_CONST;
    ASSERT_EQ(4u, listReader.size());
    EXPECT_EQ(7777.75, listReader[0]);
    EXPECT_EQ(kj::inf(), listReader[1]);
    EXPECT_EQ(-kj::inf(), listReader[2]);
    EXPECT_TRUE(listReader[3] != listReader[3]);
  }
  checkList(*test::TestConstants::TEXT_LIST_CONST, {"plugh", "xyzzy", "thud"});
  checkList(*test::TestConstants::DATA_LIST_CONST, {data("oops"), data("exhausted"), data("rfc3092")});
  {
    List<TestAllTypes>::Reader listReader = test::TestConstants::STRUCT_LIST_CONST;
    ASSERT_EQ(3u, listReader.size());
    EXPECT_EQ("structlist 1", listReader[0].getTextField());
    EXPECT_EQ("structlist 2", listReader[1].getTextField());
    EXPECT_EQ("structlist 3", listReader[2].getTextField());
  }
  checkList(*test::TestConstants::ENUM_LIST_CONST, {TestEnum::FOO, TestEnum::GARPLY});
}

TEST(Encoding, AnyPointerConstants) {
  auto reader = test::ANY_POINTER_CONSTANTS.get();

  EXPECT_EQ("baz", reader.getAnyKindAsStruct().getAs<TestAllTypes>().getTextField());
  EXPECT_EQ("baz", reader.getAnyStructAsStruct().as<TestAllTypes>().getTextField());

  EXPECT_EQ(111111111, reader.getAnyKindAsList().getAs<List<int32_t>>()[0]);
  EXPECT_EQ(111111111, reader.getAnyListAsList().as<List<int32_t>>()[0]);
}

TEST(Encoding, GlobalConstants) {
  EXPECT_EQ(12345u, test::GLOBAL_INT);
  EXPECT_EQ("foobar", test::GLOBAL_TEXT.get());
  EXPECT_EQ(54321, test::GLOBAL_STRUCT->getInt32Field());

  TestAllTypes::Reader reader = test::DERIVED_CONSTANT;

  EXPECT_EQ(12345, reader.getUInt32Field());
  EXPECT_EQ("foo", reader.getTextField());
  checkList(reader.getStructField().getTextList(), {"quux", "corge", "grault"});
  checkList(reader.getInt16List(), {11111, -11111});
  {
    List<TestAllTypes>::Reader listReader = reader.getStructList();
    ASSERT_EQ(3u, listReader.size());
    EXPECT_EQ("structlist 1", listReader[0].getTextField());
    EXPECT_EQ("structlist 2", listReader[1].getTextField());
    EXPECT_EQ("structlist 3", listReader[2].getTextField());
  }
}

TEST(Encoding, Embeds) {
  {
    kj::ArrayInputStream input(test::EMBEDDED_DATA);
    PackedMessageReader reader(input);
    checkTestMessage(reader.getRoot<TestAllTypes>());
  }

#if !CAPNP_LITE

  {
    MallocMessageBuilder builder;
    auto root = builder.getRoot<TestAllTypes>();
    initTestMessage(root);
    kj::StringPtr text = test::EMBEDDED_TEXT;
    EXPECT_EQ(kj::str(root, text.endsWith("\r\n") ? "\r\n" : "\n"), text);
  }

#endif // CAPNP_LITE

  {
    checkTestMessage(test::EMBEDDED_STRUCT);
  }
}

TEST(Encoding, HasEmptyStruct) {
  MallocMessageBuilder message;
  auto root = message.initRoot<test::TestAnyPointer>();

  EXPECT_EQ(1, root.totalSize().wordCount);

  EXPECT_FALSE(root.asReader().hasAnyPointerField());
  EXPECT_FALSE(root.hasAnyPointerField());
  root.getAnyPointerField().initAs<test::TestEmptyStruct>();
  EXPECT_TRUE(root.asReader().hasAnyPointerField());
  EXPECT_TRUE(root.hasAnyPointerField());

  EXPECT_EQ(1, root.totalSize().wordCount);
}

TEST(Encoding, HasEmptyList) {
  MallocMessageBuilder message;
  auto root = message.initRoot<test::TestAnyPointer>();

  EXPECT_EQ(1, root.totalSize().wordCount);

  EXPECT_FALSE(root.asReader().hasAnyPointerField());
  EXPECT_FALSE(root.hasAnyPointerField());
  root.getAnyPointerField().initAs<List<int32_t>>(0);
  EXPECT_TRUE(root.asReader().hasAnyPointerField());
  EXPECT_TRUE(root.hasAnyPointerField());

  EXPECT_EQ(1, root.totalSize().wordCount);
}

TEST(Encoding, HasEmptyStructList) {
  MallocMessageBuilder message;
  auto root = message.initRoot<test::TestAnyPointer>();

  EXPECT_EQ(1, root.totalSize().wordCount);

  EXPECT_FALSE(root.asReader().hasAnyPointerField());
  EXPECT_FALSE(root.hasAnyPointerField());
  root.getAnyPointerField().initAs<List<TestAllTypes>>(0);
  EXPECT_TRUE(root.asReader().hasAnyPointerField());
  EXPECT_TRUE(root.hasAnyPointerField());

  EXPECT_EQ(2, root.totalSize().wordCount);
}

TEST(Encoding, NameAnnotation) {
  EXPECT_EQ(2, static_cast<uint16_t>(test::RenamedStruct::RenamedEnum::QUX));
  EXPECT_EQ(2, static_cast<uint16_t>(test::RenamedStruct::RenamedNestedStruct::RenamedDeeplyNestedEnum::GARPLY));

  MallocMessageBuilder message;
  auto root = message.initRoot<test::RenamedStruct>();

  root.setGoodFieldName(true);
  EXPECT_EQ(true, root.getGoodFieldName());
  EXPECT_TRUE(root.isGoodFieldName());

  root.setBar(0xff);
  EXPECT_FALSE(root.isGoodFieldName());

  root.setAnotherGoodFieldName(test::RenamedStruct::RenamedEnum::QUX);
  EXPECT_EQ(test::RenamedStruct::RenamedEnum::QUX, root.getAnotherGoodFieldName());

  EXPECT_FALSE(root.getRenamedUnion().isQux());
  auto quxBuilder = root.getRenamedUnion().initQux();
  EXPECT_TRUE(root.getRenamedUnion().isQux());
  EXPECT_FALSE(root.getRenamedUnion().getQux().hasAnotherGoodNestedFieldName());

  quxBuilder.setGoodNestedFieldName(true);
  EXPECT_EQ(true, quxBuilder.getGoodNestedFieldName());

  EXPECT_FALSE(quxBuilder.hasAnotherGoodNestedFieldName());
  auto nestedFieldBuilder = quxBuilder.initAnotherGoodNestedFieldName();
  EXPECT_TRUE(quxBuilder.hasAnotherGoodNestedFieldName());

  nestedFieldBuilder.setGoodNestedFieldName(true);
  EXPECT_EQ(true, nestedFieldBuilder.getGoodNestedFieldName());
  EXPECT_FALSE(nestedFieldBuilder.hasAnotherGoodNestedFieldName());

  EXPECT_FALSE(root.getRenamedUnion().isRenamedGroup());
  auto renamedGroupBuilder KJ_UNUSED = root.getRenamedUnion().initRenamedGroup();
  EXPECT_TRUE(root.getRenamedUnion().isRenamedGroup());

  test::RenamedInterface::RenamedMethodParams::Reader renamedInterfaceParams;
  renamedInterfaceParams.getRenamedParam();
}

TEST(Encoding, DefaultFloatPlusNan) {
  MallocMessageBuilder message;
  auto root = message.initRoot<TestDefaults>();

  root.setFloat32Field(kj::nan());
  root.setFloat64Field(kj::nan());

  float f = root.getFloat32Field();
  EXPECT_TRUE(f != f);

  double d = root.getFloat64Field();
  EXPECT_TRUE(d != d);
}

TEST(Encoding, WholeFloatDefault) {
  MallocMessageBuilder message;
  auto root = message.initRoot<test::TestWholeFloatDefault>();

  EXPECT_EQ(123.0f, root.getField());
  EXPECT_EQ(2e30f, root.getBigField());
  EXPECT_EQ(456.0f, test::TestWholeFloatDefault::CONSTANT);
  EXPECT_EQ(4e30f, test::TestWholeFloatDefault::BIG_CONSTANT);
}

TEST(Encoding, Generics) {
  MallocMessageBuilder message;
  auto root = message.initRoot<test::TestUseGenerics>();
  auto reader = root.asReader();

  initTestMessage(root.initBasic().initFoo());
  checkTestMessage(reader.getBasic().getFoo());

  {
    auto typed = root.getBasic();
    test::TestGenerics<>::Reader generic = typed.asGeneric<>();
    checkTestMessage(generic.getFoo().getAs<TestAllTypes>());
    test::TestGenerics<TestAllTypes>::Reader halfGeneric = typed.asGeneric<TestAllTypes>();
    checkTestMessage(halfGeneric.getFoo());
  }

  {
    auto typed = root.getBasic().asReader();
    test::TestGenerics<>::Reader generic = typed.asGeneric<>();
    checkTestMessage(generic.getFoo().getAs<TestAllTypes>());
    test::TestGenerics<TestAllTypes>::Reader halfGeneric = typed.asGeneric<TestAllTypes>();
    checkTestMessage(halfGeneric.getFoo());
  }

  initTestMessage(root.initInner().initFoo());
  checkTestMessage(reader.getInner().getFoo());

  {
    auto typed = root.getInner();
    test::TestGenerics<>::Inner::Reader generic = typed.asTestGenericsGeneric<>();
    checkTestMessage(generic.getFoo().getAs<TestAllTypes>());
    test::TestGenerics<TestAllTypes>::Inner::Reader halfGeneric = typed.asTestGenericsGeneric<TestAllTypes>();
    checkTestMessage(halfGeneric.getFoo());
  }

  {
    auto typed = root.getInner().asReader();
    test::TestGenerics<>::Inner::Reader generic = typed.asTestGenericsGeneric<>();
    checkTestMessage(generic.getFoo().getAs<TestAllTypes>());
    test::TestGenerics<TestAllTypes>::Inner::Reader halfGeneric = typed.asTestGenericsGeneric<TestAllTypes>();
    checkTestMessage(halfGeneric.getFoo());
  }

  root.initInner2().setBaz("foo");
  EXPECT_EQ("foo", reader.getInner2().getBaz());

  initTestMessage(root.getInner2().initInnerBound().initFoo());
  checkTestMessage(reader.getInner2().getInnerBound().getFoo());

  initTestMessage(root.getInner2().initInnerUnbound().getFoo().initAs<TestAllTypes>());
  checkTestMessage(reader.getInner2().getInnerUnbound().getFoo().getAs<TestAllTypes>());

  initTestMessage(root.initUnspecified().getFoo().initAs<TestAllTypes>());
  checkTestMessage(reader.getUnspecified().getFoo().getAs<TestAllTypes>());

  initTestMessage(root.initWrapper().initValue().initFoo());
  checkTestMessage(reader.getWrapper().getValue().getFoo());
}

TEST(Encoding, GenericDefaults) {
  test::TestUseGenerics::Reader reader;

  EXPECT_EQ(123, reader.getDefault().getFoo().getInt16Field());
  EXPECT_EQ(123, reader.getDefaultInner().getFoo().getInt16Field());
  EXPECT_EQ("text", reader.getDefaultInner().getBar());
  EXPECT_EQ(123, reader.getDefaultUser().getBasic().getFoo().getInt16Field());
  EXPECT_EQ("text", reader.getDefaultWrapper().getValue().getFoo());
  EXPECT_EQ(321, reader.getDefaultWrapper().getValue().getRev().getFoo().getInt16Field());
  EXPECT_EQ("text", reader.getDefaultWrapper2().getValue().getValue().getFoo());
  EXPECT_EQ(321, reader.getDefaultWrapper2().getValue()
      .getValue().getRev().getFoo().getInt16Field());
}

TEST(Encoding, UnionInGenerics) {
  MallocMessageBuilder message;
  auto builder = message.initRoot<test::TestGenerics<>>();
  auto reader = builder.asReader();

  //just call the methods to verify that generated code compiles
  reader.which();
  builder.which();

  reader.isUv();
  builder.isUv();
  reader.getUv();
  builder.getUv();
  builder.setUv();

  builder.initUg();

  reader.isUg();
  builder.isUg();
  reader.getUg();
  builder.getUg();
  builder.initUg();
}

TEST(Encoding, DefaultListBuilder) {
  // At one point, this wouldn't compile.

  List<int>::Builder(nullptr);
  List<TestAllTypes>::Builder(nullptr);
  List<List<int>>::Builder(nullptr);
  List<Text>::Builder(nullptr);
}

TEST(Encoding, ListSize) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestListDefaults>();
  initTestMessage(root);

  auto lists = root.asReader().getLists();

  auto listSizes =
      lists.getList0().totalSize() +
      lists.getList1().totalSize() +
      lists.getList8().totalSize() +
      lists.getList16().totalSize() +
      lists.getList32().totalSize() +
      lists.getList64().totalSize() +
      lists.getListP().totalSize() +
      lists.getInt32ListList().totalSize() +
      lists.getTextListList().totalSize() +
      lists.getStructListList().totalSize();

  auto structSize = lists.totalSize();

  auto shallowSize = unbound(capnp::_::structSize<test::TestLists>().total() / WORDS);

  EXPECT_EQ(structSize.wordCount - shallowSize, listSizes.wordCount);
}

KJ_TEST("list.setWithCaveats(i, list[i]) doesn't corrupt contents") {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();
  auto list = root.initStructList(2);
  initTestMessage(list[0]);
  list.setWithCaveats(0, list[0]);
  checkTestMessage(list[0]);
  checkTestMessageAllZero(list[1]);
  list.setWithCaveats(1, list[0]);
  checkTestMessage(list[0]);
  checkTestMessage(list[1]);
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
