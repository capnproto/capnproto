// Copyright (c) 2023 Cloudflare, Inc. and contributors
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

#if KJ_HAS_BROTLI

#include "brotli.h"
#include <kj/test.h>
#include <kj/debug.h>
#include <stdlib.h>

namespace kj {
namespace {

static const byte FOOBAR_BR[] = {
  0x83, 0x02, 0x80, 0x66, 0x6f, 0x6f, 0x62, 0x61, 0x72, 0x03,
};

// brotli stream with 24 window bits, i.e. the max window size. If KJ_BROTLI_MAX_DEC_WBITS is less
// than 24, the stream will be rejected by default. This approach should be acceptable in a web
// context, where few files benefit from larger windows and memory usage matters for
// concurrent transfers.
static const byte FOOBAR_BR_LARGE_WIN[] = {
  0x8f, 0x02, 0x80, 0x66, 0x6f, 0x6f, 0x62, 0x61, 0x72, 0x03,
};

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

class MockAsyncInputStream: public AsyncInputStream {
public:
  MockAsyncInputStream(kj::ArrayPtr<const byte> bytes, size_t blockSize)
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

class MockOutputStream: public OutputStream {
public:
  kj::Vector<byte> bytes;

  kj::String decompress() {
    MockInputStream rawInput(bytes, kj::maxValue);
    BrotliInputStream brotli(rawInput);
    return brotli.readAllText();
  }

  void write(const void* buffer, size_t size) override {
    bytes.addAll(arrayPtr(reinterpret_cast<const byte*>(buffer), size));
  }
  void write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    for (auto& piece: pieces) {
      bytes.addAll(piece);
    }
  }
};

class MockAsyncOutputStream: public AsyncOutputStream {
public:
  kj::Vector<byte> bytes;

  kj::String decompress(WaitScope& ws) {
    MockAsyncInputStream rawInput(bytes, kj::maxValue);
    BrotliAsyncInputStream brotli(rawInput);
    return brotli.readAllText().wait(ws);
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

  Promise<void> whenWriteDisconnected() override { KJ_UNIMPLEMENTED("not used"); }
};

KJ_TEST("brotli decompression") {
  // Normal read.
  {
    MockInputStream rawInput(FOOBAR_BR, kj::maxValue);
    BrotliInputStream brotli(rawInput);
    KJ_EXPECT(brotli.readAllText() == "foobar");
  }

  // Force read one byte at a time.
  {
    MockInputStream rawInput(FOOBAR_BR, 1);
    BrotliInputStream brotli(rawInput);
    KJ_EXPECT(brotli.readAllText() == "foobar");
  }

  // Read truncated input.
  {
    MockInputStream rawInput(kj::arrayPtr(FOOBAR_BR, sizeof(FOOBAR_BR) / 2), kj::maxValue);
    BrotliInputStream brotli(rawInput);

    char text[16];
    size_t n = brotli.tryRead(text, 1, sizeof(text));
    text[n] = '\0';
    KJ_EXPECT(StringPtr(text, n) == "fo");

    KJ_EXPECT_THROW_MESSAGE("brotli compressed stream ended prematurely",
        brotli.tryRead(text, 1, sizeof(text)));
  }

  // Check that stream with high window size is rejected. Conversely, check that it is accepted if
  // configured to accept the full window size.
  {
    MockInputStream rawInput(FOOBAR_BR_LARGE_WIN, kj::maxValue);
    BrotliInputStream brotli(rawInput, BROTLI_DEFAULT_WINDOW);
    KJ_EXPECT_THROW_MESSAGE("brotli window size too big", brotli.readAllText());
  }

  {
    MockInputStream rawInput(FOOBAR_BR_LARGE_WIN, kj::maxValue);
    BrotliInputStream brotli(rawInput, BROTLI_MAX_WINDOW_BITS);
    KJ_EXPECT(brotli.readAllText() == "foobar");
  }

  // Check that invalid stream is rejected.
  {
    MockInputStream rawInput(kj::arrayPtr(FOOBAR_BR + 3, sizeof(FOOBAR_BR) - 3), kj::maxValue);
    BrotliInputStream brotli(rawInput);
    KJ_EXPECT_THROW_MESSAGE("brotli decompression failed", brotli.readAllText());
  }

  // Read concatenated input.
  {
    Vector<byte> bytes;
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_BR));
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_BR));
    MockInputStream rawInput(bytes, kj::maxValue);
    BrotliInputStream brotli(rawInput);

    KJ_EXPECT(brotli.readAllText() == "foobarfoobar");
  }
}

KJ_TEST("async brotli decompression") {
  auto io = setupAsyncIo();

  // Normal read.
  {
    MockAsyncInputStream rawInput(FOOBAR_BR, kj::maxValue);
    BrotliAsyncInputStream brotli(rawInput);
    KJ_EXPECT(brotli.readAllText().wait(io.waitScope) == "foobar");
  }

  // Force read one byte at a time.
  {
    MockAsyncInputStream rawInput(FOOBAR_BR, 1);
    BrotliAsyncInputStream brotli(rawInput);
    KJ_EXPECT(brotli.readAllText().wait(io.waitScope) == "foobar");
  }

  // Read truncated input.
  {
    MockAsyncInputStream rawInput(kj::arrayPtr(FOOBAR_BR, sizeof(FOOBAR_BR) / 2), kj::maxValue);
    BrotliAsyncInputStream brotli(rawInput);

    char text[16];
    size_t n = brotli.tryRead(text, 1, sizeof(text)).wait(io.waitScope);
    text[n] = '\0';
    KJ_EXPECT(StringPtr(text, n) == "fo");

    KJ_EXPECT_THROW_MESSAGE("brotli compressed stream ended prematurely",
        brotli.tryRead(text, 1, sizeof(text)).wait(io.waitScope));
  }

  // Check that stream with high window size is rejected. Conversely, check that it is accepted if
  // configured to accept the full window size.
  {
    MockAsyncInputStream rawInput(FOOBAR_BR_LARGE_WIN, kj::maxValue);
    BrotliAsyncInputStream brotli(rawInput, BROTLI_DEFAULT_WINDOW);
    KJ_EXPECT_THROW_MESSAGE("brotli window size too big",
      brotli.readAllText().wait(io.waitScope));
  }

  {
    MockAsyncInputStream rawInput(FOOBAR_BR_LARGE_WIN, kj::maxValue);
    BrotliAsyncInputStream brotli(rawInput, BROTLI_MAX_WINDOW_BITS);
    KJ_EXPECT(brotli.readAllText().wait(io.waitScope) == "foobar");
  }

  // Read concatenated input.
  {
    Vector<byte> bytes;
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_BR));
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_BR));
    MockAsyncInputStream rawInput(bytes, kj::maxValue);
    BrotliAsyncInputStream brotli(rawInput);

    KJ_EXPECT(brotli.readAllText().wait(io.waitScope) == "foobarfoobar");
  }

  // Decompress using an output stream.
  {
    MockAsyncOutputStream rawOutput;
    BrotliAsyncOutputStream brotli(rawOutput, BrotliAsyncOutputStream::DECOMPRESS);

    auto mid = sizeof(FOOBAR_BR) / 2;
    brotli.write(FOOBAR_BR, mid).wait(io.waitScope);
    auto str1 = kj::heapString(rawOutput.bytes.asPtr().asChars());
    KJ_EXPECT(str1 == "fo", str1);

    brotli.write(FOOBAR_BR + mid, sizeof(FOOBAR_BR) - mid).wait(io.waitScope);
    auto str2 = kj::heapString(rawOutput.bytes.asPtr().asChars());
    KJ_EXPECT(str2 == "foobar", str2);

    brotli.end().wait(io.waitScope);
  }
}

KJ_TEST("brotli compression") {
  // Normal write.
  {
    MockOutputStream rawOutput;
    {
      BrotliOutputStream brotli(rawOutput);
      brotli.write("foobar", 6);
    }

    KJ_EXPECT(rawOutput.decompress() == "foobar");
  }

  // Multi-part write.
  {
    MockOutputStream rawOutput;
    {
      BrotliOutputStream brotli(rawOutput);
      brotli.write("foo", 3);
      brotli.write("bar", 3);
    }

    KJ_EXPECT(rawOutput.decompress() == "foobar");
  }

  // Array-of-arrays write.
  {
    MockOutputStream rawOutput;

    {
      BrotliOutputStream brotli(rawOutput);

      ArrayPtr<const byte> pieces[] = {
        kj::StringPtr("foo").asBytes(),
        kj::StringPtr("bar").asBytes(),
      };
      brotli.write(pieces);
    }

    KJ_EXPECT(rawOutput.decompress() == "foobar");
  }
}

KJ_TEST("brotli huge round trip") {
  auto bytes = heapArray<byte>(96*1024);
  for (auto& b: bytes) {
    b = rand();
  }

  MockOutputStream rawOutput;
  {
    BrotliOutputStream brotliOut(rawOutput);
    brotliOut.write(bytes.begin(), bytes.size());
  }

  MockInputStream rawInput(rawOutput.bytes, kj::maxValue);
  BrotliInputStream brotliIn(rawInput);
  auto decompressed = brotliIn.readAllBytes();

  KJ_ASSERT(decompressed.size() == bytes.size());
  KJ_ASSERT(memcmp(bytes.begin(), decompressed.begin(), bytes.size()) == 0);
}

KJ_TEST("async brotli compression") {
  auto io = setupAsyncIo();
  // Normal write.
  {
    MockAsyncOutputStream rawOutput;
    BrotliAsyncOutputStream brotli(rawOutput);
    brotli.write("foobar", 6).wait(io.waitScope);
    brotli.end().wait(io.waitScope);

    KJ_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }

  // Multi-part write.
  {
    MockAsyncOutputStream rawOutput;
    BrotliAsyncOutputStream brotli(rawOutput);

    brotli.write("foo", 3).wait(io.waitScope);
    auto prevSize = rawOutput.bytes.size();

    brotli.write("bar", 3).wait(io.waitScope);
    auto curSize = rawOutput.bytes.size();
    KJ_EXPECT(prevSize == curSize, prevSize, curSize);

    brotli.flush().wait(io.waitScope);
    curSize = rawOutput.bytes.size();
    KJ_EXPECT(prevSize < curSize, prevSize, curSize);

    brotli.end().wait(io.waitScope);

    KJ_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }

  // Array-of-arrays write.
  {
    MockAsyncOutputStream rawOutput;
    BrotliAsyncOutputStream brotli(rawOutput);

    ArrayPtr<const byte> pieces[] = {
      kj::StringPtr("foo").asBytes(),
      kj::StringPtr("bar").asBytes(),
    };
    brotli.write(pieces).wait(io.waitScope);
    brotli.end().wait(io.waitScope);

    KJ_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }
}

KJ_TEST("async brotli huge round trip") {
  auto io = setupAsyncIo();

  auto bytes = heapArray<byte>(65536);
  for (auto& b: bytes) {
    b = rand();
  }

  MockAsyncOutputStream rawOutput;
  BrotliAsyncOutputStream brotliOut(rawOutput);
  brotliOut.write(bytes.begin(), bytes.size()).wait(io.waitScope);
  brotliOut.end().wait(io.waitScope);

  MockAsyncInputStream rawInput(rawOutput.bytes, kj::maxValue);
  BrotliAsyncInputStream brotliIn(rawInput);
  auto decompressed = brotliIn.readAllBytes().wait(io.waitScope);

  KJ_ASSERT(decompressed.size() == bytes.size());
  KJ_ASSERT(memcmp(bytes.begin(), decompressed.begin(), bytes.size()) == 0);
}

}  // namespace
}  // namespace kj

#endif  // KJ_HAS_BROTLI
