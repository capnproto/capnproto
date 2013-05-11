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
#include "message.h"

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

template <Kind k> struct DynamicTypeFor_;
template <> struct DynamicTypeFor_<Kind::ENUM> { typedef DynamicEnum Type; };
template <> struct DynamicTypeFor_<Kind::STRUCT> { typedef DynamicStruct Type; };
template <> struct DynamicTypeFor_<Kind::LIST> { typedef DynamicList Type; };

template <typename T>
using DynamicTypeFor = typename DynamicTypeFor_<kind<T>()>::Type;

class SchemaPool {
  // Class representing a pool of schema data which is indexed for convenient traversal.
  //
  // TODO(someday):  Allow registration of a callback to look up missing nodes.  The callback
  //   should not return a value, but instead should receive a pointer to the pool to populate.
  //   This will make it easier to deal with ownership and to batch-add related nodes.

public:
  SchemaPool();
  ~SchemaPool();
  CAPNPROTO_DISALLOW_COPY(SchemaPool);

  void add(schema::Node::Reader node);
  // Add a schema node.  It will be copied and validated, throwing an exception if invalid.  If
  // another node with the same ID already exists, the nodes will be compared for compatibility
  // and the definition determined to be "newer" will be kept.  If the nodes are not compatible,
  // an exception will be thrown.

  template <typename T>
  inline void add() { add(internal::rawSchema<T>()); }
  // Add schema for the given compiled-in type and all of its transitive dependencies, including
  // nested nodes, but NOT necessarily including annotation definitions (because those are not
  // always compiled in) or parent scopes (because adding parent scopes would necessarily mean
  // adding all types in the file and in all transitive imports, which may be much more than you
  // want).

  Maybe<schema::Node::Reader> tryGetNode(uint64_t id) const;
  // Try to look up the node, but return nullptr if it's unknown.

  schema::Node::Reader getNode(uint64_t id) const;
  // Look up the node with the given ID, throwing an exception if not found.

  schema::Node::Reader getStruct(uint64_t id) const;
  schema::Node::Reader getEnum(uint64_t id) const;
  schema::Node::Reader getInterface(uint64_t id) const;
  // Like getNode() but also throws if the kind is not as requested.

  template <typename T>
  ReaderFor<DynamicTypeFor<FromReader<T>>> toDynamic(T&& value) const;
  template <typename T>
  BuilderFor<DynamicTypeFor<FromBuilder<T>>> toDynamic(T&& value) const;
  // Convert an arbitrary struct or list reader or builder type into the equivalent dynamic type.
  // Example:
  //     DynamicStruct::Reader foo = pool.toDynamic(myType.getFoo());

  template <typename T> DynamicEnum fromEnum(T&& value) const;
  template <typename T> DynamicStruct::Reader fromStructReader(T&& reader) const;
  template <typename T> DynamicStruct::Builder fromStructBuilder(T&& builder) const;
  template <typename T> DynamicList::Reader fromListReader(T&& reader) const;
  template <typename T> DynamicList::Builder fromListBuilder(T&& builder) const;
  // Convert native types to dynamic types.

private:
  struct Impl;

  Impl* impl;

  void add(const internal::RawSchema& rawSchema);
  void addNoCopy(schema::Node::Reader node);

  template <typename T, Kind k = kind<T>()>
  struct ToDynamicImpl;

  friend class DynamicEnum;
  friend struct DynamicStruct;
  friend struct DynamicList;
  friend struct DynamicObject;
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
struct ListSchemaForElement;

#define CAPNPROTO_DECLARE_TYPE(discrim, typeName) \
template <> \
struct ListSchemaForElement<List<typeName>> { \
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
struct ListSchemaForElement<T, Kind::ENUM> {
  static constexpr ListSchema type = ListSchema(schema::Type::Body::ENUM_TYPE, typeId<T>());
};
template <typename T>
struct ListSchemaForElement<T, Kind::STRUCT> {
  static constexpr ListSchema type = ListSchema(schema::Type::Body::STRUCT_TYPE, typeId<T>());
};
template <typename T>
struct ListSchemaForElement<T, Kind::INTERFACE> {
  static constexpr ListSchema type = ListSchema(schema::Type::Body::INTERFACE_TYPE, typeId<T>());
};

template <typename T>
struct ListSchemaForElement<List<T>, Kind::LIST> {
  static constexpr ListSchema type = ListSchemaForElement<T>::schema.deeper();
};

template <typename T>
struct ListSchemaFor;
template <typename T>
struct ListSchemaFor<List<T>>: public ListSchemaForElement<T> {};

} // namespace internal

// -------------------------------------------------------------------

class DynamicEnum {
public:
  DynamicEnum() = default;

  template <typename T>
  inline T as() { return static_cast<T>(asImpl(typeId<T>())); }
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

  uint16_t asImpl(uint64_t requestedTypeId);

  friend struct DynamicStruct;
  friend struct DynamicList;
};

// -------------------------------------------------------------------

class DynamicObject::Reader {
public:
  Reader() = default;

  template <typename T>
  inline typename T::Reader as() { return asImpl<T>::apply(*this); }
  // Convert the object to the given struct, list, or blob type.

  DynamicStruct::Reader asStruct(schema::Node::Reader schema);
  DynamicList::Reader asList(schema::Type::Reader elementType);

private:
  const SchemaPool* pool;
  internal::ObjectReader reader;

  inline Reader(const SchemaPool* pool, internal::ObjectReader reader)
      : pool(pool), reader(reader) {}

  template <typename T, Kind kind = kind<T>()> struct asImpl;
  // Implementation backing the as() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.

  DynamicStruct::Reader asStruct(uint64_t typeId);
  DynamicList::Reader asList(internal::ListSchema schema);

  friend struct DynamicStruct;
  friend struct DynamicList;
};

class DynamicObject::Builder {
public:
  Builder() = default;

  template <typename T>
  inline typename T::Builder as() { return asImpl<T>::apply(*this); }
  // Convert the object to the given struct, list, or blob type.

  DynamicStruct::Builder asStruct(schema::Node::Reader schema);
  DynamicList::Builder asList(schema::Type::Reader elementType);

private:
  const SchemaPool* pool;
  internal::ObjectBuilder builder;

  inline Builder(const SchemaPool* pool, internal::ObjectBuilder builder)
      : pool(pool), builder(builder) {}

  template <typename T, Kind kind = kind<T>()> struct asImpl;
  // Implementation backing the as() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.

  DynamicStruct::Builder asStruct(uint64_t typeId);
  DynamicList::Builder asList(internal::ListSchema schema);

  friend struct DynamicStruct;
  friend struct DynamicList;
};

// -------------------------------------------------------------------

class DynamicUnion::Reader {
public:
  Reader() = default;

  schema::StructNode::Union::Reader getSchema() { return schema; }

  Maybe<schema::StructNode::Member::Reader> which();
  // Returns which field is set, or nullptr if an unknown field is set (i.e. the schema is old, and
  // the underlying data has the union set to a member we don't know about).

  DynamicValue::Reader get();
  // Get the value of whichever field of the union is set.

private:
  const SchemaPool* pool;
  schema::StructNode::Union::Reader schema;
  internal::StructReader reader;

  inline Reader(const SchemaPool* pool, schema::StructNode::Union::Reader schema,
                internal::StructReader reader)
      : pool(pool), schema(schema), reader(reader) {}

  friend struct DynamicStruct;
};

class DynamicUnion::Builder {
public:
  Builder() = default;

  schema::StructNode::Union::Reader getSchema() { return schema; }

  Maybe<schema::StructNode::Member::Reader> which();
  // Returns which field is set, or nullptr if an unknown field is set (i.e. the schema is old, and
  // the underlying data has the union set to a member we don't know about).

  DynamicValue::Builder get();
  void set(schema::StructNode::Field::Reader field, DynamicValue::Reader value);
  DynamicValue::Builder init(schema::StructNode::Field::Reader field);
  DynamicValue::Builder init(schema::StructNode::Field::Reader field, uint size);

private:
  const SchemaPool* pool;
  schema::StructNode::Union::Reader schema;
  internal::StructBuilder builder;

  inline Builder(const SchemaPool* pool, schema::StructNode::Union::Reader schema,
                 internal::StructBuilder builder)
      : pool(pool), schema(schema), builder(builder) {}

  friend struct DynamicStruct;
};

// -------------------------------------------------------------------

class DynamicStruct::Reader {
public:
  Reader() = default;

  template <typename T>
  typename T::Reader as();
  // Convert the dynamic struct to its compiled-in type.

  schema::Node::Reader getSchemaNode() { return schema; }
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

  void verifyTypeId(uint64_t id);

  static DynamicValue::Reader getFieldImpl(
      const SchemaPool* pool, internal::StructReader reader,
      schema::StructNode::Field::Reader field);

  template <typename T>
  friend struct internal::PointerHelpers;
  friend class DynamicUnion::Reader;
  friend struct DynamicObject;
  friend class DynamicStruct::Builder;
  friend struct DynamicList;
  friend class MessageReader;
  friend class MessageBuilder;
  friend class SchemaPool;
};

class DynamicStruct::Builder {
public:
  Builder() = default;

  template <typename T>
  typename T::Builder as();
  // Cast to a particular struct type.

  schema::Node::Reader getSchemaNode() { return schema; }
  schema::StructNode::Reader getSchema();

  Maybe<schema::StructNode::Member::Reader> findMemberByName(Text::Reader name);
  // Looks up the member with the given name, or returns nullptr if no such member exists.

  DynamicValue::Builder getField(schema::StructNode::Field::Reader field);
  // Returns the value of the given field.

  void setField(schema::StructNode::Field::Reader field, DynamicValue::Reader value);
  // Sets the value of the given field.

  DynamicValue::Builder initField(schema::StructNode::Field::Reader field);
  DynamicValue::Builder initField(schema::StructNode::Field::Reader field, uint size);
  // Initialize a struct or list field by field schema.

  DynamicValue::Builder initObjectField(schema::StructNode::Field::Reader field,
                                        schema::Type::Reader type);
  DynamicValue::Builder initObjectField(schema::StructNode::Field::Reader field,
                                        schema::Type::Reader type, uint size);
  // Initialize an Object-typed field.  You must specify the type to initialize as.

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

  void verifyTypeId(uint64_t id);

  static DynamicValue::Builder getFieldImpl(
      const SchemaPool* pool, internal::StructBuilder builder,
      schema::StructNode::Field::Reader field);
  static void setFieldImpl(
      const SchemaPool* pool, internal::StructBuilder builder,
      schema::StructNode::Field::Reader field, DynamicValue::Reader value);
  static DynamicValue::Builder initFieldImpl(
      const SchemaPool* pool, internal::StructBuilder builder,
      schema::StructNode::Field::Reader field, uint size);
  static DynamicValue::Builder initFieldImpl(
      const SchemaPool* pool, internal::StructBuilder builder,
      schema::StructNode::Field::Reader field);
  static DynamicValue::Builder initFieldImpl(
      const SchemaPool* pool, internal::StructBuilder builder,
      schema::StructNode::Field::Reader field,
      schema::Type::Reader type, uint size);
  static DynamicValue::Builder initFieldImpl(
      const SchemaPool* pool, internal::StructBuilder builder,
      schema::StructNode::Field::Reader field,
      schema::Type::Reader type);

  template <typename T>
  friend struct internal::PointerHelpers;
  friend class DynamicUnion::Builder;
  friend struct DynamicObject;
  friend struct DynamicList;
  friend class MessageReader;
  friend class MessageBuilder;
  friend class SchemaPool;
};

// -------------------------------------------------------------------

class DynamicList::Reader {
public:
  Reader() = default;
  inline explicit Reader(internal::ListReader reader): reader(reader) {}

  template <typename T>
  typename T::Reader as();
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

  schema::Node::Reader elementSchema;  // if elementType is struct/enum/interface

  internal::ListReader reader;

  Reader(const SchemaPool* pool, schema::Type::Reader elementType, internal::ListReader reader);
  Reader(const SchemaPool* pool, internal::ListSchema schema, internal::ListReader reader);
  Reader(const SchemaPool* pool, schema::Type::Body::Which elementType, uint depth,
         schema::Node::Reader elementSchema, internal::ListReader reader)
      : pool(pool), elementType(elementType), depth(depth), elementSchema(elementSchema),
        reader(reader) {}

  void verifySchema(internal::ListSchema schema);

  template <typename T>
  friend struct internal::PointerHelpers;
  friend struct DynamicStruct;
  friend struct DynamicObject;
  friend class DynamicList::Builder;
  friend class SchemaPool;
};

class DynamicList::Builder {
public:
  Builder() = default;
  inline explicit Builder(internal::ListBuilder builder): builder(builder) {}

  template <typename T>
  typename T::Builder as();
  // Try to convert to any List<T>, Data, or Text.  Throws an exception if the underlying data
  // can't possibly represent the requested type.

  inline uint size() { return builder.size() / ELEMENTS; }
  DynamicValue::Builder operator[](uint index);
  void set(uint index, DynamicValue::Reader value);
  DynamicValue::Builder init(uint index, uint size);

  typedef internal::IndexingIterator<Builder, DynamicStruct::Builder> iterator;
  inline iterator begin() { return iterator(this, 0); }
  inline iterator end() { return iterator(this, size()); }

  void copyFrom(Reader other);

  Reader asReader();

private:
  const SchemaPool* pool;
  schema::Type::Body::Which elementType;
  uint depth;
  schema::Node::Reader elementSchema;
  internal::ListBuilder builder;

  Builder(const SchemaPool* pool, schema::Type::Reader elementType, internal::ListBuilder builder);
  Builder(const SchemaPool* pool, internal::ListSchema schema, internal::ListBuilder builder);
  Builder(const SchemaPool* pool, schema::Type::Body::Which elementType, uint depth,
          schema::Node::Reader elementSchema, internal::ListBuilder builder)
      : pool(pool), elementType(elementType), depth(depth), elementSchema(elementSchema),
        builder(builder) {}

  void verifySchema(internal::ListSchema schema);

  template <typename T>
  friend struct internal::PointerHelpers;
  friend struct DynamicStruct;
  friend struct DynamicObject;
  friend class SchemaPool;
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
  inline Reader(Void voidValue = Void::VOID);
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
  inline ReaderFor<T> as() { return asImpl<T>::apply(*this); }
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

  template <typename T, Kind kind = kind<T>()> struct asImpl;
  // Implementation backing the as() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.
};

class DynamicValue::Builder {
public:
  inline Builder(Void voidValue = Void::VOID);
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
  inline BuilderFor<T> as() { return asImpl<T>::apply(*this); }
  // See DynamicValue::Reader::as().

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

  template <typename T, Kind kind = kind<T>()> struct asImpl;
  // Implementation backing the as() method.  Needs to be a struct to allow partial
  // specialization.  Has a method apply() which does the work.
};

// -------------------------------------------------------------------
// Inject the ability to use DynamicStruct for message roots and Dynamic{Struct,List} for
// generated Object accessors.

template <>
DynamicStruct::Reader MessageReader::getRoot<DynamicStruct>(
    const SchemaPool& pool, uint64_t typeId);
template <>
DynamicStruct::Builder MessageBuilder::initRoot<DynamicStruct>(
    const SchemaPool& pool, uint64_t typeId);
template <>
DynamicStruct::Builder MessageBuilder::getRoot<DynamicStruct>(
    const SchemaPool& pool, uint64_t typeId);

namespace internal {

template <>
struct PointerHelpers<DynamicStruct, Kind::UNKNOWN> {
  static DynamicStruct::Reader get(StructReader reader, WireReferenceCount index,
                                   const SchemaPool& pool, uint64_t typeId);
  static DynamicStruct::Builder get(StructBuilder builder, WireReferenceCount index,
                                    const SchemaPool& pool, uint64_t typeId);
  static void set(StructBuilder builder, WireReferenceCount index, DynamicStruct::Reader value);
  static DynamicStruct::Builder init(StructBuilder builder, WireReferenceCount index,
                                     const SchemaPool& pool, uint64_t typeId);
};

template <>
struct PointerHelpers<DynamicList, Kind::UNKNOWN> {
  static DynamicList::Reader get(StructReader reader, WireReferenceCount index,
                                 const SchemaPool& pool, schema::Type::Reader elementType);
  static DynamicList::Builder get(StructBuilder builder, WireReferenceCount index,
                                  const SchemaPool& pool, schema::Type::Reader elementType);
  static void set(StructBuilder builder, WireReferenceCount index, DynamicList::Reader value);
  static DynamicList::Builder init(StructBuilder builder, WireReferenceCount index,
                                   const SchemaPool& pool, schema::Type::Reader elementType,
                                   uint size);
};

}  // namespace internal

// =======================================================================================
// Inline implementation details.

template <typename T>
struct SchemaPool::ToDynamicImpl<T, Kind::STRUCT> {
  static inline DynamicStruct::Reader apply(const SchemaPool* pool, typename T::Reader value) {
    return DynamicStruct::Reader(pool, pool->getStruct(typeId<T>()), value._reader);
  }
  static inline DynamicStruct::Builder apply(const SchemaPool* pool, typename T::Builder value) {
    return DynamicStruct::Builder(pool, pool->getStruct(typeId<T>()), value._builder);
  }
};

template <typename T>
struct SchemaPool::ToDynamicImpl<T, Kind::LIST> {
  static inline DynamicList::Reader apply(const SchemaPool* pool, typename T::Reader value) {
    return DynamicList::Reader(pool, internal::ListSchemaFor<T>::schema, value.reader);
  }
  static inline DynamicList::Builder apply(const SchemaPool* pool, typename T::Builder value) {
    return DynamicList::Builder(pool, internal::ListSchemaFor<T>::schema, value.builder);
  }
};

template <typename T>
ReaderFor<DynamicTypeFor<FromReader<T>>> SchemaPool::toDynamic(T&& value) const {
  return ToDynamicImpl<FromReader<T>>::apply(this, value);
}
template <typename T>
BuilderFor<DynamicTypeFor<FromBuilder<T>>> SchemaPool::toDynamic(T&& value) const {
  return ToDynamicImpl<FromBuilder<T>>::apply(this, value);
}

#define CAPNPROTO_DECLARE_TYPE(name, discrim, typeName) \
inline DynamicValue::Reader::Reader(ReaderFor<typeName> name##Value) \
    : type(schema::Type::Body::discrim##_TYPE), name##Value(name##Value) {} \
inline DynamicValue::Builder::Builder(BuilderFor<typeName> name##Value) \
    : type(schema::Type::Body::discrim##_TYPE), name##Value(name##Value) {} \
template <> \
struct DynamicValue::Reader::asImpl<typeName> { \
  static ReaderFor<typeName> apply(Reader reader); \
}; \
template <> \
struct DynamicValue::Builder::asImpl<typeName> { \
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
struct DynamicValue::Reader::asImpl<Void> {
  static Void apply(Reader reader);
};
template <>
struct DynamicValue::Builder::asImpl<Void> {
  static Void apply(Builder builder);
};

template <typename T>
struct DynamicValue::Reader::asImpl<T, Kind::ENUM> {
  static T apply(Reader reader) {
    return reader.as<DynamicEnum>().as<T>();
  }
};
template <typename T>
struct DynamicValue::Builder::asImpl<T, Kind::ENUM> {
  static T apply(Builder builder) {
    return builder.as<DynamicEnum>().as<T>();
  }
};

template <typename T>
struct DynamicValue::Reader::asImpl<T, Kind::STRUCT> {
  static T apply(Reader reader) {
    return reader.as<DynamicStruct>().as<T>();
  }
};
template <typename T>
struct DynamicValue::Builder::asImpl<T, Kind::STRUCT> {
  static T apply(Builder builder) {
    return builder.as<DynamicStruct>().as<T>();
  }
};

template <typename T>
struct DynamicValue::Reader::asImpl<T, Kind::LIST> {
  static T apply(Reader reader) {
    return reader.as<DynamicList>().as<T>();
  }
};
template <typename T>
struct DynamicValue::Builder::asImpl<T, Kind::LIST> {
  static T apply(Builder builder) {
    return builder.as<DynamicList>().as<T>();
  }
};

// -------------------------------------------------------------------

template <typename T>
struct DynamicObject::Reader::asImpl<T, Kind::STRUCT> {
  static T apply(Reader reader) {
    return reader.asStruct(typeId<T>()).as<T>();
  }
};
template <typename T>
struct DynamicObject::Builder::asImpl<T, Kind::STRUCT> {
  static T apply(Builder builder) {
    return builder.asStruct(typeId<T>()).as<T>();
  }
};

template <typename T>
struct DynamicObject::Reader::asImpl<List<T>, Kind::LIST> {
  static T apply(Reader reader) {
    return reader.asList(internal::ListSchemaForElement<T>::schema).as<T>();
  }
};
template <typename T>
struct DynamicObject::Builder::asImpl<List<T>, Kind::LIST> {
  static T apply(Builder builder) {
    return builder.asList(internal::ListSchemaForElement<T>::schema).as<T>();
  }
};

// -------------------------------------------------------------------

template <typename T>
typename T::Reader DynamicStruct::Reader::as() {
  static_assert(kind<T>() == Kind::STRUCT,
                "DynamicStruct::Reader::as<T>() can only convert to struct types.");
  verifyTypeId(typeId<T>());
  return typename T::Reader(reader);
}
template <typename T>
typename T::Builder DynamicStruct::Builder::as() {
  static_assert(kind<T>() == Kind::STRUCT,
                "DynamicStruct::Builder::as<T>() can only convert to struct types.");
  verifyTypeId(typeId<T>());
  return typename T::Builder(builder);
}

inline DynamicValue::Reader DynamicStruct::Reader::getField(
    schema::StructNode::Field::Reader field) {
  return getFieldImpl(pool, reader, field);
}
inline DynamicValue::Builder DynamicStruct::Builder::getField(
    schema::StructNode::Field::Reader field) {
  return getFieldImpl(pool, builder, field);
}
inline void DynamicStruct::Builder::setField(
    schema::StructNode::Field::Reader field, DynamicValue::Reader value) {
  return setFieldImpl(pool, builder, field, value);
}
inline DynamicValue::Builder DynamicStruct::Builder::initField(
    schema::StructNode::Field::Reader field) {
  return initFieldImpl(pool, builder, field);
}
inline DynamicValue::Builder DynamicStruct::Builder::initField(
    schema::StructNode::Field::Reader field, uint size) {
  return initFieldImpl(pool, builder, field, size);
}

inline DynamicStruct::Reader DynamicStruct::Builder::asReader() {
  return DynamicStruct::Reader(pool, schema, builder.asReader());
}

// -------------------------------------------------------------------

template <typename T>
typename T::Reader DynamicList::Reader::as() {
  static_assert(kind<T>() == Kind::LIST,
                "DynamicStruct::Reader::as<T>() can only convert to list types.");
  verifySchema(internal::ListSchemaFor<T>::schema);
  return typename T::Reader(reader);
}
template <typename T>
typename T::Builder DynamicList::Builder::as() {
  static_assert(kind<T>() == Kind::LIST,
                "DynamicStruct::Builder::as<T>() can only convert to list types.");
  verifySchema(internal::ListSchemaFor<T>::schema);
  return typename T::Builder(builder);
}

}  // namespace capnproto

#endif  // CAPNPROTO_DYNAMIC_H_
