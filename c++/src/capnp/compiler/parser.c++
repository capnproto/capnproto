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

#include "parser.h"
#include "md5.h"
#include <capnp/dynamic.h>
#include <kj/debug.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace capnp {
namespace compiler {

uint64_t generateRandomId() {
  uint64_t result;

  int fd;
  KJ_SYSCALL(fd = open("/dev/urandom", O_RDONLY));

  ssize_t n;
  KJ_SYSCALL(n = read(fd, &result, sizeof(result)), "/dev/urandom");
  KJ_ASSERT(n == sizeof(result), "Incomplete read from /dev/urandom.", n);

  return result | (1ull << 63);
}

uint64_t generateChildId(uint64_t parentId, kj::StringPtr childName) {
  // Compute ID by MD5 hashing the concatenation of the parent ID and the declaration name, and
  // then taking the first 8 bytes.

  kj::byte parentIdBytes[sizeof(uint64_t)];
  for (uint i = 0; i < sizeof(uint64_t); i++) {
    parentIdBytes[i] = (parentId >> (i * 8)) & 0xff;
  }

  Md5 md5;
  md5.update(kj::arrayPtr(parentIdBytes, kj::size(parentIdBytes)));
  md5.update(childName);

  kj::ArrayPtr<const kj::byte> resultBytes = md5.finish();

  uint64_t result = 0;
  for (uint i = 0; i < sizeof(uint64_t); i++) {
    result = (result << 8) | resultBytes[i];
  }

  return result | (1ull << 63);
}

uint64_t generateGroupId(uint64_t parentId, uint16_t groupIndex) {
  // Compute ID by MD5 hashing the concatenation of the parent ID and the group index, and
  // then taking the first 8 bytes.

  kj::byte bytes[sizeof(uint64_t) + sizeof(uint16_t)];
  for (uint i = 0; i < sizeof(uint64_t); i++) {
    bytes[i] = (parentId >> (i * 8)) & 0xff;
  }
  for (uint i = 0; i < sizeof(uint16_t); i++) {
    bytes[sizeof(uint64_t) + i] = (groupIndex >> (i * 8)) & 0xff;
  }

  Md5 md5;
  md5.update(bytes);

  kj::ArrayPtr<const kj::byte> resultBytes = md5.finish();

  uint64_t result = 0;
  for (uint i = 0; i < sizeof(uint64_t); i++) {
    result = (result << 8) | resultBytes[i];
  }

  return result | (1ull << 63);
}

uint64_t generateMethodParamsId(uint64_t parentId, uint16_t methodOrdinal, bool isResults) {
  // Compute ID by MD5 hashing the concatenation of the parent ID, the method ordinal, and a
  // boolean indicating whether this is the params or the results, and then taking the first 8
  // bytes.

  kj::byte bytes[sizeof(uint64_t) + sizeof(uint16_t) + 1];
  for (uint i = 0; i < sizeof(uint64_t); i++) {
    bytes[i] = (parentId >> (i * 8)) & 0xff;
  }
  for (uint i = 0; i < sizeof(uint16_t); i++) {
    bytes[sizeof(uint64_t) + i] = (methodOrdinal >> (i * 8)) & 0xff;
  }
  bytes[sizeof(bytes) - 1] = isResults;

  Md5 md5;
  md5.update(bytes);

  kj::ArrayPtr<const kj::byte> resultBytes = md5.finish();

  uint64_t result = 0;
  for (uint i = 0; i < sizeof(uint64_t); i++) {
    result = (result << 8) | resultBytes[i];
  }

  return result | (1ull << 63);
}

void parseFile(List<Statement>::Reader statements, ParsedFile::Builder result,
               ErrorReporter& errorReporter) {
  CapnpParser parser(Orphanage::getForMessageContaining(result), errorReporter);

  kj::Vector<Orphan<Declaration>> decls(statements.size());
  kj::Vector<Orphan<Declaration::AnnotationApplication>> annotations;

  auto fileDecl = result.getRoot();
  fileDecl.setFile(VOID);

  for (auto statement: statements) {
    KJ_IF_MAYBE(decl, parser.parseStatement(statement, parser.getParsers().fileLevelDecl)) {
      Declaration::Builder builder = decl->get();
      switch (builder.which()) {
        case Declaration::NAKED_ID:
          if (fileDecl.getId().isUid()) {
            errorReporter.addError(builder.getStartByte(), builder.getEndByte(),
                                   "File can only have one ID.");
          } else {
            fileDecl.getId().adoptUid(builder.disownNakedId());
            if (builder.hasDocComment()) {
              fileDecl.adoptDocComment(builder.disownDocComment());
            }
          }
          break;
        case Declaration::NAKED_ANNOTATION:
          annotations.add(builder.disownNakedAnnotation());
          break;
        default:
          decls.add(kj::mv(*decl));
          break;
      }
    }
  }

  if (fileDecl.getId().which() != Declaration::Id::UID) {
    uint64_t id = generateRandomId();
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

template <typename T, Token::Which type, T (Token::Reader::*get)() const>
struct MatchTokenType {
  kj::Maybe<Located<T>> operator()(Token::Reader token) const {
    if (token.which() == type) {
      return Located<T>((token.*get)(), token.getStartByte(), token.getEndByte());
    } else {
      return nullptr;
    }
  }
};

#define TOKEN_TYPE_PARSER(type, discrim, getter) \
    p::transformOrReject(p::any, \
        MatchTokenType<type, Token::discrim, &Token::Reader::getter>())

constexpr auto identifier = TOKEN_TYPE_PARSER(Text::Reader, IDENTIFIER, getIdentifier);
constexpr auto stringLiteral = TOKEN_TYPE_PARSER(Text::Reader, STRING_LITERAL, getStringLiteral);
constexpr auto binaryLiteral = TOKEN_TYPE_PARSER(Data::Reader, BINARY_LITERAL, getBinaryLiteral);
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

template <typename BuilderType>
void initLocation(kj::parse::Span<List<Token>::Reader::Iterator> location,
                  BuilderType builder) {
  if (location.begin() < location.end()) {
    builder.setStartByte(location.begin()->getStartByte());
    builder.setEndByte((location.end() - 1)->getEndByte());
  }
}

}  // namespace

// =======================================================================================

CapnpParser::CapnpParser(Orphanage orphanageParam, ErrorReporter& errorReporterParam)
    : orphanage(orphanageParam), errorReporter(errorReporterParam) {
  parsers.declName = arena.copy(p::transformWithLocation(
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
      [this](kj::parse::Span<List<Token>::Reader::Iterator> location,
             Orphan<DeclName>&& result, kj::Array<Located<Text::Reader>>&& memberPath)
          -> Orphan<DeclName> {
        auto builder = result.get();
        auto pathBuilder = builder.initMemberPath(memberPath.size());
        for (size_t i = 0; i < memberPath.size(); i++) {
          memberPath[i].copyTo(pathBuilder[i]);
        }
        initLocation(location, builder);
        return kj::mv(result);
      }));

  parsers.typeExpression = arena.copy(p::transformWithLocation(
      p::sequence(parsers.declName, p::optional(
          parenthesizedList(parsers.typeExpression, errorReporter))),
      [this](kj::parse::Span<List<Token>::Reader::Iterator> location,
             Orphan<DeclName>&& name,
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
        initLocation(location, builder);
        return result;
      }));

  // Parser for a "name = value" pair.  Also matches "name = unionMember(value)",
  // "unionMember(value)" (unnamed union), and just "value" (which is not actually a valid field
  // assigment, but simplifies the parser for parenthesizedValueExpression).
  auto& fieldAssignment = arena.copy(p::transform(
      p::sequence(p::optional(p::sequence(identifier, op("="))), parsers.valueExpression),
      [this](kj::Maybe<Located<Text::Reader>>&& fieldName, Orphan<ValueExpression>&& fieldValue)
             -> Orphan<ValueExpression::FieldAssignment> {
        auto result = orphanage.newOrphan<ValueExpression::FieldAssignment>();
        auto builder = result.get();
        KJ_IF_MAYBE(fn, fieldName) {
          fn->copyTo(builder.initFieldName());
        }
        builder.adoptValue(kj::mv(fieldValue));
        return kj::mv(result);
      }));

  parsers.parenthesizedValueExpression = arena.copy(p::transform(
      parenthesizedList(fieldAssignment, errorReporter),
      [this](Located<kj::Array<kj::Maybe<Orphan<ValueExpression::FieldAssignment>>>>&& value)
          -> Orphan<ValueExpression> {
        if (value.value.size() == 1) {
          KJ_IF_MAYBE(firstVal, value.value[0]) {
            auto reader = firstVal->getReader();
            if (reader.getFieldName().getValue().size() == 0) {
              // There is only one value and it isn't an assignment, therefore the value is
              // not a struct.
              return firstVal->get().disownValue();
            }
          } else {
            // There is only one value and it failed to parse.
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.setUnknown();
            value.copyLocationTo(builder);
            return result;
          }
        }

        // If we get here, the parentheses appear to contain a list of field assignments, meaning
        // the value is a struct.

        auto result = orphanage.newOrphan<ValueExpression>();
        auto builder = result.get();
        value.copyLocationTo(builder);

        auto structBuilder = builder.initStruct(value.value.size());
        for (uint i = 0; i < value.value.size(); i++) {
          KJ_IF_MAYBE(field, value.value[i]) {
            auto reader = field->getReader();
            if (reader.getFieldName().getValue().size() > 0) {
              structBuilder.adoptWithCaveats(i, kj::mv(*field));
            } else {
              errorReporter.addErrorOn(reader.getValue(), "Missing field name.");
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
            builder.setPositiveInt(value.value);
            value.copyLocationTo(builder);
            return result;
          }),
      p::transform(p::sequence(op("-"), integerLiteral),
          [this](Located<uint64_t>&& value) -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.setNegativeInt(value.value);
            value.copyLocationTo(builder);
            return result;
          }),
      p::transform(floatLiteral,
          [this](Located<double>&& value) -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.setFloat(value.value);
            value.copyLocationTo(builder);
            return result;
          }),
      p::transform(p::sequence(op("-"), floatLiteral),
          [this](Located<double>&& value) -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.setFloat(-value.value);
            value.copyLocationTo(builder);
            return result;
          }),
      p::transformWithLocation(p::sequence(op("-"), keyword("inf")),
          [this](kj::parse::Span<List<Token>::Reader::Iterator> location)
              -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.setFloat(-kj::inf());
            initLocation(location, builder);
            return result;
          }),
      p::transform(stringLiteral,
          [this](Located<Text::Reader>&& value) -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.setString(value.value);
            value.copyLocationTo(builder);
            return result;
          }),
      p::transform(binaryLiteral,
          [this](Located<Data::Reader>&& value) -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.setBinary(value.value);
            value.copyLocationTo(builder);
            return result;
          }),
      p::transformWithLocation(parsers.declName,
          [this](kj::parse::Span<List<Token>::Reader::Iterator> location,
                 Orphan<DeclName>&& value) -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            builder.adoptName(kj::mv(value));
            initLocation(location, builder);
            return result;
          }),
      p::transform(bracketedList(parsers.valueExpression, errorReporter),
          [this](Located<kj::Array<kj::Maybe<Orphan<ValueExpression>>>>&& value)
              -> Orphan<ValueExpression> {
            auto result = orphanage.newOrphan<ValueExpression>();
            auto builder = result.get();
            auto listBuilder = builder.initList(value.value.size());
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
            auto structBuilder = builder.initStruct(value.value.size());
            for (uint i = 0; i < value.value.size(); i++) {
              KJ_IF_MAYBE(field, value.value[i]) {
                auto reader = field->get();
                if (reader.getFieldName().getValue().size() > 0) {
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

  parsers.annotation = arena.copy(p::transform(
      p::sequence(op("$"), parsers.declName, p::optional(parsers.parenthesizedValueExpression)),
      [this](Orphan<DeclName>&& name, kj::Maybe<Orphan<ValueExpression>>&& value)
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
      p::sequence(keyword("using"), p::optional(p::sequence(identifier, op("="))),
                  parsers.declName),
      [this](kj::Maybe<Located<Text::Reader>>&& name, Orphan<DeclName>&& target)
          -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        auto builder = decl.get();
        KJ_IF_MAYBE(n, name) {
          n->copyTo(builder.initName());
        } else {
          auto targetPath = target.getReader().getMemberPath();
          if (targetPath.size() == 0) {
            errorReporter.addErrorOn(
                target.getReader(), "'using' declaration without '=' must use a qualified path.");
          } else {
            builder.setName(targetPath[targetPath.size() - 1]);
          }
        }
        // no id, no annotations for using decl
        builder.initUsing().adoptTarget(kj::mv(target));
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
        auto builder =
            initDecl(decl.get(), kj::mv(name), kj::mv(id), kj::mv(annotations)).initConst();
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
        initDecl(decl.get(), kj::mv(name), kj::mv(id), kj::mv(annotations)).setEnum();
        return DeclParserResult(kj::mv(decl), parsers.enumLevelDecl);
      }));

  parsers.enumerantDecl = arena.copy(p::transform(
      p::sequence(identifier, parsers.ordinal, p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, Orphan<LocatedInteger>&& ordinal,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        initMemberDecl(decl.get(), kj::mv(name), kj::mv(ordinal), kj::mv(annotations))
            .setEnumerant();
        return DeclParserResult(kj::mv(decl));
      }));

  parsers.structDecl = arena.copy(p::transform(
      p::sequence(keyword("struct"), identifier, p::optional(parsers.uid),
                  p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, kj::Maybe<Orphan<LocatedInteger>>&& id,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        initDecl(decl.get(), kj::mv(name), kj::mv(id), kj::mv(annotations)).setStruct();
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
                .initField();
        builder.adoptType(kj::mv(type));
        KJ_IF_MAYBE(val, defaultValue) {
          builder.getDefaultValue().adoptValue(kj::mv(*val));
        } else {
          builder.getDefaultValue().setNone();
        }
        return DeclParserResult(kj::mv(decl));
      }));

  // Parse an ordinal followed by an optional colon, or no ordinal but require a colon.
  auto& ordinalOrColon = arena.copy(p::oneOf(
      p::transform(p::sequence(parsers.ordinal, p::optional(op("!")), p::optional(op(":"))),
          [this](Orphan<LocatedInteger>&& ordinal,
                 kj::Maybe<kj::Tuple<>> exclamation,
                 kj::Maybe<kj::Tuple<>> colon)
                   -> kj::Tuple<kj::Maybe<Orphan<LocatedInteger>>, bool, bool> {
            return kj::tuple(kj::mv(ordinal), exclamation == nullptr, colon == nullptr);
          }),
      p::transform(op(":"),
          []() -> kj::Tuple<kj::Maybe<Orphan<LocatedInteger>>, bool, bool> {
            return kj::tuple(nullptr, false, false);
          })));

  parsers.unionDecl = arena.copy(p::transform(
      // The first branch of this oneOf() matches named unions.  The second branch matches unnamed
      // unions and generates dummy values for the parse results.
      p::oneOf(
          p::sequence(
              identifier, ordinalOrColon,
              keyword("union"), p::many(parsers.annotation)),
          p::transformWithLocation(p::sequence(keyword("union"), p::endOfInput),
              [](kj::parse::Span<List<Token>::Reader::Iterator> location) {
                return kj::tuple(
                    Located<Text::Reader>("", location.begin()->getStartByte(),
                                          location.begin()->getEndByte()),
                    kj::Maybe<Orphan<LocatedInteger>>(nullptr),
                    false, false,
                    kj::Array<Orphan<Declaration::AnnotationApplication>>(nullptr));
              })),
      [this](Located<Text::Reader>&& name,
             kj::Maybe<Orphan<LocatedInteger>>&& ordinal,
             bool missingExclamation, bool missingColon,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        if (missingExclamation) {
          errorReporter.addErrorOn(KJ_ASSERT_NONNULL(ordinal).getReader(),
              "As of Cap'n Proto v0.3, it is no longer necessary to assign numbers to "
              "unions. However, removing the number will break binary compatibility. "
              "If this is an old protocol and you need to retain compatibility, please "
              "add an exclamation point after the number to indicate that it is really "
              "needed, e.g. `foo @1! :union {`. If this is a new protocol or compatibility "
              "doesn't matter, just remove the @n entirely. Sorry for the inconvenience, "
              "and thanks for being an early adopter!  :)");
        }
        if (missingColon) {
          errorReporter.addErrorOn(KJ_ASSERT_NONNULL(ordinal).getReader(),
              "As of Cap'n Proto v0.3, the 'union' keyword should be prefixed with a colon "
              "for named unions, e.g. `foo :union {`.");
        }

        auto decl = orphanage.newOrphan<Declaration>();
        auto builder = decl.get();
        name.copyTo(builder.initName());
        KJ_IF_MAYBE(ord, ordinal) {
          builder.getId().adoptOrdinal(kj::mv(*ord));
        } else {
          builder.getId().setUnspecified();
        }
        auto list = builder.initAnnotations(annotations.size());
        for (uint i = 0; i < annotations.size(); i++) {
          list.adoptWithCaveats(i, kj::mv(annotations[i]));
        }
        builder.setUnion();
        return DeclParserResult(kj::mv(decl), parsers.structLevelDecl);
      }));

  parsers.groupDecl = arena.copy(p::transform(
      p::sequence(identifier, op(":"), keyword("group"), p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        auto builder = decl.get();
        name.copyTo(builder.getName());
        builder.getId().setUnspecified();
        auto list = builder.initAnnotations(annotations.size());
        for (uint i = 0; i < annotations.size(); i++) {
          list.adoptWithCaveats(i, kj::mv(annotations[i]));
        }
        builder.setGroup();
        return DeclParserResult(kj::mv(decl), parsers.structLevelDecl);
      }));

  parsers.interfaceDecl = arena.copy(p::transform(
      p::sequence(keyword("interface"), identifier, p::optional(parsers.uid),
                  p::optional(p::sequence(
                      keyword("extends"), parenthesizedList(parsers.declName, errorReporter))),
                  p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, kj::Maybe<Orphan<LocatedInteger>>&& id,
             kj::Maybe<Located<kj::Array<kj::Maybe<Orphan<DeclName>>>>>&& extends,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        auto builder = initDecl(
            decl.get(), kj::mv(name), kj::mv(id), kj::mv(annotations)).initInterface();
        KJ_IF_MAYBE(e, extends) {
          auto extendsBuilder = builder.initExtends(e->value.size());
          for (uint i: kj::indices(e->value)) {
            KJ_IF_MAYBE(extend, e->value[i]) {
              extendsBuilder.adoptWithCaveats(i, kj::mv(*extend));
            }
          }
        }
        return DeclParserResult(kj::mv(decl), parsers.interfaceLevelDecl);
      }));

  parsers.param = arena.copy(p::transformWithLocation(
      p::sequence(identifier, op(":"), parsers.typeExpression,
                  p::optional(p::sequence(op("="), parsers.valueExpression)),
                  p::many(parsers.annotation)),
      [this](kj::parse::Span<List<Token>::Reader::Iterator> location,
             Located<Text::Reader>&& name, Orphan<TypeExpression>&& type,
             kj::Maybe<Orphan<ValueExpression>>&& defaultValue,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> Orphan<Declaration::Param> {
        auto result = orphanage.newOrphan<Declaration::Param>();
        auto builder = result.get();

        initLocation(location, builder);

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

  auto& paramList = arena.copy(p::oneOf(
      p::transform(parenthesizedList(parsers.param, errorReporter),
          [this](Located<kj::Array<kj::Maybe<Orphan<Declaration::Param>>>>&& params)
                -> Orphan<Declaration::ParamList> {
            auto decl = orphanage.newOrphan<Declaration::ParamList>();
            auto builder = decl.get();
            params.copyLocationTo(builder);
            auto listBuilder = builder.initNamedList(params.value.size());
            for (uint i: kj::indices(params.value)) {
              KJ_IF_MAYBE(param, params.value[i]) {
                listBuilder.adoptWithCaveats(i, kj::mv(*param));
              }
            }
            return decl;
          }),
      p::transform(parsers.declName,
          [this](Orphan<DeclName>&& name) -> Orphan<Declaration::ParamList> {
            auto decl = orphanage.newOrphan<Declaration::ParamList>();
            auto builder = decl.get();
            auto nameReader = name.getReader();
            builder.setStartByte(nameReader.getStartByte());
            builder.setEndByte(nameReader.getEndByte());
            builder.adoptType(kj::mv(name));
            return decl;
          })));

  parsers.methodDecl = arena.copy(p::transform(
      p::sequence(identifier, parsers.ordinal, paramList,
                  p::optional(p::sequence(op("->"), paramList)),
                  p::many(parsers.annotation)),
      [this](Located<Text::Reader>&& name, Orphan<LocatedInteger>&& ordinal,
             Orphan<Declaration::ParamList>&& params,
             kj::Maybe<Orphan<Declaration::ParamList>>&& results,
             kj::Array<Orphan<Declaration::AnnotationApplication>>&& annotations)
                 -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        auto builder =
            initMemberDecl(decl.get(), kj::mv(name), kj::mv(ordinal), kj::mv(annotations))
                .initMethod();

        builder.adoptParams(kj::mv(params));

        KJ_IF_MAYBE(r, results) {
          builder.getResults().adoptExplicit(kj::mv(*r));
        } else {
          builder.getResults().setNone();
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
        auto builder =
            initDecl(decl.get(), kj::mv(name), kj::mv(id), kj::mv(annotations)).initAnnotation();
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

              for (auto field: dynamicBuilder.getSchema().getFields()) {
                if (field.getProto().getName().startsWith("targets")) {
                  dynamicBuilder.set(field, true);
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
                KJ_IF_MAYBE(field, dynamicBuilder.getSchema().findFieldByName(buffer)) {
                  if (dynamicBuilder.get(*field).as<bool>()) {
                    errorReporter.addError(target->startByte, target->endByte,
                                           "Duplicate target specification.");
                  }
                  dynamicBuilder.set(*field, true);
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
        decl.get().adoptNakedId(kj::mv(value));
        return DeclParserResult(kj::mv(decl));
      }));

  auto& nakedAnnotation = arena.copy(p::transform(parsers.annotation,
      [this](Orphan<Declaration::AnnotationApplication>&& value) -> DeclParserResult {
        auto decl = orphanage.newOrphan<Declaration>();
        decl.get().adoptNakedAnnotation(kj::mv(value));
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
      parsers.unionDecl, parsers.fieldDecl, parsers.groupDecl, parsers.genericDecl));
  parsers.interfaceLevelDecl = arena.copy(p::oneOf(
      parsers.methodDecl, parsers.genericDecl));
}

CapnpParser::~CapnpParser() noexcept(false) {}

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

    switch (statement.which()) {
      case Statement::LINE:
        if (output->memberParser != nullptr) {
          errorReporter.addError(statement.getStartByte(), statement.getEndByte(),
              "This statement should end with a block, not a semicolon.");
        }
        break;

      case Statement::BLOCK:
        KJ_IF_MAYBE(memberParser, output->memberParser) {
          auto memberStatements = statement.getBlock();
          kj::Vector<Orphan<Declaration>> members(memberStatements.size());
          for (auto memberStatement: memberStatements) {
            KJ_IF_MAYBE(member, parseStatement(memberStatement, *memberParser)) {
              members.add(kj::mv(*member));
            }
          }
          builder.adoptNestedDecls(arrayToList(orphanage, members.releaseAsArray()));
        } else {
          errorReporter.addError(statement.getStartByte(), statement.getEndByte(),
              "This statement should end with a semicolon, not a block.");
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
      bestByte = statement.getStartByte();
    }

    errorReporter.addError(bestByte, bestByte, "Parse error.");
    return nullptr;
  }
}

}  // namespace compiler
}  // namespace capnp
