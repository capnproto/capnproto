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

Token::Body::Builder initTok(Orphan<Token>& t, const Location& loc) {
  auto tb = t.get();
  tb.setStartByte(loc.begin());
  tb.setEndByte(loc.end());
  return tb.getBody();
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

constexpr auto commentsAndWhitespace =
    sequence(p::discardWhitespace,
             p::discard(p::many(sequence(discardComment, p::discardWhitespace))));

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

Lexer::Lexer(Orphanage orphanageParam, ErrorReporter& errorReporterParam)
    : orphanage(orphanageParam), errorReporter(errorReporterParam) {

  // Note that because passing an lvalue to a parser constructor uses it by-referencee, it's safe
  // for us to use parsers.tokenSequence even though we haven't yet constructed it.
  auto& tokenSequence = parsers.tokenSequence;

  auto& commaDelimitedList = arena.copy(p::transform(
      p::sequence(tokenSequence, p::many(p::sequence(p::exactChar<','>(), tokenSequence))),
      [this](kj::Array<Orphan<Token>>&& first, kj::Array<kj::Array<Orphan<Token>>>&& rest)
          -> kj::Array<kj::Array<Orphan<Token>>> {
        if (first == nullptr && rest == nullptr) {
          // Completely empty list.
          return nullptr;
        } else {
          auto result = kj::heapArrayBuilder<kj::Array<Orphan<Token>>>(rest.size() + 1);
          result.add(kj::mv(first));
          for (auto& item: rest) {
            result.add(kj::mv(item));
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
          })
      ));
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
            builder.getBlock().setNone();
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
            auto list = builder.getBlock().initStatements(statements.size());
            for (uint i = 0; i < statements.size(); i++) {
              list.adoptWithCaveats(i, kj::mv(statements[i]));
            }
            return result;
          })
      ));

  auto& statement = arena.copy(p::transformWithLocation(p::sequence(tokenSequence, statementEnd),
      [this](Location loc, kj::Array<Orphan<Token>>&& tokens, Orphan<Statement>&& statement) {
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

Lexer::~Lexer() {}

}  // namespace compiler
}  // namespace capnp
