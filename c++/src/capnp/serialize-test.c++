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
#include <kj/compat/gtest.h>
#include <kj/miniposix.h>
#include <string>
#include <stdlib.h>
#include <fcntl.h>
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

kj::Array<word> copyWords(kj::ArrayPtr<const word> input) {
  auto result = kj::heapArray<word>(input.size());
  memcpy(result.asBytes().begin(), input.asBytes().begin(), input.asBytes().size());
  return result;
}

TEST(Serialize, FlatArray) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  {
    FlatArrayMessageReader reader(serialized.asPtr());
    checkTestMessage(reader.getRoot<TestAllTypes>());
    EXPECT_EQ(serialized.end(), reader.getEnd());
  }

  {
    MallocMessageBuilder builder2;
    auto remaining = initMessageBuilderFromFlatArrayCopy(serialized, builder2);
    checkTestMessage(builder2.getRoot<TestAllTypes>());
    EXPECT_EQ(serialized.end(), remaining.begin());
    EXPECT_EQ(serialized.end(), remaining.end());
  }

  kj::Array<word> serializedWithSuffix = kj::heapArray<word>(serialized.size() + 5);
  memcpy(serializedWithSuffix.begin(), serialized.begin(), serialized.size() * sizeof(word));

  {
    FlatArrayMessageReader reader(serializedWithSuffix.asPtr());
    checkTestMessage(reader.getRoot<TestAllTypes>());
    EXPECT_EQ(serializedWithSuffix.end() - 5, reader.getEnd());
  }

  {
    MallocMessageBuilder builder2;
    auto remaining = initMessageBuilderFromFlatArrayCopy(serializedWithSuffix, builder2);
    checkTestMessage(builder2.getRoot<TestAllTypes>());
    EXPECT_EQ(serializedWithSuffix.end() - 5, remaining.begin());
    EXPECT_EQ(serializedWithSuffix.end(), remaining.end());
  }

  {
    // Test expectedSizeInWordsFromPrefix(). We pass in a copy of the slice so that valgrind can
    // detect out-of-bounds access.
    EXPECT_EQ(1, expectedSizeInWordsFromPrefix(copyWords(serialized.slice(0, 0))));
    for (uint i = 1; i <= serialized.size(); i++) {
      EXPECT_EQ(serialized.size(),
          expectedSizeInWordsFromPrefix(copyWords(serialized.slice(0, i))));
    }
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

  {
    // Test expectedSizeInWordsFromPrefix(). We pass in a copy of the slice so that valgrind can
    // detect out-of-bounds access.

    // Segment table is 4 words, so with fewer words we'll have incomplete information.
    for (uint i = 0; i < 4; i++) {
      size_t expectedSize = expectedSizeInWordsFromPrefix(copyWords(serialized.slice(0, i)));
      EXPECT_LT(expectedSize, serialized.size());
      EXPECT_GT(expectedSize, i);
    }
    // After that, we get the exact length.
    for (uint i = 4; i <= serialized.size(); i++) {
      EXPECT_EQ(serialized.size(),
          expectedSizeInWordsFromPrefix(copyWords(serialized.slice(0, i))));
    }
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

  {
    // Test expectedSizeInWordsFromPrefix(). We pass in a copy of the slice so that valgrind can
    // detect out-of-bounds access.

    // Segment table is 6 words, so with fewer words we'll have incomplete information.
    for (uint i = 0; i < 6; i++) {
      size_t expectedSize = expectedSizeInWordsFromPrefix(copyWords(serialized.slice(0, i)));
      EXPECT_LT(expectedSize, serialized.size());
      EXPECT_GT(expectedSize, i);
    }
    // After that, we get the exact length.
    for (uint i = 6; i <= serialized.size(); i++) {
      EXPECT_EQ(serialized.size(),
          expectedSizeInWordsFromPrefix(copyWords(serialized.slice(0, i))));
    }
  }
}

class TestInputStream: public kj::InputStream {
public:
  TestInputStream(kj::ArrayPtr<const word> data, bool lazy)
      : pos(data.asChars().begin()),
        end(data.asChars().end()),
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

TEST(Serialize, InputStreamToBuilder) {
  TestMessageBuilder builder(1);
  initTestMessage(builder.initRoot<TestAllTypes>());

  kj::Array<word> serialized = messageToFlatArray(builder);

  TestInputStream stream(serialized.asPtr(), false);

  MallocMessageBuilder builder2;
  readMessageCopy(stream, builder2);

  checkTestMessage(builder2.getRoot<TestAllTypes>());
}

class TestOutputStream: public kj::OutputStream {
public:
  TestOutputStream() {}
  ~TestOutputStream() {}

  void write(const void* buffer, size_t size) override {
    data.append(reinterpret_cast<const char*>(buffer), size);
  }

  bool dataEquals(kj::ArrayPtr<const word> other) {
    return data ==
        std::string(other.asChars().begin(), other.asChars().size());
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

#if _WIN32
int mkstemp(char *tpl) {
  char* end = tpl + strlen(tpl);
  while (end > tpl && *(end-1) == 'X') --end;

  for (;;) {
    KJ_ASSERT(_mktemp(tpl) == tpl);

    int fd = open(tpl, O_RDWR | O_CREAT | O_EXCL | O_TEMPORARY | O_BINARY, 0700);
    if (fd >= 0) {
      return fd;
    }

    int error = errno;
    if (error != EEXIST && error != EINTR) {
      KJ_FAIL_SYSCALL("open(mktemp())", error, tpl);
    }

    memset(end, 'X', strlen(end));
  }
}
#endif

TEST(Serialize, FileDescriptors) {
#if _WIN32 || __ANDROID__
  // TODO(cleanup): Find the Windows temp directory? Seems overly difficult.
  char filename[] = "capnproto-serialize-test-XXXXXX";
#else
  char filename[] = "/tmp/capnproto-serialize-test-XXXXXX";
#endif
  kj::AutoCloseFd tmpfile(mkstemp(filename));
  ASSERT_GE(tmpfile.get(), 0);

#if !_WIN32
  // Unlink the file so that it will be deleted on close.
  // (For win32, we already handled this is mkstemp().)
  EXPECT_EQ(0, unlink(filename));
#endif

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

  KJ_EXPECT(e != nullptr, "Should have thrown an exception.");
}

#if !__MINGW32__  // Inexplicably crashes when exception is thrown from constructor.
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

  KJ_EXPECT(e != nullptr, "Should have thrown an exception.");
}
#endif  // !__MINGW32__

// TODO(test):  Test error cases.

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
