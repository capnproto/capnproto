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

class SchemaPool;   // Needs to be declared for dynamic Object accessors.

class DynamicStruct;  // So that it can be declared a friend.

template <typename T>
inline constexpr uint64_t typeId();
// typeId<MyType>() returns the type ID as defined in the schema.  Works with structs, enums, and
// interfaces.

namespace internal {

template <typename T>
struct PointerHelpers<T, Kind::STRUCT> {
  static inline typename T::Reader get(StructReader reader, WireReferenceCount index,
                                       const word* defaultValue = nullptr) {
    return typename T::Reader(reader.getStructField(index, defaultValue));
  }
  static inline typename T::Builder get(StructBuilder builder, WireReferenceCount index,
                                        const word* defaultValue = nullptr) {
    return typename T::Builder(builder.getStructField(index, structSize<T>(), defaultValue));
  }
  static inline void set(StructBuilder builder, WireReferenceCount index,
                         typename T::Reader value) {
    // TODO(now):  schemaless copy
    CAPNPROTO_INLINE_PRECOND(false, "Not implemented:  set() for struct fields.");
  }
  static inline typename T::Builder init(StructBuilder builder, WireReferenceCount index) {
    return typename T::Builder(builder.initStructField(index, structSize<T>()));
  }
};

template <typename T>
struct PointerHelpers<List<T>, Kind::LIST> {
  static inline typename List<T>::Reader get(StructReader reader, WireReferenceCount index,
                                             const word* defaultValue = nullptr) {
    return typename List<T>::Reader(List<T>::getAsFieldOf(reader, index, defaultValue));
  }
  static inline typename List<T>::Builder get(StructBuilder builder, WireReferenceCount index,
                                              const word* defaultValue = nullptr) {
    return typename List<T>::Builder(List<T>::getAsFieldOf(builder, index, defaultValue));
  }
  static inline void set(StructBuilder builder, WireReferenceCount index,
                         typename List<T>::Reader value) {
    init(builder, index, value.size()).copyFrom(value);
  }
  static inline void set(StructBuilder builder, WireReferenceCount index,
                         std::initializer_list<ReaderFor<T>> value) {
    init(builder, index, value.size()).copyFrom(value);
  }
  static inline typename List<T>::Builder init(
      StructBuilder builder, WireReferenceCount index, uint size) {
    return typename List<T>::Builder(List<T>::initAsFieldOf(builder, index, size));
  }
};

template <typename T>
struct PointerHelpers<T, Kind::BLOB> {
  static inline typename T::Reader get(StructReader reader, WireReferenceCount index,
                                       const void* defaultValue = nullptr,
                                       uint defaultBytes = 0) {
    return reader.getBlobField<T>(index, defaultValue, defaultBytes * BYTES);
  }
  static inline typename T::Builder get(StructBuilder builder, WireReferenceCount index,
                                        const void* defaultValue = nullptr,
                                        uint defaultBytes = 0) {
    return builder.getBlobField<T>(index, defaultValue, defaultBytes * BYTES);
  }
  static inline void set(StructBuilder builder, WireReferenceCount index, typename T::Reader value) {
    builder.setBlobField<T>(index, value);
  }
  static inline typename T::Builder init(StructBuilder builder, WireReferenceCount index, uint size) {
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

  static inline const word* get(StructReader reader, WireReferenceCount index) {
    return reader.getTrustedPointer(index);
  }
};

#endif

struct RawSchema {
  const word* encodedNode;
  const RawSchema* dependencies;
};

template <typename T>
inline constexpr const RawSchema& rawSchema();

}  // namespace internal
}  // namespace capnproto

#define CAPNPROTO_DECLARE_ENUM(type, id) \
    template <> struct KindOf<type> { static constexpr Kind kind = Kind::ENUM; }
#define CAPNPROTO_DECLARE_STRUCT(type, id, dataWordSize, pointerCount, preferredElementEncoding) \
    template <> struct KindOf<type> { static constexpr Kind kind = Kind::STRUCT; }; \
    template <> struct StructSizeFor<type> { \
      static constexpr StructSize value = StructSize( \
          dataWordSize * WORDS, pointerCount * REFERENCES, FieldSize::preferredElementEncoding); \
    }
#define CAPNPROTO_DECLARE_INTERFACE(type, id) \
    template <> struct KindOf<type> { static constexpr Kind kind = Kind::INTERFACE; }

#endif  // CAPNPROTO_GENERATED_HEADER_SUPPORT_H_
