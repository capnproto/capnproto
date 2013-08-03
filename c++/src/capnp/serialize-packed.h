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

#ifndef CAPNP_SERIALIZE_PACKED_H_
#define CAPNP_SERIALIZE_PACKED_H_

#include "serialize.h"

namespace capnp {

namespace _ {  // private

class PackedInputStream: public kj::InputStream {
  // An input stream that unpacks packed data with a picky constraint:  The caller must read data
  // in the exact same size and sequence as the data was written to PackedOutputStream.

public:
  explicit PackedInputStream(kj::BufferedInputStream& inner);
  KJ_DISALLOW_COPY(PackedInputStream);
  ~PackedInputStream() noexcept(false);

  // implements InputStream ------------------------------------------
  size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) override;
  void skip(size_t bytes) override;

private:
  kj::BufferedInputStream& inner;
};

class PackedOutputStream: public kj::OutputStream {
public:
  explicit PackedOutputStream(kj::BufferedOutputStream& inner);
  KJ_DISALLOW_COPY(PackedOutputStream);
  ~PackedOutputStream() noexcept(false);

  // implements OutputStream -----------------------------------------
  void write(const void* buffer, size_t bytes) override;

private:
  kj::BufferedOutputStream& inner;
};

}  // namespace _ (private)

class PackedMessageReader: private _::PackedInputStream, public InputStreamMessageReader {
public:
  PackedMessageReader(kj::BufferedInputStream& inputStream, ReaderOptions options = ReaderOptions(),
                      kj::ArrayPtr<word> scratchSpace = nullptr);
  KJ_DISALLOW_COPY(PackedMessageReader);
  ~PackedMessageReader() noexcept(false);
};

class PackedFdMessageReader: private kj::FdInputStream, private kj::BufferedInputStreamWrapper,
                             public PackedMessageReader {
public:
  PackedFdMessageReader(int fd, ReaderOptions options = ReaderOptions(),
                        kj::ArrayPtr<word> scratchSpace = nullptr);
  // Read message from a file descriptor, without taking ownership of the descriptor.
  // Note that if you want to reuse the descriptor after the reader is destroyed, you'll need to
  // seek it, since otherwise the position is unspecified.

  PackedFdMessageReader(kj::AutoCloseFd fd, ReaderOptions options = ReaderOptions(),
                        kj::ArrayPtr<word> scratchSpace = nullptr);
  // Read a message from a file descriptor, taking ownership of the descriptor.

  KJ_DISALLOW_COPY(PackedFdMessageReader);

  ~PackedFdMessageReader() noexcept(false);
};

void writePackedMessage(kj::BufferedOutputStream& output, MessageBuilder& builder);
void writePackedMessage(kj::BufferedOutputStream& output,
                        kj::ArrayPtr<const kj::ArrayPtr<const word>> segments);
// Write a packed message to a buffered output stream.

void writePackedMessage(kj::OutputStream& output, MessageBuilder& builder);
void writePackedMessage(kj::OutputStream& output,
                        kj::ArrayPtr<const kj::ArrayPtr<const word>> segments);
// Write a packed message to an unbuffered output stream.  If you intend to write multiple messages
// in succession, consider wrapping your output in a buffered stream in order to reduce system
// call overhead.

void writePackedMessageToFd(int fd, MessageBuilder& builder);
void writePackedMessageToFd(int fd, kj::ArrayPtr<const kj::ArrayPtr<const word>> segments);
// Write a single packed message to the file descriptor.

// =======================================================================================
// inline stuff

inline void writePackedMessage(kj::BufferedOutputStream& output, MessageBuilder& builder) {
  writePackedMessage(output, builder.getSegmentsForOutput());
}

inline void writePackedMessage(kj::OutputStream& output, MessageBuilder& builder) {
  writePackedMessage(output, builder.getSegmentsForOutput());
}

inline void writePackedMessageToFd(int fd, MessageBuilder& builder) {
  writePackedMessageToFd(fd, builder.getSegmentsForOutput());
}

}  // namespace capnp

#endif  // CAPNP_SERIALIZE_PACKED_H_
