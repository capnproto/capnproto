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
#include <sstream>

// TODO(cleanup):  Rewrite this using something other than iostream?

namespace capnp {

namespace {

static const char HEXDIGITS[] = "0123456789abcdef";

static void print(std::ostream& os, const DynamicValue::Reader& value,
                  schema::Type::Body::Which which, bool alreadyParenthesized = false) {
  // Print an arbitrary message via the dynamic API by
  // iterating over the schema.  Look at the handling
  // of STRUCT in particular.

  switch (value.getType()) {
    case DynamicValue::UNKNOWN:
      os << "?";
      break;
    case DynamicValue::VOID:
      os << "void";
      break;
    case DynamicValue::BOOL:
      os << (value.as<bool>() ? "true" : "false");
      break;
    case DynamicValue::INT:
      os << value.as<int64_t>();
      break;
    case DynamicValue::UINT:
      os << value.as<uint64_t>();
      break;
    case DynamicValue::FLOAT: {
      if (which == schema::Type::Body::FLOAT32_TYPE) {
        auto buf = kj::toCharSequence(value.as<float>());
        os.write(buf.begin(), buf.size());
      } else {
        auto buf = kj::toCharSequence(value.as<double>());
        os.write(buf.begin(), buf.size());
      }
      break;
    }
    case DynamicValue::TEXT:
    case DynamicValue::DATA: {
      os << '\"';
      // TODO(someday):  Data probably shouldn't be printed as a string.
      kj::ArrayPtr<const char> chars;
      if (value.getType() == DynamicValue::DATA) {
        auto reader = value.as<Data>();
        chars = kj::arrayPtr(reinterpret_cast<const char*>(reader.begin()), reader.size());
      } else {
        chars = value.as<Text>();
      }
      for (char c: chars) {
        switch (c) {
          case '\a': os << "\\a"; break;
          case '\b': os << "\\b"; break;
          case '\f': os << "\\f"; break;
          case '\n': os << "\\n"; break;
          case '\r': os << "\\r"; break;
          case '\t': os << "\\t"; break;
          case '\v': os << "\\v"; break;
          case '\'': os << "\\\'"; break;
          case '\"': os << "\\\""; break;
          case '\\': os << "\\\\"; break;
          default:
            if (c < 0x20) {
              uint8_t c2 = c;
              os << "\\x" << HEXDIGITS[c2 / 16] << HEXDIGITS[c2 % 16];
            } else {
              os << c;
            }
            break;
        }
      }
      os << '\"';
      break;
    }
    case DynamicValue::LIST: {
      os << "[";
      bool first = true;
      auto listValue = value.as<DynamicList>();
      for (auto element: listValue) {
        if (first) {
          first = false;
        } else {
          os << ", ";
        }
        print(os, element, listValue.getSchema().whichElementType());
      }
      os << "]";
      break;
    }
    case DynamicValue::ENUM: {
      auto enumValue = value.as<DynamicEnum>();
      KJ_IF_MAYBE(enumerant, enumValue.getEnumerant()) {
        os << enumerant->getProto().getName().cStr();
      } else {
        // Unknown enum value; output raw number.
        os << enumValue.getRaw();
      }
      break;
    }
    case DynamicValue::STRUCT: {
      if (!alreadyParenthesized) os << "(";
      auto structValue = value.as<DynamicStruct>();
      bool first = true;
      for (auto member: structValue.getSchema().getMembers()) {
        if (structValue.has(member)) {
          if (first) {
            first = false;
          } else {
            os << ", ";
          }
          os << member.getProto().getName().cStr() << " = ";

          auto memberBody = member.getProto().getBody();
          switch (memberBody.which()) {
            case schema::StructNode::Member::Body::UNION_MEMBER:
              print(os, structValue.get(member), schema::Type::Body::VOID_TYPE);
              break;
            case schema::StructNode::Member::Body::FIELD_MEMBER:
              print(os, structValue.get(member),
                    memberBody.getFieldMember().getType().getBody().which());
              break;
          }
        }
      }
      if (!alreadyParenthesized) os << ")";
      break;
    }
    case DynamicValue::UNION: {
      auto unionValue = value.as<DynamicUnion>();
      KJ_IF_MAYBE(tag, unionValue.which()) {
        os << tag->getProto().getName().cStr() << "(";
        print(os, unionValue.get(),
              tag->getProto().getBody().getFieldMember().getType().getBody().which(),
              true /* alreadyParenthesized */);
        os << ")";
      } else {
        // Unknown union member; must have come from newer
        // version of the protocol.
        os << "?";
      }
      break;
    }
    case DynamicValue::INTERFACE:
      KJ_FAIL_ASSERT("Don't know how to print interfaces.") {
        break;
      }
      break;
    case DynamicValue::OBJECT:
      os << "(opaque object)";
      break;
  }
}

kj::String stringify(DynamicValue::Reader value) {
  std::stringstream out;
  print(out, value, schema::Type::Body::STRUCT_TYPE);
  auto content = out.str();
  return kj::heapString(content.data(), content.size());
}

}  // namespace

kj::String KJ_STRINGIFY(const DynamicValue::Reader& value) { return stringify(value); }
kj::String KJ_STRINGIFY(const DynamicValue::Builder& value) { return stringify(value.asReader()); }
kj::String KJ_STRINGIFY(DynamicEnum value) { return stringify(value); }
kj::String KJ_STRINGIFY(const DynamicObject& value) { return stringify(value); }
kj::String KJ_STRINGIFY(const DynamicUnion::Reader& value) { return stringify(value); }
kj::String KJ_STRINGIFY(const DynamicUnion::Builder& value) { return stringify(value.asReader()); }
kj::String KJ_STRINGIFY(const DynamicStruct::Reader& value) { return stringify(value); }
kj::String KJ_STRINGIFY(const DynamicStruct::Builder& value) { return stringify(value.asReader()); }
kj::String KJ_STRINGIFY(const DynamicList::Reader& value) { return stringify(value); }
kj::String KJ_STRINGIFY(const DynamicList::Builder& value) { return stringify(value.asReader()); }

namespace _ {  // private

kj::String structString(StructReader reader, const RawSchema& schema) {
  return stringify(DynamicStruct::Reader(StructSchema(&schema), reader));
}

kj::String unionString(StructReader reader, const RawSchema& schema, uint memberIndex) {
  return stringify(DynamicUnion::Reader(
      StructSchema(&schema).getMembers()[memberIndex].asUnion(), reader));
}

}  // namespace _ (private)

}  // namespace capnp
