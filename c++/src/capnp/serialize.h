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

// This file implements a simple serialization format for Cap'n Proto messages.  The format
// is as follows:
//
// * 32-bit little-endian segment count (4 bytes).
// * 32-bit little-endian size of each segment (4*(segment count) bytes).
// * Padding so that subsequent data is 64-bit-aligned (0 or 4 bytes).  (I.e., if there are an even
//     number of segments, there are 4 bytes of zeros here, otherwise there is no padding.)
// * Data from each segment, in order (8*sum(segment sizes) bytes)
//
// This format has some important properties:
// - It is self-delimiting, so multiple messages may be written to a stream without any external
//   delimiter.
// - The total size and position of each segment can be determined by reading only the first part
//   of the message, allowing lazy and random-access reading of the segment data.
// - A message is always at least 8 bytes.
// - A single-segment message can be read entirely in two system calls with no buffering.
// - A multi-segment message can be read entirely in three system calls with no buffering.
// - The format is appropriate for mmap()ing since all data is aligned.

#ifndef CAPNP_SERIALIZE_H_
#define CAPNP_SERIALIZE_H_

#include "message.h"
#include <kj/io.h>

namespace capnp {

class FlatArrayMessageReader: public MessageReader {
  // Parses a message from a flat array.  Note that it makes sense to use this together with mmap()
  // for extremely fast parsing.

public:
  FlatArrayMessageReader(kj::ArrayPtr<const word> array, ReaderOptions options = ReaderOptions());
  // The array must remain valid until the MessageReader is destroyed.

  kj::ArrayPtr<const word> getSegment(uint id) override;

private:
  // Optimize for single-segment case.
  kj::ArrayPtr<const word> segment0;
  kj::Array<kj::ArrayPtr<const word>> moreSegments;
};

kj::Array<word> messageToFlatArray(MessageBuilder& builder);
// Constructs a flat array containing the entire content of the given message.

kj::Array<word> messageToFlatArray(kj::ArrayPtr<const kj::ArrayPtr<const word>> segments);
// Version of messageToFlatArray that takes a raw segment array.

// =======================================================================================

class InputStreamMessageReader: public MessageReader {
public:
  InputStreamMessageReader(kj::InputStream& inputStream,
                           ReaderOptions options = ReaderOptions(),
                           kj::ArrayPtr<word> scratchSpace = nullptr);
  ~InputStreamMessageReader() noexcept(false);

  // implements MessageReader ----------------------------------------
  kj::ArrayPtr<const word> getSegment(uint id) override;

private:
  kj::InputStream& inputStream;
  byte* readPos;

  // Optimize for single-segment case.
  kj::ArrayPtr<const word> segment0;
  kj::Array<kj::ArrayPtr<const word>> moreSegments;

  kj::Array<word> ownedSpace;
  // Only if scratchSpace wasn't big enough.

  kj::UnwindDetector unwindDetector;
};

void writeMessage(kj::OutputStream& output, MessageBuilder& builder);
// Write the message to the given output stream.

void writeMessage(kj::OutputStream& output, kj::ArrayPtr<const kj::ArrayPtr<const word>> segments);
// Write the segment array to the given output stream.

// =======================================================================================
// Specializations for reading from / writing to file descriptors.

class StreamFdMessageReader: private kj::FdInputStream, public InputStreamMessageReader {
  // A MessageReader that reads from a steam-based file descriptor.  For seekable file descriptors
  // (e.g. actual disk files), FdFileMessageReader is better, but this will still work.

public:
  StreamFdMessageReader(int fd, ReaderOptions options = ReaderOptions(),
                        kj::ArrayPtr<word> scratchSpace = nullptr)
      : FdInputStream(fd), InputStreamMessageReader(*this, options, scratchSpace) {}
  // Read message from a file descriptor, without taking ownership of the descriptor.

  StreamFdMessageReader(kj::AutoCloseFd fd, ReaderOptions options = ReaderOptions(),
                        kj::ArrayPtr<word> scratchSpace = nullptr)
      : FdInputStream(kj::mv(fd)), InputStreamMessageReader(*this, options, scratchSpace) {}
  // Read a message from a file descriptor, taking ownership of the descriptor.

  ~StreamFdMessageReader() noexcept(false);
};

void writeMessageToFd(int fd, MessageBuilder& builder);
// Write the message to the given file descriptor.
//
// This function throws an exception on any I/O error.  If your code is not exception-safe, be sure
// you catch this exception at the call site.  If throwing an exception is not acceptable, you
// can implement your own OutputStream with arbitrary error handling and then use writeMessage().

void writeMessageToFd(int fd, kj::ArrayPtr<const kj::ArrayPtr<const word>> segments);
// Write the segment array to the given file descriptor.
//
// This function throws an exception on any I/O error.  If your code is not exception-safe, be sure
// you catch this exception at the call site.  If throwing an exception is not acceptable, you
// can implement your own OutputStream with arbitrary error handling and then use writeMessage().

// =======================================================================================
// inline stuff

inline kj::Array<word> messageToFlatArray(MessageBuilder& builder) {
  return messageToFlatArray(builder.getSegmentsForOutput());
}

inline void writeMessage(kj::OutputStream& output, MessageBuilder& builder) {
  writeMessage(output, builder.getSegmentsForOutput());
}

inline void writeMessageToFd(int fd, MessageBuilder& builder) {
  writeMessageToFd(fd, builder.getSegmentsForOutput());
}

}  // namespace capnp

#endif  // SERIALIZE_H_
