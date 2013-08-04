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

#ifndef CAPNP_COMPILER_PARSER_H_
#define CAPNP_COMPILER_PARSER_H_

#include <capnp/compiler/grammar.capnp.h>
#include <capnp/compiler/lexer.capnp.h>
#include <kj/parse/common.h>
#include <kj/arena.h>
#include "error-reporter.h"

namespace capnp {
namespace compiler {

void parseFile(List<Statement>::Reader statements, ParsedFile::Builder result,
               const ErrorReporter& errorReporter);
// Parse a list of statements to build a ParsedFile.
//
// If any errors are reported, then the output is not usable.  However, it may be passed on through
// later stages of compilation in order to detect additional errors.

uint64_t generateRandomId();
// Generate a new random unique ID.  This lives here mostly for lack of a better location.

class CapnpParser {
  // Advanced parser interface.  This interface exposes the inner parsers so that you can embed
  // them into your own parsers.

public:
  CapnpParser(Orphanage orphanage, const ErrorReporter& errorReporter);
  // `orphanage` is used to allocate Cap'n Proto message objects in the result.  `inputStart` is
  // a pointer to the beginning of the input, used to compute byte offsets.

  ~CapnpParser() noexcept(false);

  KJ_DISALLOW_COPY(CapnpParser);

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

    Parser<Orphan<DeclName>> declName;
    Parser<Orphan<TypeExpression>> typeExpression;
    Parser<Orphan<ValueExpression>> valueExpression;
    Parser<Orphan<ValueExpression>> parenthesizedValueExpression;
    Parser<Orphan<Declaration::AnnotationApplication>> annotation;
    Parser<Orphan<LocatedInteger>> uid;
    Parser<Orphan<LocatedInteger>> ordinal;
    Parser<Orphan<Declaration::Method::Param>> param;

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
  const ErrorReporter& errorReporter;
  kj::Arena arena;
  Parsers parsers;
};

}  // namespace compiler
}  // namespace capnp

#endif  // CAPNP_COMPILER_PARSER_H_
