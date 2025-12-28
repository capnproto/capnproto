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

#include "message.h"
#include "test-util.h"
#include <kj/array.h>
#include <kj/vector.h>
#include <kj/debug.h>
#include <kj/compat/gtest.h>
#include <stdlib.h>

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

KJ_TEST("MallocMessageBuilder with NO_ZERO_MEMORY strategy") {
  // Verify that the new InitializationStrategy API works for basic usage.
  MallocMessageBuilder builder(1024, AllocationStrategy::FIXED_SIZE,
                               MallocMessageBuilder::InitializationStrategy::NO_ZERO_MEMORY);
  auto root = builder.initRoot<TestAllTypes>();
  root.setInt64Field(12345);
  KJ_EXPECT(root.getInt64Field() == 12345);
}

KJ_TEST("SECURITY: Uninitialized DATA zeroes padding bytes (Info Leak Prevention)") {
  // Setup a "Dirty" Scratch Buffer filled with 0xAA.
  // This simulates a scenario where malloc returns memory containing sensitive residual data.
  byte dirtyBuffer[1024];
  memset(dirtyBuffer, 0xAA, sizeof(dirtyBuffer));
  kj::ArrayPtr<word> scratch(reinterpret_cast<word*>(dirtyBuffer), sizeof(dirtyBuffer) / sizeof(word));

  // Create a builder that opts out of zeroing.
  MallocMessageBuilder builder(scratch, AllocationStrategy::FIXED_SIZE,
                               MallocMessageBuilder::InitializationStrategy::NO_ZERO_MEMORY);

  auto root = builder.initRoot<TestAllTypes>();

  // Allocate 3 bytes of DATA.
  // Physical layout: 1 word (8 bytes) allocated.
  // Bytes [0, 1, 2] are the data body.
  // Bytes [3, 4, 5, 6, 7] are alignment padding.
  auto data = root.initDataFieldUninitialized(3);
  const byte* rawPtr = data.begin();

  // CHECK 1: Performance verification.
  // The body bytes [0..2] should remain 0xAA.
  // This proves that we successfully skipped the memset for the data payload.
  KJ_EXPECT(rawPtr[0] == 0xAA);
  KJ_EXPECT(rawPtr[1] == 0xAA);
  KJ_EXPECT(rawPtr[2] == 0xAA);

  // CHECK 2: Security verification.
  // The padding bytes [3..7] MUST be zeroed.
  // Leaving these as 0xAA would constitute an Information Leak vulnerability.
  KJ_EXPECT(rawPtr[3] == 0x00);
  KJ_EXPECT(rawPtr[4] == 0x00);
  KJ_EXPECT(rawPtr[5] == 0x00);
  KJ_EXPECT(rawPtr[6] == 0x00);
  KJ_EXPECT(rawPtr[7] == 0x00);
}

KJ_TEST("SAFETY: Structs are always zeroed even with NO_ZERO_MEMORY") {
  // Setup a dirty buffer (filled with 0xBB).
  byte dirtyBuffer[1024];
  memset(dirtyBuffer, 0xBB, sizeof(dirtyBuffer));
  kj::ArrayPtr<word> scratch(reinterpret_cast<word*>(dirtyBuffer), sizeof(dirtyBuffer) / sizeof(word));

  MallocMessageBuilder builder(scratch, AllocationStrategy::FIXED_SIZE,
                               MallocMessageBuilder::InitializationStrategy::NO_ZERO_MEMORY);

  // Initialize a Struct.
  // Even though the builder is in NO_ZERO_MEMORY mode, Struct allocations must always
  // enforce zero-initialization to ensure pointer validity and default values.
  auto root = builder.initRoot<TestAllTypes>();

  // Verify fields are 0/false, not 0xBB.
  KJ_EXPECT(root.getInt64Field() == 0);
  KJ_EXPECT(root.getUInt32Field() == 0);
  KJ_EXPECT(root.getBoolField() == false);
}

KJ_TEST("Corner Case: Uninitialized DATA with exact word alignment") {
  // Test boundary condition: Size is exactly 8 bytes (No padding).
  byte dirtyBuffer[1024];
  memset(dirtyBuffer, 0xCC, sizeof(dirtyBuffer));
  kj::ArrayPtr<word> scratch(reinterpret_cast<word*>(dirtyBuffer), sizeof(dirtyBuffer) / sizeof(word));

  MallocMessageBuilder builder(scratch, AllocationStrategy::FIXED_SIZE,
                               MallocMessageBuilder::InitializationStrategy::NO_ZERO_MEMORY);
  auto root = builder.initRoot<TestAllTypes>();

  // 8 bytes -> Exactly 1 word. Padding size is 0.
  // The padding zeroing logic must not crash or overwrite the next word.
  auto data = root.initDataFieldUninitialized(8);
  const byte* rawPtr = data.begin();

  // Body is dirty (0xCC).
  for(int i = 0; i < 8; ++i) {
    KJ_EXPECT(rawPtr[i] == 0xCC);
  }

  // Verify we didn't touch the *next* word (which should still be 0xCC).
  // Note: Accessing rawPtr[8] is technically out of bounds for the 'data' blob,
  // but valid within our controlled scratch buffer context.
  KJ_EXPECT(rawPtr[8] == 0xCC);
}

KJ_TEST("Corner Case: Uninitialized DATA with 1 byte padding") {
  // Test boundary condition: Size is 7 bytes (1 byte padding).
  byte dirtyBuffer[1024];
  memset(dirtyBuffer, 0xDD, sizeof(dirtyBuffer));
  kj::ArrayPtr<word> scratch(reinterpret_cast<word*>(dirtyBuffer), sizeof(dirtyBuffer) / sizeof(word));

  MallocMessageBuilder builder(scratch, AllocationStrategy::FIXED_SIZE,
                               MallocMessageBuilder::InitializationStrategy::NO_ZERO_MEMORY);
  auto root = builder.initRoot<TestAllTypes>();

  auto data = root.initDataFieldUninitialized(7);
  const byte* rawPtr = data.begin();

  // Body [0..6] is dirty.
  for(int i = 0; i < 7; ++i) {
    KJ_EXPECT(rawPtr[i] == 0xDD);
  }

  // The single padding byte [7] must be zeroed.
  KJ_EXPECT(rawPtr[7] == 0x00);
}

// Helper class for the Custom Builder test.
// Simulates a user-defined MessageBuilder that uses a custom allocator (e.g., malloc)
// and does not zero memory, overriding isAllocationZeroed() to return false.
class DirtyMallocMessageBuilder : public MessageBuilder {
public:
  DirtyMallocMessageBuilder()
      : MessageBuilder(kj::heapArray<SegmentInit>({
          // Initialize with nullptr. The Arena will call allocateSegment() on first use.
          SegmentInit { nullptr, 0, false }
        })) {}

  ~DirtyMallocMessageBuilder() {
    for (void* ptr : allocations) {
      free(ptr);
    }
  }

  // CRITICAL: Explicitly declare that this allocator provides dirty memory.
  bool isAllocationZeroed() const override { return false; }

  kj::ArrayPtr<word> allocateSegment(uint minimumSize) override {
    size_t sizeBytes = minimumSize * sizeof(word);
    void* ptr = malloc(sizeBytes);
    KJ_ASSERT(ptr != nullptr);

    // Deliberately fill with garbage (0xEE) to verify lazy zeroing logic.
    memset(ptr, 0xEE, sizeBytes);

    allocations.add(ptr);
    return kj::arrayPtr(reinterpret_cast<word*>(ptr), minimumSize);
  }

private:
  kj::Vector<void*> allocations;
};

KJ_TEST("CustomMessageBuilder: Lazy zeroing works with custom dirty allocator") {
  DirtyMallocMessageBuilder builder;

  // 1. Verify Safety: Structs must be zeroed.
  auto root = builder.initRoot<TestAllTypes>();
  KJ_EXPECT(root.getInt64Field() == 0);
  KJ_EXPECT(root.getBoolField() == false);

  // 2. Verify Performance & Security for Data Blob.
  // Allocate 3 bytes. Expect 0xEE in body, 0x00 in padding.
  auto data = root.initDataFieldUninitialized(3);
  const byte* ptr = data.begin();

  // Check Body (Dirty / Performance)
  KJ_EXPECT(ptr[0] == 0xEE);
  KJ_EXPECT(ptr[1] == 0xEE);
  KJ_EXPECT(ptr[2] == 0xEE);

  // Check Padding (Clean / Security)
  KJ_EXPECT(ptr[3] == 0x00);
  KJ_EXPECT(ptr[4] == 0x00);
  KJ_EXPECT(ptr[5] == 0x00);
  KJ_EXPECT(ptr[6] == 0x00);
  KJ_EXPECT(ptr[7] == 0x00);
}

// TODO(test):  More tests.

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
