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
#include "serialize.h"
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

TEST(Serialize, FlatArray) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  FlatArrayMessageReader reader(serialized.asPtr());
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, FlatArrayOddSegmentCount) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  FlatArrayMessageReader reader(serialized.asPtr());
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, FlatArrayEventSegmentCount) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  FlatArrayMessageReader reader(serialized.asPtr());
  checkTestMessage(reader.getRoot<TestAllTypes>());
}

class TestInputStream: public InputStream {
public:
  TestInputStream(ArrayPtr<const word> data)
      : pos(reinterpret_cast<const char*>(data.begin())),
        end(reinterpret_cast<const char*>(data.end())) {}
  ~TestInputStream() {}

  bool read(void* buffer, size_t size) override {
    if (size_t(end - pos) < size) {
      ADD_FAILURE() << "Overran end of stream.";
      return false;
    } else {
      memcpy(buffer, pos, size);
      pos += size;
      return true;
    }
  }

private:
  const char* pos;
  const char* end;
};

TEST(Serialize, InputStream) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr());
  InputStreamMessageReader reader(&stream, ReaderOptions(), InputStrategy::EAGER);

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputStreamLazy) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr());
  InputStreamMessageReader reader(&stream, ReaderOptions(), InputStrategy::LAZY);

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputStreamOddSegmentCount) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr());
  InputStreamMessageReader reader(&stream, ReaderOptions(), InputStrategy::EAGER);

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputStreamOddSegmentCountLazy) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr());
  InputStreamMessageReader reader(&stream, ReaderOptions(), InputStrategy::LAZY);

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputStreamEventSegmentCount) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr());
  InputStreamMessageReader reader(&stream, ReaderOptions(), InputStrategy::EAGER);

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputStreamEventSegmentCountLazy) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr());
  InputStreamMessageReader reader(&stream, ReaderOptions(), InputStrategy::LAZY);

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

class TestInputFile: public InputFile {
public:
  TestInputFile(ArrayPtr<const word> data)
      : begin(reinterpret_cast<const char*>(data.begin())),
        size_(data.size() * sizeof(word)) {}
  ~TestInputFile() {}

  bool read(size_t offset, void* buffer, size_t size) override {
    if (size_ < offset + size) {
      ADD_FAILURE() << "Overran end of file.";
      return false;
    } else {
      memcpy(buffer, begin + offset, size);
      return true;
    }
  }

private:
  const char* begin;
  size_t size_;
};

TEST(Serialize, InputFile) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestInputFile file(serialized.asPtr());
  InputFileMessageReader reader(&file, ReaderOptions(), InputStrategy::EAGER);

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputFileLazy) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestInputFile file(serialized.asPtr());
  InputFileMessageReader reader(&file, ReaderOptions(), InputStrategy::LAZY);

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputFileOddSegmentCount) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestInputFile file(serialized.asPtr());
  InputFileMessageReader reader(&file, ReaderOptions(), InputStrategy::EAGER);

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputFileOddSegmentCountLazy) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestInputFile file(serialized.asPtr());
  InputFileMessageReader reader(&file, ReaderOptions(), InputStrategy::LAZY);

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputFileEventSegmentCount) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestInputFile file(serialized.asPtr());
  InputFileMessageReader reader(&file, ReaderOptions(), InputStrategy::EAGER);

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

TEST(Serialize, InputFileEventSegmentCountLazy) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestInputFile file(serialized.asPtr());
  InputFileMessageReader reader(&file, ReaderOptions(), InputStrategy::LAZY);

  checkTestMessage(reader.getRoot<TestAllTypes>());
}

class TestOutputStream: public OutputStream {
public:
  TestOutputStream() {}
  ~TestOutputStream() {}

  void write(const void* buffer, size_t size) override {
    data.append(reinterpret_cast<const char*>(buffer), size);
  }

  const bool dataEquals(ArrayPtr<const word> other) {
    return data ==
        std::string(reinterpret_cast<const char*>(other.begin()), other.size() * sizeof(word));
  }

private:
  std::string data;
};

TEST(Serialize, WriteMessage) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestOutputStream output;
  writeMessage(output, builder);

  EXPECT_TRUE(output.dataEquals(serialized.asPtr()));
}

TEST(Serialize, WriteMessageOddSegmentCount) {
  TestMessageBuilder builder(7);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestOutputStream output;
  writeMessage(output, builder);

  EXPECT_TRUE(output.dataEquals(serialized.asPtr()));
}

TEST(Serialize, WriteMessageEventSegmentCount) {
  TestMessageBuilder builder(10);
  initTestMessage(builder.initRoot<TestAllTypes>());

  Array<word> serialized = messageToFlatArray(builder);

  TestOutputStream output;
  writeMessage(output, builder);

  EXPECT_TRUE(output.dataEquals(serialized.asPtr()));
}

TEST(Serialize, FileDescriptors) {
  char filename[] = "/tmp/capnproto-serialize-test-XXXXXX";
  AutoCloseFd tmpfile(mkstemp(filename));
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
    FileFdMessageReader reader(tmpfile.get(), 0);
    checkTestMessage(reader.getRoot<TestAllTypes>());
  }

  size_t secondStart = lseek(tmpfile, 0, SEEK_CUR);

  {
    StreamFdMessageReader reader(tmpfile.get());
    EXPECT_EQ("second message in file", reader.getRoot<TestAllTypes>().getTextField());
  }

  {
    FileFdMessageReader reader(move(tmpfile), secondStart);
    EXPECT_EQ("second message in file", reader.getRoot<TestAllTypes>().getTextField());
  }
}

// TODO:  Test error cases.

}  // namespace
}  // namespace internal
}  // namespace capnproto
