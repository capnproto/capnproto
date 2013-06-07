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

#ifndef CAPNP_SERIALIZE_SNAPPY_H_
#define CAPNP_SERIALIZE_SNAPPY_H_

#include "serialize.h"
#include "serialize-packed.h"

namespace capnp {

constexpr size_t SNAPPY_BUFFER_SIZE = 65536;
constexpr size_t SNAPPY_COMPRESSED_BUFFER_SIZE = 76490;

class SnappyInputStream: public kj::BufferedInputStream {
public:
  explicit SnappyInputStream(BufferedInputStream& inner, kj::ArrayPtr<byte> buffer = nullptr);
  KJ_DISALLOW_COPY(SnappyInputStream);
  ~SnappyInputStream() noexcept(false);

  // implements BufferedInputStream ----------------------------------
  kj::ArrayPtr<const byte> getReadBuffer() override;
  size_t read(void* buffer, size_t minBytes, size_t maxBytes) override;
  void skip(size_t bytes) override;

private:
  class InputStreamSnappySource;

  BufferedInputStream& inner;
  kj::Array<byte> ownedBuffer;
  kj::ArrayPtr<byte> buffer;
  kj::ArrayPtr<byte> bufferAvailable;

  void refill();
};

class SnappyOutputStream: public kj::BufferedOutputStream {
public:
  explicit SnappyOutputStream(OutputStream& inner,
                              kj::ArrayPtr<byte> buffer = nullptr,
                              kj::ArrayPtr<byte> compressedBuffer = nullptr);
  KJ_DISALLOW_COPY(SnappyOutputStream);
  ~SnappyOutputStream() noexcept(false);

  void flush();
  // Force the stream to write any remaining bytes in its buffer to the inner stream.  This will
  // hurt compression, of course, by forcing the current block to end prematurely.

  // implements BufferedOutputStream ---------------------------------
  kj::ArrayPtr<byte> getWriteBuffer() override;
  void write(const void* buffer, size_t size) override;

private:
  OutputStream& inner;

  kj::Array<byte> ownedBuffer;
  kj::ArrayPtr<byte> buffer;
  byte* bufferPos;

  kj::Array<byte> ownedCompressedBuffer;
  kj::ArrayPtr<byte> compressedBuffer;

  kj::UnwindDetector unwindDetector;
};

class SnappyPackedMessageReader: private SnappyInputStream, public PackedMessageReader {
public:
  SnappyPackedMessageReader(
      BufferedInputStream& inputStream, ReaderOptions options = ReaderOptions(),
      kj::ArrayPtr<word> scratchSpace = nullptr, kj::ArrayPtr<byte> buffer = nullptr);
  ~SnappyPackedMessageReader() noexcept(false);
};

void writeSnappyPackedMessage(kj::OutputStream& output, MessageBuilder& builder,
                              kj::ArrayPtr<byte> buffer = nullptr,
                              kj::ArrayPtr<byte> compressedBuffer = nullptr);
void writeSnappyPackedMessage(kj::OutputStream& output,
                              kj::ArrayPtr<const kj::ArrayPtr<const word>> segments,
                              kj::ArrayPtr<byte> buffer = nullptr,
                              kj::ArrayPtr<byte> compressedBuffer = nullptr);

// =======================================================================================
// inline stuff

inline void writeSnappyPackedMessage(kj::OutputStream& output, MessageBuilder& builder,
                                     kj::ArrayPtr<byte> buffer,
                                     kj::ArrayPtr<byte> compressedBuffer) {
  writeSnappyPackedMessage(output, builder.getSegmentsForOutput(), buffer, compressedBuffer);
}

}  // namespace capnp

#endif  // CAPNP_SERIALIZE_SNAPPY_H_
