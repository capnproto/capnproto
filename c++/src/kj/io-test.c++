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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "io.h"
#include "debug.h"
#include "miniposix.h"
#include "test-util.h"
#include <kj/compat/gtest.h>

namespace kj {
namespace {

#if INTPTR_MAX == INT64_MAX
// These are helper constants for the large write to disk unit test. The basic idea is that we write
// many GBs to disk & then validate that the contents were round-tripped correctly by reading it
// back. This mechanism does a neat trick to avoid using a lot of memory. Since most (all?) modern
// OSes oversubscribe & lazy allocate pages by COW mapping a reference page filled with 0s,
// allocating 3 GB won't actually "do" anything. We then write out a prefix to each 1GB boundary
// which means we'll dirty 3 pages with the remaining mapped to the reference page (this also
// assumes that the allocation request will be handled by the OS rather than the user-space memory
// allocator, but this seems like a safe assumption unless you're repeatedly running tests in the
// same process that allocate such large amounts of RAM and the allocator happened to keep a lot of
// pages around without returning it to the OS.

static constexpr size_t ONE_GB = 1 * 1024 * 1024 * 1024;
// Helper constant to represent the number of bytes in 1 GB.

static constexpr size_t BUFFER_SIZE = 3 * ONE_GB;
// The large write test uses 3GB as the target size of the I/O operation to perform as it's outside
// the 2GB range that previously caused problems. Hopefully picking round numbers doesn't cause us
// to test "happy" paths but a representative scenario of an arbitrarily large size.

static constexpr kj::StringPtr ONE_GB_ALIGNED_PREFIXES_STORAGE[] = {
// TODO(cleanup): KJ doesn't have an equivalent to std::array but if it were to that would be
// preferred.
  "kj says hello 1"_kj,
  "kj says hello 2"_kj,
  "kj says hello 3"_kj,
};
static constexpr kj::ArrayPtr<const kj::StringPtr> ONE_GB_ALIGNED_PREFIXES{ONE_GB_ALIGNED_PREFIXES_STORAGE};
// These are prefixes for each 1GB that we right (i.e. the first GB will start with
// "kj says hello 1" followed by 0s, the second GB will start with "kj says hello 2". This is just
// used to confirm we've successfully written out all the pages in the correct order.

static constexpr size_t PREFIX_SIZE_WITHOUT_NULL_TERMINATOR = ONE_GB_ALIGNED_PREFIXES[0].size();

static const kj::StringPtr& nthPrefix(int gb) {
  KJ_ASSERT(gb < ONE_GB_ALIGNED_PREFIXES.size());
  return ONE_GB_ALIGNED_PREFIXES[gb];
};

static void setPrefix(char* ptr, int gb) {
  KJ_ASSERT(gb < ONE_GB_ALIGNED_PREFIXES.size());
  strcpy(ptr + gb * ONE_GB, (ONE_GB_ALIGNED_PREFIXES.begin() + gb)->cStr());
};
#endif

TEST(Io, WriteLarge) {
  // Check that writing a single large write > 2GB succeeds. This is primarily to workaround an
  // Apple POSIX compliance issue (filed to Apple as FB8934446). This test only makes sense on
  // 64-bit platforms as smaller platforms can't really even try to issue such large writes
  // (since SSIZE_MAX on those platforms will be 2GB anyway).
#if INTPTR_MAX == INT64_MAX
  // Have to thunk through a temporary file since pipes would block on write trying to push this
  // much data (i.e. would never get to read anything);

  AutoCloseFd file = test::mkstemp_auto_erased();

  {
    FdOutputStream out(file.get());

    auto toWrite = heapArray<char>(BUFFER_SIZE);
    setPrefix(toWrite.begin(), 0);
    setPrefix(toWrite.begin(), 1);
    setPrefix(toWrite.begin(), 2);

    out.write(toWrite.begin(), toWrite.size());
  }

  {
    FdInputStream in(file.get());

    for (int i = 0; i < 3; i++) {
        auto toRead = heapString(PREFIX_SIZE_WITHOUT_NULL_TERMINATOR);
        lseek(file.get(), i * ONE_GB, SEEK_SET);
        in.read(toRead.begin(), toRead.size());
        EXPECT_STREQ(nthPrefix(i).cStr(), toRead.cStr());
    }
  }
#endif
}

TEST(Io, WriteVec) {
  // Check that writing an array of arrays works even when some of the arrays are empty.  (This
  // used to not work in some cases.)

  int fds[2];
  KJ_SYSCALL(miniposix::pipe(fds));

  FdInputStream in((AutoCloseFd(fds[0])));
  FdOutputStream out((AutoCloseFd(fds[1])));

  ArrayPtr<const byte> pieces[5] = {
    arrayPtr(implicitCast<const byte*>(nullptr), 0),
    arrayPtr(reinterpret_cast<const byte*>("foo"), 3),
    arrayPtr(implicitCast<const byte*>(nullptr), 0),
    arrayPtr(reinterpret_cast<const byte*>("bar"), 3),
    arrayPtr(implicitCast<const byte*>(nullptr), 0)
  };

  out.write(pieces);

  char buf[7];
  in.read(buf, 6);
  buf[6] = '\0';

  EXPECT_STREQ("foobar", buf);
}

TEST(Io, WriteVecLarge) {
  // Check that writing an array of arrays works when one a single writev entry is > 2GB. This is
  // primarily to workaround an Apple POSIX compliance issue (filed to Apple as FB8934446). This
  // test only makes sense on 64-bit platforms as smaller platforms can't really even try to issue
  // such large writes (since SSIZE_MAX on those platforms will be 2GB anyway).

  // NOTE: This unit test is optimistic that the splitting of the iovec is correct because the
  // scatter/gather puts each GB into a separate iovec. For full bullet-proofness the test should
  // arrange the writev call to ensure that boundary conditions are covered in that the partition
  // terminates at the exact end of an iovec section (what this test does currently), the first byte
  // of an iovec section, and somewhere in the middle (& potentially test that empty iovecs don't
  // pose a problem).

#if INTPTR_MAX == INT64_MAX
  AutoCloseFd file = test::mkstemp_auto_erased();

  {
    auto toWrite = heapArray<char>(BUFFER_SIZE);

    setPrefix(toWrite.begin(), 0);
    setPrefix(toWrite.begin(), 1);
    setPrefix(toWrite.begin(), 2);

    ArrayPtr<const byte> pieces[] = {
      arrayPtr(const_cast<const byte*>(toWrite.asBytes().begin()), ONE_GB),
      arrayPtr(const_cast<const byte*>(toWrite.asBytes().begin()), 3 * ONE_GB),
      arrayPtr(const_cast<const byte*>(toWrite.asBytes().begin() + 2 * ONE_GB), ONE_GB),
    };

    FdOutputStream out(file.get());
    out.write(pieces);
  }

  {
    // The file has 0th page prefix, 0th page prefix, 1st page prefix, 2nd page prefix, 2nd page prefix. 
    auto expectedPrefixes = {0, 0, 1, 2, 2};

    FdInputStream in(file.get());

    for (int i = 0; i < expectedPrefixes.size(); i++) {
        auto toRead = heapString(PREFIX_SIZE_WITHOUT_NULL_TERMINATOR);
        lseek(file.get(), i * ONE_GB, SEEK_SET);
        in.read(toRead.begin(), toRead.size());
        EXPECT_STREQ(nthPrefix(*(expectedPrefixes.begin() + i)).cStr(), toRead.cStr());
    }
  }
#endif
}

KJ_TEST("stringify AutoCloseFd") {
  int fds[2];
  KJ_SYSCALL(miniposix::pipe(fds));
  AutoCloseFd in(fds[0]), out(fds[1]);

  KJ_EXPECT(kj::str(in) == kj::str(fds[0]), in, fds[0]);
}

KJ_TEST("VectorOutputStream") {
  VectorOutputStream output(16);
  auto buf = output.getWriteBuffer();
  KJ_ASSERT(buf.size() == 16);

  for (auto i: kj::indices(buf)) {
    buf[i] = 'a' + i;
  }

  output.write(buf.begin(), 4);
  KJ_ASSERT(output.getArray().begin() == buf.begin());
  KJ_ASSERT(output.getArray().size() == 4);

  auto buf2 = output.getWriteBuffer();
  KJ_ASSERT(buf2.end() == buf.end());
  KJ_ASSERT(buf2.size() == 12);

  output.write(buf2.begin(), buf2.size());
  KJ_ASSERT(output.getArray().begin() == buf.begin());
  KJ_ASSERT(output.getArray().size() == 16);

  auto buf3 = output.getWriteBuffer();
  KJ_ASSERT(buf3.size() == 16);
  KJ_ASSERT(output.getArray().begin() != buf.begin());
  KJ_ASSERT(output.getArray().end() == buf3.begin());
  KJ_ASSERT(kj::str(output.getArray().asChars()) == "abcdefghijklmnop");

  byte junk[24];
  for (auto i: kj::indices(junk)) {
    junk[i] = 'A' + i;
  }

  output.write(junk, 4);
  KJ_ASSERT(output.getArray().begin() != buf.begin());
  KJ_ASSERT(output.getArray().end() == buf3.begin() + 4);
  KJ_ASSERT(kj::str(output.getArray().asChars()) == "abcdefghijklmnopABCD");

  output.write(junk + 4, 20);
  // (We can't assert output.getArray().begin() != buf.begin() because the memory allocator could
  // legitimately have allocated a new array in the same space.)
  KJ_ASSERT(output.getArray().end() != buf3.begin() + 24);
  KJ_ASSERT(kj::str(output.getArray().asChars()) == "abcdefghijklmnopABCDEFGHIJKLMNOPQRSTUVWX");

  KJ_ASSERT(output.getWriteBuffer().size() == 24);
  KJ_ASSERT(output.getWriteBuffer().begin() == output.getArray().begin() + 40);
}

class MockInputStream: public InputStream {
public:
  MockInputStream(kj::ArrayPtr<const byte> bytes, size_t blockSize)
      : bytes(bytes), blockSize(blockSize) {}

  size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    // Clamp max read to blockSize.
    size_t n = kj::min(blockSize, maxBytes);

    // Unless that's less than minBytes -- in which case, use minBytes.
    n = kj::max(n, minBytes);

    // But also don't read more data than we have.
    n = kj::min(n, bytes.size());

    memcpy(buffer, bytes.begin(), n);
    bytes = bytes.slice(n, bytes.size());
    return n;
  }

private:
  kj::ArrayPtr<const byte> bytes;
  size_t blockSize;
};

KJ_TEST("InputStream::readAllText() / readAllBytes()") {
  auto bigText = strArray(kj::repeat("foo bar baz"_kj, 12345), ",");
  size_t inputSizes[] = { 0, 1, 256, 4096, 8191, 8192, 8193, 10000, bigText.size() };
  size_t blockSizes[] = { 1, 4, 256, 4096, 8192, bigText.size() };
  uint64_t limits[] = {
    0, 1, 256,
    bigText.size() / 2,
    bigText.size() - 1,
    bigText.size(),
    bigText.size() + 1,
    kj::maxValue
  };

  for (size_t inputSize: inputSizes) {
    for (size_t blockSize: blockSizes) {
      for (uint64_t limit: limits) {
        KJ_CONTEXT(inputSize, blockSize, limit);
        auto textSlice = bigText.asBytes().slice(0, inputSize);
        auto readAllText = [&]() {
          MockInputStream input(textSlice, blockSize);
          return input.readAllText(limit);
        };
        auto readAllBytes = [&]() {
          MockInputStream input(textSlice, blockSize);
          return input.readAllBytes(limit);
        };
        if (limit > inputSize) {
          KJ_EXPECT(readAllText().asBytes() == textSlice);
          KJ_EXPECT(readAllBytes() == textSlice);
        } else {
          KJ_EXPECT_THROW_MESSAGE("Reached limit before EOF.", readAllText());
          KJ_EXPECT_THROW_MESSAGE("Reached limit before EOF.", readAllBytes());
        }
      }
    }
  }
}

KJ_TEST("ArrayOutputStream::write() does not assume adjacent write buffer is its own") {
  // Previously, if ArrayOutputStream::write(src, size) saw that `src` equaled its fill position, it
  // would assume that the write was already in its buffer. This assumption was buggy if the write
  // buffer was directly adjacent in memory to the ArrayOutputStream's buffer, and the
  // ArrayOutputStream was full (i.e., its fill position was one-past-the-end).
  //
  // VectorOutputStream also suffered a similar bug, but it is much harder to test, since it
  // performs its own allocation.

  kj::byte buffer[10] = { 0 };

  ArrayOutputStream output(arrayPtr(buffer, buffer + 5));

  // Succeeds and fills the ArrayOutputStream.
  output.write(buffer + 5, 5);

  // Previously this threw an inscrutable "size <= array.end() - fillPos" requirement failure.
  KJ_EXPECT_THROW_MESSAGE(
      "backing array was not large enough for the data written",
      output.write(buffer + 5, 5));
}

}  // namespace
}  // namespace kj
