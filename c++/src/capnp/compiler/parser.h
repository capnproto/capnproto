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

#include <capnp/compiler/grammar.capnp.h>
#include <capnp/compiler/lexer.capnp.h>
#include <kj/parse/common.h>
#include <kj/arena.h>
#include "error-reporter.h"

CAPNP_BEGIN_HEADER

namespace capnp {
namespace compiler {

void parseFile(List<Statement>::Reader statements, ParsedFile::Builder result,
               ErrorReporter& errorReporter, bool requiresId);
// Parse a list of statements to build a ParsedFile.
//
// If any errors are reported, then the output is not usable.  However, it may be passed on through
// later stages of compilation in order to detect additional errors.

uint64_t generateRandomId();
// Generate a new random unique ID.  This lives here mostly for lack of a better location.

uint64_t generateChildId(uint64_t parentId, kj::StringPtr childName);
// Generate the ID for a child node given its parent ID and name.

uint64_t generateGroupId(uint64_t parentId, uint16_t groupIndex);
// Generate the ID for a group within a struct.

uint64_t generateMethodParamsId(uint64_t parentId, uint16_t methodOrdinal, bool isResults);
// Generate the ID for a struct representing method params / results.
//
// TODO(cleanup):  Move generate*Id() somewhere more sensible.

class CapnpParser {
  // Advanced parser interface.  This interface exposes the inner parsers so that you can embed
  // them into your own parsers.

public:
  CapnpParser(Orphanage orphanage, ErrorReporter& errorReporter);
  // `orphanage` is used to allocate Cap'n Proto message objects in the result.  `inputStart` is
  // a pointer to the beginning of the input, used to compute byte offsets.

  ~CapnpParser() noexcept(false);

  KJ_DISALLOW_COPY_AND_MOVE(CapnpParser);

  using ParserInput = kj::parse::IteratorInput<Token::Reader, List<Token>::Reader::Iterator>;
  struct DeclParserResult;
  template <typename Output>
  using Parser = kj::parse::ParserRef<ParserInput, Output>;
  using DeclParser = Parser<DeclParserResult>;

  kj::Maybe<Orphan<Declaration>> parseStatement(
      Statement::Reader statement, const DeclParser& parser);
  // Parse a statement using the given parser.  In addition to parsing the token sequence itself,
  // this takes care of parsing the block (if any) and copying over the doc comment (if any).

  struct DeclParserResult {
    // DeclParser parses a sequence of tokens representing just the "line" part of the statement --
    // i.e. everything up to the semicolon or opening curly brace.
    //
    // Use `parseStatement()` to avoid having to deal with this struct.

    Orphan<Declaration> decl;
    // The decl parsed so far.  The decl's `docComment` and `nestedDecls` are both empty at this
    // point.

    kj::Maybe<DeclParser> memberParser;
    // If null, the statement should not have a block.  If non-null, the statement should have a
    // block containing statements parseable by this parser.

    DeclParserResult(Orphan<Declaration>&& decl, const DeclParser& memberParser)
        : decl(kj::mv(decl)), memberParser(memberParser) {}
    explicit DeclParserResult(Orphan<Declaration>&& decl)
        : decl(kj::mv(decl)), memberParser(nullptr) {}
  };

  struct Parsers {
    DeclParser genericDecl;
    // Parser that matches any declaration type except those that have ordinals (since they are
    // context-dependent).

    DeclParser fileLevelDecl;
    DeclParser enumLevelDecl;
    DeclParser structLevelDecl;
    DeclParser interfaceLevelDecl;
    // Parsers that match genericDecl *and* the ordinal-based declaration types valid in the given
    // contexts.  Note that these may match declarations that are not actually allowed in the given
    // contexts, as long as the grammar is unambiguous.  E.g. nested types are not allowed in
    // enums, but they'll be accepted by enumLevelDecl.  A later stage of compilation should report
    // these as errors.

    Parser<Orphan<Expression>> expression;
    Parser<Orphan<Declaration::AnnotationApplication>> annotation;
    Parser<Orphan<LocatedInteger>> uid;
    Parser<Orphan<LocatedInteger>> ordinal;
    Parser<Orphan<Declaration::Param>> param;

    DeclParser usingDecl;
    DeclParser constDecl;
    DeclParser enumDecl;
    DeclParser enumerantDecl;
    DeclParser structDecl;
    DeclParser fieldDecl;
    DeclParser unionDecl;
    DeclParser groupDecl;
    DeclParser interfaceDecl;
    DeclParser methodDecl;
    DeclParser paramDecl;
    DeclParser annotationDecl;
    // Parsers for individual declaration types.
  };

  const Parsers& getParsers() { return parsers; }

private:
  Orphanage orphanage;
  ErrorReporter& errorReporter;
  kj::Arena arena;
  Parsers parsers;
};

kj::String expressionString(Expression::Reader name);
// Stringify the expression as code.

}  // namespace compiler
}  // namespace capnp

CAPNP_END_HEADER
