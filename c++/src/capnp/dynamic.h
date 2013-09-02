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

#ifndef CAPNP_DYNAMIC_H_
#define CAPNP_DYNAMIC_H_

#include "schema.h"
#include "layout.h"
#include "message.h"

namespace capnp {

class MessageReader;
class MessageBuilder;

struct DynamicValue {
  DynamicValue() = delete;

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
    INTERFACE,
    OBJECT
  };

  class Reader;
  class Builder;
};
class DynamicEnum;
struct DynamicObject {
  DynamicObject() = delete;
  class Reader;
  class Builder;
};
struct DynamicStruct {
  DynamicStruct() = delete;
  class Reader;
  class Builder;
};
struct DynamicList {
  DynamicList() = delete;
  class Reader;
  class Builder;
};
template <> class Orphan<DynamicValue>;

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
  inline DynamicEnum(EnumSchema::Enumerant enumerant)
      : schema(enumerant.getContainingEnum()), value(enumerant.getOrdinal()) {}

  template <typename T, typename = kj::EnableIf<kind<T>() == Kind::ENUM>>
  inline DynamicEnum(T&& value): DynamicEnum(toDynamic(value)) {}

  template <typename T>
  inline T as() const { return static_cast<T>(asImpl(typeId<T>())); }
  // Cast to a native enum type.

  inline EnumSchema getSchema() const { return schema; }

  kj::Maybe<EnumSchema::Enumerant> getEnumerant() const;
  // Get which enumerant this enum value represents.  Returns nullptr if the numeric value does not
  // correspond to any enumerant in the schema -- this can happen if the data was built using a
  // newer schema that has more values defined.

  inline uint16_t getRaw() const { return value; }
  // Returns the raw underlying enum value.

private:
  EnumSchema schema;
  uint16_t value;

  inline DynamicEnum(EnumSchema schema, uint16_t value)
      : schema(schema), value(value) {}

  uint16_t asImpl(uint64_t requestedTypeId) const;

  friend struct DynamicStruct;
  friend struct DynamicList;
  friend struct DynamicValue;
  template <typename T>
  friend DynamicTypeFor<TypeIfEnum<T>> toDynamic(T&& value);
};

// -------------------------------------------------------------------

class DynamicObject::Reader {
  // Represents an "Object" field of unknown type.

public:
  typedef DynamicObject Reads;

  Reader() = default;

  template <typename T>
  typename T::Reader as() const;
  // Convert the object to the given struct, list, or blob type.

  DynamicStruct::Reader as(StructSchema schema) const;
  DynamicList::Reader as(ListSchema schema) const;

private:
  _::ObjectReader reader;

  inline Reader(_::ObjectReader reader): reader(reader) {}

  friend struct DynamicStruct;
  friend struct DynamicList;
  template <typename T, Kind K>
  friend struct _::PointerHelpers;
  friend class DynamicObject::Builder;
  friend class Orphan<DynamicObject>;
  friend class Orphan<DynamicValue>;
  friend class Orphanage;
};

class DynamicObject::Builder: public kj::DisallowConstCopy {
  // Represents an "Object" field of unknown type.
  //
  // You can't actually do anything with a DynamicObject::Builder except read it.  It can't be
  // converted to a Builder for any specific type because that could require initializing or
  // updating the pointer that points *to* this object.  Therefore, you must call
  // DynamicStruct::Builder::{get,set,init}Object() and pass a type schema to build object fields.

public:
  typedef DynamicObject Builds;

  Builder() = default;
  inline Builder(decltype(nullptr)) {}
  Builder(Builder&) = default;
  Builder(Builder&&) = default;

  Reader asReader() const { return Reader(builder.asReader()); }

private:
  _::ObjectBuilder builder;

  inline Builder(_::ObjectBuilder builder): builder(builder) {}
  friend struct DynamicStruct;
  friend struct DynamicList;
  template <typename T, Kind K>
  friend struct _::PointerHelpers;
  friend class Orphan<DynamicObject>;
  friend class Orphan<DynamicValue>;
};

// -------------------------------------------------------------------

class DynamicStruct::Reader {
public:
  typedef DynamicStruct Reads;

  Reader() = default;

  template <typename T, typename = kj::EnableIf<kind<FromReader<T>>() == Kind::STRUCT>>
  inline Reader(T&& value): Reader(toDynamic(value)) {}

  inline size_t totalSizeInWords() const { return reader.totalSize() / ::capnp::WORDS; }

  template <typename T>
  typename T::Reader as() const;
  // Convert the dynamic struct to its compiled-in type.

  inline StructSchema getSchema() const { return schema; }

  DynamicValue::Reader get(StructSchema::Field field) const;
  // Read the given field value.

  bool has(StructSchema::Field field) const;
  // Tests whether the given field is set to its default value.  For pointer values, this does
  // not actually traverse the value comparing it with the default, but simply returns true if the
  // pointer is non-null.  For members of unions, has() returns false if the union member is not
  // active, but does not necessarily return true if the member is active (depends on the field's
  // value).

  kj::Maybe<StructSchema::Field> which() const;
  // If the struct contains an (unnamed) union, and the currently-active field within that union
  // is known, this returns that field.  Otherwise, it returns null.  In other words, this returns
  // null if there is no union present _or_ if the union's discriminant is set to an unrecognized
  // value.  This could happen in particular when receiving a message from a sender who has a
  // newer version of the protocol and is using a field of the union that you don't know about yet.

  DynamicValue::Reader get(kj::StringPtr name) const;
  bool has(kj::StringPtr name) const;
  // Shortcuts to access fields by name.  These throw exceptions if no such field exists.

private:
  StructSchema schema;
  _::StructReader reader;

  inline Reader(StructSchema schema, _::StructReader reader)
      : schema(schema), reader(reader) {}

  bool isSetInUnion(StructSchema::Field field) const;
  void verifySetInUnion(StructSchema::Field field) const;
  static DynamicValue::Reader getImpl(_::StructReader reader, StructSchema::Field field);

  template <typename T, Kind K>
  friend struct _::PointerHelpers;
  friend struct DynamicObject;
  friend class DynamicStruct::Builder;
  friend struct DynamicList;
  friend class MessageReader;
  friend class MessageBuilder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend kj::StringTree _::structString(
      _::StructReader reader, const _::RawSchema& schema);
  friend class Orphanage;
  friend class Orphan<DynamicStruct>;
  friend class Orphan<DynamicValue>;
};

class DynamicStruct::Builder {
public:
  typedef DynamicStruct Builds;

  Builder() = default;
  inline Builder(decltype(nullptr)) {}

  template <typename T, typename = kj::EnableIf<kind<FromBuilder<T>>() == Kind::STRUCT>>
  inline Builder(T&& value): Builder(toDynamic(value)) {}

  inline size_t totalSizeInWords() const { return asReader().totalSizeInWords(); }

  template <typename T>
  typename T::Builder as();
  // Cast to a particular struct type.

  inline StructSchema getSchema() const { return schema; }

  DynamicValue::Builder get(StructSchema::Field field);
  // Read the given field value.

  inline bool has(StructSchema::Field field) { return asReader().has(field); }
  // Tests whether the given field is set to its default value.  For pointer values, this does
  // not actually traverse the value comparing it with the default, but simply returns true if the
  // pointer is non-null.  For members of unions, has() returns whether the field is currently
  // active and the union as a whole is non-default -- so, the only time has() will return false
  // for an active union field is if it is the default active field and it has its default value.

  kj::Maybe<StructSchema::Field> which();
  // If the struct contains an (unnamed) union, and the currently-active field within that union
  // is known, this returns that field.  Otherwise, it returns null.  In other words, this returns
  // null if there is no union present _or_ if the union's discriminant is set to an unrecognized
  // value.  This could happen in particular when receiving a message from a sender who has a
  // newer version of the protocol and is using a field of the union that you don't know about yet.

  void set(StructSchema::Field field, const DynamicValue::Reader& value);
  // Set the given field value.

  DynamicValue::Builder init(StructSchema::Field field);
  DynamicValue::Builder init(StructSchema::Field field, uint size);
  // Init a struct, list, or blob field.

  void adopt(StructSchema::Field field, Orphan<DynamicValue>&& orphan);
  Orphan<DynamicValue> disown(StructSchema::Field field);
  // Adopt/disown.  This works even for non-pointer fields: adopt() becomes equivalent to set()
  // and disown() becomes like get() followed by clear().

  void clear(StructSchema::Field field);
  // Clear a field, setting it to its default value.  For pointer fields, this actually makes the
  // field null.

  DynamicStruct::Builder getObject(StructSchema::Field field, StructSchema type);
  DynamicList::Builder getObject(StructSchema::Field field, ListSchema type);
  Text::Builder getObjectAsText(StructSchema::Field field);
  Data::Builder getObjectAsData(StructSchema::Field field);
  // Get an object field.  You must specify the type.

  DynamicStruct::Builder initObject(StructSchema::Field field, StructSchema type);
  DynamicList::Builder initObject(StructSchema::Field field, ListSchema type, uint size);
  Text::Builder initObjectAsText(StructSchema::Field field, uint size);
  Data::Builder initObjectAsData(StructSchema::Field field, uint size);
  // Init an object field.  You must specify the type.

  DynamicValue::Builder get(kj::StringPtr name);
  bool has(kj::StringPtr name);
  void set(kj::StringPtr name, const DynamicValue::Reader& value);
  void set(kj::StringPtr name, std::initializer_list<DynamicValue::Reader> value);
  DynamicValue::Builder init(kj::StringPtr name);
  DynamicValue::Builder init(kj::StringPtr name, uint size);
  void adopt(kj::StringPtr name, Orphan<DynamicValue>&& orphan);
  Orphan<DynamicValue> disown(kj::StringPtr name);
  void clear(kj::StringPtr name);
  DynamicStruct::Builder getObject(kj::StringPtr name, StructSchema type);
  DynamicList::Builder getObject(kj::StringPtr name, ListSchema type);
  Text::Builder getObjectAsText(kj::StringPtr name);
  Data::Builder getObjectAsData(kj::StringPtr name);
  DynamicStruct::Builder initObject(kj::StringPtr name, StructSchema type);
  DynamicList::Builder initObject(kj::StringPtr name, ListSchema type, uint size);
  Text::Builder initObjectAsText(kj::StringPtr name, uint size);
  Data::Builder initObjectAsData(kj::StringPtr name, uint size);
  // Shortcuts to access fields by name.  These throw exceptions if no such field exists.

  Reader asReader() const;

private:
  StructSchema schema;
  _::StructBuilder builder;

  inline Builder(StructSchema schema, _::StructBuilder builder)
      : schema(schema), builder(builder) {}

  bool isSetInUnion(StructSchema::Field field);
  void verifySetInUnion(StructSchema::Field field);
  void setInUnion(StructSchema::Field field);

  WirePointerCount verifyIsObject(StructSchema::Field field);

  template <typename T, Kind k>
  friend struct _::PointerHelpers;
  friend struct DynamicList;
  friend class MessageReader;
  friend class MessageBuilder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class Orphanage;
  friend class Orphan<DynamicStruct>;
  friend class Orphan<DynamicObject>;
  friend class Orphan<DynamicValue>;
};

// -------------------------------------------------------------------

class DynamicList::Reader {
public:
  typedef DynamicList Reads;

  Reader() = default;

  template <typename T, typename = kj::EnableIf<kind<FromReader<T>>() == Kind::LIST>>
  inline Reader(T&& value): Reader(toDynamic(value)) {}

  template <typename T>
  typename T::Reader as() const;
  // Try to convert to any List<T>, Data, or Text.  Throws an exception if the underlying data
  // can't possibly represent the requested type.

  inline ListSchema getSchema() const { return schema; }

  inline uint size() const { return reader.size() / ELEMENTS; }
  DynamicValue::Reader operator[](uint index) const;

  typedef _::IndexingIterator<const Reader, DynamicValue::Reader> Iterator;
  inline Iterator begin() const { return Iterator(this, 0); }
  inline Iterator end() const { return Iterator(this, size()); }

private:
  ListSchema schema;
  _::ListReader reader;

  Reader(ListSchema schema, _::ListReader reader): schema(schema), reader(reader) {}

  template <typename T, Kind k>
  friend struct _::PointerHelpers;
  friend struct DynamicStruct;
  friend struct DynamicObject;
  friend class DynamicList::Builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class Orphanage;
  friend class Orphan<DynamicList>;
  friend class Orphan<DynamicValue>;
};

class DynamicList::Builder {
public:
  typedef DynamicList Builds;

  Builder() = default;
  inline Builder(decltype(nullptr)) {}

  template <typename T, typename = kj::EnableIf<kind<FromBuilder<T>>() == Kind::LIST>>
  inline Builder(T&& value): Builder(toDynamic(value)) {}

  template <typename T>
  typename T::Builder as();
  // Try to convert to any List<T>, Data, or Text.  Throws an exception if the underlying data
  // can't possibly represent the requested type.

  inline ListSchema getSchema() const { return schema; }

  inline uint size() const { return builder.size() / ELEMENTS; }
  DynamicValue::Builder operator[](uint index);
  void set(uint index, const DynamicValue::Reader& value);
  DynamicValue::Builder init(uint index, uint size);
  void adopt(uint index, Orphan<DynamicValue>&& orphan);
  Orphan<DynamicValue> disown(uint index);

  typedef _::IndexingIterator<Builder, DynamicStruct::Builder> Iterator;
  inline Iterator begin() { return Iterator(this, 0); }
  inline Iterator end() { return Iterator(this, size()); }

  void copyFrom(std::initializer_list<DynamicValue::Reader> value);

  Reader asReader() const;

private:
  ListSchema schema;
  _::ListBuilder builder;

  Builder(ListSchema schema, _::ListBuilder builder): schema(schema), builder(builder) {}

  template <typename T, Kind k>
  friend struct _::PointerHelpers;
  friend struct DynamicStruct;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class Orphanage;
  template <typename T, Kind k>
  friend struct _::OrphanGetImpl;
  friend class Orphan<DynamicList>;
  friend class Orphan<DynamicObject>;
  friend class Orphan<DynamicValue>;
};

// -------------------------------------------------------------------

// Make sure ReaderFor<T> and BuilderFor<T> work for DynamicEnum, DynamicObject, DynamicStruct, and
// DynamicList, so that we can define DynamicValue::as().

template <> struct ReaderFor_ <DynamicEnum, Kind::UNKNOWN> { typedef DynamicEnum Type; };
template <> struct BuilderFor_<DynamicEnum, Kind::UNKNOWN> { typedef DynamicEnum Type; };
template <> struct ReaderFor_ <DynamicObject, Kind::UNKNOWN> { typedef DynamicObject::Reader Type; };
template <> struct BuilderFor_<DynamicObject, Kind::UNKNOWN> { typedef DynamicObject::Builder Type; };
template <> struct ReaderFor_ <DynamicStruct, Kind::UNKNOWN> { typedef DynamicStruct::Reader Type; };
template <> struct BuilderFor_<DynamicStruct, Kind::UNKNOWN> { typedef DynamicStruct::Builder Type; };
template <> struct ReaderFor_ <DynamicList, Kind::UNKNOWN> { typedef DynamicList::Reader Type; };
template <> struct BuilderFor_<DynamicList, Kind::UNKNOWN> { typedef DynamicList::Builder Type; };

class DynamicValue::Reader {
public:
  typedef DynamicValue Reads;

  inline Reader(decltype(nullptr) n = nullptr);  // UNKNOWN
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
  inline Reader(const Text::Reader& value);
  inline Reader(const Data::Reader& value);
  inline Reader(const DynamicList::Reader& value);
  inline Reader(DynamicEnum value);
  inline Reader(const DynamicStruct::Reader& value);
  inline Reader(const DynamicObject::Reader& value);
  Reader(ConstSchema constant);

  template <typename T, typename = decltype(toDynamic(kj::instance<T>()))>
  inline Reader(T value): Reader(toDynamic(value)) {}

  template <typename T>
  inline ReaderFor<T> as() const { return AsImpl<T>::apply(*this); }
  // Use to interpret the value as some Cap'n Proto type.  Allowed types are:
  // - Void, bool, [u]int{8,16,32,64}_t, float, double, any enum:  Returns the raw value.
  // - Text, Data, any struct type:  Returns the corresponding Reader.
  // - List<T> for any T listed above:  Returns List<T>::Reader.
  // - DynamicEnum, DynamicObject:  Returns the corresponding type.
  // - DynamicStruct, DynamicList:  Returns the corresponding Reader.
  //
  // DynamicValue allows various implicit conversions, mostly just to make the interface friendlier.
  // - Any integer can be converted to any other integer type so long as the actual value is within
  //   the new type's range.
  // - Floating-point types can be converted to integers as long as no information would be lost
  //   in the conversion.
  // - Integers can be converted to floating points.  This may lose information, but won't throw.
  // - Float32/Float64 can be converted between each other.  Converting Float64 -> Float32 may lose
  //   information, but won't throw.
  // - Text can be converted to an enum, if the Text matches one of the enumerant names (but not
  //   vice-versa).
  //
  // Any other conversion attempt will throw an exception.

  inline Type getType() const { return type; }
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
    DynamicObject::Reader objectValue;
  };

  template <typename T, Kind kind = kind<T>()> struct AsImpl;
  // Implementation backing the as() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.

  friend class Orphanage;  // to speed up newOrphanCopy(DynamicValue::Reader)
};

class DynamicValue::Builder {
public:
  typedef DynamicValue Builds;

  inline Builder(decltype(nullptr) n = nullptr);  // UNKNOWN
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
  inline Builder(DynamicObject::Builder value);

  template <typename T, typename = decltype(toDynamic(kj::instance<T>()))>
  inline Builder(T value): Builder(toDynamic(value)) {}

  template <typename T>
  inline BuilderFor<T> as() { return AsImpl<T>::apply(*this); }
  // See DynamicValue::Reader::as().

  inline Type getType() { return type; }
  // Get the type of this value.

  Reader asReader() const;

  inline Builder(Builder& other) { memcpy(this, &other, sizeof(*this)); }
  inline Builder(Builder&& other) { memcpy(this, &other, sizeof(*this)); }
  inline Builder& operator=(Builder& other) { memcpy(this, &other, sizeof(*this)); return *this; }
  inline Builder& operator=(Builder&& other) { memcpy(this, &other, sizeof(*this)); return *this; }
  static_assert(__has_trivial_copy(StructSchema) && __has_trivial_copy(ListSchema),
                "Assumptions made here do not hold.");
  // Hack:  We know this type is trivially constructable but the use of DisallowConstCopy causes
  //   the compiler to believe otherwise.

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
    DynamicObject::Builder objectValue;
  };

  template <typename T, Kind kind = kind<T>()> struct AsImpl;
  // Implementation backing the as() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.

  friend class Orphan<DynamicValue>;
};

kj::StringTree KJ_STRINGIFY(const DynamicValue::Reader& value);
kj::StringTree KJ_STRINGIFY(const DynamicValue::Builder& value);
kj::StringTree KJ_STRINGIFY(DynamicEnum value);
kj::StringTree KJ_STRINGIFY(const DynamicObject::Reader& value);
kj::StringTree KJ_STRINGIFY(const DynamicObject::Builder& value);
kj::StringTree KJ_STRINGIFY(const DynamicStruct::Reader& value);
kj::StringTree KJ_STRINGIFY(const DynamicStruct::Builder& value);
kj::StringTree KJ_STRINGIFY(const DynamicList::Reader& value);
kj::StringTree KJ_STRINGIFY(const DynamicList::Builder& value);

// -------------------------------------------------------------------
// Orphan <-> Dynamic glue

template <>
class Orphan<DynamicStruct> {
public:
  Orphan() = default;
  KJ_DISALLOW_COPY(Orphan);
  Orphan(Orphan&&) = default;
  Orphan& operator=(Orphan&&) = default;

  DynamicStruct::Builder get();
  DynamicStruct::Reader getReader() const;

  template <typename T>
  Orphan<T> releaseAs();
  // Like DynamicStruct::Builder::as(), but coerces the Orphan type.  Since Orphans are move-only,
  // the original Orphan<DynamicStruct> is no longer valid after this call; ownership is
  // transferred to the returned Orphan<T>.

  inline bool operator==(decltype(nullptr)) const { return builder == nullptr; }
  inline bool operator!=(decltype(nullptr)) const { return builder != nullptr; }

private:
  StructSchema schema;
  _::OrphanBuilder builder;

  inline Orphan(StructSchema schema, _::OrphanBuilder&& builder)
      : schema(schema), builder(kj::mv(builder)) {}

  template <typename, Kind>
  friend struct _::PointerHelpers;
  friend struct DynamicList;
  friend class Orphanage;
  friend class Orphan<DynamicObject>;
  friend class Orphan<DynamicValue>;
  friend class MessageBuilder;
};

template <>
class Orphan<DynamicList> {
public:
  Orphan() = default;
  KJ_DISALLOW_COPY(Orphan);
  Orphan(Orphan&&) = default;
  Orphan& operator=(Orphan&&) = default;

  DynamicList::Builder get();
  DynamicList::Reader getReader() const;

  template <typename T>
  Orphan<T> releaseAs();
  // Like DynamicList::Builder::as(), but coerces the Orphan type.  Since Orphans are move-only,
  // the original Orphan<DynamicStruct> is no longer valid after this call; ownership is
  // transferred to the returned Orphan<T>.

  inline bool operator==(decltype(nullptr)) const { return builder == nullptr; }
  inline bool operator!=(decltype(nullptr)) const { return builder != nullptr; }

private:
  ListSchema schema;
  _::OrphanBuilder builder;

  inline Orphan(ListSchema schema, _::OrphanBuilder&& builder)
      : schema(schema), builder(kj::mv(builder)) {}

  template <typename, Kind>
  friend struct _::PointerHelpers;
  friend struct DynamicList;
  friend class Orphanage;
  friend class Orphan<DynamicObject>;
  friend class Orphan<DynamicValue>;
};

template <>
class Orphan<DynamicObject> {
public:
  Orphan() = default;
  KJ_DISALLOW_COPY(Orphan);
  Orphan(Orphan&&) = default;
  Orphan& operator=(Orphan&&) = default;

  DynamicObject::Builder get();
  DynamicObject::Reader getReader() const;

  template <typename T>
  BuilderFor<T> getAs();
  // Coerce the object to the given type and return a builder for that type.  This may relocate
  // the object if it was originally created with a previous version of the schema and the sizes
  // don't match.
  //
  // Notice that DynamicObject::Builder does not have an "as<T>()" method, which is why this is
  // needed.

  template <typename T>
  Orphan<T> releaseAs();
  // Like DynamicValue::Builder::as(), but coerces the Orphan type.  Since Orphans are move-only,
  // the original Orphan<DynamicStruct> is no longer valid after this call; ownership is
  // transferred to the returned Orphan<T>.

  DynamicStruct::Builder getAs(StructSchema schema);
  DynamicList::Builder getAs(ListSchema schema);
  // Dynamic versions of 'getAs()'.

  Orphan<DynamicStruct> releaseAs(StructSchema schema);
  Orphan<DynamicList> releaseAs(ListSchema schema);
  // Dynamic versions of 'releaseAs()'.

  inline bool operator==(decltype(nullptr)) const { return builder == nullptr; }
  inline bool operator!=(decltype(nullptr)) const { return builder != nullptr; }

private:
  _::OrphanBuilder builder;

  explicit Orphan(_::OrphanBuilder&& builder): builder(kj::mv(builder)) {}

  template <typename, Kind>
  friend struct _::PointerHelpers;
  friend class Orphan<DynamicValue>;
  friend class Orphanage;
};

template <>
class Orphan<DynamicValue> {
public:
  inline Orphan(decltype(nullptr) n = nullptr): type(DynamicValue::UNKNOWN) {}
  inline Orphan(Void value);
  inline Orphan(bool value);
  inline Orphan(char value);
  inline Orphan(signed char value);
  inline Orphan(short value);
  inline Orphan(int value);
  inline Orphan(long value);
  inline Orphan(long long value);
  inline Orphan(unsigned char value);
  inline Orphan(unsigned short value);
  inline Orphan(unsigned int value);
  inline Orphan(unsigned long value);
  inline Orphan(unsigned long long value);
  inline Orphan(float value);
  inline Orphan(double value);
  inline Orphan(DynamicEnum value);
  Orphan(Orphan&&) = default;
  template <typename T>
  Orphan(Orphan<T>&&);
  KJ_DISALLOW_COPY(Orphan);

  Orphan& operator=(Orphan&&) = default;

  inline DynamicValue::Type getType() { return type; }

  DynamicValue::Builder get();
  DynamicValue::Reader getReader() const;

  template <typename T>
  Orphan<T> releaseAs();
  // Like DynamicValue::Builder::as(), but coerces the Orphan type.  Since Orphans are move-only,
  // the original Orphan<DynamicStruct> is no longer valid after this call; ownership is
  // transferred to the returned Orphan<T>.

private:
  DynamicValue::Type type;
  union {
    Void voidValue;
    bool boolValue;
    int64_t intValue;
    uint64_t uintValue;
    double floatValue;
    DynamicEnum enumValue;
    StructSchema structSchema;
    ListSchema listSchema;
  };

  _::OrphanBuilder builder;
  // Only used if `type` is a pointer type.

  Orphan(DynamicValue::Builder value, _::OrphanBuilder&& builder);
  Orphan(DynamicValue::Type type, _::OrphanBuilder&& builder)
      : type(type), builder(kj::mv(builder)) {}
  Orphan(StructSchema structSchema, _::OrphanBuilder&& builder)
      : type(DynamicValue::STRUCT), structSchema(structSchema), builder(kj::mv(builder)) {}
  Orphan(ListSchema listSchema, _::OrphanBuilder&& builder)
      : type(DynamicValue::LIST), listSchema(listSchema), builder(kj::mv(builder)) {}

  template <typename, Kind>
  friend struct _::PointerHelpers;
  friend struct DynamicStruct;
  friend struct DynamicList;
  friend class Orphanage;
};

template <typename T>
BuilderFor<T> Orphan<DynamicObject>::getAs() {
  return getAs(Schema::from<T>()).template as<T>();
}
template <>
Text::Builder Orphan<DynamicObject>::getAs<Text>();
template <>
Data::Builder Orphan<DynamicObject>::getAs<Data>();

template <typename T>
Orphan<T> Orphan<DynamicObject>::releaseAs() {
  return releaseAs(Schema::from<T>()).template releaseAs<T>();
}
template <>
Orphan<Text> Orphan<DynamicObject>::releaseAs<Text>();
template <>
Orphan<Data> Orphan<DynamicObject>::releaseAs<Data>();

template <typename T>
Orphan<DynamicValue>::Orphan(Orphan<T>&& other)
    : Orphan(other.get(), kj::mv(other.builder)) {}

template <typename T>
Orphan<T> Orphan<DynamicStruct>::releaseAs() {
  get().as<T>();  // type check
  return Orphan<T>(kj::mv(builder));
}

template <typename T>
Orphan<T> Orphan<DynamicList>::releaseAs() {
  get().as<T>();  // type check
  return Orphan<T>(kj::mv(builder));
}

template <typename T>
Orphan<T> Orphan<DynamicValue>::releaseAs() {
  get().as<T>();  // type check
  type = DynamicValue::UNKNOWN;
  return Orphan<T>(kj::mv(builder));
}

template <>
Orphan<DynamicStruct> Orphan<DynamicValue>::releaseAs<DynamicStruct>();
template <>
Orphan<DynamicList> Orphan<DynamicValue>::releaseAs<DynamicList>();

template <>
struct Orphanage::GetInnerBuilder<DynamicStruct, Kind::UNKNOWN> {
  static inline _::StructBuilder apply(DynamicStruct::Builder& t) {
    return t.builder;
  }
};

template <>
struct Orphanage::GetInnerBuilder<DynamicList, Kind::UNKNOWN> {
  static inline _::ListBuilder apply(DynamicList::Builder& t) {
    return t.builder;
  }
};

template <>
inline Orphan<DynamicStruct> Orphanage::newOrphanCopy<DynamicStruct::Reader>(
    const DynamicStruct::Reader& copyFrom) const {
  return Orphan<DynamicStruct>(
      copyFrom.getSchema(), _::OrphanBuilder::copy(arena, copyFrom.reader));
}

template <>
inline Orphan<DynamicList> Orphanage::newOrphanCopy<DynamicList::Reader>(
    const DynamicList::Reader& copyFrom) const {
  return Orphan<DynamicList>(copyFrom.getSchema(), _::OrphanBuilder::copy(arena, copyFrom.reader));
}

template <>
Orphan<DynamicObject> Orphanage::newOrphanCopy<DynamicObject::Reader>(
    const DynamicObject::Reader& copyFrom) const;

template <>
Orphan<DynamicValue> Orphanage::newOrphanCopy<DynamicValue::Reader>(
    const DynamicValue::Reader& copyFrom) const;

// -------------------------------------------------------------------
// Inject the ability to use DynamicStruct for message roots and Dynamic{Struct,List} for
// generated Object accessors.

template <>
DynamicStruct::Reader MessageReader::getRoot<DynamicStruct>(StructSchema schema);
template <>
DynamicStruct::Builder MessageBuilder::initRoot<DynamicStruct>(StructSchema schema);
template <>
DynamicStruct::Builder MessageBuilder::getRoot<DynamicStruct>(StructSchema schema);
template <>
void MessageBuilder::setRoot<DynamicStruct::Reader>(DynamicStruct::Reader&& value);
template <>
void MessageBuilder::setRoot<const DynamicStruct::Reader&>(const DynamicStruct::Reader& value);
template <>
void MessageBuilder::setRoot<DynamicStruct::Reader&>(DynamicStruct::Reader& value);
template <>
inline void MessageBuilder::adoptRoot<DynamicStruct>(Orphan<DynamicStruct>&& orphan) {
  adoptRootInternal(kj::mv(orphan.builder));
}

namespace _ {  // private

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
      StructBuilder builder, WirePointerCount index, const DynamicStruct::Reader& value);
  static DynamicStruct::Builder init(
      StructBuilder builder, WirePointerCount index, StructSchema schema);
  static inline void adopt(StructBuilder builder, WirePointerCount index,
                           Orphan<DynamicStruct>&& value) {
    builder.adopt(index, kj::mv(value.builder));
  }
  static inline Orphan<DynamicStruct> disown(StructBuilder builder, WirePointerCount index,
                                             StructSchema schema) {
    return Orphan<DynamicStruct>(schema, builder.disown(index));
  }
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
      StructBuilder builder, WirePointerCount index, const DynamicList::Reader& value);
  static DynamicList::Builder init(
      StructBuilder builder, WirePointerCount index, ListSchema schema, uint size);
  static inline void adopt(StructBuilder builder, WirePointerCount index,
                           Orphan<DynamicList>&& value) {
    builder.adopt(index, kj::mv(value.builder));
  }
  static inline Orphan<DynamicList> disown(StructBuilder builder, WirePointerCount index,
                                           ListSchema schema) {
    return Orphan<DynamicList>(schema, builder.disown(index));
  }
};

template <>
struct PointerHelpers<DynamicObject, Kind::UNKNOWN> {
  static DynamicObject::Reader get(StructReader reader, WirePointerCount index);
  static DynamicObject::Builder get(StructBuilder builder, WirePointerCount index);
  static void set(
      StructBuilder builder, WirePointerCount index, const DynamicObject::Reader& value);
  static inline void adopt(StructBuilder builder, WirePointerCount index,
                           Orphan<DynamicObject>&& value) {
    builder.adopt(index, kj::mv(value.builder));
  }
  static inline Orphan<DynamicObject> disown(StructBuilder builder, WirePointerCount index) {
    return Orphan<DynamicObject>(builder.disown(index));
  }
};

}  // namespace _ (private)

// =======================================================================================
// Inline implementation details.

template <typename T>
struct ToDynamic_<T, Kind::STRUCT> {
  static inline DynamicStruct::Reader apply(const typename T::Reader& value) {
    return DynamicStruct::Reader(Schema::from<T>(), value._reader);
  }
  static inline DynamicStruct::Builder apply(typename T::Builder& value) {
    return DynamicStruct::Builder(Schema::from<T>(), value._builder);
  }
};

template <typename T>
struct ToDynamic_<T, Kind::LIST> {
  static inline DynamicList::Reader apply(const typename T::Reader& value) {
    return DynamicList::Reader(Schema::from<T>(), value.reader);
  }
  static inline DynamicList::Builder apply(typename T::Builder& value) {
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
  return DynamicEnum(Schema::from<kj::Decay<T>>(), static_cast<uint16_t>(value));
}

inline DynamicValue::Reader::Reader(std::nullptr_t n): type(UNKNOWN) {}
inline DynamicValue::Builder::Builder(std::nullptr_t n): type(UNKNOWN) {}

#define CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(cppType, typeTag, fieldName) \
inline DynamicValue::Reader::Reader(cppType value) \
    : type(typeTag), fieldName##Value(value) {} \
inline DynamicValue::Builder::Builder(cppType value) \
    : type(typeTag), fieldName##Value(value) {} \
inline Orphan<DynamicValue>::Orphan(cppType value) \
    : type(DynamicValue::typeTag), fieldName##Value(value) {}

CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(Void, VOID, void);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(bool, BOOL, bool);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(char, INT, int);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(signed char, INT, int);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(short, INT, int);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(int, INT, int);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(long, INT, int);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(long long, INT, int);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(unsigned char, UINT, uint);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(unsigned short, UINT, uint);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(unsigned int, UINT, uint);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(unsigned long, UINT, uint);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(unsigned long long, UINT, uint);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(float, FLOAT, float);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(double, FLOAT, float);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(DynamicEnum, ENUM, enum);
#undef CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR

#define CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(cppType, typeTag, fieldName) \
inline DynamicValue::Reader::Reader(const cppType::Reader& value) \
    : type(typeTag), fieldName##Value(value) {} \
inline DynamicValue::Builder::Builder(cppType::Builder value) \
    : type(typeTag), fieldName##Value(value) {}

CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(Text, TEXT, text);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(Data, DATA, data);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(DynamicList, LIST, list);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(DynamicStruct, STRUCT, struct);
CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR(DynamicObject, OBJECT, object);

#undef CAPNP_DECLARE_DYNAMIC_VALUE_CONSTRUCTOR

inline DynamicValue::Reader::Reader(const char* value): Reader(Text::Reader(value)) {}

#define CAPNP_DECLARE_TYPE(name, discrim, typeName) \
template <> \
struct DynamicValue::Reader::AsImpl<typeName> { \
  static ReaderFor<typeName> apply(const Reader& reader); \
}; \
template <> \
struct DynamicValue::Builder::AsImpl<typeName> { \
  static BuilderFor<typeName> apply(Builder& builder); \
};

//CAPNP_DECLARE_TYPE(void, VOID, Void)
CAPNP_DECLARE_TYPE(bool, BOOL, bool)
CAPNP_DECLARE_TYPE(int8, INT8, int8_t)
CAPNP_DECLARE_TYPE(int16, INT16, int16_t)
CAPNP_DECLARE_TYPE(int32, INT32, int32_t)
CAPNP_DECLARE_TYPE(int64, INT64, int64_t)
CAPNP_DECLARE_TYPE(uint8, UINT8, uint8_t)
CAPNP_DECLARE_TYPE(uint16, UINT16, uint16_t)
CAPNP_DECLARE_TYPE(uint32, UINT32, uint32_t)
CAPNP_DECLARE_TYPE(uint64, UINT64, uint64_t)
CAPNP_DECLARE_TYPE(float32, FLOAT32, float)
CAPNP_DECLARE_TYPE(float64, FLOAT64, double)

CAPNP_DECLARE_TYPE(text, TEXT, Text)
CAPNP_DECLARE_TYPE(data, DATA, Data)
CAPNP_DECLARE_TYPE(list, LIST, DynamicList)
CAPNP_DECLARE_TYPE(struct, STRUCT, DynamicStruct)
CAPNP_DECLARE_TYPE(enum, ENUM, DynamicEnum)
CAPNP_DECLARE_TYPE(object, OBJECT, DynamicObject)
#undef CAPNP_DECLARE_TYPE

// CAPNP_DECLARE_TYPE(Void) causes gcc 4.7 to segfault.  If I do it manually and remove the
// ReaderFor<> and BuilderFor<> wrappers, it works.
template <>
struct DynamicValue::Reader::AsImpl<Void> {
  static Void apply(const Reader& reader);
};
template <>
struct DynamicValue::Builder::AsImpl<Void> {
  static Void apply(Builder& builder);
};

template <typename T>
struct DynamicValue::Reader::AsImpl<T, Kind::ENUM> {
  static T apply(const Reader& reader) {
    return reader.as<DynamicEnum>().as<T>();
  }
};
template <typename T>
struct DynamicValue::Builder::AsImpl<T, Kind::ENUM> {
  static T apply(Builder& builder) {
    return builder.as<DynamicEnum>().as<T>();
  }
};

template <typename T>
struct DynamicValue::Reader::AsImpl<T, Kind::STRUCT> {
  static typename T::Reader apply(const Reader& reader) {
    return reader.as<DynamicStruct>().as<T>();
  }
};
template <typename T>
struct DynamicValue::Builder::AsImpl<T, Kind::STRUCT> {
  static typename T::Builder apply(Builder& builder) {
    return builder.as<DynamicStruct>().as<T>();
  }
};

template <typename T>
struct DynamicValue::Reader::AsImpl<T, Kind::LIST> {
  static typename T::Reader apply(const Reader& reader) {
    return reader.as<DynamicList>().as<T>();
  }
};
template <typename T>
struct DynamicValue::Builder::AsImpl<T, Kind::LIST> {
  static typename T::Builder apply(Builder& builder) {
    return builder.as<DynamicList>().as<T>();
  }
};

// -------------------------------------------------------------------

template <typename T>
inline typename T::Reader DynamicObject::Reader::as() const {
  return as(Schema::from<T>()).template as<T>();
}

// -------------------------------------------------------------------

template <typename T>
typename T::Reader DynamicStruct::Reader::as() const {
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
inline DynamicStruct::Reader DynamicStruct::Reader::as<DynamicStruct>() const {
  return *this;
}
template <>
inline DynamicStruct::Builder DynamicStruct::Builder::as<DynamicStruct>() {
  return *this;
}

inline DynamicStruct::Reader DynamicStruct::Builder::asReader() const {
  return DynamicStruct::Reader(schema, builder.asReader());
}

// -------------------------------------------------------------------

template <typename T>
typename T::Reader DynamicList::Reader::as() const {
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
inline DynamicList::Reader DynamicList::Reader::as<DynamicList>() const {
  return *this;
}
template <>
inline DynamicList::Builder DynamicList::Builder::as<DynamicList>() {
  return *this;
}

// -------------------------------------------------------------------

template <typename T>
ReaderFor<T> ConstSchema::as() const {
  return DynamicValue::Reader(*this).as<T>();
}

}  // namespace capnp

#endif  // CAPNP_DYNAMIC_H_
