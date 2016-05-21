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

#ifndef ERROR_REPORTER_H_
#define ERROR_REPORTER_H_

#if defined(__GNUC__) && !defined(CAPNP_HEADER_WARNINGS)
#pragma GCC system_header
#endif

#include "../common.h"
#include <kj/string.h>
#include <kj/exception.h>
#include <kj/vector.h>

namespace capnp {
namespace compiler {

class ErrorReporter {
  // Callback for reporting errors within a particular file.

public:
  virtual void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) = 0;
  // Report an error at the given location in the input text.  `startByte` and `endByte` indicate
  // the span of text that is erroneous.  They may be equal, in which case the parser was only
  // able to identify where the error begins, not where it ends.

  template <typename T>
  inline void addErrorOn(T&& decl, kj::StringPtr message) {
    // Works for any `T` that defines `getStartByte()` and `getEndByte()` methods, which many
    // of the Cap'n Proto types defined in `grammar.capnp` do.

    addError(decl.getStartByte(), decl.getEndByte(), message);
  }

  virtual bool hadErrors() = 0;
  // Return true if any errors have been reported, globally.  The main use case for this callback
  // is to inhibit the reporting of errors which may have been caused by previous errors, or to
  // allow the compiler to bail out entirely if it gets confused and thinks this could be because
  // of previous errors.
};

class GlobalErrorReporter {
  // Callback for reporting errors in any file.

public:
  struct SourcePos {
    uint byte;
    uint line;
    uint column;
  };

  virtual void addError(kj::StringPtr file, SourcePos start, SourcePos end,
                        kj::StringPtr message) = 0;
  // Report an error at the given location in the given file.

  virtual bool hadErrors() = 0;
  // Return true if any errors have been reported, globally.  The main use case for this callback
  // is to inhibit the reporting of errors which may have been caused by previous errors, or to
  // allow the compiler to bail out entirely if it gets confused and thinks this could be because
  // of previous errors.
};

class LineBreakTable {
public:
  LineBreakTable(kj::ArrayPtr<const char> content);

  GlobalErrorReporter::SourcePos toSourcePos(uint32_t byteOffset) const;

private:
  kj::Vector<uint> lineBreaks;
  // Byte offsets of the first byte in each source line.  The first element is always zero.
  // Initialized the first time the module is loaded.
};

}  // namespace compiler
}  // namespace capnp

#endif  // ERROR_REPORTER_H_
