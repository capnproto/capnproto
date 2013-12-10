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

#ifndef CAPNP_POINTER_HELPERS_H_
#define CAPNP_POINTER_HELPERS_H_

#include "layout.h"
#include "list.h"

namespace capnp {
namespace _ {  // private

// PointerHelpers is a template class that assists in wrapping/unwrapping the low-level types in
// layout.h with the high-level public API and generated types.  This way, the code generator
// and other templates do not have to specialize on each kind of pointer.

template <typename T>
struct PointerHelpers<T, Kind::STRUCT> {
  static inline typename T::Reader get(PointerReader reader, const word* defaultValue = nullptr) {
    return typename T::Reader(reader.getStruct(defaultValue));
  }
  static inline typename T::Builder get(PointerBuilder builder,
                                        const word* defaultValue = nullptr) {
    return typename T::Builder(builder.getStruct(structSize<T>(), defaultValue));
  }
  static inline void set(PointerBuilder builder, typename T::Reader value) {
    builder.setStruct(value._reader);
  }
  static inline typename T::Builder init(PointerBuilder builder) {
    return typename T::Builder(builder.initStruct(structSize<T>()));
  }
  static inline void adopt(PointerBuilder builder, Orphan<T>&& value) {
    builder.adopt(kj::mv(value.builder));
  }
  static inline Orphan<T> disown(PointerBuilder builder) {
    return Orphan<T>(builder.disown());
  }

  static inline _::StructReader getInternalReader(const typename T::Reader& reader) {
    // TODO(cleanup):  This is used by RpcSystem::Connect, but perhaps it should be used more
    //   broadly so that we can reduce the number of friends declared by every Reader type.

    return reader._reader;
  }
};

template <typename T>
struct PointerHelpers<List<T>, Kind::LIST> {
  static inline typename List<T>::Reader get(PointerReader reader,
                                             const word* defaultValue = nullptr) {
    return typename List<T>::Reader(List<T>::getFromPointer(reader, defaultValue));
  }
  static inline typename List<T>::Builder get(PointerBuilder builder,
                                              const word* defaultValue = nullptr) {
    return typename List<T>::Builder(List<T>::getFromPointer(builder, defaultValue));
  }
  static inline void set(PointerBuilder builder, typename List<T>::Reader value) {
    builder.setList(value.reader);
  }
  static void set(PointerBuilder builder, kj::ArrayPtr<const ReaderFor<T>> value) {
    auto l = init(builder, value.size());
    uint i = 0;
    for (auto& element: value) {
      l.set(i++, element);
    }
  }
  static inline typename List<T>::Builder init(PointerBuilder builder, uint size) {
    return typename List<T>::Builder(List<T>::initPointer(builder, size));
  }
  static inline void adopt(PointerBuilder builder, Orphan<List<T>>&& value) {
    builder.adopt(kj::mv(value.builder));
  }
  static inline Orphan<List<T>> disown(PointerBuilder builder) {
    return Orphan<List<T>>(builder.disown());
  }
};

template <typename T>
struct PointerHelpers<T, Kind::BLOB> {
  static inline typename T::Reader get(PointerReader reader,
                                       const void* defaultValue = nullptr,
                                       uint defaultBytes = 0) {
    return reader.getBlob<T>(defaultValue, defaultBytes * BYTES);
  }
  static inline typename T::Builder get(PointerBuilder builder,
                                        const void* defaultValue = nullptr,
                                        uint defaultBytes = 0) {
    return builder.getBlob<T>(defaultValue, defaultBytes * BYTES);
  }
  static inline void set(PointerBuilder builder, typename T::Reader value) {
    builder.setBlob<T>(value);
  }
  static inline typename T::Builder init(PointerBuilder builder, uint size) {
    return builder.initBlob<T>(size * BYTES);
  }
  static inline void adopt(PointerBuilder builder, Orphan<T>&& value) {
    builder.adopt(kj::mv(value.builder));
  }
  static inline Orphan<T> disown(PointerBuilder builder) {
    return Orphan<T>(builder.disown());
  }
};

struct UncheckedMessage {
  typedef const word* Reader;
};

template <>
struct PointerHelpers<UncheckedMessage> {
  // Reads an AnyPointer field as an unchecked message pointer.  Requires that the containing
  // message is itself unchecked.  This hack is currently private.  It is used to locate default
  // values within encoded schemas.

  static inline const word* get(PointerReader reader) {
    return reader.getUnchecked();
  }
};

}  // namespace _ (private)
}  // namespace capnp

#endif  // CAPNP_POINTER_HELPERS_H_
