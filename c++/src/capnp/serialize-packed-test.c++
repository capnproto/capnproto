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

#include "serialize-packed.h"
#include <kj/debug.h>
#include <kj/compat/gtest.h>
#include <string>
#include <stdlib.h>
#include "test-util.h"

namespace capnp {
namespace _ {  // private
namespace {

class TestPipe: public kj::BufferedInputStream, public kj::OutputStream {
public:
  TestPipe()
      : preferredReadSize(kj::maxValue), readPos(0) {}
  explicit TestPipe(size_t preferredReadSize)
      : preferredReadSize(preferredReadSize), readPos(0) {}
  ~TestPipe() {}

  const std::string& getData() { return data; }

  kj::ArrayPtr<const byte> getArray() {
    return kj::arrayPtr(reinterpret_cast<const byte*>(data.data()), data.size());
  }

  void resetRead(size_t preferredReadSize = kj::maxValue) {
    readPos = 0;
    this->preferredReadSize = preferredReadSize;
  }

  bool allRead() {
    return readPos == data.size();
  }

  void clear(size_t preferredReadSize = kj::maxValue) {
    resetRead(preferredReadSize);
    data.clear();
  }

  void write(const void* buffer, size_t size) override {
    data.append(reinterpret_cast<const char*>(buffer), size);
  }

  size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    KJ_ASSERT(maxBytes <= data.size() - readPos, "Overran end of stream.");
    size_t amount = kj::min(maxBytes, kj::max(minBytes, preferredReadSize));
    memcpy(buffer, data.data() + readPos, amount);
    readPos += amount;
    return amount;
  }

  void skip(size_t bytes) override {
    KJ_ASSERT(bytes <= data.size() - readPos, "Overran end of stream.");
    readPos += bytes;
  }

  kj::ArrayPtr<const byte> tryGetReadBuffer() override {
    size_t amount = kj::min(data.size() - readPos, preferredReadSize);
    return kj::arrayPtr(reinterpret_cast<const byte*>(data.data() + readPos), amount);
  }

private:
  size_t preferredReadSize;
  std::string data;
  std::string::size_type readPos;
};

void expectPacksTo(kj::ArrayPtr<const byte> unpacked, kj::ArrayPtr<const byte> packed) {
  TestPipe pipe;

  EXPECT_EQ(unpacked.size(), computeUnpackedSizeInWords(packed) * sizeof(word));

  // -----------------------------------------------------------------
  // write

  {
    kj::BufferedOutputStreamWrapper bufferedOut(pipe);
    PackedOutputStream packedOut(bufferedOut);
    packedOut.write(unpacked.begin(), unpacked.size());
  }

  if (pipe.getData() != std::string(packed.asChars().begin(), packed.asChars().size())) {
    KJ_FAIL_ASSERT("Tried to pack `unpacked`, expected `packed`, got `pipe.getData()`",
                   unpacked, packed, pipe.getData());
    return;
  }

  // -----------------------------------------------------------------
  // read

  kj::Array<byte> roundTrip = kj::heapArray<byte>(unpacked.size());

  {
    PackedInputStream packedIn(pipe);
    packedIn.InputStream::read(roundTrip.begin(), roundTrip.size());
    EXPECT_TRUE(pipe.allRead());
  }

  if (memcmp(roundTrip.begin(), unpacked.begin(), unpacked.size()) != 0) {
    KJ_FAIL_ASSERT("Tried to unpack `packed`, expected `unpacked`, got `roundTrip`",
                   packed, unpacked, roundTrip);
    return;
  }

  for (uint blockSize = 1; blockSize < packed.size(); blockSize <<= 1) {
    pipe.resetRead(blockSize);

    {
      PackedInputStream packedIn(pipe);
      packedIn.InputStream::read(roundTrip.begin(), roundTrip.size());
      EXPECT_TRUE(pipe.allRead());
    }

    if (memcmp(roundTrip.begin(), unpacked.begin(), unpacked.size()) != 0) {
      KJ_FAIL_ASSERT("Tried to unpack `packed`, expected `unpacked`, got `roundTrip`",
                     packed, blockSize, unpacked, roundTrip);
    }
  }

  // -----------------------------------------------------------------
  // skip

  pipe.resetRead();

  {
    PackedInputStream packedIn(pipe);
    packedIn.skip(unpacked.size());
    EXPECT_TRUE(pipe.allRead());
  }

  for (uint blockSize = 1; blockSize < packed.size(); blockSize <<= 1) {
    pipe.resetRead(blockSize);

    {
      PackedInputStream packedIn(pipe);
      packedIn.skip(unpacked.size());
      EXPECT_TRUE(pipe.allRead());
    }
  }

  pipe.clear();

  // -----------------------------------------------------------------
  // write / read multiple

  {
    kj::BufferedOutputStreamWrapper bufferedOut(pipe);
    PackedOutputStream packedOut(bufferedOut);
    for (uint i = 0; i < 5; i++) {
      packedOut.write(unpacked.begin(), unpacked.size());
    }
  }

  for (uint i = 0; i < 5; i++) {
    PackedInputStream packedIn(pipe);
    packedIn.InputStream::read(&*roundTrip.begin(), roundTrip.size());

    if (memcmp(roundTrip.begin(), unpacked.begin(), unpacked.size()) != 0) {
      KJ_FAIL_ASSERT("Tried to unpack `packed`, expected `unpacked`, got `roundTrip`",
                     packed, i, unpacked, roundTrip);
    }
  }

  EXPECT_TRUE(pipe.allRead());
}

#ifdef __CDT_PARSER__
// CDT doesn't seem to understand these initializer lists.
#define expectPacksTo(...)
#endif

TEST(Packed, SimplePacking) {
  expectPacksTo({}, {});
  expectPacksTo({0,0,0,0,0,0,0,0}, {0,0});
  expectPacksTo({0,0,12,0,0,34,0,0}, {0x24,12,34});
  expectPacksTo({1,3,2,4,5,7,6,8}, {0xff,1,3,2,4,5,7,6,8,0});
  expectPacksTo({0,0,0,0,0,0,0,0,1,3,2,4,5,7,6,8}, {0,0,0xff,1,3,2,4,5,7,6,8,0});
  expectPacksTo({0,0,12,0,0,34,0,0,1,3,2,4,5,7,6,8}, {0x24,12,34,0xff,1,3,2,4,5,7,6,8,0});
  expectPacksTo({1,3,2,4,5,7,6,8,8,6,7,4,5,2,3,1}, {0xff,1,3,2,4,5,7,6,8,1,8,6,7,4,5,2,3,1});

  expectPacksTo(
      {1,2,3,4,5,6,7,8, 1,2,3,4,5,6,7,8, 1,2,3,4,5,6,7,8, 1,2,3,4,5,6,7,8, 0,2,4,0,9,0,5,1},
      {0xff,1,2,3,4,5,6,7,8, 3, 1,2,3,4,5,6,7,8, 1,2,3,4,5,6,7,8, 1,2,3,4,5,6,7,8, 0xd6,2,4,9,5,1});
  expectPacksTo(
      {1,2,3,4,5,6,7,8, 1,2,3,4,5,6,7,8, 6,2,4,3,9,0,5,1, 1,2,3,4,5,6,7,8, 0,2,4,0,9,0,5,1},
      {0xff,1,2,3,4,5,6,7,8, 3, 1,2,3,4,5,6,7,8, 6,2,4,3,9,0,5,1, 1,2,3,4,5,6,7,8, 0xd6,2,4,9,5,1});

  expectPacksTo(
      {8,0,100,6,0,1,1,2, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,1,0,2,0,3,1},
      {0xed,8,100,6,1,1,2, 0,2, 0xd4,1,2,3,1});
}

// =======================================================================================

class TestMessageBuilder: public MallocMessageBuilder {
  // A MessageBuilder that tries to allocate an exact number of total segments, by allocating
  // minimum-size segments until it reaches the number, then allocating one large segment to
  // finish.

public:
  explicit TestMessageBuilder(uint desiredSegmentCount)
      : MallocMessageBuilder(0, AllocationStrategy::FIXED_SIZE),
        desiredSegmentCount(desiredSegmentCount) {}
  ~TestMessageBuilder() {
    EXPECT_EQ(0u, desiredSegmentCount);
  }

  kj::ArrayPtr<word> allocateSegment(uint minimumSize) override {
    if (desiredSegmentCount <= 1) {
      if (desiredSegmentCount < 1) {
        ADD_FAILURE() << "Allocated more segments than desired.";
      } else {
        --desiredSegmentCount;
      }
      return MallocMessageBuilder::allocateSegment(SUGGESTED_FIRST_SEGMENT_WORDS);
    } else {
      --desiredSegmentCount;
      return MallocMessageBuilder::allocateSegment(minimumSize);
    }
  }

private:
  uint desiredSegmentCount;
};

TEST(Packed, RoundTrip) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripScratchSpace) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  word scratch[1024];
  PackedMessageReader reader(pipe, ReaderOptions(), kj::ArrayPtr<word>(scratch, 1024));
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripLazy) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripOddSegmentCount) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripOddSegmentCountLazy) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripEvenSegmentCount) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripEvenSegmentCountLazy) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripTwoMessages) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestMessageBuilder builder2(1);
  builder2.initRoot<TestAllTypes>().setTextField("Second message.");

  TestPipe pipe;
  writePackedMessage(pipe, builder);
  writePackedMessage(pipe, builder2);

  EXPECT_EQ(computeSerializedSizeInWords(builder) + computeSerializedSizeInWords(builder2),
            computeUnpackedSizeInWords(pipe.getArray()));

  {
    PackedMessageReader reader(pipe);
    checkTestMessage(reader.getRoot<TestAllTypes>());
  }

  {
    PackedMessageReader reader(pipe);
    EXPECT_EQ("Second message.", reader.getRoot<TestAllTypes>().getTextField());
  }
}

// =======================================================================================

TEST(Packed, RoundTripAllZero) {
  TestMessageBuilder builder(1);
  builder.initRoot<TestAllTypes>();

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  checkTestMessageAllZero(reader.getRoot<TestAllTypes>());

  // Segment table packs to 2 bytes.
  // Root pointer packs to 3 bytes.
  // Content packs to 2 bytes (zero span).
  EXPECT_LE(pipe.getData().size(), 7u);
}

TEST(Packed, RoundTripAllZeroScratchSpace) {
  TestMessageBuilder builder(1);
  builder.initRoot<TestAllTypes>();

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  word scratch[1024];
  PackedMessageReader reader(pipe, ReaderOptions(), kj::ArrayPtr<word>(scratch, 1024));
  checkTestMessageAllZero(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripAllZeroLazy) {
  TestMessageBuilder builder(1);
  builder.initRoot<TestAllTypes>();

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  checkTestMessageAllZero(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripAllZeroOddSegmentCount) {
  TestMessageBuilder builder(3);
  builder.initRoot<TestAllTypes>().initStructField().initStructField();

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  checkTestMessageAllZero(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripAllZeroOddSegmentCountLazy) {
  TestMessageBuilder builder(3);
  builder.initRoot<TestAllTypes>().initStructField().initStructField();

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  checkTestMessageAllZero(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripAllZeroEvenSegmentCount) {
  TestMessageBuilder builder(2);
  builder.initRoot<TestAllTypes>().initStructField().initStructField();

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  checkTestMessageAllZero(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripAllZeroEvenSegmentCountLazy) {
  TestMessageBuilder builder(2);
  builder.initRoot<TestAllTypes>().initStructField().initStructField();

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  checkTestMessageAllZero(reader.getRoot<TestAllTypes>());
}

// =======================================================================================

TEST(Packed, RoundTripHugeString) {
  kj::String huge = kj::heapString(5023);
  memset(huge.begin(), 'x', 5023);

  TestMessageBuilder builder(1);
  builder.initRoot<TestAllTypes>().setTextField(huge);

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  EXPECT_TRUE(reader.getRoot<TestAllTypes>().getTextField() == huge);
}

TEST(Packed, RoundTripHugeStringScratchSpace) {
  kj::String huge = kj::heapString(5023);
  memset(huge.begin(), 'x', 5023);

  TestMessageBuilder builder(1);
  builder.initRoot<TestAllTypes>().setTextField(huge);

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  word scratch[1024];
  PackedMessageReader reader(pipe, ReaderOptions(), kj::ArrayPtr<word>(scratch, 1024));
  EXPECT_TRUE(reader.getRoot<TestAllTypes>().getTextField() == huge);
}

TEST(Packed, RoundTripHugeStringLazy) {
  kj::String huge = kj::heapString(5023);
  memset(huge.begin(), 'x', 5023);

  TestMessageBuilder builder(1);
  builder.initRoot<TestAllTypes>().setTextField(huge);

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  EXPECT_TRUE(reader.getRoot<TestAllTypes>().getTextField() == huge);
}

TEST(Packed, RoundTripHugeStringOddSegmentCount) {
  kj::String huge = kj::heapString(5023);
  memset(huge.begin(), 'x', 5023);

  TestMessageBuilder builder(3);
  builder.initRoot<TestAllTypes>().setTextField(huge);

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  EXPECT_TRUE(reader.getRoot<TestAllTypes>().getTextField() == huge);
}

TEST(Packed, RoundTripHugeStringOddSegmentCountLazy) {
  kj::String huge = kj::heapString(5023);
  memset(huge.begin(), 'x', 5023);

  TestMessageBuilder builder(3);
  builder.initRoot<TestAllTypes>().setTextField(huge);

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  EXPECT_TRUE(reader.getRoot<TestAllTypes>().getTextField() == huge);
}

TEST(Packed, RoundTripHugeStringEvenSegmentCount) {
  kj::String huge = kj::heapString(5023);
  memset(huge.begin(), 'x', 5023);

  TestMessageBuilder builder(2);
  builder.initRoot<TestAllTypes>().setTextField(huge);

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  EXPECT_TRUE(reader.getRoot<TestAllTypes>().getTextField() == huge);
}

TEST(Packed, RoundTripHugeStringEvenSegmentCountLazy) {
  kj::String huge = kj::heapString(5023);
  memset(huge.begin(), 'x', 5023);

  TestMessageBuilder builder(2);
  builder.initRoot<TestAllTypes>().setTextField(huge);

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  EXPECT_EQ(computeSerializedSizeInWords(builder), computeUnpackedSizeInWords(pipe.getArray()));

  PackedMessageReader reader(pipe);
  EXPECT_TRUE(reader.getRoot<TestAllTypes>().getTextField() == huge);
}

// TODO(test):  Test error cases.

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
