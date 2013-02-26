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

#include "macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exception>

namespace capnproto {
namespace internal {

class Exception: public std::exception {
  // Exception thrown in case of fatal errors.

public:
  Exception(const char* file, int line, const char* expectation, const char* message);
  virtual ~Exception() noexcept;

  const char* getFile() { return file; }
  int getLine() { return line; }
  const char* getExpectation() { return expectation; }
  const char* getMessage() { return message; }

  virtual const char* what();

private:
  const char* file;
  int line;
  const char* expectation;
  const char* message;
  char* whatBuffer;
};

Exception::Exception(
    const char* file, int line, const char* expectation, const char* message)
    : file(file), line(line), expectation(expectation), message(message), whatBuffer(nullptr) {
  fprintf(stderr, "Captain Proto debug assertion failed:\n  %s:%d: %s\n  %s",
          file, line, expectation, message);
}

Exception::~Exception() noexcept {
  delete [] whatBuffer;
}

const char* Exception::what() {
  whatBuffer = new char[strlen(file) + strlen(expectation) + strlen(message) + 256];
  sprintf(whatBuffer, "Captain Proto debug assertion failed:\n  %s:%d: %s\n  %s",
          file, line, expectation, message);
  return whatBuffer;
}

void assertionFailure(const char* file, int line, const char* expectation, const char* message) {
  throw Exception(file, line, expectation, message);
}

}  // namespace internal
}  // namespace capnproto
