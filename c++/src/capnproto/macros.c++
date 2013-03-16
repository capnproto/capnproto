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
#include <exception>
#include <string>
#include <unistd.h>
#include <stdio.h>

namespace capnproto {
namespace internal {

class Exception: public std::exception {
  // Exception thrown in case of fatal errors.

public:
  Exception(const char* file, int line, const char* expectation, const char* message);
  ~Exception() noexcept;

  const char* what() const noexcept override;

private:
  std::string description;
};

Exception::Exception(
    const char* file, int line, const char* expectation, const char* message) {
  description = "Captain Proto debug assertion failed:\n  ";
  description += file;
  description += ':';
  char buf[32];
  sprintf(buf, "%d", line);
  description += buf;
  description += ": ";
  description += expectation;
  description += "\n  ";
  description += message;
  description += "\n";

  write(STDERR_FILENO, description.data(), description.size());
}

Exception::~Exception() noexcept {}

const char* Exception::what() const noexcept {
  return description.c_str();
}

void assertionFailure(const char* file, int line, const char* expectation, const char* message) {
  throw Exception(file, line, expectation, message);
}

}  // namespace internal
}  // namespace capnproto
