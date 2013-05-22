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

// This file defines classes that can be used to manipulate messages based on schemas that are not
// known until runtime.  This is also useful for writing generic code that uses schemas to handle
// arbitrary types in a generic way.
//
// Each of the classes defined here has a to() template method which converts an instance back to a
// native type.  This method will throw an exception if the requested type does not match the
// schema.  To convert native types to dynamic, use DynamicFactory.
//
// As always, underlying data is validated lazily, so you have to actually traverse the whole
// message if you want to validate all content.

#ifndef CAPNPROTO_DYNAMIC_H_
#define CAPNPROTO_DYNAMIC_H_

#include "schema.h"
#include "layout.h"
#include "message.h"

namespace capnproto {

class MessageReader;
class MessageBuilder;

struct DynamicValue {
  enum Type {
    UNKNOWN,
    // Means that the value has unknown type and content because it comes from a newer version of
    // the schema, or from a newer version of Cap'n Proto that has new features that this version
    // doesn't understand.

    VOID,
    BOOL,
    INT,
    UINT,
    FLOAT,
    TEXT,
    DATA,
    LIST,
    ENUM,
    STRUCT,
    UNION,
    INTERFACE,
    OBJECT
  };

  class Reader;
  class Builder;
};
class DynamicEnum;
class DynamicObject;
struct DynamicUnion {
  class Reader;
  class Builder;
};
struct DynamicStruct {
  class Reader;
  class Builder;
};
struct DynamicList {
  class Reader;
  class Builder;
};

template <Kind k> struct DynamicTypeFor_;
template <> struct DynamicTypeFor_<Kind::ENUM> { typedef DynamicEnum Type; };
template <> struct DynamicTypeFor_<Kind::STRUCT> { typedef DynamicStruct Type; };
template <> struct DynamicTypeFor_<Kind::LIST> { typedef DynamicList Type; };

template <typename T>
using DynamicTypeFor = typename DynamicTypeFor_<kind<T>()>::Type;

template <typename T>
ReaderFor<DynamicTypeFor<FromReader<T>>> toDynamic(T&& value);
template <typename T>
BuilderFor<DynamicTypeFor<FromBuilder<T>>> toDynamic(T&& value);
template <typename T>
DynamicTypeFor<TypeIfEnum<T>> toDynamic(T&& value);

// -------------------------------------------------------------------

class DynamicEnum {
public:
  DynamicEnum() = default;

  template <typename T, typename EnableIf = TypeIfEnum<T>>
  inline DynamicEnum(T&& value): DynamicEnum(toDynamic(value)) {}

  template <typename T>
  inline T as() { return static_cast<T>(asImpl(typeId<T>())); }
  // Cast to a native enum type.

  inline EnumSchema getSchema() { return schema; }

  Maybe<EnumSchema::Enumerant> getEnumerant();
  // Get which enumerant this enum value represents.  Returns nullptr if the numeric value does not
  // correspond to any enumerant in the schema -- this can happen if the data was built using a
  // newer schema that has more values defined.

  inline uint16_t getRaw() { return value; }
  // Returns the raw underlying enum value.

private:
  EnumSchema schema;
  uint16_t value;

  inline DynamicEnum(EnumSchema schema, uint16_t value)
      : schema(schema), value(value) {}

  uint16_t asImpl(uint64_t requestedTypeId);

  friend struct DynamicStruct;
  friend struct DynamicList;
  template <typename T>
  friend DynamicTypeFor<TypeIfEnum<T>> toDynamic(T&& value);
};

// -------------------------------------------------------------------

class DynamicObject {
  // Represents an "Object" field of unknown type.  This class behaves as a Reader.  There is no
  // equivalent Builder; you must use getObject() or initObject() on the containing struct and
  // specify a type if you want to build an Object field.

public:
  DynamicObject() = default;

  template <typename T>
  inline typename T::Reader as() { return AsImpl<T>::apply(*this); }
  // Convert the object to the given struct, list, or blob type.

  DynamicStruct::Reader as(StructSchema schema);
  DynamicList::Reader as(ListSchema schema);

private:
  internal::ObjectReader reader;

  inline DynamicObject(internal::ObjectReader reader): reader(reader) {}

  template <typename T, Kind kind = kind<T>()> struct AsImpl;
  // Implementation backing the as() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.

  friend struct DynamicStruct;
  friend struct DynamicList;
};

// -------------------------------------------------------------------

class DynamicUnion::Reader {
public:
  Reader() = default;

  inline StructSchema::Union getSchema() { return schema; }

  Maybe<StructSchema::Member> which();
  // Returns which field is set, or nullptr if an unknown field is set (i.e. the schema is old, and
  // the underlying data has the union set to a member we don't know about).

  DynamicValue::Reader get();
  // Get the value of whichever field of the union is set.  Throws an exception if which() returns
  // nullptr.

private:
  StructSchema::Union schema;
  internal::StructReader reader;

  inline Reader(StructSchema::Union schema, internal::StructReader reader)
      : schema(schema), reader(reader) {}

  friend struct DynamicStruct;
};

class DynamicUnion::Builder {
public:
  Builder() = default;

  inline StructSchema::Union getSchema() { return schema; }

  Maybe<StructSchema::Member> which();
  // Returns which field is set, or nullptr if an unknown field is set (i.e. the schema is old, and
  // the underlying data has the union set to a member we don't know about).

  DynamicValue::Builder get();
  void set(StructSchema::Member member, DynamicValue::Reader value);
  DynamicValue::Builder init(StructSchema::Member member);
  DynamicValue::Builder init(StructSchema::Member member, uint size);

  DynamicStruct::Builder getObject(StructSchema schema);
  DynamicList::Builder getObject(ListSchema schema);
  Text::Builder getObjectAsText();
  Data::Builder getObjectAsData();
  DynamicStruct::Builder initObject(StructSchema::Member member, StructSchema type);
  DynamicList::Builder initObject(StructSchema::Member member, ListSchema type, uint size);
  Text::Builder initObjectAsText(StructSchema::Member member, uint size);
  Data::Builder initObjectAsData(StructSchema::Member member, uint size);
  // Get/init an "Object" member.  Must specify the type.

  void set(Text::Reader name, DynamicValue::Reader value);
  DynamicValue::Builder init(Text::Reader member);
  DynamicValue::Builder init(Text::Reader name, uint size);
  DynamicStruct::Builder initObject(Text::Reader name, StructSchema type);
  DynamicList::Builder initObject(Text::Reader name, ListSchema type, uint size);
  Text::Builder initObjectAsText(Text::Reader name, uint size);
  Data::Builder initObjectAsData(Text::Reader name, uint size);
  // Convenience methods that identify the member by text name.

private:
  StructSchema::Union schema;
  internal::StructBuilder builder;

  inline Builder(StructSchema::Union schema, internal::StructBuilder builder)
      : schema(schema), builder(builder) {}

  StructSchema::Member checkIsObject();
  void setDiscriminant(StructSchema::Member member);
  void setObjectDiscriminant(StructSchema::Member member);

  friend struct DynamicStruct;
};

// -------------------------------------------------------------------

class DynamicStruct::Reader {
public:
  Reader() = default;

  template <typename T, typename EnableIf = FromReader<T>>
  inline Reader(T&& value): Reader(toDynamic(value)) {}

  template <typename T>
  typename T::Reader as();
  // Convert the dynamic struct to its compiled-in type.

  inline StructSchema getSchema() { return schema; }

  DynamicValue::Reader get(StructSchema::Member member);
  // Read the given member value.

  bool has(StructSchema::Member member);
  // Tests whether the given member is set to its default value.  For pointer values, this does
  // not actually traverse the value comparing it with the default, but simply returns true if the
  // pointer is non-null.

  DynamicValue::Reader get(Text::Reader name);
  bool has(Text::Reader name);
  // Shortcuts to access members by name.  These throw exceptions if no such member exists.

private:
  StructSchema schema;
  internal::StructReader reader;

  inline Reader(StructSchema schema, internal::StructReader reader)
      : schema(schema), reader(reader) {}

  static DynamicValue::Reader getImpl(internal::StructReader reader, StructSchema::Member member);

  template <typename T>
  friend struct internal::PointerHelpers;
  friend class DynamicUnion::Reader;
  friend class DynamicObject;
  friend class DynamicStruct::Builder;
  friend struct DynamicList;
  friend class MessageReader;
  friend class MessageBuilder;
  template <typename T, ::capnproto::Kind k>
  friend struct ::capnproto::ToDynamic_;
  friend String internal::debugString(StructReader reader, const RawSchema& schema);
};

class DynamicStruct::Builder {
public:
  Builder() = default;

  template <typename T, typename EnableIf = FromBuilder<T>>
  inline Builder(T&& value): Builder(toDynamic(value)) {}

  template <typename T>
  typename T::Builder as();
  // Cast to a particular struct type.

  inline StructSchema getSchema() { return schema; }

  DynamicValue::Builder get(StructSchema::Member member);
  // Read the given member value.

  bool has(StructSchema::Member member);
  // Tests whether the given member is set to its default value.  For pointer values, this does
  // not actually traverse the value comparing it with the default, but simply returns true if the
  // pointer is non-null.

  void set(StructSchema::Member member, DynamicValue::Reader value);
  // Set the given member value.

  DynamicValue::Builder init(StructSchema::Member member);
  DynamicValue::Builder init(StructSchema::Member member, uint size);
  // Init a struct, list, or blob field.

  DynamicStruct::Builder getObject(StructSchema::Member member, StructSchema type);
  DynamicList::Builder getObject(StructSchema::Member member, ListSchema type);
  Text::Builder getObjectAsText(StructSchema::Member member);
  Data::Builder getObjectAsData(StructSchema::Member member);
  // Get an object field.  You must specify the type.

  DynamicStruct::Builder initObject(StructSchema::Member member, StructSchema type);
  DynamicList::Builder initObject(StructSchema::Member member, ListSchema type, uint size);
  Text::Builder initObjectAsText(StructSchema::Member member, uint size);
  Data::Builder initObjectAsData(StructSchema::Member member, uint size);
  // Init an object field.  You must specify the type.

  DynamicValue::Builder get(Text::Reader name);
  bool has(Text::Reader name);
  void set(Text::Reader name, DynamicValue::Reader value);
  void set(Text::Reader name, std::initializer_list<DynamicValue::Reader> value);
  DynamicValue::Builder init(Text::Reader name);
  DynamicValue::Builder init(Text::Reader name, uint size);
  DynamicStruct::Builder getObject(Text::Reader name, StructSchema type);
  DynamicList::Builder getObject(Text::Reader name, ListSchema type);
  Text::Builder getObjectAsText(Text::Reader name);
  Data::Builder getObjectAsData(Text::Reader name);
  DynamicStruct::Builder initObject(Text::Reader name, StructSchema type);
  DynamicList::Builder initObject(Text::Reader name, ListSchema type, uint size);
  Text::Builder initObjectAsText(Text::Reader name, uint size);
  Data::Builder initObjectAsData(Text::Reader name, uint size);
  // Shortcuts to access members by name.  These throw exceptions if no such member exists.

  Reader asReader();

private:
  StructSchema schema;
  internal::StructBuilder builder;

  inline Builder(StructSchema schema, internal::StructBuilder builder)
      : schema(schema), builder(builder) {}

  static DynamicValue::Builder getImpl(
      internal::StructBuilder builder, StructSchema::Member member);
  static DynamicStruct::Builder getObjectImpl(
      internal::StructBuilder builder, StructSchema::Member field, StructSchema type);
  static DynamicList::Builder getObjectImpl(
      internal::StructBuilder builder, StructSchema::Member field, ListSchema type);
  static Text::Builder getObjectAsTextImpl(
      internal::StructBuilder builder, StructSchema::Member field);
  static Data::Builder getObjectAsDataImpl(
      internal::StructBuilder builder, StructSchema::Member field);

  static void setImpl(
      internal::StructBuilder builder, StructSchema::Member member, DynamicValue::Reader value);

  static DynamicValue::Builder initImpl(
      internal::StructBuilder builder, StructSchema::Member member, uint size);
  static DynamicValue::Builder initImpl(
      internal::StructBuilder builder, StructSchema::Member member);
  static DynamicStruct::Builder initFieldImpl(
      internal::StructBuilder builder, StructSchema::Member field, StructSchema type);
  static DynamicList::Builder initFieldImpl(
      internal::StructBuilder builder, StructSchema::Member field, ListSchema type, uint size);
  static Text::Builder initFieldAsTextImpl(
      internal::StructBuilder builder, StructSchema::Member field, uint size);
  static Data::Builder initFieldAsDataImpl(
      internal::StructBuilder builder, StructSchema::Member field, uint size);

  template <typename T>
  friend struct internal::PointerHelpers;
  friend class DynamicUnion::Builder;
  friend struct DynamicList;
  friend class MessageReader;
  friend class MessageBuilder;
  template <typename T, ::capnproto::Kind k>
  friend struct ::capnproto::ToDynamic_;
};

// -------------------------------------------------------------------

class DynamicList::Reader {
public:
  Reader() = default;

  template <typename T, typename EnableIf = FromReader<T>>
  inline Reader(T&& value): Reader(toDynamic(value)) {}

  template <typename T>
  typename T::Reader as();
  // Try to convert to any List<T>, Data, or Text.  Throws an exception if the underlying data
  // can't possibly represent the requested type.

  inline ListSchema getSchema() const { return schema; }

  inline uint size() const { return reader.size() / ELEMENTS; }
  DynamicValue::Reader operator[](uint index) const;

  typedef internal::IndexingIterator<Reader, DynamicValue::Reader> iterator;
  inline iterator begin() const { return iterator(this, 0); }
  inline iterator end() const { return iterator(this, size()); }

private:
  ListSchema schema;
  internal::ListReader reader;

  Reader(ListSchema schema, internal::ListReader reader): schema(schema), reader(reader) {}

  template <typename T>
  friend struct internal::PointerHelpers;
  friend struct DynamicStruct;
  friend class DynamicObject;
  friend class DynamicList::Builder;
  template <typename T, ::capnproto::Kind k>
  friend struct ::capnproto::ToDynamic_;
};

class DynamicList::Builder {
public:
  Builder() = default;

  template <typename T, typename EnableIf = FromBuilder<T>>
  inline Builder(T&& value): Builder(toDynamic(value)) {}

  template <typename T>
  typename T::Builder as();
  // Try to convert to any List<T>, Data, or Text.  Throws an exception if the underlying data
  // can't possibly represent the requested type.

  inline ListSchema getSchema() const { return schema; }

  inline uint size() const { return builder.size() / ELEMENTS; }
  DynamicValue::Builder operator[](uint index) const;
  void set(uint index, DynamicValue::Reader value);
  DynamicValue::Builder init(uint index, uint size);

  typedef internal::IndexingIterator<Builder, DynamicStruct::Builder> iterator;
  inline iterator begin() { return iterator(this, 0); }
  inline iterator end() { return iterator(this, size()); }

  void copyFrom(std::initializer_list<DynamicValue::Reader> value);

  Reader asReader();

private:
  ListSchema schema;
  internal::ListBuilder builder;

  Builder(ListSchema schema, internal::ListBuilder builder): schema(schema), builder(builder) {}

  template <typename T>
  friend struct internal::PointerHelpers;
  friend struct DynamicStruct;
  template <typename T, ::capnproto::Kind k>
  friend struct ::capnproto::ToDynamic_;
};

// -------------------------------------------------------------------

namespace internal {

// Make sure ReaderFor<T> and BuilderFor<T> work for DynamicEnum, DynamicObject, DynamicStruct, and
// DynamicList, so that we can define DynamicValue::as().

template <>
struct MaybeReaderBuilder<DynamicEnum, Kind::UNKNOWN> {
  typedef DynamicEnum Reader;
  typedef DynamicEnum Builder;
};

template <>
struct MaybeReaderBuilder<DynamicObject, Kind::UNKNOWN> {
  typedef DynamicObject Reader;
  typedef DynamicObject Builder;
};

template <>
struct MaybeReaderBuilder<DynamicStruct, Kind::UNKNOWN> {
  typedef DynamicStruct::Reader Reader;
  typedef DynamicStruct::Builder Builder;
};

template <>
struct MaybeReaderBuilder<DynamicList, Kind::UNKNOWN> {
  typedef DynamicList::Reader Reader;
  typedef DynamicList::Builder Builder;
};

}  // namespace internal

class DynamicValue::Reader {
public:
  inline Reader(std::nullptr_t n = nullptr);  // UNKNOWN
  inline Reader(Void value);
  inline Reader(bool value);
  inline Reader(char value);
  inline Reader(signed char value);
  inline Reader(short value);
  inline Reader(int value);
  inline Reader(long value);
  inline Reader(long long value);
  inline Reader(unsigned char value);
  inline Reader(unsigned short value);
  inline Reader(unsigned int value);
  inline Reader(unsigned long value);
  inline Reader(unsigned long long value);
  inline Reader(float value);
  inline Reader(double value);
  inline Reader(const char* value);  // Text
  inline Reader(Text::Reader value);
  inline Reader(Data::Reader value);
  inline Reader(DynamicList::Reader value);
  inline Reader(DynamicEnum value);
  inline Reader(DynamicStruct::Reader value);
  inline Reader(DynamicUnion::Reader value);
  inline Reader(DynamicObject value);

  template <typename T, typename EnableIf = decltype(toDynamic(instance<T>()))>
  inline Reader(T value): Reader(toDynamic(value)) {}

  template <typename T>
  inline ReaderFor<T> as() { return AsImpl<T>::apply(*this); }
  // Use to interpret the value as some Cap'n Proto type.  Allowed types are:
  // - Void, bool, [u]int{8,16,32,64}_t, float, double, any enum:  Returns the raw value.
  // - Text, Data, any struct type:  Returns the corresponding Reader.
  // - List<T> for any T listed above:  Returns List<T>::Reader.
  // - DynamicEnum, DynamicObject:  Returns the corresponding type.
  // - DynamicStruct, DynamicList, DynamicUnion:  Returns the corresponding Reader.
  //
  // DynamicValue allows various implicit conversions, mostly just to make the interface friendlier.
  // - Any integer can be converted to any other integer type so long as the actual value is within
  //   the new type's range.
  // - Floating-point types can be converted to integers as long as no information would be lost
  //   in the conversion.
  // - Integers can be converted to floating points.  This may lose information, but won't throw.
  // - Float32/Float64 can be converted between each other.  Converting Float64 -> Float32 may lose
  //   information, but won't throw.
  // - Text can be converted to Data (but not vice-versa).
  // - Text can be converted to an enum, if the Text matches one of the enumerant names (but not
  //   vice-versa).
  //
  // Any other conversion attempt will throw an exception.

  inline Type getType() { return type; }
  // Get the type of this value.

private:
  Type type;

  union {
    Void voidValue;
    bool boolValue;
    int64_t intValue;
    uint64_t uintValue;
    double floatValue;
    Text::Reader textValue;
    Data::Reader dataValue;
    DynamicList::Reader listValue;
    DynamicEnum enumValue;
    DynamicStruct::Reader structValue;
    DynamicUnion::Reader unionValue;
    DynamicObject objectValue;
  };

  template <typename T, Kind kind = kind<T>()> struct AsImpl;
  // Implementation backing the as() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.
};

class DynamicValue::Builder {
public:
  inline Builder(std::nullptr_t n = nullptr);  // UNKNOWN
  inline Builder(Void value);
  inline Builder(bool value);
  inline Builder(char value);
  inline Builder(signed char value);
  inline Builder(short value);
  inline Builder(int value);
  inline Builder(long value);
  inline Builder(long long value);
  inline Builder(unsigned char value);
  inline Builder(unsigned short value);
  inline Builder(unsigned int value);
  inline Builder(unsigned long value);
  inline Builder(unsigned long long value);
  inline Builder(float value);
  inline Builder(double value);
  inline Builder(Text::Builder value);
  inline Builder(Data::Builder value);
  inline Builder(DynamicList::Builder value);
  inline Builder(DynamicEnum value);
  inline Builder(DynamicStruct::Builder value);
  inline Builder(DynamicUnion::Builder value);
  inline Builder(DynamicObject value);

  template <typename T, typename EnableIf = decltype(toDynamic(instance<T>()))>
  inline Builder(T value): Builder(toDynamic(value)) {}

  template <typename T>
  inline BuilderFor<T> as() { return AsImpl<T>::apply(*this); }
  // See DynamicValue::Reader::as().

  inline Type getType() { return type; }
  // Get the type of this value.

  Reader asReader();

private:
  Type type;

  union {
    Void voidValue;
    bool boolValue;
    int64_t intValue;
    uint64_t uintValue;
    double floatValue;
    Text::Builder textValue;
    Data::Builder dataValue;
    DynamicList::Builder listValue;
    DynamicEnum enumValue;
    DynamicStruct::Builder structValue;
    DynamicUnion::Builder unionValue;
    DynamicObject objectValue;
  };

  template <typename T, Kind kind = kind<T>()> struct AsImpl;
  // Implementation backing the as() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.
};

// -------------------------------------------------------------------
// Inject the ability to use DynamicStruct for message roots and Dynamic{Struct,List} for
// generated Object accessors.

template <>
DynamicStruct::Reader MessageReader::getRoot<DynamicStruct>(StructSchema schema);
template <>
DynamicStruct::Builder MessageBuilder::initRoot<DynamicStruct>(StructSchema schema);
template <>
DynamicStruct::Builder MessageBuilder::getRoot<DynamicStruct>(StructSchema schema);

namespace internal {

template <>
struct PointerHelpers<DynamicStruct, Kind::UNKNOWN> {
  // getDynamic() is used when an Object's get() accessor is passed arguments, because for
  // non-dynamic types PointerHelpers::get() takes a default value as the third argument, and we
  // don't want people to accidentally be able to provide their own default value.
  static DynamicStruct::Reader getDynamic(
      StructReader reader, WirePointerCount index, StructSchema schema);
  static DynamicStruct::Builder getDynamic(
      StructBuilder builder, WirePointerCount index, StructSchema schema);
  static void set(
      StructBuilder builder, WirePointerCount index, DynamicStruct::Reader value);
  static DynamicStruct::Builder init(
      StructBuilder builder, WirePointerCount index, StructSchema schema);
};

template <>
struct PointerHelpers<DynamicList, Kind::UNKNOWN> {
  // getDynamic() is used when an Object's get() accessor is passed arguments, because for
  // non-dynamic types PointerHelpers::get() takes a default value as the third argument, and we
  // don't want people to accidentally be able to provide their own default value.
  static DynamicList::Reader getDynamic(
      StructReader reader, WirePointerCount index, ListSchema schema);
  static DynamicList::Builder getDynamic(
      StructBuilder builder, WirePointerCount index, ListSchema schema);
  static void set(
      StructBuilder builder, WirePointerCount index, DynamicList::Reader value);
  static DynamicList::Builder init(
      StructBuilder builder, WirePointerCount index, ListSchema schema, uint size);
};

}  // namespace internal

// =======================================================================================
// Inline implementation details.

template <typename T>
struct ToDynamic_<T, Kind::STRUCT> {
  static inline DynamicStruct::Reader apply(typename T::Reader value) {
    return DynamicStruct::Reader(Schema::from<T>(), value._reader);
  }
  static inline DynamicStruct::Builder apply(typename T::Builder value) {
    return DynamicStruct::Builder(Schema::from<T>(), value._builder);
  }
};

template <typename T>
struct ToDynamic_<T, Kind::LIST> {
  static inline DynamicList::Reader apply(typename T::Reader value) {
    return DynamicList::Reader(Schema::from<T>(), value.reader);
  }
  static inline DynamicList::Builder apply(typename T::Builder value) {
    return DynamicList::Builder(Schema::from<T>(), value.builder);
  }
};

template <typename T>
ReaderFor<DynamicTypeFor<FromReader<T>>> toDynamic(T&& value) {
  return ToDynamic_<FromReader<T>>::apply(value);
}
template <typename T>
BuilderFor<DynamicTypeFor<FromBuilder<T>>> toDynamic(T&& value) {
  return ToDynamic_<FromBuilder<T>>::apply(value);
}
template <typename T>
DynamicTypeFor<TypeIfEnum<T>> toDynamic(T&& value) {
  return DynamicEnum(Schema::from<RemoveReference<T>>(), static_cast<uint16_t>(value));
}

inline DynamicValue::Reader::Reader(std::nullptr_t n): type(UNKNOWN) {}
inline DynamicValue::Builder::Builder(std::nullptr_t n): type(UNKNOWN) {}

    #define CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(cppType, typeTag, fieldName) \
inline DynamicValue::Reader::Reader(cppType value) \
    : type(typeTag), fieldName##Value(value) {} \
inline DynamicValue::Builder::Builder(cppType value) \
    : type(typeTag), fieldName##Value(value) {}

CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(Void, VOID, void);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(bool, BOOL, bool);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(char, INT, int);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(signed char, INT, int);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(short, INT, int);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(int, INT, int);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(long, INT, int);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(long long, INT, int);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(unsigned char, UINT, uint);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(unsigned short, UINT, uint);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(unsigned int, UINT, uint);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(unsigned long, UINT, uint);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(unsigned long long, UINT, uint);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(float, FLOAT, float);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(double, FLOAT, float);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(DynamicEnum, ENUM, enum);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(DynamicObject, OBJECT, object);
#undef CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR

#define CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(cppType, typeTag, fieldName) \
inline DynamicValue::Reader::Reader(cppType::Reader value) \
    : type(typeTag), fieldName##Value(value) {} \
inline DynamicValue::Builder::Builder(cppType::Builder value) \
    : type(typeTag), fieldName##Value(value) {}

CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(Text, TEXT, text);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(Data, DATA, data);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(DynamicList, LIST, list);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(DynamicStruct, STRUCT, struct);
CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(DynamicUnion, UNION, union);

#undef CAPNPROTO_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR

inline DynamicValue::Reader::Reader(const char* value): Reader(Text::Reader(value)) {}

#define CAPNPROTO_DECLARE_TYPE(name, discrim, typeName) \
template <> \
struct DynamicValue::Reader::AsImpl<typeName> { \
  static ReaderFor<typeName> apply(Reader reader); \
}; \
template <> \
struct DynamicValue::Builder::AsImpl<typeName> { \
  static BuilderFor<typeName> apply(Builder builder); \
};

//CAPNPROTO_DECLARE_TYPE(void, VOID, Void)
CAPNPROTO_DECLARE_TYPE(bool, BOOL, bool)
CAPNPROTO_DECLARE_TYPE(int8, INT8, int8_t)
CAPNPROTO_DECLARE_TYPE(int16, INT16, int16_t)
CAPNPROTO_DECLARE_TYPE(int32, INT32, int32_t)
CAPNPROTO_DECLARE_TYPE(int64, INT64, int64_t)
CAPNPROTO_DECLARE_TYPE(uint8, UINT8, uint8_t)
CAPNPROTO_DECLARE_TYPE(uint16, UINT16, uint16_t)
CAPNPROTO_DECLARE_TYPE(uint32, UINT32, uint32_t)
CAPNPROTO_DECLARE_TYPE(uint64, UINT64, uint64_t)
CAPNPROTO_DECLARE_TYPE(float32, FLOAT32, float)
CAPNPROTO_DECLARE_TYPE(float64, FLOAT64, double)

CAPNPROTO_DECLARE_TYPE(text, TEXT, Text)
CAPNPROTO_DECLARE_TYPE(data, DATA, Data)
CAPNPROTO_DECLARE_TYPE(list, LIST, DynamicList)
CAPNPROTO_DECLARE_TYPE(struct, STRUCT, DynamicStruct)
CAPNPROTO_DECLARE_TYPE(enum, ENUM, DynamicEnum)
CAPNPROTO_DECLARE_TYPE(object, OBJECT, DynamicObject)
CAPNPROTO_DECLARE_TYPE(union, UNION, DynamicUnion)
#undef CAPNPROTO_DECLARE_TYPE

// CAPNPROTO_DECLARE_TYPE(Void) causes gcc 4.7 to segfault.  If I do it manually and remove the
// ReaderFor<> and BuilderFor<> wrappers, it works.
template <>
struct DynamicValue::Reader::AsImpl<Void> {
  static Void apply(Reader reader);
};
template <>
struct DynamicValue::Builder::AsImpl<Void> {
  static Void apply(Builder builder);
};

template <typename T>
struct DynamicValue::Reader::AsImpl<T, Kind::ENUM> {
  static T apply(Reader reader) {
    return reader.as<DynamicEnum>().as<T>();
  }
};
template <typename T>
struct DynamicValue::Builder::AsImpl<T, Kind::ENUM> {
  static T apply(Builder builder) {
    return builder.as<DynamicEnum>().as<T>();
  }
};

template <typename T>
struct DynamicValue::Reader::AsImpl<T, Kind::STRUCT> {
  static typename T::Reader apply(Reader reader) {
    return reader.as<DynamicStruct>().as<T>();
  }
};
template <typename T>
struct DynamicValue::Builder::AsImpl<T, Kind::STRUCT> {
  static typename T::Builder apply(Builder builder) {
    return builder.as<DynamicStruct>().as<T>();
  }
};

template <typename T>
struct DynamicValue::Reader::AsImpl<T, Kind::LIST> {
  static typename T::Reader apply(Reader reader) {
    return reader.as<DynamicList>().as<T>();
  }
};
template <typename T>
struct DynamicValue::Builder::AsImpl<T, Kind::LIST> {
  static typename T::Builder apply(Builder builder) {
    return builder.as<DynamicList>().as<T>();
  }
};

// -------------------------------------------------------------------

template <typename T>
struct DynamicObject::AsImpl<T, Kind::STRUCT> {
  static T apply(DynamicObject value) {
    return value.as(Schema::from<T>()).as<T>();
  }
};

template <typename T>
struct DynamicObject::AsImpl<T, Kind::LIST> {
  static T apply(DynamicObject value) {
    return value.as(Schema::from<T>()).as<T>();
  }
};

// -------------------------------------------------------------------

template <typename T>
typename T::Reader DynamicStruct::Reader::as() {
  static_assert(kind<T>() == Kind::STRUCT,
                "DynamicStruct::Reader::as<T>() can only convert to struct types.");
  schema.requireUsableAs<T>();
  return typename T::Reader(reader);
}
template <typename T>
typename T::Builder DynamicStruct::Builder::as() {
  static_assert(kind<T>() == Kind::STRUCT,
                "DynamicStruct::Builder::as<T>() can only convert to struct types.");
  schema.requireUsableAs<T>();
  return typename T::Builder(builder);
}

template <>
inline DynamicStruct::Reader DynamicStruct::Reader::as<DynamicStruct>() {
  return *this;
}
template <>
inline DynamicStruct::Builder DynamicStruct::Builder::as<DynamicStruct>() {
  return *this;
}

inline DynamicStruct::Reader DynamicStruct::Builder::asReader() {
  return DynamicStruct::Reader(schema, builder.asReader());
}

// -------------------------------------------------------------------

template <typename T>
typename T::Reader DynamicList::Reader::as() {
  static_assert(kind<T>() == Kind::LIST,
                "DynamicStruct::Reader::as<T>() can only convert to list types.");
  schema.requireUsableAs<T>();
  return typename T::Reader(reader);
}
template <typename T>
typename T::Builder DynamicList::Builder::as() {
  static_assert(kind<T>() == Kind::LIST,
                "DynamicStruct::Builder::as<T>() can only convert to list types.");
  schema.requireUsableAs<T>();
  return typename T::Builder(builder);
}

template <>
inline DynamicList::Reader DynamicList::Reader::as<DynamicList>() {
  return *this;
}
template <>
inline DynamicList::Builder DynamicList::Builder::as<DynamicList>() {
  return *this;
}

}  // namespace capnproto

#endif  // CAPNPROTO_DYNAMIC_H_
