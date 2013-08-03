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

#include "serialize-snappy.h"
#include <kj/debug.h>
#include "test.capnp.h"
#include <gtest/gtest.h>
#include <string>
#include <stdlib.h>
#include "test-util.h"

namespace capnp {
namespace _ {  // private
namespace {

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

class TestPipe: public kj::BufferedInputStream, public kj::OutputStream {
public:
  TestPipe()
      : preferredReadSize(std::numeric_limits<size_t>::max()), readPos(0) {}
  explicit TestPipe(size_t preferredReadSize)
      : preferredReadSize(preferredReadSize), readPos(0) {}
  ~TestPipe() {}

  const std::string& getData() { return data; }
  std::string getUnreadData() { return data.substr(readPos); }
  std::string::size_type getReadPos() { return readPos; }

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

//struct DisplayByteArray {
//  DisplayByteArray(const std::string& str)
//      : data(reinterpret_cast<const uint8_t*>(str.data())), size(str.size()) {}
//  DisplayByteArray(const std::initializer_list<uint8_t>& list)
//      : data(list.begin()), size(list.size()) {}
//
//  const uint8_t* data;
//  size_t size;
//};
//
//std::ostream& operator<<(std::ostream& os, const DisplayByteArray& bytes) {
//  os << "{ ";
//  for (size_t i = 0; i < bytes.size; i++) {
//    if (i > 0) {
//      os << ", ";
//    }
//    os << (uint)bytes.data[i];
//  }
//  os << " }";
//
//  return os;
//}

TEST(Snappy, RoundTrip) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe;
  writeSnappyPackedMessage(pipe, builder);

  SnappyPackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
  EXPECT_TRUE(pipe.allRead());
}

TEST(Snappy, RoundTripScratchSpace) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe;
  writeSnappyPackedMessage(pipe, builder);

  word scratch[1024];
  SnappyPackedMessageReader reader(pipe, ReaderOptions(), kj::ArrayPtr<word>(scratch, 1024));
  checkTestMessage(reader.getRoot<TestAllTypes>());
  EXPECT_TRUE(pipe.allRead());
}

TEST(Snappy, RoundTripLazy) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(1);
  writeSnappyPackedMessage(pipe, builder);

  SnappyPackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
  EXPECT_TRUE(pipe.allRead());
}

TEST(Snappy, RoundTripOddSegmentCount) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe;
  writeSnappyPackedMessage(pipe, builder);

  SnappyPackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
  EXPECT_TRUE(pipe.allRead());
}

TEST(Snappy, RoundTripOddSegmentCountLazy) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(1);
  writeSnappyPackedMessage(pipe, builder);

  SnappyPackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
  EXPECT_TRUE(pipe.allRead());
}

TEST(Snappy, RoundTripEvenSegmentCount) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe;
  writeSnappyPackedMessage(pipe, builder);

  SnappyPackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
  EXPECT_TRUE(pipe.allRead());
}

TEST(Snappy, RoundTripEvenSegmentCountLazy) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(1);
  writeSnappyPackedMessage(pipe, builder);

  SnappyPackedMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
  EXPECT_TRUE(pipe.allRead());
}

TEST(Snappy, RoundTripTwoMessages) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestMessageBuilder builder2(1);
  builder2.initRoot<TestAllTypes>().setTextField("Second message.");

  TestPipe pipe(1);
  writeSnappyPackedMessage(pipe, builder);
  size_t firstSize = pipe.getData().size();
  writeSnappyPackedMessage(pipe, builder2);

  {
    SnappyPackedMessageReader reader(pipe);
    checkTestMessage(reader.getRoot<TestAllTypes>());
  }

  EXPECT_EQ(firstSize, pipe.getReadPos());

  {
    SnappyPackedMessageReader reader(pipe);
    EXPECT_EQ("Second message.", reader.getRoot<TestAllTypes>().getTextField());
  }
  EXPECT_TRUE(pipe.allRead());
}

// TODO(test):  Test error cases.

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
