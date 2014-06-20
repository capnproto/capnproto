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

#ifndef CAPNP_SCHEMA_H_
#define CAPNP_SCHEMA_H_

#include <capnp/schema.capnp.h>

namespace capnp {

class Schema;
class StructSchema;
class EnumSchema;
class InterfaceSchema;
class ConstSchema;
class ListSchema;

template <typename T, Kind k = kind<T>()> struct SchemaType_ { typedef Schema Type; };
template <typename T> struct SchemaType_<T, Kind::PRIMITIVE> { typedef schema::Type::Which Type; };
template <typename T> struct SchemaType_<T, Kind::BLOB> { typedef schema::Type::Which Type; };
template <typename T> struct SchemaType_<T, Kind::ENUM> { typedef EnumSchema Type; };
template <typename T> struct SchemaType_<T, Kind::STRUCT> { typedef StructSchema Type; };
template <typename T> struct SchemaType_<T, Kind::INTERFACE> { typedef InterfaceSchema Type; };
template <typename T> struct SchemaType_<T, Kind::LIST> { typedef ListSchema Type; };

template <typename T>
using SchemaType = typename SchemaType_<T>::Type;
// SchemaType<T> is the type of T's schema, e.g. StructSchema if T is a struct.

namespace _ {  // private
extern const RawSchema NULL_SCHEMA;
extern const RawSchema NULL_STRUCT_SCHEMA;
extern const RawSchema NULL_ENUM_SCHEMA;
extern const RawSchema NULL_INTERFACE_SCHEMA;
extern const RawSchema NULL_CONST_SCHEMA;
// The schema types default to these null (empty) schemas in case of error, especially when
// exceptions are disabled.
}  // namespace _ (private)

class Schema {
  // Convenience wrapper around capnp::schema::Node.

public:
  inline Schema(): raw(&_::NULL_SCHEMA) {}

  template <typename T>
  static inline SchemaType<T> from() { return SchemaType<T>::template fromImpl<T>(); }
  // Get the Schema for a particular compiled-in type.

  schema::Node::Reader getProto() const;
  // Get the underlying Cap'n Proto representation of the schema node.  (Note that this accessor
  // has performance comparable to accessors of struct-typed fields on Reader classes.)

  kj::ArrayPtr<const word> asUncheckedMessage() const;
  // Get the encoded schema node content as a single message segment.  It is safe to read as an
  // unchecked message.

  Schema getDependency(uint64_t id) const;
  // Gets the Schema for one of this Schema's dependencies.  For example, if this Schema is for a
  // struct, you could look up the schema for one of its fields' types.  Throws an exception if this
  // schema doesn't actually depend on the given id.
  //
  // Note that not all type IDs found in the schema node are considered "dependencies" -- only the
  // ones that are needed to implement the dynamic API are.  That includes:
  // - Field types.
  // - Group types.
  // - scopeId for group nodes, but NOT otherwise.
  // - Method parameter and return types.
  //
  // The following are NOT considered dependencies:
  // - Nested nodes.
  // - scopeId for a non-group node.
  // - Annotations.
  //
  // To obtain schemas for those, you would need a SchemaLoader.

  StructSchema asStruct() const;
  EnumSchema asEnum() const;
  InterfaceSchema asInterface() const;
  ConstSchema asConst() const;
  // Cast the Schema to a specific type.  Throws an exception if the type doesn't match.  Use
  // getProto() to determine type, e.g. getProto().isStruct().

  inline bool operator==(const Schema& other) const { return raw == other.raw; }
  inline bool operator!=(const Schema& other) const { return raw != other.raw; }
  // Determine whether two Schemas are wrapping the exact same underlying data, by identity.  If
  // you want to check if two Schemas represent the same type (but possibly different versions of
  // it), compare their IDs instead.

  template <typename T>
  void requireUsableAs() const;
  // Throws an exception if a value with this Schema cannot safely be cast to a native value of
  // the given type.  This passes if either:
  // - *this == from<T>()
  // - This schema was loaded with SchemaLoader, the type ID matches typeId<T>(), and
  //   loadCompiledTypeAndDependencies<T>() was called on the SchemaLoader.

  kj::StringPtr getShortDisplayName() const;
  // Get the short version of the node's display name.

private:
  const _::RawSchema* raw;

  inline explicit Schema(const _::RawSchema* raw): raw(raw) {
    KJ_IREQUIRE(raw->lazyInitializer == nullptr,
        "Must call ensureInitialized() on RawSchema before constructing Schema.");
  }

  template <typename T> static inline Schema fromImpl() {
    return Schema(&_::rawSchema<T>());
  }

  void requireUsableAs(const _::RawSchema* expected) const;

  uint32_t getSchemaOffset(const schema::Value::Reader& value) const;

  friend class StructSchema;
  friend class EnumSchema;
  friend class InterfaceSchema;
  friend class ConstSchema;
  friend class ListSchema;
  friend class SchemaLoader;
};

// -------------------------------------------------------------------

class StructSchema: public Schema {
public:
  inline StructSchema(): Schema(&_::NULL_STRUCT_SCHEMA) {}

  class Field;
  class FieldList;
  class FieldSubset;

  FieldList getFields() const;
  // List top-level fields of this struct.  This list will contain top-level groups (including
  // named unions) but not the members of those groups.  The list does, however, contain the
  // members of the unnamed union, if there is one.

  FieldSubset getUnionFields() const;
  // If the field contains an unnamed union, get a list of fields in the union, ordered by
  // ordinal.  Since discriminant values are assigned sequentially by ordinal, you may index this
  // list by discriminant value.

  FieldSubset getNonUnionFields() const;
  // Get the fields of this struct which are not in an unnamed union, ordered by ordinal.

  kj::Maybe<Field> findFieldByName(kj::StringPtr name) const;
  // Find the field with the given name, or return null if there is no such field.  If the struct
  // contains an unnamed union, then this will find fields of that union in addition to fields
  // of the outer struct, since they exist in the same namespace.  It will not, however, find
  // members of groups (including named unions) -- you must first look up the group itself,
  // then dig into its type.

  Field getFieldByName(kj::StringPtr name) const;
  // Like findFieldByName() but throws an exception on failure.

  kj::Maybe<Field> getFieldByDiscriminant(uint16_t discriminant) const;
  // Finds the field whose `discriminantValue` is equal to the given value, or returns null if
  // there is no such field.  (If the schema does not represent a union or a struct containing
  // an unnamed union, then this always returns null.)

private:
  StructSchema(const _::RawSchema* raw): Schema(raw) {}
  template <typename T> static inline StructSchema fromImpl() {
    return StructSchema(&_::rawSchema<T>());
  }
  friend class Schema;
  friend kj::StringTree _::structString(
      _::StructReader reader, const _::RawSchema& schema);
};

class StructSchema::Field {
public:
  Field() = default;

  inline schema::Field::Reader getProto() const { return proto; }
  inline StructSchema getContainingStruct() const { return parent; }

  inline uint getIndex() const { return index; }
  // Get the index of this field within the containing struct or union.

  uint32_t getDefaultValueSchemaOffset() const;
  // For struct, list, and object fields, returns the offset, in words, within the first segment of
  // the struct's schema, where this field's default value pointer is located.  The schema is
  // always stored as a single-segment unchecked message, which in turn means that the default
  // value pointer itself can be treated as the root of an unchecked message -- if you know where
  // to find it, which is what this method helps you with.
  //
  // For blobs, returns the offset of the begging of the blob's content within the first segment of
  // the struct's schema.
  //
  // This is primarily useful for code generators.  The C++ code generator, for example, embeds
  // the entire schema as a raw word array within the generated code.  Of course, to implement
  // field accessors, it needs access to those fields' default values.  Embedding separate copies
  // of those default values would be redundant since they are already included in the schema, but
  // seeking through the schema at runtime to find the default values would be ugly.  Instead,
  // the code generator can use getDefaultValueSchemaOffset() to find the offset of the default
  // value within the schema, and can simply apply that offset at runtime.
  //
  // If the above does not make sense, you probably don't need this method.

  inline bool operator==(const Field& other) const;
  inline bool operator!=(const Field& other) const { return !(*this == other); }

private:
  StructSchema parent;
  uint index;
  schema::Field::Reader proto;

  inline Field(StructSchema parent, uint index, schema::Field::Reader proto)
      : parent(parent), index(index), proto(proto) {}

  friend class StructSchema;
};

class StructSchema::FieldList {
public:
  FieldList() = default;  // empty list

  inline uint size() const { return list.size(); }
  inline Field operator[](uint index) const { return Field(parent, index, list[index]); }

  typedef _::IndexingIterator<const FieldList, Field> Iterator;
  inline Iterator begin() const { return Iterator(this, 0); }
  inline Iterator end() const { return Iterator(this, size()); }

private:
  StructSchema parent;
  List<schema::Field>::Reader list;

  inline FieldList(StructSchema parent, List<schema::Field>::Reader list)
      : parent(parent), list(list) {}

  friend class StructSchema;
};

class StructSchema::FieldSubset {
public:
  FieldSubset() = default;  // empty list

  inline uint size() const { return size_; }
  inline Field operator[](uint index) const {
    return Field(parent, indices[index], list[indices[index]]);
  }

  typedef _::IndexingIterator<const FieldSubset, Field> Iterator;
  inline Iterator begin() const { return Iterator(this, 0); }
  inline Iterator end() const { return Iterator(this, size()); }

private:
  StructSchema parent;
  List<schema::Field>::Reader list;
  const uint16_t* indices;
  uint size_;

  inline FieldSubset(StructSchema parent, List<schema::Field>::Reader list,
                     const uint16_t* indices, uint size)
      : parent(parent), list(list), indices(indices), size_(size) {}

  friend class StructSchema;
};

// -------------------------------------------------------------------

class EnumSchema: public Schema {
public:
  inline EnumSchema(): Schema(&_::NULL_ENUM_SCHEMA) {}

  class Enumerant;
  class EnumerantList;

  EnumerantList getEnumerants() const;

  kj::Maybe<Enumerant> findEnumerantByName(kj::StringPtr name) const;

  Enumerant getEnumerantByName(kj::StringPtr name) const;
  // Like findEnumerantByName() but throws an exception on failure.

private:
  EnumSchema(const _::RawSchema* raw): Schema(raw) {}
  template <typename T> static inline EnumSchema fromImpl() {
    return EnumSchema(&_::rawSchema<T>());
  }
  friend class Schema;
};

class EnumSchema::Enumerant {
public:
  Enumerant() = default;

  inline schema::Enumerant::Reader getProto() const { return proto; }
  inline EnumSchema getContainingEnum() const { return parent; }

  inline uint16_t getOrdinal() const { return ordinal; }
  inline uint getIndex() const { return ordinal; }

  inline bool operator==(const Enumerant& other) const;
  inline bool operator!=(const Enumerant& other) const { return !(*this == other); }

private:
  EnumSchema parent;
  uint16_t ordinal;
  schema::Enumerant::Reader proto;

  inline Enumerant(EnumSchema parent, uint16_t ordinal, schema::Enumerant::Reader proto)
      : parent(parent), ordinal(ordinal), proto(proto) {}

  friend class EnumSchema;
};

class EnumSchema::EnumerantList {
public:
  EnumerantList() = default;  // empty list

  inline uint size() const { return list.size(); }
  inline Enumerant operator[](uint index) const { return Enumerant(parent, index, list[index]); }

  typedef _::IndexingIterator<const EnumerantList, Enumerant> Iterator;
  inline Iterator begin() const { return Iterator(this, 0); }
  inline Iterator end() const { return Iterator(this, size()); }

private:
  EnumSchema parent;
  List<schema::Enumerant>::Reader list;

  inline EnumerantList(EnumSchema parent, List<schema::Enumerant>::Reader list)
      : parent(parent), list(list) {}

  friend class EnumSchema;
};

// -------------------------------------------------------------------

class InterfaceSchema: public Schema {
public:
  inline InterfaceSchema(): Schema(&_::NULL_INTERFACE_SCHEMA) {}

  class Method;
  class MethodList;

  MethodList getMethods() const;

  kj::Maybe<Method> findMethodByName(kj::StringPtr name) const;

  Method getMethodByName(kj::StringPtr name) const;
  // Like findMethodByName() but throws an exception on failure.

  bool extends(InterfaceSchema other) const;
  // Returns true if `other` is a superclass of this interface (including if `other == *this`).

  kj::Maybe<InterfaceSchema> findSuperclass(uint64_t typeId) const;
  // Find the superclass of this interface with the given type ID.  Returns null if the interface
  // extends no such type.

private:
  InterfaceSchema(const _::RawSchema* raw): Schema(raw) {}
  template <typename T> static inline InterfaceSchema fromImpl() {
    return InterfaceSchema(&_::rawSchema<T>());
  }
  friend class Schema;

  kj::Maybe<Method> findMethodByName(kj::StringPtr name, uint& counter) const;
  bool extends(InterfaceSchema other, uint& counter) const;
  kj::Maybe<InterfaceSchema> findSuperclass(uint64_t typeId, uint& counter) const;
  // We protect against malicious schemas with large or cyclic hierarchies by cutting off the
  // search when the counter reaches a threshold.
};

class InterfaceSchema::Method {
public:
  Method() = default;

  inline schema::Method::Reader getProto() const { return proto; }
  inline InterfaceSchema getContainingInterface() const { return parent; }

  inline uint16_t getOrdinal() const { return ordinal; }
  inline uint getIndex() const { return ordinal; }

  inline bool operator==(const Method& other) const;
  inline bool operator!=(const Method& other) const { return !(*this == other); }

private:
  InterfaceSchema parent;
  uint16_t ordinal;
  schema::Method::Reader proto;

  inline Method(InterfaceSchema parent, uint16_t ordinal,
                schema::Method::Reader proto)
      : parent(parent), ordinal(ordinal), proto(proto) {}

  friend class InterfaceSchema;
};

class InterfaceSchema::MethodList {
public:
  MethodList() = default;  // empty list

  inline uint size() const { return list.size(); }
  inline Method operator[](uint index) const { return Method(parent, index, list[index]); }

  typedef _::IndexingIterator<const MethodList, Method> Iterator;
  inline Iterator begin() const { return Iterator(this, 0); }
  inline Iterator end() const { return Iterator(this, size()); }

private:
  InterfaceSchema parent;
  List<schema::Method>::Reader list;

  inline MethodList(InterfaceSchema parent, List<schema::Method>::Reader list)
      : parent(parent), list(list) {}

  friend class InterfaceSchema;
};

// -------------------------------------------------------------------

class ConstSchema: public Schema {
  // Represents a constant declaration.
  //
  // `ConstSchema` can be implicitly cast to DynamicValue to read its value.

public:
  inline ConstSchema(): Schema(&_::NULL_CONST_SCHEMA) {}

  template <typename T>
  ReaderFor<T> as() const;
  // Read the constant's value.  This is a convenience method equivalent to casting the ConstSchema
  // to a DynamicValue and then calling its `as<T>()` method.  For dependency reasons, this method
  // is defined in <capnp/dynamic.h>, which you must #include explicitly.

  uint32_t getValueSchemaOffset() const;
  // Much like StructSchema::Field::getDefaultValueSchemaOffset(), if the constant has pointer
  // type, this gets the offset from the beginning of the constant's schema node to a pointer
  // representing the constant value.

private:
  ConstSchema(const _::RawSchema* raw): Schema(raw) {}
  friend class Schema;
};

// -------------------------------------------------------------------

class ListSchema {
  // ListSchema is a little different because list types are not described by schema nodes.  So,
  // ListSchema doesn't subclass Schema.

public:
  ListSchema() = default;

  static ListSchema of(schema::Type::Which primitiveType);
  static ListSchema of(StructSchema elementType);
  static ListSchema of(EnumSchema elementType);
  static ListSchema of(InterfaceSchema elementType);
  static ListSchema of(ListSchema elementType);
  // Construct the schema for a list of the given type.

  static ListSchema of(schema::Type::Reader elementType, Schema context);
  // Construct from an element type schema.  Requires a context which can handle getDependency()
  // requests for any type ID found in the schema.

  inline schema::Type::Which whichElementType() const;
  // Get the element type's "which()".  ListSchema does not actually store a schema::Type::Reader
  // describing the element type, but if it did, this would be equivalent to calling
  // .getBody().which() on that type.

  StructSchema getStructElementType() const;
  EnumSchema getEnumElementType() const;
  InterfaceSchema getInterfaceElementType() const;
  ListSchema getListElementType() const;
  // Get the schema for complex element types.  Each of these throws an exception if the element
  // type is not of the requested kind.

  inline bool operator==(const ListSchema& other) const;
  inline bool operator!=(const ListSchema& other) const { return !(*this == other); }

  template <typename T>
  void requireUsableAs() const;

private:
  schema::Type::Which elementType;
  uint8_t nestingDepth;  // 0 for T, 1 for List(T), 2 for List(List(T)), ...
  Schema elementSchema;  // if elementType is struct, enum, interface...

  inline ListSchema(schema::Type::Which elementType)
      : elementType(elementType), nestingDepth(0) {}
  inline ListSchema(schema::Type::Which elementType, Schema elementSchema)
      : elementType(elementType), nestingDepth(0), elementSchema(elementSchema) {}
  inline ListSchema(schema::Type::Which elementType, uint8_t nestingDepth,
                    Schema elementSchema)
      : elementType(elementType), nestingDepth(nestingDepth), elementSchema(elementSchema) {}

  template <typename T>
  struct FromImpl;
  template <typename T> static inline ListSchema fromImpl() {
    return FromImpl<T>::get();
  }

  void requireUsableAs(ListSchema expected) const;

  friend class Schema;
};

// =======================================================================================
// inline implementation

template <> inline schema::Type::Which Schema::from<Void>() { return schema::Type::VOID; }
template <> inline schema::Type::Which Schema::from<bool>() { return schema::Type::BOOL; }
template <> inline schema::Type::Which Schema::from<int8_t>() { return schema::Type::INT8; }
template <> inline schema::Type::Which Schema::from<int16_t>() { return schema::Type::INT16; }
template <> inline schema::Type::Which Schema::from<int32_t>() { return schema::Type::INT32; }
template <> inline schema::Type::Which Schema::from<int64_t>() { return schema::Type::INT64; }
template <> inline schema::Type::Which Schema::from<uint8_t>() { return schema::Type::UINT8; }
template <> inline schema::Type::Which Schema::from<uint16_t>() { return schema::Type::UINT16; }
template <> inline schema::Type::Which Schema::from<uint32_t>() { return schema::Type::UINT32; }
template <> inline schema::Type::Which Schema::from<uint64_t>() { return schema::Type::UINT64; }
template <> inline schema::Type::Which Schema::from<float>() { return schema::Type::FLOAT32; }
template <> inline schema::Type::Which Schema::from<double>() { return schema::Type::FLOAT64; }
template <> inline schema::Type::Which Schema::from<Text>() { return schema::Type::TEXT; }
template <> inline schema::Type::Which Schema::from<Data>() { return schema::Type::DATA; }

template <typename T>
inline void Schema::requireUsableAs() const {
  requireUsableAs(&_::rawSchema<T>());
}

inline bool StructSchema::Field::operator==(const Field& other) const {
  return parent == other.parent && index == other.index;
}
inline bool EnumSchema::Enumerant::operator==(const Enumerant& other) const {
  return parent == other.parent && ordinal == other.ordinal;
}
inline bool InterfaceSchema::Method::operator==(const Method& other) const {
  return parent == other.parent && ordinal == other.ordinal;
}

inline ListSchema ListSchema::of(StructSchema elementType) {
  return ListSchema(schema::Type::STRUCT, 0, elementType);
}
inline ListSchema ListSchema::of(EnumSchema elementType) {
  return ListSchema(schema::Type::ENUM, 0, elementType);
}
inline ListSchema ListSchema::of(InterfaceSchema elementType) {
  return ListSchema(schema::Type::INTERFACE, 0, elementType);
}
inline ListSchema ListSchema::of(ListSchema elementType) {
  return ListSchema(elementType.elementType, elementType.nestingDepth + 1,
                    elementType.elementSchema);
}

inline schema::Type::Which ListSchema::whichElementType() const {
  return nestingDepth == 0 ? elementType : schema::Type::LIST;
}

inline bool ListSchema::operator==(const ListSchema& other) const {
  return elementType == other.elementType && nestingDepth == other.nestingDepth &&
      elementSchema == other.elementSchema;
}

template <typename T>
inline void ListSchema::requireUsableAs() const {
  static_assert(kind<T>() == Kind::LIST,
                "ListSchema::requireUsableAs<T>() requires T is a list type.");
  requireUsableAs(Schema::from<T>());
}

template <typename T>
struct ListSchema::FromImpl<List<T>> {
  static inline ListSchema get() { return of(Schema::from<T>()); }
};

}  // namespace capnp

#endif  // CAPNP_SCHEMA_H_
