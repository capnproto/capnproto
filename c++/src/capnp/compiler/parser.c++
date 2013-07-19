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

#include "parser.h"
#include <capnp/dynamic.h>
#include <kj/debug.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace capnp {
namespace compiler {

namespace {

uint64_t randomId() {
  uint64_t result;

  int fd;
  KJ_SYSCALL(fd = open("/dev/urandom", O_RDONLY));

  ssize_t n;
  KJ_SYSCALL(n = read(fd, &result, sizeof(result)), "/dev/urandom");
  KJ_ASSERT(n == sizeof(result), "Incomplete read from /dev/urandom.", n);

  return result | (1ull << 63);
}

}  // namespace

void parseFile(List<Statement>::Reader statements, ParsedFile::Builder result,
               ErrorReporter& errorReporter) {
  CapnpParser parser(Orphanage::getForMessageContaining(result), errorReporter);

  kj::Vector<Orphan<Declaration>> decls(statements.size());
  kj::Vector<Orphan<Declaration::AnnotationApplication>> annotations;

  auto fileDecl = result.getRoot();

  for (auto statement: statements) {
    KJ_IF_MAYBE(decl, parser.parseStatement(statement, parser.getParsers().fileLevelDecl)) {
      Declaration::Builder builder = decl->get();
      auto body = builder.getBody();
      switch (body.which()) {
        case Declaration::Body::NAKED_ID:
          if (fileDecl.getId().which() == Declaration::Id::UID) {
            errorReporter.addError(builder.getStartByte(), builder.getEndByte(),
                                   "File can only have one ID.");
          } else {
            fileDecl.getId().adoptUid(body.disownNakedId());
            if (builder.hasDocComment()) {
              fileDecl.adoptDocComment(builder.disownDocComment());
            }
          }
          break;
        case Declaration::Body::NAKED_ANNOTATION:
          annotations.add(body.disownNakedAnnotation());
          break;
        default:
          decls.add(kj::mv(*decl));
          break;
      }
    }
  }

  if (fileDecl.getId().which() != Declaration::Id::UID) {
    uint64_t id = randomId();
    fileDecl.getId().initUid().setValue(id);
    errorReporter.addError(0, 0,
        kj::str("File does not declare an ID.  I've generated one for you.  Add this line to your "
                "file: @0x", kj::hex(id), ";"));
  }

  auto declsBuilder = fileDecl.initNestedDecls(decls.size());
  for (size_t i = 0; i < decls.size(); i++) {
    declsBuilder.adoptWithCaveats(i, kj::mv(decls[i]));
  }

  auto annotationsBuilder = fileDecl.initAnnotations(annotations.size());
  for (size_t i = 0; i < annotations.size(); i++) {
    annotationsBuilder.adoptWithCaveats(i, kj::mv(annotations[i]));
  }
}

namespace p = kj::parse;

namespace {

// =======================================================================================

template <typename T>
struct Located {
  T value;
  uint32_t startByte;
  uint32_t endByte;

  template <typename Builder>
  void copyLocationTo(Builder builder) {
    builder.setStartByte(startByte);
    builder.setEndByte(endByte);
  }
  template <typename Builder>
  void copyTo(Builder builder) {
    builder.setValue(value);
    copyLocationTo(builder);
  }
  template <typename Result>
  Orphan<Result> asProto(Orphanage orphanage) {
    auto result = orphanage.newOrphan<Result>();
    copyTo(result.get());
    return result;
  }

  Located(const T& value, uint32_t startByte, uint32_t endByte)
      : value(value), startByte(startByte), endByte(endByte) {}
  Located(T&& value, uint32_t startByte, uint32_t endByte)
      : value(kj::mv(value)), startByte(startByte), endByte(endByte) {}
};

// =======================================================================================

template <typename T, Token::Body::Which type, T (Token::Body::Reader::*get)() const>
struct MatchTokenType {
  kj::Maybe<Located<T>> operator()(Token::Reader token) const {
    auto body = token.getBody();
    if (body.which() == type) {
      return Located<T>((body.*get)(), token.getStartByte(), token.getEndByte());
    } else {
      return nullptr;
    }
  }
};

#define TOKEN_TYPE_PARSER(type, discrim, getter) \
    p::transformOrReject(p::any, \
        MatchTokenType<type, Token::Body::discrim, &Token::Body::Reader::getter>())

constexpr auto identifier = TOKEN_TYPE_PARSER(Text::Reader, IDENTIFIER, getIdentifier);
constexpr auto stringLiteral = TOKEN_TYPE_PARSER(Text::Reader, STRING_LITERAL, getStringLiteral);
constexpr auto integerLiteral = TOKEN_TYPE_PARSER(uint64_t, INTEGER_LITERAL, getIntegerLiteral);
constexpr auto floatLiteral = TOKEN_TYPE_PARSER(double, FLOAT_LITERAL, getFloatLiteral);
constexpr auto operatorToken = TOKEN_TYPE_PARSER(Text::Reader, OPERATOR, getOperator);
constexpr auto rawParenthesizedList =
    TOKEN_TYPE_PARSER(List<List<Token>>::Reader, PARENTHESIZED_LIST, getParenthesizedList);
constexpr auto rawBracketedList =
    TOKEN_TYPE_PARSER(List<List<Token>>::Reader, BRACKETED_LIST, getBracketedList);

// =======================================================================================

class ExactString {
public:
  constexpr ExactString(const char* expected): expected(expected) {}

  kj::Maybe<kj::Tuple<>> operator()(Located<Text::Reader>&& text) const {
    if (text.value == expected) {
      return kj::Tuple<>();
    } else {
      return nullptr;
    }
  }

private:
  const char* expected;
};

constexpr auto keyword(const char* expected)
    -> decltype(p::transformOrReject(identifier, ExactString(expected))) {
  return p::transformOrReject(identifier, ExactString(expected));
}

constexpr auto op(const char* expected)
    -> decltype(p::transformOrReject(operatorToken, ExactString(expected))) {
  return p::transformOrReject(operatorToken, ExactString(expected));
}

// =======================================================================================

template <typename ItemParser>
class ParseListItems {
  // Transformer that parses all items in the input token sequence list using the given parser.

public:
  constexpr ParseListItems(ItemParser&& itemParser, ErrorReporter& errorReporter)
      : itemParser(p::sequence(kj::fwd<ItemParser>(itemParser), p::endOfInput)),
        errorReporter(errorReporter) {}

  Located<kj::Array<kj::Maybe<p::OutputType<ItemParser, CapnpParser::ParserInput>>>> operator()(
      Located<List<List<Token>>::Reader>&& items) const {
    auto result = kj::heapArray<kj::Maybe<p::OutputType<ItemParser, CapnpParser::ParserInput>>>(
        items.value.size());
    for (uint i = 0; i < items.value.size(); i++) {
      auto item = items.value[i];
      CapnpParser::ParserInput input(item.begin(), item.end());
      result[i] = itemParser(input);
      if (result[i] == nullptr) {
        // Parsing failed.  Report an error.
        auto best = input.getBest();
        if (best < item.end()) {
          // Report error from the point where parsing failed to the end of the item.
          errorReporter.addError(
              best->getStartByte(), (item.end() - 1)->getEndByte(), "Parse error.");
        } else if (item.size() > 0) {
          // The item is non-empty and the parser consumed all of it before failing.  Report an
          // error for the whole thing.
          errorReporter.addError(
              item.begin()->getStartByte(), (item.end() - 1)->getEndByte(), "Parse error.");
        } else {
          // The item has no content.
          // TODO(cleanup):  We don't actually know the item's location, so we can only report
          //   an error across the whole list.  Fix this.
          errorReporter.addError(items.startByte, items.endByte, "Parse error: Empty list item.");
        }
      }
    }
    return Located<kj::Array<kj::Maybe<p::OutputType<ItemParser, CapnpParser::ParserInput>>>>(
        kj::mv(result), items.startByte, items.endByte);
  }

private:
  decltype(p::sequence(kj::instance<ItemParser>(), p::endOfInput)) itemParser;
  ErrorReporter& errorReporter;
};

template <typename ItemParser>
constexpr auto parenthesizedList(ItemParser&& itemParser, ErrorReporter& errorReporter) -> decltype(
         transform(rawParenthesizedList, ParseListItems<ItemParser>(
             kj::fwd<ItemParser>(itemParser), errorReporter))) {
  return transform(rawParenthesizedList, ParseListItems<ItemParser>(
             kj::fwd<ItemParser>(itemParser), errorReporter));
}

template <typename ItemParser>
constexpr auto bracketedList(ItemParser&& itemParser, ErrorReporter& errorReporter) -> decltype(
         transform(rawBracketedList, ParseListItems<ItemParser>(
             kj::fwd<ItemParser>(itemParser), errorReporter))) {
  return transform(rawBracketedList, ParseListItems<ItemParser>(
             kj::fwd<ItemParser>(itemParser), errorReporter));
}

// =======================================================================================

template <typename T>
Orphan<List<T>> arrayToList(Orphanage& orphanage, kj::Array<Orphan<T>>&& elements) {
  auto result = orphanage.newOrphan<List<T>>(elements.size());
  auto builder = result.get();
  for (size_t i = 0; i < elements.size(); i++) {
    builder.adoptWithCaveats(i, kj::mv(elements[i]));
  }
  return kj::mv(result);
}

inline Declaration::Builder initDecl(
    Declaration::Builder builder, Located<Text::Reader>&& name,
    kj::Maybe<Orphan<LocatedInteger>>&& id,
    kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations) {
  name.copyTo(builder.initName());
  KJ_IF_MAYBE(i, id) {
    builder.getId().adoptUid(kj::mv(*i));
  }
  auto list = builder.initAnnotations(annotations.size());
  for (uint i = 0; i < annotations.size(); i++) {
    list.adoptWithCaveats(i, kj::mv(annotations[i]));
  }
  return builder;
}

inline Declaration::Builder initMemberDecl(
    Declaration::Builder builder, Located<Text::Reader>&& name,
    Orphan<LocatedInteger>&& ordinal,
    kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations) {
  name.copyTo(builder.initName());
  builder.getId().adoptOrdinal(kj::mv(ordinal));
  auto list = builder.initAnnotations(annotations.size());
  for (uint i = 0; i < annotations.size(); i++) {
    list.adoptWithCaveats(i, kj::mv(annotations[i]));
  }
  return builder;
}

}  // namespace

// =======================================================================================

CapnpParser::CapnpParser(Orphanage orphanageParam, ErrorReporter& errorReporterParam)
    : orphanage(orphanageParam), errorReporter(errorReporterParam) {
  parsers.declName = arena.copy(p::transform(
      p::sequence(
          p::oneOf(
              p::transform(p::sequence(keyword("import"), stringLiteral),
                  [this](Located<Text::Reader>&& filename) -> Orphan<DeclName> {
                    auto result = orphanage.newOrphan<DeclName>();
                    filename.copyTo(result.get().getBase().initImportName());
                    return result;
                  }),
              p::transform(p::sequence(op("."), identifier),
                  [this](Located<Text::Reader>&& filename) -> Orphan<DeclName> {
                    auto result = orphanage.newOrphan<DeclName>();
                    filename.copyTo(result.get().getBase().initAbsoluteName());
                    return result;
                  }),
              p::transform(identifier,
                  [this](Located<Text::Reader>&& filename) -> Orphan<DeclName> {
                    auto result = orphanage.newOrphan<DeclName>();
                    filename.copyTo(result.get().getBase().initRelativeName());
                    return result;
                  })),
          p::many(p::sequence(op("."), identifier))),
      [this](Orphan<DeclName>&& result, kj::Array<Located<Text::Reader>>&& memberPath)
          -> Orphan<DeclName> {
        auto builder = result.get().initMemberPath(memberPath.size());
        for (size_t i = 0; i < memberPath.size(); i++) {
          memberPath[i].copyTo(builder[i]);
        }
        return kj::mv(result);
      }));

  parsers.typeExpression = arena.copy(p::transform(
      p::sequence(parsers.declName, p::optional(
          parenthesizedList(parsers.typeExpression, errorReporter))),
      [this](Orphan<DeclName>&& name,
             kj::Maybe<Located<kj::Array<kj::Maybe<Orphan<TypeExpression>>>>>&& params)
             -> Orphan<TypeExpression> {
        auto result = orphanage.newOrphan<TypeExpression>();
        auto builder = result.get();
        builder.adoptName(kj::mv(name));
        KJ_IF_MAYBE(p, params) {
          auto paramsBuilder = builder.initParams(p->value.size());
          for (uint i = 0; i < p->value.size(); i++) {
            KJ_IF_MAYBE(param, p->value[i]) {
              paramsBuilder.adoptWithCaveats(i, kj::mv(*param));
            } else {
              // param failed to parse
              paramsBuilder[i].initName().getBase().initAbsoluteName().setValue("Void");
            }
          }
        }
        return result;
      }));

  auto& fieldAssignment = arena.copy(p::transform(
      p::sequence(p::optional(p::sequence(identifier, op("="))), parsers.valueExpression),
      [this](kj::Maybe<Located<Text::Reader>>&& fieldName, Orphan<ValueExpression>&& value)
          -> Orphan<ValueExpression::FieldAssignment> {
        auto result = orphanage.newOrphan<ValueExpression::FieldAssignment>();
        auto builder = result.get();
        // The field name is optional for now because this makes it easier for us to parse unions
        // later.  We'll produce an error later if the name is missing when required.
        KJ_IF_MAYBE(fn, fieldName) {
          fn->copyTo(builder.initFieldName());
        }
        builder.adoptValue(kj::mv(value));
        return result;
      }));

  parsers.parenthesizedValueExpression = arena.copy(p::transform(
      parenthesizedList(fieldAssignment, errorReporter),
      [this](Located<kj::Array<kj::Maybe<Orphan<ValueExpression::FieldAssignment>>>>&& value)
          -> Orphan<ValueExpression> {
        if (value.value.size() == 1) {
          KJ_IF_MAYBE(firstVal, value.value[0]) {
            if (!firstVal->get().hasFieldName()) {
              // There is only one value and it isn't an assignment, therefore the value is
              // not a struct.
              return firstVal->get().disownValue();
            }
          } else {
            // There is only one value and it failed to parse.
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.getBody().setUnknown();
            value.copyLocationTo(builder);
            return result;
          }
        }

        // If we get here, the parentheses appear to contain a list of field assignments, meaning
        // the value is a struct.

        auto result = orphanage.newOrphan<ValueExpression>();
        auto builder = result.get();
        value.copyLocationTo(builder);

        auto structBuilder = builder.getBody().initStructValue(value.value.size());
        for (uint i = 0; i < value.value.size(); i++) {
          KJ_IF_MAYBE(field, value.value[i]) {
            if (field->get().hasFieldName()) {
              structBuilder.adoptWithCaveats(i, kj::mv(*field));
            } else {
              auto fieldValue = field->get().getValue();
              errorReporter.addError(fieldValue.getStartByte(), fieldValue.getEndByte(),
                                     "Missing field name.");
            }
          }
        }

        return result;
      }));

  parsers.valueExpression = arena.copy(p::oneOf(
      p::transform(integerLiteral,
          [this](Located<uint64_t>&& value) -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.getBody().setPositiveInt(value.value);
            value.copyLocationTo(builder);
            return result;
          }),
      p::transform(p::sequence(op("-"), integerLiteral),
          [this](Located<uint64_t>&& value) -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.getBody().setNegativeInt(value.value);
            value.copyLocationTo(builder);
            return result;
          }),
      p::transform(floatLiteral,
          [this](Located<double>&& value) -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.getBody().setFloat(value.value);
            value.copyLocationTo(builder);
            return result;
          }),
      p::transform(p::sequence(op("-"), floatLiteral),
          [this](Located<double>&& value) -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.getBody().setFloat(-value.value);
            value.copyLocationTo(builder);
            return result;
          }),
      p::transform(stringLiteral,
          [this](Located<Text::Reader>&& value) -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.getBody().setString(value.value);
            value.copyLocationTo(builder);
            return result;
          }),
      p::transform(p::sequence(identifier, parsers.parenthesizedValueExpression),
          [this](Located<Text::Reader>&& fieldName, Orphan<ValueExpression>&& value)
              -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();

            auto builder = result.get();
            builder.setStartByte(fieldName.startByte);
            builder.setEndByte(value.get().getEndByte());

            auto unionBuilder = builder.getBody().initUnionValue();
            fieldName.copyTo(unionBuilder.initFieldName());
            unionBuilder.adoptValue(kj::mv(value));

            return result;
          }),
      p::transform(identifier,
          [this](Located<Text::Reader>&& value) -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.getBody().setIdentifier(value.value);
            value.copyLocationTo(builder);
            return result;
          }),
      p::transform(bracketedList(parsers.valueExpression, errorReporter),
          [this](Located<kj::Array<kj::Maybe<Orphan<ValueExpression>>>>&& value)
              -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            auto listBuilder = builder.getBody().initList(value.value.size());
            for (uint i = 0; i < value.value.size(); i++) {
              KJ_IF_MAYBE(element, value.value[i]) {
                listBuilder.adoptWithCaveats(i, kj::mv(*element));
              }
            }
            value.copyLocationTo(builder);
            return result;
          }),
      p::transform(parenthesizedList(fieldAssignment, errorReporter),
          [this](Located<kj::Array<kj::Maybe<Orphan<ValueExpression::FieldAssignment>>>>&& value)
              -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            auto structBuilder = builder.getBody().initStructValue(value.value.size());
            for (uint i = 0; i < value.value.size(); i++) {
              KJ_IF_MAYBE(field, value.value[i]) {
                if (field->get().hasFieldName()) {
                  structBuilder.adoptWithCaveats(i, kj::mv(*field));
                } else {
                  auto fieldValue = field->get().getValue();
                  errorReporter.addError(fieldValue.getStartByte(), fieldValue.getEndByte(),
                                         "Missing field name.");
                }
              }
            }
            value.copyLocationTo(builder);
            return result;
          })
      ));

  parsers.annotation = arena.copy(p::transformWithLocation(
      p::sequence(op("$"), parsers.declName, p::optional(parsers.parenthesizedValueExpression)),
      [this](kj::parse::Span<List<Token>::Reader::Iterator> location,
             Orphan<DeclName>&& name, kj::Maybe<Orphan<ValueExpression>>&& value)
          -> Orphan<Declaration::AnnotationApplication> {
        auto result = orphanage.newOrphan<Declaration::AnnotationApplication>();
        auto builder = result.get();
        builder.adoptName(kj::mv(name));
        KJ_IF_MAYBE(v, value) {
          builder.getValue().adoptExpression(kj::mv(*v));
        } else {
          builder.getValue().setNone();
        }
        return result;
      }));

  parsers.uid = arena.copy(p::transform(
      p::sequence(op("@"), integerLiteral),
      [this](Located<uint64_t>&& value) {
        if (value.value < (1ull << 63)) {
          errorReporter.addError(value.startByte, value.endByte,
              "Invalid ID.  Please generate a new one with 'capnpc -i'.");
        }
        return value.asProto<LocatedInteger>(orphanage);
      }));

  parsers.ordinal = arena.copy(p::transform(
      p::sequence(op("@"), integerLiteral),
      [this](Located<uint64_t>&& value) {
        if (value.value >= 65536) {
          errorReporter.addError(value.startByte, value.endByte,
              "Ordinals cannot be greater than 65535.");
        }
        return value.asProto<LocatedInteger>(orphanage);
      }));

  // -----------------------------------------------------------------

  parsers.usingDecl = arena.copy(p::transform(
      p::sequence(keyword("using"), identifier, op("="), parsers.typeExpression),
      [this](Located<Text::Reader>&& name, Orphan<TypeExpression>&& type) -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        auto builder = decl.get();
        name.copyTo(builder.initName());
        // no id, no annotations for using decl
        builder.getBody().initUsingDecl().adoptTarget(kj::mv(type));
        return DeclParserResult(kj::mv(decl));
      }));

  parsers.constDecl = arena.copy(p::transform(
      p::sequence(keyword("const"), identifier, p::optional(parsers.uid),
                  op(":"), parsers.typeExpression,
                  op("="), parsers.valueExpression,
                  p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, kj::Maybe<Orphan<LocatedInteger>>&& id,
             Orphan<TypeExpression>&& type, Orphan<ValueExpression>&& value,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        auto builder = initDecl(decl.get(), kj::mv(name), kj::mv(id), kj::mv(annotations))
            .getBody().initConstDecl();
        builder.adoptType(kj::mv(type));
        builder.adoptValue(kj::mv(value));
        return DeclParserResult(kj::mv(decl));
      }));

  parsers.enumDecl = arena.copy(p::transform(
      p::sequence(keyword("enum"), identifier, p::optional(parsers.uid),
                  p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, kj::Maybe<Orphan<LocatedInteger>>&& id,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        initDecl(decl.get(), kj::mv(name), kj::mv(id), kj::mv(annotations))
            .getBody().initEnumDecl();
        return DeclParserResult(kj::mv(decl), parsers.enumLevelDecl);
      }));

  parsers.enumerantDecl = arena.copy(p::transform(
      p::sequence(identifier, parsers.ordinal, p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, Orphan<LocatedInteger>&& ordinal,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        initMemberDecl(decl.get(), kj::mv(name), kj::mv(ordinal), kj::mv(annotations))
            .getBody().initEnumerantDecl();
        return DeclParserResult(kj::mv(decl));
      }));

  parsers.structDecl = arena.copy(p::transform(
      p::sequence(keyword("struct"), identifier, p::optional(parsers.uid),
                  p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, kj::Maybe<Orphan<LocatedInteger>>&& id,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        initDecl(decl.get(), kj::mv(name), kj::mv(id), kj::mv(annotations))
            .getBody().initStructDecl();
        return DeclParserResult(kj::mv(decl), parsers.structLevelDecl);
      }));

  parsers.fieldDecl = arena.copy(p::transform(
      p::sequence(identifier, parsers.ordinal, op(":"), parsers.typeExpression,
                  p::optional(p::sequence(op("="), parsers.valueExpression)),
                  p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, Orphan<LocatedInteger>&& ordinal,
             Orphan<TypeExpression>&& type, kj::Maybe<Orphan<ValueExpression>>&& defaultValue,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        auto builder =
            initMemberDecl(decl.get(), kj::mv(name), kj::mv(ordinal), kj::mv(annotations))
                .getBody().initFieldDecl();
        builder.adoptType(kj::mv(type));
        KJ_IF_MAYBE(val, defaultValue) {
          builder.getDefaultValue().adoptValue(kj::mv(*val));
        } else {
          builder.getDefaultValue().setNone();
        }
        return DeclParserResult(kj::mv(decl));
      }));

  parsers.unionDecl = arena.copy(p::transform(
      p::sequence(p::optional(identifier), p::optional(parsers.ordinal), keyword("union"),
                  p::many(parsers.annotation)),
      [this](kj::Maybe<Located<Text::Reader>>&& name,
             kj::Maybe<Orphan<LocatedInteger>>&& ordinal,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        auto builder = decl.get();
        KJ_IF_MAYBE(n, name) {
          n->copyTo(builder.initName());
        }
        KJ_IF_MAYBE(ord, ordinal) {
          builder.getId().adoptOrdinal(kj::mv(*ord));
        } else {
          builder.getId().setUnspecified();
        }
        auto list = builder.initAnnotations(annotations.size());
        for (uint i = 0; i < annotations.size(); i++) {
          list.adoptWithCaveats(i, kj::mv(annotations[i]));
        }
        builder.getBody().initGroupDecl();
        return DeclParserResult(kj::mv(decl), parsers.structLevelDecl);
      }));

  parsers.groupDecl = arena.copy(p::transform(
      p::sequence(keyword("group"), p::many(parsers.annotation)),
      [this](kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        auto builder = decl.get();
        builder.getId().setUnspecified();
        auto list = builder.initAnnotations(annotations.size());
        for (uint i = 0; i < annotations.size(); i++) {
          list.adoptWithCaveats(i, kj::mv(annotations[i]));
        }
        builder.getBody().initGroupDecl();
        return DeclParserResult(kj::mv(decl), parsers.structLevelDecl);
      }));

  parsers.interfaceDecl = arena.copy(p::transform(
      p::sequence(keyword("interface"), identifier, p::optional(parsers.uid),
                  p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, kj::Maybe<Orphan<LocatedInteger>>&& id,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        initDecl(decl.get(), kj::mv(name), kj::mv(id), kj::mv(annotations))
            .getBody().initInterfaceDecl();
        return DeclParserResult(kj::mv(decl), parsers.interfaceLevelDecl);
      }));

  parsers.param = arena.copy(p::transform(
      p::sequence(identifier, op(":"), parsers.typeExpression,
                  p::optional(p::sequence(op("="), parsers.valueExpression)),
                  p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, Orphan<TypeExpression>&& type,
             kj::Maybe<Orphan<ValueExpression>>&& defaultValue,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> Orphan<Declaration::Method::Param> {
        auto result = orphanage.newOrphan<Declaration::Method::Param>();
        auto builder = result.get();

        name.copyTo(builder.initName());
        builder.adoptType(kj::mv(type));
        builder.adoptAnnotations(arrayToList(orphanage, kj::mv(annotations)));
        KJ_IF_MAYBE(val, defaultValue) {
          builder.getDefaultValue().adoptValue(kj::mv(*val));
        } else {
          builder.getDefaultValue().setNone();
        }

        return kj::mv(result);
      }));

  parsers.methodDecl = arena.copy(p::transform(
      p::sequence(identifier, parsers.ordinal,
                  parenthesizedList(parsers.param, errorReporter),
                  p::optional(p::sequence(op(":"), parsers.typeExpression)),
                  p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, Orphan<LocatedInteger>&& ordinal,
             Located<kj::Array<kj::Maybe<Orphan<Declaration::Method::Param>>>>&& params,
             kj::Maybe<Orphan<TypeExpression>>&& returnType,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        auto builder =
            initMemberDecl(decl.get(), kj::mv(name), kj::mv(ordinal), kj::mv(annotations))
                .getBody().initMethodDecl();

        auto paramsBuilder = builder.initParams(params.value.size());
        for (uint i = 0; i < params.value.size(); i++) {
          KJ_IF_MAYBE(param, params.value[i]) {
            paramsBuilder.adoptWithCaveats(i, kj::mv(*param));
          }
        }

        KJ_IF_MAYBE(t, returnType) {
          builder.getReturnType().adoptExpression(kj::mv(*t));
        } else {
          builder.getReturnType().setNone();
        }
        return DeclParserResult(kj::mv(decl));
      }));

  auto& annotationTarget = arena.copy(p::oneOf(
      identifier,
      p::transformWithLocation(op("*"),
          [this](kj::parse::Span<List<Token>::Reader::Iterator> location) {
            // Hacky...
            return Located<Text::Reader>("*",
                location.begin()->getStartByte(),
                location.begin()->getEndByte());
          })));

  parsers.annotationDecl = arena.copy(p::transform(
      p::sequence(keyword("annotation"), identifier, p::optional(parsers.uid),
                  parenthesizedList(annotationTarget, errorReporter),
                  op(":"), parsers.typeExpression,
                  p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, kj::Maybe<Orphan<LocatedInteger>>&& id,
             Located<kj::Array<kj::Maybe<Located<Text::Reader>>>>&& targets,
             Orphan<TypeExpression>&& type,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        auto builder = initDecl(decl.get(), kj::mv(name), kj::mv(id), kj::mv(annotations))
            .getBody().initAnnotationDecl();
        builder.adoptType(kj::mv(type));
        DynamicStruct::Builder dynamicBuilder = builder;
        for (auto& maybeTarget: targets.value) {
          KJ_IF_MAYBE(target, maybeTarget) {
            if (target->value == "*") {
              // Set all.
              if (targets.value.size() > 1) {
                errorReporter.addError(target->startByte, target->endByte,
                    "Wildcard should not be specified together with other targets.");
              }

              for (auto member: dynamicBuilder.getSchema().getMembers()) {
                if (member.getProto().getName().startsWith("targets")) {
                  dynamicBuilder.set(member, true);
                }
              }
            } else {
              if (target->value.size() == 0 || target->value.size() >= 32 ||
                  target->value[0] < 'a' || target->value[0] > 'z') {
                errorReporter.addError(target->startByte, target->endByte,
                                       "Not a valid annotation target.");
              } else {
                char buffer[64];
                strcpy(buffer, "targets");
                strcat(buffer, target->value.cStr());
                buffer[strlen("targets")] += 'A' - 'a';
                KJ_IF_MAYBE(member, dynamicBuilder.getSchema().findMemberByName(buffer)) {
                  if (dynamicBuilder.get(*member).as<bool>()) {
                    errorReporter.addError(target->startByte, target->endByte,
                                           "Duplicate target specification.");
                  }
                  dynamicBuilder.set(*member, true);
                } else {
                  errorReporter.addError(target->startByte, target->endByte,
                                         "Not a valid annotation target.");
                }
              }
            }
          }
        }
        return DeclParserResult(kj::mv(decl));
      }));

  // -----------------------------------------------------------------

  auto& nakedId = arena.copy(p::transform(parsers.uid,
      [this](Orphan<LocatedInteger>&& value) -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        decl.get().getBody().adoptNakedId(kj::mv(value));
        return DeclParserResult(kj::mv(decl));
      }));

  auto& nakedAnnotation = arena.copy(p::transform(parsers.annotation,
      [this](Orphan<Declaration::AnnotationApplication>&& value) -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        decl.get().getBody().adoptNakedAnnotation(kj::mv(value));
        return DeclParserResult(kj::mv(decl));
      }));

  // -----------------------------------------------------------------

  parsers.genericDecl = arena.copy(p::oneOf(
      parsers.usingDecl, parsers.constDecl, parsers.annotationDecl,
      parsers.enumDecl, parsers.structDecl, parsers.interfaceDecl));
  parsers.fileLevelDecl = arena.copy(p::oneOf(
      parsers.genericDecl, nakedId, nakedAnnotation));
  parsers.enumLevelDecl = arena.copy(p::oneOf(parsers.enumerantDecl));
  parsers.structLevelDecl = arena.copy(p::oneOf(
      parsers.fieldDecl, parsers.unionDecl, parsers.groupDecl, parsers.genericDecl));
  parsers.interfaceLevelDecl = arena.copy(p::oneOf(
      parsers.methodDecl, parsers.genericDecl));
}

CapnpParser::~CapnpParser() {}

kj::Maybe<Orphan<Declaration>> CapnpParser::parseStatement(
    Statement::Reader statement, const DeclParser& parser) {
  auto fullParser = p::sequence(parser, p::endOfInput);

  auto tokens = statement.getTokens();
  ParserInput parserInput(tokens.begin(), tokens.end());

  KJ_IF_MAYBE(output, fullParser(parserInput)) {
    auto builder = output->decl.get();

    if (statement.hasDocComment()) {
      builder.setDocComment(statement.getDocComment());
    }

    builder.setStartByte(statement.getStartByte());
    builder.setEndByte(statement.getEndByte());

    switch (statement.getBlock().which()) {
      case Statement::Block::NONE:
        if (output->memberParser != nullptr) {
          errorReporter.addError(statement.getStartByte(), statement.getEndByte(),
              "This statement should end with a semicolon, not a block.");
        }
        break;

      case Statement::Block::STATEMENTS:
        KJ_IF_MAYBE(memberParser, output->memberParser) {
          auto memberStatements = statement.getBlock().getStatements();
          kj::Vector<Orphan<Declaration>> members(memberStatements.size());
          for (auto memberStatement: memberStatements) {
            KJ_IF_MAYBE(member, parseStatement(memberStatement, *memberParser)) {
              members.add(kj::mv(*member));
            }
          }
          builder.adoptNestedDecls(arrayToList(orphanage, members.releaseAsArray()));
        } else {
          errorReporter.addError(statement.getStartByte(), statement.getEndByte(),
              "This statement should end with a block, not a semicolon.");
        }
        break;
    }

    return kj::mv(output->decl);

  } else {
    // Parse error.  Figure out where to report it.
    auto best = parserInput.getBest();
    uint32_t bestByte;

    if (best != tokens.end()) {
      bestByte = best->getStartByte();
    } else if (tokens.end() != tokens.begin()) {
      bestByte = (tokens.end() - 1)->getEndByte();
    } else {
      bestByte = 0;
    }

    errorReporter.addError(bestByte, bestByte, "Parse error.");
    return nullptr;
  }
}

}  // namespace compiler
}  // namespace capnp
