// Copyright (c) 2019 Cloudflare, Inc. and contributors
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

#if __linux__

// This test compiles mutex.c++ and mutex-test.c++ with KJ_USE_FUTEX disabled, so that pthreads
// will be used instead, as it would be on many other systems.
//
// This test is only intended to run on Linux, but is intended to make the code behave like it
// would on a generic flavor of Unix.
//
// At present this test only runs under Ekam builds. Integrating it into other builds would be
// awkward since it #includes async-io-unix.c++, so it cannot link against that file, but
// needs to link against the rest of KJ. Ekam "just figures it out", but other build systems would
// require a lot of work here.

#define KJ_USE_FUTEX 0

#define KJ_MUTEX_TEST 1
#include "mutex.c++"
#include "mutex-test.c++"

#endif  // __linux__
