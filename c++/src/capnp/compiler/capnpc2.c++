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

#include "lexer.h"
#include <kj/vector.h>
#include <kj/io.h>
#include <unistd.h>
#include <kj/debug.h>
#include "../message.h"

int main(int argc, char* argv[]) {
  // Eventually this will be capnpc.  For now it's just a dummy program that tests parsing.

  kj::Vector<char> input;
  char buffer[4096];
  for (;;) {
    ssize_t n;
    KJ_SYSCALL(n = read(STDIN_FILENO, buffer, sizeof(buffer)));
    if (n == 0) {
      break;
    }
    input.addAll(buffer, buffer + n);
  }

  KJ_DBG(input);

  capnp::MallocMessageBuilder message;
  auto file = message.initRoot<capnp::compiler::LexedStatements>();
  capnp::compiler::lex(input, file);

  KJ_DBG(file);

  return 0;
}
