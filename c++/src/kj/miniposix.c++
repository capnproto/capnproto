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
#if __APPLE__
ssize_t writev(int fd, struct iovec* iov, size_t iovcnt) {
  // Workaround for writev POSIX bug on Apple (filed as FB8934446) that prevents writes > 2GB.
  // The process for doing this is:
  //   1. We find the iov entry that causes us to exceed a 2GB write.
  //   2. We truncate that entry to only have a length that would get us to 2GB
  //   3. We do the actual write.
  //   4. We restore the original length in the modified iovec entry before returning the result.
  // This is a bit suboptimal for very large vectored I/O. This could be optimized at the cost of
  // complicating the I/O on all other platforms + adding a bit of overhead (or providing a
  // dedicated pessimizatioin path for MacO) in kj/io.c++, but this seems like enough of a corner
  // case within kj/cap'n'proto for now that the simpler approach is more warranted. It seems
  // unlikely to me that relative cost of doing large I/O doesn't absolutely dominate the CPU cost
  // of finding this boundary.
  ssize_t total = 0;
  size_t originalTruncatedEntryLen;
  size_t numEntriesToWrite;

  for (numEntriesToWrite = 0; numEntriesToWrite < iovcnt; ++numEntriesToWrite) {
    // Step 1: Find the iov entry that would cause us to exceed a 2GB write.
    total += iov[numEntriesToWrite].iov_len;

    if (total > INT_MAX) {
      // Step 2: Truncate the entry so that we only consume the part of it that gets us up to 2GB.
       originalTruncatedEntryLen = iov[numEntriesToWrite].iov_len;
       iov[numEntriesToWrite].iov_len = INT_MAX - (total - originalTruncatedEntryLen);

       // Increment 1 final time since the break will skip the increment in the for loop but we
       // still intend for this entry to be written (e.g. it might be the first one).
       ++numEntriesToWrite;
       break;
    }
  }

  // Step 3: Do the write.
  ssize_t result = ::writev(fd, iov, numEntriesToWrite);

  // Step 4: Restore the original length of the iovec so that the caller 
  if (numEntriesToWrite != iovcnt) {
    iov[numEntriesToWrite - 1].iov_len = originalTruncatedEntryLen;
  }
  return result;
}
#endif
}  // namespace miniposix
}  // namespace kj
