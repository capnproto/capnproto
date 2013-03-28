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

#ifndef CAPNPROTO_SERIALIZE_PACKED_H_
#define CAPNPROTO_SERIALIZE_PACKED_H_

#include "serialize.h"

namespace capnproto {

namespace internal {

class PackedInputStream: public InputStream {
  // An input stream that unpacks packed data with a picky constraint:  The caller must read data
  // in the exact same size and sequence as the data was written to PackedOutputStream.

public:
  explicit PackedInputStream(BufferedInputStream& inner);
  CAPNPROTO_DISALLOW_COPY(PackedInputStream);
  ~PackedInputStream();

  // implements InputStream ------------------------------------------
  size_t read(void* buffer, size_t minBytes, size_t maxBytes) override;
  void skip(size_t bytes) override;

private:
  BufferedInputStream& inner;
};

class PackedOutputStream: public OutputStream {
public:
  explicit PackedOutputStream(BufferedOutputStream& inner);
  CAPNPROTO_DISALLOW_COPY(PackedOutputStream);
  ~PackedOutputStream();

  // implements OutputStream -----------------------------------------
  void write(const void* buffer, size_t bytes) override;

private:
  BufferedOutputStream& inner;
};

}  // namespace internal

class PackedMessageReader: private internal::PackedInputStream, public InputStreamMessageReader {
public:
  PackedMessageReader(BufferedInputStream& inputStream, ReaderOptions options = ReaderOptions(),
                      ArrayPtr<word> scratchSpace = nullptr);
  CAPNPROTO_DISALLOW_COPY(PackedMessageReader);
  ~PackedMessageReader();
};

class PackedFdMessageReader: private FdInputStream, private BufferedInputStreamWrapper,
                             public PackedMessageReader {
public:
  PackedFdMessageReader(int fd, ReaderOptions options = ReaderOptions(),
                        ArrayPtr<word> scratchSpace = nullptr);
  // Read message from a file descriptor, without taking ownership of the descriptor.
  // Note that if you want to reuse the descriptor after the reader is destroyed, you'll need to
  // seek it, since otherwise the position is unspecified.

  PackedFdMessageReader(AutoCloseFd fd, ReaderOptions options = ReaderOptions(),
                        ArrayPtr<word> scratchSpace = nullptr);
  // Read a message from a file descriptor, taking ownership of the descriptor.

  CAPNPROTO_DISALLOW_COPY(PackedFdMessageReader);

  ~PackedFdMessageReader();
};

void writePackedMessage(BufferedOutputStream& output, MessageBuilder& builder);
void writePackedMessage(BufferedOutputStream& output,
                        ArrayPtr<const ArrayPtr<const word>> segments);
// Write a packed message to a buffered output stream.

void writePackedMessage(OutputStream& output, MessageBuilder& builder);
void writePackedMessage(OutputStream& output, ArrayPtr<const ArrayPtr<const word>> segments);
// Write a packed message to an unbuffered output stream.  If you intend to write multiple messages
// in succession, consider wrapping your output in a buffered stream in order to reduce system
// call overhead.

void writePackedMessageToFd(int fd, MessageBuilder& builder);
void writePackedMessageToFd(int fd, ArrayPtr<const ArrayPtr<const word>> segments);
// Write a single packed message to the file descriptor.

// =======================================================================================
// inline stuff

inline void writePackedMessage(BufferedOutputStream& output, MessageBuilder& builder) {
  writePackedMessage(output, builder.getSegmentsForOutput());
}

inline void writePackedMessage(OutputStream& output, MessageBuilder& builder) {
  writePackedMessage(output, builder.getSegmentsForOutput());
}

inline void writePackedMessageToFd(int fd, MessageBuilder& builder) {
  writePackedMessageToFd(fd, builder.getSegmentsForOutput());
}

}  // namespace capnproto

#endif  // CAPNPROTO_SERIALIZE_PACKED_H_
