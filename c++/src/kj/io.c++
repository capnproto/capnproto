// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "io.h"
#include "debug.h"
#include "miniposix.h"
#include <algorithm>
#include <errno.h>

#if _WIN32
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "windows-sanity.h"
#else
#include <sys/uio.h>
#endif

namespace kj {

InputStream::~InputStream() noexcept(false) {}
OutputStream::~OutputStream() noexcept(false) {}
BufferedInputStream::~BufferedInputStream() noexcept(false) {}
BufferedOutputStream::~BufferedOutputStream() noexcept(false) {}

size_t InputStream::read(void* buffer, size_t minBytes, size_t maxBytes) {
  size_t n = tryRead(buffer, minBytes, maxBytes);
  KJ_REQUIRE(n >= minBytes, "Premature EOF") {
    // Pretend we read zeros from the input.
    memset(reinterpret_cast<byte*>(buffer) + n, 0, minBytes - n);
    return minBytes;
  }
  return n;
}

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

ArrayPtr<const byte> BufferedInputStream::getReadBuffer() {
  auto result = tryGetReadBuffer();
  KJ_REQUIRE(result.size() > 0, "Premature EOF");
  return result;
}

// =======================================================================================

BufferedInputStreamWrapper::BufferedInputStreamWrapper(InputStream& inner, ArrayPtr<byte> buffer)
    : inner(inner), ownedBuffer(buffer == nullptr ? heapArray<byte>(8192) : nullptr),
      buffer(buffer == nullptr ? ownedBuffer : buffer) {}

BufferedInputStreamWrapper::~BufferedInputStreamWrapper() noexcept(false) {}

ArrayPtr<const byte> BufferedInputStreamWrapper::tryGetReadBuffer() {
  if (bufferAvailable.size() == 0) {
    size_t n = inner.tryRead(buffer.begin(), 1, buffer.size());
    bufferAvailable = buffer.slice(0, n);
  }

  return bufferAvailable;
}

size_t BufferedInputStreamWrapper::tryRead(void* dst, size_t minBytes, size_t maxBytes) {
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
      inner.skip(bytes);
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

ArrayPtr<const byte> ArrayInputStream::tryGetReadBuffer() {
  return array;
}

size_t ArrayInputStream::tryRead(void* dst, size_t minBytes, size_t maxBytes) {
  size_t n = std::min(maxBytes, array.size());
  memcpy(dst, array.begin(), n);
  array = array.slice(n, array.size());
  return n;
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
    KJ_REQUIRE(size <= array.end() - fillPos);
    fillPos += size;
  } else {
    KJ_REQUIRE(size <= (size_t)(array.end() - fillPos),
            "ArrayOutputStream's backing array was not large enough for the data written.");
    memcpy(fillPos, src, size);
    fillPos += size;
  }
}

// -------------------------------------------------------------------

VectorOutputStream::VectorOutputStream(size_t initialCapacity)
    : vector(heapArray<byte>(initialCapacity)), fillPos(vector.begin()) {}
VectorOutputStream::~VectorOutputStream() noexcept(false) {}

ArrayPtr<byte> VectorOutputStream::getWriteBuffer() {
  // Grow if needed.
  if (fillPos == vector.end()) {
    grow(vector.size() + 1);
  }

  return arrayPtr(fillPos, vector.end());
}

void VectorOutputStream::write(const void* src, size_t size) {
  if (src == fillPos) {
    // Oh goody, the caller wrote directly into our buffer.
    KJ_REQUIRE(size <= vector.end() - fillPos);
    fillPos += size;
  } else {
    if (vector.end() - fillPos < size) {
      grow(fillPos - vector.begin() + size);
    }

    memcpy(fillPos, src, size);
    fillPos += size;
  }
}

void VectorOutputStream::grow(size_t minSize) {
  size_t newSize = vector.size() * 2;
  while (newSize < minSize) newSize *= 2;
  auto newVector = heapArray<byte>(newSize);
  memcpy(newVector.begin(), vector.begin(), fillPos - vector.begin());
  fillPos = fillPos - vector.begin() + newVector.begin();
  vector = kj::mv(newVector);
}

// =======================================================================================

AutoCloseFd::~AutoCloseFd() noexcept(false) {
  if (fd >= 0) {
    unwindDetector.catchExceptionsIfUnwinding([&]() {
      // Don't use SYSCALL() here because close() should not be repeated on EINTR.
      if (miniposix::close(fd) < 0) {
        KJ_FAIL_SYSCALL("close", errno, fd) {
          break;
        }
      }
    });
  }
}

FdInputStream::~FdInputStream() noexcept(false) {}

size_t FdInputStream::tryRead(void* buffer, size_t minBytes, size_t maxBytes) {
  byte* pos = reinterpret_cast<byte*>(buffer);
  byte* min = pos + minBytes;
  byte* max = pos + maxBytes;

  while (pos < min) {
    miniposix::ssize_t n;
    KJ_SYSCALL(n = miniposix::read(fd, pos, max - pos), fd);
    if (n == 0) {
      break;
    }
    pos += n;
  }

  return pos - reinterpret_cast<byte*>(buffer);
}

FdOutputStream::~FdOutputStream() noexcept(false) {}

void FdOutputStream::write(const void* buffer, size_t size) {
  const char* pos = reinterpret_cast<const char*>(buffer);

  while (size > 0) {
    miniposix::ssize_t n;
    KJ_SYSCALL(n = miniposix::write(fd, pos, size), fd);
    KJ_ASSERT(n > 0, "write() returned zero.");
    pos += n;
    size -= n;
  }
}

void FdOutputStream::write(ArrayPtr<const ArrayPtr<const byte>> pieces) {
#if _WIN32
  // Windows has no reasonable writev(). It has WriteFileGather, but this call has the unreasonable
  // restriction that each segment must be page-aligned. So, fall back to the default implementation

  OutputStream::write(pieces);

#else
  const size_t iovmax = miniposix::iovMax(pieces.size());
  while (pieces.size() > iovmax) {
    write(pieces.slice(0, iovmax));
    pieces = pieces.slice(iovmax, pieces.size());
  }

  KJ_STACK_ARRAY(struct iovec, iov, pieces.size(), 16, 128);

  for (uint i = 0; i < pieces.size(); i++) {
    // writev() interface is not const-correct.  :(
    iov[i].iov_base = const_cast<byte*>(pieces[i].begin());
    iov[i].iov_len = pieces[i].size();
  }

  struct iovec* current = iov.begin();

  // Advance past any leading empty buffers so that a write full of only empty buffers does not
  // cause a syscall at all.
  while (current < iov.end() && current->iov_len == 0) {
    ++current;
  }

  while (current < iov.end()) {
    // Issue the write.
    ssize_t n = 0;
    KJ_SYSCALL(n = ::writev(fd, current, iov.end() - current), fd);
    KJ_ASSERT(n > 0, "writev() returned zero.");

    // Advance past all buffers that were fully-written.
    while (current < iov.end() && static_cast<size_t>(n) >= current->iov_len) {
      n -= current->iov_len;
      ++current;
    }

    // If we only partially-wrote one of the buffers, adjust the pointer and size to include only
    // the unwritten part.
    if (n > 0) {
      current->iov_base = reinterpret_cast<byte*>(current->iov_base) + n;
      current->iov_len -= n;
    }
  }
#endif
}

// =======================================================================================

#if _WIN32

AutoCloseHandle::~AutoCloseHandle() noexcept(false) {
  if (handle != (void*)-1) {
    KJ_WIN32(CloseHandle(handle));
  }
}

HandleInputStream::~HandleInputStream() noexcept(false) {}

size_t HandleInputStream::tryRead(void* buffer, size_t minBytes, size_t maxBytes) {
  byte* pos = reinterpret_cast<byte*>(buffer);
  byte* min = pos + minBytes;
  byte* max = pos + maxBytes;

  while (pos < min) {
    DWORD n;
    KJ_WIN32(ReadFile(handle, pos, kj::min(max - pos, DWORD(kj::maxValue)), &n, nullptr));
    if (n == 0) {
      break;
    }
    pos += n;
  }

  return pos - reinterpret_cast<byte*>(buffer);
}

HandleOutputStream::~HandleOutputStream() noexcept(false) {}

void HandleOutputStream::write(const void* buffer, size_t size) {
  const char* pos = reinterpret_cast<const char*>(buffer);

  while (size > 0) {
    DWORD n;
    KJ_WIN32(WriteFile(handle, pos, kj::min(size, DWORD(kj::maxValue)), &n, nullptr));
    KJ_ASSERT(n > 0, "write() returned zero.");
    pos += n;
    size -= n;
  }
}

#endif  // _WIN32

}  // namespace kj
