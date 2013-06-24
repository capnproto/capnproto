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

#ifndef CAPNP_SCHEMA_H_
#define CAPNP_SCHEMA_H_

#include <capnp/schema.capnp.h>

namespace capnp {

class Schema;
class StructSchema;
class EnumSchema;
class InterfaceSchema;
class ListSchema;

template <typename T, Kind k = kind<T>()> struct SchemaType_ { typedef Schema Type; };
template <typename T> struct SchemaType_<T, Kind::PRIMITIVE> { typedef schema::Type::Body::Which Type; };
template <typename T> struct SchemaType_<T, Kind::BLOB> { typedef schema::Type::Body::Which Type; };
template <typename T> struct SchemaType_<T, Kind::ENUM> { typedef EnumSchema Type; };
template <typename T> struct SchemaType_<T, Kind::STRUCT> { typedef StructSchema Type; };
template <typename T> struct SchemaType_<T, Kind::INTERFACE> { typedef InterfaceSchema Type; };
template <typename T> struct SchemaType_<T, Kind::LIST> { typedef ListSchema Type; };

template <typename T>
using SchemaType = typename SchemaType_<T>::Type;
// SchemaType<T> is the type of T's schema, e.g. StructSchema if T is a struct.

class Schema {
  // Convenience wrapper around capnp::schema::Node.

public:
  inline Schema(): raw(nullptr) {}

  template <typename T>
  static inline SchemaType<T> from() { return SchemaType<T>::template fromImpl<T>(); }
  // Get the Schema for a particular compiled-in type.

  schema::Node::Reader getProto() const;

  Schema getDependency(uint64_t id) const;
  // Gets the Schema for one of this Schema's dependencies.  For example, if this Schema is for a
  // struct, you could look up the schema for one of its fields' types.  Throws an exception if this
  // schema doesn't actually depend on the given id.  Note that annotation declarations are not
  // considered dependencies for this purpose.

  StructSchema asStruct() const;
  EnumSchema asEnum() const;
  InterfaceSchema asInterface() const;
  // Cast the Schema to a specific type.  Throws an exception if the type doesn't match.

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

private:
  const _::RawSchema* raw;

  inline explicit Schema(const _::RawSchema* raw): raw(raw) {}

  template <typename T> static inline Schema fromImpl() {
    return Schema(&_::rawSchema<T>());
  }

  void requireUsableAs(const _::RawSchema* expected) const;

  friend class StructSchema;
  friend class EnumSchema;
  friend class InterfaceSchema;
  friend class ListSchema;
  friend class SchemaLoader;
};

// -------------------------------------------------------------------

class StructSchema: public Schema {
public:
  StructSchema() = default;

  class Member;
  class Union;
  class MemberList;

  MemberList getMembers() const;

  kj::Maybe<Member> findMemberByName(kj::StringPtr name) const;

  Member getMemberByName(kj::StringPtr name) const;
  // Like findMemberByName() but throws an exception on failure.

private:
  StructSchema(const _::RawSchema* raw): Schema(raw) {}
  template <typename T> static inline StructSchema fromImpl() {
    return StructSchema(&_::rawSchema<T>());
  }
  friend class Schema;
  friend kj::String _::structString(
      _::StructReader reader, const _::RawSchema& schema);
  friend kj::String _::unionString(
      _::StructReader reader, const _::RawSchema& schema, uint memberIndex);
};

class StructSchema::Member {
public:
  Member() = default;

  inline schema::StructNode::Member::Reader getProto() const { return proto; }
  inline StructSchema getContainingStruct() const { return parent; }

  inline uint getIndex() const { return index; }
  // Get the index of this member within the containing struct or union.

  kj::Maybe<Union> getContainingUnion() const;
  // If this a member of a union, gets the containing union schema.

  Union asUnion() const;
  // Cast the member to a Union.  Throws an exception if not a union.

  inline bool operator==(const Member& other) const;
  inline bool operator!=(const Member& other) const { return !(*this == other); }

private:
  StructSchema parent;
  uint unionIndex;  // 0 = none, >0 = actual union index - 1
  uint index;
  mutable schema::StructNode::Member::Reader proto;
  // TODO(soon): Make all reader methods const and then remove this ugly use of "mutable".

  inline Member(StructSchema parent, uint unionIndex, uint index,
                schema::StructNode::Member::Reader proto)
      : parent(parent), unionIndex(unionIndex), index(index), proto(proto) {}

  friend class StructSchema;
};

class StructSchema::Union: public Member {
public:
  Union() = default;

  MemberList getMembers() const;

  kj::Maybe<Member> findMemberByName(kj::StringPtr name) const;

  Member getMemberByName(kj::StringPtr name) const;
  // Like findMemberByName() but throws an exception on failure.

private:
  inline Union(const Member& base): Member(base) {}

  friend class StructSchema;
};

class StructSchema::MemberList {
public:
  inline uint size() const { return list.size(); }
  inline Member operator[](uint index) const { return Member(parent, unionIndex, index, list[index]); }

  typedef _::IndexingIterator<const MemberList, Member> Iterator;
  inline Iterator begin() const { return Iterator(this, 0); }
  inline Iterator end() const { return Iterator(this, size()); }

private:
  StructSchema parent;
  uint unionIndex;
  List<schema::StructNode::Member>::Reader list;

  inline MemberList(StructSchema parent, uint unionIndex,
                    List<schema::StructNode::Member>::Reader list)
      : parent(parent), unionIndex(unionIndex), list(list) {}

  friend class StructSchema;
};

// -------------------------------------------------------------------

class EnumSchema: public Schema {
public:
  EnumSchema() = default;

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

  inline schema::EnumNode::Enumerant::Reader getProto() const { return proto; }
  inline EnumSchema getContainingEnum() { return parent; }

  inline uint16_t getOrdinal() { return ordinal; }

  inline bool operator==(const Enumerant& other) const;
  inline bool operator!=(const Enumerant& other) const { return !(*this == other); }

private:
  EnumSchema parent;
  uint16_t ordinal;
  mutable schema::EnumNode::Enumerant::Reader proto;
  // TODO(soon): Make all reader methods const and then remove this ugly use of "mutable".

  inline Enumerant(EnumSchema parent, uint16_t ordinal, schema::EnumNode::Enumerant::Reader proto)
      : parent(parent), ordinal(ordinal), proto(proto) {}

  friend class EnumSchema;
};

class EnumSchema::EnumerantList {
public:
  inline uint size() const { return list.size(); }
  inline Enumerant operator[](uint index) const { return Enumerant(parent, index, list[index]); }

  typedef _::IndexingIterator<const EnumerantList, Enumerant> Iterator;
  inline Iterator begin() const { return Iterator(this, 0); }
  inline Iterator end() const { return Iterator(this, size()); }

private:
  EnumSchema parent;
  List<schema::EnumNode::Enumerant>::Reader list;

  inline EnumerantList(EnumSchema parent, List<schema::EnumNode::Enumerant>::Reader list)
      : parent(parent), list(list) {}

  friend class EnumSchema;
};

// -------------------------------------------------------------------

class InterfaceSchema: public Schema {
public:
  InterfaceSchema() = default;

  class Method;
  class MethodList;

  MethodList getMethods() const;

  kj::Maybe<Method> findMethodByName(kj::StringPtr name) const;

  Method getMethodByName(kj::StringPtr name) const;
  // Like findMethodByName() but throws an exception on failure.

private:
  InterfaceSchema(const _::RawSchema* raw): Schema(raw) {}
  template <typename T> static inline InterfaceSchema fromImpl() {
    return InterfaceSchema(&_::rawSchema<T>());
  }
  friend class Schema;
};

class InterfaceSchema::Method {
public:
  Method() = default;

  inline schema::InterfaceNode::Method::Reader getProto() const { return proto; }
  inline InterfaceSchema getContainingInterface() { return parent; }

  inline uint16_t getOrdinal() { return ordinal; }

  inline bool operator==(const Method& other) const;
  inline bool operator!=(const Method& other) const { return !(*this == other); }

private:
  InterfaceSchema parent;
  uint16_t ordinal;
  mutable schema::InterfaceNode::Method::Reader proto;
  // TODO(soon): Make all reader methods const and then remove this ugly use of "mutable".

  inline Method(InterfaceSchema parent, uint16_t ordinal,
                schema::InterfaceNode::Method::Reader proto)
      : parent(parent), ordinal(ordinal), proto(proto) {}

  friend class InterfaceSchema;
};

class InterfaceSchema::MethodList {
public:
  inline uint size() const { return list.size(); }
  inline Method operator[](uint index) const { return Method(parent, index, list[index]); }

  typedef _::IndexingIterator<const MethodList, Method> Iterator;
  inline Iterator begin() const { return Iterator(this, 0); }
  inline Iterator end() const { return Iterator(this, size()); }

private:
  InterfaceSchema parent;
  List<schema::InterfaceNode::Method>::Reader list;

  inline MethodList(InterfaceSchema parent, List<schema::InterfaceNode::Method>::Reader list)
      : parent(parent), list(list) {}

  friend class InterfaceSchema;
};

// -------------------------------------------------------------------

class ListSchema {
  // ListSchema is a little different because list types are not described by schema nodes.  So,
  // ListSchema doesn't subclass Schema.

public:
  ListSchema() = default;

  static ListSchema of(schema::Type::Body::Which primitiveType);
  static ListSchema of(StructSchema elementType);
  static ListSchema of(EnumSchema elementType);
  static ListSchema of(InterfaceSchema elementType);
  static ListSchema of(ListSchema elementType);
  // Construct the schema for a list of the given type.

  static ListSchema of(schema::Type::Reader elementType, Schema context);
  // Construct from an element type schema.  Requires a context which can handle getDependency()
  // requests for any type ID found in the schema.

  inline schema::Type::Body::Which whichElementType() const;
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
  schema::Type::Body::Which elementType;
  uint8_t nestingDepth;  // 0 for T, 1 for List(T), 2 for List(List(T)), ...
  Schema elementSchema;  // if elementType is struct, enum, interface...

  inline ListSchema(schema::Type::Body::Which elementType)
      : elementType(elementType), nestingDepth(0) {}
  inline ListSchema(schema::Type::Body::Which elementType, Schema elementSchema)
      : elementType(elementType), nestingDepth(0), elementSchema(elementSchema) {}
  inline ListSchema(schema::Type::Body::Which elementType, uint8_t nestingDepth,
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

template <> inline schema::Type::Body::Which Schema::from<Void>() { return schema::Type::Body::VOID_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<bool>() { return schema::Type::Body::BOOL_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<int8_t>() { return schema::Type::Body::INT8_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<int16_t>() { return schema::Type::Body::INT16_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<int32_t>() { return schema::Type::Body::INT32_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<int64_t>() { return schema::Type::Body::INT64_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<uint8_t>() { return schema::Type::Body::UINT8_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<uint16_t>() { return schema::Type::Body::UINT16_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<uint32_t>() { return schema::Type::Body::UINT32_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<uint64_t>() { return schema::Type::Body::UINT64_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<float>() { return schema::Type::Body::FLOAT32_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<double>() { return schema::Type::Body::FLOAT64_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<Text>() { return schema::Type::Body::TEXT_TYPE; }
template <> inline schema::Type::Body::Which Schema::from<Data>() { return schema::Type::Body::DATA_TYPE; }

template <typename T>
inline void Schema::requireUsableAs() const {
  requireUsableAs(&_::rawSchema<T>());
}

inline bool StructSchema::Member::operator==(const Member& other) const {
  return parent == other.parent && unionIndex == other.unionIndex && index == other.index;
}
inline bool EnumSchema::Enumerant::operator==(const Enumerant& other) const {
  return parent == other.parent && ordinal == other.ordinal;
}
inline bool InterfaceSchema::Method::operator==(const Method& other) const {
  return parent == other.parent && ordinal == other.ordinal;
}

inline ListSchema ListSchema::of(StructSchema elementType) {
  return ListSchema(schema::Type::Body::STRUCT_TYPE, 0, elementType);
}
inline ListSchema ListSchema::of(EnumSchema elementType) {
  return ListSchema(schema::Type::Body::ENUM_TYPE, 0, elementType);
}
inline ListSchema ListSchema::of(InterfaceSchema elementType) {
  return ListSchema(schema::Type::Body::INTERFACE_TYPE, 0, elementType);
}
inline ListSchema ListSchema::of(ListSchema elementType) {
  return ListSchema(elementType.elementType, elementType.nestingDepth + 1,
                    elementType.elementSchema);
}

inline schema::Type::Body::Which ListSchema::whichElementType() const {
  return nestingDepth == 0 ? elementType : schema::Type::Body::LIST_TYPE;
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
