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

#include "serialize.h"
#include <kj/debug.h>
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

TEST(Serialize, FlatArray) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  {
    FlatArrayMessageReader reader(serialized.asPtr());
    checkTestMessage(reader.getRoot<TestAllTypes>());
    EXPECT_EQ(serialized.end(), reader.getEnd());
  }

  kj::Array<word> serializedWithSuffix = kj::heapArray<word>(serialized.size() + 5);
  memcpy(serializedWithSuffix.begin(), serialized.begin(), serialized.size() * sizeof(word));

  {
    FlatArrayMessageReader reader(serializedWithSuffix.asPtr());
    checkTestMessage(reader.getRoot<TestAllTypes>());
    EXPECT_EQ(serializedWithSuffix.end() - 5, reader.getEnd());
  }
}

TEST(Serialize, FlatArrayOddSegmentCount) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  {
    FlatArrayMessageReader reader(serialized.asPtr());
    checkTestMessage(reader.getRoot<TestAllTypes>());
    EXPECT_EQ(serialized.end(), reader.getEnd());
  }

  kj::Array<word> serializedWithSuffix = kj::heapArray<word>(serialized.size() + 5);
  memcpy(serializedWithSuffix.begin(), serialized.begin(), serialized.size() * sizeof(word));

  {
    FlatArrayMessageReader reader(serializedWithSuffix.asPtr());
    checkTestMessage(reader.getRoot<TestAllTypes>());
    EXPECT_EQ(serializedWithSuffix.end() - 5, reader.getEnd());
  }
}

TEST(Serialize, FlatArrayEvenSegmentCount) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  {
    FlatArrayMessageReader reader(serialized.asPtr());
    checkTestMessage(reader.getRoot<TestAllTypes>());
    EXPECT_EQ(serialized.end(), reader.getEnd());
  }

  kj::Array<word> serializedWithSuffix = kj::heapArray<word>(serialized.size() + 5);
  memcpy(serializedWithSuffix.begin(), serialized.begin(), serialized.size() * sizeof(word));

  {
    FlatArrayMessageReader reader(serializedWithSuffix.asPtr());
    checkTestMessage(reader.getRoot<TestAllTypes>());
    EXPECT_EQ(serializedWithSuffix.end() - 5, reader.getEnd());
  }
}

class TestInputStream: public kj::InputStream {
public:
  TestInputStream(kj::ArrayPtr<const word> data, bool lazy)
      : pos(reinterpret_cast<const char*>(data.begin())),
        end(reinterpret_cast<const char*>(data.end())),
        lazy(lazy) {}
  ~TestInputStream() {}

  size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    KJ_ASSERT(maxBytes <= size_t(end - pos), "Overran end of stream.");
    size_t amount = lazy ? minBytes : maxBytes;
    memcpy(buffer, pos, amount);
    pos += amount;
    return amount;
  }

private:
  const char* pos;
  const char* end;
  bool lazy;
};

TEST(Serialize, InputStream) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr(), false);
  InputStreamMessageReader reader(stream, ReaderOptions());

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputStreamScratchSpace) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  word scratch[4096];
  TestInputStream stream(serialized.asPtr(), false);
  InputStreamMessageReader reader(stream, ReaderOptions(), kj::ArrayPtr<word>(scratch, 4096));

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputStreamLazy) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr(), true);
  InputStreamMessageReader reader(stream, ReaderOptions());

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputStreamOddSegmentCount) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr(), false);
  InputStreamMessageReader reader(stream, ReaderOptions());

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputStreamOddSegmentCountLazy) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr(), true);
  InputStreamMessageReader reader(stream, ReaderOptions());

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputStreamEvenSegmentCount) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr(), false);
  InputStreamMessageReader reader(stream, ReaderOptions());

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputStreamEvenSegmentCountLazy) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr(), true);
  InputStreamMessageReader reader(stream, ReaderOptions());

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

class TestOutputStream: public kj::OutputStream {
public:
  TestOutputStream() {}
  ~TestOutputStream() {}

  void write(const void* buffer, size_t size) override {
    data.append(reinterpret_cast<const char*>(buffer), size);
  }

  const bool dataEquals(kj::ArrayPtr<const word> other) {
    return data ==
        std::string(reinterpret_cast<const char*>(other.begin()), other.size() * sizeof(word));
  }

private:
  std::string data;
};

TEST(Serialize, WriteMessage) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  TestOutputStream output;
  writeMessage(output, builder);

  EXPECT_TRUE(output.dataEquals(serialized.asPtr()));
}

TEST(Serialize, WriteMessageOddSegmentCount) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  TestOutputStream output;
  writeMessage(output, builder);

  EXPECT_TRUE(output.dataEquals(serialized.asPtr()));
}

TEST(Serialize, WriteMessageEvenSegmentCount) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  TestOutputStream output;
  writeMessage(output, builder);

  EXPECT_TRUE(output.dataEquals(serialized.asPtr()));
}

TEST(Serialize, FileDescriptors) {
  char filename[] = "/tmp/capnproto-serialize-test-XXXXXX";
  kj::AutoCloseFd tmpfile(mkstemp(filename));
  ASSERT_GE(tmpfile.get(), 0);

  // Unlink the file so that it will be deleted on close.
  EXPECT_EQ(0, unlink(filename));

  {
    TestMessageBuilder builder(7);
    initTestMessage(builder.initRoot<TestAllTypes>());
    writeMessageToFd(tmpfile.get(), builder);
  }

  {
    TestMessageBuilder builder(1);
    builder.initRoot<TestAllTypes>().setTextField("second message in file");
    writeMessageToFd(tmpfile.get(), builder);
  }

  lseek(tmpfile, 0, SEEK_SET);

  {
    StreamFdMessageReader reader(tmpfile.get());
    checkTestMessage(reader.getRoot<TestAllTypes>());
  }

  {
    StreamFdMessageReader reader(tmpfile.get());
    EXPECT_EQ("second message in file", reader.getRoot<TestAllTypes>().getTextField());
  }
}

TEST(Serialize, RejectTooManySegments) {
  kj::Array<word> data = kj::heapArray<word>(8192);
  WireValue<uint32_t>* table = reinterpret_cast<WireValue<uint32_t>*>(data.begin());
  table[0].set(1024);
  for (uint i = 0; i < 1024; i++) {
    table[i+1].set(1);
  }
  TestInputStream input(data.asPtr(), false);

  kj::Maybe<kj::Exception> e = kj::runCatchingExceptions([&]() {
    InputStreamMessageReader reader(input);
#if !KJ_NO_EXCEPTIONS
    ADD_FAILURE() << "Should have thrown an exception.";
#endif
  });

  EXPECT_TRUE(e != nullptr) << "Should have thrown an exception.";
}

TEST(Serialize, RejectHugeMessage) {
  // A message whose root struct contains two words of data!
  AlignedData<4> data = {{0,0,0,0,3,0,0,0, 0,0,0,0,2,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0}};

  TestInputStream input(kj::arrayPtr(data.words, 4), false);

  // We'll set the traversal limit to 2 words so our 3-word message is too big.
  ReaderOptions options;
  options.traversalLimitInWords = 2;

  kj::Maybe<kj::Exception> e = kj::runCatchingExceptions([&]() {
    InputStreamMessageReader reader(input, options);
#if !KJ_NO_EXCEPTIONS
    ADD_FAILURE() << "Should have thrown an exception.";
#endif
  });

  EXPECT_TRUE(e != nullptr) << "Should have thrown an exception.";
}

// TODO(test):  Test error cases.

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
