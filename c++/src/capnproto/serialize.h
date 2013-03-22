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

#ifndef CAPNPROTO_SERIALIZE_H_
#define CAPNPROTO_SERIALIZE_H_

#include "message.h"

namespace capnproto {

class FlatArrayMessageReader: public MessageReader {
  // Parses a message from a flat array.  Note that it makes sense to use this together with mmap()
  // for extremely fast parsing.

public:
  FlatArrayMessageReader(ArrayPtr<const word> array, ReaderOptions options = ReaderOptions());
  // The array must remain valid until the MessageReader is destroyed.

  ArrayPtr<const word> getSegment(uint id) override;

private:
  // Optimize for single-segment case.
  ArrayPtr<const word> segment0;
  Array<ArrayPtr<const word>> moreSegments;
};

Array<word> messageToFlatArray(MessageBuilder& builder);
// Constructs a flat array containing the entire content of the given message.

Array<word> messageToFlatArray(ArrayPtr<const ArrayPtr<const word>> segments);
// Version of messageToFlatArray that takes a raw segment array.

// =======================================================================================

class InputStream {
public:
  virtual ~InputStream();

  virtual size_t read(void* buffer, size_t minBytes, size_t maxBytes) = 0;
  // Reads at least minBytes and at most maxBytes, copying them into the given buffer.  Returns
  // the size read.  Throws an exception on errors.
  //
  // maxBytes is the number of bytes the caller really wants, but minBytes is the minimum amount
  // needed by the caller before it can start doing useful processing.  If the stream returns less
  // than maxBytes, the caller will usually call read() again later to get the rest.  Returning
  // less than maxBytes is useful when it makes sense for the caller to parallelize processing
  // with I/O.
  //
  // Cap'n Proto never asks for more bytes than it knows are part of the message.  Therefore, if
  // the InputStream happens to know that the stream will never reach maxBytes -- even if it has
  // reached minBytes -- it should throw an exception to avoid wasting time processing an incomplete
  // message.  If it can't even reach minBytes, it MUST throw an exception, as the caller is not
  // expected to understand how to deal with partial reads.

  inline void read(void* buffer, size_t bytes) { read(buffer, bytes, bytes); }
  // Convenience method for reading an exact number of bytes.

  virtual void skip(size_t bytes);
  // Skips past the given number of bytes, discarding them.  The default implementation read()s
  // into a scratch buffer.
};

class OutputStream {
public:
  virtual ~OutputStream();

  virtual void write(const void* buffer, size_t size) = 0;
  // Always writes the full size.  Throws exception on error.

  virtual void write(ArrayPtr<const ArrayPtr<const byte>> pieces);
  // Equivalent to write()ing each byte array in sequence, which is what the default implementation
  // does.  Override if you can do something better, e.g. use writev() to do the write in a single
  // syscall.
};

class InputStreamMessageReader: public MessageReader {
public:
  InputStreamMessageReader(InputStream& inputStream,
                           ReaderOptions options = ReaderOptions(),
                           ArrayPtr<word> scratchSpace = nullptr);
  ~InputStreamMessageReader();

  // implements MessageReader ----------------------------------------
  ArrayPtr<const word> getSegment(uint id) override;

private:
  InputStream& inputStream;
  byte* readPos;

  // Optimize for single-segment case.
  ArrayPtr<const word> segment0;
  Array<ArrayPtr<const word>> moreSegments;

  Array<word> ownedSpace;
  // Only if scratchSpace wasn't big enough.
};

void writeMessage(OutputStream& output, MessageBuilder& builder);
// Write the message to the given output stream.

void writeMessage(OutputStream& output, ArrayPtr<const ArrayPtr<const word>> segments);
// Write the segment array to the given output stream.

// =======================================================================================
// Specializations for reading from / writing to file descriptors.

class AutoCloseFd {
  // A wrapper around a file descriptor which automatically closes the descriptor when destroyed.
  // The wrapper supports move construction for transferring ownership of the descriptor.  If
  // close() returns an error, the destructor throws an exception, UNLESS the destructor is being
  // called during unwind from another exception, in which case the close error is ignored.
  //
  // If your code is not exception-safe, you should not use AutoCloseFd.  In this case you will
  // have to call close() yourself and handle errors appropriately.
  //
  // TODO:  Create a general helper library for reporting/detecting secondary exceptions that
  //   occurred during unwind of some primary exception.

public:
  inline AutoCloseFd(): fd(-1) {}
  inline AutoCloseFd(std::nullptr_t): fd(-1) {}
  inline explicit AutoCloseFd(int fd): fd(fd) {}
  inline AutoCloseFd(AutoCloseFd&& other): fd(other.fd) { other.fd = -1; }
  CAPNPROTO_DISALLOW_COPY(AutoCloseFd);
  ~AutoCloseFd();

  inline operator int() { return fd; }
  inline int get() { return fd; }

  inline bool operator==(std::nullptr_t) { return fd < 0; }
  inline bool operator!=(std::nullptr_t) { return fd >= 0; }

private:
  int fd;
};

class FdInputStream: public InputStream {
  // An InputStream wrapping a file descriptor.

public:
  FdInputStream(int fd): fd(fd) {};
  FdInputStream(AutoCloseFd fd): fd(fd), autoclose(move(fd)) {}
  ~FdInputStream();

  size_t read(void* buffer, size_t minBytes, size_t maxBytes) override;

private:
  int fd;
  AutoCloseFd autoclose;
};

class FdOutputStream: public OutputStream {
  // An OutputStream wrapping a file descriptor.

public:
  FdOutputStream(int fd): fd(fd) {};
  FdOutputStream(AutoCloseFd fd): fd(fd), autoclose(move(fd)) {}
  ~FdOutputStream();

  void write(const void* buffer, size_t size) override;
  void write(ArrayPtr<const ArrayPtr<const byte>> pieces) override;

private:
  int fd;
  AutoCloseFd autoclose;
};

class StreamFdMessageReader: private FdInputStream, public InputStreamMessageReader {
  // A MessageReader that reads from a steam-based file descriptor.  For seekable file descriptors
  // (e.g. actual disk files), FdFileMessageReader is better, but this will still work.

public:
  StreamFdMessageReader(int fd, ReaderOptions options = ReaderOptions(),
                        ArrayPtr<word> scratchSpace = nullptr)
      : FdInputStream(fd), InputStreamMessageReader(*this, options, scratchSpace) {}
  // Read message from a file descriptor, without taking ownership of the descriptor.

  StreamFdMessageReader(AutoCloseFd fd, ReaderOptions options = ReaderOptions(),
                        ArrayPtr<word> scratchSpace = nullptr)
      : FdInputStream(move(fd)), InputStreamMessageReader(*this, options, scratchSpace) {}
  // Read a message from a file descriptor, taking ownership of the descriptor.

  ~StreamFdMessageReader();
};

void writeMessageToFd(int fd, MessageBuilder& builder);
// Write the message to the given file descriptor.
//
// This function throws an exception on any I/O error.  If your code is not exception-safe, be sure
// you catch this exception at the call site.  If throwing an exception is not acceptable, you
// can implement your own OutputStream with arbitrary error handling and then use writeMessage().

void writeMessageToFd(int fd, ArrayPtr<const ArrayPtr<const word>> segments);
// Write the segment array to the given file descriptor.
//
// This function throws an exception on any I/O error.  If your code is not exception-safe, be sure
// you catch this exception at the call site.  If throwing an exception is not acceptable, you
// can implement your own OutputStream with arbitrary error handling and then use writeMessage().

// =======================================================================================
// inline stuff

inline Array<word> messageToFlatArray(MessageBuilder& builder) {
  return messageToFlatArray(builder.getSegmentsForOutput());
}

inline void writeMessage(OutputStream& output, MessageBuilder& builder) {
  writeMessage(output, builder.getSegmentsForOutput());
}

inline void writeMessageToFd(int fd, MessageBuilder& builder) {
  writeMessageToFd(fd, builder.getSegmentsForOutput());
}

}  // namespace capnproto

#endif  // SERIALIZE_H_
