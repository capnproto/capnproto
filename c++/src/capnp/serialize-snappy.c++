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
#include "layout.h"
#include <snappy/snappy.h>
#include <snappy/snappy-sinksource.h>
#include <vector>

namespace capnp {

class SnappyInputStream::InputStreamSnappySource: public snappy::Source {
public:
  inline InputStreamSnappySource(BufferedInputStream& inputStream)
      : inputStream(inputStream) {}
  inline ~InputStreamSnappySource() noexcept {}

  bool atEnd() {
    return inputStream.getReadBuffer().size() == 0;
  }

  // implements snappy::Source ---------------------------------------

  size_t Available() const override {
    KJ_FAIL_ASSERT("Snappy doesn't actually call this.");
    return 0;
  }

  const char* Peek(size_t* len) override {
    kj::ArrayPtr<const byte> buffer = inputStream.getReadBuffer();
    *len = buffer.size();
    return reinterpret_cast<const char*>(buffer.begin());
  }

  void Skip(size_t n) override {
    inputStream.skip(n);
  }

private:
  BufferedInputStream& inputStream;
};

SnappyInputStream::SnappyInputStream(BufferedInputStream& inner, kj::ArrayPtr<byte> buffer)
    : inner(inner) {
  if (buffer.size() < SNAPPY_BUFFER_SIZE) {
    ownedBuffer = kj::heapArray<byte>(SNAPPY_BUFFER_SIZE);
    buffer = ownedBuffer;
  }
  this->buffer = buffer;
}

SnappyInputStream::~SnappyInputStream() noexcept(false) {}

kj::ArrayPtr<const byte> SnappyInputStream::tryGetReadBuffer() {
  if (bufferAvailable.size() == 0) {
    refill();
  }

  return bufferAvailable;
}

size_t SnappyInputStream::tryRead(void* dst, size_t minBytes, size_t maxBytes) {
  size_t total = 0;
  while (minBytes > bufferAvailable.size()) {
    memcpy(dst, bufferAvailable.begin(), bufferAvailable.size());

    dst = reinterpret_cast<byte*>(dst) + bufferAvailable.size();
    total += bufferAvailable.size();
    minBytes -= bufferAvailable.size();
    maxBytes -= bufferAvailable.size();

    if (!refill()) {
      return total;
    }
  }

  // Serve from current buffer.
  size_t n = std::min(bufferAvailable.size(), maxBytes);
  memcpy(dst, bufferAvailable.begin(), n);
  bufferAvailable = bufferAvailable.slice(n, bufferAvailable.size());
  return total + n;
}

void SnappyInputStream::skip(size_t bytes) {
  while (bytes > bufferAvailable.size()) {
    bytes -= bufferAvailable.size();
    KJ_REQUIRE(refill(), "Premature EOF");
  }
  bufferAvailable = bufferAvailable.slice(bytes, bufferAvailable.size());
}

bool SnappyInputStream::refill() {
  uint32_t length = 0;
  InputStreamSnappySource snappySource(inner);

  if (snappySource.atEnd()) {
    return false;
  }

  KJ_REQUIRE(
      snappy::RawUncompress(
          &snappySource, reinterpret_cast<char*>(buffer.begin()), buffer.size(), &length),
      "Snappy decompression failed.") {
    return false;
  }

  bufferAvailable = buffer.slice(0, length);
  return true;
}

// =======================================================================================

SnappyOutputStream::SnappyOutputStream(
    OutputStream& inner, kj::ArrayPtr<byte> buffer, kj::ArrayPtr<byte> compressedBuffer)
    : inner(inner) {
  KJ_DASSERT(SNAPPY_COMPRESSED_BUFFER_SIZE >= snappy::MaxCompressedLength(snappy::kBlockSize),
      "snappy::MaxCompressedLength() changed?");

  if (buffer.size() < SNAPPY_BUFFER_SIZE) {
    ownedBuffer = kj::heapArray<byte>(SNAPPY_BUFFER_SIZE);
    buffer = ownedBuffer;
  }
  this->buffer = buffer;
  bufferPos = buffer.begin();

  if (compressedBuffer.size() < SNAPPY_COMPRESSED_BUFFER_SIZE) {
    ownedCompressedBuffer = kj::heapArray<byte>(SNAPPY_COMPRESSED_BUFFER_SIZE);
    compressedBuffer = ownedCompressedBuffer;
  }
  this->compressedBuffer = compressedBuffer;
}

SnappyOutputStream::~SnappyOutputStream() noexcept(false) {
  if (bufferPos > buffer.begin()) {
    unwindDetector.catchExceptionsIfUnwinding([&]() {
      flush();
    });
  }
}

void SnappyOutputStream::flush() {
  if (bufferPos > buffer.begin()) {
    snappy::ByteArraySource source(
        reinterpret_cast<char*>(buffer.begin()), bufferPos - buffer.begin());
    snappy::UncheckedByteArraySink sink(reinterpret_cast<char*>(compressedBuffer.begin()));

    size_t n = snappy::Compress(&source, &sink);
    KJ_ASSERT(n <= compressedBuffer.size(),
        "Critical security bug:  Snappy compression overran its output buffer.");
    inner.write(compressedBuffer.begin(), n);

    bufferPos = buffer.begin();
  }
}

kj::ArrayPtr<byte> SnappyOutputStream::getWriteBuffer() {
  return kj::arrayPtr(bufferPos, buffer.end());
}

void SnappyOutputStream::write(const void* src, size_t size) {
  if (src == bufferPos) {
    // Oh goody, the caller wrote directly into our buffer.
    bufferPos += size;
  } else {
    for (;;) {
      size_t available = buffer.end() - bufferPos;
      if (size < available) break;
      memcpy(bufferPos, src, available);
      size -= available;
      src = reinterpret_cast<const byte*>(src) + available;
      bufferPos = buffer.end();
      flush();
    }

    memcpy(bufferPos, src, size);
    bufferPos += size;
  }
}

// =======================================================================================

SnappyPackedMessageReader::SnappyPackedMessageReader(
    BufferedInputStream& inputStream, ReaderOptions options,
    kj::ArrayPtr<word> scratchSpace, kj::ArrayPtr<byte> buffer)
    : SnappyInputStream(inputStream, buffer),
      PackedMessageReader(static_cast<SnappyInputStream&>(*this), options, scratchSpace) {}

SnappyPackedMessageReader::~SnappyPackedMessageReader() noexcept(false) {}

void writeSnappyPackedMessage(kj::OutputStream& output,
                              kj::ArrayPtr<const kj::ArrayPtr<const word>> segments,
                              kj::ArrayPtr<byte> buffer, kj::ArrayPtr<byte> compressedBuffer) {
  SnappyOutputStream snappyOut(output, buffer, compressedBuffer);
  writePackedMessage(snappyOut, segments);
}

}  // namespace capnp
