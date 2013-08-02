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

#ifndef ERROR_REPORTER_H_
#define ERROR_REPORTER_H_

#include "../common.h"
#include <kj/string.h>
#include <kj/exception.h>

namespace capnp {
namespace compiler {

class ErrorReporter {
  // Callback for reporting errors within a particular file.

public:
  virtual ~ErrorReporter() noexcept(false);

  virtual void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) const = 0;
  // Report an error at the given location in the input text.  `startByte` and `endByte` indicate
  // the span of text that is erroneous.  They may be equal, in which case the parser was only
  // able to identify where the error begins, not where it ends.

  template <typename T>
  inline void addErrorOn(T&& decl, kj::StringPtr message) const {
    // Works for any `T` that defines `getStartByte()` and `getEndByte()` methods, which many
    // of the Cap'n Proto types defined in `grammar.capnp` do.

    addError(decl.getStartByte(), decl.getEndByte(), message);
  }

  virtual bool hadErrors() const = 0;
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
                        kj::StringPtr message) const = 0;
  // Report an error at the given location in the given file.

  virtual bool hadErrors() const = 0;
  // Return true if any errors have been reported, globally.  The main use case for this callback
  // is to inhibit the reporting of errors which may have been caused by previous errors, or to
  // allow the compiler to bail out entirely if it gets confused and thinks this could be because
  // of previous errors.
};

}  // namespace compiler
}  // namespace capnp

#endif  // ERROR_REPORTER_H_
