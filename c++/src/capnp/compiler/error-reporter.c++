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

#include "error-reporter.h"
#include <kj/debug.h>

namespace capnp {
namespace compiler {

namespace {

template <typename T>
static size_t findLargestElementBefore(const kj::Vector<T>& vec, const T& key) {
  KJ_REQUIRE(vec.size() > 0 && vec[0] <= key);

  size_t lower = 0;
  size_t upper = vec.size();

  while (upper - lower > 1) {
    size_t mid = (lower + upper) / 2;
    if (vec[mid] > key) {
      upper = mid;
    } else {
      lower = mid;
    }
  }

  return lower;
}

}  // namespace

LineBreakTable::LineBreakTable(kj::ArrayPtr<const char> content)
    : lineBreaks(content.size() / 40) {
  lineBreaks.add(0);
  for (const char* pos = content.begin(); pos < content.end(); ++pos) {
    if (*pos == '\n') {
      lineBreaks.add(pos + 1 - content.begin());
    }
  }
}

GlobalErrorReporter::SourcePos LineBreakTable::toSourcePos(uint32_t byteOffset) const {
  uint line = findLargestElementBefore(lineBreaks, byteOffset);
  uint col = byteOffset - lineBreaks[line];
  return GlobalErrorReporter::SourcePos { byteOffset, line, col };
}

}  // namespace compiler
}  // namespace capnp
