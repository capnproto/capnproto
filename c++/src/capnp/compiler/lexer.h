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

#ifndef CAPNP_COMPILER_LEXER_H_
#define CAPNP_COMPILER_LEXER_H_

#include <capnp/compiler/lexer.capnp.h>
#include <kj/parse/common.h>
#include <kj/arena.h>
#include "error-reporter.h"

namespace capnp {
namespace compiler {

bool lex(kj::ArrayPtr<const char> input, LexedStatements::Builder result,
         const ErrorReporter& errorReporter);
bool lex(kj::ArrayPtr<const char> input, LexedTokens::Builder result,
         const ErrorReporter& errorReporter);
// Lex the given source code, placing the results in `result`.  Returns true if there
// were no errors, false if there were.  Even when errors are present, the file may have partial
// content which can be fed into later stages of parsing in order to find more errors.
//
// There are two versions, one that parses a list of statements, and one which just parses tokens
// that might form a part of one statement.  In other words, in the later case, the input should
// not contain semicolons or curly braces, unless they are in string literals of course.

class Lexer {
  // Advanced lexer interface.  This interface exposes the inner parsers so that you can embed them
  // into your own parsers.

public:
  Lexer(Orphanage orphanage, const ErrorReporter& errorReporter);
  // `orphanage` is used to allocate Cap'n Proto message objects in the result.  `inputStart` is
  // a pointer to the beginning of the input, used to compute byte offsets.

  ~Lexer();

  class ParserInput: public kj::parse::IteratorInput<char, const char*> {
    // Like IteratorInput<char, const char*> except that positions are measured as byte offsets
    // rather than pointers.

  public:
    ParserInput(const char* begin, const char* end)
      : IteratorInput<char, const char*>(begin, end), begin(begin) {}
    explicit ParserInput(ParserInput& parent)
      : IteratorInput<char, const char*>(parent), begin(parent.begin) {}

    inline uint32_t getBest() {
      return IteratorInput<char, const char*>::getBest() - begin;
    }
    inline uint32_t getPosition() {
      return IteratorInput<char, const char*>::getPosition() - begin;
    }

  private:
    const char* begin;
  };

  template <typename Output>
  using Parser = kj::parse::ParserRef<ParserInput, Output>;

  struct Parsers {
    Parser<kj::Tuple<>> emptySpace;
    Parser<Orphan<Token>> token;
    Parser<kj::Array<Orphan<Token>>> tokenSequence;
    Parser<Orphan<Statement>> statement;
    Parser<kj::Array<Orphan<Statement>>> statementSequence;
  };

  const Parsers& getParsers() { return parsers; }

private:
  Orphanage orphanage;
  const ErrorReporter& errorReporter;
  kj::Arena arena;
  Parsers parsers;
};

}  // namespace compiler
}  // namespace capnp

#endif  // CAPNP_COMPILER_LEXER_H_
