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

class DynamicStruct;  // So that it can be declared a friend.

namespace internal {

template <typename T>
struct PointerHelpers {
  static inline typename T::Reader get(StructReader reader, WireReferenceCount index) {
    return typename T::Reader(reader.getStructField(index, nullptr));
  }
  static inline typename T::Builder get(StructBuilder builder, WireReferenceCount index) {
    return typename T::Builder(builder.getStructField(index, T::STRUCT_SIZE, nullptr));
  }
  static inline typename T::Builder init(StructBuilder builder, WireReferenceCount index) {
    return typename T::Builder(builder.initStructField(index, T::STRUCT_SIZE));
  }
};

template <typename T>
struct PointerHelpers<List<T>> {
  static inline typename List<T>::Reader get(StructReader reader, WireReferenceCount index) {
    return typename List<T>::Reader(List<T>::getAsFieldOf(reader, index));
  }
  static inline typename List<T>::Builder get(StructBuilder builder, WireReferenceCount index) {
    return typename List<T>::Builder(List<T>::getAsFieldOf(builder, index));
  }
  static inline typename List<T>::Builder init(
      StructBuilder builder, WireReferenceCount index, int size) {
    return typename List<T>::Builder(List<T>::initAsFieldOf(builder, index, size));
  }
};

template <>
struct PointerHelpers<Text> {
  static inline Text::Reader get(StructReader reader, WireReferenceCount index) {
    return reader.getTextField(index, nullptr, 0 * BYTES);
  }
  static inline Text::Builder get(StructBuilder builder, WireReferenceCount index) {
    return builder.getTextField(index, nullptr, 0 * BYTES);
  }
  static inline void set(StructBuilder builder, WireReferenceCount index, Text::Reader value) {
    builder.setTextField(index, value);
  }
  static inline Text::Builder init(StructBuilder builder, WireReferenceCount index, int size) {
    return builder.initTextField(index, size * BYTES);
  }
};

template <>
struct PointerHelpers<Data> {
  static inline Data::Reader get(StructReader reader, WireReferenceCount index) {
    return reader.getDataField(index, nullptr, 0 * BYTES);
  }
  static inline Data::Builder get(StructBuilder builder, WireReferenceCount index) {
    return builder.getDataField(index, nullptr, 0 * BYTES);
  }
  static inline void set(StructBuilder builder, WireReferenceCount index, Data::Reader value) {
    builder.setDataField(index, value);
  }
  static inline Data::Builder init(StructBuilder builder, WireReferenceCount index, int size) {
    return builder.initDataField(index, size * BYTES);
  }
};

#ifdef CAPNPROTO_PRIVATE

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

}  // namespace internal
}  // namespace capnproto

#endif  // CAPNPROTO_GENERATED_HEADER_SUPPORT_H_
