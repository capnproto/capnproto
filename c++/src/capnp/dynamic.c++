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

namespace capnp {

namespace {

template <typename T, typename U>
KJ_ALWAYS_INLINE(T bitCast(U value));

template <typename T, typename U>
inline T bitCast(U value) {
  static_assert(sizeof(T) == sizeof(U), "Size must match.");
  return value;
}
//template <>
//inline float bitCast<float, uint32_t>(uint32_t value) {
//  float result;
//  memcpy(&result, &value, sizeof(value));
//  return result;
//}
//template <>
//inline double bitCast<double, uint64_t>(uint64_t value) {
//  double result;
//  memcpy(&result, &value, sizeof(value));
//  return result;
//}
template <>
inline uint32_t bitCast<uint32_t, float>(float value) {
  uint32_t result;
  memcpy(&result, &value, sizeof(value));
  return result;
}
template <>
inline uint64_t bitCast<uint64_t, double>(double value) {
  uint64_t result;
  memcpy(&result, &value, sizeof(value));
  return result;
}

_::FieldSize elementSizeFor(schema::Type::Body::Which elementType) {
  switch (elementType) {
    case schema::Type::Body::VOID_TYPE: return _::FieldSize::VOID;
    case schema::Type::Body::BOOL_TYPE: return _::FieldSize::BIT;
    case schema::Type::Body::INT8_TYPE: return _::FieldSize::BYTE;
    case schema::Type::Body::INT16_TYPE: return _::FieldSize::TWO_BYTES;
    case schema::Type::Body::INT32_TYPE: return _::FieldSize::FOUR_BYTES;
    case schema::Type::Body::INT64_TYPE: return _::FieldSize::EIGHT_BYTES;
    case schema::Type::Body::UINT8_TYPE: return _::FieldSize::BYTE;
    case schema::Type::Body::UINT16_TYPE: return _::FieldSize::TWO_BYTES;
    case schema::Type::Body::UINT32_TYPE: return _::FieldSize::FOUR_BYTES;
    case schema::Type::Body::UINT64_TYPE: return _::FieldSize::EIGHT_BYTES;
    case schema::Type::Body::FLOAT32_TYPE: return _::FieldSize::FOUR_BYTES;
    case schema::Type::Body::FLOAT64_TYPE: return _::FieldSize::EIGHT_BYTES;

    case schema::Type::Body::TEXT_TYPE: return _::FieldSize::POINTER;
    case schema::Type::Body::DATA_TYPE: return _::FieldSize::POINTER;
    case schema::Type::Body::LIST_TYPE: return _::FieldSize::POINTER;
    case schema::Type::Body::ENUM_TYPE: return _::FieldSize::TWO_BYTES;
    case schema::Type::Body::STRUCT_TYPE: return _::FieldSize::INLINE_COMPOSITE;
    case schema::Type::Body::INTERFACE_TYPE: return _::FieldSize::POINTER;
    case schema::Type::Body::OBJECT_TYPE: KJ_FAIL_ASSERT("List(Object) not supported."); break;
  }

  // Unknown type.  Treat it as zero-size.
  return _::FieldSize::VOID;
}

inline _::StructSize structSizeFromSchema(StructSchema schema) {
  auto node = schema.getProto().getBody().getStructNode();
  return _::StructSize(
      node.getDataSectionWordSize() * WORDS,
      node.getPointerSectionSize() * POINTERS,
      static_cast<_::FieldSize>(node.getPreferredListEncoding()));
}

}  // namespace

// =======================================================================================

kj::Maybe<EnumSchema::Enumerant> DynamicEnum::getEnumerant() const {
  auto enumerants = schema.getEnumerants();
  if (value < enumerants.size()) {
    return enumerants[value];
  } else {
    return nullptr;
  }
}

uint16_t DynamicEnum::asImpl(uint64_t requestedTypeId) const {
  KJ_REQUIRE(requestedTypeId == schema.getProto().getId(),
             "Type mismatch in DynamicEnum.as().") {
    // use it anyway
    break;
  }
  return value;
}

// =======================================================================================

DynamicStruct::Reader DynamicObject::as(StructSchema schema) const {
  if (reader.kind == _::ObjectKind::NULL_POINTER) {
    return DynamicStruct::Reader(schema, _::StructReader());
  }
  KJ_REQUIRE(reader.kind == _::ObjectKind::STRUCT, "Object is not a struct.") {
    // Return default struct.
    return DynamicStruct::Reader(schema, _::StructReader());
  }
  return DynamicStruct::Reader(schema, reader.structReader);
}

DynamicList::Reader DynamicObject::as(ListSchema schema) const {
  if (reader.kind == _::ObjectKind::NULL_POINTER) {
    return DynamicList::Reader(schema, _::ListReader());
  }
  KJ_REQUIRE(reader.kind == _::ObjectKind::LIST, "Object is not a list.") {
    // Return empty list.
    return DynamicList::Reader(schema, _::ListReader());
  }
  return DynamicList::Reader(schema, reader.listReader);
}

// =======================================================================================

kj::Maybe<StructSchema::Member> DynamicUnion::Reader::which() const {
  auto members = schema.getMembers();
  uint16_t discrim = reader.getDataField<uint16_t>(
      schema.getProto().getBody().getUnionMember().getDiscriminantOffset() * ELEMENTS);

  if (discrim < members.size()) {
    return members[discrim];
  } else {
    return nullptr;
  }
}
kj::Maybe<StructSchema::Member> DynamicUnion::Builder::which() {
  auto members = schema.getMembers();
  uint16_t discrim = builder.getDataField<uint16_t>(
      schema.getProto().getBody().getUnionMember().getDiscriminantOffset() * ELEMENTS);

  if (discrim < members.size()) {
    return members[discrim];
  } else {
    return nullptr;
  }
}

DynamicValue::Reader DynamicUnion::Reader::get() const {
  KJ_IF_MAYBE(w, which()) {
    return DynamicValue::Reader(DynamicStruct::Reader::getImpl(reader, *w));
  } else {
    return nullptr;
  }
}

DynamicValue::Builder DynamicUnion::Builder::get() {
  KJ_IF_MAYBE(w, which()) {
    return DynamicValue::Builder(DynamicStruct::Builder::getImpl(builder, *w));
  } else {
    return nullptr;
  }
}

void DynamicUnion::Builder::set(StructSchema::Member member, const DynamicValue::Reader& value) {
  setDiscriminant(member);
  DynamicStruct::Builder::setImpl(builder, member, value);
}

DynamicValue::Builder DynamicUnion::Builder::init(StructSchema::Member member) {
  setDiscriminant(member);
  return DynamicStruct::Builder::initImpl(builder, member);
}

DynamicValue::Builder DynamicUnion::Builder::init(StructSchema::Member member, uint size) {
  setDiscriminant(member);
  return DynamicStruct::Builder::initImpl(builder, member, size);
}

DynamicStruct::Builder DynamicUnion::Builder::getObject(StructSchema schema) {
  return DynamicStruct::Builder::getObjectImpl(builder, checkIsObject(), schema);
}
DynamicList::Builder DynamicUnion::Builder::getObject(ListSchema schema) {
  return DynamicStruct::Builder::getObjectImpl(builder, checkIsObject(), schema);
}
Text::Builder DynamicUnion::Builder::getObjectAsText() {
  return DynamicStruct::Builder::getObjectAsTextImpl(builder, checkIsObject());
}
Data::Builder DynamicUnion::Builder::getObjectAsData() {
  return DynamicStruct::Builder::getObjectAsDataImpl(builder, checkIsObject());
}
DynamicStruct::Builder DynamicUnion::Builder::initObject(
    StructSchema::Member member, StructSchema type) {
  setObjectDiscriminant(member);
  return DynamicStruct::Builder::initFieldImpl(builder, member, type);
}
DynamicList::Builder DynamicUnion::Builder::initObject(
    StructSchema::Member member, ListSchema type, uint size) {
  setObjectDiscriminant(member);
  return DynamicStruct::Builder::initFieldImpl(builder, member, type, size);
}
Text::Builder DynamicUnion::Builder::initObjectAsText(StructSchema::Member member, uint size) {
  setObjectDiscriminant(member);
  return DynamicStruct::Builder::initFieldAsTextImpl(builder, member, size);
}
Data::Builder DynamicUnion::Builder::initObjectAsData(StructSchema::Member member, uint size) {
  setObjectDiscriminant(member);
  return DynamicStruct::Builder::initFieldAsDataImpl(builder, member, size);
}

void DynamicUnion::Builder::set(kj::StringPtr name, const DynamicValue::Reader& value) {
  set(schema.getMemberByName(name), value);
}
DynamicValue::Builder DynamicUnion::Builder::init(kj::StringPtr name) {
  return init(schema.getMemberByName(name));
}
DynamicValue::Builder DynamicUnion::Builder::init(kj::StringPtr name, uint size) {
  return init(schema.getMemberByName(name), size);
}
DynamicStruct::Builder DynamicUnion::Builder::initObject(kj::StringPtr name, StructSchema type) {
  return initObject(schema.getMemberByName(name), type);
}
DynamicList::Builder DynamicUnion::Builder::initObject(
    kj::StringPtr name, ListSchema type, uint size) {
  return initObject(schema.getMemberByName(name), type, size);
}
Text::Builder DynamicUnion::Builder::initObjectAsText(kj::StringPtr name, uint size) {
  return initObjectAsText(schema.getMemberByName(name), size);
}
Data::Builder DynamicUnion::Builder::initObjectAsData(kj::StringPtr name, uint size) {
  return initObjectAsData(schema.getMemberByName(name), size);
}

StructSchema::Member DynamicUnion::Builder::checkIsObject() {
  KJ_IF_MAYBE(w, which()) {
    KJ_ASSERT(w->getProto().getBody().which() == schema::StructNode::Member::Body::FIELD_MEMBER,
          "Unsupported union member type.");
    KJ_REQUIRE(w->getProto().getBody().getFieldMember().getType().getBody().which() ==
            schema::Type::Body::OBJECT_TYPE, "Expected Object.");
    return *w;
  } else {
    KJ_FAIL_REQUIRE("Can't get() unknown union value.");
  }
}

void DynamicUnion::Builder::setDiscriminant(StructSchema::Member member) {
  KJ_IF_MAYBE(containingUnion, member.getContainingUnion()) {
    KJ_REQUIRE(*containingUnion == schema, "`member` is not a member of this union.");
    builder.setDataField<uint16_t>(
        schema.getProto().getBody().getUnionMember().getDiscriminantOffset() * ELEMENTS,
        member.getIndex());
  } else {
    KJ_FAIL_REQUIRE("`member` is not a member of this union.");
  }
}

void DynamicUnion::Builder::setObjectDiscriminant(StructSchema::Member member) {
  KJ_REQUIRE(member.getProto().getBody().getFieldMember().getType().getBody().which() ==
          schema::Type::Body::OBJECT_TYPE, "Expected Object.");
  setDiscriminant(member);
}

// =======================================================================================

DynamicValue::Reader DynamicStruct::Reader::get(StructSchema::Member member) const {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  return getImpl(reader, member);
}
DynamicValue::Builder DynamicStruct::Builder::get(StructSchema::Member member) {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  return getImpl(builder, member);
}

bool DynamicStruct::Reader::has(StructSchema::Member member) const {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");

  auto body = member.getProto().getBody();
  switch (body.which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER: {
      auto u = body.getUnionMember();
      if (reader.getDataField<uint16_t>(u.getDiscriminantOffset() * ELEMENTS) != 0) {
        // Union has non-default member set.
        return true;
      }
      auto members = member.asUnion().getMembers();
      if (members.size() == 0) {
        // Union has no defined members.  This should probably be disallowed?
        return false;
      }

      // The union has the default member set, so now the question is whether that member is set
      // to its default value.  So, continue on with the function using that member.
      member = members[0];
      break;
    }

    case schema::StructNode::Member::Body::FIELD_MEMBER:
      // Continue to below.
      break;
  }

  auto field = member.getProto().getBody().getFieldMember();
  auto type = field.getType().getBody();

  switch (type.which()) {
    case schema::Type::Body::VOID_TYPE:
      return false;

#define HANDLE_TYPE(discrim, type) \
    case schema::Type::Body::discrim##_TYPE: \
      return reader.getDataField<type>(field.getOffset() * ELEMENTS) != 0;

    HANDLE_TYPE(BOOL, bool)
    HANDLE_TYPE(INT8, uint8_t)
    HANDLE_TYPE(INT16, uint16_t)
    HANDLE_TYPE(INT32, uint32_t)
    HANDLE_TYPE(INT64, uint64_t)
    HANDLE_TYPE(UINT8, uint8_t)
    HANDLE_TYPE(UINT16, uint16_t)
    HANDLE_TYPE(UINT32, uint32_t)
    HANDLE_TYPE(UINT64, uint64_t)
    HANDLE_TYPE(FLOAT32, uint32_t)
    HANDLE_TYPE(FLOAT64, uint64_t)
    HANDLE_TYPE(ENUM, uint16_t)

#undef HANDLE_TYPE

    case schema::Type::Body::TEXT_TYPE:
    case schema::Type::Body::DATA_TYPE:
    case schema::Type::Body::LIST_TYPE:
    case schema::Type::Body::STRUCT_TYPE:
    case schema::Type::Body::OBJECT_TYPE:
    case schema::Type::Body::INTERFACE_TYPE:
      return !reader.isPointerFieldNull(field.getOffset() * POINTERS);
  }

  // Unknown type.  As far as we know, it isn't set.
  return false;
}
bool DynamicStruct::Builder::has(StructSchema::Member member) {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");

  auto body = member.getProto().getBody();
  switch (body.which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER: {
      auto u = body.getUnionMember();
      if (builder.getDataField<uint16_t>(u.getDiscriminantOffset() * ELEMENTS) != 0) {
        // Union has non-default member set.
        return true;
      }
      auto members = member.asUnion().getMembers();
      if (members.size() == 0) {
        // Union has no defined members.  This should probably be disallowed?
        return false;
      }

      // The union has the default member set, so now the question is whether that member is set
      // to its default value.  So, continue on with the function using that member.
      member = members[0];
      break;
    }

    case schema::StructNode::Member::Body::FIELD_MEMBER:
      // Continue to below.
      break;
  }

  auto field = member.getProto().getBody().getFieldMember();
  auto type = field.getType().getBody();

  switch (type.which()) {
    case schema::Type::Body::VOID_TYPE:
      return false;

#define HANDLE_TYPE(discrim, type) \
    case schema::Type::Body::discrim##_TYPE: \
      return builder.getDataField<type>(field.getOffset() * ELEMENTS) != 0;

    HANDLE_TYPE(BOOL, bool)
    HANDLE_TYPE(INT8, uint8_t)
    HANDLE_TYPE(INT16, uint16_t)
    HANDLE_TYPE(INT32, uint32_t)
    HANDLE_TYPE(INT64, uint64_t)
    HANDLE_TYPE(UINT8, uint8_t)
    HANDLE_TYPE(UINT16, uint16_t)
    HANDLE_TYPE(UINT32, uint32_t)
    HANDLE_TYPE(UINT64, uint64_t)
    HANDLE_TYPE(FLOAT32, uint32_t)
    HANDLE_TYPE(FLOAT64, uint64_t)
    HANDLE_TYPE(ENUM, uint16_t)

#undef HANDLE_TYPE

    case schema::Type::Body::TEXT_TYPE:
    case schema::Type::Body::DATA_TYPE:
    case schema::Type::Body::LIST_TYPE:
    case schema::Type::Body::STRUCT_TYPE:
    case schema::Type::Body::OBJECT_TYPE:
    case schema::Type::Body::INTERFACE_TYPE:
      return !builder.isPointerFieldNull(field.getOffset() * POINTERS);
  }

  // Unknown type.  As far as we know, it isn't set.
  return false;
}

void DynamicStruct::Builder::set(StructSchema::Member member, const DynamicValue::Reader& value) {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  return setImpl(builder, member, value);
}
DynamicValue::Builder DynamicStruct::Builder::init(StructSchema::Member member) {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  return initImpl(builder, member);
}
DynamicValue::Builder DynamicStruct::Builder::init(StructSchema::Member member, uint size) {
  KJ_REQUIRE(member.getContainingStruct() == schema,
          "`member` is not a member of this struct.");
  return initImpl(builder, member, size);
}

DynamicStruct::Builder DynamicStruct::Builder::getObject(
    StructSchema::Member member, StructSchema type) {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      KJ_FAIL_REQUIRE("Expected an Object.");
      break;

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      KJ_REQUIRE(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return getObjectImpl(builder, member, type);
    }
  }

  KJ_FAIL_ASSERT("switch() missing case.", (uint)member.getProto().getBody().which());
  return DynamicStruct::Builder();
}
DynamicList::Builder DynamicStruct::Builder::getObject(
    StructSchema::Member member, ListSchema type) {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      KJ_FAIL_REQUIRE("Expected an Object.");
      break;

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      KJ_REQUIRE(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return getObjectImpl(builder, member, type);
    }
  }

  KJ_FAIL_ASSERT("switch() missing case.", (uint)member.getProto().getBody().which());
  return DynamicList::Builder();
}
Text::Builder DynamicStruct::Builder::getObjectAsText(StructSchema::Member member) {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      KJ_FAIL_REQUIRE("Expected an Object.");
      return Text::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      KJ_REQUIRE(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return getObjectAsTextImpl(builder, member);
    }
  }

  KJ_FAIL_ASSERT("switch() missing case.", (uint)member.getProto().getBody().which());
  return Text::Builder();
}
Data::Builder DynamicStruct::Builder::getObjectAsData(StructSchema::Member member) {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      KJ_FAIL_REQUIRE("Expected an Object.");
      return Data::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      KJ_REQUIRE(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return getObjectAsDataImpl(builder, member);
    }
  }

  KJ_FAIL_ASSERT("switch() missing case.", (uint)member.getProto().getBody().which());
  return Data::Builder();
}

DynamicStruct::Builder DynamicStruct::Builder::initObject(
    StructSchema::Member member, StructSchema type) {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      KJ_FAIL_REQUIRE("Expected an Object.");
      return DynamicStruct::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      KJ_REQUIRE(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return initFieldImpl(builder, member, type);
    }
  }

  KJ_FAIL_ASSERT("switch() missing case.", (uint)member.getProto().getBody().which());
  return DynamicStruct::Builder();
}
DynamicList::Builder DynamicStruct::Builder::initObject(
    StructSchema::Member member, ListSchema type, uint size) {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      KJ_FAIL_REQUIRE("Expected an Object.");
      return DynamicList::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      KJ_REQUIRE(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return initFieldImpl(builder, member, type, size);
    }
  }

  KJ_FAIL_ASSERT("switch() missing case.", (uint)member.getProto().getBody().which());
  return DynamicList::Builder();
}
Text::Builder DynamicStruct::Builder::initObjectAsText(StructSchema::Member member, uint size) {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      KJ_FAIL_REQUIRE("Expected an Object.");
      return Text::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      KJ_REQUIRE(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return initFieldAsTextImpl(builder, member, size);
    }
  }

  KJ_FAIL_ASSERT("switch() missing case.", (uint)member.getProto().getBody().which());
  return Text::Builder();
}
Data::Builder DynamicStruct::Builder::initObjectAsData(StructSchema::Member member, uint size) {
  KJ_REQUIRE(member.getContainingStruct() == schema, "`member` is not a member of this struct.");
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      KJ_FAIL_REQUIRE("Expected an Object.");
      return Data::Builder();

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      KJ_REQUIRE(field.getType().getBody().which() == schema::Type::Body::OBJECT_TYPE,
              "Expected an Object.");
      return initFieldAsDataImpl(builder, member, size);
    }
  }

  KJ_FAIL_ASSERT("switch() missing case.", (uint)member.getProto().getBody().which());
  return Data::Builder();
}

DynamicValue::Reader DynamicStruct::Reader::get(kj::StringPtr name) const {
  return getImpl(reader, schema.getMemberByName(name));
}
DynamicValue::Builder DynamicStruct::Builder::get(kj::StringPtr name) {
  return getImpl(builder, schema.getMemberByName(name));
}
bool DynamicStruct::Reader::has(kj::StringPtr name) const {
  return has(schema.getMemberByName(name));
}
bool DynamicStruct::Builder::has(kj::StringPtr name) {
  return has(schema.getMemberByName(name));
}
void DynamicStruct::Builder::set(kj::StringPtr name, const DynamicValue::Reader& value) {
  setImpl(builder, schema.getMemberByName(name), value);
}
void DynamicStruct::Builder::set(kj::StringPtr name,
                                 std::initializer_list<DynamicValue::Reader> value) {
  init(name, value.size()).as<DynamicList>().copyFrom(value);
}
DynamicValue::Builder DynamicStruct::Builder::init(kj::StringPtr name) {
  return initImpl(builder, schema.getMemberByName(name));
}
DynamicValue::Builder DynamicStruct::Builder::init(kj::StringPtr name, uint size) {
  return initImpl(builder, schema.getMemberByName(name), size);
}
DynamicStruct::Builder DynamicStruct::Builder::getObject(
    kj::StringPtr name, StructSchema type) {
  return getObject(schema.getMemberByName(name), type);
}
DynamicList::Builder DynamicStruct::Builder::getObject(kj::StringPtr name, ListSchema type) {
  return getObject(schema.getMemberByName(name), type);
}
Text::Builder DynamicStruct::Builder::getObjectAsText(kj::StringPtr name) {
  return getObjectAsText(schema.getMemberByName(name));
}
Data::Builder DynamicStruct::Builder::getObjectAsData(kj::StringPtr name) {
  return getObjectAsData(schema.getMemberByName(name));
}
DynamicStruct::Builder DynamicStruct::Builder::initObject(
    kj::StringPtr name, StructSchema type) {
  return initObject(schema.getMemberByName(name), type);
}
DynamicList::Builder DynamicStruct::Builder::initObject(
    kj::StringPtr name, ListSchema type, uint size) {
  return initObject(schema.getMemberByName(name), type, size);
}
Text::Builder DynamicStruct::Builder::initObjectAsText(kj::StringPtr name, uint size) {
  return initObjectAsText(schema.getMemberByName(name), size);
}
Data::Builder DynamicStruct::Builder::initObjectAsData(kj::StringPtr name, uint size) {
  return initObjectAsData(schema.getMemberByName(name), size);
}

DynamicValue::Reader DynamicStruct::Reader::getImpl(
    _::StructReader reader, StructSchema::Member member) {
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      return DynamicUnion::Reader(member.asUnion(), reader);

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      auto type = field.getType().getBody();
      auto dval = field.getDefaultValue().getBody();

      switch (type.which()) {
        case schema::Type::Body::VOID_TYPE:
          return DynamicValue::Reader(reader.getDataField<Void>(field.getOffset() * ELEMENTS));

#define HANDLE_TYPE(discrim, titleCase, type) \
        case schema::Type::Body::discrim##_TYPE: \
          return DynamicValue::Reader(reader.getDataField<type>( \
              field.getOffset() * ELEMENTS, \
              bitCast<_::Mask<type>>(dval.get##titleCase##Value())));

        HANDLE_TYPE(BOOL, Bool, bool)
        HANDLE_TYPE(INT8, Int8, int8_t)
        HANDLE_TYPE(INT16, Int16, int16_t)
        HANDLE_TYPE(INT32, Int32, int32_t)
        HANDLE_TYPE(INT64, Int64, int64_t)
        HANDLE_TYPE(UINT8, Uint8, uint8_t)
        HANDLE_TYPE(UINT16, Uint16, uint16_t)
        HANDLE_TYPE(UINT32, Uint32, uint32_t)
        HANDLE_TYPE(UINT64, Uint64, uint64_t)
        HANDLE_TYPE(FLOAT32, Float32, float)
        HANDLE_TYPE(FLOAT64, Float64, double)

#undef HANDLE_TYPE

        case schema::Type::Body::ENUM_TYPE: {
          uint16_t typedDval;
          typedDval = dval.getEnumValue();
          return DynamicValue::Reader(DynamicEnum(
              member.getContainingStruct().getDependency(type.getEnumType()).asEnum(),
              reader.getDataField<uint16_t>(field.getOffset() * ELEMENTS, typedDval)));
        }

        case schema::Type::Body::TEXT_TYPE: {
          Text::Reader typedDval = dval.getTextValue();
          return DynamicValue::Reader(
              reader.getBlobField<Text>(field.getOffset() * POINTERS,
                                        typedDval.begin(), typedDval.size() * BYTES));
        }

        case schema::Type::Body::DATA_TYPE: {
          Data::Reader typedDval = dval.getDataValue();
          return DynamicValue::Reader(
              reader.getBlobField<Data>(field.getOffset() * POINTERS,
                                        typedDval.begin(), typedDval.size() * BYTES));
        }

        case schema::Type::Body::LIST_TYPE: {
          auto elementType = type.getListType();
          return DynamicValue::Reader(DynamicList::Reader(
              ListSchema::of(elementType, member.getContainingStruct()),
              reader.getListField(field.getOffset() * POINTERS,
                                  elementSizeFor(elementType.getBody().which()),
                                  dval.getListValue<_::UncheckedMessage>())));
        }

        case schema::Type::Body::STRUCT_TYPE: {
          return DynamicValue::Reader(DynamicStruct::Reader(
              member.getContainingStruct().getDependency(type.getStructType()).asStruct(),
              reader.getStructField(field.getOffset() * POINTERS,
                                    dval.getStructValue<_::UncheckedMessage>())));
        }

        case schema::Type::Body::OBJECT_TYPE: {
          return DynamicValue::Reader(DynamicObject(
              reader.getObjectField(field.getOffset() * POINTERS,
                                    dval.getObjectValue<_::UncheckedMessage>())));
        }

        case schema::Type::Body::INTERFACE_TYPE:
          KJ_FAIL_ASSERT("Interfaces not yet implemented.");
          break;
      }

      return nullptr;
    }
  }

  KJ_FAIL_ASSERT("switch() missing case.", (uint)member.getProto().getBody().which());
  return nullptr;
}

DynamicValue::Builder DynamicStruct::Builder::getImpl(
    _::StructBuilder builder, StructSchema::Member member) {
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      return DynamicUnion::Builder(member.asUnion(), builder);

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      auto type = field.getType().getBody();
      auto dval = field.getDefaultValue().getBody();

      switch (type.which()) {
        case schema::Type::Body::VOID_TYPE:
          return DynamicValue::Builder(builder.getDataField<Void>(field.getOffset() * ELEMENTS));

#define HANDLE_TYPE(discrim, titleCase, type) \
        case schema::Type::Body::discrim##_TYPE: \
          return DynamicValue::Builder(builder.getDataField<type>( \
              field.getOffset() * ELEMENTS, \
              bitCast<_::Mask<type>>(dval.get##titleCase##Value())));

        HANDLE_TYPE(BOOL, Bool, bool)
        HANDLE_TYPE(INT8, Int8, int8_t)
        HANDLE_TYPE(INT16, Int16, int16_t)
        HANDLE_TYPE(INT32, Int32, int32_t)
        HANDLE_TYPE(INT64, Int64, int64_t)
        HANDLE_TYPE(UINT8, Uint8, uint8_t)
        HANDLE_TYPE(UINT16, Uint16, uint16_t)
        HANDLE_TYPE(UINT32, Uint32, uint32_t)
        HANDLE_TYPE(UINT64, Uint64, uint64_t)
        HANDLE_TYPE(FLOAT32, Float32, float)
        HANDLE_TYPE(FLOAT64, Float64, double)

#undef HANDLE_TYPE

        case schema::Type::Body::ENUM_TYPE: {
          uint16_t typedDval;
          typedDval = dval.getEnumValue();
          return DynamicValue::Builder(DynamicEnum(
              member.getContainingStruct().getDependency(type.getEnumType()).asEnum(),
              builder.getDataField<uint16_t>(field.getOffset() * ELEMENTS, typedDval)));
        }

        case schema::Type::Body::TEXT_TYPE: {
          Text::Reader typedDval = dval.getTextValue();
          return DynamicValue::Builder(
              builder.getBlobField<Text>(field.getOffset() * POINTERS,
                                         typedDval.begin(), typedDval.size() * BYTES));
        }

        case schema::Type::Body::DATA_TYPE: {
          Data::Reader typedDval = dval.getDataValue();
          return DynamicValue::Builder(
              builder.getBlobField<Data>(field.getOffset() * POINTERS,
                                         typedDval.begin(), typedDval.size() * BYTES));
        }

        case schema::Type::Body::LIST_TYPE: {
          ListSchema listType = ListSchema::of(type.getListType(), member.getContainingStruct());
          if (listType.whichElementType() == schema::Type::Body::STRUCT_TYPE) {
            return DynamicValue::Builder(DynamicList::Builder(listType,
                builder.getStructListField(field.getOffset() * POINTERS,
                                           structSizeFromSchema(listType.getStructElementType()),
                                           dval.getListValue<_::UncheckedMessage>())));
          } else {
            return DynamicValue::Builder(DynamicList::Builder(listType,
                builder.getListField(field.getOffset() * POINTERS,
                                     elementSizeFor(listType.whichElementType()),
                                     dval.getListValue<_::UncheckedMessage>())));
          }
        }

        case schema::Type::Body::STRUCT_TYPE: {
          auto structSchema =
              member.getContainingStruct().getDependency(type.getStructType()).asStruct();
          return DynamicValue::Builder(DynamicStruct::Builder(
              structSchema,
              builder.getStructField(
                  field.getOffset() * POINTERS,
                  structSizeFromSchema(structSchema),
                  dval.getStructValue<_::UncheckedMessage>())));
        }

        case schema::Type::Body::OBJECT_TYPE: {
          return DynamicValue::Builder(DynamicObject(
              builder.asReader().getObjectField(
                  field.getOffset() * POINTERS,
                  dval.getObjectValue<_::UncheckedMessage>())));
        }

        case schema::Type::Body::INTERFACE_TYPE:
          KJ_FAIL_ASSERT("Interfaces not yet implemented.");
          break;
      }

      return nullptr;
    }
  }

  KJ_FAIL_ASSERT("switch() missing case.", (uint)member.getProto().getBody().which());
  return nullptr;
}
DynamicStruct::Builder DynamicStruct::Builder::getObjectImpl(
    _::StructBuilder builder, StructSchema::Member field, StructSchema type) {
  return DynamicStruct::Builder(type,
      builder.getStructField(
          field.getProto().getBody().getFieldMember().getOffset() * POINTERS,
          structSizeFromSchema(type), nullptr));
}
DynamicList::Builder DynamicStruct::Builder::getObjectImpl(
    _::StructBuilder builder, StructSchema::Member field, ListSchema type) {
  if (type.whichElementType() == schema::Type::Body::STRUCT_TYPE) {
    return DynamicList::Builder(type,
        builder.getStructListField(
            field.getProto().getBody().getFieldMember().getOffset() * POINTERS,
            structSizeFromSchema(type.getStructElementType()),
            nullptr));
  } else {
    return DynamicList::Builder(type,
        builder.getListField(
            field.getProto().getBody().getFieldMember().getOffset() * POINTERS,
            elementSizeFor(type.whichElementType()),
            nullptr));
  }
}
Text::Builder DynamicStruct::Builder::getObjectAsTextImpl(
    _::StructBuilder builder, StructSchema::Member field) {
  return builder.getBlobField<Text>(
      field.getProto().getBody().getFieldMember().getOffset() * POINTERS, nullptr, 0 * BYTES);
}
Data::Builder DynamicStruct::Builder::getObjectAsDataImpl(
    _::StructBuilder builder, StructSchema::Member field) {
  return builder.getBlobField<Data>(
      field.getProto().getBody().getFieldMember().getOffset() * POINTERS, nullptr, 0 * BYTES);
}

void DynamicStruct::Builder::setImpl(
    _::StructBuilder builder, StructSchema::Member member,
    const DynamicValue::Reader& value) {
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER: {
      auto src = value.as<DynamicUnion>();
      KJ_IF_MAYBE(which, src.which()) {
        getImpl(builder, member).as<DynamicUnion>().set(member, src.get());
        return;
      } else {
        KJ_FAIL_REQUIRE(
            "Trying to copy a union value, but the union's discriminant is not recognized.  It "
            "was probably constructed using a newer version of the schema.") {
          // Just don't copy anything.
          return;
        }
      }
    }

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto field = member.getProto().getBody().getFieldMember();
      auto type = field.getType().getBody();
      auto dval = field.getDefaultValue().getBody();

      switch (type.which()) {
        case schema::Type::Body::VOID_TYPE:
          builder.setDataField<Void>(field.getOffset() * ELEMENTS, value.as<Void>());
          return;

#define HANDLE_TYPE(discrim, titleCase, type) \
        case schema::Type::Body::discrim##_TYPE: \
          builder.setDataField<type>( \
              field.getOffset() * ELEMENTS, value.as<type>(), \
              bitCast<_::Mask<type> >(dval.get##titleCase##Value())); \
          return;

        HANDLE_TYPE(BOOL, Bool, bool)
        HANDLE_TYPE(INT8, Int8, int8_t)
        HANDLE_TYPE(INT16, Int16, int16_t)
        HANDLE_TYPE(INT32, Int32, int32_t)
        HANDLE_TYPE(INT64, Int64, int64_t)
        HANDLE_TYPE(UINT8, Uint8, uint8_t)
        HANDLE_TYPE(UINT16, Uint16, uint16_t)
        HANDLE_TYPE(UINT32, Uint32, uint32_t)
        HANDLE_TYPE(UINT64, Uint64, uint64_t)
        HANDLE_TYPE(FLOAT32, Float32, float)
        HANDLE_TYPE(FLOAT64, Float64, double)

#undef HANDLE_TYPE

        case schema::Type::Body::ENUM_TYPE: {
          uint16_t rawValue;
          auto enumSchema = member.getContainingStruct().getDependency(type.getEnumType()).asEnum();
          if (value.getType() == DynamicValue::TEXT) {
            // Convert from text.
            rawValue = enumSchema.getEnumerantByName(value.as<Text>()).getOrdinal();
          } else {
            DynamicEnum enumValue = value.as<DynamicEnum>();
            KJ_REQUIRE(enumValue.getSchema() == enumSchema,
                       "Type mismatch when using DynamicList::Builder::set().") {
              return;
            }
            rawValue = enumValue.getRaw();
          }
          builder.setDataField<uint16_t>(field.getOffset() * ELEMENTS, rawValue,
                                         dval.getEnumValue());
          return;
        }

        case schema::Type::Body::TEXT_TYPE:
          builder.setBlobField<Text>(field.getOffset() * POINTERS, value.as<Text>());
          return;

        case schema::Type::Body::DATA_TYPE:
          builder.setBlobField<Data>(field.getOffset() * POINTERS, value.as<Data>());
          return;

        case schema::Type::Body::LIST_TYPE: {
          builder.setListField(field.getOffset() * POINTERS, value.as<DynamicList>().reader);
          return;
        }

        case schema::Type::Body::STRUCT_TYPE: {
          builder.setStructField(field.getOffset() * POINTERS, value.as<DynamicStruct>().reader);
          return;
        }

        case schema::Type::Body::OBJECT_TYPE: {
          builder.setObjectField(field.getOffset() * POINTERS, value.as<DynamicObject>().reader);
          return;
        }

        case schema::Type::Body::INTERFACE_TYPE:
          KJ_FAIL_ASSERT("Interfaces not yet implemented.");
          return;
      }

      KJ_FAIL_REQUIRE("can't set field of unknown type", (uint)type.which()) {
        return;
      }
    }
  }

  KJ_FAIL_ASSERT("switch() missing case.", (uint)member.getProto().getBody().which());
}

DynamicValue::Builder DynamicStruct::Builder::initImpl(
    _::StructBuilder builder, StructSchema::Member member, uint size) {
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      KJ_FAIL_REQUIRE(
          "Can't init() a union.  get() it first and then init() one of its members.");
      break;

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto type = member.getProto().getBody().getFieldMember().getType().getBody();
      switch (type.which()) {
        case schema::Type::Body::LIST_TYPE:
          return initFieldImpl(builder, member, ListSchema::of(
              type.getListType(), member.getContainingStruct()), size);
        case schema::Type::Body::TEXT_TYPE:
          return initFieldAsTextImpl(builder, member, size);
        case schema::Type::Body::DATA_TYPE:
          return initFieldAsDataImpl(builder, member, size);
        default:
          KJ_FAIL_REQUIRE(
              "init() with size is only valid for list, text, or data fields.", (uint)type.which());
          break;
      }
      break;
    }
  }

  // Failed.
  return getImpl(builder, member);
}

DynamicValue::Builder DynamicStruct::Builder::initImpl(
    _::StructBuilder builder, StructSchema::Member member) {
  switch (member.getProto().getBody().which()) {
    case schema::StructNode::Member::Body::UNION_MEMBER:
      KJ_FAIL_REQUIRE(
          "Can't init() a union.  get() it first and then init() one of its members.");
      break;

    case schema::StructNode::Member::Body::FIELD_MEMBER: {
      auto type = member.getProto().getBody().getFieldMember().getType().getBody();
      KJ_REQUIRE(type.which() == schema::Type::Body::STRUCT_TYPE,
              "init() without a size is only valid for struct fields.");
      return initFieldImpl(builder, member,
          member.getContainingStruct().getDependency(type.getStructType()).asStruct());
    }
  }

  // Failed.
  return getImpl(builder, member);
}
DynamicStruct::Builder DynamicStruct::Builder::initFieldImpl(
    _::StructBuilder builder, StructSchema::Member field, StructSchema type) {
  return DynamicStruct::Builder(
      type, builder.initStructField(
          field.getProto().getBody().getFieldMember().getOffset() * POINTERS,
          structSizeFromSchema(type)));
}
DynamicList::Builder DynamicStruct::Builder::initFieldImpl(
    _::StructBuilder builder, StructSchema::Member field,
    ListSchema type, uint size) {
  if (type.whichElementType() == schema::Type::Body::STRUCT_TYPE) {
    return DynamicList::Builder(
        type, builder.initStructListField(
            field.getProto().getBody().getFieldMember().getOffset() * POINTERS, size * ELEMENTS,
            structSizeFromSchema(type.getStructElementType())));
  } else {
    return DynamicList::Builder(
        type, builder.initListField(
            field.getProto().getBody().getFieldMember().getOffset() * POINTERS,
            elementSizeFor(type.whichElementType()),
            size * ELEMENTS));
  }
}
Text::Builder DynamicStruct::Builder::initFieldAsTextImpl(
    _::StructBuilder builder, StructSchema::Member field, uint size) {
  return builder.initBlobField<Text>(
      field.getProto().getBody().getFieldMember().getOffset() * POINTERS, size * BYTES);
}
Data::Builder DynamicStruct::Builder::initFieldAsDataImpl(
    _::StructBuilder builder, StructSchema::Member field, uint size) {
  return builder.initBlobField<Data>(
      field.getProto().getBody().getFieldMember().getOffset() * POINTERS, size * BYTES);
}

// =======================================================================================

DynamicValue::Reader DynamicList::Reader::operator[](uint index) const {
  KJ_REQUIRE(index < size(), "List index out-of-bounds.");

  switch (schema.whichElementType()) {
#define HANDLE_TYPE(name, discrim, typeName) \
    case schema::Type::Body::discrim##_TYPE: \
      return DynamicValue::Reader(reader.getDataElement<typeName>(index * ELEMENTS));

    HANDLE_TYPE(void, VOID, Void)
    HANDLE_TYPE(bool, BOOL, bool)
    HANDLE_TYPE(int8, INT8, int8_t)
    HANDLE_TYPE(int16, INT16, int16_t)
    HANDLE_TYPE(int32, INT32, int32_t)
    HANDLE_TYPE(int64, INT64, int64_t)
    HANDLE_TYPE(uint8, UINT8, uint8_t)
    HANDLE_TYPE(uint16, UINT16, uint16_t)
    HANDLE_TYPE(uint32, UINT32, uint32_t)
    HANDLE_TYPE(uint64, UINT64, uint64_t)
    HANDLE_TYPE(float32, FLOAT32, float)
    HANDLE_TYPE(float64, FLOAT64, double)
#undef HANDLE_TYPE

    case schema::Type::Body::TEXT_TYPE:
      return DynamicValue::Reader(reader.getBlobElement<Text>(index * ELEMENTS));
    case schema::Type::Body::DATA_TYPE:
      return DynamicValue::Reader(reader.getBlobElement<Data>(index * ELEMENTS));

    case schema::Type::Body::LIST_TYPE: {
      auto elementType = schema.getListElementType();
      return DynamicValue::Reader(DynamicList::Reader(
          elementType, reader.getListElement(
              index * ELEMENTS, elementSizeFor(elementType.whichElementType()))));
    }

    case schema::Type::Body::STRUCT_TYPE:
      return DynamicValue::Reader(DynamicStruct::Reader(
          schema.getStructElementType(), reader.getStructElement(index * ELEMENTS)));

    case schema::Type::Body::ENUM_TYPE:
      return DynamicValue::Reader(DynamicEnum(
          schema.getEnumElementType(), reader.getDataElement<uint16_t>(index * ELEMENTS)));

    case schema::Type::Body::OBJECT_TYPE:
      return DynamicValue::Reader(DynamicObject(
          reader.getObjectElement(index * ELEMENTS)));

    case schema::Type::Body::INTERFACE_TYPE:
      KJ_FAIL_ASSERT("Interfaces not implemented.") {
        return nullptr;
      }
  }

  return nullptr;
}

DynamicValue::Builder DynamicList::Builder::operator[](uint index) {
  KJ_REQUIRE(index < size(), "List index out-of-bounds.");

  switch (schema.whichElementType()) {
#define HANDLE_TYPE(name, discrim, typeName) \
    case schema::Type::Body::discrim##_TYPE: \
      return DynamicValue::Builder(builder.getDataElement<typeName>(index * ELEMENTS));

    HANDLE_TYPE(void, VOID, Void)
    HANDLE_TYPE(bool, BOOL, bool)
    HANDLE_TYPE(int8, INT8, int8_t)
    HANDLE_TYPE(int16, INT16, int16_t)
    HANDLE_TYPE(int32, INT32, int32_t)
    HANDLE_TYPE(int64, INT64, int64_t)
    HANDLE_TYPE(uint8, UINT8, uint8_t)
    HANDLE_TYPE(uint16, UINT16, uint16_t)
    HANDLE_TYPE(uint32, UINT32, uint32_t)
    HANDLE_TYPE(uint64, UINT64, uint64_t)
    HANDLE_TYPE(float32, FLOAT32, float)
    HANDLE_TYPE(float64, FLOAT64, double)
#undef HANDLE_TYPE

    case schema::Type::Body::TEXT_TYPE:
      return DynamicValue::Builder(builder.getBlobElement<Text>(index * ELEMENTS));
    case schema::Type::Body::DATA_TYPE:
      return DynamicValue::Builder(builder.getBlobElement<Data>(index * ELEMENTS));

    case schema::Type::Body::LIST_TYPE: {
      ListSchema elementType = schema.getListElementType();
      if (elementType.whichElementType() == schema::Type::Body::STRUCT_TYPE) {
        return DynamicValue::Builder(DynamicList::Builder(elementType,
            builder.getStructListElement(
                index * ELEMENTS,
                structSizeFromSchema(elementType.getStructElementType()))));
      } else {
        return DynamicValue::Builder(DynamicList::Builder(elementType,
            builder.getListElement(
                index * ELEMENTS,
                elementSizeFor(elementType.whichElementType()))));
      }
    }

    case schema::Type::Body::STRUCT_TYPE:
      return DynamicValue::Builder(DynamicStruct::Builder(
          schema.getStructElementType(), builder.getStructElement(index * ELEMENTS)));

    case schema::Type::Body::ENUM_TYPE:
      return DynamicValue::Builder(DynamicEnum(
          schema.getEnumElementType(), builder.getDataElement<uint16_t>(index * ELEMENTS)));

    case schema::Type::Body::OBJECT_TYPE:
      KJ_FAIL_ASSERT("List(Object) not supported.");
      return nullptr;

    case schema::Type::Body::INTERFACE_TYPE:
      KJ_FAIL_ASSERT("Interfaces not implemented.") {
        return nullptr;
      }
  }

  return nullptr;
}

void DynamicList::Builder::set(uint index, const DynamicValue::Reader& value) {
  KJ_REQUIRE(index < size(), "List index out-of-bounds.") {
    return;
  }

  switch (schema.whichElementType()) {
#define HANDLE_TYPE(name, discrim, typeName) \
    case schema::Type::Body::discrim##_TYPE: \
      builder.setDataElement<typeName>(index * ELEMENTS, value.as<typeName>()); \
      return;

    HANDLE_TYPE(void, VOID, Void)
    HANDLE_TYPE(bool, BOOL, bool)
    HANDLE_TYPE(int8, INT8, int8_t)
    HANDLE_TYPE(int16, INT16, int16_t)
    HANDLE_TYPE(int32, INT32, int32_t)
    HANDLE_TYPE(int64, INT64, int64_t)
    HANDLE_TYPE(uint8, UINT8, uint8_t)
    HANDLE_TYPE(uint16, UINT16, uint16_t)
    HANDLE_TYPE(uint32, UINT32, uint32_t)
    HANDLE_TYPE(uint64, UINT64, uint64_t)
    HANDLE_TYPE(float32, FLOAT32, float)
    HANDLE_TYPE(float64, FLOAT64, double)
#undef HANDLE_TYPE

    case schema::Type::Body::TEXT_TYPE:
      builder.setBlobElement<Text>(index * ELEMENTS, value.as<Text>());
      return;
    case schema::Type::Body::DATA_TYPE:
      builder.setBlobElement<Data>(index * ELEMENTS, value.as<Data>());
      return;

    case schema::Type::Body::LIST_TYPE: {
      builder.setListElement(index * ELEMENTS, value.as<DynamicList>().reader);
      return;
    }

    case schema::Type::Body::STRUCT_TYPE:
      // Not supported for the same reason List<struct> doesn't support it -- the space for the
      // element is already allocated, and if it's smaller than the input value the copy would
      // have to be lossy.
      KJ_FAIL_ASSERT("DynamicList of structs does not support set().") {
        return;
      }

    case schema::Type::Body::ENUM_TYPE: {
      uint16_t rawValue;
      if (value.getType() == DynamicValue::TEXT) {
        // Convert from text.
        rawValue = schema.getEnumElementType().getEnumerantByName(value.as<Text>()).getOrdinal();
      } else {
        DynamicEnum enumValue = value.as<DynamicEnum>();
        KJ_REQUIRE(schema.getEnumElementType() == enumValue.getSchema(),
                   "Type mismatch when using DynamicList::Builder::set().") {
          return;
        }
        rawValue = enumValue.getRaw();
      }
      builder.setDataElement<uint16_t>(index * ELEMENTS, rawValue);
      return;
    }

    case schema::Type::Body::OBJECT_TYPE:
      KJ_FAIL_ASSERT("List(Object) not supported.") {
        return;
      }

    case schema::Type::Body::INTERFACE_TYPE:
      KJ_FAIL_ASSERT("Interfaces not implemented.") {
        return;
      }
  }

  KJ_FAIL_REQUIRE("can't set element of unknown type", (uint)schema.whichElementType()) {
    return;
  }
}

DynamicValue::Builder DynamicList::Builder::init(uint index, uint size) {
  KJ_REQUIRE(index < this->size(), "List index out-of-bounds.");

  switch (schema.whichElementType()) {
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
    case schema::Type::Body::ENUM_TYPE:
    case schema::Type::Body::STRUCT_TYPE:
    case schema::Type::Body::INTERFACE_TYPE:
      KJ_FAIL_REQUIRE("Expected a list or blob.");
      return nullptr;

    case schema::Type::Body::TEXT_TYPE:
      return DynamicValue::Builder(builder.initBlobElement<Text>(index * ELEMENTS, size * BYTES));

    case schema::Type::Body::DATA_TYPE:
      return DynamicValue::Builder(builder.initBlobElement<Data>(index * ELEMENTS, size * BYTES));

    case schema::Type::Body::LIST_TYPE: {
      auto elementType = schema.getListElementType();

      if (elementType.whichElementType() == schema::Type::Body::STRUCT_TYPE) {
        return DynamicValue::Builder(DynamicList::Builder(
            elementType, builder.initStructListElement(
                index * ELEMENTS, size * ELEMENTS,
                structSizeFromSchema(elementType.getStructElementType()))));
      } else {
        return DynamicValue::Builder(DynamicList::Builder(
            elementType, builder.initListElement(
                index * ELEMENTS, elementSizeFor(elementType.whichElementType()),
                size * ELEMENTS)));
      }
    }

    case schema::Type::Body::OBJECT_TYPE: {
      KJ_FAIL_ASSERT("List(Object) not supported.");
      return nullptr;
    }
  }

  return nullptr;
}

void DynamicList::Builder::copyFrom(std::initializer_list<DynamicValue::Reader> value) {
  KJ_REQUIRE(value.size() == size(), "DynamicList::copyFrom() argument had different size.");
  uint i = 0;
  for (auto element: value) {
    set(i++, element);
  }
}

DynamicList::Reader DynamicList::Builder::asReader() const {
  return DynamicList::Reader(schema, builder.asReader());
}

// =======================================================================================

DynamicValue::Reader DynamicValue::Builder::asReader() const {
  switch (type) {
    case UNKNOWN: return Reader();
    case VOID: return Reader(voidValue);
    case BOOL: return Reader(boolValue);
    case INT: return Reader(intValue);
    case UINT: return Reader(uintValue);
    case FLOAT: return Reader(floatValue);
    case TEXT: return Reader(textValue.asReader());
    case DATA: return Reader(dataValue.asReader());
    case LIST: return Reader(listValue.asReader());
    case ENUM: return Reader(enumValue);
    case STRUCT: return Reader(structValue.asReader());
    case UNION: return Reader(unionValue.asReader());
    case INTERFACE: KJ_FAIL_ASSERT("Interfaces not implemented."); return Reader();
    case OBJECT: return Reader(objectValue);
  }
  KJ_FAIL_ASSERT("Missing switch case.");
  return Reader();
}

namespace {

template <typename T>
T signedToUnsigned(long long value) {
  KJ_REQUIRE(value >= 0 && T(value) == value, "Value out-of-range for requested type.", value) {
    // Use it anyway.
    break;
  }
  return value;
}

template <>
uint64_t signedToUnsigned<uint64_t>(long long value) {
  KJ_REQUIRE(value >= 0, "Value out-of-range for requested type.", value) {
    // Use it anyway.
    break;
  }
  return value;
}

template <typename T>
T unsignedToSigned(unsigned long long value) {
  KJ_REQUIRE(T(value) >= 0 && (unsigned long long)T(value) == value,
             "Value out-of-range for requested type.", value) {
    // Use it anyway.
    break;
  }
  return value;
}

template <>
int64_t unsignedToSigned<int64_t>(unsigned long long value) {
  KJ_REQUIRE(int64_t(value) >= 0, "Value out-of-range for requested type.", value) {
    // Use it anyway.
    break;
  }
  return value;
}

template <typename T, typename U>
T checkRoundTrip(U value) {
  KJ_REQUIRE(T(value) == value, "Value out-of-range for requested type.", value) {
    // Use it anyway.
    break;
  }
  return value;
}

}  // namespace

#define HANDLE_NUMERIC_TYPE(typeName, ifInt, ifUint, ifFloat) \
typeName DynamicValue::Reader::AsImpl<typeName>::apply(const Reader& reader) { \
  switch (reader.type) { \
    case INT: \
      return ifInt<typeName>(reader.intValue); \
    case UINT: \
      return ifUint<typeName>(reader.uintValue); \
    case FLOAT: \
      return ifFloat<typeName>(reader.floatValue); \
    default: \
      KJ_FAIL_REQUIRE("Value type mismatch.") { \
        return 0; \
      } \
  } \
} \
typeName DynamicValue::Builder::AsImpl<typeName>::apply(Builder& builder) { \
  switch (builder.type) { \
    case INT: \
      return ifInt<typeName>(builder.intValue); \
    case UINT: \
      return ifUint<typeName>(builder.uintValue); \
    case FLOAT: \
      return ifFloat<typeName>(builder.floatValue); \
    default: \
      KJ_FAIL_REQUIRE("Value type mismatch.") { \
        return 0; \
      } \
  } \
}

HANDLE_NUMERIC_TYPE(int8_t, checkRoundTrip, unsignedToSigned, checkRoundTrip)
HANDLE_NUMERIC_TYPE(int16_t, checkRoundTrip, unsignedToSigned, checkRoundTrip)
HANDLE_NUMERIC_TYPE(int32_t, checkRoundTrip, unsignedToSigned, checkRoundTrip)
HANDLE_NUMERIC_TYPE(int64_t, kj::implicitCast, unsignedToSigned, checkRoundTrip)
HANDLE_NUMERIC_TYPE(uint8_t, signedToUnsigned, checkRoundTrip, checkRoundTrip)
HANDLE_NUMERIC_TYPE(uint16_t, signedToUnsigned, checkRoundTrip, checkRoundTrip)
HANDLE_NUMERIC_TYPE(uint32_t, signedToUnsigned, checkRoundTrip, checkRoundTrip)
HANDLE_NUMERIC_TYPE(uint64_t, signedToUnsigned, kj::implicitCast, checkRoundTrip)
HANDLE_NUMERIC_TYPE(float, kj::implicitCast, kj::implicitCast, kj::implicitCast)
HANDLE_NUMERIC_TYPE(double, kj::implicitCast, kj::implicitCast, kj::implicitCast)

#undef HANDLE_NUMERIC_TYPE

#define HANDLE_TYPE(name, discrim, typeName) \
ReaderFor<typeName> DynamicValue::Reader::AsImpl<typeName>::apply(const Reader& reader) { \
  KJ_REQUIRE(reader.type == discrim, "Value type mismatch.") { \
    return ReaderFor<typeName>(); \
  } \
  return reader.name##Value; \
} \
BuilderFor<typeName> DynamicValue::Builder::AsImpl<typeName>::apply(Builder& builder) { \
  KJ_REQUIRE(builder.type == discrim, "Value type mismatch.") { \
    return BuilderFor<typeName>(); \
  } \
  return builder.name##Value; \
}

//HANDLE_TYPE(void, VOID, Void)
HANDLE_TYPE(bool, BOOL, bool)

HANDLE_TYPE(text, TEXT, Text)
HANDLE_TYPE(list, LIST, DynamicList)
HANDLE_TYPE(struct, STRUCT, DynamicStruct)
HANDLE_TYPE(enum, ENUM, DynamicEnum)
HANDLE_TYPE(object, OBJECT, DynamicObject)
HANDLE_TYPE(union, UNION, DynamicUnion)

#undef HANDLE_TYPE

Data::Reader DynamicValue::Reader::AsImpl<Data>::apply(const Reader& reader) {
  if (reader.type == TEXT) {
    // Coerce text to data.
    return Data::Reader(reinterpret_cast<const byte*>(reader.textValue.begin()),
                        reader.textValue.size());
  }
  KJ_REQUIRE(reader.type == DATA, "Value type mismatch.") {
    return Data::Reader();
  }
  return reader.dataValue;
}
Data::Builder DynamicValue::Builder::AsImpl<Data>::apply(Builder& builder) {
  if (builder.type == TEXT) {
    // Coerce text to data.
    return Data::Builder(reinterpret_cast<byte*>(builder.textValue.begin()),
                         builder.textValue.size());
  }
  KJ_REQUIRE(builder.type == DATA, "Value type mismatch.") {
    return BuilderFor<Data>();
  }
  return builder.dataValue;
}

// As in the header, HANDLE_TYPE(void, VOID, Void) crashes GCC 4.7.
Void DynamicValue::Reader::AsImpl<Void>::apply(const Reader& reader) {
  KJ_REQUIRE(reader.type == VOID, "Value type mismatch.") {
    return Void();
  }
  return reader.voidValue;
}
Void DynamicValue::Builder::AsImpl<Void>::apply(Builder& builder) {
  KJ_REQUIRE(builder.type == VOID, "Value type mismatch.") {
    return Void();
  }
  return builder.voidValue;
}

// =======================================================================================

template <>
DynamicStruct::Reader MessageReader::getRoot<DynamicStruct>(StructSchema schema) {
  return DynamicStruct::Reader(schema, getRootInternal());
}

template <>
DynamicStruct::Builder MessageBuilder::initRoot<DynamicStruct>(StructSchema schema) {
  return DynamicStruct::Builder(schema, initRoot(structSizeFromSchema(schema)));
}

template <>
DynamicStruct::Builder MessageBuilder::getRoot<DynamicStruct>(StructSchema schema) {
  return DynamicStruct::Builder(schema, getRoot(structSizeFromSchema(schema)));
}

namespace _ {  // private

DynamicStruct::Reader PointerHelpers<DynamicStruct, Kind::UNKNOWN>::getDynamic(
    StructReader reader, WirePointerCount index, StructSchema schema) {
  return DynamicStruct::Reader(schema, reader.getStructField(index, nullptr));
}
DynamicStruct::Builder PointerHelpers<DynamicStruct, Kind::UNKNOWN>::getDynamic(
    StructBuilder builder, WirePointerCount index, StructSchema schema) {
  return DynamicStruct::Builder(schema, builder.getStructField(
      index, structSizeFromSchema(schema), nullptr));
}
void PointerHelpers<DynamicStruct, Kind::UNKNOWN>::set(
    StructBuilder builder, WirePointerCount index, const DynamicStruct::Reader& value) {
  builder.setStructField(index, value.reader);
}
DynamicStruct::Builder PointerHelpers<DynamicStruct, Kind::UNKNOWN>::init(
    StructBuilder builder, WirePointerCount index, StructSchema schema) {
  return DynamicStruct::Builder(schema,
      builder.initStructField(index, structSizeFromSchema(schema)));
}

DynamicList::Reader PointerHelpers<DynamicList, Kind::UNKNOWN>::getDynamic(
    StructReader reader, WirePointerCount index, ListSchema schema) {
  return DynamicList::Reader(schema,
      reader.getListField(index, elementSizeFor(schema.whichElementType()), nullptr));
}
DynamicList::Builder PointerHelpers<DynamicList, Kind::UNKNOWN>::getDynamic(
    StructBuilder builder, WirePointerCount index, ListSchema schema) {
  if (schema.whichElementType() == schema::Type::Body::STRUCT_TYPE) {
    return DynamicList::Builder(schema,
        builder.getStructListField(index,
            structSizeFromSchema(schema.getStructElementType()),
            nullptr));
  } else {
    return DynamicList::Builder(schema,
        builder.getListField(index, elementSizeFor(schema.whichElementType()), nullptr));
  }
}
void PointerHelpers<DynamicList, Kind::UNKNOWN>::set(
    StructBuilder builder, WirePointerCount index, const DynamicList::Reader& value) {
  builder.setListField(index, value.reader);
}
DynamicList::Builder PointerHelpers<DynamicList, Kind::UNKNOWN>::init(
    StructBuilder builder, WirePointerCount index, ListSchema schema, uint size) {
  if (schema.whichElementType() == schema::Type::Body::STRUCT_TYPE) {
    return DynamicList::Builder(schema,
        builder.initStructListField(index, size * ELEMENTS,
            structSizeFromSchema(schema.getStructElementType())));
  } else {
    return DynamicList::Builder(schema,
        builder.initListField(index, elementSizeFor(schema.whichElementType()), size * ELEMENTS));
  }
}

}  // namespace _ (private)

// -------------------------------------------------------------------

Orphan<DynamicStruct> Orphanage::newOrphan(StructSchema schema) const {
  return Orphan<DynamicStruct>(
      schema, _::OrphanBuilder::initStruct(arena, structSizeFromSchema(schema)));
}

Orphan<DynamicList> Orphanage::newOrphan(ListSchema schema, uint size) const {
  if (schema.whichElementType() == schema::Type::Body::STRUCT_TYPE) {
    return Orphan<DynamicList>(schema, _::OrphanBuilder::initStructList(
        arena, size * ELEMENTS, structSizeFromSchema(schema.getStructElementType())));
  } else {
    return Orphan<DynamicList>(schema, _::OrphanBuilder::initList(
        arena, size * ELEMENTS, elementSizeFor(schema.whichElementType())));
  }
}

DynamicStruct::Builder Orphan<DynamicStruct>::get() {
  return DynamicStruct::Builder(schema, builder.asStruct(structSizeFromSchema(schema)));
}

DynamicStruct::Reader Orphan<DynamicStruct>::getReader() const {
  return DynamicStruct::Reader(schema, builder.asStructReader(structSizeFromSchema(schema)));
}

DynamicList::Builder Orphan<DynamicList>::get() {
  if (schema.whichElementType() == schema::Type::Body::STRUCT_TYPE) {
    return DynamicList::Builder(
        schema, builder.asStructList(structSizeFromSchema(schema.getStructElementType())));
  } else {
    return DynamicList::Builder(
        schema, builder.asList(elementSizeFor(schema.whichElementType())));
  }
}

DynamicList::Reader Orphan<DynamicList>::getReader() const {
  return DynamicList::Reader(
      schema, builder.asListReader(elementSizeFor(schema.whichElementType())));
}

}  // namespace capnp
