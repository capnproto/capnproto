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
#include "dynamic.h"
#include "logging.h"
#include <unordered_map>
#include <string>

namespace capnproto {

struct IdTextHash {
  static size_t hash(std::pair<uint64_t, Text::Reader> p) {
    // djb2a hash, but seeded with ID.
    size_t result = p.first;
    int c;
    const char* str = p.second.c_str();

    while ((c = *str++)) {
      result = ((result << 5) + result) ^ c;  // (result * 33) ^ c
    }

    return result;
  }
};

struct SchemaPool::Impl {
  std::unordered_map<uint64_t, schema::Node::Reader> nodeMap;
  std::unordered_map<std::pair<uint64_t, std::string>, schema::StructNode::Member::Reader,
                     IdTextHash>
      memberMap;
  std::unordered_map<std::pair<uint64_t, std::string>, schema::EnumNode::Enumerant::Reader,
                     IdTextHash>
      enumerantMap;
};

SchemaPool::~SchemaPool() {
  delete impl;
}

// TODO(soon):  Implement this.  Need to copy, ick.
//void add(schema::Node::Reader node) {
//}

void SchemaPool::addNoCopy(schema::Node::Reader node) {
  if (impl == nullptr) {
    impl = new Impl;
  }

  // TODO(soon):  Check if node is in base.
  // TODO(soon):  Check if existing node came from generated code.

  auto entry = std::make_pair(node.getId(), node);
  auto ins = impl->nodeMap.insert(entry);
  if (!ins.second) {
    // TODO(soon):  Check for compatibility.
    FAIL_CHECK("TODO:  Check schema compatibility when adding.");
  }
}

bool SchemaPool::has(uint64_t id) const {
  return (impl != nullptr && impl->nodeMap.count(id) != 0) || (base != nullptr && base->has(id));
}

DynamicStruct::Reader SchemaPool::getRoot(MessageReader& message, uint64_t typeId) const {

}

DynamicStruct::Builder SchemaPool::getRoot(MessageBuilder& message, uint64_t typeId) const {

}

// =======================================================================================

namespace {

template <typename T, typename U>
CAPNPROTO_ALWAYS_INLINE(T bitCast(U value));

template <typename T, typename U>
inline T bitCast(U value) {
  static_assert(sizeof(T) == sizeof(U), "Size must match.");
  return value;
}
template <>
inline float bitCast<float, uint32_t>(uint32_t value) {
  float result;
  memcpy(&result, &value, sizeof(value));
  return result;
}
template <>
inline double bitCast<double, uint64_t>(uint64_t value) {
  double result;
  memcpy(&result, &value, sizeof(value));
  return result;
}
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

internal::FieldSize elementSizeFor(schema::Type::Reader elementType) {
  switch (elementType.getBody().which()) {
    case schema::Type::Body::VOID_TYPE: return internal::FieldSize::VOID;
    case schema::Type::Body::BOOL_TYPE: return internal::FieldSize::BIT;
    case schema::Type::Body::INT8_TYPE: return internal::FieldSize::BYTE;
    case schema::Type::Body::INT16_TYPE: return internal::FieldSize::TWO_BYTES;
    case schema::Type::Body::INT32_TYPE: return internal::FieldSize::FOUR_BYTES;
    case schema::Type::Body::INT64_TYPE: return internal::FieldSize::EIGHT_BYTES;
    case schema::Type::Body::UINT8_TYPE: return internal::FieldSize::BYTE;
    case schema::Type::Body::UINT16_TYPE: return internal::FieldSize::TWO_BYTES;
    case schema::Type::Body::UINT32_TYPE: return internal::FieldSize::FOUR_BYTES;
    case schema::Type::Body::UINT64_TYPE: return internal::FieldSize::EIGHT_BYTES;
    case schema::Type::Body::FLOAT32_TYPE: return internal::FieldSize::FOUR_BYTES;
    case schema::Type::Body::FLOAT64_TYPE: return internal::FieldSize::EIGHT_BYTES;

    case schema::Type::Body::TEXT_TYPE: return internal::FieldSize::REFERENCE;
    case schema::Type::Body::DATA_TYPE: return internal::FieldSize::REFERENCE;
    case schema::Type::Body::LIST_TYPE: return internal::FieldSize::REFERENCE;
    case schema::Type::Body::ENUM_TYPE: return internal::FieldSize::TWO_BYTES;
    case schema::Type::Body::STRUCT_TYPE: return internal::FieldSize::INLINE_COMPOSITE;
    case schema::Type::Body::INTERFACE_TYPE: return internal::FieldSize::REFERENCE;
    case schema::Type::Body::OBJECT_TYPE: FAIL_CHECK("List(Object) not supported.");
  }
  FAIL_CHECK("Can't get here.");
  return internal::FieldSize::VOID;
}

}  // namespace

// =======================================================================================

schema::EnumNode::Reader DynamicEnum::getSchema() {
  return schema.getBody().getEnumNode();
}

Maybe<schema::EnumNode::Enumerant::Reader> DynamicEnum::getEnumerant() {
  auto enumerants = getSchema().getEnumerants();
  if (value < enumerants.size()) {
    return enumerants[value];
  } else {
    return nullptr;
  }
}

Maybe<schema::EnumNode::Enumerant::Reader> DynamicEnum::findEnumerantByName(Text::Reader name) {
  auto iter = pool->impl->enumerantMap.find(std::make_pair(schema.getId(), name));
  if (iter == pool->impl->enumerantMap.end()) {
    return nullptr;
  } else {
    return iter->second;
  }
}

uint16_t DynamicEnum::toImpl(uint64_t requestedTypeId) {
  VALIDATE_INPUT(requestedTypeId == schema.getId(), "Type mismatch in DynamicEnum.to().") {
    // Go on with value.
  }
  return value;
}

// =======================================================================================

DynamicStruct::Reader DynamicObject::Reader::toStruct(schema::Node::Reader schema) {
  PRECOND(schema.getBody().which() == schema::Node::Body::STRUCT_NODE,
          "toStruct() passed a non-struct schema.");
  if (reader.kind == internal::ObjectKind::NULL_POINTER) {
    return DynamicStruct::Reader(pool, schema, internal::StructReader());
  }
  VALIDATE_INPUT(reader.kind == internal::ObjectKind::STRUCT, "Object is not a struct.") {
    return DynamicStruct::Reader(pool, schema, internal::StructReader());
  }
  return DynamicStruct::Reader(pool, schema, reader.structReader);
}
DynamicStruct::Builder DynamicObject::Builder::toStruct(schema::Node::Reader schema) {
  PRECOND(schema.getBody().which() == schema::Node::Body::STRUCT_NODE,
          "toStruct() passed a non-struct schema.");
  if (builder.kind == internal::ObjectKind::NULL_POINTER) {
    return DynamicStruct::Builder(pool, schema, internal::StructBuilder());
  }
  VALIDATE_INPUT(builder.kind == internal::ObjectKind::STRUCT, "Object is not a struct.") {
    return DynamicStruct::Builder(pool, schema, internal::StructBuilder());
  }
  return DynamicStruct::Builder(pool, schema, builder.structBuilder);
}

DynamicStruct::Reader DynamicObject::Reader::toStruct(uint64_t typeId) {
  return toStruct(pool->getStruct(typeId));
}
DynamicStruct::Builder DynamicObject::Builder::toStruct(uint64_t typeId) {
  return toStruct(pool->getStruct(typeId));
}

DynamicList::Reader DynamicObject::Reader::toList(schema::Type::Reader elementType) {
  return toList(internal::ListSchema(elementType));
}
DynamicList::Builder DynamicObject::Builder::toList(schema::Type::Reader elementType) {
  return toList(internal::ListSchema(elementType));
}

DynamicList::Reader DynamicObject::Reader::toList(internal::ListSchema schema) {
  if (reader.kind == internal::ObjectKind::NULL_POINTER) {
    return DynamicList::Reader(pool, schema, internal::ListReader());
  }
  VALIDATE_INPUT(reader.kind == internal::ObjectKind::LIST, "Object is not a list.") {
    return DynamicList::Reader(pool, schema, internal::ListReader());
  }
  return DynamicList::Reader(pool, schema, reader.listReader);
}
DynamicList::Builder DynamicObject::Builder::toList(internal::ListSchema schema) {
  if (builder.kind == internal::ObjectKind::NULL_POINTER) {
    return DynamicList::Builder(pool, schema, internal::ListBuilder());
  }
  VALIDATE_INPUT(builder.kind == internal::ObjectKind::LIST, "Object is not a list.") {
    return DynamicList::Builder(pool, schema, internal::ListBuilder());
  }
  return DynamicList::Builder(pool, schema, builder.listBuilder);
}

// =======================================================================================

schema::StructNode::Union::Reader DynamicUnion::Reader::getSchema() {
  return schema.getBody().getUnionMember();
}
schema::StructNode::Union::Reader DynamicUnion::Builder::getSchema() {
  return schema.getBody().getUnionMember();
}

Maybe<schema::StructNode::Member::Reader> DynamicUnion::Reader::which() {
  auto uschema = getSchema();
  auto members = uschema.getMembers();
  uint16_t discrim = reader.getDataField<uint32_t>(uschema.getDiscriminantOffset() * ELEMENTS);

  if (discrim < members.size()) {
    return members[discrim];
  } else {
    return nullptr;
  }
}
Maybe<schema::StructNode::Member::Reader> DynamicUnion::Builder::which() {
  auto uschema = getSchema();
  auto members = uschema.getMembers();
  uint16_t discrim = builder.getDataField<uint32_t>(uschema.getDiscriminantOffset() * ELEMENTS);

  if (discrim < members.size()) {
    return members[discrim];
  } else {
    return nullptr;
  }
}

DynamicValue::Reader DynamicUnion::Reader::get() {
  auto w = which();
  RECOVERABLE_PRECOND(w != nullptr, "Can't get() unknown union value.") {
    return DynamicValue::Reader(Void::VOID);
  }
  auto body = w->getBody();
  CHECK(body.which() == schema::StructNode::Member::Body::FIELD_MEMBER,
        "Unsupported union member type.");
  return DynamicValue::Reader(DynamicStruct::Reader::getFieldImpl(
      pool, reader, body.getFieldMember()));
}

DynamicValue::Builder DynamicUnion::Builder::get() {
  auto w = which();
  RECOVERABLE_PRECOND(w != nullptr, "Can't get() unknown union value.") {
    return DynamicValue::Builder(Void::VOID);
  }
  auto body = w->getBody();
  CHECK(body.which() == schema::StructNode::Member::Body::FIELD_MEMBER,
        "Unsupported union member type.");
  return DynamicValue::Builder(DynamicStruct::Builder::getFieldImpl(
      pool, builder, body.getFieldMember()));
}

void DynamicUnion::Builder::set(
    schema::StructNode::Field::Reader field, DynamicValue::Reader value) {
  auto uschema = getSchema();
  builder.setDataField<uint16_t>(uschema.getTagOffset(), field.getIndex());
  DynamicStruct::Builder::setFieldImpl(pool, builder, field, value);
}

// =======================================================================================

DynamicValue::Reader DynamicStruct::Reader::getFieldImpl(
    const SchemaPool* pool, internal::StructReader reader,
    schema::StructNode::Field::Reader field) {
  auto type = field.getType().getBody();
  auto dval = field.getDefaultValue().getBody();

  switch (type.which()) {
    case schema::Type::Body::VOID_TYPE:
      return DynamicValue::Reader(reader.getDataField<Void>(field.getOffset() * ELEMENTS));

#define HANDLE_TYPE(discrim, titleCase, type) \
    case schema::Type::Body::discrim##_TYPE: \
      return DynamicValue::Reader(reader.getDataField<type>( \
          field.getOffset() * ELEMENTS, \
          bitCast<typename internal::MaskType<type>::Type>(dval.get##titleCase##Value())));

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
          pool, pool->getEnum(type.getEnumType()),
          reader.getDataField<uint16_t>(field.getOffset() * ELEMENTS, typedDval)));
    }

    case schema::Type::Body::TEXT_TYPE: {
      Text::Reader typedDval = dval.getTextValue();
      return DynamicValue::Reader(
          reader.getBlobField<Text>(field.getOffset() * REFERENCES,
                                    typedDval.data(), typedDval.size() * BYTES));
    }

    case schema::Type::Body::DATA_TYPE: {
      Data::Reader typedDval = dval.getDataValue();
      return DynamicValue::Reader(
          reader.getBlobField<Data>(field.getOffset() * REFERENCES,
                                    typedDval.data(), typedDval.size() * BYTES));
    }

    case schema::Type::Body::LIST_TYPE: {
      auto elementType = type.getListType();
      return DynamicValue::Reader(DynamicList::Reader(
          pool, elementType,
          reader.getListField(field.getOffset() * REFERENCES,
                              elementSizeFor(elementType),
                              dval.getListValue<internal::TrustedMessage>())));
    }

    case schema::Type::Body::STRUCT_TYPE: {
      return DynamicValue::Reader(DynamicStruct::Reader(
          pool, pool->getStruct(type.getStructType()),
          reader.getStructField(field.getOffset() * REFERENCES,
                                dval.getStructValue<internal::TrustedMessage>())));
    }

    case schema::Type::Body::OBJECT_TYPE: {
      return DynamicValue::Reader(DynamicObject::Reader(
          pool, reader.getObjectField(field.getOffset() * REFERENCES,
                                      dval.getObjectValue<internal::TrustedMessage>())));
    }

    case schema::Type::Body::INTERFACE_TYPE:
      FAIL_CHECK("Interfaces not yet implemented.");
      break;
  }

  FAIL_CHECK("Can't get here.");
  return DynamicValue::Reader();
}

DynamicValue::Builder DynamicStruct::Builder::getFieldImpl(
    const SchemaPool* pool, internal::StructBuilder builder,
    schema::StructNode::Field::Reader field) {
  auto type = field.getType().getBody();
  auto dval = field.getDefaultValue().getBody();

  switch (type.which()) {
    case schema::Type::Body::VOID_TYPE:
      return DynamicValue::Builder(builder.getDataField<Void>(field.getOffset() * ELEMENTS));

#define HANDLE_TYPE(discrim, titleCase, type) \
    case schema::Type::Body::discrim##_TYPE: \
      return DynamicValue::Builder(builder.getDataField<type>( \
          field.getOffset() * ELEMENTS, \
          bitCast<typename internal::MaskType<type>::Type>(dval.get##titleCase##Value())));

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
          pool, pool->getEnum(type.getEnumType()),
          builder.getDataField<uint16_t>(field.getOffset() * ELEMENTS, typedDval)));
    }

    case schema::Type::Body::TEXT_TYPE: {
      Text::Reader typedDval = dval.getTextValue();
      return DynamicValue::Builder(
          builder.getBlobField<Text>(field.getOffset() * REFERENCES,
                                     typedDval.data(), typedDval.size() * BYTES));
    }

    case schema::Type::Body::DATA_TYPE: {
      Data::Reader typedDval = dval.getDataValue();
      return DynamicValue::Builder(
          builder.getBlobField<Data>(field.getOffset() * REFERENCES,
                                     typedDval.data(), typedDval.size() * BYTES));
    }

    case schema::Type::Body::LIST_TYPE: {
      auto elementType = type.getListType();
      return DynamicValue::Builder(DynamicList::Builder(
          pool, elementType,
          builder.getListField(field.getOffset() * REFERENCES,
                               dval.getListValue<internal::TrustedMessage>())));
    }

    case schema::Type::Body::STRUCT_TYPE: {
      auto structNode = pool->getStruct(type.getStructType());
      auto structSchema = structNode.getBody().getStructNode();
      return DynamicValue::Builder(DynamicStruct::Builder(
          pool, structNode,
          builder.getStructField(
              field.getOffset() * REFERENCES,
              internal::StructSize(
                  structSchema.getDataSectionWordSize() * WORDS,
                  structSchema.getPointerSectionSize() * REFERENCES,
                  static_cast<internal::FieldSize>(structSchema.getPreferredListEncoding())),
              dval.getStructValue<internal::TrustedMessage>())));
    }

    case schema::Type::Body::OBJECT_TYPE: {
      return DynamicValue::Builder(DynamicObject::Builder(
          pool, builder.getObjectField(field.getOffset() * REFERENCES,
                                       dval.getObjectValue<internal::TrustedMessage>())));
    }

    case schema::Type::Body::INTERFACE_TYPE:
      FAIL_CHECK("Interfaces not yet implemented.");
      break;
  }

  FAIL_CHECK("Can't get here.");
  return DynamicValue::Builder();
}

void DynamicStruct::Builder::setFieldImpl(
    const SchemaPool* pool, internal::StructBuilder builder,
    schema::StructNode::Field::Reader field, DynamicValue::Reader value) {
  auto type = field.getType().getBody();
  auto dval = field.getDefaultValue().getBody();

  switch (type.which()) {
    case schema::Type::Body::VOID_TYPE:
      builder.setDataField<Void>(field.getOffset() * ELEMENTS, value.to<Void>());
      break;

#define HANDLE_TYPE(discrim, titleCase, type) \
    case schema::Type::Body::discrim##_TYPE: \
      builder.setDataField<type>( \
          field.getOffset() * ELEMENTS, value.to<type>(), \
          bitCast<internal::Mask<type> >(dval.get##titleCase##Value()));
      break;

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

    case schema::Type::Body::ENUM_TYPE:
      builder.setDataField<uint16_t>(
          field.getOffset() * ELEMENTS, value.to<DynamicEnum>().getRaw(),
          dval.getEnumValue());
      break;

    case schema::Type::Body::TEXT_TYPE:
      builder.setBlobField<Text>(field.getOffset() * REFERENCES, value.to<Text>());
      break;

    case schema::Type::Body::DATA_TYPE:
      builder.setBlobField<Data>(field.getOffset() * REFERENCES, value.to<Data>());
      break;

    case schema::Type::Body::LIST_TYPE: {
      // TODO(soon):  We need to do a schemaless copy to avoid losing information if the values are
      //   larger than what the schema defines.
      auto listValue = value.to<DynamicList>();
      initListFieldImpl(pool, builder, field, listValue.size()).copyFrom(listValue);
      break;
    }

    case schema::Type::Body::STRUCT_TYPE: {
      // TODO(soon):  We need to do a schemaless copy to avoid losing information if the values are
      //   larger than what the schema defines.
      initStructFieldImpl(pool, builder, field).copyFrom(value.to<DynamicStruct>());
      break;
    }

    case schema::Type::Body::OBJECT_TYPE: {
      // TODO(soon):  Perform schemaless copy.
      FAIL_CHECK("TODO");
      break;
    }

    case schema::Type::Body::INTERFACE_TYPE:
      FAIL_CHECK("Interfaces not yet implemented.");
      break;
  }
}

// =======================================================================================

#if 0
#define HANDLE_TYPE(name, discrim, typeName) \
ReaderFor<typeName> DynamicValue::Reader::ToImpl<typeName>::apply(Reader reader) { \
  VALIDATE_INPUT(reader.type == schema::Type::Body::discrim##_TYPE, \
      "Type mismatch when using DynamicValue::to().") { \
    return typeName(); \
  } \
  return reader.name##Value; \
} \
BuilderFor<typeName> DynamicValue::Builder::ToImpl<typeName>::apply(Builder builder) { \
  VALIDATE_INPUT(builder.type == schema::Type::Body::discrim##_TYPE, \
      "Type mismatch when using DynamicValue::to().") { \
    return typeName(); \
  } \
  return builder.name##Value; \
}

//HANDLE_TYPE(void, VOID, Void)
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

HANDLE_TYPE(text, TEXT, Text)
HANDLE_TYPE(data, DATA, Data)
HANDLE_TYPE(list, LIST, DynamicList)
HANDLE_TYPE(struct, STRUCT, DynamicStruct)
HANDLE_TYPE(enum, ENUM, DynamicEnum)
HANDLE_TYPE(object, OBJECT, DynamicObject)

#undef HANDLE_TYPE
#endif

// As in the header, HANDLE_TYPE(void, VOID, Void) crashes GCC 4.7.
Void DynamicValue::Reader::ToImpl<Void>::apply(Reader reader) {
  VALIDATE_INPUT(reader.type == schema::Type::Body::VOID_TYPE,
      "Type mismatch when using DynamicValue::to().") {
    return Void();
  }
  return reader.voidValue;
}
Void DynamicValue::Builder::ToImpl<Void>::apply(Builder builder) {
  VALIDATE_INPUT(builder.type == schema::Type::Body::VOID_TYPE,
      "Type mismatch when using DynamicValue::to().") {
    return Void();
  }
  return builder.voidValue;
}

}  // namespace capnproto
