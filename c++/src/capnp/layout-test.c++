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

#define CAPNP_PRIVATE
#include "layout.h"
#include "message.h"
#include "arena.h"
#include <kj/compat/gtest.h>

#if CAPNP_DEBUG_TYPES
namespace kj {
  template <typename T, typename U>
  String KJ_STRINGIFY(kj::Quantity<T, U> value) {
    return kj::str(unboundAs<uint64_t>(value / kj::unit<kj::Quantity<T, U>>()));
  }

  // Hack: Allow direct comparisons and multiplications so that we don't have to rewrite the code
  //   below.
  template <uint64_t maxN, typename T>
  inline constexpr Bounded<65535, T> operator*(uint a, Bounded<maxN, T> b) {
    return assumeBits<16>(a * unbound(b));
  }
  template <uint b>
  inline constexpr Bounded<65535, uint> operator*(uint a, BoundedConst<b>) {
    return assumeBits<16>(a * b);
  }
}
#endif

namespace capnp {
namespace _ {  // private
namespace {

TEST(WireFormat, SimpleRawDataStruct) {
  AlignedData<2> data = {{
    // Struct ref, offset = 1, dataSize = 1, pointerCount = 0
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    // Content for the data section.
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef
  }};

  StructReader reader = PointerReader::getRootUnchecked(data.words).getStruct(nullptr);

  EXPECT_EQ(0xefcdab8967452301ull, reader.getDataField<uint64_t>(0 * ELEMENTS));
  EXPECT_EQ(0u, reader.getDataField<uint64_t>(1 * ELEMENTS));
  EXPECT_EQ(0x67452301u, reader.getDataField<uint32_t>(0 * ELEMENTS));
  EXPECT_EQ(0xefcdab89u, reader.getDataField<uint32_t>(1 * ELEMENTS));
  EXPECT_EQ(0u, reader.getDataField<uint32_t>(2 * ELEMENTS));
  EXPECT_EQ(0x2301u, reader.getDataField<uint16_t>(0 * ELEMENTS));
  EXPECT_EQ(0x6745u, reader.getDataField<uint16_t>(1 * ELEMENTS));
  EXPECT_EQ(0xab89u, reader.getDataField<uint16_t>(2 * ELEMENTS));
  EXPECT_EQ(0xefcdu, reader.getDataField<uint16_t>(3 * ELEMENTS));
  EXPECT_EQ(0u, reader.getDataField<uint16_t>(4 * ELEMENTS));

  EXPECT_EQ(321u ^ 0xefcdab8967452301ull, reader.getDataField<uint64_t>(0 * ELEMENTS, 321u));
  EXPECT_EQ(321u ^ 0x67452301u, reader.getDataField<uint32_t>(0 * ELEMENTS, 321u));
  EXPECT_EQ(321u ^ 0x2301u, reader.getDataField<uint16_t>(0 * ELEMENTS, 321u));
  EXPECT_EQ(321u, reader.getDataField<uint64_t>(1 * ELEMENTS, 321u));
  EXPECT_EQ(321u, reader.getDataField<uint32_t>(2 * ELEMENTS, 321u));
  EXPECT_EQ(321u, reader.getDataField<uint16_t>(4 * ELEMENTS, 321u));

  // Bits
  EXPECT_TRUE (reader.getDataField<bool>(0 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(1 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(2 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(3 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(4 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(5 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(6 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(7 * ELEMENTS));

  EXPECT_TRUE (reader.getDataField<bool>( 8 * ELEMENTS));
  EXPECT_TRUE (reader.getDataField<bool>( 9 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(10 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(11 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(12 * ELEMENTS));
  EXPECT_TRUE (reader.getDataField<bool>(13 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(14 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(15 * ELEMENTS));

  EXPECT_TRUE (reader.getDataField<bool>(63 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(64 * ELEMENTS));

  EXPECT_TRUE (reader.getDataField<bool>(0 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(1 * ELEMENTS, false));
  EXPECT_TRUE (reader.getDataField<bool>(63 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(64 * ELEMENTS, false));
  EXPECT_FALSE(reader.getDataField<bool>(0 * ELEMENTS, true));
  EXPECT_TRUE (reader.getDataField<bool>(1 * ELEMENTS, true));
  EXPECT_FALSE(reader.getDataField<bool>(63 * ELEMENTS, true));
  EXPECT_TRUE (reader.getDataField<bool>(64 * ELEMENTS, true));
}

static const AlignedData<2> SUBSTRUCT_DEFAULT = {{0,0,0,0,1,0,0,0,  0,0,0,0,0,0,0,0}};
static const AlignedData<2> STRUCTLIST_ELEMENT_SUBSTRUCT_DEFAULT =
    {{0,0,0,0,1,0,0,0,  0,0,0,0,0,0,0,0}};

static constexpr StructSize STRUCTLIST_ELEMENT_SIZE(1 * WORDS, 1 * POINTERS);

static void setupStruct(StructBuilder builder) {
  builder.setDataField<uint64_t>(0 * ELEMENTS, 0x1011121314151617ull);
  builder.setDataField<uint32_t>(2 * ELEMENTS, 0x20212223u);
  builder.setDataField<uint16_t>(6 * ELEMENTS, 0x3031u);
  builder.setDataField<uint8_t>(14 * ELEMENTS, 0x40u);
  builder.setDataField<bool>(120 * ELEMENTS, false);
  builder.setDataField<bool>(121 * ELEMENTS, false);
  builder.setDataField<bool>(122 * ELEMENTS, true);
  builder.setDataField<bool>(123 * ELEMENTS, false);
  builder.setDataField<bool>(124 * ELEMENTS, true);
  builder.setDataField<bool>(125 * ELEMENTS, true);
  builder.setDataField<bool>(126 * ELEMENTS, true);
  builder.setDataField<bool>(127 * ELEMENTS, false);

  {
    StructBuilder subStruct = builder.getPointerField(0 * POINTERS).initStruct(
        StructSize(1 * WORDS, 0 * POINTERS));
    subStruct.setDataField<uint32_t>(0 * ELEMENTS, 123);
  }

  {
    ListBuilder list = builder.getPointerField(1 * POINTERS)
        .initList(ElementSize::FOUR_BYTES, 3 * ELEMENTS);
    EXPECT_EQ(3 * ELEMENTS, list.size());
    list.setDataElement<int32_t>(0 * ELEMENTS, 200);
    list.setDataElement<int32_t>(1 * ELEMENTS, 201);
    list.setDataElement<int32_t>(2 * ELEMENTS, 202);
  }

  {
    ListBuilder list = builder.getPointerField(2 * POINTERS).initStructList(
        4 * ELEMENTS, STRUCTLIST_ELEMENT_SIZE);
    EXPECT_EQ(4 * ELEMENTS, list.size());
    for (int i = 0; i < 4; i++) {
      StructBuilder element = list.getStructElement(i * ELEMENTS);
      element.setDataField<int32_t>(0 * ELEMENTS, 300 + i);
      element.getPointerField(0 * POINTERS)
             .initStruct(StructSize(1 * WORDS, 0 * POINTERS))
             .setDataField<int32_t>(0 * ELEMENTS, 400 + i);
    }
  }

  {
    ListBuilder list = builder.getPointerField(3 * POINTERS)
                              .initList(ElementSize::POINTER, 5 * ELEMENTS);
    EXPECT_EQ(5 * ELEMENTS, list.size());
    for (uint i = 0; i < 5; i++) {
      ListBuilder element = list.getPointerElement(i * ELEMENTS)
                                .initList(ElementSize::TWO_BYTES, (i + 1) * ELEMENTS);
      EXPECT_EQ((i + 1) * ELEMENTS, element.size());
      for (uint j = 0; j <= i; j++) {
        element.setDataElement<uint16_t>(j * ELEMENTS, 500 + j);
      }
    }
  }
}

static void checkStruct(StructBuilder builder) {
  EXPECT_EQ(0x1011121314151617ull, builder.getDataField<uint64_t>(0 * ELEMENTS));
  EXPECT_EQ(0x20212223u, builder.getDataField<uint32_t>(2 * ELEMENTS));
  EXPECT_EQ(0x3031u, builder.getDataField<uint16_t>(6 * ELEMENTS));
  EXPECT_EQ(0x40u, builder.getDataField<uint8_t>(14 * ELEMENTS));
  EXPECT_FALSE(builder.getDataField<bool>(120 * ELEMENTS));
  EXPECT_FALSE(builder.getDataField<bool>(121 * ELEMENTS));
  EXPECT_TRUE (builder.getDataField<bool>(122 * ELEMENTS));
  EXPECT_FALSE(builder.getDataField<bool>(123 * ELEMENTS));
  EXPECT_TRUE (builder.getDataField<bool>(124 * ELEMENTS));
  EXPECT_TRUE (builder.getDataField<bool>(125 * ELEMENTS));
  EXPECT_TRUE (builder.getDataField<bool>(126 * ELEMENTS));
  EXPECT_FALSE(builder.getDataField<bool>(127 * ELEMENTS));

  {
    StructBuilder subStruct = builder.getPointerField(0 * POINTERS).getStruct(
        StructSize(1 * WORDS, 0 * POINTERS), SUBSTRUCT_DEFAULT.words);
    EXPECT_EQ(123u, subStruct.getDataField<uint32_t>(0 * ELEMENTS));
  }

  {
    ListBuilder list = builder.getPointerField(1 * POINTERS)
                              .getList(ElementSize::FOUR_BYTES, nullptr);
    ASSERT_EQ(3 * ELEMENTS, list.size());
    EXPECT_EQ(200, list.getDataElement<int32_t>(0 * ELEMENTS));
    EXPECT_EQ(201, list.getDataElement<int32_t>(1 * ELEMENTS));
    EXPECT_EQ(202, list.getDataElement<int32_t>(2 * ELEMENTS));
  }

  {
    ListBuilder list = builder.getPointerField(2 * POINTERS)
                              .getStructList(STRUCTLIST_ELEMENT_SIZE, nullptr);
    ASSERT_EQ(4 * ELEMENTS, list.size());
    for (int i = 0; i < 4; i++) {
      StructBuilder element = list.getStructElement(i * ELEMENTS);
      EXPECT_EQ(300 + i, element.getDataField<int32_t>(0 * ELEMENTS));
      EXPECT_EQ(400 + i,
          element.getPointerField(0 * POINTERS)
                 .getStruct(StructSize(1 * WORDS, 0 * POINTERS),
                            STRUCTLIST_ELEMENT_SUBSTRUCT_DEFAULT.words)
                 .getDataField<int32_t>(0 * ELEMENTS));
    }
  }

  {
    ListBuilder list = builder.getPointerField(3 * POINTERS).getList(ElementSize::POINTER, nullptr);
    ASSERT_EQ(5 * ELEMENTS, list.size());
    for (uint i = 0; i < 5; i++) {
      ListBuilder element = list.getPointerElement(i * ELEMENTS)
                                .getList(ElementSize::TWO_BYTES, nullptr);
      ASSERT_EQ((i + 1) * ELEMENTS, element.size());
      for (uint j = 0; j <= i; j++) {
        EXPECT_EQ(500u + j, element.getDataElement<uint16_t>(j * ELEMENTS));
      }
    }
  }
}

static void checkStruct(StructReader reader) {
  EXPECT_EQ(0x1011121314151617ull, reader.getDataField<uint64_t>(0 * ELEMENTS));
  EXPECT_EQ(0x20212223u, reader.getDataField<uint32_t>(2 * ELEMENTS));
  EXPECT_EQ(0x3031u, reader.getDataField<uint16_t>(6 * ELEMENTS));
  EXPECT_EQ(0x40u, reader.getDataField<uint8_t>(14 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(120 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(121 * ELEMENTS));
  EXPECT_TRUE (reader.getDataField<bool>(122 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(123 * ELEMENTS));
  EXPECT_TRUE (reader.getDataField<bool>(124 * ELEMENTS));
  EXPECT_TRUE (reader.getDataField<bool>(125 * ELEMENTS));
  EXPECT_TRUE (reader.getDataField<bool>(126 * ELEMENTS));
  EXPECT_FALSE(reader.getDataField<bool>(127 * ELEMENTS));

  {
    StructReader subStruct = reader.getPointerField(0 * POINTERS)
                                   .getStruct(SUBSTRUCT_DEFAULT.words);
    EXPECT_EQ(123u, subStruct.getDataField<uint32_t>(0 * ELEMENTS));
  }

  {
    ListReader list = reader.getPointerField(1 * POINTERS).getList(ElementSize::FOUR_BYTES, nullptr);
    ASSERT_EQ(3 * ELEMENTS, list.size());
    EXPECT_EQ(200, list.getDataElement<int32_t>(0 * ELEMENTS));
    EXPECT_EQ(201, list.getDataElement<int32_t>(1 * ELEMENTS));
    EXPECT_EQ(202, list.getDataElement<int32_t>(2 * ELEMENTS));
  }

  {
    ListReader list = reader.getPointerField(2 * POINTERS)
                            .getList(ElementSize::INLINE_COMPOSITE, nullptr);
    ASSERT_EQ(4 * ELEMENTS, list.size());
    for (int i = 0; i < 4; i++) {
      StructReader element = list.getStructElement(i * ELEMENTS);
      EXPECT_EQ(300 + i, element.getDataField<int32_t>(0 * ELEMENTS));
      EXPECT_EQ(400 + i,
          element.getPointerField(0 * POINTERS)
                 .getStruct(STRUCTLIST_ELEMENT_SUBSTRUCT_DEFAULT.words)
                 .getDataField<int32_t>(0 * ELEMENTS));
    }
  }

  {
    ListReader list = reader.getPointerField(3 * POINTERS).getList(ElementSize::POINTER, nullptr);
    ASSERT_EQ(5 * ELEMENTS, list.size());
    for (uint i = 0; i < 5; i++) {
      ListReader element = list.getPointerElement(i * ELEMENTS)
                               .getList(ElementSize::TWO_BYTES, nullptr);
      ASSERT_EQ((i + 1) * ELEMENTS, element.size());
      for (uint j = 0; j <= i; j++) {
        EXPECT_EQ(500u + j, element.getDataElement<uint16_t>(j * ELEMENTS));
      }
    }
  }
}

TEST(WireFormat, StructRoundTrip_OneSegment) {
  MallocMessageBuilder message;
  BuilderArena arena(&message);
  auto allocation = arena.allocate(1 * WORDS);
  SegmentBuilder* segment = allocation.segment;
  word* rootLocation = allocation.words;

  StructBuilder builder = PointerBuilder::getRoot(segment, nullptr, rootLocation)
      .initStruct(StructSize(2 * WORDS, 4 * POINTERS));
  setupStruct(builder);

  // word count:
  //    1  root pointer
  //    6  root struct
  //    1  sub message
  //    2  3-element int32 list
  //   13  struct list
  //         1 tag
  //        12 4x struct
  //           1 data section
  //           1 pointer section
  //           1 sub-struct
  //   11  list list
  //         5 pointers to sub-lists
  //         6 sub-lists (4x 1 word, 1x 2 words)
  // -----
  //   34
  kj::ArrayPtr<const kj::ArrayPtr<const word>> segments = arena.getSegmentsForOutput();
  ASSERT_EQ(1u, segments.size());
  EXPECT_EQ(34u, segments[0].size());

  checkStruct(builder);
  checkStruct(builder.asReader());
  checkStruct(PointerReader::getRootUnchecked(segment->getStartPtr()).getStruct(nullptr));
  checkStruct(PointerReader::getRoot(segment, nullptr, segment->getStartPtr(), 4)
      .getStruct(nullptr));
}

TEST(WireFormat, StructRoundTrip_OneSegmentPerAllocation) {
  MallocMessageBuilder message(0, AllocationStrategy::FIXED_SIZE);
  BuilderArena arena(&message);
  auto allocation = arena.allocate(1 * WORDS);
  SegmentBuilder* segment = allocation.segment;
  word* rootLocation = allocation.words;

  StructBuilder builder = PointerBuilder::getRoot(segment, nullptr, rootLocation)
      .initStruct(StructSize(2 * WORDS, 4 * POINTERS));
  setupStruct(builder);

  // Verify that we made 15 segments.
  kj::ArrayPtr<const kj::ArrayPtr<const word>> segments = arena.getSegmentsForOutput();
  ASSERT_EQ(15u, segments.size());

  // Check that each segment has the expected size.  Recall that the first word of each segment will
  // actually be a pointer to the first thing allocated within that segment.
  EXPECT_EQ( 1u, segments[ 0].size());  // root ref
  EXPECT_EQ( 7u, segments[ 1].size());  // root struct
  EXPECT_EQ( 2u, segments[ 2].size());  // sub-struct
  EXPECT_EQ( 3u, segments[ 3].size());  // 3-element int32 list
  EXPECT_EQ(10u, segments[ 4].size());  // struct list
  EXPECT_EQ( 2u, segments[ 5].size());  // struct list substruct 1
  EXPECT_EQ( 2u, segments[ 6].size());  // struct list substruct 2
  EXPECT_EQ( 2u, segments[ 7].size());  // struct list substruct 3
  EXPECT_EQ( 2u, segments[ 8].size());  // struct list substruct 4
  EXPECT_EQ( 6u, segments[ 9].size());  // list list
  EXPECT_EQ( 2u, segments[10].size());  // list list sublist 1
  EXPECT_EQ( 2u, segments[11].size());  // list list sublist 2
  EXPECT_EQ( 2u, segments[12].size());  // list list sublist 3
  EXPECT_EQ( 2u, segments[13].size());  // list list sublist 4
  EXPECT_EQ( 3u, segments[14].size());  // list list sublist 5

  checkStruct(builder);
  checkStruct(builder.asReader());
  checkStruct(PointerReader::getRoot(segment, nullptr, segment->getStartPtr(), 4)
      .getStruct(nullptr));
}

TEST(WireFormat, StructRoundTrip_MultipleSegmentsWithMultipleAllocations) {
  MallocMessageBuilder message(8, AllocationStrategy::FIXED_SIZE);
  BuilderArena arena(&message);
  auto allocation = arena.allocate(1 * WORDS);
  SegmentBuilder* segment = allocation.segment;
  word* rootLocation = allocation.words;

  StructBuilder builder = PointerBuilder::getRoot(segment, nullptr, rootLocation)
      .initStruct(StructSize(2 * WORDS, 4 * POINTERS));
  setupStruct(builder);

  // Verify that we made 6 segments.
  kj::ArrayPtr<const kj::ArrayPtr<const word>> segments = arena.getSegmentsForOutput();
  ASSERT_EQ(6u, segments.size());

  // Check that each segment has the expected size.  Recall that each object will be prefixed by an
  // extra word if its parent is in a different segment.
  EXPECT_EQ( 8u, segments[0].size());  // root ref + struct + sub
  EXPECT_EQ( 3u, segments[1].size());  // 3-element int32 list
  EXPECT_EQ(10u, segments[2].size());  // struct list
  EXPECT_EQ( 8u, segments[3].size());  // struct list substructs
  EXPECT_EQ( 8u, segments[4].size());  // list list + sublist 1,2
  EXPECT_EQ( 7u, segments[5].size());  // list list sublist 3,4,5

  checkStruct(builder);
  checkStruct(builder.asReader());
  checkStruct(PointerReader::getRoot(segment, nullptr, segment->getStartPtr(), 4)
      .getStruct(nullptr));
}

inline bool isNan(float f) { return f != f; }
inline bool isNan(double f) { return f != f; }

TEST(WireFormat, NanPatching) {
  EXPECT_EQ(0x7fc00000u, mask(kj::nan(), 0));
  EXPECT_TRUE(isNan(unmask<float>(0x7fc00000u, 0)));
  EXPECT_TRUE(isNan(unmask<float>(0x7fc00001u, 0)));
  EXPECT_TRUE(isNan(unmask<float>(0x7fc00005u, 0)));
  EXPECT_EQ(0x7fc00000u, mask(unmask<float>(0x7fc00000u, 0), 0));
  EXPECT_EQ(0x7ff8000000000000ull, mask((double)kj::nan(), 0));
  EXPECT_TRUE(isNan(unmask<double>(0x7ff8000000000000ull, 0)));
  EXPECT_TRUE(isNan(unmask<double>(0x7ff8000000000001ull, 0)));
  EXPECT_TRUE(isNan(unmask<double>(0x7ff8000000000005ull, 0)));
  EXPECT_EQ(0x7ff8000000000000ull, mask(unmask<double>(0x7ff8000000000000ull, 0), 0));
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
