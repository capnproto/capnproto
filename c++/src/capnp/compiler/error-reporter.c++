// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
