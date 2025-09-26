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

#include <cstdlib>
#include <ctime>
#include "message.h"
#include "test-util.h"
#include <kj/array.h>
#include <kj/vector.h>
#include <kj/debug.h>
#include <kj/compat/gtest.h>

namespace capnp {
namespace _ {  // private
namespace {

TEST(Message, MallocBuilderWithFirstSegment) {
  word scratch[16];
  memset(scratch, 0, sizeof(scratch));
  MallocMessageBuilder builder(kj::arrayPtr(scratch, 16), AllocationStrategy::FIXED_SIZE);

  kj::ArrayPtr<word> segment = builder.allocateSegment(1);
  EXPECT_EQ(scratch, segment.begin());
  EXPECT_EQ(16u, segment.size());

  segment = builder.allocateSegment(1);
  EXPECT_NE(scratch, segment.begin());
  EXPECT_EQ(16u, segment.size());

  segment = builder.allocateSegment(1);
  EXPECT_NE(scratch, segment.begin());
  EXPECT_EQ(16u, segment.size());
}

class TestInitMessageBuilder: public MessageBuilder {
public:
  TestInitMessageBuilder(kj::ArrayPtr<SegmentInit> segments): MessageBuilder(segments) {}

  kj::ArrayPtr<word> allocateSegment(uint minimumSize) override {
    auto array = kj::heapArray<word>(minimumSize);
    memset(array.begin(), 0, array.asBytes().size());
    allocations.add(kj::mv(array));
    return allocations.back();
  }

  kj::Vector<kj::Array<word>> allocations;
};

TEST(Message, MessageBuilderInit) {
  MallocMessageBuilder builder(2048);
  initTestMessage(builder.getRoot<TestAllTypes>());

  // Pull the segments out and make a segment init table out of them.
  //
  // We const_cast for simplicity of implementing the test, but you shouldn't do that at home. :)
  auto segs = builder.getSegmentsForOutput();
  ASSERT_EQ(1, segs.size());

  auto segInits = KJ_MAP(seg, segs) -> MessageBuilder::SegmentInit {
    return { kj::arrayPtr(const_cast<word*>(seg.begin()), seg.size()), seg.size() };
  };

  // Init a new builder from the old segments.
  TestInitMessageBuilder builder2(segInits);
  checkTestMessage(builder2.getRoot<TestAllTypes>());

  // Verify that they're really using the same underlying memory.
  builder2.getRoot<TestAllTypes>().setInt64Field(123321);
  EXPECT_EQ(123321, builder.getRoot<TestAllTypes>().getInt64Field());

  // Force builder2 to allocate new space.
  EXPECT_EQ(0, builder2.allocations.size());
  builder2.getRoot<TestAllTypes>().setTextField("foobarbaz");
  EXPECT_EQ(1, builder2.allocations.size());
}

TEST(Message, MessageBuilderInitMultiSegment) {
  // Same as previous test, but with a message containing many segments.

  MallocMessageBuilder builder(1, AllocationStrategy::FIXED_SIZE);
  initTestMessage(builder.getRoot<TestAllTypes>());

  // Pull the segments out and make a segment init table out of them.
  //
  // We const_cast for simplicity of implementing the test, but you shouldn't do that at home. :)
  auto segs = builder.getSegmentsForOutput();
  ASSERT_NE(1, segs.size());

  auto segInits = KJ_MAP(seg, segs) -> MessageBuilder::SegmentInit {
    return { kj::arrayPtr(const_cast<word*>(seg.begin()), seg.size()), seg.size() };
  };

  // Init a new builder from the old segments.
  TestInitMessageBuilder builder2(segInits);
  checkTestMessage(builder2.getRoot<TestAllTypes>());

  // Verify that they're really using the same underlying memory.
  builder2.getRoot<TestAllTypes>().setInt64Field(123321);
  EXPECT_EQ(123321, builder.getRoot<TestAllTypes>().getInt64Field());

  // Force builder2 to allocate new space.
  EXPECT_EQ(0, builder2.allocations.size());
  builder2.getRoot<TestAllTypes>().setTextField("foobarbaz");
  EXPECT_EQ(1, builder2.allocations.size());
}

TEST(Message, MessageBuilderInitSpaceAvailable) {
  word buffer[2048];
  memset(buffer, 0, sizeof(buffer));
  MallocMessageBuilder builder(buffer);
  initTestMessage(builder.getRoot<TestAllTypes>());

  // Find out how much space in `buffer` was used in order to use in initializing the new message.
  auto segs = builder.getSegmentsForOutput();
  ASSERT_EQ(1, segs.size());
  KJ_ASSERT(segs[0].begin() == buffer);

  MessageBuilder::SegmentInit init = { kj::ArrayPtr<word>(buffer), segs[0].size() };

  // Init a new builder from the old segments.
  TestInitMessageBuilder builder2(kj::arrayPtr(init));
  checkTestMessage(builder2.getRoot<TestAllTypes>());

  // Verify that they're really using the same underlying memory.
  builder2.getRoot<TestAllTypes>().setInt64Field(123321);
  EXPECT_EQ(123321, builder.getRoot<TestAllTypes>().getInt64Field());

  // Ask builder2 to allocate new space. It should go into the free space at the end of the
  // segment.
  EXPECT_EQ(0, builder2.allocations.size());
  builder2.getRoot<TestAllTypes>().setTextField("foobarbaz");
  EXPECT_EQ(0, builder2.allocations.size());

  EXPECT_EQ(kj::implicitCast<void*>(buffer + segs[0].size()),
            kj::implicitCast<void*>(builder2.getRoot<TestAllTypes>().getTextField().begin()));
}

TEST(Message, ReadWriteDataStruct) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<TestAllTypes>();

  root.setUInt32Field(123);
  root.setFloat64Field(1.5);
  root.setTextField("foo");

  auto copy = readDataStruct<TestAllTypes>(writeDataStruct(root));
  EXPECT_EQ(123, copy.getUInt32Field());
  EXPECT_EQ(1.5, copy.getFloat64Field());
  EXPECT_FALSE(copy.hasTextField());

  checkTestMessageAllZero(readDataStruct<TestAllTypes>(nullptr));
  checkTestMessageAllZero(defaultValue<TestAllTypes>());
}

KJ_TEST("clone()") {
  MallocMessageBuilder builder(2048);
  initTestMessage(builder.getRoot<TestAllTypes>());

  auto copy = clone(builder.getRoot<TestAllTypes>().asReader());
  checkTestMessage(*copy);
}

#if !CAPNP_ALLOW_UNALIGNED
KJ_TEST("disallow unaligned") {
  union {
    char buffer[16];
    word align;
  };
  memset(buffer, 0, sizeof(buffer));

  auto unaligned = kj::arrayPtr(reinterpret_cast<word*>(buffer + 1), 1);

  kj::ArrayPtr<const word> segments[1] = {unaligned};
  SegmentArrayMessageReader message(segments);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("unaligned", message.getRoot<TestAllTypes>());
}
#endif

KJ_TEST("MessageBuilder::sizeInWords()") {
  capnp::MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();
  initTestMessage(root);

  size_t expected = root.totalSize().wordCount + 1;

  KJ_EXPECT(builder.sizeInWords() == expected);

  auto segments = builder.getSegmentsForOutput();
  size_t total = 0;
  for (auto& segment: segments) {
    total += segment.size();
  }
  KJ_EXPECT(total == expected);

  capnp::SegmentArrayMessageReader reader(segments);
  checkTestMessage(reader.getRoot<TestAllTypes>());
  KJ_EXPECT(reader.sizeInWords() == expected);
}

class MyCustomMessageBuilder : public MessageBuilder {
public:
  MyCustomMessageBuilder(BuilderOptions options): MessageBuilder(options) {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
  }

  kj::ArrayPtr<word> allocateSegment(uint minimumSize) override {
    auto array = kj::heapArray<word>(minimumSize);
    auto bytes = array.asBytes();
    // mock dirty memory
    for (size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<uint8_t>(std::rand() % 256);
    }
    allocations.add(kj::mv(array));
    return allocations.back();
  }

  kj::Vector<kj::Array<word>> allocations;
};

TEST(Message, LazyZeroCustomBuilder_DataDirty_OthersZero) {
  // Enable lazy-zero and skip zeroing for DATA type.
  BuilderOptions options;
  BuilderOptions::LazyZeroSegmentAlloc lazy;
  lazy.skipLazyZeroTypes.insert(schema::Type::DATA);
  options.lazyZeroSegmentAlloc = &lazy;

  // Use the custom allocator that returns "dirty" memory.
  MyCustomMessageBuilder builder(options);

  // Init root
  auto root = builder.initRoot<TestAllTypes>();

  // Allocate a DATA field (size 64) — because we skipped zeroing for DATA,
  // the returned buffer should contain the allocator's random bytes.
  auto dataBuf = root.initDataField(64);

  // Check DATA is not all zero (i.e., "dirty")
  bool dataAllZero = true;
  for (size_t i = 0; i < dataBuf.size(); ++i) {
    if (dataBuf[i] != 0) { dataAllZero = false; break; }
  }
  EXPECT_FALSE(dataAllZero);

  // Check other primitive/pointer fields are zero / not present by default.
  // (These names follow the TestAllTypes generated accessors used elsewhere in tests.)
  EXPECT_EQ(0u, root.getUInt32Field());
  EXPECT_EQ(0, root.getInt64Field());
  EXPECT_EQ(0.0, root.getFloat64Field());
  EXPECT_FALSE(root.hasTextField());
  EXPECT_FALSE(root.hasStructField()); // if there is a nested struct field, it should be null

  // Also sanity-check that writing/reading doesn't crash: getSegmentsForOutput() is callable.
  auto segs = builder.getSegmentsForOutput();
  EXPECT_GE(segs.size(), 1u);
}

TEST(Message, LazyZeroCustomBuilder_MultipleDataFieldsDirty_OtherZero) {
  // Enable lazy-zero and skip zeroing for DATA type.
  BuilderOptions options;
  BuilderOptions::LazyZeroSegmentAlloc lazy;
  lazy.skipLazyZeroTypes.insert(schema::Type::DATA);
  options.lazyZeroSegmentAlloc = &lazy;

  MyCustomMessageBuilder builder(options);
  auto root = builder.initRoot<TestAllTypes>();

  // Allocate two DATA fields with different sizes.
  auto d1 = root.initDataField(64);
  auto d2 = root.initDataField(128);

  // Verify both DATA buffers are not all zero (i.e. "dirty").
  bool d1AllZero = true;
  for (auto b : d1) { if (b != 0) { d1AllZero = false; break; } }
  bool d2AllZero = true;
  for (auto b : d2) { if (b != 0) { d2AllZero = false; break; } }

  EXPECT_FALSE(d1AllZero);
  EXPECT_FALSE(d2AllZero);

  // Other scalar/pointer fields should still be default (0 / not present).
  EXPECT_EQ(0u, root.getUInt32Field());
  EXPECT_EQ(0, root.getInt64Field());
  EXPECT_EQ(0.0, root.getFloat64Field());
  EXPECT_FALSE(root.hasTextField());
}

TEST(Message, LazyZeroCustomBuilder_DataWriteAndReadback_Persists) {
  // Setup builder with lazy-zero skip for DATA.
  BuilderOptions options;
  BuilderOptions::LazyZeroSegmentAlloc lazy;
  lazy.skipLazyZeroTypes.insert(schema::Type::DATA);
  options.lazyZeroSegmentAlloc = &lazy;

  MyCustomMessageBuilder builder(options);
  auto root = builder.initRoot<TestAllTypes>();

  // Fill DATA with a recognizable pattern.
  const size_t N = 64;
  auto data = root.initDataField(N);
  for (size_t i = 0; i < N; ++i) data[i] = static_cast<capnp::byte>(i & 0xFF);

  // Read back by creating a SegmentArrayMessageReader from the builder segments.
  auto segs = builder.getSegmentsForOutput();
  capnp::SegmentArrayMessageReader readerFromSegments(segs);
  auto readBack = readerFromSegments.getRoot<TestAllTypes>();
  auto readData = readBack.getDataField();

  ASSERT_EQ(readData.size(), N);
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(readData[i], static_cast<capnp::byte>(i & 0xFF));
  }
}

TEST(Message, LazyZeroCustomBuilder_ClonePreservesDirtyData_ViaSegments) {
  // Use lazy-zero skipping for DATA to keep allocator's dirty bytes.
  BuilderOptions options;
  BuilderOptions::LazyZeroSegmentAlloc lazy;
  lazy.skipLazyZeroTypes.insert(schema::Type::DATA);
  options.lazyZeroSegmentAlloc = &lazy;

  MyCustomMessageBuilder builder(options);
  auto root = builder.initRoot<TestAllTypes>();

  // Allocate DATA and capture original bytes.
  const size_t SIZE = 100;
  auto data = root.initDataField(SIZE);
  kj::Vector<capnp::byte> original;
  original.reserve(SIZE);
  for (size_t i = 0; i < SIZE; ++i) {
    original.add(data[i]);
  }

  // Export segments and re-read using SegmentArrayMessageReader to simulate clone/readback.
  auto segs = builder.getSegmentsForOutput();
  capnp::SegmentArrayMessageReader sar(segs);
  auto readBack = sar.getRoot<TestAllTypes>();
  auto copiedData = readBack.getDataField();

  ASSERT_EQ(copiedData.size(), SIZE);
  for (size_t i = 0; i < SIZE; ++i) {
    EXPECT_EQ(copiedData[i], original[i]);
  }
}

TEST(Message, LazyZeroCustomBuilder_ManySmallDataAllocations_Stress) {
  // Setup builder and lazy-zero skip for DATA.
  BuilderOptions options;
  BuilderOptions::LazyZeroSegmentAlloc lazy;
  lazy.skipLazyZeroTypes.insert(schema::Type::DATA);
  options.lazyZeroSegmentAlloc = &lazy;

  MyCustomMessageBuilder builder(options);
  auto root = builder.initRoot<TestAllTypes>();

  // Many small DATA allocations to stress allocation paths.
  const int COUNT = 256;
  kj::Vector< kj::ArrayPtr<capnp::byte> > allocated;
  allocated.reserve(COUNT);

  for (int i = 0; i < COUNT; ++i) {
    auto d = root.initDataField(8 + (i % 16));
    allocated.add(d);

    // Quick check: each data allocation should have at least one non-zero byte.
    bool allZero = true;
    for (auto b : d) { if (b != 0) { allZero = false; break; } }
    EXPECT_FALSE(allZero);
  }

  // Ensure at least one segment exists.
  auto segs = builder.getSegmentsForOutput();
  EXPECT_GE(segs.size(), 1u);
}

TEST(Message, LazyZeroCustomBuilder_PartialOverwriteLeavesRestDirtyForData) {
  // Setup lazy-zero skip for DATA.
  BuilderOptions options;
  BuilderOptions::LazyZeroSegmentAlloc lazy;
  lazy.skipLazyZeroTypes.insert(schema::Type::DATA);
  options.lazyZeroSegmentAlloc = &lazy;

  MyCustomMessageBuilder builder(options);
  auto root = builder.initRoot<TestAllTypes>();

  const size_t SIZE = 64;
  auto data = root.initDataField(SIZE);

  // Overwrite only the first half with zeros, leave the second half untouched.
  for (size_t i = 0; i < SIZE / 2; ++i) data[i] = 0;

  // The second half should remain dirty (not all zero).
  bool secondHalfAllZero = true;
  for (size_t i = SIZE / 2; i < SIZE; ++i) {
    if (data[i] != 0) { secondHalfAllZero = false; break; }
  }
  EXPECT_FALSE(secondHalfAllZero);

  // Other fields remain default.
  EXPECT_EQ(0u, root.getUInt32Field());
  EXPECT_FALSE(root.hasTextField());
}

// TODO(test):  More tests.

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
