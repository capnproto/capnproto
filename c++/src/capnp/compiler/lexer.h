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

#pragma once

#include <capnp/compiler/lexer.capnp.h>
#include <kj/parse/common.h>
#include <kj/arena.h>
#include "error-reporter.h"

CAPNP_BEGIN_HEADER

namespace capnp {
namespace compiler {

bool lex(kj::ArrayPtr<const char> input, LexedStatements::Builder result,
         ErrorReporter& errorReporter);
bool lex(kj::ArrayPtr<const char> input, LexedTokens::Builder result, ErrorReporter& errorReporter);
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
  Lexer(Orphanage orphanage, ErrorReporter& errorReporter);
  // `orphanage` is used to allocate Cap'n Proto message objects in the result.  `inputStart` is
  // a pointer to the beginning of the input, used to compute byte offsets.

  ~Lexer() noexcept(false);

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
  kj::Arena arena;
  Parsers parsers;
};

}  // namespace compiler
}  // namespace capnp

CAPNP_END_HEADER
