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

#ifndef KJ_IO_H_
#define KJ_IO_H_

#include <stddef.h>
#include "common.h"
#include "array.h"
#include "exception.h"

namespace kj {

// =======================================================================================
// Abstract interfaces

class InputStream {
public:
  virtual ~InputStream() noexcept(false);

  size_t read(void* buffer, size_t minBytes, size_t maxBytes);
  // Reads at least minBytes and at most maxBytes, copying them into the given buffer.  Returns
  // the size read.  Throws an exception on errors.  Implemented in terms of tryRead().
  //
  // maxBytes is the number of bytes the caller really wants, but minBytes is the minimum amount
  // needed by the caller before it can start doing useful processing.  If the stream returns less
  // than maxBytes, the caller will usually call read() again later to get the rest.  Returning
  // less than maxBytes is useful when it makes sense for the caller to parallelize processing
  // with I/O.
  //
  // Never blocks if minBytes is zero.  If minBytes is zero and maxBytes is non-zero, this may
  // attempt a non-blocking read or may just return zero.  To force a read, use a non-zero minBytes.
  // To detect EOF without throwing an exception, use tryRead().
  //
  // Cap'n Proto never asks for more bytes than it knows are part of the message.  Therefore, if
  // the InputStream happens to know that the stream will never reach maxBytes -- even if it has
  // reached minBytes -- it should throw an exception to avoid wasting time processing an incomplete
  // message.  If it can't even reach minBytes, it MUST throw an exception, as the caller is not
  // expected to understand how to deal with partial reads.

  virtual size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) = 0;
  // Like read(), but may return fewer than minBytes on EOF.

  inline void read(void* buffer, size_t bytes) { read(buffer, bytes, bytes); }
  // Convenience method for reading an exact number of bytes.

  virtual void skip(size_t bytes);
  // Skips past the given number of bytes, discarding them.  The default implementation read()s
  // into a scratch buffer.
};

class OutputStream {
public:
  virtual ~OutputStream() noexcept(false);

  virtual void write(const void* buffer, size_t size) = 0;
  // Always writes the full size.  Throws exception on error.

  virtual void write(ArrayPtr<const ArrayPtr<const byte>> pieces);
  // Equivalent to write()ing each byte array in sequence, which is what the default implementation
  // does.  Override if you can do something better, e.g. use writev() to do the write in a single
  // syscall.
};

class BufferedInputStream: public InputStream {
  // An input stream which buffers some bytes in memory to reduce system call overhead.
  // - OR -
  // An input stream that actually reads from some in-memory data structure and wants to give its
  // caller a direct pointer to that memory to potentially avoid a copy.

public:
  virtual ~BufferedInputStream() noexcept(false);

  ArrayPtr<const byte> getReadBuffer();
  // Get a direct pointer into the read buffer, which contains the next bytes in the input.  If the
  // caller consumes any bytes, it should then call skip() to indicate this.  This always returns a
  // non-empty buffer or throws an exception.  Implemented in terms of tryGetReadBuffer().

  virtual ArrayPtr<const byte> tryGetReadBuffer() = 0;
  // Like getReadBuffer() but may return an empty buffer on EOF.
};

class BufferedOutputStream: public OutputStream {
  // An output stream which buffers some bytes in memory to reduce system call overhead.
  // - OR -
  // An output stream that actually writes into some in-memory data structure and wants to give its
  // caller a direct pointer to that memory to potentially avoid a copy.

public:
  virtual ~BufferedOutputStream() noexcept(false);

  virtual ArrayPtr<byte> getWriteBuffer() = 0;
  // Get a direct pointer into the write buffer.  The caller may choose to fill in some prefix of
  // this buffer and then pass it to write(), in which case write() may avoid a copy.  It is
  // incorrect to pass to write any slice of this buffer which is not a prefix.
};

// =======================================================================================
// Buffered streams implemented as wrappers around regular streams

class BufferedInputStreamWrapper: public BufferedInputStream {
  // Implements BufferedInputStream in terms of an InputStream.
  //
  // Note that the underlying stream's position is unpredictable once the wrapper is destroyed,
  // unless the entire stream was consumed.  To read a predictable number of bytes in a buffered
  // way without going over, you'd need this wrapper to wrap some other wrapper which itself
  // implements an artificial EOF at the desired point.  Such a stream should be trivial to write
  // but is not provided by the library at this time.

public:
  explicit BufferedInputStreamWrapper(InputStream& inner, ArrayPtr<byte> buffer = nullptr);
  // Creates a buffered stream wrapping the given non-buffered stream.  No guarantee is made about
  // the position of the inner stream after a buffered wrapper has been created unless the entire
  // input is read.
  //
  // If the second parameter is non-null, the stream uses the given buffer instead of allocating
  // its own.  This may improve performance if the buffer can be reused.

  KJ_DISALLOW_COPY(BufferedInputStreamWrapper);
  ~BufferedInputStreamWrapper() noexcept(false);

  // implements BufferedInputStream ----------------------------------
  ArrayPtr<const byte> tryGetReadBuffer() override;
  size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) override;
  void skip(size_t bytes) override;

private:
  InputStream& inner;
  Array<byte> ownedBuffer;
  ArrayPtr<byte> buffer;
  ArrayPtr<byte> bufferAvailable;
};

class BufferedOutputStreamWrapper: public BufferedOutputStream {
  // Implements BufferedOutputStream in terms of an OutputStream.  Note that writes to the
  // underlying stream may be delayed until flush() is called or the wrapper is destroyed.

public:
  explicit BufferedOutputStreamWrapper(OutputStream& inner, ArrayPtr<byte> buffer = nullptr);
  // Creates a buffered stream wrapping the given non-buffered stream.
  //
  // If the second parameter is non-null, the stream uses the given buffer instead of allocating
  // its own.  This may improve performance if the buffer can be reused.

  KJ_DISALLOW_COPY(BufferedOutputStreamWrapper);
  ~BufferedOutputStreamWrapper() noexcept(false);

  void flush();
  // Force the wrapper to write any remaining bytes in its buffer to the inner stream.  Note that
  // this only flushes this object's buffer; this object has no idea how to flush any other buffers
  // that may be present in the underlying stream.

  // implements BufferedOutputStream ---------------------------------
  ArrayPtr<byte> getWriteBuffer() override;
  void write(const void* buffer, size_t size) override;

private:
  OutputStream& inner;
  Array<byte> ownedBuffer;
  ArrayPtr<byte> buffer;
  byte* bufferPos;
  UnwindDetector unwindDetector;
};

// =======================================================================================
// Array I/O

class ArrayInputStream: public BufferedInputStream {
public:
  explicit ArrayInputStream(ArrayPtr<const byte> array);
  KJ_DISALLOW_COPY(ArrayInputStream);
  ~ArrayInputStream() noexcept(false);

  // implements BufferedInputStream ----------------------------------
  ArrayPtr<const byte> tryGetReadBuffer() override;
  size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) override;
  void skip(size_t bytes) override;

private:
  ArrayPtr<const byte> array;
};

class ArrayOutputStream: public BufferedOutputStream {
public:
  explicit ArrayOutputStream(ArrayPtr<byte> array);
  KJ_DISALLOW_COPY(ArrayOutputStream);
  ~ArrayOutputStream() noexcept(false);

  ArrayPtr<byte> getArray() {
    // Get the portion of the array which has been filled in.
    return arrayPtr(array.begin(), fillPos);
  }

  // implements BufferedInputStream ----------------------------------
  ArrayPtr<byte> getWriteBuffer() override;
  void write(const void* buffer, size_t size) override;

private:
  ArrayPtr<byte> array;
  byte* fillPos;
};

// =======================================================================================
// File descriptor I/O

class AutoCloseFd {
  // A wrapper around a file descriptor which automatically closes the descriptor when destroyed.
  // The wrapper supports move construction for transferring ownership of the descriptor.  If
  // close() returns an error, the destructor throws an exception, UNLESS the destructor is being
  // called during unwind from another exception, in which case the close error is ignored.
  //
  // If your code is not exception-safe, you should not use AutoCloseFd.  In this case you will
  // have to call close() yourself and handle errors appropriately.

public:
  inline AutoCloseFd(): fd(-1) {}
  inline AutoCloseFd(decltype(nullptr)): fd(-1) {}
  inline explicit AutoCloseFd(int fd): fd(fd) {}
  inline AutoCloseFd(AutoCloseFd&& other): fd(other.fd) { other.fd = -1; }
  KJ_DISALLOW_COPY(AutoCloseFd);
  ~AutoCloseFd() noexcept(false);

  inline operator int() { return fd; }
  inline int get() { return fd; }

  inline bool operator==(decltype(nullptr)) { return fd < 0; }
  inline bool operator!=(decltype(nullptr)) { return fd >= 0; }

private:
  int fd;
  UnwindDetector unwindDetector;
};

class FdInputStream: public InputStream {
  // An InputStream wrapping a file descriptor.

public:
  explicit FdInputStream(int fd): fd(fd) {}
  explicit FdInputStream(AutoCloseFd fd): fd(fd), autoclose(mv(fd)) {}
  KJ_DISALLOW_COPY(FdInputStream);
  ~FdInputStream() noexcept(false);

  size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) override;

private:
  int fd;
  AutoCloseFd autoclose;
};

class FdOutputStream: public OutputStream {
  // An OutputStream wrapping a file descriptor.

public:
  explicit FdOutputStream(int fd): fd(fd) {}
  explicit FdOutputStream(AutoCloseFd fd): fd(fd), autoclose(mv(fd)) {}
  KJ_DISALLOW_COPY(FdOutputStream);
  ~FdOutputStream() noexcept(false);

  void write(const void* buffer, size_t size) override;
  void write(ArrayPtr<const ArrayPtr<const byte>> pieces) override;

private:
  int fd;
  AutoCloseFd autoclose;
};

}  // namespace kj

#endif  // KJ_IO_H_
