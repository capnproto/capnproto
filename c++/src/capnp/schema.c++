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

kj::ArrayPtr<const word> Schema::asUncheckedMessage() const {
  return kj::arrayPtr(raw->encodedNode, raw->encodedSize);
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

  KJ_FAIL_REQUIRE("Requested ID not found in dependency table.", kj::hex(id));
  return Schema();
}

StructSchema Schema::asStruct() const {
  KJ_REQUIRE(getProto().isStruct(), "Tried to use non-struct schema as a struct.",
             getProto().getDisplayName());
  return StructSchema(raw);
}

EnumSchema Schema::asEnum() const {
  KJ_REQUIRE(getProto().isEnum(), "Tried to use non-enum schema as an enum.",
             getProto().getDisplayName());
  return EnumSchema(raw);
}

InterfaceSchema Schema::asInterface() const {
  KJ_REQUIRE(getProto().isInterface(), "Tried to use non-interface schema as an interface.",
             getProto().getDisplayName());
  return InterfaceSchema(raw);
}

ConstSchema Schema::asConst() const {
  KJ_REQUIRE(getProto().isConst(), "Tried to use non-constant schema as a constant.",
             getProto().getDisplayName());
  return ConstSchema(raw);
}

kj::StringPtr Schema::getShortDisplayName() const {
  auto proto = getProto();
  return proto.getDisplayName().slice(proto.getDisplayNamePrefixLength());
}

void Schema::requireUsableAs(const _::RawSchema* expected) const {
  KJ_REQUIRE(raw == expected ||
          (raw != nullptr && expected != nullptr && raw->canCastTo == expected),
          "This schema is not compatible with the requested native type.");
}

uint32_t Schema::getSchemaOffset(const schema::Value::Reader& value) const {
  const word* ptr;

  switch (value.which()) {
    case schema::Value::TEXT:
      ptr = reinterpret_cast<const word*>(value.getText().begin());
      break;
    case schema::Value::DATA:
      ptr = reinterpret_cast<const word*>(value.getData().begin());
      break;
    case schema::Value::STRUCT:
      ptr = value.getStruct<_::UncheckedMessage>();
      break;
    case schema::Value::LIST:
      ptr = value.getList<_::UncheckedMessage>();
      break;
    case schema::Value::OBJECT:
      ptr = value.getObject<_::UncheckedMessage>();
      break;
    default:
      KJ_FAIL_ASSERT("getDefaultValueSchemaOffset() can only be called on struct, list, "
                     "and object fields.");
  }

  return ptr - raw->encodedNode;
}

// =======================================================================================

namespace {

template <typename List>
auto findSchemaMemberByName(const _::RawSchema* raw, kj::StringPtr name, List&& list)
    -> kj::Maybe<decltype(list[0])> {
  uint lower = 0;
  uint upper = raw->memberCount;
  List unnamedUnionMembers;

  while (lower < upper) {
    uint mid = (lower + upper) / 2;

    uint16_t memberIndex = raw->membersByName[mid];

    auto candidate = list[memberIndex];
    kj::StringPtr candidateName = candidate.getProto().getName();
    if (candidateName == name) {
      return candidate;
    } else if (candidateName < name) {
      lower = mid + 1;
    } else {
      upper = mid;
    }
  }

  return nullptr;
}

}  // namespace

StructSchema::FieldList StructSchema::getFields() const {
  return FieldList(*this, getProto().getStruct().getFields());
}

StructSchema::FieldSubset StructSchema::getUnionFields() const {
  auto proto = getProto().getStruct();
  return FieldSubset(*this, proto.getFields(),
                     raw->membersByDiscriminant, proto.getDiscriminantCount());
}

StructSchema::FieldSubset StructSchema::getNonUnionFields() const {
  auto proto = getProto().getStruct();
  auto fields = proto.getFields();
  auto offset = proto.getDiscriminantCount();
  auto size = fields.size() - offset;
  return FieldSubset(*this, fields, raw->membersByDiscriminant + offset, size);
}

kj::Maybe<StructSchema::Field> StructSchema::findFieldByName(kj::StringPtr name) const {
  return findSchemaMemberByName(raw, name, getFields());
}

StructSchema::Field StructSchema::getFieldByName(kj::StringPtr name) const {
  KJ_IF_MAYBE(member, findFieldByName(name)) {
    return *member;
  } else {
    KJ_FAIL_REQUIRE("struct has no such member", name);
  }
}

kj::Maybe<StructSchema::Field> StructSchema::getFieldByDiscriminant(uint16_t discriminant) const {
  auto unionFields = getUnionFields();

  if (discriminant >= unionFields.size()) {
    return nullptr;
  } else {
    return unionFields[discriminant];
  }
}

uint32_t StructSchema::Field::getDefaultValueSchemaOffset() const {
  return parent.getSchemaOffset(proto.getSlot().getDefaultValue());
}

// -------------------------------------------------------------------

EnumSchema::EnumerantList EnumSchema::getEnumerants() const {
  return EnumerantList(*this, getProto().getEnum().getEnumerants());
}

kj::Maybe<EnumSchema::Enumerant> EnumSchema::findEnumerantByName(kj::StringPtr name) const {
  return findSchemaMemberByName(raw, name, getEnumerants());
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
  return MethodList(*this, getProto().getInterface().getMethods());
}

kj::Maybe<InterfaceSchema::Method> InterfaceSchema::findMethodByName(kj::StringPtr name) const {
  return findSchemaMemberByName(raw, name, getMethods());
}

InterfaceSchema::Method InterfaceSchema::getMethodByName(kj::StringPtr name) const {
  KJ_IF_MAYBE(method, findMethodByName(name)) {
    return *method;
  } else {
    KJ_FAIL_REQUIRE("interface has no such method", name);
  }
}

// -------------------------------------------------------------------

uint32_t ConstSchema::getValueSchemaOffset() const {
  return getSchemaOffset(getProto().getConst().getValue());
}

// =======================================================================================

ListSchema ListSchema::of(schema::Type::Which primitiveType) {
  switch (primitiveType) {
    case schema::Type::VOID:
    case schema::Type::BOOL:
    case schema::Type::INT8:
    case schema::Type::INT16:
    case schema::Type::INT32:
    case schema::Type::INT64:
    case schema::Type::UINT8:
    case schema::Type::UINT16:
    case schema::Type::UINT32:
    case schema::Type::UINT64:
    case schema::Type::FLOAT32:
    case schema::Type::FLOAT64:
    case schema::Type::TEXT:
    case schema::Type::DATA:
      break;

    case schema::Type::STRUCT:
    case schema::Type::ENUM:
    case schema::Type::INTERFACE:
    case schema::Type::LIST:
      KJ_FAIL_REQUIRE("Must use one of the other ListSchema::of() overloads for complex types.");
      break;

    case schema::Type::OBJECT:
      KJ_FAIL_REQUIRE("List(Object) not supported.");
      break;
  }

  return ListSchema(primitiveType);
}

ListSchema ListSchema::of(schema::Type::Reader elementType, Schema context) {
  switch (elementType.which()) {
    case schema::Type::VOID:
    case schema::Type::BOOL:
    case schema::Type::INT8:
    case schema::Type::INT16:
    case schema::Type::INT32:
    case schema::Type::INT64:
    case schema::Type::UINT8:
    case schema::Type::UINT16:
    case schema::Type::UINT32:
    case schema::Type::UINT64:
    case schema::Type::FLOAT32:
    case schema::Type::FLOAT64:
    case schema::Type::TEXT:
    case schema::Type::DATA:
      return of(elementType.which());

    case schema::Type::STRUCT:
      return of(context.getDependency(elementType.getStruct().getTypeId()).asStruct());

    case schema::Type::ENUM:
      return of(context.getDependency(elementType.getEnum().getTypeId()).asEnum());

    case schema::Type::INTERFACE:
      return of(context.getDependency(elementType.getInterface().getTypeId()).asInterface());

    case schema::Type::LIST:
      return of(of(elementType.getList().getElementType(), context));

    case schema::Type::OBJECT:
      KJ_FAIL_REQUIRE("List(Object) not supported.");
      return ListSchema();
  }

  // Unknown type is acceptable.
  return ListSchema(elementType.which());
}

StructSchema ListSchema::getStructElementType() const {
  KJ_REQUIRE(nestingDepth == 0 && elementType == schema::Type::STRUCT,
          "ListSchema::getStructElementType(): The elements are not structs.");
  return elementSchema.asStruct();
}

EnumSchema ListSchema::getEnumElementType() const {
  KJ_REQUIRE(nestingDepth == 0 && elementType == schema::Type::ENUM,
          "ListSchema::getEnumElementType(): The elements are not enums.");
  return elementSchema.asEnum();
}

InterfaceSchema ListSchema::getInterfaceElementType() const {
  KJ_REQUIRE(nestingDepth == 0 && elementType == schema::Type::INTERFACE,
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
