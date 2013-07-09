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

namespace {

typedef p::IteratorInput<char, const char*> Input;
typedef p::Span<const char*> Location;

void buildTokenSequenceList(List<List<TokenPointer>>::Builder builder,
                            kj::Array<kj::Array<Orphan<Token>>>&& items) {
  for (uint i = 0; i < items.size(); i++) {
    auto& item = items[i];
    auto itemBuilder = builder.init(i, item.size());
    for (uint j = 0; j < item.size(); j++) {
      itemBuilder[j].adoptToken(kj::mv(item[j]));
    }
  }
}

void attachDocComment(Statement::Builder statement, kj::Array<kj::String>&& comment) {
  size_t size = 0;
  for (auto& line: comment) {
    size += line.size() + 1;  // include newline
  }
  if (size > 0) {
    Text::Builder builder = statement.initDocComment(size);
    char* pos = builder.begin();
    for (auto& line: comment) {
      memcpy(pos, line.begin(), line.size());
      pos += line.size();
      *pos++ = '\n';
    }
    KJ_ASSERT(pos == builder.end());
  }
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

constexpr auto docComment = sequence(
    discardLineWhitespace,
    p::discard(p::optional(newline)),
    p::many(p::sequence(discardLineWhitespace, saveComment)));
// Parses a set of comment lines preceded by at most one newline and with no intervening blank
// lines.

}  // namespace

bool lex(kj::ArrayPtr<const char> input,
         LexedStatements::Builder* resultStatements,
         LexedTokens::Builder* resultTokens) {
  // This is a bit hacky.  Since the transformations applied by our parser require access to an
  // Orphanage in order to build objects, we construct the parsers as local variables.  This means
  // that all the parsers need to live in a single function scope.  In order to handle both tokens
  // and statements, we have the function take `resultStatements` and `resultTokens` and parse
  // into whichever one is non-null.
  //
  // TODO(someday):  Perhaps there should be a utility class called ParserPool which has a method
  //   that takes a parser, allocates a copy of it within some arena, then returns a ParserRef
  //   referencing that copy.  Then there could be a Lexer class which contains a ParserPool and
  //   builds all its parsers in its constructor.  This would allow the class to directly expose
  //   the parsers so that they can be used within other parser combinators.

  Orphanage orphanage = resultStatements == nullptr ?
      Orphanage::getForMessageContaining(*resultTokens) :
      Orphanage::getForMessageContaining(*resultStatements);

  auto initTok = [&](Orphan<Token>& t, const Location& loc) -> Token::Body::Builder {
    auto tb = t.get();
    tb.setStartByte(loc.begin() - input.begin());
    tb.setEndByte(loc.end() - input.begin());
    return tb.getBody();
  };

  p::ParserRef<Input, kj::Array<Orphan<Token>>> tokenSequence;

  auto commaDelimitedList = transform(
      p::sequence(tokenSequence, p::many(p::sequence(p::exactChar<','>(), tokenSequence))),
      [&](kj::Array<Orphan<Token>>&& first, kj::Array<kj::Array<Orphan<Token>>>&& rest)
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
      });

  auto token = p::oneOf(
      p::transformWithLocation(p::identifier,
          [&](Location loc, kj::String name) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            initTok(t, loc).setIdentifier(name);
            return t;
          }),
      p::transformWithLocation(p::doubleQuotedString,
          [&](Location loc, kj::String text) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            initTok(t, loc).setStringLiteral(text);
            return t;
          }),
      p::transformWithLocation(p::integer,
          [&](Location loc, uint64_t i) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            initTok(t, loc).setIntegerLiteral(i);
            return t;
          }),
      p::transformWithLocation(p::number,
          [&](Location loc, double x) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            initTok(t, loc).setFloatLiteral(x);
            return t;
          }),
      p::transformWithLocation(
          p::charsToString(p::oneOrMore(p::anyOfChars("!$%&*+-./:<=>?@^|~"))),
          [&](Location loc, kj::String text) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            initTok(t, loc).setOperator(text);
            return t;
          }),
      p::transformWithLocation(
          sequence(p::exactChar<'('>(), commaDelimitedList, p::exactChar<')'>()),
          [&](Location loc, kj::Array<kj::Array<Orphan<Token>>>&& items) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            buildTokenSequenceList(
                initTok(t, loc).initParenthesizedList(items.size()), kj::mv(items));
            return t;
          }),
      p::transformWithLocation(
          sequence(p::exactChar<'['>(), commaDelimitedList, p::exactChar<']'>()),
          [&](Location loc, kj::Array<kj::Array<Orphan<Token>>>&& items) -> Orphan<Token> {
            auto t = orphanage.newOrphan<Token>();
            buildTokenSequenceList(
                initTok(t, loc).initBracketedList(items.size()), kj::mv(items));
            return t;
          })
      );
  auto tokenSequence_ =
      sequence(commentsAndWhitespace, many(sequence(token, commentsAndWhitespace)));
  tokenSequence = tokenSequence_;

  if (resultStatements == nullptr) {
    // Only a token sequence is requested.
    Input parserInput(input.begin(), input.end());
    kj::Maybe<kj::Array<Orphan<Token>>> parseOutput = tokenSequence(parserInput);

    if (!parserInput.atEnd()) {
      return false;
    }

    KJ_IF_MAYBE(output, parseOutput) {
      auto l = resultTokens->initTokens(output->size());
      for (uint i = 0; i < output->size(); i++) {
        l[i].adoptToken(kj::mv((*output)[i]));
      }
      return true;
    } else {
      return false;
    }
  } else {
    p::ParserRef<Input, kj::Array<Orphan<Statement>>> statementSequence;

    auto statementEnd = p::oneOf(
        transform(p::sequence(p::exactChar<';'>(), docComment),
            [&](kj::Array<kj::String>&& comment) -> Orphan<Statement> {
              auto result = orphanage.newOrphan<Statement>();
              auto builder = result.get();
              attachDocComment(builder, kj::mv(comment));
              builder.getBlock().setNone();
              return result;
            }),
        transform(
            p::sequence(p::exactChar<'{'>(), docComment, statementSequence, p::exactChar<'}'>()),
            [&](kj::Array<kj::String>&& comment, kj::Array<Orphan<Statement>>&& statements)
                -> Orphan<Statement> {
              auto result = orphanage.newOrphan<Statement>();
              auto builder = result.get();
              attachDocComment(builder, kj::mv(comment));
              auto list = builder.getBlock().initStatements(statements.size());
              for (uint i = 0; i < statements.size(); i++) {
                list[i].adoptStatement(kj::mv(statements[i]));
              }
              return result;
            })
        );

    auto statement = p::transform(p::sequence(tokenSequence, statementEnd),
        [&](kj::Array<Orphan<Token>>&& tokens, Orphan<Statement>&& statement) {
          auto tokensBuilder = statement.get().initTokens(tokens.size());
          for (uint i = 0; i < tokens.size(); i++) {
            tokensBuilder[i].adoptToken(kj::mv(tokens[i]));
          }
          return kj::mv(statement);
        });

    auto statementSequence_ =
        sequence(commentsAndWhitespace, many(sequence(statement, commentsAndWhitespace)));
    statementSequence = statementSequence_;

    Input parserInput(input.begin(), input.end());
    kj::Maybe<kj::Array<Orphan<Statement>>> parseOutput = statementSequence(parserInput);

    if (!parserInput.atEnd()) {
      return false;
    }

    KJ_IF_MAYBE(output, parseOutput) {
      auto l = resultStatements->initStatements(output->size());
      for (uint i = 0; i < output->size(); i++) {
        l[i].adoptStatement(kj::mv((*output)[i]));
      }
      return true;
    } else {
      return false;
    }
  }
}

bool lex(kj::ArrayPtr<const char> input, LexedStatements::Builder result) {
  return lex(kj::mv(input), &result, nullptr);
}
bool lex(kj::ArrayPtr<const char> input, LexedTokens::Builder result) {
  return lex(kj::mv(input), nullptr, &result);
}

}  // namespace compiler
}  // namespace capnp
