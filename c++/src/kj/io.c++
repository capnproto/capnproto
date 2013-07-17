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

#include "io.h"
#include "debug.h"
#include <unistd.h>
#include <sys/uio.h>
#include <algorithm>
#include <errno.h>

namespace kj {

InputStream::~InputStream() noexcept(false) {}
OutputStream::~OutputStream() noexcept(false) {}
BufferedInputStream::~BufferedInputStream() noexcept(false) {}
BufferedOutputStream::~BufferedOutputStream() noexcept(false) {}

void InputStream::skip(size_t bytes) {
  char scratch[8192];
  while (bytes > 0) {
    size_t amount = std::min(bytes, sizeof(scratch));
    read(scratch, amount);
    bytes -= amount;
  }
}

void OutputStream::write(ArrayPtr<const ArrayPtr<const byte>> pieces) {
  for (auto piece: pieces) {
    write(piece.begin(), piece.size());
  }
}

// =======================================================================================

BufferedInputStreamWrapper::BufferedInputStreamWrapper(InputStream& inner, ArrayPtr<byte> buffer)
    : inner(inner), ownedBuffer(buffer == nullptr ? heapArray<byte>(8192) : nullptr),
      buffer(buffer == nullptr ? ownedBuffer : buffer) {}

BufferedInputStreamWrapper::~BufferedInputStreamWrapper() noexcept(false) {}

ArrayPtr<const byte> BufferedInputStreamWrapper::getReadBuffer() {
  if (bufferAvailable.size() == 0) {
    size_t n = inner.read(buffer.begin(), 1, buffer.size());
    bufferAvailable = buffer.slice(0, n);
  }

  return bufferAvailable;
}

size_t BufferedInputStreamWrapper::read(void* dst, size_t minBytes, size_t maxBytes) {
  if (minBytes <= bufferAvailable.size()) {
    // Serve from current buffer.
    size_t n = std::min(bufferAvailable.size(), maxBytes);
    memcpy(dst, bufferAvailable.begin(), n);
    bufferAvailable = bufferAvailable.slice(n, bufferAvailable.size());
    return n;
  } else {
    // Copy current available into destination.
    memcpy(dst, bufferAvailable.begin(), bufferAvailable.size());
    size_t fromFirstBuffer = bufferAvailable.size();

    dst = reinterpret_cast<byte*>(dst) + fromFirstBuffer;
    minBytes -= fromFirstBuffer;
    maxBytes -= fromFirstBuffer;

    if (maxBytes <= buffer.size()) {
      // Read the next buffer-full.
      size_t n = inner.read(buffer.begin(), minBytes, buffer.size());
      size_t fromSecondBuffer = std::min(n, maxBytes);
      memcpy(dst, buffer.begin(), fromSecondBuffer);
      bufferAvailable = buffer.slice(fromSecondBuffer, n);
      return fromFirstBuffer + fromSecondBuffer;
    } else {
      // Forward large read to the underlying stream.
      bufferAvailable = nullptr;
      return fromFirstBuffer + inner.read(dst, minBytes, maxBytes);
    }
  }
}

void BufferedInputStreamWrapper::skip(size_t bytes) {
  if (bytes <= bufferAvailable.size()) {
    bufferAvailable = bufferAvailable.slice(bytes, bufferAvailable.size());
  } else {
    bytes -= bufferAvailable.size();
    if (bytes <= buffer.size()) {
      // Read the next buffer-full.
      size_t n = inner.read(buffer.begin(), bytes, buffer.size());
      bufferAvailable = buffer.slice(bytes, n);
    } else {
      // Forward large skip to the underlying stream.
      bufferAvailable = nullptr;
      inner.skip(bytes - bufferAvailable.size());
    }
  }
}

// -------------------------------------------------------------------

BufferedOutputStreamWrapper::BufferedOutputStreamWrapper(OutputStream& inner, ArrayPtr<byte> buffer)
    : inner(inner),
      ownedBuffer(buffer == nullptr ? heapArray<byte>(8192) : nullptr),
      buffer(buffer == nullptr ? ownedBuffer : buffer),
      bufferPos(this->buffer.begin()) {}

BufferedOutputStreamWrapper::~BufferedOutputStreamWrapper() noexcept(false) {
  unwindDetector.catchExceptionsIfUnwinding([&]() {
    flush();
  });
}

void BufferedOutputStreamWrapper::flush() {
  if (bufferPos > buffer.begin()) {
    inner.write(buffer.begin(), bufferPos - buffer.begin());
    bufferPos = buffer.begin();
  }
}

ArrayPtr<byte> BufferedOutputStreamWrapper::getWriteBuffer() {
  return arrayPtr(bufferPos, buffer.end());
}

void BufferedOutputStreamWrapper::write(const void* src, size_t size) {
  if (src == bufferPos) {
    // Oh goody, the caller wrote directly into our buffer.
    bufferPos += size;
  } else {
    size_t available = buffer.end() - bufferPos;

    if (size <= available) {
      memcpy(bufferPos, src, size);
      bufferPos += size;
    } else if (size <= buffer.size()) {
      // Too much for this buffer, but not a full buffer's worth, so we'll go ahead and copy.
      memcpy(bufferPos, src, available);
      inner.write(buffer.begin(), buffer.size());

      size -= available;
      src = reinterpret_cast<const byte*>(src) + available;

      memcpy(buffer.begin(), src, size);
      bufferPos = buffer.begin() + size;
    } else {
      // Writing so much data that we might as well write directly to avoid a copy.
      inner.write(buffer.begin(), bufferPos - buffer.begin());
      bufferPos = buffer.begin();
      inner.write(src, size);
    }
  }
}

// =======================================================================================

ArrayInputStream::ArrayInputStream(ArrayPtr<const byte> array): array(array) {}
ArrayInputStream::~ArrayInputStream() noexcept(false) {}

ArrayPtr<const byte> ArrayInputStream::getReadBuffer() {
  return array;
}

size_t ArrayInputStream::read(void* dst, size_t minBytes, size_t maxBytes) {
  size_t n = std::min(maxBytes, array.size());
  size_t result = n;
  KJ_REQUIRE(n >= minBytes, "ArrayInputStream ended prematurely.") {
    result = minBytes;  // garbage
    break;
  }
  memcpy(dst, array.begin(), n);
  array = array.slice(n, array.size());
  return result;
}

void ArrayInputStream::skip(size_t bytes) {
  KJ_REQUIRE(array.size() >= bytes, "ArrayInputStream ended prematurely.") {
    bytes = array.size();
    break;
  }
  array = array.slice(bytes, array.size());
}

// -------------------------------------------------------------------

ArrayOutputStream::ArrayOutputStream(ArrayPtr<byte> array): array(array), fillPos(array.begin()) {}
ArrayOutputStream::~ArrayOutputStream() noexcept(false) {}

ArrayPtr<byte> ArrayOutputStream::getWriteBuffer() {
  return arrayPtr(fillPos, array.end());
}

void ArrayOutputStream::write(const void* src, size_t size) {
  if (src == fillPos) {
    // Oh goody, the caller wrote directly into our buffer.
    fillPos += size;
  } else {
    KJ_REQUIRE(size <= (size_t)(array.end() - fillPos),
            "ArrayOutputStream's backing array was not large enough for the data written.");
    memcpy(fillPos, src, size);
    fillPos += size;
  }
}

// =======================================================================================

AutoCloseFd::~AutoCloseFd() noexcept(false) {
  unwindDetector.catchExceptionsIfUnwinding([&]() {
    // Don't use SYSCALL() here because close() should not be repeated on EINTR.
    if (fd >= 0 && close(fd) < 0) {
      KJ_FAIL_SYSCALL("close", errno, fd) {
        break;
      }
    }
  });
}

FdInputStream::~FdInputStream() noexcept(false) {}

size_t FdInputStream::read(void* buffer, size_t minBytes, size_t maxBytes) {
  byte* pos = reinterpret_cast<byte*>(buffer);
  byte* min = pos + minBytes;
  byte* max = pos + maxBytes;

  while (pos < min) {
    ssize_t n;
    KJ_SYSCALL(n = ::read(fd, pos, max - pos), fd);
    KJ_REQUIRE(n > 0, "Premature EOF") {
      return minBytes;
    }
    pos += n;
  }

  return pos - reinterpret_cast<byte*>(buffer);
}

FdOutputStream::~FdOutputStream() noexcept(false) {}

void FdOutputStream::write(const void* buffer, size_t size) {
  const char* pos = reinterpret_cast<const char*>(buffer);

  while (size > 0) {
    ssize_t n;
    KJ_SYSCALL(n = ::write(fd, pos, size), fd);
    KJ_ASSERT(n > 0, "write() returned zero.");
    pos += n;
    size -= n;
  }
}

void FdOutputStream::write(ArrayPtr<const ArrayPtr<const byte>> pieces) {
  KJ_STACK_ARRAY(struct iovec, iov, pieces.size(), 16, 128);

  for (uint i = 0; i < pieces.size(); i++) {
    // writev() interface is not const-correct.  :(
    iov[i].iov_base = const_cast<byte*>(pieces[i].begin());
    iov[i].iov_len = pieces[i].size();
  }

  struct iovec* current = iov.begin();

  // Make sure we don't do anything on an empty write.
  while (current < iov.end() && current->iov_len == 0) {
    ++current;
  }

  while (current < iov.end()) {
    ssize_t n;
    KJ_SYSCALL(n = ::writev(fd, current, iov.end() - current), fd);
    KJ_ASSERT(n > 0, "writev() returned zero.");

    while (static_cast<size_t>(n) >= current->iov_len) {
      n -= current->iov_len;
      ++current;
    }

    if (n > 0) {
      current->iov_base = reinterpret_cast<byte*>(current->iov_base) + n;
      current->iov_len -= n;
    }
  }
}

}  // namespace kj
