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
#include <kj/parse/char.h>
#include <kj/debug.h>

namespace capnp {
namespace compiler {

namespace p = kj::parse;

bool lex(kj::ArrayPtr<const char> input, LexedStatements::Builder result,
         ErrorReporter& errorReporter) {
  Lexer lexer(Orphanage::getForMessageContaining(result), errorReporter);

  auto parser = p::sequence(lexer.getParsers().statementSequence, p::endOfInput);

  Lexer::ParserInput parserInput(input.begin(), input.end());
  kj::Maybe<kj::Array<Orphan<Statement>>> parseOutput = parser(parserInput);

  KJ_IF_MAYBE(output, parseOutput) {
    auto l = result.initStatements(output->size());
    for (uint i = 0; i < output->size(); i++) {
      l.adoptWithCaveats(i, kj::mv((*output)[i]));
    }
    return true;
  } else {
    uint32_t best = parserInput.getBest();
    errorReporter.addError(best, best, kj::str("Parse error."));
    return false;
  }
}

bool lex(kj::ArrayPtr<const char> input, LexedTokens::Builder result,
         ErrorReporter& errorReporter) {
  Lexer lexer(Orphanage::getForMessageContaining(result), errorReporter);

  auto parser = p::sequence(lexer.getParsers().tokenSequence, p::endOfInput);

  Lexer::ParserInput parserInput(input.begin(), input.end());
  kj::Maybe<kj::Array<Orphan<Token>>> parseOutput = parser(parserInput);

  KJ_IF_MAYBE(output, parseOutput) {
    auto l = result.initTokens(output->size());
    for (uint i = 0; i < output->size(); i++) {
      l.adoptWithCaveats(i, kj::mv((*output)[i]));
    }
    return true;
  } else {
    uint32_t best = parserInput.getBest();
    errorReporter.addError(best, best, kj::str("Parse error."));
    return false;
  }
}

namespace {

typedef p::Span<uint32_t> Location;

Token::Builder initTok(Orphan<Token>& t, const Location& loc) {
  auto builder = t.get();
  builder.setStartByte(loc.begin());
  builder.setEndByte(loc.end());
  return builder;
}

void buildTokenSequenceList(List<List<Token>>::Builder builder,
                            kj::Array<kj::Array<Orphan<Token>>>&& items) {
  for (uint i = 0; i < items.size(); i++) {
    auto& item = items[i];
    auto itemBuilder = builder.init(i, item.size());
    for (uint j = 0; j < item.size(); j++) {
      itemBuilder.adoptWithCaveats(j, kj::mv(item[j]));
    }
  }
}

void attachDocComment(Statement::Builder statement, kj::Array<kj::String>&& comment) {
  size_t size = 0;
  for (auto& line: comment) {
    size += line.size() + 1;  // include newline
  }
  Text::Builder builder = statement.initDocComment(size);
  char* pos = builder.begin();
  for (auto& line: comment) {
    memcpy(pos, line.begin(), line.size());
    pos += line.size();
    *pos++ = '\n';
  }
  KJ_ASSERT(pos == builder.end());
}

constexpr auto discardComment =
    sequence(p::exactChar<'#'>(), p::discard(p::many(p::discard(p::anyOfChars("\n").invert()))),
             p::oneOf(p::exactChar<'\n'>(), p::endOfInput));
constexpr auto saveComment =
    sequence(p::exactChar<'#'>(), p::discard(p::optional(p::exactChar<' '>())),
             p::charsToString(p::many(p::anyOfChars("\n").invert())),
             p::oneOf(p::exactChar<'\n'>(), p::endOfInput));

constexpr auto utf8Bom =
    sequence(p::exactChar<'\xef'>(), p::exactChar<'\xbb'>(), p::exactChar<'\xbf'>());

constexpr auto bomsAndWhitespace =
    sequence(p::discardWhitespace,
             p::discard(p::many(sequence(utf8Bom, p::discardWhitespace))));

constexpr auto commentsAndWhitespace =
    sequence(bomsAndWhitespace,
             p::discard(p::many(sequence(discardComment, bomsAndWhitespace))));

constexpr auto discardLineWhitespace =
    p::discard(p::many(p::discard(p::whitespaceChar.invert().orAny("\r\n").invert())));
constexpr auto newline = p::oneOf(
    p::exactChar<'\n'>(),
    sequence(p::exactChar<'\r'>(), p::discard(p::optional(p::exactChar<'\n'>()))));

constexpr auto docComment = p::optional(p::sequence(
    discardLineWhitespace,
    p::discard(p::optional(newline)),
    p::oneOrMore(p::sequence(discardLineWhitespace, saveComment))));
// Parses a set of comment lines preceded by at most one newline and with no intervening blank
// lines.

}  // namespace

Lexer::Lexer(Orphanage orphanageParam, ErrorReporter& errorReporter)
    : orphanage(orphanageParam) {

  // Note that because passing an lvalue to a parser constructor uses it by-referencee, it's safe
  // for us to use parsers.tokenSequence even though we haven't yet constructed it.
  auto& tokenSequence = parsers.tokenSequence;

  auto& commaDelimitedList = arena.copy(p::transform(
      p::sequence(tokenSequence, p::many(p::sequence(p::exactChar<','>(), tokenSequence))),
      [](kj::Array<Orphan<Token>>&& first, kj::Array<kj::Array<Orphan<Token>>>&& rest)
          -> kj::Array<kj::Array<Orphan<Token>>> {
        if (first == nullptr && rest == nullptr) {
          // Completely empty list.
          return nullptr;
        } else {
          uint restSize = rest.size();
          if (restSize > 0 && rest[restSize - 1] == nullptr) {
            // Allow for trailing commas by shortening the list by one item if the final token is
            // nullptr
            restSize--;
          }
          auto result = kj::heapArrayBuilder<kj::Array<Orphan<Token>>>(1 + restSize); // first+rest
          result.add(kj::mv(first));
          for (uint i = 0; i < restSize ; i++) {
            result.add(kj::mv(rest[i]));
          }
          return result.finish();
        }
      }));

  auto& token = arena.copy(p::oneOf(
      p::transformWithLocation(p::identifier,
          [this](Location loc, kj::String name) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            initTok(t, loc).setIdentifier(name);
            return t;
          }),
      p::transformWithLocation(p::doubleQuotedString,
          [this](Location loc, kj::String text) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            initTok(t, loc).setStringLiteral(text);
            return t;
          }),
      p::transformWithLocation(
          sequence(p::exactChar<'`'>(), p::many(p::anyOfChars("\r\n").invert())),
          [this](Location loc, kj::Array<char> text) -> Orphan<Token> {
            // Backtick-quoted line. Note that we assume either `\r` or `\n` is a valid line
            // ending (to cover all known line ending formats) but we replace the line ending
            // with `\n`. This way, changing the line endings of your source code doesn't affect
            // the compiled code.
            auto t = orphanage.newOrphan<Token>();
            // Append '\n' to the text.
            auto out = initTok(t, loc).initStringLiteral(text.size() + 1);
            memcpy(out.begin(), text.begin(), text.size());
            out[out.size() - 1] = '\n';
            return t;
          }),
      p::transformWithLocation(p::doubleQuotedHexBinary,
          [this](Location loc, kj::Array<byte> data) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            initTok(t, loc).setBinaryLiteral(data);
            return t;
          }),
      p::transformWithLocation(p::integer,
          [this](Location loc, uint64_t i) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            initTok(t, loc).setIntegerLiteral(i);
            return t;
          }),
      p::transformWithLocation(p::number,
          [this](Location loc, double x) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            initTok(t, loc).setFloatLiteral(x);
            return t;
          }),
      p::transformWithLocation(
          p::charsToString(p::oneOrMore(p::anyOfChars("!$%&*+-./:<=>?@^|~"))),
          [this](Location loc, kj::String text) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            initTok(t, loc).setOperator(text);
            return t;
          }),
      p::transformWithLocation(
          sequence(p::exactChar<'('>(), commaDelimitedList, p::exactChar<')'>()),
          [this](Location loc, kj::Array<kj::Array<Orphan<Token>>>&& items) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            buildTokenSequenceList(
                initTok(t, loc).initParenthesizedList(items.size()), kj::mv(items));
            return t;
          }),
      p::transformWithLocation(
          sequence(p::exactChar<'['>(), commaDelimitedList, p::exactChar<']'>()),
          [this](Location loc, kj::Array<kj::Array<Orphan<Token>>>&& items) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            buildTokenSequenceList(
                initTok(t, loc).initBracketedList(items.size()), kj::mv(items));
            return t;
          }),
      p::transformOrReject(p::transformWithLocation(
          p::oneOf(sequence(p::exactChar<'\xff'>(), p::exactChar<'\xfe'>()),
                   sequence(p::exactChar<'\xfe'>(), p::exactChar<'\xff'>()),
                   sequence(p::exactChar<'\x00'>())),
          [&errorReporter](Location loc) -> kj::Maybe<Orphan<Token>> {
            errorReporter.addError(loc.begin(), loc.end(),
                "Non-UTF-8 input detected. Cap'n Proto schema files must be UTF-8 text.");
            return nullptr;
          }), [](kj::Maybe<Orphan<Token>> param) { return param; })));
  parsers.tokenSequence = arena.copy(p::sequence(
      commentsAndWhitespace, p::many(p::sequence(token, commentsAndWhitespace))));

  auto& statementSequence = parsers.statementSequence;

  auto& statementEnd = arena.copy(p::oneOf(
      transform(p::sequence(p::exactChar<';'>(), docComment),
          [this](kj::Maybe<kj::Array<kj::String>>&& comment) -> Orphan<Statement> {
            auto result = orphanage.newOrphan<Statement>();
            auto builder = result.get();
            KJ_IF_MAYBE(c, comment) {
              attachDocComment(builder, kj::mv(*c));
            }
            builder.setLine();
            return result;
          }),
      transform(
          p::sequence(p::exactChar<'{'>(), docComment, statementSequence, p::exactChar<'}'>(),
                      docComment),
          [this](kj::Maybe<kj::Array<kj::String>>&& comment,
                 kj::Array<Orphan<Statement>>&& statements,
                 kj::Maybe<kj::Array<kj::String>>&& lateComment)
              -> Orphan<Statement> {
            auto result = orphanage.newOrphan<Statement>();
            auto builder = result.get();
            KJ_IF_MAYBE(c, comment) {
              attachDocComment(builder, kj::mv(*c));
            } else KJ_IF_MAYBE(c, lateComment) {
              attachDocComment(builder, kj::mv(*c));
            }
            auto list = builder.initBlock(statements.size());
            for (uint i = 0; i < statements.size(); i++) {
              list.adoptWithCaveats(i, kj::mv(statements[i]));
            }
            return result;
          })
      ));

  auto& statement = arena.copy(p::transformWithLocation(p::sequence(tokenSequence, statementEnd),
      [](Location loc, kj::Array<Orphan<Token>>&& tokens, Orphan<Statement>&& statement) {
        auto builder = statement.get();
        auto tokensBuilder = builder.initTokens(tokens.size());
        for (uint i = 0; i < tokens.size(); i++) {
          tokensBuilder.adoptWithCaveats(i, kj::mv(tokens[i]));
        }
        builder.setStartByte(loc.begin());
        builder.setEndByte(loc.end());
        return kj::mv(statement);
      }));

  parsers.statementSequence = arena.copy(sequence(
      commentsAndWhitespace, many(sequence(statement, commentsAndWhitespace))));

  parsers.token = token;
  parsers.statement = statement;
  parsers.emptySpace = commentsAndWhitespace;
}

Lexer::~Lexer() noexcept(false) {}

}  // namespace compiler
}  // namespace capnp
