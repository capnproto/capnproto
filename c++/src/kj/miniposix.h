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

#ifndef KJ_PORTABLE_FD_H_
#define KJ_PORTABLE_FD_H_

// This header provides a small subset of the POSIX API which also happens to be available on
// Windows under slightly-different names.

#if defined(__GNUC__) && !KJ_HEADER_WARNINGS
#pragma GCC system_header
#endif

#if _WIN32
#include <io.h>
#include <direct.h>
#endif

#if !_WIN32 || __MINGW32__
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace kj {
namespace miniposix {

#if _WIN32 && !__MINGW32__
// We're on Windows and not MinGW. So, we need to define wrappers for the POSIX API.

typedef int ssize_t;

inline ssize_t read(int fd, void* buffer, size_t size) {
  return ::_read(fd, buffer, size);
}
inline ssize_t write(int fd, const void* buffer, size_t size) {
  return ::_write(fd, buffer, size);
}
inline int close(int fd) {
  return ::_close(fd);
}

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
using ::read;
using ::write;
using ::close;

#endif

#if _WIN32
// We're on Windows, including MinGW. pipe() and mkdir() are non-standard even on MinGW.

inline int pipe(int fds[2]) {
  return ::_pipe(fds, 4096, false);
}
inline int mkdir(const char* path, int mode) {
  return ::_mkdir(path);
}

#else
// We're on real POSIX.

using ::pipe;
using ::mkdir;

#endif

}  // namespace miniposix
}  // namespace kj

#endif  // KJ_WIN32_FD_H_
