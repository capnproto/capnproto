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
template <>
inline float bitCast<float, uint32_t>(uint32_t value) KJ_UNUSED;
template <>
inline float bitCast<float, uint32_t>(uint32_t value) {
  float result;
  memcpy(&result, &value, sizeof(value));
  return result;
}
template <>
inline double bitCast<double, uint64_t>(uint64_t value) KJ_UNUSED;
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

_::FieldSize elementSizeFor(schema::Type::Which elementType) {
  switch (elementType) {
    case schema::Type::VOID: return _::FieldSize::VOID;
    case schema::Type::BOOL: return _::FieldSize::BIT;
    case schema::Type::INT8: return _::FieldSize::BYTE;
    case schema::Type::INT16: return _::FieldSize::TWO_BYTES;
    case schema::Type::INT32: return _::FieldSize::FOUR_BYTES;
    case schema::Type::INT64: return _::FieldSize::EIGHT_BYTES;
    case schema::Type::UINT8: return _::FieldSize::BYTE;
    case schema::Type::UINT16: return _::FieldSize::TWO_BYTES;
    case schema::Type::UINT32: return _::FieldSize::FOUR_BYTES;
    case schema::Type::UINT64: return _::FieldSize::EIGHT_BYTES;
    case schema::Type::FLOAT32: return _::FieldSize::FOUR_BYTES;
    case schema::Type::FLOAT64: return _::FieldSize::EIGHT_BYTES;

    case schema::Type::TEXT: return _::FieldSize::POINTER;
    case schema::Type::DATA: return _::FieldSize::POINTER;
    case schema::Type::LIST: return _::FieldSize::POINTER;
    case schema::Type::ENUM: return _::FieldSize::TWO_BYTES;
    case schema::Type::STRUCT: return _::FieldSize::INLINE_COMPOSITE;
    case schema::Type::INTERFACE: return _::FieldSize::POINTER;
    case schema::Type::OBJECT: KJ_FAIL_ASSERT("List(Object) not supported."); break;
  }

  // Unknown type.  Treat it as zero-size.
  return _::FieldSize::VOID;
}

inline _::StructSize structSizeFromSchema(StructSchema schema) {
  auto node = schema.getProto().getStruct();
  return _::StructSize(
      node.getDataWordCount() * WORDS,
      node.getPointerCount() * POINTERS,
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

DynamicStruct::Reader DynamicObject::Reader::as(StructSchema schema) const {
  if (reader.kind == _::ObjectKind::NULL_POINTER) {
    return DynamicStruct::Reader(schema, _::StructReader());
  }
  KJ_REQUIRE(reader.kind == _::ObjectKind::STRUCT, "Object is not a struct.") {
    // Return default struct.
    return DynamicStruct::Reader(schema, _::StructReader());
  }
  return DynamicStruct::Reader(schema, reader.structReader);
}

DynamicList::Reader DynamicObject::Reader::as(ListSchema schema) const {
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

bool DynamicStruct::Reader::isSetInUnion(StructSchema::Field field) const {
  auto proto = field.getProto();
  if (proto.hasDiscriminantValue()) {
    uint16_t discrim = reader.getDataField<uint16_t>(
        schema.getProto().getStruct().getDiscriminantOffset() * ELEMENTS);
    return discrim == proto.getDiscriminantValue();
  } else {
    return true;
  }
}

void DynamicStruct::Reader::verifySetInUnion(StructSchema::Field field) const {
  KJ_REQUIRE(isSetInUnion(field),
      "Tried to get() a union member which is not currently initialized.",
      field.getProto().getName(), schema.getProto().getDisplayName());
}

bool DynamicStruct::Builder::isSetInUnion(StructSchema::Field field) {
  auto proto = field.getProto();
  if (proto.hasDiscriminantValue()) {
    uint16_t discrim = builder.getDataField<uint16_t>(
        schema.getProto().getStruct().getDiscriminantOffset() * ELEMENTS);
    return discrim == proto.getDiscriminantValue();
  } else {
    return true;
  }
}

void DynamicStruct::Builder::verifySetInUnion(StructSchema::Field field) {
  KJ_REQUIRE(isSetInUnion(field),
      "Tried to get() a union member which is not currently initialized.",
      field.getProto().getName(), schema.getProto().getDisplayName());
}

void DynamicStruct::Builder::setInUnion(StructSchema::Field field) {
  // If a union member, set the discriminant to match.
  auto proto = field.getProto();
  if (proto.hasDiscriminantValue()) {
    builder.setDataField<uint16_t>(
        schema.getProto().getStruct().getDiscriminantOffset() * ELEMENTS,
        proto.getDiscriminantValue());
  }
}

DynamicValue::Reader DynamicStruct::Reader::get(StructSchema::Field field) const {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");
  verifySetInUnion(field);

  auto proto = field.getProto();
  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();
      auto type = slot.getType();
      auto dval = slot.getDefaultValue();

      switch (type.which()) {
        case schema::Type::VOID:
          return reader.getDataField<Void>(slot.getOffset() * ELEMENTS);

#define HANDLE_TYPE(discrim, titleCase, type) \
        case schema::Type::discrim: \
          return reader.getDataField<type>( \
              slot.getOffset() * ELEMENTS, \
              bitCast<_::Mask<type>>(dval.get##titleCase()));

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

        case schema::Type::ENUM: {
          uint16_t typedDval;
          typedDval = dval.getEnum();
          return DynamicEnum(
              field.getContainingStruct().getDependency(type.getEnum().getTypeId()).asEnum(),
              reader.getDataField<uint16_t>(slot.getOffset() * ELEMENTS, typedDval));
        }

        case schema::Type::TEXT: {
          Text::Reader typedDval = dval.getText();
          return reader.getBlobField<Text>(slot.getOffset() * POINTERS,
                                           typedDval.begin(), typedDval.size() * BYTES);
        }

        case schema::Type::DATA: {
          Data::Reader typedDval = dval.getData();
          return reader.getBlobField<Data>(slot.getOffset() * POINTERS,
                                           typedDval.begin(), typedDval.size() * BYTES);
        }

        case schema::Type::LIST: {
          auto elementType = type.getList().getElementType();
          return DynamicList::Reader(
              ListSchema::of(elementType, field.getContainingStruct()),
              reader.getListField(slot.getOffset() * POINTERS,
                                  elementSizeFor(elementType.which()),
                                  dval.getList<_::UncheckedMessage>()));
        }

        case schema::Type::STRUCT: {
          return DynamicStruct::Reader(
              field.getContainingStruct().getDependency(type.getStruct().getTypeId()).asStruct(),
              reader.getStructField(slot.getOffset() * POINTERS,
                                    dval.getStruct<_::UncheckedMessage>()));
        }

        case schema::Type::OBJECT: {
          return DynamicObject::Reader(
              reader.getObjectField(slot.getOffset() * POINTERS,
                                    dval.getObject<_::UncheckedMessage>()));
        }

        case schema::Type::INTERFACE:
          KJ_FAIL_ASSERT("Interfaces not yet implemented.");
          break;
      }

      KJ_UNREACHABLE;
    }

    case schema::Field::GROUP:
      return DynamicStruct::Reader(
          schema.getDependency(proto.getGroup().getTypeId()).asStruct(), reader);
  }

  KJ_UNREACHABLE;
}

DynamicValue::Builder DynamicStruct::Builder::get(StructSchema::Field field) {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");
  verifySetInUnion(field);

  auto proto = field.getProto();
  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();
      auto type = slot.getType();
      auto dval = slot.getDefaultValue();

      switch (type.which()) {
        case schema::Type::VOID:
          return builder.getDataField<Void>(slot.getOffset() * ELEMENTS);

#define HANDLE_TYPE(discrim, titleCase, type) \
        case schema::Type::discrim: \
          return builder.getDataField<type>( \
              slot.getOffset() * ELEMENTS, \
              bitCast<_::Mask<type>>(dval.get##titleCase()));

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

        case schema::Type::ENUM: {
          uint16_t typedDval;
          typedDval = dval.getEnum();
          return DynamicEnum(
              field.getContainingStruct().getDependency(type.getEnum().getTypeId()).asEnum(),
              builder.getDataField<uint16_t>(slot.getOffset() * ELEMENTS, typedDval));
        }

        case schema::Type::TEXT: {
          Text::Reader typedDval = dval.getText();
          return builder.getBlobField<Text>(slot.getOffset() * POINTERS,
                                            typedDval.begin(), typedDval.size() * BYTES);
        }

        case schema::Type::DATA: {
          Data::Reader typedDval = dval.getData();
          return builder.getBlobField<Data>(slot.getOffset() * POINTERS,
                                            typedDval.begin(), typedDval.size() * BYTES);
        }

        case schema::Type::LIST: {
          ListSchema listType = ListSchema::of(type.getList().getElementType(),
                                               field.getContainingStruct());
          if (listType.whichElementType() == schema::Type::STRUCT) {
            return DynamicList::Builder(listType,
                builder.getStructListField(slot.getOffset() * POINTERS,
                                           structSizeFromSchema(listType.getStructElementType()),
                                           dval.getList<_::UncheckedMessage>()));
          } else {
            return DynamicList::Builder(listType,
                builder.getListField(slot.getOffset() * POINTERS,
                                     elementSizeFor(listType.whichElementType()),
                                     dval.getList<_::UncheckedMessage>()));
          }
        }

        case schema::Type::STRUCT: {
          auto structSchema =
              field.getContainingStruct().getDependency(type.getStruct().getTypeId()).asStruct();
          return DynamicStruct::Builder(structSchema,
              builder.getStructField(
                  slot.getOffset() * POINTERS,
                  structSizeFromSchema(structSchema),
                  dval.getStruct<_::UncheckedMessage>()));
        }

        case schema::Type::OBJECT: {
          return DynamicObject::Builder(
              builder.getObjectField(
                  slot.getOffset() * POINTERS,
                  dval.getObject<_::UncheckedMessage>()));
        }

        case schema::Type::INTERFACE:
          KJ_FAIL_ASSERT("Interfaces not yet implemented.");
          break;
      }

      KJ_UNREACHABLE;
    }

    case schema::Field::GROUP:
      return DynamicStruct::Builder(
          schema.getDependency(proto.getGroup().getTypeId()).asStruct(), builder);
  }

  KJ_UNREACHABLE;
}

bool DynamicStruct::Reader::has(StructSchema::Field field) const {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");

  auto proto = field.getProto();
  if (proto.hasDiscriminantValue()) {
    uint16_t discrim = reader.getDataField<uint16_t>(
        schema.getProto().getStruct().getDiscriminantOffset() * ELEMENTS);
    if (discrim != proto.getDiscriminantValue()) {
      // Field is not active in the union.
      return false;
    }
  }

  switch (proto.which()) {
    case schema::Field::SLOT:
      // Continue to below.
      break;

    case schema::Field::GROUP: {
      // Bleh, we have to check all of the members to see if any are set.
      auto group = get(field).as<DynamicStruct>();

      if (group.schema.getProto().getStruct().getDiscriminantCount() > 0) {
        // This group contains a union.  If the discriminant is non-zero, or if it is zero but the
        // field with discriminant zero is non-default, then we consider the group to be
        // non-default.
        KJ_IF_MAYBE(unionField, group.which()) {
          if (unionField->getProto().getDiscriminantValue() != 0 ||
              group.has(*unionField)) {
            return true;
          }
        } else {
          // Unrecognized discriminant.  Must be non-zero.
          return true;
        }
      }

      // Check if any of the non-union fields are non-default.
      for (auto field: group.schema.getNonUnionFields()) {
        if (group.has(field)) {
          return true;
        }
      }
      return false;
    }
  }

  auto slot = proto.getSlot();
  auto type = slot.getType();

  switch (type.which()) {
    case schema::Type::VOID:
      return false;

#define HANDLE_TYPE(discrim, type) \
    case schema::Type::discrim: \
      return reader.getDataField<type>(slot.getOffset() * ELEMENTS) != 0;

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

    case schema::Type::TEXT:
    case schema::Type::DATA:
    case schema::Type::LIST:
    case schema::Type::STRUCT:
    case schema::Type::OBJECT:
    case schema::Type::INTERFACE:
      return !reader.isPointerFieldNull(slot.getOffset() * POINTERS);
  }

  // Unknown type.  As far as we know, it isn't set.
  return false;
}

kj::Maybe<StructSchema::Field> DynamicStruct::Reader::which() const {
  auto structProto = schema.getProto().getStruct();
  if (structProto.getDiscriminantCount() == 0) {
    return nullptr;
  }

  uint16_t discrim = reader.getDataField<uint16_t>(structProto.getDiscriminantOffset() * ELEMENTS);
  return schema.getFieldByDiscriminant(discrim);
}

kj::Maybe<StructSchema::Field> DynamicStruct::Builder::which() {
  auto structProto = schema.getProto().getStruct();
  if (structProto.getDiscriminantCount() == 0) {
    return nullptr;
  }

  uint16_t discrim = builder.getDataField<uint16_t>(structProto.getDiscriminantOffset() * ELEMENTS);
  return schema.getFieldByDiscriminant(discrim);
}

void DynamicStruct::Builder::set(StructSchema::Field field, const DynamicValue::Reader& value) {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");
  setInUnion(field);

  auto proto = field.getProto();
  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();
      auto type = slot.getType();
      auto dval = slot.getDefaultValue();

      switch (type.which()) {
        case schema::Type::VOID:
          builder.setDataField<Void>(slot.getOffset() * ELEMENTS, value.as<Void>());
          return;

#define HANDLE_TYPE(discrim, titleCase, type) \
        case schema::Type::discrim: \
          builder.setDataField<type>( \
              slot.getOffset() * ELEMENTS, value.as<type>(), \
              bitCast<_::Mask<type> >(dval.get##titleCase())); \
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

        case schema::Type::ENUM: {
          uint16_t rawValue;
          auto enumSchema = field.getContainingStruct()
              .getDependency(type.getEnum().getTypeId()).asEnum();
          if (value.getType() == DynamicValue::TEXT) {
            // Convert from text.
            rawValue = enumSchema.getEnumerantByName(value.as<Text>()).getOrdinal();
          } else {
            DynamicEnum enumValue = value.as<DynamicEnum>();
            KJ_REQUIRE(enumValue.getSchema() == enumSchema, "Value type mismatch.") {
              return;
            }
            rawValue = enumValue.getRaw();
          }
          builder.setDataField<uint16_t>(slot.getOffset() * ELEMENTS, rawValue,
                                         dval.getEnum());
          return;
        }

        case schema::Type::TEXT:
          builder.setBlobField<Text>(slot.getOffset() * POINTERS, value.as<Text>());
          return;

        case schema::Type::DATA:
          builder.setBlobField<Data>(slot.getOffset() * POINTERS, value.as<Data>());
          return;

        case schema::Type::LIST: {
          ListSchema listType = ListSchema::of(type.getList().getElementType(), schema);
          auto listValue = value.as<DynamicList>();
          KJ_REQUIRE(listValue.getSchema() == listType, "Value type mismatch.") {
            return;
          }
          builder.setListField(slot.getOffset() * POINTERS, listValue.reader);
          return;
        }

        case schema::Type::STRUCT: {
          auto structType = schema.getDependency(type.getStruct().getTypeId()).asStruct();
          auto structValue = value.as<DynamicStruct>();
          KJ_REQUIRE(structValue.getSchema() == structType, "Value type mismatch.") {
            return;
          }
          builder.setStructField(slot.getOffset() * POINTERS, structValue.reader);
          return;
        }

        case schema::Type::OBJECT:
          builder.setObjectField(slot.getOffset() * POINTERS, value.as<DynamicObject>().reader);
          return;

        case schema::Type::INTERFACE:
          KJ_FAIL_ASSERT("Interfaces not yet implemented.");
          return;
      }

      KJ_UNREACHABLE;
    }

    case schema::Field::GROUP: {
      auto src = value.as<DynamicStruct>();
      auto dst = init(field).as<DynamicStruct>();

      KJ_IF_MAYBE(unionField, src.which()) {
        dst.set(*unionField, src.get(*unionField));
      }

      for (auto field: src.schema.getNonUnionFields()) {
        if (src.has(field)) {
          dst.set(field, src.get(field));
        }
      }
    }
  }

  KJ_UNREACHABLE;
}

DynamicValue::Builder DynamicStruct::Builder::init(StructSchema::Field field) {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");
  setInUnion(field);

  auto proto = field.getProto();

  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();
      auto type = slot.getType();
      KJ_REQUIRE(type.isStruct(), "init() without a size is only valid for struct fields.");
      auto subSchema = schema.getDependency(type.getStruct().getTypeId()).asStruct();
      return DynamicStruct::Builder(subSchema,
          builder.initStructField(slot.getOffset() * POINTERS,
              structSizeFromSchema(subSchema)));
    }

    case schema::Field::GROUP: {
      clear(field);
      return DynamicStruct::Builder(
          schema.getDependency(proto.getGroup().getTypeId()).asStruct(), builder);
    }
  }

  KJ_UNREACHABLE;
}

DynamicValue::Builder DynamicStruct::Builder::init(StructSchema::Field field, uint size) {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");
  setInUnion(field);

  auto proto = field.getProto();

  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();
      auto type = slot.getType();
      switch (type.which()) {
        case schema::Type::LIST: {
          auto listType = ListSchema::of(type.getList().getElementType(), schema);
          if (listType.whichElementType() == schema::Type::STRUCT) {
            return DynamicList::Builder(listType,
                builder.initStructListField(
                    slot.getOffset() * POINTERS, size * ELEMENTS,
                    structSizeFromSchema(listType.getStructElementType())));
          } else {
            return DynamicList::Builder(listType,
                builder.initListField(
                    slot.getOffset() * POINTERS,
                    elementSizeFor(listType.whichElementType()), size * ELEMENTS));
          }
        }
        case schema::Type::TEXT:
          return builder.initBlobField<Text>(
              proto.getSlot().getOffset() * POINTERS, size * BYTES);
        case schema::Type::DATA:
          return builder.initBlobField<Data>(
              proto.getSlot().getOffset() * POINTERS, size * BYTES);
        default:
          KJ_FAIL_REQUIRE(
              "init() with size is only valid for list, text, or data fields.", (uint)type.which());
          break;
      }
    }

    case schema::Field::GROUP:
      KJ_FAIL_REQUIRE("init() with size is only valid for list, text, or data fields.");
  }

  KJ_UNREACHABLE;
}

void DynamicStruct::Builder::adopt(StructSchema::Field field, Orphan<DynamicValue>&& orphan) {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");
  setInUnion(field);

  auto proto = field.getProto();
  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();
      auto type = slot.getType();

      switch (type.which()) {
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
        case schema::Type::ENUM:
          set(field, orphan.getReader());
          return;

        case schema::Type::TEXT:
          KJ_REQUIRE(orphan.getType() == DynamicValue::TEXT, "Value type mismatch.");
          break;

        case schema::Type::DATA:
          KJ_REQUIRE(orphan.getType() == DynamicValue::DATA, "Value type mismatch.");
          break;

        case schema::Type::LIST: {
          ListSchema listType = ListSchema::of(type.getList().getElementType(), schema);
          KJ_REQUIRE(orphan.getType() == DynamicValue::LIST && orphan.listSchema == listType,
                     "Value type mismatch.") {
            return;
          }
          break;
        }

        case schema::Type::STRUCT: {
          auto structType = schema.getDependency(type.getStruct().getTypeId()).asStruct();
          KJ_REQUIRE(orphan.getType() == DynamicValue::STRUCT && orphan.structSchema == structType,
                     "Value type mismatch.") {
            return;
          }
          break;
        }

        case schema::Type::OBJECT:
          KJ_REQUIRE(orphan.getType() == DynamicValue::STRUCT ||
                     orphan.getType() == DynamicValue::LIST ||
                     orphan.getType() == DynamicValue::OBJECT,
                     "Value type mismatch.") {
            return;
          }
          break;

        case schema::Type::INTERFACE:
          KJ_FAIL_ASSERT("Interfaces not yet implemented.");
          break;
      }

      builder.adopt(slot.getOffset() * POINTERS, kj::mv(orphan.builder));
      return;
    }

    case schema::Field::GROUP:
      // Have to transfer fields.
      auto src = orphan.get().as<DynamicStruct>();
      auto dst = init(field).as<DynamicStruct>();

      KJ_REQUIRE(orphan.getType() == DynamicValue::STRUCT && orphan.structSchema == dst.getSchema(),
                 "Value type mismatch.");

      KJ_IF_MAYBE(unionField, src.which()) {
        dst.adopt(*unionField, src.disown(*unionField));
      }

      for (auto field: src.schema.getNonUnionFields()) {
        if (src.has(field)) {
          dst.adopt(field, src.disown(field));
        }
      }

      return;
  }

  KJ_UNREACHABLE;
}

Orphan<DynamicValue> DynamicStruct::Builder::disown(StructSchema::Field field) {
  // We end up calling get(field) below, so we don't need to validate `field` here.

  auto proto = field.getProto();
  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();

      switch (slot.getType().which()) {
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
        case schema::Type::ENUM: {
          auto result = Orphan<DynamicValue>(get(field), _::OrphanBuilder());
          clear(field);
          return kj::mv(result);
        }

        case schema::Type::TEXT:
        case schema::Type::DATA:
        case schema::Type::LIST:
        case schema::Type::STRUCT:
        case schema::Type::OBJECT:
        case schema::Type::INTERFACE: {
          auto value = get(field);
          return Orphan<DynamicValue>(value, builder.disown(slot.getOffset() * POINTERS));
        }
      }
      KJ_UNREACHABLE;
    }

    case schema::Field::GROUP: {
      // We have to allocate new space for the group, unfortunately.
      auto src = get(field).as<DynamicStruct>();

      Orphan<DynamicStruct> result =
          Orphanage::getForMessageContaining(*this).newOrphan(src.getSchema());
      auto dst = result.get();

      KJ_IF_MAYBE(unionField, src.which()) {
        dst.adopt(*unionField, src.disown(*unionField));
      }

      // We need to explicitly reset the union to its default field.
      KJ_IF_MAYBE(unionField, src.schema.getFieldByDiscriminant(0)) {
        src.clear(*unionField);
      }

      for (auto field: src.schema.getNonUnionFields()) {
        if (src.has(field)) {
          dst.adopt(field, src.disown(field));
        }
      }

      return kj::mv(result);
    }
  }

  KJ_UNREACHABLE;
}

void DynamicStruct::Builder::clear(StructSchema::Field field) {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");
  setInUnion(field);

  auto proto = field.getProto();
  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();
      auto type = slot.getType();

      switch (type.which()) {
        case schema::Type::VOID:
          builder.setDataField<Void>(slot.getOffset() * ELEMENTS, VOID);
          return;

#define HANDLE_TYPE(discrim, type) \
        case schema::Type::discrim: \
          builder.setDataField<type>(slot.getOffset() * ELEMENTS, 0); \
          return;

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

        case schema::Type::TEXT:
        case schema::Type::DATA:
        case schema::Type::LIST:
        case schema::Type::STRUCT:
        case schema::Type::OBJECT:
          builder.clearPointer(slot.getOffset() * POINTERS);
          return;

        case schema::Type::INTERFACE:
          KJ_FAIL_ASSERT("Interfaces not yet implemented.");
          return;
      }

      KJ_UNREACHABLE;
    }

    case schema::Field::GROUP: {
      DynamicStruct::Builder group(
          schema.getDependency(proto.getGroup().getTypeId()).asStruct(), builder);

      // We clear the union field with discriminant 0 rather than the one that is set because
      // we want the union to end up with its default field active.
      KJ_IF_MAYBE(unionField, group.schema.getFieldByDiscriminant(0)) {
        group.clear(*unionField);
      }

      for (auto subField: group.schema.getNonUnionFields()) {
        group.clear(subField);
      }
      return;
    }
  }

  KJ_UNREACHABLE;
}

WirePointerCount DynamicStruct::Builder::verifyIsObject(StructSchema::Field field) {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");

  auto proto = field.getProto();
  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();
      KJ_REQUIRE(slot.getType().isObject(), "Expected an Object.");
      return slot.getOffset() * POINTERS;
    }

    case schema::Field::GROUP:
      KJ_FAIL_REQUIRE("Expected an Object.");
  }

  KJ_UNREACHABLE;
}

DynamicStruct::Builder DynamicStruct::Builder::getObject(
    StructSchema::Field field, StructSchema type) {
  auto offset = verifyIsObject(field);
  verifySetInUnion(field);
  return DynamicStruct::Builder(type,
      builder.getStructField(offset, structSizeFromSchema(type), nullptr));
}

DynamicList::Builder DynamicStruct::Builder::getObject(
    StructSchema::Field field, ListSchema type) {
  auto offset = verifyIsObject(field);
  verifySetInUnion(field);
  if (type.whichElementType() == schema::Type::STRUCT) {
    return DynamicList::Builder(type, builder.getStructListField(
        offset, structSizeFromSchema(type.getStructElementType()), nullptr));
  } else {
    return DynamicList::Builder(type, builder.getListField(
        offset, elementSizeFor(type.whichElementType()), nullptr));
  }
}

Text::Builder DynamicStruct::Builder::getObjectAsText(StructSchema::Field field) {
  auto offset = verifyIsObject(field);
  verifySetInUnion(field);
  return builder.getBlobField<Text>(offset, nullptr, 0 * BYTES);
}

Data::Builder DynamicStruct::Builder::getObjectAsData(StructSchema::Field field) {
  auto offset = verifyIsObject(field);
  verifySetInUnion(field);
  return builder.getBlobField<Data>(offset, nullptr, 0 * BYTES);
}

DynamicStruct::Builder DynamicStruct::Builder::initObject(
    StructSchema::Field field, StructSchema type) {
  auto offset = verifyIsObject(field);
  setInUnion(field);
  return DynamicStruct::Builder(type,
      builder.initStructField(offset, structSizeFromSchema(type)));
}
DynamicList::Builder DynamicStruct::Builder::initObject(
    StructSchema::Field field, ListSchema type, uint size) {
  auto offset = verifyIsObject(field);
  setInUnion(field);
  if (type.whichElementType() == schema::Type::STRUCT) {
    return DynamicList::Builder(type,
        builder.initStructListField(
            offset, size * ELEMENTS, structSizeFromSchema(type.getStructElementType())));
  } else {
    return DynamicList::Builder(type,
        builder.initListField(
            offset, elementSizeFor(type.whichElementType()), size * ELEMENTS));
  }
}
Text::Builder DynamicStruct::Builder::initObjectAsText(StructSchema::Field field, uint size) {
  auto offset = verifyIsObject(field);
  setInUnion(field);
  return builder.initBlobField<Text>(offset, size * BYTES);
}
Data::Builder DynamicStruct::Builder::initObjectAsData(StructSchema::Field field, uint size) {
  auto offset = verifyIsObject(field);
  setInUnion(field);
  return builder.initBlobField<Data>(offset, size * BYTES);
}

DynamicValue::Reader DynamicStruct::Reader::get(kj::StringPtr name) const {
  return get(schema.getFieldByName(name));
}
DynamicValue::Builder DynamicStruct::Builder::get(kj::StringPtr name) {
  return get(schema.getFieldByName(name));
}
bool DynamicStruct::Reader::has(kj::StringPtr name) const {
  return has(schema.getFieldByName(name));
}
bool DynamicStruct::Builder::has(kj::StringPtr name) {
  return has(schema.getFieldByName(name));
}
void DynamicStruct::Builder::set(kj::StringPtr name, const DynamicValue::Reader& value) {
  set(schema.getFieldByName(name), value);
}
void DynamicStruct::Builder::set(kj::StringPtr name,
                                 std::initializer_list<DynamicValue::Reader> value) {
  auto list = init(name, value.size()).as<DynamicList>();
  uint i = 0;
  for (auto element: value) {
    list.set(i++, element);
  }
}
DynamicValue::Builder DynamicStruct::Builder::init(kj::StringPtr name) {
  return init(schema.getFieldByName(name));
}
DynamicValue::Builder DynamicStruct::Builder::init(kj::StringPtr name, uint size) {
  return init(schema.getFieldByName(name), size);
}
void DynamicStruct::Builder::adopt(kj::StringPtr name, Orphan<DynamicValue>&& orphan) {
  adopt(schema.getFieldByName(name), kj::mv(orphan));
}
Orphan<DynamicValue> DynamicStruct::Builder::disown(kj::StringPtr name) {
  return disown(schema.getFieldByName(name));
}
void DynamicStruct::Builder::clear(kj::StringPtr name) {
  clear(schema.getFieldByName(name));
}
DynamicStruct::Builder DynamicStruct::Builder::getObject(
    kj::StringPtr name, StructSchema type) {
  return getObject(schema.getFieldByName(name), type);
}
DynamicList::Builder DynamicStruct::Builder::getObject(kj::StringPtr name, ListSchema type) {
  return getObject(schema.getFieldByName(name), type);
}
Text::Builder DynamicStruct::Builder::getObjectAsText(kj::StringPtr name) {
  return getObjectAsText(schema.getFieldByName(name));
}
Data::Builder DynamicStruct::Builder::getObjectAsData(kj::StringPtr name) {
  return getObjectAsData(schema.getFieldByName(name));
}
DynamicStruct::Builder DynamicStruct::Builder::initObject(
    kj::StringPtr name, StructSchema type) {
  return initObject(schema.getFieldByName(name), type);
}
DynamicList::Builder DynamicStruct::Builder::initObject(
    kj::StringPtr name, ListSchema type, uint size) {
  return initObject(schema.getFieldByName(name), type, size);
}
Text::Builder DynamicStruct::Builder::initObjectAsText(kj::StringPtr name, uint size) {
  return initObjectAsText(schema.getFieldByName(name), size);
}
Data::Builder DynamicStruct::Builder::initObjectAsData(kj::StringPtr name, uint size) {
  return initObjectAsData(schema.getFieldByName(name), size);
}

// =======================================================================================

DynamicValue::Reader DynamicList::Reader::operator[](uint index) const {
  KJ_REQUIRE(index < size(), "List index out-of-bounds.");

  switch (schema.whichElementType()) {
#define HANDLE_TYPE(name, discrim, typeName) \
    case schema::Type::discrim: \
      return reader.getDataElement<typeName>(index * ELEMENTS);

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

    case schema::Type::TEXT:
      return reader.getBlobElement<Text>(index * ELEMENTS);
    case schema::Type::DATA:
      return reader.getBlobElement<Data>(index * ELEMENTS);

    case schema::Type::LIST: {
      auto elementType = schema.getListElementType();
      return DynamicList::Reader(elementType, reader.getListElement(
          index * ELEMENTS, elementSizeFor(elementType.whichElementType())));
    }

    case schema::Type::STRUCT:
      return DynamicStruct::Reader(schema.getStructElementType(),
                                   reader.getStructElement(index * ELEMENTS));

    case schema::Type::ENUM:
      return DynamicEnum(schema.getEnumElementType(),
                         reader.getDataElement<uint16_t>(index * ELEMENTS));

    case schema::Type::OBJECT:
      return DynamicObject::Reader(reader.getObjectElement(index * ELEMENTS));

    case schema::Type::INTERFACE:
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
    case schema::Type::discrim: \
      return builder.getDataElement<typeName>(index * ELEMENTS);

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

    case schema::Type::TEXT:
      return builder.getBlobElement<Text>(index * ELEMENTS);
    case schema::Type::DATA:
      return builder.getBlobElement<Data>(index * ELEMENTS);

    case schema::Type::LIST: {
      ListSchema elementType = schema.getListElementType();
      if (elementType.whichElementType() == schema::Type::STRUCT) {
        return DynamicList::Builder(elementType,
            builder.getStructListElement(
                index * ELEMENTS,
                structSizeFromSchema(elementType.getStructElementType())));
      } else {
        return DynamicList::Builder(elementType,
            builder.getListElement(
                index * ELEMENTS,
                elementSizeFor(elementType.whichElementType())));
      }
    }

    case schema::Type::STRUCT:
      return DynamicStruct::Builder(schema.getStructElementType(),
                                    builder.getStructElement(index * ELEMENTS));

    case schema::Type::ENUM:
      return DynamicEnum(schema.getEnumElementType(),
                         builder.getDataElement<uint16_t>(index * ELEMENTS));

    case schema::Type::OBJECT:
      KJ_FAIL_ASSERT("List(Object) not supported.");
      return nullptr;

    case schema::Type::INTERFACE:
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
    case schema::Type::discrim: \
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

    case schema::Type::TEXT:
      builder.setBlobElement<Text>(index * ELEMENTS, value.as<Text>());
      return;
    case schema::Type::DATA:
      builder.setBlobElement<Data>(index * ELEMENTS, value.as<Data>());
      return;

    case schema::Type::LIST: {
      auto listValue = value.as<DynamicList>();
      KJ_REQUIRE(listValue.getSchema() == schema.getListElementType(), "Value type mismatch.") {
        return;
      }
      builder.setListElement(index * ELEMENTS, listValue.reader);
      return;
    }

    case schema::Type::STRUCT: {
      auto structValue = value.as<DynamicStruct>();
      KJ_REQUIRE(structValue.getSchema() == schema.getStructElementType(), "Value type mismatch.") {
        return;
      }
      builder.getStructElement(index * ELEMENTS).copyContentFrom(structValue.reader);
      return;
    }

    case schema::Type::ENUM: {
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

    case schema::Type::OBJECT:
      KJ_FAIL_ASSERT("List(Object) not supported.") {
        return;
      }

    case schema::Type::INTERFACE:
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
    case schema::Type::ENUM:
    case schema::Type::STRUCT:
    case schema::Type::INTERFACE:
      KJ_FAIL_REQUIRE("Expected a list or blob.");
      return nullptr;

    case schema::Type::TEXT:
      return builder.initBlobElement<Text>(index * ELEMENTS, size * BYTES);

    case schema::Type::DATA:
      return builder.initBlobElement<Data>(index * ELEMENTS, size * BYTES);

    case schema::Type::LIST: {
      auto elementType = schema.getListElementType();

      if (elementType.whichElementType() == schema::Type::STRUCT) {
        return DynamicList::Builder(
            elementType, builder.initStructListElement(
                index * ELEMENTS, size * ELEMENTS,
                structSizeFromSchema(elementType.getStructElementType())));
      } else {
        return DynamicList::Builder(
            elementType, builder.initListElement(
                index * ELEMENTS, elementSizeFor(elementType.whichElementType()),
                size * ELEMENTS));
      }
    }

    case schema::Type::OBJECT: {
      KJ_FAIL_ASSERT("List(Object) not supported.");
      return nullptr;
    }
  }

  return nullptr;
}

void DynamicList::Builder::adopt(uint index, Orphan<DynamicValue>&& orphan) {
  switch (schema.whichElementType()) {
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
    case schema::Type::ENUM:
      set(index, orphan.getReader());
      return;

    case schema::Type::TEXT:
      KJ_REQUIRE(orphan.getType() == DynamicValue::TEXT, "Value type mismatch.");
      builder.adopt(index * ELEMENTS, kj::mv(orphan.builder));
      return;

    case schema::Type::DATA:
      KJ_REQUIRE(orphan.getType() == DynamicValue::DATA, "Value type mismatch.");
      builder.adopt(index * ELEMENTS, kj::mv(orphan.builder));
      return;

    case schema::Type::LIST: {
      ListSchema elementType = schema.getListElementType();
      KJ_REQUIRE(orphan.getType() == DynamicValue::LIST && orphan.listSchema == elementType,
                 "Value type mismatch.");
      builder.adopt(index * ELEMENTS, kj::mv(orphan.builder));
      return;
    }

    case schema::Type::STRUCT: {
      auto elementType = schema.getStructElementType();
      KJ_REQUIRE(orphan.getType() == DynamicValue::STRUCT && orphan.structSchema == elementType,
                 "Value type mismatch.");
      builder.getStructElement(index * ELEMENTS).transferContentFrom(
          orphan.builder.asStruct(structSizeFromSchema(elementType)));
      return;
    }

    case schema::Type::OBJECT:
      KJ_FAIL_ASSERT("List(Object) not supported.");

    case schema::Type::INTERFACE:
      KJ_FAIL_ASSERT("Interfaces not yet implemented.");
  }

  KJ_UNREACHABLE;
}

Orphan<DynamicValue> DynamicList::Builder::disown(uint index) {
  switch (schema.whichElementType()) {
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
    case schema::Type::ENUM: {
      auto result = Orphan<DynamicValue>(operator[](index), _::OrphanBuilder());
      switch (elementSizeFor(schema.whichElementType())) {
        case _::FieldSize::VOID: break;
        case _::FieldSize::BIT: builder.setDataElement<bool>(index * ELEMENTS, false); break;
        case _::FieldSize::BYTE: builder.setDataElement<uint8_t>(index * ELEMENTS, 0); break;
        case _::FieldSize::TWO_BYTES: builder.setDataElement<uint16_t>(index * ELEMENTS, 0); break;
        case _::FieldSize::FOUR_BYTES: builder.setDataElement<uint32_t>(index * ELEMENTS, 0); break;
        case _::FieldSize::EIGHT_BYTES: builder.setDataElement<uint64_t>(index * ELEMENTS, 0);break;

        case _::FieldSize::POINTER:
        case _::FieldSize::INLINE_COMPOSITE:
          KJ_UNREACHABLE;
      }
      return kj::mv(result);
    }

    case schema::Type::TEXT:
    case schema::Type::DATA:
    case schema::Type::LIST:
    case schema::Type::OBJECT:
    case schema::Type::INTERFACE: {
      auto value = operator[](index);
      return Orphan<DynamicValue>(value, builder.disown(index * ELEMENTS));
    }

    case schema::Type::STRUCT: {
      // We have to make a copy.
      Orphan<DynamicStruct> result =
          Orphanage::getForMessageContaining(*this).newOrphan(schema.getStructElementType());
      auto element = builder.getStructElement(index * ELEMENTS);
      result.get().builder.transferContentFrom(element);
      element.clearAll();
      return kj::mv(result);
    }
  }
  KJ_UNREACHABLE;
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

DynamicValue::Reader::Reader(ConstSchema constant) {
  auto typeSchema = constant.getProto().getConst().getType();
  auto value = constant.getProto().getConst().getValue();
  switch (typeSchema.which()) {
    case schema::Type::VOID: *this = capnp::VOID; break;
    case schema::Type::BOOL: *this = value.getBool(); break;
    case schema::Type::INT8: *this = value.getInt8(); break;
    case schema::Type::INT16: *this = value.getInt16(); break;
    case schema::Type::INT32: *this = value.getInt32(); break;
    case schema::Type::INT64: *this = value.getInt64(); break;
    case schema::Type::UINT8: *this = value.getUint8(); break;
    case schema::Type::UINT16: *this = value.getUint16(); break;
    case schema::Type::UINT32: *this = value.getUint32(); break;
    case schema::Type::UINT64: *this = value.getUint64(); break;
    case schema::Type::FLOAT32: *this = value.getFloat32(); break;
    case schema::Type::FLOAT64: *this = value.getFloat64(); break;
    case schema::Type::TEXT: *this = value.getText(); break;
    case schema::Type::DATA: *this = value.getData(); break;

    case schema::Type::ENUM:
      *this = DynamicEnum(constant.getDependency(
          typeSchema.getEnum().getTypeId()).asEnum(), value.getEnum());
      break;

    case schema::Type::STRUCT:
      *this = value.getStruct<DynamicStruct>(
          constant.getDependency(typeSchema.getStruct().getTypeId()).asStruct());
      break;

    case schema::Type::LIST:
      *this = value.getList<DynamicList>(
          ListSchema::of(typeSchema.getList().getElementType(), constant));
      break;

    case schema::Type::OBJECT:
      *this = value.getObject<DynamicObject>();
      break;

    case schema::Type::INTERFACE:
      KJ_FAIL_ASSERT("Constants can't have interface type.");
  }
}

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
    case INTERFACE: KJ_FAIL_ASSERT("Interfaces not implemented."); return Reader();
    case OBJECT: return Reader(objectValue.asReader());
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
  KJ_REQUIRE(builder.type == discrim, "Value type mismatch."); \
  return builder.name##Value; \
}

//HANDLE_TYPE(void, VOID, Void)
HANDLE_TYPE(bool, BOOL, bool)

HANDLE_TYPE(text, TEXT, Text)
HANDLE_TYPE(list, LIST, DynamicList)
HANDLE_TYPE(struct, STRUCT, DynamicStruct)
HANDLE_TYPE(enum, ENUM, DynamicEnum)
HANDLE_TYPE(object, OBJECT, DynamicObject)

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
  KJ_REQUIRE(!schema.getProto().getStruct().getIsGroup(),
             "Can't use group type as the root of a message.");
  return DynamicStruct::Reader(schema, getRootInternal());
}

template <>
DynamicStruct::Builder MessageBuilder::initRoot<DynamicStruct>(StructSchema schema) {
  KJ_REQUIRE(!schema.getProto().getStruct().getIsGroup(),
             "Can't use group type as the root of a message.");
  return DynamicStruct::Builder(schema, initRoot(structSizeFromSchema(schema)));
}

template <>
DynamicStruct::Builder MessageBuilder::getRoot<DynamicStruct>(StructSchema schema) {
  KJ_REQUIRE(!schema.getProto().getStruct().getIsGroup(),
             "Can't use group type as the root of a message.");
  return DynamicStruct::Builder(schema, getRoot(structSizeFromSchema(schema)));
}

template <>
void MessageBuilder::setRoot<DynamicStruct::Reader>(DynamicStruct::Reader&& value) {
  setRootInternal(value.reader);
}
template <>
void MessageBuilder::setRoot<const DynamicStruct::Reader&>(const DynamicStruct::Reader& value) {
  setRootInternal(value.reader);
}
template <>
void MessageBuilder::setRoot<DynamicStruct::Reader&>(DynamicStruct::Reader& value) {
  setRootInternal(value.reader);
}

namespace _ {  // private

DynamicStruct::Reader PointerHelpers<DynamicStruct, Kind::UNKNOWN>::getDynamic(
    StructReader reader, WirePointerCount index, StructSchema schema) {
  KJ_REQUIRE(!schema.getProto().getStruct().getIsGroup(),
             "Cannot form pointer to group type.");
  return DynamicStruct::Reader(schema, reader.getStructField(index, nullptr));
}
DynamicStruct::Builder PointerHelpers<DynamicStruct, Kind::UNKNOWN>::getDynamic(
    StructBuilder builder, WirePointerCount index, StructSchema schema) {
  KJ_REQUIRE(!schema.getProto().getStruct().getIsGroup(),
             "Cannot form pointer to group type.");
  return DynamicStruct::Builder(schema, builder.getStructField(
      index, structSizeFromSchema(schema), nullptr));
}
void PointerHelpers<DynamicStruct, Kind::UNKNOWN>::set(
    StructBuilder builder, WirePointerCount index, const DynamicStruct::Reader& value) {
  KJ_REQUIRE(!value.schema.getProto().getStruct().getIsGroup(),
             "Cannot form pointer to group type.");
  builder.setStructField(index, value.reader);
}
DynamicStruct::Builder PointerHelpers<DynamicStruct, Kind::UNKNOWN>::init(
    StructBuilder builder, WirePointerCount index, StructSchema schema) {
  KJ_REQUIRE(!schema.getProto().getStruct().getIsGroup(),
             "Cannot form pointer to group type.");
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
  if (schema.whichElementType() == schema::Type::STRUCT) {
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
  if (schema.whichElementType() == schema::Type::STRUCT) {
    return DynamicList::Builder(schema,
        builder.initStructListField(index, size * ELEMENTS,
            structSizeFromSchema(schema.getStructElementType())));
  } else {
    return DynamicList::Builder(schema,
        builder.initListField(index, elementSizeFor(schema.whichElementType()), size * ELEMENTS));
  }
}

DynamicObject::Reader PointerHelpers<DynamicObject, Kind::UNKNOWN>::get(
    StructReader reader, WirePointerCount index) {
  return DynamicObject::Reader(reader.getObjectField(index, nullptr));
}
DynamicObject::Builder PointerHelpers<DynamicObject, Kind::UNKNOWN>::get(
    StructBuilder builder, WirePointerCount index) {
  return DynamicObject::Builder(builder.getObjectField(index, nullptr));
}
void PointerHelpers<DynamicObject, Kind::UNKNOWN>::set(
    StructBuilder builder, WirePointerCount index, const DynamicObject::Reader& value) {
  builder.setObjectField(index, value.reader);
}

}  // namespace _ (private)

// -------------------------------------------------------------------

Orphan<DynamicStruct> Orphanage::newOrphan(StructSchema schema) const {
  return Orphan<DynamicStruct>(
      schema, _::OrphanBuilder::initStruct(arena, structSizeFromSchema(schema)));
}

Orphan<DynamicList> Orphanage::newOrphan(ListSchema schema, uint size) const {
  if (schema.whichElementType() == schema::Type::STRUCT) {
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
  if (schema.whichElementType() == schema::Type::STRUCT) {
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

DynamicObject::Builder Orphan<DynamicObject>::get() {
  return DynamicObject::Builder(builder.asObject());
}
DynamicObject::Reader Orphan<DynamicObject>::getReader() const {
  return DynamicObject::Reader(builder.asObjectReader());
}

DynamicStruct::Builder Orphan<DynamicObject>::getAs(StructSchema schema) {
  return DynamicStruct::Builder(schema, builder.asStruct(structSizeFromSchema(schema)));
}
DynamicList::Builder Orphan<DynamicObject>::getAs(ListSchema schema) {
  if (schema.whichElementType() == schema::Type::STRUCT) {
    return DynamicList::Builder(schema,
        builder.asStructList(structSizeFromSchema(schema.getStructElementType())));
  } else {
    return DynamicList::Builder(schema,
        builder.asList(elementSizeFor(schema.whichElementType())));
  }
}
template <>
Text::Builder Orphan<DynamicObject>::getAs<Text>() {
  return builder.asText();
}
template <>
Data::Builder Orphan<DynamicObject>::getAs<Data>() {
  return builder.asData();
}

Orphan<DynamicStruct> Orphan<DynamicObject>::releaseAs(StructSchema schema) {
  getAs(schema);  // type check
  return Orphan<DynamicStruct>(schema, kj::mv(builder));
}
Orphan<DynamicList> Orphan<DynamicObject>::releaseAs(ListSchema schema) {
  getAs(schema);  // type check
  return Orphan<DynamicList>(schema, kj::mv(builder));
}
template <>
Orphan<Text> Orphan<DynamicObject>::releaseAs<Text>() {
  getAs<Text>();  // type check
  return Orphan<Text>(kj::mv(builder));
}
template <>
Orphan<Data> Orphan<DynamicObject>::releaseAs<Data>() {
  getAs<Data>();  // type check
  return Orphan<Data>(kj::mv(builder));
}

Orphan<DynamicValue>::Orphan(DynamicValue::Builder value, _::OrphanBuilder&& builder)
    : type(value.getType()), builder(kj::mv(builder)) {
  switch (type) {
    case DynamicValue::UNKNOWN: break;
    case DynamicValue::VOID: voidValue = value.voidValue; break;
    case DynamicValue::BOOL: boolValue = value.boolValue; break;
    case DynamicValue::INT: intValue = value.intValue; break;
    case DynamicValue::UINT: uintValue = value.uintValue; break;
    case DynamicValue::FLOAT: floatValue = value.floatValue; break;
    case DynamicValue::ENUM: enumValue = value.enumValue; break;

    case DynamicValue::TEXT: break;
    case DynamicValue::DATA: break;
    case DynamicValue::LIST: listSchema = value.listValue.getSchema(); break;
    case DynamicValue::STRUCT: structSchema = value.structValue.getSchema(); break;
    case DynamicValue::INTERFACE: KJ_FAIL_ASSERT("Interfaces not implemented.");
    case DynamicValue::OBJECT: break;
  }
}

DynamicValue::Builder Orphan<DynamicValue>::get() {
  switch (type) {
    case DynamicValue::UNKNOWN: return nullptr;
    case DynamicValue::VOID: return voidValue;
    case DynamicValue::BOOL: return boolValue;
    case DynamicValue::INT: return intValue;
    case DynamicValue::UINT: return uintValue;
    case DynamicValue::FLOAT: return floatValue;
    case DynamicValue::ENUM: return enumValue;

    case DynamicValue::TEXT: return builder.asText();
    case DynamicValue::DATA: return builder.asData();
    case DynamicValue::LIST:
      if (listSchema.whichElementType() == schema::Type::STRUCT) {
        return DynamicList::Builder(listSchema,
            builder.asStructList(structSizeFromSchema(listSchema.getStructElementType())));
      } else {
        return DynamicList::Builder(listSchema,
            builder.asList(elementSizeFor(listSchema.whichElementType())));
      }
    case DynamicValue::STRUCT:
      return DynamicStruct::Builder(structSchema,
          builder.asStruct(structSizeFromSchema(structSchema)));
    case DynamicValue::INTERFACE:
      KJ_FAIL_ASSERT("Interfaces not implemented.");
    case DynamicValue::OBJECT:
      return DynamicObject::Builder(builder.asObject());
  }
  KJ_UNREACHABLE;
}
DynamicValue::Reader Orphan<DynamicValue>::getReader() const {
  switch (type) {
    case DynamicValue::UNKNOWN: return nullptr;
    case DynamicValue::VOID: return voidValue;
    case DynamicValue::BOOL: return boolValue;
    case DynamicValue::INT: return intValue;
    case DynamicValue::UINT: return uintValue;
    case DynamicValue::FLOAT: return floatValue;
    case DynamicValue::ENUM: return enumValue;

    case DynamicValue::TEXT: return builder.asTextReader();
    case DynamicValue::DATA: return builder.asDataReader();
    case DynamicValue::LIST:
      return DynamicList::Reader(listSchema,
          builder.asListReader(elementSizeFor(listSchema.whichElementType())));
    case DynamicValue::STRUCT:
      return DynamicStruct::Reader(structSchema,
          builder.asStructReader(structSizeFromSchema(structSchema)));
    case DynamicValue::INTERFACE:
      KJ_FAIL_ASSERT("Interfaces not implemented.");
    case DynamicValue::OBJECT:
      return DynamicObject::Reader(builder.asObjectReader());
  }
  KJ_UNREACHABLE;
}

template <>
Orphan<DynamicStruct> Orphan<DynamicValue>::releaseAs<DynamicStruct>() {
  KJ_REQUIRE(type == DynamicValue::STRUCT, "Value type mismatch.");
  type = DynamicValue::UNKNOWN;
  return Orphan<DynamicStruct>(structSchema, kj::mv(builder));
}
template <>
Orphan<DynamicList> Orphan<DynamicValue>::releaseAs<DynamicList>() {
  KJ_REQUIRE(type == DynamicValue::LIST, "Value type mismatch.");
  type = DynamicValue::UNKNOWN;
  return Orphan<DynamicList>(listSchema, kj::mv(builder));
}

template <>
Orphan<DynamicObject> Orphanage::newOrphanCopy<DynamicObject::Reader>(
    const DynamicObject::Reader& copyFrom) const {
  switch (copyFrom.reader.kind) {
    case _::ObjectKind::NULL_POINTER:
      return Orphan<DynamicObject>();
    case _::ObjectKind::STRUCT:
      return Orphan<DynamicObject>(_::OrphanBuilder::copy(arena, copyFrom.reader.structReader));
    case _::ObjectKind::LIST:
      return Orphan<DynamicObject>(_::OrphanBuilder::copy(arena, copyFrom.reader.listReader));
  }
  KJ_UNREACHABLE;
}

template <>
Orphan<DynamicValue> Orphanage::newOrphanCopy<DynamicValue::Reader>(
    const DynamicValue::Reader& copyFrom) const {
  switch (copyFrom.getType()) {
    case DynamicValue::UNKNOWN: return nullptr;
    case DynamicValue::VOID: return copyFrom.voidValue;
    case DynamicValue::BOOL: return copyFrom.boolValue;
    case DynamicValue::INT: return copyFrom.intValue;
    case DynamicValue::UINT: return copyFrom.uintValue;
    case DynamicValue::FLOAT: return copyFrom.floatValue;
    case DynamicValue::ENUM: return copyFrom.enumValue;

    case DynamicValue::TEXT: return newOrphanCopy(copyFrom.textValue);
    case DynamicValue::DATA: return newOrphanCopy(copyFrom.dataValue);
    case DynamicValue::LIST: return newOrphanCopy(copyFrom.listValue);
    case DynamicValue::STRUCT: return newOrphanCopy(copyFrom.structValue);
    case DynamicValue::INTERFACE: KJ_FAIL_ASSERT("Interfaces not implemented.");
    case DynamicValue::OBJECT: return newOrphanCopy(copyFrom.objectValue);
  }
  KJ_UNREACHABLE;
}

}  // namespace capnp
