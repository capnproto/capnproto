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
#include <kj/string.h>

namespace capnp {

class MessageBuilder;  // So that it can be declared a friend.

template <typename T, Kind k = kind<T>()>
struct ToDynamic_;   // Defined in dynamic.h, needs to be declared as everyone's friend.

struct DynamicStruct;  // So that it can be declared a friend.

namespace _ {  // private

template <typename T>
struct PointerHelpers<T, Kind::STRUCT> {
  static inline typename T::Reader get(StructReader reader, WirePointerCount index,
                                       const word* defaultValue = nullptr) {
    return typename T::Reader(reader.getStructField(index, defaultValue));
  }
  static inline typename T::Builder get(StructBuilder builder, WirePointerCount index,
                                        const word* defaultValue = nullptr) {
    return typename T::Builder(builder.getStructField(index, structSize<T>(), defaultValue));
  }
  static inline void set(StructBuilder builder, WirePointerCount index,
                         typename T::Reader value) {
    builder.setStructField(index, value._reader);
  }
  static inline typename T::Builder init(StructBuilder builder, WirePointerCount index) {
    return typename T::Builder(builder.initStructField(index, structSize<T>()));
  }
  static inline void adopt(StructBuilder builder, WirePointerCount index, Orphan<T>&& value) {
    builder.adopt(index, kj::mv(value.builder));
  }
  static inline Orphan<T> disown(StructBuilder builder, WirePointerCount index) {
    return Orphan<T>(builder.disown(index));
  }
};

template <typename T>
struct PointerHelpers<List<T>, Kind::LIST> {
  static inline typename List<T>::Reader get(StructReader reader, WirePointerCount index,
                                             const word* defaultValue = nullptr) {
    return typename List<T>::Reader(List<T>::getAsFieldOf(reader, index, defaultValue));
  }
  static inline typename List<T>::Builder get(StructBuilder builder, WirePointerCount index,
                                              const word* defaultValue = nullptr) {
    return typename List<T>::Builder(List<T>::getAsFieldOf(builder, index, defaultValue));
  }
  static inline void set(StructBuilder builder, WirePointerCount index,
                         typename List<T>::Reader value) {
    builder.setListField(index, value.reader);
  }
  template <typename U>
  static void set(StructBuilder builder, WirePointerCount index, std::initializer_list<U> value) {
    auto l = init(builder, index, value.size());
    uint i = 0;
    for (auto& element: value) {
      l.set(i++, element);
    }
  }
  static inline typename List<T>::Builder init(
      StructBuilder builder, WirePointerCount index, uint size) {
    return typename List<T>::Builder(List<T>::initAsFieldOf(builder, index, size));
  }
  static inline void adopt(StructBuilder builder, WirePointerCount index, Orphan<List<T>>&& value) {
    builder.adopt(index, kj::mv(value.builder));
  }
  static inline Orphan<List<T>> disown(StructBuilder builder, WirePointerCount index) {
    return Orphan<List<T>>(builder.disown(index));
  }
};

template <typename T>
struct PointerHelpers<T, Kind::BLOB> {
  static inline typename T::Reader get(StructReader reader, WirePointerCount index,
                                       const void* defaultValue = nullptr,
                                       uint defaultBytes = 0) {
    return reader.getBlobField<T>(index, defaultValue, defaultBytes * BYTES);
  }
  static inline typename T::Builder get(StructBuilder builder, WirePointerCount index,
                                        const void* defaultValue = nullptr,
                                        uint defaultBytes = 0) {
    return builder.getBlobField<T>(index, defaultValue, defaultBytes * BYTES);
  }
  static inline void set(StructBuilder builder, WirePointerCount index, typename T::Reader value) {
    builder.setBlobField<T>(index, value);
  }
  static inline typename T::Builder init(StructBuilder builder, WirePointerCount index, uint size) {
    return builder.initBlobField<T>(index, size * BYTES);
  }
  static inline void adopt(StructBuilder builder, WirePointerCount index, Orphan<T>&& value) {
    builder.adopt(index, kj::mv(value.builder));
  }
  static inline Orphan<T> disown(StructBuilder builder, WirePointerCount index) {
    return Orphan<T>(builder.disown(index));
  }
};

struct UncheckedMessage {
  typedef const word* Reader;
};

template <>
struct PointerHelpers<UncheckedMessage> {
  // Reads an Object field as an unchecked message pointer.  Requires that the containing message is
  // itself unchecked.  This hack is currently private.  It is used to locate default values within
  // encoded schemas.

  static inline const word* get(StructReader reader, WirePointerCount index) {
    return reader.getUncheckedPointer(index);
  }
};

struct RawSchema {
  // The generated code defines a constant RawSchema for every compiled declaration.
  //
  // This is an internal structure which could change in the future.

  uint64_t id;

  const word* encodedNode;
  // Encoded SchemaNode, readable via readMessageUnchecked<schema::Node>(encodedNode).

  const RawSchema* const* dependencies;
  // Pointers to other types on which this one depends, sorted by ID.  The schemas in this table
  // may be uninitialized -- you must call ensureInitialized() on the one you wish to use before
  // using it.
  //
  // TODO(someday):  Make this a hashtable.

  struct MemberInfo {
    uint16_t unionIndex;  // 0 = not in a union, >0 = parent union's index + 1
    uint16_t index;       // index of the member
  };

  const MemberInfo* membersByName;
  // Indexes of members sorted by name.  Used to implement name lookup.
  // TODO(someday):  Make this a hashtable.

  uint32_t dependencyCount;
  uint32_t memberCount;
  // Sizes of above tables.

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

template <typename T>
struct TypeId_;

template <typename T>
struct UnionMemberIndex_;
template <typename T>
inline uint unionMemberIndex() { return UnionMemberIndex_<T>::value; }

template <typename T>
struct UnionParentType_;
template <typename T>
using UnionParentType = typename UnionParentType_<T>::Type;

kj::String structString(StructReader reader, const RawSchema& schema);
kj::String unionString(StructReader reader, const RawSchema& schema, uint memberIndex);
// Declared here so that we can declare inline stringify methods on generated types.
// Defined in stringify.c++, which depends on dynamic.c++, which is allowed not to be linked in.

template <typename T>
inline kj::String structString(StructReader reader) {
  return structString(reader, rawSchema<T>());
}

template <typename T>
inline kj::String unionString(StructReader reader) {
  return unionString(reader, rawSchema<UnionParentType<T>>(), unionMemberIndex<T>());
}

}  // namespace _ (private)

template <typename T>
inline constexpr uint64_t typeId() { return _::TypeId_<T>::typeId; }
// typeId<MyType>() returns the type ID as defined in the schema.  Works with structs, enums, and
// interfaces.

}  // namespace capnp

#define CAPNP_DECLARE_ENUM(type, id) \
    template <> struct Kind_<type> { static constexpr Kind kind = Kind::ENUM; }; \
    template <> struct TypeId_<type> { static constexpr uint64_t typeId = 0x##id; }; \
    template <> struct RawSchema_<type> { \
      static inline const RawSchema& get() { return schemas::s_##id; } \
    }
#define CAPNP_DEFINE_ENUM(type) \
    constexpr Kind Kind_<type>::kind; \
    constexpr uint64_t TypeId_<type>::typeId;

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
    constexpr uint64_t TypeId_<type>::typeId;

#define CAPNP_DECLARE_UNION(type, parentType, memberIndex) \
    template <> struct Kind_<type> { static constexpr Kind kind = Kind::UNION; }; \
    template <> struct UnionMemberIndex_<type> { static constexpr uint value = memberIndex; }; \
    template <> struct UnionParentType_<type> { typedef parentType Type; }
#define CAPNP_DEFINE_UNION(type) \
    constexpr Kind Kind_<type>::kind; \
    constexpr uint UnionMemberIndex_<type>::value;

#define CAPNP_DECLARE_INTERFACE(type, id) \
    template <> struct Kind_<type> { static constexpr Kind kind = Kind::INTERFACE; }; \
    template <> struct TypeId_<type> { static constexpr uint64_t typeId = 0x##id; }; \
    template <> struct RawSchema_<type> { \
      static inline const RawSchema& get() { return schemas::s_##id; } \
    }
#define CAPNP_DEFINE_INTERFACE(type) \
    constexpr Kind Kind_<type>::kind; \
    constexpr uint64_t TypeId_<type>::typeId;

#endif  // CAPNP_GENERATED_HEADER_SUPPORT_H_
