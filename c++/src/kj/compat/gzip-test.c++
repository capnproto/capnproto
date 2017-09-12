// Copyright (c) 2017 Cloudflare, Inc. and contributors
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

#if KJ_HAS_ZLIB

#include "gzip.h"
#include <kj/test.h>
#include <kj/debug.h>
#include <stdlib.h>

namespace kj {
namespace {

static const byte FOOBAR_GZIP[] = {
  0x1F, 0x8B, 0x08, 0x00, 0xF9, 0x05, 0xB7, 0x59,
  0x00, 0x03, 0x4B, 0xCB, 0xCF, 0x4F, 0x4A, 0x2C,
  0x02, 0x00, 0x95, 0x1F, 0xF6, 0x9E, 0x06, 0x00,
  0x00, 0x00,
};

class MockInputStream: public AsyncInputStream {
public:
  MockInputStream(kj::ArrayPtr<const byte> bytes, size_t blockSize)
      : bytes(bytes), blockSize(blockSize) {}

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
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

KJ_TEST("gzip decompression") {
  auto io = setupAsyncIo();

  // Normal read.
  {
    MockInputStream rawInput(FOOBAR_GZIP, kj::maxValue);
    GzipAsyncInputStream gzip(rawInput);
    KJ_EXPECT(gzip.readAllText().wait(io.waitScope) == "foobar");
  }

  // Force read one byte at a time.
  {
    MockInputStream rawInput(FOOBAR_GZIP, 1);
    GzipAsyncInputStream gzip(rawInput);
    KJ_EXPECT(gzip.readAllText().wait(io.waitScope) == "foobar");
  }

  // Read truncated input.
  {
    MockInputStream rawInput(kj::arrayPtr(FOOBAR_GZIP, sizeof(FOOBAR_GZIP) / 2), kj::maxValue);
    GzipAsyncInputStream gzip(rawInput);

    char text[16];
    size_t n = gzip.tryRead(text, 1, sizeof(text)).wait(io.waitScope);
    text[n] = '\0';
    KJ_EXPECT(StringPtr(text, n) == "fo");

    KJ_EXPECT_THROW_MESSAGE("gzip compressed stream ended prematurely",
        gzip.tryRead(text, 1, sizeof(text)).wait(io.waitScope));
  }

  // Read concatenated input.
  {
    Vector<byte> bytes;
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_GZIP));
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_GZIP));
    MockInputStream rawInput(bytes, kj::maxValue);
    GzipAsyncInputStream gzip(rawInput);

    KJ_EXPECT(gzip.readAllText().wait(io.waitScope) == "foobarfoobar");
  }
}

class MockOutputStream: public AsyncOutputStream {
public:
  kj::Vector<byte> bytes;

  kj::String decompress(WaitScope& ws) {
    MockInputStream rawInput(bytes, kj::maxValue);
    GzipAsyncInputStream gzip(rawInput);
    return gzip.readAllText().wait(ws);
  }

  Promise<void> write(const void* buffer, size_t size) override {
    bytes.addAll(arrayPtr(reinterpret_cast<const byte*>(buffer), size));
    return kj::READY_NOW;
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    for (auto& piece: pieces) {
      bytes.addAll(piece);
    }
    return kj::READY_NOW;
  }
};

KJ_TEST("gzip compression") {
  auto io = setupAsyncIo();

  // Normal write.
  {
    MockOutputStream rawOutput;
    GzipAsyncOutputStream gzip(rawOutput);
    gzip.write("foobar", 6).wait(io.waitScope);
    gzip.end().wait(io.waitScope);

    KJ_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }

  // Multi-part write.
  {
    MockOutputStream rawOutput;
    GzipAsyncOutputStream gzip(rawOutput);
    gzip.write("foo", 3).wait(io.waitScope);
    gzip.write("bar", 3).wait(io.waitScope);
    gzip.end().wait(io.waitScope);

    KJ_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }

  // Array-of-arrays write.
  {
    MockOutputStream rawOutput;
    GzipAsyncOutputStream gzip(rawOutput);

    ArrayPtr<const byte> pieces[] = {
      kj::StringPtr("foo").asBytes(),
      kj::StringPtr("bar").asBytes(),
    };
    gzip.write(pieces).wait(io.waitScope);
    gzip.end().wait(io.waitScope);

    KJ_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }
}

KJ_TEST("gzip huge round trip") {
  auto io = setupAsyncIo();

  auto bytes = heapArray<byte>(65536);
  for (auto& b: bytes) {
    b = rand();
  }

  MockOutputStream rawOutput;
  GzipAsyncOutputStream gzipOut(rawOutput);
  gzipOut.write(bytes.begin(), bytes.size()).wait(io.waitScope);
  gzipOut.end().wait(io.waitScope);

  MockInputStream rawInput(rawOutput.bytes, kj::maxValue);
  GzipAsyncInputStream gzipIn(rawInput);
  auto decompressed = gzipIn.readAllBytes().wait(io.waitScope);

  KJ_ASSERT(decompressed.size() == bytes.size());
  KJ_ASSERT(memcmp(bytes.begin(), decompressed.begin(), bytes.size()) == 0);
}

}  // namespace
}  // namespace kj

#endif  // KJ_HAS_ZLIB
