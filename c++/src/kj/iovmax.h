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

#ifndef KJ_IOVMAX_H_
#define KJ_IOVMAX_H_

#if !_WIN32
#include <limits.h>
#include <errno.h>

// Apparently, there is a maximum number of iovecs allowed per call.  I don't understand why.
// Most platforms define IOV_MAX but Linux defines only UIO_MAXIOV and others, like Hurd,
// define neither.
//
// On platforms where both IOV_MAX and UIO_MAXIOV are undefined, we poke sysconf(_SC_IOV_MAX),
// try to fall back to the POSIX-mandated minimum of _XOPEN_IOV_MAX if that fails.
//
// http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/limits.h.html#tag_13_23_03_01
inline size_t iovMax(size_t count)
{
#if defined(IOV_MAX)
    // Solaris (and others?)
    return IOV_MAX;
#elif defined(UIO_MAXIOV)
    // Linux
    return UIO_MAXIOV;
#else
    // POSIX mystery meat

    long iovmax;

    errno = 0;
    if ((iovmax = sysconf(_SC_IOV_MAX)) == -1) {
        // assume iovmax == -1 && errno == 0 means "unbounded"
        return errno ? _XOPEN_IOV_MAX : count;
    }
    else {
        return (size_t) iovmax;
    }
#endif
}

#endif

#endif // KJ_IOVMAX_H_

