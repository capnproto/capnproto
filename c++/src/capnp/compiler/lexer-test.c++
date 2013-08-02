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
#include "../message.h"
#include <gtest/gtest.h>

namespace capnp {
namespace compiler {
namespace {

class TestFailingErrorReporter: public ErrorReporter {
public:
  void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) const override {
    ADD_FAILURE() << "Parse failed: (" << startByte << "-" << endByte << ") " << message.cStr();
  }

  bool hadErrors() const override {
    // Not used by lexer.
    return false;
  }
};

template <typename LexResult>
kj::String doLex(kj::StringPtr constText) {
  // Parse the given string into the given Cap'n Proto struct type using lex(), then stringify the
  // result and return that string.  Additionally, single quotes in the input are converted to
  // double quotes, and double quotes in the output are converted to single quotes, to reduce the
  // amount of escaping needed in the test strings.
  //
  // Comparing stringifications against golden strings is ugly and brittle.  If we had a
  // text-format parser we could use that.  Except that said parser would probably be built on
  // the very lexer being tested here, so...  maybe this is the best we can reasonably do.

  kj::String text = heapString(constText);
  for (char& c: text) {
    // Make it easier to write input strings below.
    if (c == '\'') c = '\"';
  }
  MallocMessageBuilder message;
  auto file = message.initRoot<LexResult>();
  TestFailingErrorReporter errorReporter;
  EXPECT_TRUE(lex(text, file, errorReporter));
  kj::String result = kj::str(file);
  for (char& c: result) {
    // Make it easier to write golden strings below.
    if (c == '\"') c = '\'';
  }
  return result;
}

TEST(Lexer, Tokens) {
  EXPECT_STREQ(
      "(tokens = ["
        "(body = identifier('foo'), endByte = 3), "
        "(body = identifier('bar'), startByte = 4, endByte = 7)"
      "])",
      doLex<LexedTokens>("foo bar").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(body = identifier('foo'), endByte = 3), "
        "(body = identifier('bar'), startByte = 15, endByte = 18)"
      "])",
      doLex<LexedTokens>("foo # comment\n bar").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(body = stringLiteral('foo '), startByte = 2, endByte = 11), "
        "(body = integerLiteral(123), startByte = 12, endByte = 15), "
        "(body = floatLiteral(2.75), startByte = 16, endByte = 20), "
        "(body = floatLiteral(60000), startByte = 21, endByte = 24), "
        "(body = operator('+'), startByte = 25, endByte = 26), "
        "(body = operator('-='), startByte = 27, endByte = 29)"
      "])",
      doLex<LexedTokens>("  'foo\\x20' 123 2.75 6e4 + -=  ").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(body = parenthesizedList(["
          "["
            "(body = identifier('foo'), startByte = 1, endByte = 4), "
            "(body = identifier('bar'), startByte = 5, endByte = 8)"
          "], ["
            "(body = identifier('baz'), startByte = 10, endByte = 13), "
            "(body = identifier('qux'), startByte = 14, endByte = 17)"
          "], ["
            "(body = identifier('corge'), startByte = 19, endByte = 24), "
            "(body = identifier('grault'), startByte = 25, endByte = 31)"
          "]"
        "]), endByte = 32)"
      "])",
      doLex<LexedTokens>("(foo bar, baz qux, corge grault)").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(body = parenthesizedList(["
          "["
            "(body = identifier('foo'), startByte = 1, endByte = 4), "
            "(body = identifier('bar'), startByte = 5, endByte = 8)"
          "]"
        "]), endByte = 9)"
      "])",
      doLex<LexedTokens>("(foo bar)").cStr());

  // Empty parentheses should result in an empty list-of-lists, *not* a list containing an empty
  // list.
  EXPECT_STREQ(
      "(tokens = ["
        "(body = parenthesizedList([]), endByte = 4)"
      "])",
      doLex<LexedTokens>("(  )").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(body = bracketedList(["
          "["
            "(body = identifier('foo'), startByte = 1, endByte = 4), "
            "(body = identifier('bar'), startByte = 5, endByte = 8)"
          "], ["
            "(body = identifier('baz'), startByte = 10, endByte = 13), "
            "(body = identifier('qux'), startByte = 14, endByte = 17)"
          "], ["
            "(body = identifier('corge'), startByte = 19, endByte = 24), "
            "(body = identifier('grault'), startByte = 25, endByte = 31)"
          "]"
        "]), endByte = 32)"
      "])",
      doLex<LexedTokens>("[foo bar, baz qux, corge grault]").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(body = bracketedList(["
          "["
            "(body = identifier('foo'), startByte = 1, endByte = 4)"
          "], ["
            "(body = parenthesizedList(["
              "["
                "(body = identifier('bar'), startByte = 7, endByte = 10)"
              "], ["
                "(body = identifier('baz'), startByte = 12, endByte = 15)"
              "]"
            "]), startByte = 6, endByte = 16)"
          "]"
        "]), endByte = 17), "
        "(body = identifier('qux'), startByte = 18, endByte = 21)"
      "])",
      doLex<LexedTokens>("[foo, (bar, baz)] qux").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(body = identifier('foo'), endByte = 3), "
        "(body = identifier('bar'), startByte = 7, endByte = 10)"
      "])",
      doLex<LexedTokens>("foo\n\r\t\vbar").cStr());
}

TEST(Lexer, Statements) {
  EXPECT_STREQ(
      "(statements = ["
        "(tokens = ["
          "(body = identifier('foo'), endByte = 3), "
          "(body = identifier('bar'), startByte = 4, endByte = 7)"
        "], endByte = 8)"
      "])",
      doLex<LexedStatements>("foo bar;").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "(tokens = ["
          "(body = identifier('foo'), endByte = 3)"
        "], endByte = 4), "
        "(tokens = ["
          "(body = identifier('bar'), startByte = 5, endByte = 8)"
        "], startByte = 5, endByte = 9), "
        "(tokens = ["
          "(body = identifier('baz'), startByte = 10, endByte = 13)"
        "], startByte = 10, endByte = 14)"
      "])",
      doLex<LexedStatements>("foo; bar; baz; ").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(body = identifier('foo'), endByte = 3)"
          "], "
          "block = statements(["
            "(tokens = ["
              "(body = identifier('bar'), startByte = 5, endByte = 8)"
            "], startByte = 5, endByte = 9), "
            "(tokens = ["
              "(body = identifier('baz'), startByte = 10, endByte = 13)"
            "], startByte = 10, endByte = 14)"
          "]), "
          "endByte = 15"
        "), "
        "(tokens = ["
          "(body = identifier('qux'), startByte = 16, endByte = 19)"
        "], startByte = 16, endByte = 20)"
      "])",
      doLex<LexedStatements>("foo {bar; baz;} qux;").cStr());
}

TEST(Lexer, DocComments) {
  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(body = identifier('foo'), endByte = 3)"
          "], "
          "docComment = 'blah blah\\n', "
          "endByte = 16"
        ")"
      "])",
      doLex<LexedStatements>("foo; # blah blah").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(body = identifier('foo'), endByte = 3)"
          "], "
          "docComment = 'blah blah\\n', "
          "endByte = 15"
        ")"
      "])",
      doLex<LexedStatements>("foo; #blah blah").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(body = identifier('foo'), endByte = 3)"
          "], "
          "docComment = ' blah blah\\n', "
          "endByte = 17"
        ")"
      "])",
      doLex<LexedStatements>("foo; #  blah blah").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(body = identifier('foo'), endByte = 3)"
          "], "
          "docComment = 'blah blah\\n', "
          "endByte = 16"
        ")"
      "])",
      doLex<LexedStatements>("foo;\n# blah blah").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(body = identifier('foo'), endByte = 3)"
          "], "
          "endByte = 4"
        ")"
      "])",
      doLex<LexedStatements>("foo;\n\n# blah blah").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(body = identifier('foo'), endByte = 3)"
          "], "
          "docComment = 'bar baz\\nqux corge\\n', "
          "endByte = 30"
        ")"
      "])",
      doLex<LexedStatements>("foo;\n # bar baz\n  # qux corge\n\n# grault\n# garply").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(body = identifier('foo'), endByte = 3)"
          "], "
          "block = statements(["
            "(tokens = ["
              "(body = identifier('bar'), startByte = 17, endByte = 20)"
            "], docComment = 'hi\\n', startByte = 17, endByte = 27), "
            "(tokens = ["
              "(body = identifier('baz'), startByte = 28, endByte = 31)"
            "], startByte = 28, endByte = 32)"
          "]), "
          "docComment = 'blah blah\\n', "
          "endByte = 44"
        "), "
        "(tokens = ["
          "(body = identifier('qux'), startByte = 44, endByte = 47)"
        "], startByte = 44, endByte = 48)"
      "])",
      doLex<LexedStatements>("foo {# blah blah\nbar; # hi\n baz;} # ignored\nqux;").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(body = identifier('foo'), endByte = 3)"
          "], "
          "block = statements(["
            "(tokens = ["
              "(body = identifier('bar'), startByte = 5, endByte = 8)"
            "], startByte = 5, endByte = 9), "
            "(tokens = ["
              "(body = identifier('baz'), startByte = 10, endByte = 13)"
            "], startByte = 10, endByte = 14)"
          "]), "
          "docComment = 'late comment\\n', "
          "endByte = 31"
        "), "
        "(tokens = ["
          "(body = identifier('qux'), startByte = 31, endByte = 34)"
        "], startByte = 31, endByte = 35)"
      "])",
      doLex<LexedStatements>("foo {bar; baz;}\n# late comment\nqux;").cStr());
}

}  // namespace
}  // namespace compiler
}  // namespace capnp
