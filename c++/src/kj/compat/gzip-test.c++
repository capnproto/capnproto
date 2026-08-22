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

class MockInputStream: public InputStream {
public:
  MockInputStream(kj::ArrayPtr<const byte> bytes, size_t blockSize)
      : bytes(bytes), blockSize(blockSize) {}

  size_t tryRead(ArrayPtr<byte> buffer, size_t minBytes) override {
    // Clamp max read to blockSize.
    size_t n = kj::min(blockSize, buffer.size());

    // Unless that's less than minBytes -- in which case, use minBytes.
    n = kj::max(n, minBytes);

    // But also don't read more data than we have.
    n = kj::min(n, bytes.size());

    buffer.write(bytes.first(n));
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

    kj::arrayPtr(reinterpret_cast<byte*>(buffer), n).copyFrom(bytes.first(n));
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
    GzipInputStream gzip(rawInput);
    return gzip.readAllText();
  }

  void write(ArrayPtr<const byte> data) override { bytes.addAll(data); }
};

class MockAsyncOutputStream: public AsyncOutputStream {
public:
  kj::Vector<byte> bytes;

  kj::String decompress(WaitScope& ws) {
    MockAsyncInputStream rawInput(bytes, kj::maxValue);
    GzipAsyncInputStream gzip(rawInput);
    return gzip.readAllText().wait(ws);
  }

  Promise<void> write(ArrayPtr<const byte> buffer) override {
    bytes.addAll(buffer);
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

KJ_TEST("gzip decompression") {
  // Normal read.
  {
    MockInputStream rawInput(FOOBAR_GZIP, kj::maxValue);
    GzipInputStream gzip(rawInput);
    KJ_EXPECT(gzip.readAllText() == "foobar");
  }

  // Force read one byte at a time.
  {
    MockInputStream rawInput(FOOBAR_GZIP, 1);
    GzipInputStream gzip(rawInput);
    KJ_EXPECT(gzip.readAllText() == "foobar");
  }

  // Read truncated input.
  {
    MockInputStream rawInput(kj::arrayPtr(FOOBAR_GZIP, sizeof(FOOBAR_GZIP) / 2), kj::maxValue);
    GzipInputStream gzip(rawInput);

    byte text[16]{};
    auto amount = gzip.tryRead(text, 1);
    KJ_EXPECT(arrayPtr(text).first(amount) == "fo"_kjb);

    KJ_EXPECT_THROW_MESSAGE("gzip compressed stream ended prematurely", gzip.tryRead(text, 1));
  }

  // Read concatenated input.
  {
    Vector<byte> bytes;
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_GZIP));
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_GZIP));
    MockInputStream rawInput(bytes, kj::maxValue);
    GzipInputStream gzip(rawInput);

    KJ_EXPECT(gzip.readAllText() == "foobarfoobar");
  }
}

KJ_TEST("async gzip decompression") {
  auto io = setupAsyncIo();

  // Normal read.
  {
    MockAsyncInputStream rawInput(FOOBAR_GZIP, kj::maxValue);
    GzipAsyncInputStream gzip(rawInput);
    KJ_EXPECT(gzip.readAllText().wait(io.waitScope) == "foobar");
  }

  // Force read one byte at a time.
  {
    MockAsyncInputStream rawInput(FOOBAR_GZIP, 1);
    GzipAsyncInputStream gzip(rawInput);
    KJ_EXPECT(gzip.readAllText().wait(io.waitScope) == "foobar");
  }

  // Read truncated input.
  {
    MockAsyncInputStream rawInput(kj::arrayPtr(FOOBAR_GZIP, sizeof(FOOBAR_GZIP) / 2), kj::maxValue);
    GzipAsyncInputStream gzip(rawInput);

    char text[16]{};
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
    MockAsyncInputStream rawInput(bytes, kj::maxValue);
    GzipAsyncInputStream gzip(rawInput);

    KJ_EXPECT(gzip.readAllText().wait(io.waitScope) == "foobarfoobar");
  }

  // Decompress using an output stream.
  {
    MockAsyncOutputStream rawOutput;
    GzipAsyncOutputStream gzip(rawOutput, GzipAsyncOutputStream::DECOMPRESS);

    auto mid = sizeof(FOOBAR_GZIP) / 2;
    gzip.write(arrayPtr(FOOBAR_GZIP).first(mid)).wait(io.waitScope);
    auto str1 = kj::heapString(rawOutput.bytes.asPtr().asChars());
    KJ_EXPECT(str1 == "fo", str1);

    gzip.write(arrayPtr(FOOBAR_GZIP).slice(mid)).wait(io.waitScope);
    auto str2 = kj::heapString(rawOutput.bytes.asPtr().asChars());
    KJ_EXPECT(str2 == "foobar", str2);

    gzip.end().wait(io.waitScope);
  }
}

KJ_TEST("async gzip tryReadSync") {
  auto io = setupAsyncIo();

  {
    MockAsyncInputStream rawInput(FOOBAR_GZIP, kj::maxValue);
    GzipAsyncInputStream gzip(rawInput);

    byte text[16]{};

    // Nothing is buffered yet, so no synchronous read is possible.
    KJ_EXPECT(gzip.tryReadSync(arrayPtr(text), 1) == kj::none);

    // While an asynchronous read is in progress, concurrent synchronous reads are a programming
    // error.
    auto readPromise = gzip.tryRead(text, 3, 3);
    KJ_EXPECT_THROW_MESSAGE("concurrent read operations not allowed",
        gzip.tryReadSync(arrayPtr(text), 1));

    // The asynchronous read buffers the entire compressed input but only decompresses what was
    // asked for.
    KJ_EXPECT(readPromise.wait(io.waitScope) == 3);
    KJ_EXPECT(arrayPtr(text).first(3) == "foo"_kjb);

    // minBytes > 1 cannot be guaranteed synchronously, so it is refused (with no side effects).
    KJ_EXPECT(gzip.tryReadSync(arrayPtr(text), 2) == kj::none);

    // The rest of the data can be decompressed synchronously from the buffered input.
    KJ_EXPECT(KJ_ASSERT_NONNULL(gzip.tryReadSync(arrayPtr(text), 1)) == 3);
    KJ_EXPECT(arrayPtr(text).first(3) == "bar"_kjb);

    // All buffered input has been consumed; EOF detection requires the async path.
    KJ_EXPECT(gzip.tryReadSync(arrayPtr(text), 1) == kj::none);
    KJ_EXPECT(gzip.tryRead(text, 1, sizeof(text)).wait(io.waitScope) == 0);
  }

  // Concatenated gzip members decompress synchronously as well.
  {
    Vector<byte> bytes;
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_GZIP));
    bytes.addAll(ArrayPtr<const byte>(FOOBAR_GZIP));
    MockAsyncInputStream rawInput(bytes, kj::maxValue);
    GzipAsyncInputStream gzip(rawInput);

    byte text[16]{};

    // Buffer all compressed input with a small initial async read.
    KJ_EXPECT(gzip.tryRead(text, 1, 1).wait(io.waitScope) == 1);
    KJ_EXPECT(text[0] == 'f');

    // The remainder of the first member is served synchronously; the member boundary resets the
    // decompressor.
    KJ_EXPECT(KJ_ASSERT_NONNULL(gzip.tryReadSync(arrayPtr(text), 1)) == 5);
    KJ_EXPECT(arrayPtr(text).first(5) == "oobar"_kjb);

    // The second member is served synchronously too.
    KJ_EXPECT(KJ_ASSERT_NONNULL(gzip.tryReadSync(arrayPtr(text), 1)) == 6);
    KJ_EXPECT(arrayPtr(text).first(6) == "foobar"_kjb);

    KJ_EXPECT(gzip.tryReadSync(arrayPtr(text), 1) == kj::none);
    KJ_EXPECT(gzip.tryRead(text, 1, sizeof(text)).wait(io.waitScope) == 0);
  }
}

KJ_TEST("gzip compression") {
  // Normal write.
  {
    MockOutputStream rawOutput;
    {
      GzipOutputStream gzip(rawOutput);
      gzip.write("foobar"_kjb);
    }

    KJ_EXPECT(rawOutput.decompress() == "foobar");
  }

  // Multi-part write.
  {
    MockOutputStream rawOutput;
    {
      GzipOutputStream gzip(rawOutput);
      gzip.write("foo"_kjb);
      gzip.write("bar"_kjb);
    }

    KJ_EXPECT(rawOutput.decompress() == "foobar");
  }

  // Array-of-arrays write.
  {
    MockOutputStream rawOutput;

    {
      GzipOutputStream gzip(rawOutput);
      ArrayPtr<const byte> pieces[] = { "foo"_kjb, "bar"_kjb, };
      gzip.write(pieces);
    }

    KJ_EXPECT(rawOutput.decompress() == "foobar");
  }
}

KJ_TEST("gzip huge round trip") {
  auto bytes = heapArray<byte>(65536);
  for (auto& b: bytes) {
    b = rand();
  }

  MockOutputStream rawOutput;
  {
    GzipOutputStream gzipOut(rawOutput);
    gzipOut.write(bytes);
  }

  MockInputStream rawInput(rawOutput.bytes, kj::maxValue);
  GzipInputStream gzipIn(rawInput);
  auto decompressed = gzipIn.readAllBytes();

  KJ_ASSERT(bytes ==  decompressed);
}

KJ_TEST("async gzip compression") {
  auto io = setupAsyncIo();

  // Normal write.
  {
    MockAsyncOutputStream rawOutput;
    GzipAsyncOutputStream gzip(rawOutput);
    gzip.write("foobar"_kjb).wait(io.waitScope);
    gzip.end().wait(io.waitScope);

    KJ_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }

  // Multi-part write.
  {
    MockAsyncOutputStream rawOutput;
    GzipAsyncOutputStream gzip(rawOutput);

    gzip.write("foo"_kjb).wait(io.waitScope);
    auto prevSize = rawOutput.bytes.size();

    gzip.write("bar"_kjb).wait(io.waitScope);
    auto curSize = rawOutput.bytes.size();
    KJ_EXPECT(prevSize == curSize, prevSize, curSize);

    gzip.flush().wait(io.waitScope);
    curSize = rawOutput.bytes.size();
    KJ_EXPECT(prevSize < curSize, prevSize, curSize);

    gzip.end().wait(io.waitScope);

    KJ_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }

  // Array-of-arrays write.
  {
    MockAsyncOutputStream rawOutput;
    GzipAsyncOutputStream gzip(rawOutput);

    ArrayPtr<const byte> pieces[] = { "foo"_kjb, "bar"_kjb };
    gzip.write(pieces).wait(io.waitScope);
    gzip.end().wait(io.waitScope);

    KJ_EXPECT(rawOutput.decompress(io.waitScope) == "foobar");
  }
}

KJ_TEST("async gzip huge round trip") {
  auto io = setupAsyncIo();

  auto bytes = heapArray<byte>(65536);
  for (auto& b: bytes) {
    b = rand();
  }

  MockAsyncOutputStream rawOutput;
  GzipAsyncOutputStream gzipOut(rawOutput);
  gzipOut.write(bytes).wait(io.waitScope);
  gzipOut.end().wait(io.waitScope);

  MockAsyncInputStream rawInput(rawOutput.bytes, kj::maxValue);
  GzipAsyncInputStream gzipIn(rawInput);
  auto decompressed = gzipIn.readAllBytes().wait(io.waitScope);

  KJ_ASSERT(bytes == decompressed);
}

}  // namespace
}  // namespace kj

#endif  // KJ_HAS_ZLIB
