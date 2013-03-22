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

#ifndef CAPNPROTO_SERIALIZE_SNAPPY_H_
#define CAPNPROTO_SERIALIZE_SNAPPY_H_

#include "serialize.h"

namespace capnproto {

class SnappyMessageReader: public MessageReader {
public:
  SnappyMessageReader(InputStream& inputStream, ReaderOptions options = ReaderOptions(),
                      ArrayPtr<word> scratchSpace = nullptr);
  ~SnappyMessageReader();

  ArrayPtr<const word> getSegment(uint id) override;

private:
  InputStream& inputStream;
  Array<word> space;

  union {
    FlatArrayMessageReader underlyingReader;
  };
};

class SnappyFdMessageReader: private FdInputStream, public SnappyMessageReader {
public:
  SnappyFdMessageReader(int fd, ReaderOptions options = ReaderOptions(),
                        ArrayPtr<word> scratchSpace = nullptr)
      : FdInputStream(fd), SnappyMessageReader(*this, options, scratchSpace) {}
  // Read message from a file descriptor, without taking ownership of the descriptor.

  SnappyFdMessageReader(AutoCloseFd fd, ReaderOptions options = ReaderOptions(),
                        ArrayPtr<word> scratchSpace = nullptr)
      : FdInputStream(move(fd)), SnappyMessageReader(*this, options, scratchSpace) {}
  // Read a message from a file descriptor, taking ownership of the descriptor.

  ~SnappyFdMessageReader();
};

void writeSnappyMessage(OutputStream& output, MessageBuilder& builder);
void writeSnappyMessage(OutputStream& output, ArrayPtr<const ArrayPtr<const word>> segments);

void writeSnappyMessageToFd(int fd, MessageBuilder& builder);
void writeSnappyMessageToFd(int fd, ArrayPtr<const ArrayPtr<const word>> segments);

// =======================================================================================
// inline stuff

inline void writeSnappyMessage(OutputStream& output, MessageBuilder& builder) {
  writeSnappyMessage(output, builder.getSegmentsForOutput());
}

inline void writeSnappyMessageToFd(int fd, MessageBuilder& builder) {
  writeSnappyMessageToFd(fd, builder.getSegmentsForOutput());
}

}  // namespace capnproto

#endif  // CAPNPROTO_SERIALIZE_SNAPPY_H_
