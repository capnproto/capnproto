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

#include "test.capnp.h"
#include "serialize-snappy.h"
#include <gtest/gtest.h>
#include <string>
#include <stdlib.h>
#include "test-util.h"

namespace capnproto {
namespace internal {
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

  ArrayPtr<word> allocateSegment(uint minimumSize) override {
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

class TestPipe: public InputStream, public OutputStream {
public:
  TestPipe(bool lazy)
      : lazy(lazy), readPos(0) {}
  ~TestPipe() {}

  void write(const void* buffer, size_t size) override {
    data.append(reinterpret_cast<const char*>(buffer), size);
  }

  size_t read(void* buffer, size_t minBytes, size_t maxBytes) override {
    CAPNPROTO_ASSERT(maxBytes <= data.size() - readPos, "Overran end of stream.");
    size_t amount = lazy ? minBytes : maxBytes;
    memcpy(buffer, data.data() + readPos, amount);
    readPos += amount;
    return amount;
  }

private:
  bool lazy;
  std::string data;
  std::string::size_type readPos;
};

TEST(Snappy, RoundTrip) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(false);
  writeSnappyMessage(pipe, builder);

  SnappyMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Snappy, RoundTripScratchSpace) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(false);
  writeSnappyMessage(pipe, builder);

  word scratch[1024];
  SnappyMessageReader reader(pipe, ReaderOptions(), ArrayPtr<word>(scratch, 1024));
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Snappy, RoundTripLazy) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(true);
  writeSnappyMessage(pipe, builder);

  SnappyMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Snappy, RoundTripOddSegmentCount) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(false);
  writeSnappyMessage(pipe, builder);

  SnappyMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Snappy, RoundTripOddSegmentCountLazy) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(true);
  writeSnappyMessage(pipe, builder);

  SnappyMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Snappy, RoundTripEvenSegmentCount) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(false);
  writeSnappyMessage(pipe, builder);

  SnappyMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Snappy, RoundTripEvenSegmentCountLazy) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestPipe pipe(true);
  writeSnappyMessage(pipe, builder);

  SnappyMessageReader reader(pipe);
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Snappy, RoundTripTwoMessages) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  TestMessageBuilder builder2(1);
  builder2.initRoot<TestAllTypes>().setTextField("Second message.");

  TestPipe pipe(false);
  writeSnappyMessage(pipe, builder);
  writeSnappyMessage(pipe, builder2);

  {
    SnappyMessageReader reader(pipe);
    checkTestMessage(reader.getRoot<TestAllTypes>());
  }

  {
    SnappyMessageReader reader(pipe);
    EXPECT_EQ("Second message.", reader.getRoot<TestAllTypes>().getTextField());
  }
}

// TODO:  Test error cases.

}  // namespace
}  // namespace internal
}  // namespace capnproto
