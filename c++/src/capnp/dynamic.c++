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

#include "dynamic.h"
#include <kj/debug.h>

namespace capnp {

namespace {

bool hasDiscriminantValue(const schema::Field::Reader& reader) {
  return reader.getDiscriminantValue() != schema::Field::NO_DISCRIMINANT;
}

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

ElementSize elementSizeFor(schema::Type::Which elementType) {
  switch (elementType) {
    case schema::Type::VOID: return ElementSize::VOID;
    case schema::Type::BOOL: return ElementSize::BIT;
    case schema::Type::INT8: return ElementSize::BYTE;
    case schema::Type::INT16: return ElementSize::TWO_BYTES;
    case schema::Type::INT32: return ElementSize::FOUR_BYTES;
    case schema::Type::INT64: return ElementSize::EIGHT_BYTES;
    case schema::Type::UINT8: return ElementSize::BYTE;
    case schema::Type::UINT16: return ElementSize::TWO_BYTES;
    case schema::Type::UINT32: return ElementSize::FOUR_BYTES;
    case schema::Type::UINT64: return ElementSize::EIGHT_BYTES;
    case schema::Type::FLOAT32: return ElementSize::FOUR_BYTES;
    case schema::Type::FLOAT64: return ElementSize::EIGHT_BYTES;

    case schema::Type::TEXT: return ElementSize::POINTER;
    case schema::Type::DATA: return ElementSize::POINTER;
    case schema::Type::LIST: return ElementSize::POINTER;
    case schema::Type::ENUM: return ElementSize::TWO_BYTES;
    case schema::Type::STRUCT: return ElementSize::INLINE_COMPOSITE;
    case schema::Type::INTERFACE: return ElementSize::POINTER;
    case schema::Type::ANY_POINTER: KJ_FAIL_ASSERT("List(AnyPointer) not supported."); break;
  }

  // Unknown type.  Treat it as zero-size.
  return ElementSize::VOID;
}

inline _::StructSize structSizeFromSchema(StructSchema schema) {
  auto node = schema.getProto().getStruct();
  return _::StructSize(
      bounded(node.getDataWordCount()) * WORDS,
      bounded(node.getPointerCount()) * POINTERS);
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

bool DynamicStruct::Reader::isSetInUnion(StructSchema::Field field) const {
  auto proto = field.getProto();
  if (hasDiscriminantValue(proto)) {
    uint16_t discrim = reader.getDataField<uint16_t>(
        assumeDataOffset(schema.getProto().getStruct().getDiscriminantOffset()));
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
  if (hasDiscriminantValue(proto)) {
    uint16_t discrim = builder.getDataField<uint16_t>(
        assumeDataOffset(schema.getProto().getStruct().getDiscriminantOffset()));
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
  if (hasDiscriminantValue(proto)) {
    builder.setDataField<uint16_t>(
        assumeDataOffset(schema.getProto().getStruct().getDiscriminantOffset()),
        proto.getDiscriminantValue());
  }
}

DynamicValue::Reader DynamicStruct::Reader::get(StructSchema::Field field) const {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");
  verifySetInUnion(field);

  auto type = field.getType();
  auto proto = field.getProto();
  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();

      // Note that the default value might be "anyPointer" even if the type is some poniter type
      // *other than* anyPointer. This happens with generics -- the field is actually a generic
      // parameter that has been bound, but the default value was of course compiled without any
      // binding available.
      auto dval = slot.getDefaultValue();

      switch (type.which()) {
        case schema::Type::VOID:
          return reader.getDataField<Void>(assumeDataOffset(slot.getOffset()));

#define HANDLE_TYPE(discrim, titleCase, type) \
        case schema::Type::discrim: \
          return reader.getDataField<type>( \
              assumeDataOffset(slot.getOffset()), \
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
          uint16_t typedDval = dval.getEnum();
          return DynamicEnum(type.asEnum(),
              reader.getDataField<uint16_t>(assumeDataOffset(slot.getOffset()), typedDval));
        }

        case schema::Type::TEXT: {
          Text::Reader typedDval = dval.isAnyPointer() ? Text::Reader() : dval.getText();
          return reader.getPointerField(assumePointerOffset(slot.getOffset()))
                       .getBlob<Text>(typedDval.begin(),
                           assumeMax<MAX_TEXT_SIZE>(typedDval.size()) * BYTES);
        }

        case schema::Type::DATA: {
          Data::Reader typedDval = dval.isAnyPointer() ? Data::Reader() : dval.getData();
          return reader.getPointerField(assumePointerOffset(slot.getOffset()))
                       .getBlob<Data>(typedDval.begin(),
                           assumeBits<BLOB_SIZE_BITS>(typedDval.size()) * BYTES);
        }

        case schema::Type::LIST: {
          auto elementType = type.asList().getElementType();
          return DynamicList::Reader(type.asList(),
              reader.getPointerField(assumePointerOffset(slot.getOffset()))
                    .getList(elementSizeFor(elementType.which()), dval.isAnyPointer() ? nullptr :
                        dval.getList().getAs<_::UncheckedMessage>()));
        }

        case schema::Type::STRUCT:
          return DynamicStruct::Reader(type.asStruct(),
              reader.getPointerField(assumePointerOffset(slot.getOffset()))
                    .getStruct(dval.isAnyPointer() ? nullptr :
                        dval.getStruct().getAs<_::UncheckedMessage>()));

        case schema::Type::ANY_POINTER:
          return AnyPointer::Reader(reader.getPointerField(assumePointerOffset(slot.getOffset())));

        case schema::Type::INTERFACE:
          return DynamicCapability::Client(type.asInterface(),
              reader.getPointerField(assumePointerOffset(slot.getOffset())).getCapability());
      }

      KJ_UNREACHABLE;
    }

    case schema::Field::GROUP:
      return DynamicStruct::Reader(type.asStruct(), reader);
  }

  KJ_UNREACHABLE;
}

DynamicValue::Builder DynamicStruct::Builder::get(StructSchema::Field field) {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");
  verifySetInUnion(field);

  auto proto = field.getProto();
  auto type = field.getType();
  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();

      // Note that the default value might be "anyPointer" even if the type is some poniter type
      // *other than* anyPointer. This happens with generics -- the field is actually a generic
      // parameter that has been bound, but the default value was of course compiled without any
      // binding available.
      auto dval = slot.getDefaultValue();

      switch (type.which()) {
        case schema::Type::VOID:
          return builder.getDataField<Void>(assumeDataOffset(slot.getOffset()));

#define HANDLE_TYPE(discrim, titleCase, type) \
        case schema::Type::discrim: \
          return builder.getDataField<type>( \
              assumeDataOffset(slot.getOffset()), \
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
          uint16_t typedDval = dval.getEnum();
          return DynamicEnum(type.asEnum(),
              builder.getDataField<uint16_t>(assumeDataOffset(slot.getOffset()), typedDval));
        }

        case schema::Type::TEXT: {
          Text::Reader typedDval = dval.isAnyPointer() ? Text::Reader() : dval.getText();
          return builder.getPointerField(assumePointerOffset(slot.getOffset()))
                        .getBlob<Text>(typedDval.begin(),
                            assumeMax<MAX_TEXT_SIZE>(typedDval.size()) * BYTES);
        }

        case schema::Type::DATA: {
          Data::Reader typedDval = dval.isAnyPointer() ? Data::Reader() : dval.getData();
          return builder.getPointerField(assumePointerOffset(slot.getOffset()))
                        .getBlob<Data>(typedDval.begin(),
                            assumeBits<BLOB_SIZE_BITS>(typedDval.size()) * BYTES);
        }

        case schema::Type::LIST: {
          ListSchema listType = type.asList();
          if (listType.whichElementType() == schema::Type::STRUCT) {
            return DynamicList::Builder(listType,
                builder.getPointerField(assumePointerOffset(slot.getOffset()))
                       .getStructList(structSizeFromSchema(listType.getStructElementType()),
                                      dval.isAnyPointer() ? nullptr :
                                          dval.getList().getAs<_::UncheckedMessage>()));
          } else {
            return DynamicList::Builder(listType,
                builder.getPointerField(assumePointerOffset(slot.getOffset()))
                       .getList(elementSizeFor(listType.whichElementType()),
                                dval.isAnyPointer() ? nullptr :
                                    dval.getList().getAs<_::UncheckedMessage>()));
          }
        }

        case schema::Type::STRUCT: {
          auto structSchema = type.asStruct();
          return DynamicStruct::Builder(structSchema,
              builder.getPointerField(assumePointerOffset(slot.getOffset()))
                     .getStruct(structSizeFromSchema(structSchema),
                                dval.isAnyPointer() ? nullptr :
                                    dval.getStruct().getAs<_::UncheckedMessage>()));
        }

        case schema::Type::ANY_POINTER:
          return AnyPointer::Builder(
              builder.getPointerField(assumePointerOffset(slot.getOffset())));

        case schema::Type::INTERFACE:
          return DynamicCapability::Client(type.asInterface(),
              builder.getPointerField(assumePointerOffset(slot.getOffset())).getCapability());
      }

      KJ_UNREACHABLE;
    }

    case schema::Field::GROUP:
      return DynamicStruct::Builder(type.asStruct(), builder);
  }

  KJ_UNREACHABLE;
}

DynamicValue::Pipeline DynamicStruct::Pipeline::get(StructSchema::Field field) {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");

  auto proto = field.getProto();
  KJ_REQUIRE(!hasDiscriminantValue(proto), "Can't pipeline on union members.");

  auto type = field.getType();

  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();

      switch (type.which()) {
        case schema::Type::STRUCT:
          return DynamicStruct::Pipeline(type.asStruct(),
              typeless.getPointerField(slot.getOffset()));

        case schema::Type::INTERFACE:
          return DynamicCapability::Client(type.asInterface(),
              typeless.getPointerField(slot.getOffset()).asCap());

        case schema::Type::ANY_POINTER:
          switch (type.whichAnyPointerKind()) {
            case schema::Type::AnyPointer::Unconstrained::STRUCT:
              return DynamicStruct::Pipeline(StructSchema(),
                  typeless.getPointerField(slot.getOffset()));
            case schema::Type::AnyPointer::Unconstrained::CAPABILITY:
              return DynamicCapability::Client(Capability::Client(
                  typeless.getPointerField(slot.getOffset()).asCap()));
            default:
              KJ_FAIL_REQUIRE("Can only pipeline on struct and interface fields.");
          }

        default:
          KJ_FAIL_REQUIRE("Can only pipeline on struct and interface fields.");
      }

      KJ_UNREACHABLE;
    }

    case schema::Field::GROUP:
      return DynamicStruct::Pipeline(type.asStruct(), typeless.noop());
  }

  KJ_UNREACHABLE;
}

bool DynamicStruct::Reader::has(StructSchema::Field field) const {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");

  auto proto = field.getProto();
  if (hasDiscriminantValue(proto)) {
    uint16_t discrim = reader.getDataField<uint16_t>(
        assumeDataOffset(schema.getProto().getStruct().getDiscriminantOffset()));
    if (discrim != proto.getDiscriminantValue()) {
      // Field is not active in the union.
      return false;
    }
  }

  switch (proto.which()) {
    case schema::Field::SLOT:
      // Continue to below.
      break;

    case schema::Field::GROUP:
      return true;
  }

  auto slot = proto.getSlot();
  auto type = field.getType();

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
      // Primitive types are always present.
      return true;

    case schema::Type::TEXT:
    case schema::Type::DATA:
    case schema::Type::LIST:
    case schema::Type::STRUCT:
    case schema::Type::ANY_POINTER:
    case schema::Type::INTERFACE:
      return !reader.getPointerField(assumePointerOffset(slot.getOffset())).isNull();
  }

  // Unknown type.  As far as we know, it isn't set.
  return false;
}

kj::Maybe<StructSchema::Field> DynamicStruct::Reader::which() const {
  auto structProto = schema.getProto().getStruct();
  if (structProto.getDiscriminantCount() == 0) {
    return nullptr;
  }

  uint16_t discrim = reader.getDataField<uint16_t>(
      assumeDataOffset(structProto.getDiscriminantOffset()));
  return schema.getFieldByDiscriminant(discrim);
}

kj::Maybe<StructSchema::Field> DynamicStruct::Builder::which() {
  auto structProto = schema.getProto().getStruct();
  if (structProto.getDiscriminantCount() == 0) {
    return nullptr;
  }

  uint16_t discrim = builder.getDataField<uint16_t>(
      assumeDataOffset(structProto.getDiscriminantOffset()));
  return schema.getFieldByDiscriminant(discrim);
}

void DynamicStruct::Builder::set(StructSchema::Field field, const DynamicValue::Reader& value) {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");
  setInUnion(field);

  auto proto = field.getProto();
  auto type = field.getType();
  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();
      auto dval = slot.getDefaultValue();

      switch (type.which()) {
        case schema::Type::VOID:
          builder.setDataField<Void>(assumeDataOffset(slot.getOffset()), value.as<Void>());
          return;

#define HANDLE_TYPE(discrim, titleCase, type) \
        case schema::Type::discrim: \
          builder.setDataField<type>( \
              assumeDataOffset(slot.getOffset()), value.as<type>(), \
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
          auto enumSchema = type.asEnum();
          if (value.getType() == DynamicValue::TEXT) {
            // Convert from text.
            rawValue = enumSchema.getEnumerantByName(value.as<Text>()).getOrdinal();
          } else if (value.getType() == DynamicValue::INT ||
                     value.getType() == DynamicValue::UINT) {
            rawValue = value.as<uint16_t>();
          } else {
            DynamicEnum enumValue = value.as<DynamicEnum>();
            KJ_REQUIRE(enumValue.getSchema() == enumSchema, "Value type mismatch.") {
              return;
            }
            rawValue = enumValue.getRaw();
          }
          builder.setDataField<uint16_t>(assumeDataOffset(slot.getOffset()), rawValue,
                                         dval.getEnum());
          return;
        }

        case schema::Type::TEXT:
          builder.getPointerField(assumePointerOffset(slot.getOffset()))
                 .setBlob<Text>(value.as<Text>());
          return;

        case schema::Type::DATA:
          builder.getPointerField(assumePointerOffset(slot.getOffset()))
                 .setBlob<Data>(value.as<Data>());
          return;

        case schema::Type::LIST: {
          ListSchema listType = type.asList();
          auto listValue = value.as<DynamicList>();
          KJ_REQUIRE(listValue.getSchema() == listType, "Value type mismatch.") {
            return;
          }
          builder.getPointerField(assumePointerOffset(slot.getOffset()))
                 .setList(listValue.reader);
          return;
        }

        case schema::Type::STRUCT: {
          auto structType = type.asStruct();
          auto structValue = value.as<DynamicStruct>();
          KJ_REQUIRE(structValue.getSchema() == structType, "Value type mismatch.") {
            return;
          }
          builder.getPointerField(assumePointerOffset(slot.getOffset()))
                 .setStruct(structValue.reader);
          return;
        }

        case schema::Type::ANY_POINTER: {
          auto target = AnyPointer::Builder(
              builder.getPointerField(assumePointerOffset(slot.getOffset())));

          switch (value.getType()) {
            case DynamicValue::Type::TEXT:
              target.setAs<Text>(value.as<Text>());
              return;
            case DynamicValue::Type::DATA:
              target.setAs<Data>(value.as<Data>());
              return;
            case DynamicValue::Type::LIST:
              target.setAs<DynamicList>(value.as<DynamicList>());
              return;
            case DynamicValue::Type::STRUCT:
              target.setAs<DynamicStruct>(value.as<DynamicStruct>());
              return;
            case DynamicValue::Type::CAPABILITY:
              target.setAs<DynamicCapability>(value.as<DynamicCapability>());
              return;
            case DynamicValue::Type::ANY_POINTER:
              target.set(value.as<AnyPointer>());
              return;

            case DynamicValue::Type::UNKNOWN:
            case DynamicValue::Type::VOID:
            case DynamicValue::Type::BOOL:
            case DynamicValue::Type::INT:
            case DynamicValue::Type::UINT:
            case DynamicValue::Type::FLOAT:
            case DynamicValue::Type::ENUM:
              KJ_FAIL_ASSERT("Value type mismatch; expected AnyPointer");
          }

          KJ_UNREACHABLE;
        }

        case schema::Type::INTERFACE: {
          auto interfaceType = type.asInterface();
          auto capability = value.as<DynamicCapability>();
          KJ_REQUIRE(capability.getSchema().extends(interfaceType), "Value type mismatch.") {
            return;
          }
          builder.getPointerField(assumePointerOffset(slot.getOffset()))
                 .setCapability(kj::mv(capability.hook));
          return;
        }
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
  auto type = field.getType();

  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();
      switch (type.which()) {
        case schema::Type::STRUCT: {
          auto subSchema = type.asStruct();
          return DynamicStruct::Builder(subSchema,
              builder.getPointerField(assumePointerOffset(slot.getOffset()))
                     .initStruct(structSizeFromSchema(subSchema)));
        }
        case schema::Type::ANY_POINTER: {
          auto pointer = builder.getPointerField(assumePointerOffset(slot.getOffset()));
          pointer.clear();
          return AnyPointer::Builder(pointer);
        }
        default:
          KJ_FAIL_REQUIRE("init() without a size is only valid for struct and object fields.");
      }
    }

    case schema::Field::GROUP: {
      clear(field);
      return DynamicStruct::Builder(type.asStruct(), builder);
    }
  }

  KJ_UNREACHABLE;
}

DynamicValue::Builder DynamicStruct::Builder::init(StructSchema::Field field, uint size) {
  KJ_REQUIRE(field.getContainingStruct() == schema, "`field` is not a field of this struct.");
  setInUnion(field);

  auto proto = field.getProto();
  auto type = field.getType();

  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();
      switch (type.which()) {
        case schema::Type::LIST: {
          auto listType = type.asList();
          if (listType.whichElementType() == schema::Type::STRUCT) {
            return DynamicList::Builder(listType,
                builder.getPointerField(assumePointerOffset(slot.getOffset()))
                       .initStructList(bounded(size) * ELEMENTS,
                                       structSizeFromSchema(listType.getStructElementType())));
          } else {
            return DynamicList::Builder(listType,
                builder.getPointerField(assumePointerOffset(slot.getOffset()))
                       .initList(elementSizeFor(listType.whichElementType()),
                                 bounded(size) * ELEMENTS));
          }
        }
        case schema::Type::TEXT:
          return builder.getPointerField(assumePointerOffset(slot.getOffset()))
                        .initBlob<Text>(bounded(size) * BYTES);
        case schema::Type::DATA:
          return builder.getPointerField(assumePointerOffset(slot.getOffset()))
                        .initBlob<Data>(bounded(size) * BYTES);
        default:
          KJ_FAIL_REQUIRE(
              "init() with size is only valid for list, text, or data fields.",
              (uint)type.which());
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
      auto type = field.getType();

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
          ListSchema listType = type.asList();
          KJ_REQUIRE(orphan.getType() == DynamicValue::LIST && orphan.listSchema == listType,
                     "Value type mismatch.") {
            return;
          }
          break;
        }

        case schema::Type::STRUCT: {
          auto structType = type.asStruct();
          KJ_REQUIRE(orphan.getType() == DynamicValue::STRUCT && orphan.structSchema == structType,
                     "Value type mismatch.") {
            return;
          }
          break;
        }

        case schema::Type::ANY_POINTER:
          KJ_REQUIRE(orphan.getType() == DynamicValue::STRUCT ||
                     orphan.getType() == DynamicValue::LIST ||
                     orphan.getType() == DynamicValue::TEXT ||
                     orphan.getType() == DynamicValue::DATA ||
                     orphan.getType() == DynamicValue::CAPABILITY ||
                     orphan.getType() == DynamicValue::ANY_POINTER,
                     "Value type mismatch.") {
            return;
          }
          break;

        case schema::Type::INTERFACE: {
          auto interfaceType = type.asInterface();
          KJ_REQUIRE(orphan.getType() == DynamicValue::CAPABILITY &&
                     orphan.interfaceSchema.extends(interfaceType),
                     "Value type mismatch.") {
            return;
          }
          break;
        }
      }

      builder.getPointerField(assumePointerOffset(slot.getOffset())).adopt(kj::mv(orphan.builder));
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

      switch (field.getType().which()) {
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
        case schema::Type::ANY_POINTER:
        case schema::Type::INTERFACE: {
          auto value = get(field);
          return Orphan<DynamicValue>(
              value, builder.getPointerField(assumePointerOffset(slot.getOffset())).disown());
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
  auto type = field.getType();
  switch (proto.which()) {
    case schema::Field::SLOT: {
      auto slot = proto.getSlot();

      switch (type.which()) {
        case schema::Type::VOID:
          builder.setDataField<Void>(assumeDataOffset(slot.getOffset()), VOID);
          return;

#define HANDLE_TYPE(discrim, type) \
        case schema::Type::discrim: \
          builder.setDataField<type>(assumeDataOffset(slot.getOffset()), 0); \
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
        case schema::Type::ANY_POINTER:
        case schema::Type::INTERFACE:
          builder.getPointerField(assumePointerOffset(slot.getOffset())).clear();
          return;
      }

      KJ_UNREACHABLE;
    }

    case schema::Field::GROUP: {
      DynamicStruct::Builder group(type.asStruct(), builder);

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

DynamicValue::Reader DynamicStruct::Reader::get(kj::StringPtr name) const {
  return get(schema.getFieldByName(name));
}
DynamicValue::Builder DynamicStruct::Builder::get(kj::StringPtr name) {
  return get(schema.getFieldByName(name));
}
DynamicValue::Pipeline DynamicStruct::Pipeline::get(kj::StringPtr name) {
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

// =======================================================================================

DynamicValue::Reader DynamicList::Reader::operator[](uint index) const {
  KJ_REQUIRE(index < size(), "List index out-of-bounds.");

  switch (schema.whichElementType()) {
#define HANDLE_TYPE(name, discrim, typeName) \
    case schema::Type::discrim: \
      return reader.getDataElement<typeName>(bounded(index) * ELEMENTS);

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
      return reader.getPointerElement(bounded(index) * ELEMENTS)
                   .getBlob<Text>(nullptr, ZERO * BYTES);
    case schema::Type::DATA:
      return reader.getPointerElement(bounded(index) * ELEMENTS)
                   .getBlob<Data>(nullptr, ZERO * BYTES);

    case schema::Type::LIST: {
      auto elementType = schema.getListElementType();
      return DynamicList::Reader(elementType,
          reader.getPointerElement(bounded(index) * ELEMENTS)
                .getList(elementSizeFor(elementType.whichElementType()), nullptr));
    }

    case schema::Type::STRUCT:
      return DynamicStruct::Reader(schema.getStructElementType(),
                                   reader.getStructElement(bounded(index) * ELEMENTS));

    case schema::Type::ENUM:
      return DynamicEnum(schema.getEnumElementType(),
                         reader.getDataElement<uint16_t>(bounded(index) * ELEMENTS));

    case schema::Type::ANY_POINTER:
      return AnyPointer::Reader(reader.getPointerElement(bounded(index) * ELEMENTS));

    case schema::Type::INTERFACE:
      return DynamicCapability::Client(schema.getInterfaceElementType(),
                                       reader.getPointerElement(bounded(index) * ELEMENTS)
                                             .getCapability());
  }

  return nullptr;
}

DynamicValue::Builder DynamicList::Builder::operator[](uint index) {
  KJ_REQUIRE(index < size(), "List index out-of-bounds.");

  switch (schema.whichElementType()) {
#define HANDLE_TYPE(name, discrim, typeName) \
    case schema::Type::discrim: \
      return builder.getDataElement<typeName>(bounded(index) * ELEMENTS);

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
      return builder.getPointerElement(bounded(index) * ELEMENTS)
                    .getBlob<Text>(nullptr, ZERO * BYTES);
    case schema::Type::DATA:
      return builder.getPointerElement(bounded(index) * ELEMENTS)
                    .getBlob<Data>(nullptr, ZERO * BYTES);

    case schema::Type::LIST: {
      ListSchema elementType = schema.getListElementType();
      if (elementType.whichElementType() == schema::Type::STRUCT) {
        return DynamicList::Builder(elementType,
            builder.getPointerElement(bounded(index) * ELEMENTS)
                   .getStructList(structSizeFromSchema(elementType.getStructElementType()),
                                  nullptr));
      } else {
        return DynamicList::Builder(elementType,
            builder.getPointerElement(bounded(index) * ELEMENTS)
                   .getList(elementSizeFor(elementType.whichElementType()), nullptr));
      }
    }

    case schema::Type::STRUCT:
      return DynamicStruct::Builder(schema.getStructElementType(),
                                    builder.getStructElement(bounded(index) * ELEMENTS));

    case schema::Type::ENUM:
      return DynamicEnum(schema.getEnumElementType(),
                         builder.getDataElement<uint16_t>(bounded(index) * ELEMENTS));

    case schema::Type::ANY_POINTER:
      KJ_FAIL_ASSERT("List(AnyPointer) not supported.");
      return nullptr;

    case schema::Type::INTERFACE:
      return DynamicCapability::Client(schema.getInterfaceElementType(),
                                       builder.getPointerElement(bounded(index) * ELEMENTS)
                                              .getCapability());
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
      builder.setDataElement<typeName>(bounded(index) * ELEMENTS, value.as<typeName>()); \
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
      builder.getPointerElement(bounded(index) * ELEMENTS).setBlob<Text>(value.as<Text>());
      return;
    case schema::Type::DATA:
      builder.getPointerElement(bounded(index) * ELEMENTS).setBlob<Data>(value.as<Data>());
      return;

    case schema::Type::LIST: {
      auto listValue = value.as<DynamicList>();
      KJ_REQUIRE(listValue.getSchema() == schema.getListElementType(), "Value type mismatch.") {
        return;
      }
      builder.getPointerElement(bounded(index) * ELEMENTS).setList(listValue.reader);
      return;
    }

    case schema::Type::STRUCT: {
      auto structValue = value.as<DynamicStruct>();
      KJ_REQUIRE(structValue.getSchema() == schema.getStructElementType(), "Value type mismatch.") {
        return;
      }
      builder.getStructElement(bounded(index) * ELEMENTS).copyContentFrom(structValue.reader);
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
      builder.setDataElement<uint16_t>(bounded(index) * ELEMENTS, rawValue);
      return;
    }

    case schema::Type::ANY_POINTER:
      KJ_FAIL_ASSERT("List(AnyPointer) not supported.") {
        return;
      }

    case schema::Type::INTERFACE: {
      auto capValue = value.as<DynamicCapability>();
      KJ_REQUIRE(capValue.getSchema().extends(schema.getInterfaceElementType()),
                 "Value type mismatch.") {
        return;
      }
      builder.getPointerElement(bounded(index) * ELEMENTS).setCapability(kj::mv(capValue.hook));
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
      return builder.getPointerElement(bounded(index) * ELEMENTS)
                    .initBlob<Text>(bounded(size) * BYTES);

    case schema::Type::DATA:
      return builder.getPointerElement(bounded(index) * ELEMENTS)
                    .initBlob<Data>(bounded(size) * BYTES);

    case schema::Type::LIST: {
      auto elementType = schema.getListElementType();

      if (elementType.whichElementType() == schema::Type::STRUCT) {
        return DynamicList::Builder(elementType,
            builder.getPointerElement(bounded(index) * ELEMENTS)
                   .initStructList(bounded(size) * ELEMENTS,
                                   structSizeFromSchema(elementType.getStructElementType())));
      } else {
        return DynamicList::Builder(elementType,
            builder.getPointerElement(bounded(index) * ELEMENTS)
                   .initList(elementSizeFor(elementType.whichElementType()),
                             bounded(size) * ELEMENTS));
      }
    }

    case schema::Type::ANY_POINTER: {
      KJ_FAIL_ASSERT("List(AnyPointer) not supported.");
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
      builder.getPointerElement(bounded(index) * ELEMENTS).adopt(kj::mv(orphan.builder));
      return;

    case schema::Type::DATA:
      KJ_REQUIRE(orphan.getType() == DynamicValue::DATA, "Value type mismatch.");
      builder.getPointerElement(bounded(index) * ELEMENTS).adopt(kj::mv(orphan.builder));
      return;

    case schema::Type::LIST: {
      ListSchema elementType = schema.getListElementType();
      KJ_REQUIRE(orphan.getType() == DynamicValue::LIST && orphan.listSchema == elementType,
                 "Value type mismatch.");
      builder.getPointerElement(bounded(index) * ELEMENTS).adopt(kj::mv(orphan.builder));
      return;
    }

    case schema::Type::STRUCT: {
      auto elementType = schema.getStructElementType();
      KJ_REQUIRE(orphan.getType() == DynamicValue::STRUCT && orphan.structSchema == elementType,
                 "Value type mismatch.");
      builder.getStructElement(bounded(index) * ELEMENTS).transferContentFrom(
          orphan.builder.asStruct(structSizeFromSchema(elementType)));
      return;
    }

    case schema::Type::ANY_POINTER:
      KJ_FAIL_ASSERT("List(AnyPointer) not supported.");

    case schema::Type::INTERFACE: {
      auto elementType = schema.getInterfaceElementType();
      KJ_REQUIRE(orphan.getType() == DynamicValue::CAPABILITY &&
                 orphan.interfaceSchema.extends(elementType),
                 "Value type mismatch.");
      builder.getPointerElement(bounded(index) * ELEMENTS).adopt(kj::mv(orphan.builder));
      return;
    }
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
        case ElementSize::VOID: break;
        case ElementSize::BIT: builder.setDataElement<bool>(bounded(index) * ELEMENTS, false); break;
        case ElementSize::BYTE: builder.setDataElement<uint8_t>(bounded(index) * ELEMENTS, 0); break;
        case ElementSize::TWO_BYTES: builder.setDataElement<uint16_t>(bounded(index) * ELEMENTS, 0); break;
        case ElementSize::FOUR_BYTES: builder.setDataElement<uint32_t>(bounded(index) * ELEMENTS, 0); break;
        case ElementSize::EIGHT_BYTES: builder.setDataElement<uint64_t>(bounded(index) * ELEMENTS, 0);break;

        case ElementSize::POINTER:
        case ElementSize::INLINE_COMPOSITE:
          KJ_UNREACHABLE;
      }
      return kj::mv(result);
    }

    case schema::Type::TEXT:
    case schema::Type::DATA:
    case schema::Type::LIST:
    case schema::Type::ANY_POINTER:
    case schema::Type::INTERFACE: {
      auto value = operator[](index);
      return Orphan<DynamicValue>(value, builder.getPointerElement(bounded(index) * ELEMENTS).disown());
    }

    case schema::Type::STRUCT: {
      // We have to make a copy.
      Orphan<DynamicStruct> result =
          Orphanage::getForMessageContaining(*this).newOrphan(schema.getStructElementType());
      auto element = builder.getStructElement(bounded(index) * ELEMENTS);
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

DynamicValue::Reader::Reader(ConstSchema constant): type(VOID) {
  auto type = constant.getType();
  auto value = constant.getProto().getConst().getValue();
  switch (type.which()) {
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
      *this = DynamicEnum(type.asEnum(), value.getEnum());
      break;

    case schema::Type::STRUCT:
      *this = value.getStruct().getAs<DynamicStruct>(type.asStruct());
      break;

    case schema::Type::LIST:
      *this = value.getList().getAs<DynamicList>(type.asList());
      break;

    case schema::Type::ANY_POINTER:
      *this = value.getAnyPointer();
      break;

    case schema::Type::INTERFACE:
      KJ_FAIL_ASSERT("Constants can't have interface type.");
  }
}

DynamicValue::Reader::Reader(const Reader& other) {
  switch (other.type) {
    case UNKNOWN:
    case VOID:
    case BOOL:
    case INT:
    case UINT:
    case FLOAT:
    case TEXT:
    case DATA:
    case LIST:
    case ENUM:
    case STRUCT:
    case ANY_POINTER:
      KJ_ASSERT_CAN_MEMCPY(Text::Reader);
      KJ_ASSERT_CAN_MEMCPY(Data::Reader);
      KJ_ASSERT_CAN_MEMCPY(DynamicList::Reader);
      KJ_ASSERT_CAN_MEMCPY(DynamicEnum);
      KJ_ASSERT_CAN_MEMCPY(DynamicStruct::Reader);
      KJ_ASSERT_CAN_MEMCPY(AnyPointer::Reader);
      break;

    case CAPABILITY:
      type = CAPABILITY;
      kj::ctor(capabilityValue, other.capabilityValue);
      return;
  }

  memcpy(this, &other, sizeof(*this));
}
DynamicValue::Reader::Reader(Reader&& other) noexcept {
  switch (other.type) {
    case UNKNOWN:
    case VOID:
    case BOOL:
    case INT:
    case UINT:
    case FLOAT:
    case TEXT:
    case DATA:
    case LIST:
    case ENUM:
    case STRUCT:
    case ANY_POINTER:
      KJ_ASSERT_CAN_MEMCPY(Text::Reader);
      KJ_ASSERT_CAN_MEMCPY(Data::Reader);
      KJ_ASSERT_CAN_MEMCPY(DynamicList::Reader);
      KJ_ASSERT_CAN_MEMCPY(DynamicEnum);
      KJ_ASSERT_CAN_MEMCPY(DynamicStruct::Reader);
      KJ_ASSERT_CAN_MEMCPY(AnyPointer::Reader);
      break;

    case CAPABILITY:
      type = CAPABILITY;
      kj::ctor(capabilityValue, kj::mv(other.capabilityValue));
      return;
  }

  memcpy(this, &other, sizeof(*this));
}
DynamicValue::Reader::~Reader() noexcept(false) {
  if (type == CAPABILITY) {
    kj::dtor(capabilityValue);
  }
}

DynamicValue::Reader& DynamicValue::Reader::operator=(const Reader& other) {
  if (type == CAPABILITY) {
    kj::dtor(capabilityValue);
  }
  kj::ctor(*this, other);
  return *this;
}
DynamicValue::Reader& DynamicValue::Reader::operator=(Reader&& other) {
  if (type == CAPABILITY) {
    kj::dtor(capabilityValue);
  }
  kj::ctor(*this, kj::mv(other));
  return *this;
}

DynamicValue::Builder::Builder(Builder& other) {
  switch (other.type) {
    case UNKNOWN:
    case VOID:
    case BOOL:
    case INT:
    case UINT:
    case FLOAT:
    case TEXT:
    case DATA:
    case LIST:
    case ENUM:
    case STRUCT:
    case ANY_POINTER:
      // Unfortunately canMemcpy() doesn't work on these types due to the use of
      // DisallowConstCopy, but __has_trivial_destructor should detect if any of these types
      // become non-trivial.
      static_assert(__has_trivial_destructor(Text::Builder) &&
                    __has_trivial_destructor(Data::Builder) &&
                    __has_trivial_destructor(DynamicList::Builder) &&
                    __has_trivial_destructor(DynamicEnum) &&
                    __has_trivial_destructor(DynamicStruct::Builder) &&
                    __has_trivial_destructor(AnyPointer::Builder),
                    "Assumptions here don't hold.");
      break;

    case CAPABILITY:
      type = CAPABILITY;
      kj::ctor(capabilityValue, other.capabilityValue);
      return;
  }

  memcpy(this, &other, sizeof(*this));
}
DynamicValue::Builder::Builder(Builder&& other) noexcept {
  switch (other.type) {
    case UNKNOWN:
    case VOID:
    case BOOL:
    case INT:
    case UINT:
    case FLOAT:
    case TEXT:
    case DATA:
    case LIST:
    case ENUM:
    case STRUCT:
    case ANY_POINTER:
      // Unfortunately __has_trivial_copy doesn't work on these types due to the use of
      // DisallowConstCopy, but __has_trivial_destructor should detect if any of these types
      // become non-trivial.
      static_assert(__has_trivial_destructor(Text::Builder) &&
                    __has_trivial_destructor(Data::Builder) &&
                    __has_trivial_destructor(DynamicList::Builder) &&
                    __has_trivial_destructor(DynamicEnum) &&
                    __has_trivial_destructor(DynamicStruct::Builder) &&
                    __has_trivial_destructor(AnyPointer::Builder),
                    "Assumptions here don't hold.");
      break;

    case CAPABILITY:
      type = CAPABILITY;
      kj::ctor(capabilityValue, kj::mv(other.capabilityValue));
      return;
  }

  memcpy(this, &other, sizeof(*this));
}
DynamicValue::Builder::~Builder() noexcept(false) {
  if (type == CAPABILITY) {
    kj::dtor(capabilityValue);
  }
}

DynamicValue::Builder& DynamicValue::Builder::operator=(Builder& other) {
  if (type == CAPABILITY) {
    kj::dtor(capabilityValue);
  }
  kj::ctor(*this, other);
  return *this;
}
DynamicValue::Builder& DynamicValue::Builder::operator=(Builder&& other) {
  if (type == CAPABILITY) {
    kj::dtor(capabilityValue);
  }
  kj::ctor(*this, kj::mv(other));
  return *this;
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
    case CAPABILITY: return Reader(capabilityValue);
    case ANY_POINTER: return Reader(anyPointerValue.asReader());
  }
  KJ_FAIL_ASSERT("Missing switch case.");
  return Reader();
}

DynamicValue::Pipeline::Pipeline(Pipeline&& other) noexcept: type(other.type) {
  switch (type) {
    case UNKNOWN: break;
    case STRUCT: kj::ctor(structValue, kj::mv(other.structValue)); break;
    case CAPABILITY: kj::ctor(capabilityValue, kj::mv(other.capabilityValue)); break;
    default:
      KJ_LOG(ERROR, "Unexpected pipeline type.", (uint)type);
      type = UNKNOWN;
      break;
  }
}
DynamicValue::Pipeline& DynamicValue::Pipeline::operator=(Pipeline&& other) {
  kj::dtor(*this);
  kj::ctor(*this, kj::mv(other));
  return *this;
}
DynamicValue::Pipeline::~Pipeline() noexcept(false) {
  switch (type) {
    case UNKNOWN: break;
    case STRUCT: kj::dtor(structValue); break;
    case CAPABILITY: kj::dtor(capabilityValue); break;
    default:
      KJ_FAIL_ASSERT("Unexpected pipeline type.", (uint)type) { type = UNKNOWN; break; }
      break;
  }
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
HANDLE_TYPE(anyPointer, ANY_POINTER, AnyPointer)

#undef HANDLE_TYPE

PipelineFor<DynamicStruct> DynamicValue::Pipeline::AsImpl<DynamicStruct>::apply(
    Pipeline& pipeline) {
  KJ_REQUIRE(pipeline.type == STRUCT, "Pipeline type mismatch.");
  return kj::mv(pipeline.structValue);
}

ReaderFor<DynamicCapability> DynamicValue::Reader::AsImpl<DynamicCapability>::apply(
    const Reader& reader) {
  KJ_REQUIRE(reader.type == CAPABILITY, "Value type mismatch.") {
    return DynamicCapability::Client();
  }
  return reader.capabilityValue;
}
BuilderFor<DynamicCapability> DynamicValue::Builder::AsImpl<DynamicCapability>::apply(
    Builder& builder) {
  KJ_REQUIRE(builder.type == CAPABILITY, "Value type mismatch.") {
    return DynamicCapability::Client();
  }
  return builder.capabilityValue;
}
PipelineFor<DynamicCapability> DynamicValue::Pipeline::AsImpl<DynamicCapability>::apply(
    Pipeline& pipeline) {
  KJ_REQUIRE(pipeline.type == CAPABILITY, "Pipeline type mismatch.") {
    return DynamicCapability::Client();
  }
  return kj::mv(pipeline.capabilityValue);
}

Data::Reader DynamicValue::Reader::AsImpl<Data>::apply(const Reader& reader) {
  if (reader.type == TEXT) {
    // Coerce text to data.
    return reader.textValue.asBytes();
  }
  KJ_REQUIRE(reader.type == DATA, "Value type mismatch.") {
    return Data::Reader();
  }
  return reader.dataValue;
}
Data::Builder DynamicValue::Builder::AsImpl<Data>::apply(Builder& builder) {
  if (builder.type == TEXT) {
    // Coerce text to data.
    return builder.textValue.asBytes();
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

namespace _ {  // private

DynamicStruct::Reader PointerHelpers<DynamicStruct, Kind::OTHER>::getDynamic(
    PointerReader reader, StructSchema schema) {
  KJ_REQUIRE(!schema.getProto().getStruct().getIsGroup(),
             "Cannot form pointer to group type.");
  return DynamicStruct::Reader(schema, reader.getStruct(nullptr));
}
DynamicStruct::Builder PointerHelpers<DynamicStruct, Kind::OTHER>::getDynamic(
    PointerBuilder builder, StructSchema schema) {
  KJ_REQUIRE(!schema.getProto().getStruct().getIsGroup(),
             "Cannot form pointer to group type.");
  return DynamicStruct::Builder(schema, builder.getStruct(
      structSizeFromSchema(schema), nullptr));
}
void PointerHelpers<DynamicStruct, Kind::OTHER>::set(
    PointerBuilder builder, const DynamicStruct::Reader& value) {
  KJ_REQUIRE(!value.schema.getProto().getStruct().getIsGroup(),
             "Cannot form pointer to group type.");
  builder.setStruct(value.reader);
}
DynamicStruct::Builder PointerHelpers<DynamicStruct, Kind::OTHER>::init(
    PointerBuilder builder, StructSchema schema) {
  KJ_REQUIRE(!schema.getProto().getStruct().getIsGroup(),
             "Cannot form pointer to group type.");
  return DynamicStruct::Builder(schema,
      builder.initStruct(structSizeFromSchema(schema)));
}

DynamicList::Reader PointerHelpers<DynamicList, Kind::OTHER>::getDynamic(
    PointerReader reader, ListSchema schema) {
  return DynamicList::Reader(schema,
      reader.getList(elementSizeFor(schema.whichElementType()), nullptr));
}
DynamicList::Builder PointerHelpers<DynamicList, Kind::OTHER>::getDynamic(
    PointerBuilder builder, ListSchema schema) {
  if (schema.whichElementType() == schema::Type::STRUCT) {
    return DynamicList::Builder(schema,
        builder.getStructList(
            structSizeFromSchema(schema.getStructElementType()),
            nullptr));
  } else {
    return DynamicList::Builder(schema,
        builder.getList(elementSizeFor(schema.whichElementType()), nullptr));
  }
}
void PointerHelpers<DynamicList, Kind::OTHER>::set(
    PointerBuilder builder, const DynamicList::Reader& value) {
  builder.setList(value.reader);
}
DynamicList::Builder PointerHelpers<DynamicList, Kind::OTHER>::init(
    PointerBuilder builder, ListSchema schema, uint size) {
  if (schema.whichElementType() == schema::Type::STRUCT) {
    return DynamicList::Builder(schema,
        builder.initStructList(bounded(size) * ELEMENTS,
            structSizeFromSchema(schema.getStructElementType())));
  } else {
    return DynamicList::Builder(schema,
        builder.initList(elementSizeFor(schema.whichElementType()), bounded(size) * ELEMENTS));
  }
}

DynamicCapability::Client PointerHelpers<DynamicCapability, Kind::OTHER>::getDynamic(
    PointerReader reader, InterfaceSchema schema) {
  return DynamicCapability::Client(schema, reader.getCapability());
}
DynamicCapability::Client PointerHelpers<DynamicCapability, Kind::OTHER>::getDynamic(
    PointerBuilder builder, InterfaceSchema schema) {
  return DynamicCapability::Client(schema, builder.getCapability());
}
void PointerHelpers<DynamicCapability, Kind::OTHER>::set(
    PointerBuilder builder, DynamicCapability::Client& value) {
  builder.setCapability(value.hook->addRef());
}
void PointerHelpers<DynamicCapability, Kind::OTHER>::set(
    PointerBuilder builder, DynamicCapability::Client&& value) {
  builder.setCapability(kj::mv(value.hook));
}

}  // namespace _ (private)

template <>
void AnyPointer::Builder::adopt<DynamicValue>(Orphan<DynamicValue>&& orphan) {
  switch (orphan.getType()) {
    case DynamicValue::UNKNOWN:
    case DynamicValue::VOID:
    case DynamicValue::BOOL:
    case DynamicValue::INT:
    case DynamicValue::UINT:
    case DynamicValue::FLOAT:
    case DynamicValue::ENUM:
      KJ_FAIL_REQUIRE("AnyPointer cannot adopt primitive (non-object) value.");

    case DynamicValue::STRUCT:
    case DynamicValue::LIST:
    case DynamicValue::TEXT:
    case DynamicValue::DATA:
    case DynamicValue::CAPABILITY:
    case DynamicValue::ANY_POINTER:
      builder.adopt(kj::mv(orphan.builder));
      break;
  }
}

DynamicStruct::Reader::Reader(StructSchema schema, const _::OrphanBuilder& orphan)
    : schema(schema), reader(orphan.asStructReader(structSizeFromSchema(schema))) {}
DynamicStruct::Builder::Builder(StructSchema schema, _::OrphanBuilder& orphan)
    : schema(schema), builder(orphan.asStruct(structSizeFromSchema(schema))) {}

DynamicList::Reader::Reader(ListSchema schema, const _::OrphanBuilder& orphan)
    : schema(schema), reader(orphan.asListReader(elementSizeFor(schema.whichElementType()))) {}
DynamicList::Builder::Builder(ListSchema schema, _::OrphanBuilder& orphan)
    : schema(schema), builder(schema.whichElementType() == schema::Type::STRUCT
        ? orphan.asStructList(structSizeFromSchema(schema.getStructElementType()))
        : orphan.asList(elementSizeFor(schema.whichElementType()))) {}

// -------------------------------------------------------------------

Orphan<DynamicStruct> Orphanage::newOrphan(StructSchema schema) const {
  return Orphan<DynamicStruct>(
      schema, _::OrphanBuilder::initStruct(arena, capTable, structSizeFromSchema(schema)));
}

Orphan<DynamicList> Orphanage::newOrphan(ListSchema schema, uint size) const {
  if (schema.whichElementType() == schema::Type::STRUCT) {
    return Orphan<DynamicList>(schema, _::OrphanBuilder::initStructList(
        arena, capTable, bounded(size) * ELEMENTS,
        structSizeFromSchema(schema.getStructElementType())));
  } else {
    return Orphan<DynamicList>(schema, _::OrphanBuilder::initList(
        arena, capTable, bounded(size) * ELEMENTS,
        elementSizeFor(schema.whichElementType())));
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

DynamicCapability::Client Orphan<DynamicCapability>::get() {
  return DynamicCapability::Client(schema, builder.asCapability());
}

DynamicCapability::Client Orphan<DynamicCapability>::getReader() const {
  return DynamicCapability::Client(schema, builder.asCapability());
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
    case DynamicValue::CAPABILITY: interfaceSchema = value.capabilityValue.getSchema(); break;
    case DynamicValue::ANY_POINTER: break;
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
    case DynamicValue::CAPABILITY:
      return DynamicCapability::Client(interfaceSchema, builder.asCapability());
    case DynamicValue::ANY_POINTER:
      KJ_FAIL_REQUIRE("Can't get() an AnyPointer orphan; there is no underlying pointer to "
                      "wrap in an AnyPointer::Builder.");
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
    case DynamicValue::CAPABILITY:
      return DynamicCapability::Client(interfaceSchema, builder.asCapability());
    case DynamicValue::ANY_POINTER:
      KJ_FAIL_ASSERT("Can't get() an AnyPointer orphan; there is no underlying pointer to "
                     "wrap in an AnyPointer::Builder.");
  }
  KJ_UNREACHABLE;
}

template <>
Orphan<AnyPointer> Orphan<DynamicValue>::releaseAs<AnyPointer>() {
  KJ_REQUIRE(type == DynamicValue::ANY_POINTER, "Value type mismatch.");
  type = DynamicValue::UNKNOWN;
  return Orphan<AnyPointer>(kj::mv(builder));
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
Orphan<DynamicValue> Orphanage::newOrphanCopy<DynamicValue::Reader>(
    DynamicValue::Reader copyFrom) const {
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
    case DynamicValue::CAPABILITY: return newOrphanCopy(copyFrom.capabilityValue);
    case DynamicValue::ANY_POINTER: return newOrphanCopy(copyFrom.anyPointerValue);
  }
  KJ_UNREACHABLE;
}

}  // namespace capnp
