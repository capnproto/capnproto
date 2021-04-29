// Copyright (c) 2021 Cloudflare, Inc. and contributors
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

#include "string.h"

#if __clang__ && __clang_major__ >= 9
#define KJ_CALLER_COLUMN() __builtin_COLUMN()
// GCC does not implement __builtin_COLUMN() as that's non-standard but MSVC & clang do.
// MSVC does as of version https://github.com/microsoft/STL/issues/54) but there's currently not any
// pressing need for this for MSVC & writing the write compiler version check is annoying.
#else
#define KJ_CALLER_COLUMN() 0
#endif

#if __cplusplus > 201703L
#define KJ_COMPILER_SUPPORTS_SOURCE_LOCATION 1
#elif __clang__ && __clang_major__ >= 9
// Clang 9 added these builtins: https://releases.llvm.org/9.0.0/tools/clang/docs/LanguageExtensions.html
#define KJ_COMPILER_SUPPORTS_SOURCE_LOCATION 1
#elif __GNUC__ >= 5
// GCC 5 supports the required builtins: https://gcc.gnu.org/onlinedocs/gcc-5.1.0/gcc/Other-Builtins.html
#define KJ_COMPILER_SUPPORTS_SOURCE_LOCATION 1
#endif

namespace kj {
class SourceLocation {
  // libc++ doesn't seem to implement <source_location> (or even <experimental/source_location>), so
  // this is a non-STL wrapper over the compiler primitives (these are the same across MSVC/clang/
  // gcc). Additionally this uses kj::StringPtr for holding the strings instead of const char* which
  // makes it integrate a little more nicely into KJ.
public:
#if KJ_COMPILER_SUPPORTS_SOURCE_LOCATION
  static constexpr SourceLocation current(
      const char* file = __builtin_FILE(), const char* function = __builtin_FUNCTION(),
      uint line = __builtin_LINE(), uint column = KJ_CALLER_COLUMN()) {
    // When invoked with no arguments returns a SourceLocation that points at the caller of the
    // function.
    return SourceLocation(file, function, line, column);
  }
#endif

  constexpr SourceLocation() : SourceLocation("??", "??", 0, 0) {}
  // Constructs a dummy source location that's not pointing at anything.

  const char* fileName;
  const char* function;
  uint lineNumber;
  uint columnNumber;

private:
  constexpr SourceLocation(const char* file, const char* func, uint line, uint column)
    : fileName(file), function(func), lineNumber(line), columnNumber(column)
  {}
};

kj::String KJ_STRINGIFY(const SourceLocation& l);

class NoopSourceLocation {
  // This is used in places where we want to conditionally compile out tracking the source location.
  // As such it intentionally lacks all the features but the static `::current()` function so that
  // the API isn't accidentally used in the wrong compilation context.
public:
  static constexpr NoopSourceLocation current() {
    return NoopSourceLocation{};
  }
};

KJ_UNUSED static kj::String KJ_STRINGIFY(const NoopSourceLocation& l) {
  return kj::String();
}
}  // namespace kj
