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

#include "lexer.h"
#include "../message.h"
#include <kj/compat/gtest.h>

namespace capnp {
namespace compiler {
namespace {

class TestFailingErrorReporter: public ErrorReporter {
public:
  void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) override {
    KJ_FAIL_EXPECT("Parse failed.", startByte, endByte, message);
  }

  bool hadErrors() override {
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
        "(identifier = 'foo', startByte = 0, endByte = 3), "
        "(identifier = 'bar', startByte = 4, endByte = 7)"
      "])",
      doLex<LexedTokens>("foo bar").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(identifier = 'foo', startByte = 0, endByte = 3), "
        "(identifier = 'bar', startByte = 15, endByte = 18)"
      "])",
      doLex<LexedTokens>("foo # comment\n bar").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(stringLiteral = 'foo ', startByte = 2, endByte = 11), "
        "(integerLiteral = 123, startByte = 12, endByte = 15), "
        "(floatLiteral = 2.75, startByte = 16, endByte = 20), "
        "(floatLiteral = 60000, startByte = 21, endByte = 24), "
        "(operator = '+', startByte = 25, endByte = 26), "
        "(operator = '-=', startByte = 27, endByte = 29)"
      "])",
      doLex<LexedTokens>("  'foo\\x20' 123 2.75 6e4 + -=  ").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(parenthesizedList = ["
          "["
            "(identifier = 'foo', startByte = 1, endByte = 4), "
            "(identifier = 'bar', startByte = 5, endByte = 8)"
          "], ["
            "(identifier = 'baz', startByte = 10, endByte = 13), "
            "(identifier = 'qux', startByte = 14, endByte = 17)"
          "], ["
            "(identifier = 'corge', startByte = 19, endByte = 24), "
            "(identifier = 'grault', startByte = 25, endByte = 31)"
          "]"
        "], startByte = 0, endByte = 32)"
      "])",
      doLex<LexedTokens>("(foo bar, baz qux, corge grault)").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(parenthesizedList = ["
          "["
            "(identifier = 'foo', startByte = 1, endByte = 4), "
            "(identifier = 'bar', startByte = 5, endByte = 8)"
          "]"
        "], startByte = 0, endByte = 9)"
      "])",
      doLex<LexedTokens>("(foo bar)").cStr());

  // Empty parentheses should result in an empty list-of-lists, *not* a list containing an empty
  // list.
  EXPECT_STREQ(
      "(tokens = ["
        "(parenthesizedList = [], startByte = 0, endByte = 4)"
      "])",
      doLex<LexedTokens>("(  )").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(bracketedList = ["
          "["
            "(identifier = 'foo', startByte = 1, endByte = 4), "
            "(identifier = 'bar', startByte = 5, endByte = 8)"
          "], ["
            "(identifier = 'baz', startByte = 10, endByte = 13), "
            "(identifier = 'qux', startByte = 14, endByte = 17)"
          "], ["
            "(identifier = 'corge', startByte = 19, endByte = 24), "
            "(identifier = 'grault', startByte = 25, endByte = 31)"
          "]"
        "], startByte = 0, endByte = 32)"
      "])",
      doLex<LexedTokens>("[foo bar, baz qux, corge grault]").cStr());

  // Trailing commas should not create an empty final list item, but be stripped by the lexer.
  EXPECT_STREQ(
      "(tokens = ["
        "(bracketedList = ["
          "["
            "(identifier = 'foo', startByte = 1, endByte = 4)"
          "], ["
            "(identifier = 'bar', startByte = 6, endByte = 9)"
          "]"
        "], startByte = 0, endByte = 11)"
      "])",
      doLex<LexedTokens>("[foo, bar,]").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(bracketedList = ["
          "["
            "(identifier = 'foo', startByte = 1, endByte = 4)"
          "], ["
            "(parenthesizedList = ["
              "["
                "(identifier = 'bar', startByte = 7, endByte = 10)"
              "], ["
                "(identifier = 'baz', startByte = 12, endByte = 15)"
              "]"
            "], startByte = 6, endByte = 16)"
          "]"
        "], startByte = 0, endByte = 17), "
        "(identifier = 'qux', startByte = 18, endByte = 21)"
      "])",
      doLex<LexedTokens>("[foo, (bar, baz)] qux").cStr());

  EXPECT_STREQ(
      "(tokens = ["
        "(identifier = 'foo', startByte = 0, endByte = 3), "
        "(identifier = 'bar', startByte = 7, endByte = 10)"
      "])",
      doLex<LexedTokens>("foo\n\r\t\vbar").cStr());
}

TEST(Lexer, Statements) {
  EXPECT_STREQ(
      "(statements = ["
        "(tokens = ["
          "(identifier = 'foo', startByte = 0, endByte = 3), "
          "(identifier = 'bar', startByte = 4, endByte = 7)"
        "], line = void, startByte = 0, endByte = 8)"
      "])",
      doLex<LexedStatements>("foo bar;").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "(tokens = ["
          "(identifier = 'foo', startByte = 0, endByte = 3)"
        "], line = void, startByte = 0, endByte = 4), "
        "(tokens = ["
          "(identifier = 'bar', startByte = 5, endByte = 8)"
        "], line = void, startByte = 5, endByte = 9), "
        "(tokens = ["
          "(identifier = 'baz', startByte = 10, endByte = 13)"
        "], line = void, startByte = 10, endByte = 14)"
      "])",
      doLex<LexedStatements>("foo; bar; baz; ").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(identifier = 'foo', startByte = 0, endByte = 3)"
          "], "
          "block = ["
            "(tokens = ["
              "(identifier = 'bar', startByte = 5, endByte = 8)"
            "], line = void, startByte = 5, endByte = 9), "
            "(tokens = ["
              "(identifier = 'baz', startByte = 10, endByte = 13)"
            "], line = void, startByte = 10, endByte = 14)"
          "], "
          "startByte = 0, endByte = 15"
        "), "
        "(tokens = ["
          "(identifier = 'qux', startByte = 16, endByte = 19)"
        "], line = void, startByte = 16, endByte = 20)"
      "])",
      doLex<LexedStatements>("foo {bar; baz;} qux;").cStr());
}

TEST(Lexer, DocComments) {
  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(identifier = 'foo', startByte = 0, endByte = 3)"
          "], "
          "line = void, "
          "docComment = 'blah blah\\n', "
          "startByte = 0, endByte = 16"
        ")"
      "])",
      doLex<LexedStatements>("foo; # blah blah").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(identifier = 'foo', startByte = 0, endByte = 3)"
          "], "
          "line = void, "
          "docComment = 'blah blah\\n', "
          "startByte = 0, endByte = 15"
        ")"
      "])",
      doLex<LexedStatements>("foo; #blah blah").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(identifier = 'foo', startByte = 0, endByte = 3)"
          "], "
          "line = void, "
          "docComment = ' blah blah\\n', "
          "startByte = 0, endByte = 17"
        ")"
      "])",
      doLex<LexedStatements>("foo; #  blah blah").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(identifier = 'foo', startByte = 0, endByte = 3)"
          "], "
          "line = void, "
          "docComment = 'blah blah\\n', "
          "startByte = 0, endByte = 16"
        ")"
      "])",
      doLex<LexedStatements>("foo;\n# blah blah").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(identifier = 'foo', startByte = 0, endByte = 3)"
          "], "
          "line = void, "
          "startByte = 0, endByte = 4"
        ")"
      "])",
      doLex<LexedStatements>("foo;\n\n# blah blah").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(identifier = 'foo', startByte = 0, endByte = 3)"
          "], "
          "line = void, "
          "docComment = 'bar baz\\nqux corge\\n', "
          "startByte = 0, endByte = 30"
        ")"
      "])",
      doLex<LexedStatements>("foo;\n # bar baz\n  # qux corge\n\n# grault\n# garply").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(identifier = 'foo', startByte = 0, endByte = 3)"
          "], "
          "block = ["
            "(tokens = ["
              "(identifier = 'bar', startByte = 17, endByte = 20)"
            "], line = void, docComment = 'hi\\n', startByte = 17, endByte = 27), "
            "(tokens = ["
              "(identifier = 'baz', startByte = 28, endByte = 31)"
            "], line = void, startByte = 28, endByte = 32)"
          "], "
          "docComment = 'blah blah\\n', "
          "startByte = 0, endByte = 44"
        "), "
        "(tokens = ["
          "(identifier = 'qux', startByte = 44, endByte = 47)"
        "], line = void, startByte = 44, endByte = 48)"
      "])",
      doLex<LexedStatements>("foo {# blah blah\nbar; # hi\n baz;} # ignored\nqux;").cStr());

  EXPECT_STREQ(
      "(statements = ["
        "("
          "tokens = ["
            "(identifier = 'foo', startByte = 0, endByte = 3)"
          "], "
          "block = ["
            "(tokens = ["
              "(identifier = 'bar', startByte = 5, endByte = 8)"
            "], line = void, startByte = 5, endByte = 9), "
            "(tokens = ["
              "(identifier = 'baz', startByte = 10, endByte = 13)"
            "], line = void, startByte = 10, endByte = 14)"
          "], "
          "docComment = 'late comment\\n', "
          "startByte = 0, endByte = 31"
        "), "
        "(tokens = ["
          "(identifier = 'qux', startByte = 31, endByte = 34)"
        "], line = void, startByte = 31, endByte = 35)"
      "])",
      doLex<LexedStatements>("foo {bar; baz;}\n# late comment\nqux;").cStr());
}

TEST(Lexer, Utf8Bom) {
  EXPECT_STREQ(
      "(tokens = ["
        "(identifier = 'foo', startByte = 3, endByte = 6), "
        "(identifier = 'bar', startByte = 7, endByte = 10), "
        "(identifier = 'baz', startByte = 13, endByte = 16)"
      "])",
      doLex<LexedTokens>("\xef\xbb\xbf""foo bar\xef\xbb\xbf""baz").cStr());
}

}  // namespace
}  // namespace compiler
}  // namespace capnp
