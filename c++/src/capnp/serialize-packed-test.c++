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

#include "serialize-packed.h"
#include <kj/debug.h>
#include <gtest/gtest.h>
#include <string>
#include <stdlib.h>
#include "test-util.h"

namespace capnp {
namespace _ {  // private
namespace {

class TestPipe: public kj::BufferedInputStream, public kj::OutputStream {
public:
  TestPipe()
      : preferredReadSize(std::numeric_limits<size_t>::max()), readPos(0) {}
  explicit TestPipe(size_t preferredReadSize)
      : preferredReadSize(preferredReadSize), readPos(0) {}
  ~TestPipe() {}

  const std::string& getData() { return data; }
  void resetRead(size_t preferredReadSize = std::numeric_limits<size_t>::max()) {
    readPos = 0;
    this->preferredReadSize = preferredReadSize;
  }

  bool allRead() {
    return readPos == data.size();
  }

  void clear(size_t preferredReadSize = std::numeric_limits<size_t>::max()) {
    resetRead(preferredReadSize);
    data.clear();
  }

  void write(const void* buffer, size_t size) override {
    data.append(reinterpret_cast<const char*>(buffer), size);
  }

  size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    KJ_ASSERT(maxBytes <= data.size() - readPos, "Overran end of stream.");
    size_t amount = std::min(maxBytes, std::max(minBytes, preferredReadSize));
    memcpy(buffer, data.data() + readPos, amount);
    readPos += amount;
    return amount;
  }

  void skip(size_t bytes) override {
    KJ_ASSERT(bytes <= data.size() - readPos, "Overran end of stream.");
    readPos += bytes;
  }

  kj::ArrayPtr<const byte> tryGetReadBuffer() override {
    size_t amount = std::min(data.size() - readPos, preferredReadSize);
    return kj::arrayPtr(reinterpret_cast<const byte*>(data.data() + readPos), amount);
  }

private:
  size_t preferredReadSize;
  std::string data;
  std::string::size_type readPos;
};

struct DisplayByteArray {
  DisplayByteArray(const std::string& str)
      : data(reinterpret_cast<const uint8_t*>(str.data())), size(str.size()) {}
  DisplayByteArray(const std::initializer_list<uint8_t>& list)
      : data(list.begin()), size(list.size()) {}

  const uint8_t* data;
  size_t size;
};

std::ostream& operator<<(std::ostream& os, const DisplayByteArray& bytes) {
  os << "{ ";
  for (size_t i = 0; i < bytes.size; i++) {
    if (i > 0) {
      os << ", ";
    }
    os << (uint)bytes.data[i];
  }
  os << " }";

  return os;
}

void expectPacksTo(std::initializer_list<uint8_t> unpacked,
                   std::initializer_list<uint8_t> packed) {
  TestPipe pipe;

  // -----------------------------------------------------------------
  // write

  {
    kj::BufferedOutputStreamWrapper bufferedOut(pipe);
    PackedOutputStream packedOut(bufferedOut);
    packedOut.write(unpacked.begin(), unpacked.size());
  }

  if (pipe.getData() != std::string(reinterpret_cast<const char*>(packed.begin()), packed.size())) {
    ADD_FAILURE()
        << "Tried to pack: " << DisplayByteArray(unpacked) << "\n"
        << "Expected:      " << DisplayByteArray(packed) << "\n"
        << "Actual:        " << DisplayByteArray(pipe.getData());
    return;
  }

  // -----------------------------------------------------------------
  // read

  std::string roundTrip;
  roundTrip.resize(unpacked.size());

  {
    PackedInputStream packedIn(pipe);
    packedIn.InputStream::read(&*roundTrip.begin(), roundTrip.size());
    EXPECT_TRUE(pipe.allRead());
  }

  if (roundTrip != std::string(reinterpret_cast<const char*>(unpacked.begin()), unpacked.size())) {
    ADD_FAILURE()
        << "Tried to unpack: " << DisplayByteArray(packed) << "\n"
        << "Expected:        " << DisplayByteArray(unpacked) << "\n"
        << "Actual:          " << DisplayByteArray(roundTrip);
    return;
  }

  for (uint blockSize = 1; blockSize < packed.size(); blockSize <<= 1) {
    pipe.resetRead(blockSize);

    {
      PackedInputStream packedIn(pipe);
      packedIn.InputStream::read(&*roundTrip.begin(), roundTrip.size());
      EXPECT_TRUE(pipe.allRead());
    }

    if (roundTrip !=
        std::string(reinterpret_cast<const char*>(unpacked.begin()), unpacked.size())) {
      ADD_FAILURE()
          << "Tried to unpack: " << DisplayByteArray(packed) << "\n"
          << "  Block size: " << blockSize << "\n"
          << "Expected:        " << DisplayByteArray(unpacked) << "\n"
          << "Actual:          " << DisplayByteArray(roundTrip);
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

    if (roundTrip !=
        std::string(reinterpret_cast<const char*>(unpacked.begin()), unpacked.size())) {
      ADD_FAILURE()
          << "Tried to unpack: " << DisplayByteArray(packed) << "\n"
          << "  Index: " << i << "\n"
          << "Expected:        " << DisplayByteArray(unpacked) << "\n"
          << "Actual:          " << DisplayByteArray(roundTrip);
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

  PackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripScratchSpace) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  word scratch[1024];
  PackedMessageReader reader(pipe, ReaderOptions(), kj::ArrayPtr<word>(scratch, 1024));
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripLazy) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  PackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripOddSegmentCount) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  PackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripOddSegmentCountLazy) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  PackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripEvenSegmentCount) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  PackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripEvenSegmentCountLazy) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

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

  word scratch[1024];
  PackedMessageReader reader(pipe, ReaderOptions(), kj::ArrayPtr<word>(scratch, 1024));
  checkTestMessageAllZero(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripAllZeroLazy) {
  TestMessageBuilder builder(1);
  builder.initRoot<TestAllTypes>();

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  PackedMessageReader reader(pipe);
  checkTestMessageAllZero(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripAllZeroOddSegmentCount) {
  TestMessageBuilder builder(3);
  builder.initRoot<TestAllTypes>().initStructField().initStructField();

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  PackedMessageReader reader(pipe);
  checkTestMessageAllZero(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripAllZeroOddSegmentCountLazy) {
  TestMessageBuilder builder(3);
  builder.initRoot<TestAllTypes>().initStructField().initStructField();

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

  PackedMessageReader reader(pipe);
  checkTestMessageAllZero(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripAllZeroEvenSegmentCount) {
  TestMessageBuilder builder(2);
  builder.initRoot<TestAllTypes>().initStructField().initStructField();

  TestPipe pipe;
  writePackedMessage(pipe, builder);

  PackedMessageReader reader(pipe);
  checkTestMessageAllZero(reader.getRoot<TestAllTypes>());
}

TEST(Packed, RoundTripAllZeroEvenSegmentCountLazy) {
  TestMessageBuilder builder(2);
  builder.initRoot<TestAllTypes>().initStructField().initStructField();

  TestPipe pipe(1);
  writePackedMessage(pipe, builder);

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

  PackedMessageReader reader(pipe);
  EXPECT_TRUE(reader.getRoot<TestAllTypes>().getTextField() == huge);
}

// TODO(test):  Test error cases.

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
