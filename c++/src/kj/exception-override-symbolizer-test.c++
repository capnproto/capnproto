// Copyright (c) 2022 Cloudflare, Inc. and contributors
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

#if __GNUC__ && !_WIN32

#include "debug.h"
#include <kj/exception.h>
#include "kj/common.h"
#include "kj/array.h"
#include <kj/compat/gtest.h>
#include <stdexcept>
#include <stdint.h>

namespace kj {

// override weak symbol
String stringifyStackTrace(ArrayPtr<void* const> trace) {
  return kj::str("\n\nTEST_SYMBOLIZER\n\n");
}

namespace {

KJ_TEST("getStackTrace() uses symbolizer override") {
  auto trace = getStackTrace();
  KJ_ASSERT(strstr(trace.cStr(), "TEST_SYMBOLIZER") != nullptr, trace);
}

}  // namespace
}  // namespace kj

#endif
