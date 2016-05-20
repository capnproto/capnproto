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


KJ_TEST("canonicalize yields cannonical message") {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  initTestMessage(root);

  canonicalize(root.asReader());
  //Will assert if canonicalize failed to do so
}

KJ_TEST("isCanonical requires pointer preorder") {
  AlignedData<5> misorderedSegment = {{
    //Struct pointer, data immediately follows, two pointer fields, no data
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
    //Pointer field 1, pointing to the last entry, data size 1, no pointer
    0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    //Pointer field 2, pointing to the next entry, data size 2, no pointer
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    //Data for field 2
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    //Data for field 1
    0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00
  }};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(misorderedSegment.words,
                                                       3)};
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
}  // namespace
}  // namespace _ (private)
}  // namespace capnp
