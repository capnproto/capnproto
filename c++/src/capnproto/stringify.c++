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

#define CAPNPROTO_PRIVATE
#include "stringify.h"
#include "logging.h"
#include <sstream>

// TODO(cleanup):  Rewrite this using something other than iostream?

namespace capnproto {

namespace {

static const char HEXDIGITS[] = "0123456789abcdef";

static void print(std::ostream& os, DynamicValue::Reader value,
                  schema::Type::Body::Which which) {
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
        auto buf = STR * value.as<float>();
        os.write(buf.begin(), buf.size());
      } else {
        auto buf = STR * value.as<double>();
        os.write(buf.begin(), buf.size());
      }
      break;
    }
    case DynamicValue::TEXT:
    case DynamicValue::DATA: {
      os << '\"';
      for (char c: value.as<Data>()) {
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
      Maybe<EnumSchema::Enumerant> enumerant =
          enumValue.getEnumerant();
      if (enumerant == nullptr) {
        // Unknown enum value; output raw number.
        os << enumValue.getRaw();
      } else {
        os << enumerant->getProto().getName().c_str();
      }
      break;
    }
    case DynamicValue::STRUCT: {
      os << "(";
      auto structValue = value.as<DynamicStruct>();
      bool first = true;
      for (auto member: structValue.getSchema().getMembers()) {
        if (structValue.has(member)) {
          if (first) {
            first = false;
          } else {
            os << ", ";
          }
          os << member.getProto().getName().c_str() << " = ";

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
      os << ")";
      break;
    }
    case DynamicValue::UNION: {
      auto unionValue = value.as<DynamicUnion>();
      Maybe<StructSchema::Member> tag = unionValue.which();
      if (tag == nullptr) {
        // Unknown union member; must have come from newer
        // version of the protocol.
        os << "?";
      } else {
        os << tag->getProto().getName() << "(";
        print(os, unionValue.get(),
              tag->getProto().getBody().getFieldMember().getType().getBody().which());
        os << ")";
      }
      break;
    }
    case DynamicValue::INTERFACE:
      FAIL_RECOVERABLE_CHECK("Don't know how to print interfaces.") {}
      break;
    case DynamicValue::OBJECT:
      os << "(opaque object)";
      break;
  }
}

}  // namespace

String stringify(DynamicValue::Reader value) {
  std::stringstream out;
  print(out, value, schema::Type::Body::STRUCT_TYPE);
  auto content = out.str();
  return String(content.data(), content.size());
}

namespace internal {

String debugString(StructReader reader, const RawSchema& schema) {
  return stringify(DynamicStruct::Reader(StructSchema(&schema), reader));
}

}  // namespace internal

}  // namespace capnproto
