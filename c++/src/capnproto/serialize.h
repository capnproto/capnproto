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

  virtual bool read(void* buffer, size_t size) = 0;
  // Always reads the full size requested.  Returns true if successful.  May throw an exception
  // on failure, or report the error through some side channel and return false.
};

class InputFile {
public:
  virtual ~InputFile();

  virtual bool read(size_t offset, void* buffer, size_t size) = 0;
  // Always reads the full size requested.  Returns true if successful.  May throw an exception
  // on failure, or report the error through some side channel and return false.
};

class OutputStream {
public:
  virtual ~OutputStream();

  virtual void write(const void* buffer, size_t size) = 0;
  // Throws exception on error, or reports errors via some side channel and returns.
};

enum class InputStrategy {
  EAGER,
  // Read the whole message into RAM upfront, in the MessageReader constructor.  When reading from
  // an InputStream, the stream will then be positioned at the byte immediately after the end of
  // the message, and will not be accessed again.

  LAZY,
  // Read segments of the message into RAM as needed while the message structure is being traversed.
  // When reading from an InputStream, segments must be read in order, so segments up to the
  // required segment will also be read.  No guarantee is made about the position of the InputStream
  // after reading.  When using an InputFile, only the exact segments desired are read.

  EAGER_WAIT_FOR_READ_NEXT,
  // Like EAGER but don't read the first mesasge until readNext() is called the first time.

  LAZY_WAIT_FOR_READ_NEXT,
  // Like LAZY but don't read the first mesasge until readNext() is called the first time.
};

class InputStreamMessageReader: public MessageReader {
public:
  InputStreamMessageReader(InputStream* inputStream,
                           ReaderOptions options = ReaderOptions(),
                           InputStrategy inputStrategy = InputStrategy::EAGER);

  void readNext();
  // Progress to the next message in the input stream, reusing the same memory if possible.
  // Calling this invalidates any Readers currently pointing into this message.

  // implements MessageReader ----------------------------------------
  ArrayPtr<const word> getSegment(uint id) override;

private:
  InputStream* inputStream;
  InputStrategy inputStrategy;
  uint segmentsReadSoFar;

  struct LazySegment {
    uint size;
    Array<word> words;
    // words may be larger than the desired size in the case where space is being reused from a
    // previous read.

    inline LazySegment(): size(0), words(nullptr) {}
  };

  // Optimize for single-segment case.
  LazySegment segment0;
  Array<LazySegment> moreSegments;

  void readNextInternal();
};

class InputFileMessageReader: public MessageReader {
public:
  InputFileMessageReader(InputFile* inputFile,
                         ReaderOptions options = ReaderOptions(),
                         InputStrategy inputStrategy = InputStrategy::EAGER);

  // implements MessageReader ----------------------------------------
  ArrayPtr<const word> getSegment(uint id) override;

private:
  InputFile* inputFile;

  struct LazySegment {
    uint size;
    size_t offset;
    Array<word> words;   // null until actually read

    inline LazySegment(): size(0), offset(0), words(nullptr) {}
  };

  // Optimize for single-segment case.
  LazySegment segment0;
  Array<LazySegment> moreSegments;
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
  FdInputStream(int fd, ErrorReporter* errorReporter = getThrowingErrorReporter())
      : fd(fd), errorReporter(errorReporter) {};
  FdInputStream(AutoCloseFd fd, ErrorReporter* errorReporter = getThrowingErrorReporter())
      : fd(fd), autoclose(move(fd)), errorReporter(errorReporter) {}
  ~FdInputStream();

  bool read(void* buffer, size_t size) override;

private:
  int fd;
  AutoCloseFd autoclose;
  ErrorReporter* errorReporter;
};

class FdInputFile: public InputFile {
  // An InputFile wrapping a file descriptor.  The file descriptor must be seekable.  This
  // implementation uses pread(), so the stream pointer will not be modified.

public:
  FdInputFile(int fd, size_t offset, ErrorReporter* errorReporter = getThrowingErrorReporter())
      : fd(fd), offset(offset), errorReporter(errorReporter) {};
  FdInputFile(AutoCloseFd fd, size_t offset,
              ErrorReporter* errorReporter = getThrowingErrorReporter())
      : fd(fd), autoclose(move(fd)), offset(offset), errorReporter(errorReporter) {}
  ~FdInputFile();

  bool read(size_t offset, void* buffer, size_t size) override;

private:
  int fd;
  AutoCloseFd autoclose;
  size_t offset;
  ErrorReporter* errorReporter;
};

class FdOutputStream: public OutputStream {
  // An OutputStream wrapping a file descriptor.

public:
  FdOutputStream(int fd): fd(fd) {};
  FdOutputStream(AutoCloseFd fd): fd(fd), autoclose(move(fd)) {}
  ~FdOutputStream();

  void write(const void* buffer, size_t size) override;

private:
  int fd;
  AutoCloseFd autoclose;
};

class StreamFdMessageReader: private FdInputStream, public InputStreamMessageReader {
  // A MessageReader that reads from a steam-based file descriptor.  For seekable file descriptors
  // (e.g. actual disk files), FdFileMessageReader is better, but this will still work.

public:
  StreamFdMessageReader(int fd, ReaderOptions options = ReaderOptions(),
                        InputStrategy inputStrategy = InputStrategy::EAGER)
      : FdInputStream(fd), InputStreamMessageReader(this, options, inputStrategy) {}
  // Read message from a file descriptor, without taking ownership of the descriptor.
  //
  // Since this version implies that the caller intends to read more data from the fd later on, the
  // default is to read the entire message eagerly in the constructor, so that the fd will be
  // deterministically positioned just past the end of the message.

  StreamFdMessageReader(AutoCloseFd fd, ReaderOptions options = ReaderOptions(),
                        InputStrategy inputStrategy = InputStrategy::LAZY)
      : FdInputStream(move(fd)), InputStreamMessageReader(this, options, inputStrategy) {}
  // Read a message from a file descriptor, taking ownership of the descriptor.
  //
  // Since this version implies that the caller does not intend to read any more data from the fd,
  // the default is to read the message lazily as needed.

  ~StreamFdMessageReader();
};

class FileFdMessageReader: private FdInputFile, public InputFileMessageReader {
  // A MessageReader that reads from a seekable file descriptor, e.g. disk files.  For non-seekable
  // file descriptors, use FdStreamMessageReader.  This implementation uses pread(), so the file
  // descriptor's stream pointer will not be modified.

public:
  FileFdMessageReader(int fd, size_t offset, ReaderOptions options = ReaderOptions(),
                      InputStrategy inputStrategy = InputStrategy::LAZY)
      : FdInputFile(fd, offset, options.errorReporter),
        InputFileMessageReader(this, options, inputStrategy) {}
  // Read message from a file descriptor, without taking ownership of the descriptor.
  //
  // All reads use pread(), so the file descriptor's stream pointer will not be modified.

  FileFdMessageReader(AutoCloseFd fd, size_t offset, ReaderOptions options = ReaderOptions(),
                      InputStrategy inputStrategy = InputStrategy::LAZY)
      : FdInputFile(move(fd), offset, options.errorReporter),
        InputFileMessageReader(this, options, inputStrategy) {}
  // Read a message from a file descriptor, taking ownership of the descriptor.
  //
  // All reads use pread(), so the file descriptor's stream pointer will not be modified.

  ~FileFdMessageReader();
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
