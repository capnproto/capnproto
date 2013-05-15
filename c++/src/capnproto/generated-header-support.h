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

#ifndef CAPNPROTO_GENERATED_HEADER_SUPPORT_H_
#define CAPNPROTO_GENERATED_HEADER_SUPPORT_H_

#include "layout.h"
#include "list.h"

namespace capnproto {

template <typename T, Kind k = kind<T>()>
struct ToDynamic_;   // Defined in dynamic.h, needs to be declared as everyone's friend.

class DynamicStruct;  // So that it can be declared a friend.

namespace internal {

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
    // TODO(now):  schemaless copy
    CAPNPROTO_INLINE_PRECOND(false, "Not implemented:  set() for struct fields.");
  }
  static inline typename T::Builder init(StructBuilder builder, WirePointerCount index) {
    return typename T::Builder(builder.initStructField(index, structSize<T>()));
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
    init(builder, index, value.size()).copyFrom(value);
  }
  static inline void set(StructBuilder builder, WirePointerCount index,
                         std::initializer_list<ReaderFor<T>> value) {
    init(builder, index, value.size()).copyFrom(value);
  }
  static inline typename List<T>::Builder init(
      StructBuilder builder, WirePointerCount index, uint size) {
    return typename List<T>::Builder(List<T>::initAsFieldOf(builder, index, size));
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
};

#if defined(CAPNPROTO_PRIVATE) || defined(__CDT_PARSER__)

struct TrustedMessage {
  typedef const word* Reader;
};

template <>
struct PointerHelpers<TrustedMessage> {
  // Reads an Object field as a trusted message pointer.  Requires that the containing message is
  // itself trusted.  This hack is currently private.  It is used to locate default values within
  // encoded schemas.

  static inline const word* get(StructReader reader, WirePointerCount index) {
    return reader.getTrustedPointer(index);
  }
};

#endif

struct RawSchema {
  // The generated code defines a constant RawSchema for every compiled declaration.
  //
  // This is an internal structure which could change in the future.

  const word* encodedNode;
  // Encoded SchemaNode, readable via readMessageTrusted<schema::Node>(encodedNode).

  const RawSchema* const* dependencies;
  // Pointers to other types on which this one depends, sorted by ID.
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
};

template <typename T>
struct RawSchemaFor;

template <typename T>
inline const RawSchema& rawSchema() {
  return RawSchemaFor<T>::getSchema();
}

template <typename T>
struct TypeIdFor;

}  // namespace internal

template <typename T>
inline constexpr uint64_t typeId() { return internal::TypeIdFor<T>::typeId; }
// typeId<MyType>() returns the type ID as defined in the schema.  Works with structs, enums, and
// interfaces.

}  // namespace capnproto

#define CAPNPROTO_DECLARE_ENUM(type, id) \
    template <> struct KindOf<type> { static constexpr Kind kind = Kind::ENUM; }; \
    template <> struct TypeIdFor<type> { static constexpr uint64_t typeId = 0x##id; }; \
    template <> struct RawSchemaFor<type> { \
      static inline const RawSchema& getSchema() { return schemas::s_##id; } \
    }
#define CAPNPROTO_DECLARE_STRUCT(type, id, dataWordSize, pointerCount, preferredElementEncoding) \
    template <> struct KindOf<type> { static constexpr Kind kind = Kind::STRUCT; }; \
    template <> struct StructSizeFor<type> { \
      static constexpr StructSize value = StructSize( \
          dataWordSize * WORDS, pointerCount * POINTERS, FieldSize::preferredElementEncoding); \
    }; \
    template <> struct TypeIdFor<type> { static constexpr uint64_t typeId = 0x##id; }; \
    template <> struct RawSchemaFor<type> { \
      static inline const RawSchema& getSchema() { return schemas::s_##id; } \
    }
#define CAPNPROTO_DECLARE_INTERFACE(type, id) \
    template <> struct KindOf<type> { static constexpr Kind kind = Kind::INTERFACE; }; \
    template <> struct TypeIdFor<type> { static constexpr uint64_t typeId = 0x##id; }; \
    template <> struct RawSchemaFor<type> { \
      static inline const RawSchema& getSchema() { return schemas::s_##id; } \
    }

#endif  // CAPNPROTO_GENERATED_HEADER_SUPPORT_H_
