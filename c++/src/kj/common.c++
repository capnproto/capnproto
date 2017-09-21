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

#include "common.h"
#include "debug.h"
#include <stdlib.h>
#ifdef _MSC_VER
#include <limits>
#endif

namespace kj {
namespace _ {  // private

void inlineRequireFailure(const char* file, int line, const char* expectation,
                          const char* macroArgs, const char* message) {
  if (message == nullptr) {
    Debug::Fault f(file, line, kj::Exception::Type::FAILED, expectation, macroArgs);
    f.fatal();
  } else {
    Debug::Fault f(file, line, kj::Exception::Type::FAILED, expectation, macroArgs, message);
    f.fatal();
  }
}

void unreachable() {
  KJ_FAIL_ASSERT("Supposedly-unreachable branch executed.");

  // Really make sure we abort.
  abort();
}

}  // namespace _ (private)

#if _MSC_VER && !__clang__

float nan() { return std::numeric_limits<float>::quiet_NaN(); }

#endif

}  // namespace kj
