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

#pragma once

// This header provides a small subset of the POSIX API which also happens to be available on
// Windows under slightly-different names.

#if _WIN32
#include <io.h>
#include <direct.h>
#include <fcntl.h>  // _O_BINARY
#else
#include <errno.h>
#endif

#include <stdint.h>
#include <limits.h>

#if !_WIN32 || __MINGW32__
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#if !_WIN32
#include <sys/uio.h>
#endif

// To get KJ_BEGIN_HEADER/KJ_END_HEADER
#include "common.h"

KJ_BEGIN_HEADER

namespace kj {
namespace miniposix {

#if _WIN32 && !__MINGW32__
// We're on Windows and not MinGW. So, we need to define wrappers for the POSIX API.

typedef int ssize_t;

// Windows I/O supports maximum 4GB at a time so make sure we saturate rather than overflow when
// the caller requests a much larger size.
#define KJ_OS_MAX_IO UINT_MAX
#define KJ_OS_IO_FUNC(func) _ ## func

inline int close(int fd) {
  return ::_close(fd);
}

#ifndef F_OK
#define F_OK 0  // access() existence test
#endif

#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & S_IFMT) ==  S_IFREG)  // stat() regular file test
#endif
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) ==  S_IFDIR)  // stat() directory test
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#else
// We're on a POSIX system or MinGW which already defines the wrappers for us.

using ::ssize_t;

#if __APPLE__
// Workaround for FB8934446 - even though the APIs take size_t arguments they're (incorrectly)
// internally limited to INT_MAX.
#define KJ_OS_MAX_IO INT_MAX
#elif INTPTR_MAX >= INT64_MAX
// Normal POSIX platforms are limited to SSIZE_MAX. However on 64-bit platforms there's no point
// saturating any input I/O lengths - that would be 9 exabytes & insane to do in 1 I/O call.
// Using SIZE_MAX ensures that the kj::min evaluation should be elided and the read/write/writev
// calls should inlined directly to the libc function.
#define KJ_OS_MAX_IO SIZE_MAX
#else
// On 32-bit platforms make sure that trying to do large I/O still works correctly (saturating &
// returning the length that was processed rather than returning a low-level error).
#define KJ_OS_MAX_IO SSIZE_MAX
#endif
#define KJ_OS_IO_FUNC(func) func

using ::close;

#endif

#if _WIN32
// We're on Windows, including MinGW. pipe() and mkdir() are non-standard even on MinGW.

inline int pipe(int fds[2]) {
  return ::_pipe(fds, 8192, _O_BINARY | _O_NOINHERIT);
}
inline int mkdir(const char* path, int mode) {
  return ::_mkdir(path);
}

#else
// We're on real POSIX.

using ::pipe;
using ::mkdir;


// Apparently, there is a maximum number of iovecs allowed per call.  I don't understand why.
// Most platforms define IOV_MAX but Linux defines only UIO_MAXIOV and others, like Hurd,
// define neither.
//
// On platforms where both IOV_MAX and UIO_MAXIOV are undefined, we poke sysconf(_SC_IOV_MAX),
// then try to fall back to the POSIX-mandated minimum of _XOPEN_IOV_MAX if that fails.
//
// http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/limits.h.html#tag_13_23_03_01
#if defined(IOV_MAX)
// Solaris, MacOS (& all other BSD-variants?) (and others?)
static constexpr inline size_t iovMax() {
  return IOV_MAX;
}
#elif defined(UIO_MAX_IOV)
// Linux
static constexpr inline size_t iovMax() {
  return UIO_MAX_IOV;
}
#else
#error "Please determine the appropriate constant for IOV_MAX on your system."
#endif

#if __APPLE__
// Unlike stock libc writev we're forced to take the iov non-const because the internal details of
// this POSIX-emulation require us to potentially modify the iovec when writes > 2GB occur.
ssize_t writev(int fd, struct iovec* iov, size_t iovcnt);
#else
using ::writev;
#endif
#endif

static inline ssize_t read(int fd, void* buffer, size_t size) {
  return ::KJ_OS_IO_FUNC(read)(fd, buffer, static_cast<unsigned int>(kj::min(size_t{KJ_OS_MAX_IO}, size)));
}

static inline ssize_t write(int fd, const void* buffer, size_t size) {
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(suppress : 4996)
#endif
  return ::KJ_OS_IO_FUNC(write)(fd, buffer, static_cast<unsigned int>(kj::min(size_t{KJ_OS_MAX_IO}, size)));
}
}  // namespace miniposix
}  // namespace kj

KJ_END_HEADER
