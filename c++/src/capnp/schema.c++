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

#include "schema.h"
#include "message.h"
#include <kj/debug.h>

namespace capnp {

schema::Node::Reader Schema::getProto() const {
  return readMessageUnchecked<schema::Node>(raw->encodedNode);
}

Schema Schema::getDependency(uint64_t id) const {
  uint lower = 0;
  uint upper = raw->dependencyCount;

  while (lower < upper) {
    uint mid = (lower + upper) / 2;

    const _::RawSchema* candidate = raw->dependencies[mid];

    uint64_t candidateId = candidate->id;
    if (candidateId == id) {
      candidate->ensureInitialized();
      return Schema(candidate);
    } else if (candidateId < id) {
      lower = mid + 1;
    } else {
      upper = mid;
    }
  }

  KJ_FAIL_REQUIRE("Requested ID not found in dependency table.", id);
  return Schema();
}

StructSchema Schema::asStruct() const {
  KJ_REQUIRE(getProto().getBody().which() == schema::Node::Body::STRUCT_NODE,
          "Tried to use non-struct schema as a struct.",
          getProto().getDisplayName());
  return StructSchema(raw);
}

EnumSchema Schema::asEnum() const {
  KJ_REQUIRE(getProto().getBody().which() == schema::Node::Body::ENUM_NODE,
          "Tried to use non-enum schema as an enum.",
          getProto().getDisplayName());
  return EnumSchema(raw);
}

InterfaceSchema Schema::asInterface() const {
  KJ_REQUIRE(getProto().getBody().which() == schema::Node::Body::INTERFACE_NODE,
          "Tried to use non-interface schema as an interface.",
          getProto().getDisplayName());
  return InterfaceSchema(raw);
}

void Schema::requireUsableAs(const _::RawSchema* expected) const {
  KJ_REQUIRE(raw == expected ||
          (raw != nullptr && expected != nullptr && raw->canCastTo == expected),
          "This schema is not compatible with the requested native type.");
}

// =======================================================================================

namespace {

template <typename List>
auto findSchemaMemberByName(const _::RawSchema* raw, kj::StringPtr name,
                            uint unionIndex, List&& list)
    -> kj::Maybe<kj::Decay<decltype(list[0])>> {
  uint lower = 0;
  uint upper = raw->memberCount;

  while (lower < upper) {
    uint mid = (lower + upper) / 2;

    const _::RawSchema::MemberInfo& member = raw->membersByName[mid];

    if (member.unionIndex == unionIndex) {
      auto candidate = list[member.index];
      kj::StringPtr candidateName = candidate.getProto().getName();
      if (candidateName == name) {
        return candidate;
      } else if (candidateName < name) {
        lower = mid + 1;
      } else {
        upper = mid;
      }
    } else if (member.unionIndex < unionIndex) {
      lower = mid + 1;
    } else {
      upper = mid;
    }
  }

  return nullptr;
}

}  // namespace

StructSchema::MemberList StructSchema::getMembers() const {
  return MemberList(*this, 0, getProto().getBody().getStructNode().getMembers());
}

kj::Maybe<StructSchema::Member> StructSchema::findMemberByName(kj::StringPtr name) const {
  return findSchemaMemberByName(raw, name, 0, getMembers());
}

StructSchema::Member StructSchema::getMemberByName(kj::StringPtr name) const {
  KJ_IF_MAYBE(member, findMemberByName(name)) {
    return *member;
  } else {
    KJ_FAIL_REQUIRE("struct has no such member", name);
  }
}

kj::Maybe<StructSchema::Union> StructSchema::Member::getContainingUnion() const {
  if (unionIndex == 0) return nullptr;
  return parent.getMembers()[unionIndex - 1].asUnion();
}

StructSchema::Union StructSchema::Member::asUnion() const {
  KJ_REQUIRE(proto.getBody().which() == schema::StructNode::Member::Body::UNION_MEMBER,
          "Tried to use non-union struct member as a union.",
          parent.getProto().getDisplayName(), proto.getName());
  return Union(*this);
}

StructSchema::MemberList StructSchema::Union::getMembers() const {
  return MemberList(parent, index + 1, proto.getBody().getUnionMember().getMembers());
}

kj::Maybe<StructSchema::Member> StructSchema::Union::findMemberByName(kj::StringPtr name) const {
  return findSchemaMemberByName(parent.raw, name, index + 1, getMembers());
}

StructSchema::Member StructSchema::Union::getMemberByName(kj::StringPtr name) const {
  KJ_IF_MAYBE(member, findMemberByName(name)) {
    return *member;
  } else {
    KJ_FAIL_REQUIRE("union has no such member", name);
  }
}

// -------------------------------------------------------------------

EnumSchema::EnumerantList EnumSchema::getEnumerants() const {
  return EnumerantList(*this, getProto().getBody().getEnumNode().getEnumerants());
}

kj::Maybe<EnumSchema::Enumerant> EnumSchema::findEnumerantByName(kj::StringPtr name) const {
  return findSchemaMemberByName(raw, name, 0, getEnumerants());
}

EnumSchema::Enumerant EnumSchema::getEnumerantByName(kj::StringPtr name) const {
  KJ_IF_MAYBE(enumerant, findEnumerantByName(name)) {
    return *enumerant;
  } else {
    KJ_FAIL_REQUIRE("enum has no such enumerant", name);
  }
}

// -------------------------------------------------------------------

InterfaceSchema::MethodList InterfaceSchema::getMethods() const {
  return MethodList(*this, getProto().getBody().getInterfaceNode().getMethods());
}

kj::Maybe<InterfaceSchema::Method> InterfaceSchema::findMethodByName(kj::StringPtr name) const {
  return findSchemaMemberByName(raw, name, 0, getMethods());
}

InterfaceSchema::Method InterfaceSchema::getMethodByName(kj::StringPtr name) const {
  KJ_IF_MAYBE(method, findMethodByName(name)) {
    return *method;
  } else {
    KJ_FAIL_REQUIRE("interface has no such method", name);
  }
}

// =======================================================================================

ListSchema ListSchema::of(schema::Type::Body::Which primitiveType) {
  switch (primitiveType) {
    case schema::Type::Body::VOID_TYPE:
    case schema::Type::Body::BOOL_TYPE:
    case schema::Type::Body::INT8_TYPE:
    case schema::Type::Body::INT16_TYPE:
    case schema::Type::Body::INT32_TYPE:
    case schema::Type::Body::INT64_TYPE:
    case schema::Type::Body::UINT8_TYPE:
    case schema::Type::Body::UINT16_TYPE:
    case schema::Type::Body::UINT32_TYPE:
    case schema::Type::Body::UINT64_TYPE:
    case schema::Type::Body::FLOAT32_TYPE:
    case schema::Type::Body::FLOAT64_TYPE:
    case schema::Type::Body::TEXT_TYPE:
    case schema::Type::Body::DATA_TYPE:
      break;

    case schema::Type::Body::STRUCT_TYPE:
    case schema::Type::Body::ENUM_TYPE:
    case schema::Type::Body::INTERFACE_TYPE:
    case schema::Type::Body::LIST_TYPE:
      KJ_FAIL_REQUIRE("Must use one of the other ListSchema::of() overloads for complex types.");
      break;

    case schema::Type::Body::OBJECT_TYPE:
      KJ_FAIL_REQUIRE("List(Object) not supported.");
      break;
  }

  return ListSchema(primitiveType);
}

ListSchema ListSchema::of(schema::Type::Reader elementType, Schema context) {
  auto body = elementType.getBody();
  switch (body.which()) {
    case schema::Type::Body::VOID_TYPE:
    case schema::Type::Body::BOOL_TYPE:
    case schema::Type::Body::INT8_TYPE:
    case schema::Type::Body::INT16_TYPE:
    case schema::Type::Body::INT32_TYPE:
    case schema::Type::Body::INT64_TYPE:
    case schema::Type::Body::UINT8_TYPE:
    case schema::Type::Body::UINT16_TYPE:
    case schema::Type::Body::UINT32_TYPE:
    case schema::Type::Body::UINT64_TYPE:
    case schema::Type::Body::FLOAT32_TYPE:
    case schema::Type::Body::FLOAT64_TYPE:
    case schema::Type::Body::TEXT_TYPE:
    case schema::Type::Body::DATA_TYPE:
      return of(body.which());

    case schema::Type::Body::STRUCT_TYPE:
      return of(context.getDependency(body.getStructType()).asStruct());

    case schema::Type::Body::ENUM_TYPE:
      return of(context.getDependency(body.getEnumType()).asEnum());

    case schema::Type::Body::INTERFACE_TYPE:
      return of(context.getDependency(body.getInterfaceType()).asInterface());

    case schema::Type::Body::LIST_TYPE:
      return of(of(body.getListType(), context));

    case schema::Type::Body::OBJECT_TYPE:
      KJ_FAIL_REQUIRE("List(Object) not supported.");
      return ListSchema();
  }

  // Unknown type is acceptable.
  return ListSchema(body.which());
}

StructSchema ListSchema::getStructElementType() const {
  KJ_REQUIRE(nestingDepth == 0 && elementType == schema::Type::Body::STRUCT_TYPE,
          "ListSchema::getStructElementType(): The elements are not structs.");
  return elementSchema.asStruct();
}

EnumSchema ListSchema::getEnumElementType() const {
  KJ_REQUIRE(nestingDepth == 0 && elementType == schema::Type::Body::ENUM_TYPE,
          "ListSchema::getEnumElementType(): The elements are not enums.");
  return elementSchema.asEnum();
}

InterfaceSchema ListSchema::getInterfaceElementType() const {
  KJ_REQUIRE(nestingDepth == 0 && elementType == schema::Type::Body::INTERFACE_TYPE,
          "ListSchema::getInterfaceElementType(): The elements are not interfaces.");
  return elementSchema.asInterface();
}

ListSchema ListSchema::getListElementType() const {
  KJ_REQUIRE(nestingDepth > 0,
          "ListSchema::getListElementType(): The elements are not lists.");
  return ListSchema(elementType, nestingDepth - 1, elementSchema);
}

void ListSchema::requireUsableAs(ListSchema expected) const {
  KJ_REQUIRE(elementType == expected.elementType && nestingDepth == expected.nestingDepth,
          "This schema is not compatible with the requested native type.");
  elementSchema.requireUsableAs(expected.elementSchema.raw);
}

}  // namespace capnp
