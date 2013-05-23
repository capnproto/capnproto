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

#define CAPNPROTO_PRIVATE
#include "serialize-snappy.h"
#include "logging.h"
#include "layout.h"
#include <snappy/snappy.h>
#include <snappy/snappy-sinksource.h>
#include <vector>

namespace capnproto {

class SnappyInputStream::InputStreamSnappySource: public snappy::Source {
public:
  inline InputStreamSnappySource(BufferedInputStream& inputStream)
      : inputStream(inputStream) {}
  inline ~InputStreamSnappySource() {};

  // implements snappy::Source ---------------------------------------

  size_t Available() const override {
    FAIL_CHECK("Snappy doesn't actually call this.");
    return 0;
  }

  const char* Peek(size_t* len) override {
    ArrayPtr<const byte> buffer = inputStream.getReadBuffer();
    *len = buffer.size();
    return reinterpret_cast<const char*>(buffer.begin());
  }

  void Skip(size_t n) override {
    inputStream.skip(n);
  }

private:
  BufferedInputStream& inputStream;
};

SnappyInputStream::SnappyInputStream(BufferedInputStream& inner, ArrayPtr<byte> buffer)
    : inner(inner) {
  if (buffer.size() < SNAPPY_BUFFER_SIZE) {
    ownedBuffer = newArray<byte>(SNAPPY_BUFFER_SIZE);
    buffer = ownedBuffer;
  }
  this->buffer = buffer;
}

SnappyInputStream::~SnappyInputStream() {}

ArrayPtr<const byte> SnappyInputStream::getReadBuffer() {
  if (bufferAvailable.size() == 0) {
    refill();
  }

  return bufferAvailable;
}

size_t SnappyInputStream::read(void* dst, size_t minBytes, size_t maxBytes) {
  while (minBytes > bufferAvailable.size()) {
    memcpy(dst, bufferAvailable.begin(), bufferAvailable.size());

    dst = reinterpret_cast<byte*>(dst) + bufferAvailable.size();
    minBytes -= bufferAvailable.size();
    maxBytes -= bufferAvailable.size();

    refill();
  }

  // Serve from current buffer.
  size_t n = std::min(bufferAvailable.size(), maxBytes);
  memcpy(dst, bufferAvailable.begin(), n);
  bufferAvailable = bufferAvailable.slice(n, bufferAvailable.size());
  return n;
}

void SnappyInputStream::skip(size_t bytes) {
  while (bytes > bufferAvailable.size()) {
    bytes -= bufferAvailable.size();
    refill();
  }
  bufferAvailable = bufferAvailable.slice(bytes, bufferAvailable.size());
}

void SnappyInputStream::refill() {
  uint32_t length = 0;
  InputStreamSnappySource snappySource(inner);
  VALIDATE_INPUT(
      snappy::RawUncompress(
          &snappySource, reinterpret_cast<char*>(buffer.begin()), buffer.size(), &length),
      "Snappy decompression failed.") {
    length = 1;  // garbage
  }

  bufferAvailable = buffer.slice(0, length);
}

// =======================================================================================

SnappyOutputStream::SnappyOutputStream(
    OutputStream& inner, ArrayPtr<byte> buffer, ArrayPtr<byte> compressedBuffer)
    : inner(inner) {
  DCHECK(SNAPPY_COMPRESSED_BUFFER_SIZE >= snappy::MaxCompressedLength(snappy::kBlockSize),
      "snappy::MaxCompressedLength() changed?");

  if (buffer.size() < SNAPPY_BUFFER_SIZE) {
    ownedBuffer = newArray<byte>(SNAPPY_BUFFER_SIZE);
    buffer = ownedBuffer;
  }
  this->buffer = buffer;
  bufferPos = buffer.begin();

  if (compressedBuffer.size() < SNAPPY_COMPRESSED_BUFFER_SIZE) {
    ownedCompressedBuffer = newArray<byte>(SNAPPY_COMPRESSED_BUFFER_SIZE);
    compressedBuffer = ownedCompressedBuffer;
  }
  this->compressedBuffer = compressedBuffer;
}

SnappyOutputStream::~SnappyOutputStream() {
  if (bufferPos > buffer.begin()) {
    if (std::uncaught_exception()) {
      try {
        flush();
      } catch (...) {
        // TODO(someday): report secondary faults
      }
    } else {
      flush();
    }
  }
}

void SnappyOutputStream::flush() {
  if (bufferPos > buffer.begin()) {
    snappy::ByteArraySource source(
        reinterpret_cast<char*>(buffer.begin()), bufferPos - buffer.begin());
    snappy::UncheckedByteArraySink sink(reinterpret_cast<char*>(compressedBuffer.begin()));

    size_t n = snappy::Compress(&source, &sink);
    CHECK(n <= compressedBuffer.size(),
        "Critical security bug:  Snappy compression overran its output buffer.");
    inner.write(compressedBuffer.begin(), n);

    bufferPos = buffer.begin();
  }
}

ArrayPtr<byte> SnappyOutputStream::getWriteBuffer() {
  return arrayPtr(bufferPos, buffer.end());
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
    ArrayPtr<word> scratchSpace, ArrayPtr<byte> buffer)
    : SnappyInputStream(inputStream, buffer),
      PackedMessageReader(static_cast<SnappyInputStream&>(*this), options, scratchSpace) {}

SnappyPackedMessageReader::~SnappyPackedMessageReader() {}

void writeSnappyPackedMessage(OutputStream& output, ArrayPtr<const ArrayPtr<const word>> segments,
                              ArrayPtr<byte> buffer, ArrayPtr<byte> compressedBuffer) {
  SnappyOutputStream snappyOut(output, buffer, compressedBuffer);
  writePackedMessage(snappyOut, segments);
}

}  // namespace capnproto
