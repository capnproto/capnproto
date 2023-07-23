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
#include <kj/compat/gtest.h>

namespace kj {
namespace {

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
  KJ_ASSERT(output.getArray().begin() != buf.begin());
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
