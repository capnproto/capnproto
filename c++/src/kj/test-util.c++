// Copyright (c) 2020-2021 Cap'n Proto Contributors
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

#include "debug.h"
#include "test-util.h"
#include <stdlib.h>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <io.h>
#endif

#if _WIN32 || __ANDROID__
// TODO(cleanup): Find the Windows temp directory? Seems overly difficult pre C++17.
// https://en.cppreference.com/w/cpp/filesystem/temp_directory_path
// would require refactoring the mkstemp code a bit to take into account the types
// needed to do this at runtime.
#define KJ_TMPDIR_PREFIX ""
#else
#define KJ_TMPDIR_PREFIX "/tmp/"
#endif

namespace kj {
namespace test {
AutoCloseFd mkstempAutoErased() {
  char tpl[] = KJ_TMPDIR_PREFIX "kj-testfile-XXXXXX.tmp"

#if _WIN32
  char* end = tpl + strlen(tpl);
  while (end > tpl && *(end-1) == 'X') --end;

  for (;;) {
    KJ_ASSERT(_mktemp(tpl) == tpl);

    int fd = open(tpl, O_RDWR | O_CREAT | O_EXCL | O_TEMPORARY | O_BINARY | O_SHORT_LIVED | O_NOINHERET, 0700);
    if (fd >= 0) {
      return fd;
    }

    int error = errno;
    if (error != EEXIST && error != EINTR) {
      KJ_FAIL_SYSCALL("open(mktemp())", error, tpl);
    }

    memset(end, 'X', strlen(end));
  }
#else
  int fd;
  KJ_SYSCALL(fd = ::mkstemp(tpl), tpl);
  KJ_SYSCALL(::unlink(tpl), tpl);
  return AutoCloseFd{fd};
#endif
}
}  // namespace test
}  // namespace kj
