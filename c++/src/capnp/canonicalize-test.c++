// Copyright (c) 2016 Sandstorm Development Group, Inc. and contributors
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

#include "message.h"
#include "any.h"
#include <kj/debug.h>
#include <kj/test.h>
#include "test-util.h"

namespace capnp {
namespace _ {  // private
using test::TestLists;
namespace {


KJ_TEST("canonicalize yields canonical message") {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  initTestMessage(root);

  auto canonicalWords = canonicalize(root.asReader());
  // Throws an exception on canonicalization failure.

  kj::ArrayPtr<const capnp::word> canonicalSegments[1] = {canonicalWords.asPtr()};
  capnp::SegmentArrayMessageReader canonicalReader(kj::arrayPtr(canonicalSegments, 1));

  KJ_ASSERT(AnyStruct::Reader(root.asReader()) ==
            AnyStruct::Reader(canonicalReader.getRoot<TestAllTypes>()));
}

KJ_TEST("canonicalize succeeds on empty struct") {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  canonicalize(root.asReader());  // Throws an exception on canoncalization failure.
}

KJ_TEST("data word with only its most significant bit set does not get truncated") {
  AlignedData<3> segment = {{
    // Struct pointer, body immediately follows, two data words
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,

    // First data word
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,

    // Second data word, all zero except most significant bit
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
  }};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(segment.words, 3)};
  SegmentArrayMessageReader messageReader(kj::arrayPtr(segments, 1));

  KJ_ASSERT(messageReader.isCanonical());
  auto canonicalWords = canonicalize(messageReader.getRoot<TestAllTypes>());

  // At one point this failed because an off-by-one bug in canonicalization
  // caused the second word of the data section to be truncated.
  ASSERT_EQ(canonicalWords.asBytes(), kj::arrayPtr(segment.bytes, 3 * 8));
}

KJ_TEST("INLINE_COMPOSITE data word with only its most significant bit set does not get truncated") {
  AlignedData<5> segment = {{
    // Struct pointer, body immediately follows, one pointer
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,

    // List pointer, no offset, inline composite, two words long
    0x01, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00,

    // Tag word, list has one element with two data words and no pointers
    0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,

    // First data word
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,

    // Second data word, all zero except most significant bit
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
  }};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(segment.words, 5)};
  SegmentArrayMessageReader messageReader(kj::arrayPtr(segments, 1));

  KJ_ASSERT(messageReader.isCanonical());
  auto canonicalWords = canonicalize(messageReader.getRoot<TestLists>());

  // At one point this failed because an off-by-one bug in canonicalization
  // caused the second word of the data section to be truncated.
  ASSERT_EQ(canonicalWords.asBytes(), kj::arrayPtr(segment.bytes, 5 * 8));
}

KJ_TEST("canonical non-null empty struct field") {
  AlignedData<4> nonNullEmptyStruct = {{
    // Struct pointer, body immediately follows, two pointer fields, no data.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,

    // First pointer field, struct, offset of 1, data size 1, no pointers.
    0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,

    // Non-null pointer to empty struct.
    0xfc, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,

    // Body of struct filled with non-zero data.
    0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
  }};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(nonNullEmptyStruct.words, 4)};
  SegmentArrayMessageReader messageReader(kj::arrayPtr(segments, 1));

  KJ_ASSERT(messageReader.isCanonical());
}

KJ_TEST("for pointers to empty structs, preorder is not canonical") {
  AlignedData<4> nonNullEmptyStruct = {{
    // Struct pointer, body immediately follows, two pointer fields, no data.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,

    // First pointer field, struct, offset of 1, data size 1, no pointers.
    0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,

    // Non-null pointer to empty struct. Offset puts it in "preorder". Would need to have
    // an offset of -1 to be canonical.
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // Body of struct filled with non-zero data.
    0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
  }};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(nonNullEmptyStruct.words, 4)};
  SegmentArrayMessageReader messageReader(kj::arrayPtr(segments, 1));

  KJ_ASSERT(!messageReader.isCanonical());
}

KJ_TEST("isCanonical requires pointer preorder") {
  AlignedData<5> misorderedSegment = {{
    //Struct pointer, data immediately follows, two pointer fields, no data
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
    //Pointer field 1, pointing to the last entry, data size 1, no pointer
    0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    //Pointer field 2, pointing to the next entry, data size 2, no pointer
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    //Data for field 2
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    //Data for field 1
    0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00
  }};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(misorderedSegment.words,
                                                       5)};
  SegmentArrayMessageReader outOfOrder(kj::arrayPtr(segments, 1));

  KJ_ASSERT(!outOfOrder.isCanonical());
}

KJ_TEST("isCanonical requires dense packing") {
   AlignedData<3> gapSegment = {{
    //Struct pointer, data after a gap
    0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    //The gap
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    //Data for field 1
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  }};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(gapSegment.words,
                                                       3)};
  SegmentArrayMessageReader gap(kj::arrayPtr(segments, 1));

  KJ_ASSERT(!gap.isCanonical());
}

KJ_TEST("isCanonical rejects multi-segment messages") {
  AlignedData<1> farPtr = {{
    //Far pointer to next segment
    0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  }};

  AlignedData<2> farTarget = {{
    //Struct pointer (needed to make the far pointer legal)
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    //Dummy data
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  }};

  kj::ArrayPtr<const word> segments[2] = {
    kj::arrayPtr(farPtr.words, 1),
    kj::arrayPtr(farTarget.words, 2)
  };

  SegmentArrayMessageReader multiSegmentMessage(kj::arrayPtr(segments, 2));
  KJ_ASSERT(!multiSegmentMessage.isCanonical());
}

KJ_TEST("isCanonical rejects zero segment messages") {
  SegmentArrayMessageReader zero(kj::arrayPtr((kj::ArrayPtr<const word>*)NULL,
                                               0));
  KJ_ASSERT(!zero.isCanonical());
}

KJ_TEST("isCanonical requires truncation of 0-valued struct fields") {
  AlignedData<2> nonTruncatedSegment = {{
    //Struct pointer, data immediately follows
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    //Default data value, should have been truncated
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  }};
  kj::ArrayPtr<const word> segments[1] = {
    kj::arrayPtr(nonTruncatedSegment.words, 3)
  };
  SegmentArrayMessageReader nonTruncated(kj::arrayPtr(segments, 1));

  KJ_ASSERT(!nonTruncated.isCanonical());
}

KJ_TEST("isCanonical rejects unused trailing words") {
   AlignedData<3> segment = {{
    // Struct pointer, data in next word
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,

    // Data section of struct
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,

    // Trailing zero word
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  }};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(segment.words, 3)};
  SegmentArrayMessageReader message(kj::arrayPtr(segments, 1));

  KJ_ASSERT(!message.isCanonical());
}

KJ_TEST("isCanonical accepts empty inline composite list of zero-sized structs") {
   AlignedData<3> segment = {{
    // Struct pointer, pointer in next word
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,

    // List pointer, inline composite, zero words long
    0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,

    // Tag word
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  }};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(segment.words, 3)};
  SegmentArrayMessageReader message(kj::arrayPtr(segments, 1));

  KJ_ASSERT(message.isCanonical());
}

KJ_TEST("isCanonical rejects inline composite list with inaccurate word-length") {
   AlignedData<6> segment = {{
    // Struct pointer, no offset, pointer section has two entries
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,

    // List pointer, offset of one, inline composite, two words long
    // (The list only needs to be one word long to hold its actual elements;
    // therefore this message is not canonical.)
    0x05, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00,

    // Struct pointer, offset two, data section has one word
    0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,

    // Tag word, struct, one element, one word data section
    0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,

    // Data section of struct element of list
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,

    // Data section of struct field in top-level struct
    0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
  }};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(segment.words, 6)};
  SegmentArrayMessageReader message(kj::arrayPtr(segments, 1));

  KJ_ASSERT(!message.isCanonical());
}

KJ_TEST("upgraded lists can be canonicalized") {
  AlignedData<7> upgradedList = {{
    //Struct pointer, data immediately follows, 4 pointer fields, no data
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
    //Three words of default pointers to get to the int16 list
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    //List pointer, 3 int16s.
    0x01, 0x00, 0x00, 0x00, 0x33, 0x00, 0x00, 0x00,
    //First two elements
    0x00, 0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06,
    //Last element
    0x07, 0x08, 0x09, 0x10, 0x00, 0x00, 0x00, 0x00
  }};
  kj::ArrayPtr<const word> segments[1] = {
    kj::arrayPtr(upgradedList.words, 7)
  };
  SegmentArrayMessageReader upgraded(kj::arrayPtr(segments, 1));

  auto root = upgraded.getRoot<TestLists>();
  canonicalize(root);
}

KJ_TEST("isCanonical requires truncation of 0-valued struct fields in all list members") {
  AlignedData<6> nonTruncatedList = {{
    //List pointer, composite,
    0x01, 0x00, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00,
    //Struct tag word, 2 structs, 2 data words per struct
    0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    //Data word non-null
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    //Null trailing word
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    //Data word non-null
    0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
    //Null trailing word
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  }};
  kj::ArrayPtr<const word> segments[1] = {
    kj::arrayPtr(nonTruncatedList.words, 6)
  };
  SegmentArrayMessageReader nonTruncated(kj::arrayPtr(segments, 1));

  KJ_ASSERT(!nonTruncated.isCanonical());
}

KJ_TEST("primitive list with nonzero padding") {
   AlignedData<3> segment = {{
     // Struct, one pointer field.
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,

     // List of three byte-sized elements.
     0x01, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00,

     // Fourth byte is non-zero!
     0x01, 0x02, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00,
  }};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(segment.words, 3)};
  SegmentArrayMessageReader message(kj::arrayPtr(segments, 1));

  KJ_ASSERT(!message.isCanonical());

  auto canonicalWords = canonicalize(message.getRoot<test::TestAnyPointer>());

  AlignedData<3> canonicalSegment = {{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00,
    0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
  }};

  ASSERT_EQ(canonicalWords.asBytes(), kj::arrayPtr(canonicalSegment.bytes, 3 * 8));
}

KJ_TEST("bit list with nonzero padding") {
   AlignedData<3> segment = {{
     // Struct, one pointer field.
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,

     // List of eleven bit-sized elements.
     0x01, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00,

     // Twelfth bit is non-zero!
     0xee, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  }};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(segment.words, 3)};
  SegmentArrayMessageReader message(kj::arrayPtr(segments, 1));

  KJ_ASSERT(!message.isCanonical());

  auto canonicalWords = canonicalize(message.getRoot<test::TestAnyPointer>());

  AlignedData<3> canonicalSegment = {{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00,
    0xee, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  }};

  ASSERT_EQ(canonicalWords.asBytes(), kj::arrayPtr(canonicalSegment.bytes, 3 * 8));
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
