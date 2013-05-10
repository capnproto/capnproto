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

#include "schema.capnp.h"
#include "layout.h"

namespace capnproto {

class MessageReader;
class MessageBuilder;

struct DynamicValue {
  class Reader;
  class Builder;
};
class DynamicEnum;
struct DynamicObject {
  class Reader;
  class Builder;
};
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

class SchemaPool {
  // Class representing a pool of schema data which is indexed for convenient traversal.
  //
  // TODO(someday):  Allow registration of a callback to look up missing nodes.  The callback
  //   should not return a value, but instead should receive a pointer to the pool to populate.
  //   This will make it easier to deal with ownership and to batch-add related nodes.

public:
  SchemaPool(): base(nullptr), impl(nullptr) {}
  // Constructs an empty pool.

  explicit SchemaPool(const SchemaPool& base): base(&base), impl(nullptr) {}
  // Constructs a pool that extends the given base pool.  The behavior is similar to copying the
  // base pool, except that memory is shared.  The base pool must outlive the new pool.
  //
  // The purpose of this is to allow a pool to be shared by multiple threads.  The from*() methods
  // may add new schema nodes to the pool if they aren't already present, and therefore they are
  // not thread-safe.  However, if you create a shared pool that contains all the types you need,
  // then it is reasonably efficient to create a fresh SchemaPool on the stack extending the shared
  // pool each time you need to manipulate a type dynamically.  If the shared pool ends up
  // containing everything that is needed, then the extension pool won't allocate any memory at all.

  ~SchemaPool();

  void add(schema::Node::Reader node);
  // Add a schema node.  It will be copied and validated, throwing an exception if invalid.  If
  // another node with the same ID already exists, the nodes will be compared for compatibility
  // and the definition determined to be "newer" will be kept.  If the nodes are not compatible,
  // an exception will be thrown.

  template <typename T>
  void add();
  // Add schema for the given compiled-in type and all of its transitive dependencies, including
  // nested nodes, but NOT necessarily including annotation definitions (because those are not
  // always compiled in) or parent scopes (because adding parent scopes would necessarily mean
  // adding all types in the file and in all transitive imports, which may be much more than you
  // want).

  bool has(uint64_t typeId) const;

  template <typename T> DynamicEnum fromEnum(T&& value);
  template <typename T> DynamicStruct::Reader fromStructReader(T&& reader);
  template <typename T> DynamicStruct::Builder fromStructBuilder(T&& builder);
  template <typename T> DynamicList::Reader fromListReader(T&& reader);
  template <typename T> DynamicList::Builder fromListBuilder(T&& builder);

  DynamicStruct::Reader getRoot(MessageReader& message, uint64_t typeId) const;
  DynamicStruct::Builder getRoot(MessageBuilder& message, uint64_t typeId) const;

private:
  struct Impl;

  const SchemaPool* base;
  Impl* impl;

  SchemaPool& operator=(const SchemaPool&) = delete;

  void addNoCopy(schema::Node::Reader node);

  schema::Node::Reader getStruct(uint64_t id) const;
  schema::Node::Reader getEnum(uint64_t id) const;
  schema::Node::Reader getInterface(uint64_t id) const;

  friend class DynamicEnum;
  friend class DynamicStruct;
  friend class DynamicList;
  friend class DynamicObject;
};

// -------------------------------------------------------------------

namespace internal {

struct ListSchema {
  // Hack for determining/specifying the schema for a List without having to construct a Cap'n Proto
  // message.

  schema::Type::Body::Which elementType;
  uint8_t nestingDepth;  // 0 for T, 1 for List(T), 2 for List(List(T)), ...
  uint64_t elementTypeId;

  constexpr ListSchema(schema::Type::Body::Which elementType)
      : elementType(elementType), nestingDepth(0), elementTypeId(0) {}
  constexpr ListSchema(schema::Type::Body::Which elementType, uint64_t elementTypeId)
      : elementType(elementType), nestingDepth(0), elementTypeId(elementTypeId) {}
  constexpr ListSchema(schema::Type::Body::Which elementType, uint8_t nestingDepth,
                       uint64_t elementTypeId)
      : elementType(elementType), nestingDepth(nestingDepth), elementTypeId(elementTypeId) {}

  ListSchema(schema::Type::Reader elementType);
  // Construct from an actual schema.

  constexpr ListSchema deeper() {
    return ListSchema(elementType, nestingDepth + 1, elementTypeId);
  }
};

template <typename ElementType, Kind kind = kind<ElementType>()>
struct ListSchemaFor;

#define CAPNPROTO_DECLARE_TYPE(discrim, typeName) \
template <> \
struct ListSchemaFor<List<typeName>> { \
  static constexpr ListSchema type = ListSchema(schema::Type::Body::discrim##_TYPE); \
};

CAPNPROTO_DECLARE_TYPE(VOID, Void)
CAPNPROTO_DECLARE_TYPE(BOOL, bool)
CAPNPROTO_DECLARE_TYPE(INT8, int8_t)
CAPNPROTO_DECLARE_TYPE(INT16, int16_t)
CAPNPROTO_DECLARE_TYPE(INT32, int32_t)
CAPNPROTO_DECLARE_TYPE(INT64, int64_t)
CAPNPROTO_DECLARE_TYPE(UINT8, uint8_t)
CAPNPROTO_DECLARE_TYPE(UINT16, uint16_t)
CAPNPROTO_DECLARE_TYPE(UINT32, uint32_t)
CAPNPROTO_DECLARE_TYPE(UINT64, uint64_t)
CAPNPROTO_DECLARE_TYPE(FLOAT32, float)
CAPNPROTO_DECLARE_TYPE(FLOAT64, double)

CAPNPROTO_DECLARE_TYPE(TEXT, Text)
CAPNPROTO_DECLARE_TYPE(DATA, Data)
CAPNPROTO_DECLARE_TYPE(LIST, DynamicList)

#undef CAPNPROTO_DECLARE_TYPE

template <typename T>
struct ListSchemaFor<T, Kind::ENUM> {
  static constexpr ListSchema type = ListSchema(schema::Type::Body::ENUM_TYPE, typeId<T>());
};
template <typename T>
struct ListSchemaFor<T, Kind::STRUCT> {
  static constexpr ListSchema type = ListSchema(schema::Type::Body::STRUCT_TYPE, typeId<T>());
};
template <typename T>
struct ListSchemaFor<T, Kind::INTERFACE> {
  static constexpr ListSchema type = ListSchema(schema::Type::Body::INTERFACE_TYPE, typeId<T>());
};

template <typename T>
struct ListSchemaFor<List<T>, Kind::LIST> {
  static constexpr ListSchema type = ListSchemaFor<T>::schema.deeper();
};

} // namespace internal

// -------------------------------------------------------------------

class DynamicEnum {
public:
  DynamicEnum() = default;

  template <typename T>
  inline T to() { return static_cast<T>(toImpl(typeId<T>())); }
  // Cast to a native enum type.

  schema::Node::Reader getSchemaNode() { return schema; }
  schema::EnumNode::Reader getSchema();

  Maybe<schema::EnumNode::Enumerant::Reader> getEnumerant();
  // Get which enumerant this enum value represents.  Returns nullptr if the numeric value does not
  // correspond to any enumerant in the schema -- this can happen if the data was built using a
  // newer schema that has more values defined.

  Maybe<schema::EnumNode::Enumerant::Reader> findEnumerantByName(Text::Reader name);
  // Search this enum's type for an enumerant with the given name.

  inline uint16_t getRaw() { return value; }
  // Returns the raw underlying enum value.

private:
  const SchemaPool* pool;
  schema::Node::Reader schema;
  uint16_t value;

  inline DynamicEnum(const SchemaPool* pool, schema::Node::Reader schema, uint16_t value)
      : pool(pool), schema(schema), value(value) {}

  uint16_t toImpl(uint64_t requestedTypeId);

  friend struct DynamicStruct;
};

// -------------------------------------------------------------------

class DynamicObject::Reader {
public:
  Reader() = default;

  template <typename T>
  inline typename T::Reader to() { return ToImpl<T>::apply(*this); }
  // Convert the object to the given struct, list, or blob type.

  DynamicStruct::Reader toStruct(schema::Node::Reader schema);
  DynamicList::Reader toList(schema::Type::Reader elementType);

private:
  const SchemaPool* pool;
  internal::ObjectReader reader;

  inline Reader(const SchemaPool* pool, internal::ObjectReader reader)
      : pool(pool), reader(reader) {}

  template <typename T, Kind kind = kind<T>()> struct ToImpl;
  // Implementation backing the to() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.

  DynamicStruct::Reader toStruct(uint64_t typeId);
  DynamicList::Reader toList(internal::ListSchema schema);

  friend struct DynamicStruct;
};

class DynamicObject::Builder {
public:
  Builder() = default;

  template <typename T>
  inline typename T::Builder to() { return ToImpl<T>::apply(*this); }
  // Convert the object to the given struct, list, or blob type.

  DynamicStruct::Builder toStruct(schema::Node::Reader schema);
  DynamicList::Builder toList(schema::Type::Reader elementType);

private:
  const SchemaPool* pool;
  internal::ObjectBuilder builder;

  inline Builder(const SchemaPool* pool, internal::ObjectBuilder builder)
      : pool(pool), builder(builder) {}

  template <typename T, Kind kind = kind<T>()> struct ToImpl;
  // Implementation backing the to() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.

  DynamicStruct::Builder toStruct(uint64_t typeId);
  DynamicList::Builder toList(internal::ListSchema schema);

  friend struct DynamicStruct;
};

// -------------------------------------------------------------------

class DynamicUnion::Reader {
public:
  Reader() = default;

  schema::StructNode::Member::Reader getMemberSchema() { return schema; }
  schema::StructNode::Union::Reader getSchema();

  Maybe<schema::StructNode::Member::Reader> which();
  // Returns which field is set, or nullptr if an unknown field is set (i.e. the schema is old, and
  // the underlying data has the union set to a member we don't know about).

  DynamicValue::Reader get();
  // Get the value of whichever field of the union is set.

private:
  const SchemaPool* pool;
  schema::StructNode::Member::Reader schema;
  internal::StructReader reader;

  inline Reader(const SchemaPool* pool, schema::StructNode::Member::Reader schema,
                internal::StructReader reader)
      : pool(pool), schema(schema), reader(reader) {}
};

class DynamicUnion::Builder {
public:
  Builder() = default;

  schema::StructNode::Member::Reader getMemberSchema() { return schema; }
  schema::StructNode::Union::Reader getSchema();

  Maybe<schema::StructNode::Member::Reader> which();
  // Returns which field is set, or nullptr if an unknown field is set (i.e. the schema is old, and
  // the underlying data has the union set to a member we don't know about).

  DynamicValue::Builder get();
  void set(schema::StructNode::Field::Reader field, DynamicValue::Reader value);
  DynamicValue::Builder init(schema::StructNode::Field::Reader field);

private:
  const SchemaPool* pool;
  schema::StructNode::Member::Reader schema;
  internal::StructBuilder builder;

  inline Builder(const SchemaPool* pool, schema::StructNode::Member::Reader schema,
                 internal::StructBuilder builder)
      : pool(pool), schema(schema), builder(builder) {}
};

// -------------------------------------------------------------------

class DynamicStruct::Reader {
public:
  Reader() = default;

  template <typename T>
  typename T::Reader to();
  // Convert the dynamic struct to its compiled-in type.

  schema::Node::Reader getSchemaNode();
  schema::StructNode::Reader getSchema();

  Maybe<schema::StructNode::Member::Reader> findMemberByName(Text::Reader name);
  // Looks up the member with the given name, or returns nullptr if no such member exists.

  DynamicValue::Reader getField(schema::StructNode::Field::Reader field);
  // Returns the value of the given field.

  DynamicUnion::Reader getUnion(schema::StructNode::Union::Reader un);
  // Returns the value of the given union.

private:
  const SchemaPool* pool;
  schema::Node::Reader schema;
  internal::StructReader reader;

  inline Reader(const SchemaPool* pool, schema::Node::Reader schema, internal::StructReader reader)
      : pool(pool), schema(schema), reader(reader) {}

  static DynamicValue::Reader getFieldImpl(
      const SchemaPool* pool, internal::StructReader reader,
      schema::StructNode::Field::Reader field);

  template <typename T>
  friend struct internal::PointerHelpers;
  friend class DynamicUnion::Reader;
  friend class DynamicObject;
};

class DynamicStruct::Builder {
public:
  Builder() = default;

  template <typename T>
  typename T::Builder to();
  // Cast to a particular struct type.

  schema::Node::Reader getSchemaNode();
  schema::StructNode::Reader getSchema();

  Maybe<schema::StructNode::Member::Reader> findMemberByName(Text::Reader name);
  // Looks up the member with the given name, or returns nullptr if no such member exists.

  DynamicValue::Builder getField(schema::StructNode::Field::Reader field);
  // Returns the value of the given field.

  void setField(schema::StructNode::Field::Reader field, DynamicValue::Reader value);
  // Sets the value of the given field.

  DynamicStruct::Builder initField(schema::StructNode::Field::Reader field);
  DynamicList::Builder initField(schema::StructNode::Field::Reader field, uint size);
  // Initialize a struct or list field by field schema.

  DynamicUnion::Builder getUnion(schema::StructNode::Union::Reader un);
  // Returns the value of the given union.

  void copyFrom(Reader other);

  Reader asReader();

private:
  const SchemaPool* pool;
  schema::Node::Reader schema;
  internal::StructBuilder builder;

  inline Builder(const SchemaPool* pool, schema::Node::Reader schema,
                 internal::StructBuilder builder)
      : pool(pool), schema(schema), builder(builder) {}

  static DynamicValue::Builder getFieldImpl(
      const SchemaPool* pool, internal::StructBuilder builder,
      schema::StructNode::Field::Reader field);
  static void setFieldImpl(
      const SchemaPool* pool, internal::StructBuilder builder,
      schema::StructNode::Field::Reader field, DynamicValue::Reader value);
  static DynamicList::Builder initListFieldImpl(
      const SchemaPool* pool, internal::StructBuilder builder,
      schema::StructNode::Field::Reader field, uint length);
  static DynamicStruct::Builder initStructFieldImpl(
      const SchemaPool* pool, internal::StructBuilder builder,
      schema::StructNode::Field::Reader field);

  template <typename T>
  friend struct internal::PointerHelpers;
  friend class DynamicUnion::Builder;
  friend class DynamicObject;
};

// -------------------------------------------------------------------

class DynamicList::Reader {
public:
  Reader() = default;
  inline explicit Reader(internal::ListReader reader): reader(reader) {}

  template <typename T>
  typename T::Reader to();
  // Try to convert to any List<T>, Data, or Text.  Throws an exception if the underlying data
  // can't possibly represent the requested type.

  inline uint size() { return reader.size() / ELEMENTS; }
  DynamicValue::Reader operator[](uint index);

  typedef internal::IndexingIterator<Reader, DynamicValue::Reader> iterator;
  inline iterator begin() { return iterator(this, 0); }
  inline iterator end() { return iterator(this, size()); }

private:
  const SchemaPool* pool;

  // We don't encode the element type as schema::Type::Reader because we want to be able to
  // construct DynamicList from List<T> without knowing of any particular field that has type
  // List<T>, and we don't want to construct a fresh schema object every time this happens.

  schema::Type::Body::Which elementType;  // cannot be list
  uint depth;
  // Number of types elementType must be wrapped in List() to get the actual element type, e.g.
  // List(List(List(Bool))) has depth = 2.

  schema::Node::Body::Reader elementSchema;  // if elementType is struct/enum/interface

  internal::ListReader reader;

  Reader(const SchemaPool* pool, schema::Type::Reader elementType, internal::ListReader reader);
  Reader(const SchemaPool* pool, internal::ListSchema schema, internal::ListReader reader);
  friend class DynamicStruct;
  friend class DynamicObject;
};

class DynamicList::Builder {
public:
  Builder() = default;
  inline explicit Builder(internal::ListBuilder builder): builder(builder) {}

  template <typename T>
  typename T::Builder to();
  // Try to convert to any List<T>, Data, or Text.  Throws an exception if the underlying data
  // can't possibly represent the requested type.

  inline uint size() { return builder.size() / ELEMENTS; }
  DynamicStruct::Builder operator[](uint index);

  typedef internal::IndexingIterator<Builder, DynamicStruct::Builder> iterator;
  inline iterator begin() { return iterator(this, 0); }
  inline iterator end() { return iterator(this, size()); }

  void copyFrom(Reader other);

  Reader asReader();

private:
  const SchemaPool* pool;
  schema::Type::Body::Which elementType;
  uint depth;
  schema::Node::Body::Reader elementSchema;
  internal::ListBuilder builder;

  Builder(const SchemaPool* pool, schema::Type::Reader elementType, internal::ListBuilder builder);
  Builder(const SchemaPool* pool, internal::ListSchema schema, internal::ListBuilder builder);
  friend class DynamicStruct;
  friend class DynamicObject;
};

// -------------------------------------------------------------------

namespace internal {

// Make sure ReaderFor<T> and BuilderFor<T> work for DynamicEnum, DynamicObject, DynamicStruct, and
// DynamicList, so that we can define DynamicValue::to().

template <>
struct MaybeReaderBuilder<DynamicEnum, Kind::UNKNOWN> {
  typedef DynamicEnum Reader;
  typedef DynamicEnum Builder;
};

template <>
struct MaybeReaderBuilder<DynamicObject, Kind::UNKNOWN> {
  typedef DynamicObject::Reader Reader;
  typedef DynamicObject::Builder Builder;
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
  inline Reader() {}
  inline Reader(Void voidValue);
  inline Reader(bool boolValue);
  inline Reader(int8_t int8Value);
  inline Reader(int16_t int16Value);
  inline Reader(int32_t int32Value);
  inline Reader(int64_t int64Value);
  inline Reader(uint8_t uint8Value);
  inline Reader(uint16_t uint16Value);
  inline Reader(uint32_t uint32Value);
  inline Reader(uint64_t uint64Value);
  inline Reader(float float32Value);
  inline Reader(double float64Value);
  inline Reader(Text::Reader textValue);
  inline Reader(Data::Reader dataValue);
  inline Reader(DynamicList::Reader listValue);
  inline Reader(DynamicEnum enumValue);
  inline Reader(DynamicStruct::Reader structValue);
  inline Reader(DynamicObject::Reader objectValue);

  template <typename T>
  inline ReaderFor<T> to() { return ToImpl<T>::apply(*this); }
  // Use to interpret the value as some type.  Allowed types are:
  // - Void, bool, [u]int{8,16,32,64}_t, float, double, any enum:  Returns the raw value.
  // - Text, Data, any struct type:  Returns the corresponding Reader.
  // - List<T> for any T listed above:  Returns List<T>::Reader.
  // - DynamicEnum:  Returns DynamicEnum.
  // - DynamicStruct, DynamicList, DynamicObject:  Returns the corresponding Reader.
  // If the requested type does not match the underlying data, the result is unspecified:  it may
  // throw an exception, it may return a garbage value, or it may return a composite value that
  // when traversed throws exceptions or returns garbage values.  Under none of these
  // circumstances will the program crash.

  inline schema::Type::Body::Which getType() { return type; }
  // Get the type of this value.

private:
  schema::Type::Body::Which type;

  union {
    Void voidValue;
    bool boolValue;
    int8_t int8Value;
    int16_t int16Value;
    int32_t int32Value;
    int64_t int64Value;
    uint8_t uint8Value;
    uint16_t uint16Value;
    uint32_t uint32Value;
    uint64_t uint64Value;
    float float32Value;
    double float64Value;
    Text::Reader textValue;
    Data::Reader dataValue;
    DynamicList::Reader listValue;
    DynamicEnum enumValue;
    DynamicStruct::Reader structValue;
    DynamicObject::Reader objectValue;
  };

  template <typename T, Kind kind = kind<T>()> struct ToImpl;
  // Implementation backing the to() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.
};

class DynamicValue::Builder {
public:
  inline Builder() {}
  inline Builder(Void voidValue);
  inline Builder(bool boolValue);
  inline Builder(int8_t int8Value);
  inline Builder(int16_t int16Value);
  inline Builder(int32_t int32Value);
  inline Builder(int64_t int64Value);
  inline Builder(uint8_t uint8Value);
  inline Builder(uint16_t uint16Value);
  inline Builder(uint32_t uint32Value);
  inline Builder(uint64_t uint64Value);
  inline Builder(float float32Value);
  inline Builder(double float64Value);
  inline Builder(Text::Builder textValue);
  inline Builder(Data::Builder dataValue);
  inline Builder(DynamicList::Builder listValue);
  inline Builder(DynamicEnum enumValue);
  inline Builder(DynamicStruct::Builder structValue);
  inline Builder(DynamicObject::Builder objectValue);

  template <typename T>
  inline BuilderFor<T> to() { return ToImpl<T>::apply(*this); }
  // See DynamicValue::Reader::to().

  inline schema::Type::Body::Which getType() { return type; }
  // Get the type of this value.

  Reader asReader();

private:
  schema::Type::Body::Which type;

  union {
    Void voidValue;
    bool boolValue;
    int8_t int8Value;
    int16_t int16Value;
    int32_t int32Value;
    int64_t int64Value;
    uint8_t uint8Value;
    uint16_t uint16Value;
    uint32_t uint32Value;
    uint64_t uint64Value;
    float float32Value;
    double float64Value;
    Text::Builder textValue;
    Data::Builder dataValue;
    DynamicList::Builder listValue;
    DynamicEnum enumValue;
    DynamicStruct::Builder structValue;
    DynamicObject::Builder objectValue;
  };

  template <typename T, Kind kind = kind<T>()> struct ToImpl;
  // Implementation backing the to() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.
};

// =======================================================================================
// Implementation details.

#define CAPNPROTO_DECLARE_TYPE(name, discrim, typeName) \
inline DynamicValue::Reader::Reader(ReaderFor<typeName> name##Value) \
    : type(schema::Type::Body::discrim##_TYPE), name##Value(name##Value) {} \
inline DynamicValue::Builder::Builder(BuilderFor<typeName> name##Value) \
    : type(schema::Type::Body::discrim##_TYPE), name##Value(name##Value) {} \
template <> \
struct DynamicValue::Reader::ToImpl<typeName> { \
  static ReaderFor<typeName> apply(Reader reader); \
}; \
template <> \
struct DynamicValue::Builder::ToImpl<typeName> { \
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
#undef CAPNPROTO_DECLARE_TYPE

// CAPNPROTO_DECLARE_TYPE(Void) causes gcc 4.7 to segfault.  If I do it manually and remove the
// ReaderFor<> and BuilderFor<> wrappers, it works.
inline DynamicValue::Reader::Reader(Void voidValue) \
    : type(schema::Type::Body::VOID_TYPE), voidValue(voidValue) {} \
inline DynamicValue::Builder::Builder(Void voidValue) \
    : type(schema::Type::Body::VOID_TYPE), voidValue(voidValue) {} \
template <>
struct DynamicValue::Reader::ToImpl<Void> {
  static Void apply(Reader reader);
};
template <>
struct DynamicValue::Builder::ToImpl<Void> {
  static Void apply(Builder builder);
};

template <typename T>
struct DynamicValue::Reader::ToImpl<T, Kind::ENUM> {
  static T apply(Reader reader) {
    return reader.to<DynamicEnum>().to<T>();
  }
};
template <typename T>
struct DynamicValue::Builder::ToImpl<T, Kind::ENUM> {
  static T apply(Builder builder) {
    return builder.to<DynamicEnum>().to<T>();
  }
};

template <typename T>
struct DynamicValue::Reader::ToImpl<T, Kind::STRUCT> {
  static T apply(Reader reader) {
    return reader.to<DynamicStruct>().to<T>();
  }
};
template <typename T>
struct DynamicValue::Builder::ToImpl<T, Kind::STRUCT> {
  static T apply(Builder builder) {
    return builder.to<DynamicStruct>().to<T>();
  }
};

template <typename T>
struct DynamicValue::Reader::ToImpl<T, Kind::LIST> {
  static T apply(Reader reader) {
    return reader.to<DynamicList>().to<T>();
  }
};
template <typename T>
struct DynamicValue::Builder::ToImpl<T, Kind::LIST> {
  static T apply(Builder builder) {
    return builder.to<DynamicList>().to<T>();
  }
};

// -------------------------------------------------------------------

template <typename T>
struct DynamicObject::Reader::ToImpl<T, Kind::STRUCT> {
  static T apply(Reader reader) {
    return reader.toStruct(typeId<T>()).to<T>();
  }
};
template <typename T>
struct DynamicObject::Builder::ToImpl<T, Kind::STRUCT> {
  static T apply(Builder builder) {
    return builder.toStruct(typeId<T>()).to<T>();
  }
};

template <typename T>
struct DynamicObject::Reader::ToImpl<List<T>, Kind::LIST> {
  static T apply(Reader reader) {
    return reader.toList(internal::ListSchemaFor<T>::schema).to<T>();
  }
};
template <typename T>
struct DynamicObject::Builder::ToImpl<List<T>, Kind::LIST> {
  static T apply(Builder builder) {
    return builder.toList(internal::ListSchemaFor<T>::schema).to<T>();
  }
};

}  // namespace capnproto

#endif  // CAPNPROTO_DYNAMIC_H_
