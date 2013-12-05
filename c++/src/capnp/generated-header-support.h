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

// This file is included form all generated headers.

#ifndef CAPNP_GENERATED_HEADER_SUPPORT_H_
#define CAPNP_GENERATED_HEADER_SUPPORT_H_

#include "layout.h"
#include "list.h"
#include "orphan.h"
#include "pointer-helpers.h"
#include "any.h"
#include <kj/string.h>
#include <kj/string-tree.h>

namespace capnp {

class MessageBuilder;  // So that it can be declared a friend.

template <typename T, Kind k = kind<T>()>
struct ToDynamic_;   // Defined in dynamic.h, needs to be declared as everyone's friend.

struct DynamicStruct;  // So that it can be declared a friend.

namespace _ {  // private

struct RawSchema {
  // The generated code defines a constant RawSchema for every compiled declaration.
  //
  // This is an internal structure which could change in the future.

  uint64_t id;

  const word* encodedNode;
  // Encoded SchemaNode, readable via readMessageUnchecked<schema::Node>(encodedNode).

  uint32_t encodedSize;
  // Size of encodedNode, in words.

  const RawSchema* const* dependencies;
  // Pointers to other types on which this one depends, sorted by ID.  The schemas in this table
  // may be uninitialized -- you must call ensureInitialized() on the one you wish to use before
  // using it.
  //
  // TODO(someday):  Make this a hashtable.

  const uint16_t* membersByName;
  // Indexes of members sorted by name.  Used to implement name lookup.
  // TODO(someday):  Make this a hashtable.

  uint32_t dependencyCount;
  uint32_t memberCount;
  // Sizes of above tables.

  const uint16_t* membersByDiscriminant;
  // List of all member indexes ordered by discriminant value.  Those which don't have a
  // discriminant value are listed at the end, in order by ordinal.

  const RawSchema* canCastTo;
  // Points to the RawSchema of a compiled-in type to which it is safe to cast any DynamicValue
  // with this schema.  This is null for all compiled-in types; it is only set by SchemaLoader on
  // dynamically-loaded types.

  class Initializer {
  public:
    virtual void init(const RawSchema* schema) const = 0;
  };

  const Initializer* lazyInitializer;
  // Lazy initializer, invoked by ensureInitialized().

  inline void ensureInitialized() const {
    // Lazy initialization support.  Invoke to ensure that initialization has taken place.  This
    // is required in particular when traversing the dependency list.  RawSchemas for compiled-in
    // types are always initialized; only dynamically-loaded schemas may be lazy.

    const Initializer* i = __atomic_load_n(&lazyInitializer, __ATOMIC_ACQUIRE);
    if (i != nullptr) i->init(this);
  }
};

template <typename T>
struct RawSchema_;

template <typename T>
inline const RawSchema& rawSchema() {
  return RawSchema_<T>::get();
}

template <typename T> struct TypeId_;

extern const RawSchema NULL_INTERFACE_SCHEMA;  // defined in schema.c++
template <> struct TypeId_<Capability> { static constexpr uint64_t typeId = 0x03; };
template <> struct RawSchema_<Capability> {
  static inline const RawSchema& get() { return NULL_INTERFACE_SCHEMA; }
};

template <typename T>
struct UnionMemberIndex_;
template <typename T>
inline uint unionMemberIndex() { return UnionMemberIndex_<T>::value; }

template <typename T>
struct UnionParentType_;
template <typename T>
using UnionParentType = typename UnionParentType_<T>::Type;

kj::StringTree structString(StructReader reader, const RawSchema& schema);
// Declared here so that we can declare inline stringify methods on generated types.
// Defined in stringify.c++, which depends on dynamic.c++, which is allowed not to be linked in.

template <typename T>
inline kj::StringTree structString(StructReader reader) {
  return structString(reader, rawSchema<T>());
}

// TODO(cleanup):  Unify ConstStruct and ConstList.
template <typename T>
class ConstStruct {
public:
  ConstStruct() = delete;
  KJ_DISALLOW_COPY(ConstStruct);
  inline explicit constexpr ConstStruct(const word* ptr): ptr(ptr) {}

  inline typename T::Reader get() const {
    return AnyPointer::Reader(PointerReader::getRootUnchecked(ptr)).getAs<T>();
  }

  inline operator typename T::Reader() const { return get(); }
  inline typename T::Reader operator*() const { return get(); }
  inline TemporaryPointer<typename T::Reader> operator->() const { return get(); }

private:
  const word* ptr;
};

template <typename T>
class ConstList {
public:
  ConstList() = delete;
  KJ_DISALLOW_COPY(ConstList);
  inline explicit constexpr ConstList(const word* ptr): ptr(ptr) {}

  inline typename List<T>::Reader get() const {
    return AnyPointer::Reader(PointerReader::getRootUnchecked(ptr)).getAs<List<T>>();
  }

  inline operator typename List<T>::Reader() const { return get(); }
  inline typename List<T>::Reader operator*() const { return get(); }
  inline TemporaryPointer<typename List<T>::Reader> operator->() const { return get(); }

private:
  const word* ptr;
};

template <size_t size>
class ConstText {
public:
  ConstText() = delete;
  KJ_DISALLOW_COPY(ConstText);
  inline explicit constexpr ConstText(const word* ptr): ptr(ptr) {}

  inline Text::Reader get() const {
    return Text::Reader(reinterpret_cast<const char*>(ptr), size);
  }

  inline operator Text::Reader() const { return get(); }
  inline Text::Reader operator*() const { return get(); }
  inline TemporaryPointer<Text::Reader> operator->() const { return get(); }

private:
  const word* ptr;
};

template <size_t size>
class ConstData {
public:
  ConstData() = delete;
  KJ_DISALLOW_COPY(ConstData);
  inline explicit constexpr ConstData(const word* ptr): ptr(ptr) {}

  inline Data::Reader get() const {
    return Data::Reader(reinterpret_cast<const byte*>(ptr), size);
  }

  inline operator Data::Reader() const { return get(); }
  inline Data::Reader operator*() const { return get(); }
  inline TemporaryPointer<Data::Reader> operator->() const { return get(); }

private:
  const word* ptr;
};

}  // namespace _ (private)

template <typename T>
inline constexpr uint64_t typeId() { return _::TypeId_<T>::typeId; }
// typeId<MyType>() returns the type ID as defined in the schema.  Works with structs, enums, and
// interfaces.

template <typename T>
inline constexpr uint sizeInWords() {
  // Return the size, in words, of a Struct type, if allocated free-standing (not in a list).
  // May be useful for pre-computing space needed in order to precisely allocate messages.

  return (WordCount32(_::structSize<T>().data) +
      _::structSize<T>().pointers * WORDS_PER_POINTER) / WORDS;
}

}  // namespace capnp

#define CAPNP_DECLARE_ENUM(type, id) \
    template <> struct Kind_<type> { static constexpr Kind kind = Kind::ENUM; }; \
    template <> struct TypeId_<type> { static constexpr uint64_t typeId = 0x##id; }; \
    template <> struct RawSchema_<type> { \
      static inline const RawSchema& get() { return schemas::s_##id; } \
    }
#define CAPNP_DEFINE_ENUM(type) \
    constexpr Kind Kind_<type>::kind; \
    constexpr uint64_t TypeId_<type>::typeId

#define CAPNP_DECLARE_STRUCT(type, id, dataWordSize, pointerCount, preferredElementEncoding) \
    template <> struct Kind_<type> { static constexpr Kind kind = Kind::STRUCT; }; \
    template <> struct StructSize_<type> { \
      static constexpr StructSize value = StructSize( \
          dataWordSize * WORDS, pointerCount * POINTERS, FieldSize::preferredElementEncoding); \
    }; \
    template <> struct TypeId_<type> { static constexpr uint64_t typeId = 0x##id; }; \
    template <> struct RawSchema_<type> { \
      static inline const RawSchema& get() { return schemas::s_##id; } \
    }
#define CAPNP_DEFINE_STRUCT(type) \
    constexpr Kind Kind_<type>::kind; \
    constexpr StructSize StructSize_<type>::value; \
    constexpr uint64_t TypeId_<type>::typeId

#define CAPNP_DECLARE_UNION(type, parentType, memberIndex) \
    template <> struct Kind_<type> { static constexpr Kind kind = Kind::UNION; }; \
    template <> struct UnionMemberIndex_<type> { static constexpr uint value = memberIndex; }; \
    template <> struct UnionParentType_<type> { typedef parentType Type; }
#define CAPNP_DEFINE_UNION(type) \
    constexpr Kind Kind_<type>::kind; \
    constexpr uint UnionMemberIndex_<type>::value

#define CAPNP_DECLARE_INTERFACE(type, id) \
    template <> struct Kind_<type> { static constexpr Kind kind = Kind::INTERFACE; }; \
    template <> struct TypeId_<type> { static constexpr uint64_t typeId = 0x##id; }; \
    template <> struct RawSchema_<type> { \
      static inline const RawSchema& get() { return schemas::s_##id; } \
    }
#define CAPNP_DEFINE_INTERFACE(type) \
    constexpr Kind Kind_<type>::kind; \
    constexpr uint64_t TypeId_<type>::typeId

#endif  // CAPNP_GENERATED_HEADER_SUPPORT_H_
