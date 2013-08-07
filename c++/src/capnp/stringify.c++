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

#include "dynamic.h"
#include <kj/debug.h>
#include <kj/vector.h>

namespace capnp {

namespace {

static const char HEXDIGITS[] = "0123456789abcdef";

enum PrintMode {
  BARE,
  // The value is planned to be printed on its own line, unless it is very short and contains
  // no inner newlines.

  PREFIXED,
  // The value is planned to be printed with a prefix, like "memberName = " (a struct field).

  PARENTHESIZED
  // The value is printed in parenthesized (a union value).
};

class Indent {
public:
  explicit Indent(bool enable): amount(enable ? 1 : 0) {}

  Indent next() {
    return Indent(amount == 0 ? 0 : amount + 1);
  }

  kj::StringTree delimit(kj::Array<kj::StringTree> items, PrintMode mode) {
    if (amount == 0 || canPrintAllInline(items)) {
      return kj::StringTree(kj::mv(items), ", ");
    } else {
      char delim[amount * 2 + 3];
      delim[0] = ',';
      delim[1] = '\n';
      memset(delim + 2, ' ', amount * 2);
      delim[amount * 2 + 2] = '\0';

      // If the outer value isn't being printed on its own line, we need to add a newline/indent
      // before the first item, otherwise we only add a space on the assumption that it is preceded
      // by an open bracket or parenthesis.
      return kj::strTree(mode == BARE ? " " : delim + 1,
          kj::StringTree(kj::mv(items), kj::StringPtr(delim, amount * 2 + 2)), ' ');
    }
  }

private:
  uint amount;

  explicit Indent(uint amount): amount(amount) {}

  static constexpr size_t maxInlineValueSize = 24;

  static bool canPrintInline(const kj::StringTree& text) {
    if (text.size() > maxInlineValueSize) {
      return false;
    }

    char flat[maxInlineValueSize + 1];
    text.flattenTo(flat);
    flat[text.size()] = '\0';
    if (strchr(flat, '\n') != nullptr) {
      return false;
    }

    return true;
  }

  static bool canPrintAllInline(const kj::Array<kj::StringTree>& items) {
    for (auto& item: items) {
      if (!canPrintInline(item)) return false;
    }
    return true;
  }
};

schema::Type::Body::Which whichMemberType(const StructSchema::Member& member) {
  auto body = member.getProto().getBody();
  switch (body.which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      return schema::Type::Body::VOID_TYPE;
    case schema::StructNode::Member::Body::GROUP_MEMBER:
      return schema::Type::Body::STRUCT_TYPE;
    case schema::StructNode::Member::Body::FIELD_MEMBER:
      return body.getFieldMember().getType().getBody().which();
  }
  KJ_UNREACHABLE;
}

static kj::StringTree print(const DynamicValue::Reader& value,
                            schema::Type::Body::Which which, Indent indent,
                            PrintMode mode) {
  switch (value.getType()) {
    case DynamicValue::UNKNOWN:
      return kj::strTree("?");
    case DynamicValue::VOID:
      return kj::strTree("void");
    case DynamicValue::BOOL:
      return kj::strTree(value.as<bool>() ? "true" : "false");
    case DynamicValue::INT:
      return kj::strTree(value.as<int64_t>());
    case DynamicValue::UINT:
      return kj::strTree(value.as<uint64_t>());
    case DynamicValue::FLOAT:
      if (which == schema::Type::Body::FLOAT32_TYPE) {
        return kj::strTree(value.as<float>());
      } else {
        return kj::strTree(value.as<double>());
      }
    case DynamicValue::TEXT:
    case DynamicValue::DATA: {
      // TODO(someday):  Data probably shouldn't be printed as a string.
      kj::ArrayPtr<const char> chars;
      if (value.getType() == DynamicValue::DATA) {
        auto reader = value.as<Data>();
        chars = kj::arrayPtr(reinterpret_cast<const char*>(reader.begin()), reader.size());
      } else {
        chars = value.as<Text>();
      }

      kj::Vector<char> escaped(chars.size());

      for (char c: chars) {
        switch (c) {
          case '\a': escaped.addAll(kj::StringPtr("\\a")); break;
          case '\b': escaped.addAll(kj::StringPtr("\\b")); break;
          case '\f': escaped.addAll(kj::StringPtr("\\f")); break;
          case '\n': escaped.addAll(kj::StringPtr("\\n")); break;
          case '\r': escaped.addAll(kj::StringPtr("\\r")); break;
          case '\t': escaped.addAll(kj::StringPtr("\\t")); break;
          case '\v': escaped.addAll(kj::StringPtr("\\v")); break;
          case '\'': escaped.addAll(kj::StringPtr("\\\'")); break;
          case '\"': escaped.addAll(kj::StringPtr("\\\"")); break;
          case '\\': escaped.addAll(kj::StringPtr("\\\\")); break;
          default:
            if (c < 0x20) {
              escaped.add('\\');
              escaped.add('x');
              uint8_t c2 = c;
              escaped.add(HEXDIGITS[c2 / 16]);
              escaped.add(HEXDIGITS[c2 % 16]);
            } else {
              escaped.add(c);
            }
            break;
        }
      }
      return kj::strTree('"', escaped, '"');
    }
    case DynamicValue::LIST: {
      auto listValue = value.as<DynamicList>();
      auto which = listValue.getSchema().whichElementType();
      kj::Array<kj::StringTree> elements = KJ_MAP(listValue, element) {
        return print(element, which, indent.next(), BARE);
      };
      return kj::strTree('[', indent.delimit(kj::mv(elements), mode), ']');
    }
    case DynamicValue::ENUM: {
      auto enumValue = value.as<DynamicEnum>();
      KJ_IF_MAYBE(enumerant, enumValue.getEnumerant()) {
        return kj::strTree(enumerant->getProto().getName());
      } else {
        // Unknown enum value; output raw number.
        return kj::strTree(enumValue.getRaw());
      }
      break;
    }
    case DynamicValue::STRUCT: {
      auto structValue = value.as<DynamicStruct>();
      auto memberSchemas = structValue.getSchema().getMembers();

      kj::Vector<kj::StringTree> printedMembers(memberSchemas.size());
      for (auto member: memberSchemas) {
        if (structValue.has(member)) {
          printedMembers.add(kj::strTree(
              member.getProto().getName(), " = ",
              print(structValue.get(member), whichMemberType(member), indent.next(), PREFIXED)));
        }
      }

      if (mode == PARENTHESIZED) {
        return indent.delimit(printedMembers.releaseAsArray(), mode);
      } else {
        return kj::strTree('(', indent.delimit(printedMembers.releaseAsArray(), mode), ')');
      }
    }
    case DynamicValue::UNION: {
      auto unionValue = value.as<DynamicUnion>();
      KJ_IF_MAYBE(tag, unionValue.which()) {
        return kj::strTree(
            tag->getProto().getName(), '(',
            print(unionValue.get(), whichMemberType(*tag), indent, PARENTHESIZED), ')');
      } else {
        // Unknown union member; must have come from newer
        // version of the protocol.
        return kj::strTree("<unknown union member>");
      }
    }
    case DynamicValue::INTERFACE:
      KJ_FAIL_ASSERT("Don't know how to print interfaces.") {
        return kj::String();
      }
    case DynamicValue::OBJECT:
      return kj::strTree("<opaque object>");
  }

  KJ_UNREACHABLE;
}

kj::StringTree stringify(DynamicValue::Reader value) {
  return print(value, schema::Type::Body::STRUCT_TYPE, Indent(false), BARE);
}

}  // namespace

kj::StringTree prettyPrint(DynamicStruct::Reader value) {
  return print(value, schema::Type::Body::STRUCT_TYPE, Indent(true), BARE);
}

kj::StringTree prettyPrint(DynamicList::Reader value) {
  return print(value, schema::Type::Body::LIST_TYPE, Indent(true), BARE);
}

kj::StringTree prettyPrint(DynamicStruct::Builder value) { return prettyPrint(value.asReader()); }
kj::StringTree prettyPrint(DynamicList::Builder value) { return prettyPrint(value.asReader()); }

kj::StringTree KJ_STRINGIFY(const DynamicValue::Reader& value) { return stringify(value); }
kj::StringTree KJ_STRINGIFY(const DynamicValue::Builder& value) { return stringify(value.asReader()); }
kj::StringTree KJ_STRINGIFY(DynamicEnum value) { return stringify(value); }
kj::StringTree KJ_STRINGIFY(const DynamicObject& value) { return stringify(value); }
kj::StringTree KJ_STRINGIFY(const DynamicUnion::Reader& value) { return stringify(value); }
kj::StringTree KJ_STRINGIFY(const DynamicUnion::Builder& value) { return stringify(value.asReader()); }
kj::StringTree KJ_STRINGIFY(const DynamicStruct::Reader& value) { return stringify(value); }
kj::StringTree KJ_STRINGIFY(const DynamicStruct::Builder& value) { return stringify(value.asReader()); }
kj::StringTree KJ_STRINGIFY(const DynamicList::Reader& value) { return stringify(value); }
kj::StringTree KJ_STRINGIFY(const DynamicList::Builder& value) { return stringify(value.asReader()); }

namespace _ {  // private

kj::StringTree structString(StructReader reader, const RawSchema& schema) {
  return stringify(DynamicStruct::Reader(StructSchema(&schema), reader));
}

kj::StringTree unionString(StructReader reader, const RawSchema& schema, uint memberIndex) {
  return stringify(DynamicUnion::Reader(
      StructSchema(&schema).getMembers()[memberIndex].asUnion(), reader));
}

}  // namespace _ (private)

}  // namespace capnp
