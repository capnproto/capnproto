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

#ifndef CAPNP_OBJECT_H_
#define CAPNP_OBJECT_H_

#include "layout.h"
#include "pointer-helpers.h"
#include "orphan.h"

namespace capnp {

class StructSchema;
class ListSchema;
class Orphanage;

struct ObjectPointer {
  // Reader/Builder for the `Object` field type, i.e. a pointer that can point to an arbitrary
  // object.

  class Reader {
  public:
    typedef ObjectPointer Reads;

    Reader() = default;
    inline Reader(_::PointerReader reader): reader(reader) {}

    inline bool isNull();

    template <typename T>
    inline typename T::Reader getAs();
    // Valid for T = any generated struct type, List<U>, Text, or Data.

    template <typename T>
    inline typename T::Reader getAs(StructSchema schema);
    // Only valid for T = DynamicStruct.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline typename T::Reader getAs(ListSchema schema);
    // Only valid for T = DynamicList.  Requires `#include <capnp/dynamic.h>`.

  private:
    _::PointerReader reader;
    friend struct ObjectPointer;
    friend class Orphanage;
  };

  class Builder {
  public:
    typedef ObjectPointer Builds;

    Builder() = delete;
    inline Builder(decltype(nullptr)) {}
    inline Builder(_::PointerBuilder builder): builder(builder) {}

    inline bool isNull();

    inline void clear();
    // Set to null.

    template <typename T>
    inline typename T::Builder getAs();
    // Valid for T = any generated struct type, List<U>, Text, or Data.

    template <typename T>
    inline typename T::Builder getAs(StructSchema schema);
    // Only valid for T = DynamicStruct.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline typename T::Builder getAs(ListSchema schema);
    // Only valid for T = DynamicList.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline typename T::Builder initAs();
    // Valid for T = any generated struct type.

    template <typename T>
    inline typename T::Builder initAs(uint elementCount);
    // Valid for T = List<U>, Text, or Data.

    template <typename T>
    inline typename T::Builder initAs(StructSchema schema);
    // Only valid for T = DynamicStruct.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline typename T::Builder initAs(ListSchema schema, uint elementCount);
    // Only valid for T = DynamicList.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline void setAs(typename T::Reader value);
    // Valid for ReaderType = T::Reader for T = any generated struct type, List<U>, Text, Data,
    // DynamicStruct, or DynamicList (the dynamic types require `#include <capnp/dynamic.h>`).

    template <typename T>
    inline void setAs(std::initializer_list<ReaderFor<ListElementType<T>>> list);
    // Valid for T = List<?>.

    inline void set(Reader value) { builder.copyFrom(value.reader); }
    // Set to a copy of another ObjectPointer.

    template <typename T>
    inline void adopt(Orphan<T>&& orphan);
    // Valid for T = any generated struct type, List<U>, Text, Data, DynamicList, DynamicStruct,
    // or DynamicValue (the dynamic types require `#include <capnp/dynamic.h>`).

    template <typename T>
    inline Orphan<T> disownAs();
    // Valid for T = any generated struct type, List<U>, Text, Data.

    template <typename T>
    inline Orphan<T> disownAs(StructSchema schema);
    // Only valid for T = DynamicStruct.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline Orphan<T> disownAs(ListSchema schema);
    // Only valid for T = DynamicList.  Requires `#include <capnp/dynamic.h>`.

    inline Orphan<ObjectPointer> disown();
    // Disown without a type.

    inline Reader asReader() const { return Reader(builder.asReader()); }
    inline operator Reader() const { return Reader(builder.asReader()); }

  private:
    _::PointerBuilder builder;
  };
};

template <>
class Orphan<ObjectPointer> {
  // An orphaned object of unknown type.

public:
  Orphan() = default;
  KJ_DISALLOW_COPY(Orphan);
  Orphan(Orphan&&) = default;
  Orphan& operator=(Orphan&&) = default;

  // It's not possible to get an ObjectPointer::{Reader,Builder} directly since there is no
  // underlying pointer (the pointer would normally live in the parent, but this object is
  // orphaned).  It is possible, however, to request readers/builders.

  template <typename T>
  inline typename T::Builder getAs();
  template <typename T>
  inline typename T::Builder getAs(StructSchema schema);
  template <typename T>
  inline typename T::Builder getAs(ListSchema schema);
  template <typename T>
  inline typename T::Reader getAsReader() const;
  template <typename T>
  inline typename T::Reader getAsReader(StructSchema schema) const;
  template <typename T>
  inline typename T::Reader getAsReader(ListSchema schema) const;

  template <typename T>
  inline Orphan<T> releaseAs();
  template <typename T>
  inline Orphan<T> releaseAs(StructSchema schema);
  template <typename T>
  inline Orphan<T> releaseAs(ListSchema schema);
  // Down-cast the orphan to a specific type.

  inline bool operator==(decltype(nullptr)) const { return builder == nullptr; }
  inline bool operator!=(decltype(nullptr)) const { return builder != nullptr; }

private:
  _::OrphanBuilder builder;

  inline Orphan(_::OrphanBuilder&& builder)
      : builder(kj::mv(builder)) {}

  template <typename, Kind>
  friend struct _::PointerHelpers;
  friend class Orphanage;
  template <typename U>
  friend class Orphan;
  friend class ObjectPointer::Builder;
};

// =======================================================================================
// Inline implementation details

inline bool ObjectPointer::Reader::isNull() {
  return reader.isNull();
}

template <typename T>
inline typename T::Reader ObjectPointer::Reader::getAs() {
  return _::PointerHelpers<T>::get(reader);
}

inline bool ObjectPointer::Builder::isNull() {
  return builder.isNull();
}

inline void ObjectPointer::Builder::clear() {
  return builder.clear();
}

template <typename T>
inline typename T::Builder ObjectPointer::Builder::getAs() {
  return _::PointerHelpers<T>::get(builder);
}

template <typename T>
inline typename T::Builder ObjectPointer::Builder::initAs() {
  return _::PointerHelpers<T>::init(builder);
}

template <typename T>
inline typename T::Builder ObjectPointer::Builder::initAs(uint elementCount) {
  return _::PointerHelpers<T>::init(builder, elementCount);
}

template <typename T>
inline void ObjectPointer::Builder::setAs(typename T::Reader value) {
  return _::PointerHelpers<T>::set(builder, value);
}

template <typename T>
inline void ObjectPointer::Builder::setAs(
    std::initializer_list<ReaderFor<ListElementType<T>>> list) {
  return _::PointerHelpers<T>::set(builder, list);
}

template <typename T>
inline void ObjectPointer::Builder::adopt(Orphan<T>&& orphan) {
  _::PointerHelpers<T>::adopt(builder, kj::mv(orphan));
}

template <typename T>
inline Orphan<T> ObjectPointer::Builder::disownAs() {
  return _::PointerHelpers<T>::disown(builder);
}

inline Orphan<ObjectPointer> ObjectPointer::Builder::disown() {
  return Orphan<ObjectPointer>(builder.disown());
}

template <> struct ReaderFor_ <ObjectPointer, Kind::UNKNOWN> { typedef ObjectPointer::Reader Type; };
template <> struct BuilderFor_<ObjectPointer, Kind::UNKNOWN> { typedef ObjectPointer::Builder Type; };

template <>
struct Orphanage::GetInnerReader<ObjectPointer, Kind::UNKNOWN> {
  static inline _::PointerReader apply(const ObjectPointer::Reader& t) {
    return t.reader;
  }
};

template <typename T>
inline typename T::Builder Orphan<ObjectPointer>::getAs() {
  return _::OrphanGetImpl<T>::apply(builder);
}
template <typename T>
inline typename T::Reader Orphan<ObjectPointer>::getAsReader() const {
  return _::OrphanGetImpl<T>::applyReader(builder);
}
template <typename T>
inline Orphan<T> Orphan<ObjectPointer>::releaseAs() {
  return Orphan<T>(kj::mv(builder));
}

}  // namespace capnp

#endif  // CAPNP_OBJECT_H_
