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

#include "miniposix.h"

namespace kj {
namespace miniposix {
#if !defined(_WIN32) && !defined(IOV_MAX) && !defined(UIO_MAX_IOV)
size_t iovMax() {
  // Thread-safe & lazily initialized only on first use.
  static const size_t KJ_IOV_MAX = [] () -> size_t {
    long iovmax;

    errno = 0;
    iovmax = sysconf(_SC_IOV_MAX);
    if (iovmax == -1) {
      if (errno == 0) {
        // The -1 return value was the actual value, not an error. This means there's no limit.
        return kj::maxValue;
      } else {
        return _XOPEN_IOV_MAX;
      }
    }

    return (size_t) iovmax;
  }();
  return KJ_IOV_MAX;
}
#endif
}  // namespace miniposix
}  // namespace kj
