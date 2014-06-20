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

#include "schema.h"
#include "message.h"
#include <kj/debug.h>

namespace capnp {

namespace _ {  // private

// Null schemas generated using the below schema file with:
//
//     capnp eval -Isrc null-schemas.capnp node --flat |
//         hexdump -v -e '8/1 "0x%02x, "' -e '1/8 "\n"'; echo
//
// I totally don't understand hexdump format strings and came up with this command based on trial
// and error.
//
//     @0x879863d4b2cc4a1e;
//
//     using Node = import "/capnp/schema.capnp".Node;
//
//     const node :Node = (
//         id = 0x0000000000000000,
//         displayName = "(null schema)");
//
//     const struct :Node = (
//         id = 0x0000000000000001,
//         displayName = "(null struct schema)",
//         struct = (
//             dataWordCount = 0,
//             pointerCount = 0,
//             preferredListEncoding = empty));
//
//     const enum :Node = (
//         id = 0x0000000000000002,
//         displayName = "(null enum schema)",
//         enum = ());
//
//     const interface :Node = (
//         id = 0x0000000000000003,
//         displayName = "(null interface schema)",
//         interface = ());
//
//     const const :Node = (
//         id = 0x0000000000000004,
//         displayName = "(null const schema)",
//         const = (type = (void = void), value = (void = void)));

static const AlignedData<13> NULL_SCHEMA_BYTES = {{
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00,   // union discriminant intentionally mangled
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0x72, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x28, 0x6e, 0x75, 0x6c, 0x6c, 0x20, 0x73, 0x63,
  0x68, 0x65, 0x6d, 0x61, 0x29, 0x00, 0x00, 0x00,
}};
const RawSchema NULL_SCHEMA = {
  0x0000000000000000, NULL_SCHEMA_BYTES.words, 13,
  nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr
};

static const AlignedData<14> NULL_STRUCT_SCHEMA_BYTES = {{
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0xaa, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x28, 0x6e, 0x75, 0x6c, 0x6c, 0x20, 0x73, 0x74,
  0x72, 0x75, 0x63, 0x74, 0x20, 0x73, 0x63, 0x68,
  0x65, 0x6d, 0x61, 0x29, 0x00, 0x00, 0x00, 0x00,
}};
const RawSchema NULL_STRUCT_SCHEMA = {
  0x0000000000000001, NULL_STRUCT_SCHEMA_BYTES.words, 14,
  nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr
};

static const AlignedData<14> NULL_ENUM_SCHEMA_BYTES = {{
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0x9a, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x28, 0x6e, 0x75, 0x6c, 0x6c, 0x20, 0x65, 0x6e,
  0x75, 0x6d, 0x20, 0x73, 0x63, 0x68, 0x65, 0x6d,
  0x61, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
}};
const RawSchema NULL_ENUM_SCHEMA = {
  0x0000000000000002, NULL_ENUM_SCHEMA_BYTES.words, 14,
  nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr
};

static const AlignedData<14> NULL_INTERFACE_SCHEMA_BYTES = {{
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0xc2, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x28, 0x6e, 0x75, 0x6c, 0x6c, 0x20, 0x69, 0x6e,
  0x74, 0x65, 0x72, 0x66, 0x61, 0x63, 0x65, 0x20,
  0x73, 0x63, 0x68, 0x65, 0x6d, 0x61, 0x29, 0x00,
}};
const RawSchema NULL_INTERFACE_SCHEMA = {
  0x0000000000000003, NULL_INTERFACE_SCHEMA_BYTES.words, 14,
  nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr
};

static const AlignedData<20> NULL_CONST_SCHEMA_BYTES = {{
  0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x05, 0x00,
  0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x00, 0x00, 0x00, 0xa2, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00,
  0x18, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00,
  0x28, 0x6e, 0x75, 0x6c, 0x6c, 0x20, 0x63, 0x6f,
  0x6e, 0x73, 0x74, 0x20, 0x73, 0x63, 0x68, 0x65,
  0x6d, 0x61, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
}};
const RawSchema NULL_CONST_SCHEMA = {
  0x0000000000000004, NULL_CONST_SCHEMA_BYTES.words, 20,
  nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr
};

}  // namespace _ (private)

// =======================================================================================

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
             getProto().getDisplayName()) {
    return StructSchema();
  }
  return StructSchema(raw);
}

EnumSchema Schema::asEnum() const {
  KJ_REQUIRE(getProto().isEnum(), "Tried to use non-enum schema as an enum.",
             getProto().getDisplayName()) {
    return EnumSchema();
  }
  return EnumSchema(raw);
}

InterfaceSchema Schema::asInterface() const {
  KJ_REQUIRE(getProto().isInterface(), "Tried to use non-interface schema as an interface.",
             getProto().getDisplayName()) {
    return InterfaceSchema();
  }
  return InterfaceSchema(raw);
}

ConstSchema Schema::asConst() const {
  KJ_REQUIRE(getProto().isConst(), "Tried to use non-constant schema as a constant.",
             getProto().getDisplayName()) {
    return ConstSchema();
  }
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
      ptr = value.getStruct().getAs<_::UncheckedMessage>();
      break;
    case schema::Value::LIST:
      ptr = value.getList().getAs<_::UncheckedMessage>();
      break;
    case schema::Value::ANY_POINTER:
      ptr = value.getAnyPointer().getAs<_::UncheckedMessage>();
      break;
    default:
      KJ_FAIL_ASSERT("getDefaultValueSchemaOffset() can only be called on struct, list, "
                     "and any-pointer fields.");
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
  uint counter = 0;
  return findMethodByName(name, counter);
}

static constexpr uint MAX_SUPERCLASSES = 64;

kj::Maybe<InterfaceSchema::Method> InterfaceSchema::findMethodByName(
    kj::StringPtr name, uint& counter) const {
  // Security:  Don't let someone DOS us with a dynamic schema containing cyclic inheritance.
  KJ_REQUIRE(counter++ < MAX_SUPERCLASSES, "Cyclic or absurdly-large inheritance graph detected.") {
    return nullptr;
  }

  auto result = findSchemaMemberByName(raw, name, getMethods());

  if (result == nullptr) {
    // Search superclasses.
    // TODO(perf):  This may be somewhat slow, and in the case of lots of diamond dependencies it
    //   could get pathological.  Arguably we should generate a flat list of transitive
    //   superclasses to search and store it in the RawSchema.  It's problematic, though, because
    //   this means that a dynamically-loaded RawSchema cannot be correctly constructed until all
    //   superclasses have been loaded, which imposes an ordering requirement on SchemaLoader or
    //   requires updating subclasses whenever a new superclass is loaded.
    for (auto extendId: getProto().getInterface().getExtends()) {
      result = getDependency(extendId).asInterface().findMethodByName(name, counter);
      if (result != nullptr) {
        break;
      }
    }
  }

  return result;
}

InterfaceSchema::Method InterfaceSchema::getMethodByName(kj::StringPtr name) const {
  KJ_IF_MAYBE(method, findMethodByName(name)) {
    return *method;
  } else {
    KJ_FAIL_REQUIRE("interface has no such method", name);
  }
}

bool InterfaceSchema::extends(InterfaceSchema other) const {
  if (other.raw == &_::NULL_INTERFACE_SCHEMA) {
    // We consider all interfaces to extend the null schema.
    return true;
  }
  uint counter = 0;
  return extends(other, counter);
}

bool InterfaceSchema::extends(InterfaceSchema other, uint& counter) const {
  // Security:  Don't let someone DOS us with a dynamic schema containing cyclic inheritance.
  KJ_REQUIRE(counter++ < MAX_SUPERCLASSES, "Cyclic or absurdly-large inheritance graph detected.") {
    return nullptr;
  }

  if (other == *this) {
    return true;
  }

  // TODO(perf):  This may be somewhat slow.  See findMethodByName() for discussion.
  for (auto extendId: getProto().getInterface().getExtends()) {
    if (getDependency(extendId).asInterface().extends(other, counter)) {
      return true;
    }
  }

  return false;
}

kj::Maybe<InterfaceSchema> InterfaceSchema::findSuperclass(uint64_t typeId) const {
  if (typeId == _::NULL_INTERFACE_SCHEMA.id) {
    // We consider all interfaces to extend the null schema.
    return InterfaceSchema();
  }
  uint counter = 0;
  return findSuperclass(typeId, counter);
}

kj::Maybe<InterfaceSchema> InterfaceSchema::findSuperclass(uint64_t typeId, uint& counter) const {
  // Security:  Don't let someone DOS us with a dynamic schema containing cyclic inheritance.
  KJ_REQUIRE(counter++ < MAX_SUPERCLASSES, "Cyclic or absurdly-large inheritance graph detected.") {
    return nullptr;
  }

  if (typeId == raw->id) {
    return *this;
  }

  // TODO(perf):  This may be somewhat slow.  See findMethodByName() for discussion.
  for (auto extendId: getProto().getInterface().getExtends()) {
    KJ_IF_MAYBE(result, getDependency(extendId).asInterface().findSuperclass(typeId, counter)) {
      return *result;
    }
  }

  return nullptr;
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

    case schema::Type::ANY_POINTER:
      KJ_FAIL_REQUIRE("List(AnyPointer) not supported.");
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

    case schema::Type::ANY_POINTER:
      KJ_FAIL_REQUIRE("List(AnyPointer) not supported.");
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
