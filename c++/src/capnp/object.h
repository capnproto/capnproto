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
class InterfaceSchema;
class Orphanage;
class ClientHook;
class PipelineHook;

// =======================================================================================
// Pipeline helpers
//
// These relate to capabilities, but we don't declare them in capability.h because generated code
// for structs needs to know about these, even in files that contain no interfaces.

struct PipelineOp {
  // Corresponds to rpc.capnp's PromisedAnswer.Op.

  enum Type {
    NOOP,  // for convenience

    GET_POINTER_FIELD

    // There may be other types in the future...
  };

  Type type;
  union {
    uint16_t pointerIndex;  // for GET_POINTER_FIELD
  };
};

class PipelineHook {
  // Represents a currently-running call, and implements pipelined requests on its result.

public:
  virtual kj::Own<const PipelineHook> addRef() const = 0;
  // Increment this object's reference count.

  virtual kj::Own<const ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const = 0;
  // Extract a promised Capability from the results.

  virtual kj::Own<const ClientHook> getPipelinedCap(kj::Array<PipelineOp>&& ops) const;
  // Version of getPipelinedCap() passing the array by move.  May avoid a copy in some cases.
  // Default implementation just calls the other version.
};

// =======================================================================================
// ObjectPointer!

struct ObjectPointer {
  // Reader/Builder for the `Object` field type, i.e. a pointer that can point to an arbitrary
  // object.

  class Reader {
  public:
    typedef ObjectPointer Reads;

    Reader() = default;
    inline Reader(_::PointerReader reader): reader(reader) {}

    inline size_t targetSizeInWords() const;
    // Get the total size, in words, of the target object and all its children.

    inline bool isNull() const;

    template <typename T>
    inline ReaderFor<T> getAs() const;
    // Valid for T = any generated struct type, interface type, List<U>, Text, or Data.

    template <typename T>
    inline ReaderFor<T> getAs(StructSchema schema) const;
    // Only valid for T = DynamicStruct.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline ReaderFor<T> getAs(ListSchema schema) const;
    // Only valid for T = DynamicList.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline ReaderFor<T> getAs(InterfaceSchema schema) const;
    // Only valid for T = DynamicCapability.  Requires `#include <capnp/dynamic.h>`.

    kj::Own<const ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const;
    // Used by RPC system to implement pipelining.  Applications generally shouldn't use this
    // directly.

  private:
    _::PointerReader reader;
    friend struct ObjectPointer;
    friend class Orphanage;
    friend class CapReaderContext;
  };

  class Builder {
  public:
    typedef ObjectPointer Builds;

    Builder() = delete;
    inline Builder(decltype(nullptr)) {}
    inline Builder(_::PointerBuilder builder): builder(builder) {}

    inline size_t targetSizeInWords() const;
    // Get the total size, in words, of the target object and all its children.

    inline bool isNull();

    inline void clear();
    // Set to null.

    template <typename T>
    inline BuilderFor<T> getAs();
    // Valid for T = any generated struct type, List<U>, Text, or Data.

    template <typename T>
    inline BuilderFor<T> getAs(StructSchema schema);
    // Only valid for T = DynamicStruct.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline BuilderFor<T> getAs(ListSchema schema);
    // Only valid for T = DynamicList.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline BuilderFor<T> getAs(InterfaceSchema schema);
    // Only valid for T = DynamicCapability.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline BuilderFor<T> initAs();
    // Valid for T = any generated struct type.

    template <typename T>
    inline BuilderFor<T> initAs(uint elementCount);
    // Valid for T = List<U>, Text, or Data.

    template <typename T>
    inline BuilderFor<T> initAs(StructSchema schema);
    // Only valid for T = DynamicStruct.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline BuilderFor<T> initAs(ListSchema schema, uint elementCount);
    // Only valid for T = DynamicList.  Requires `#include <capnp/dynamic.h>`.

    template <typename T>
    inline void setAs(ReaderFor<T> value);
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

    template <typename T>
    inline Orphan<T> disownAs(InterfaceSchema schema);
    // Only valid for T = DynamicCapability.  Requires `#include <capnp/dynamic.h>`.

    inline Orphan<ObjectPointer> disown();
    // Disown without a type.

    inline Reader asReader() const { return Reader(builder.asReader()); }
    inline operator Reader() const { return Reader(builder.asReader()); }

    inline void setInternal(_::StructReader value) { builder.setStruct(value); }
    // For internal use.
    //
    // TODO(cleanup):  RPC implementation uses this, but wouldn't have to if we had an AnyStruct
    //   type, which would be useful anyawy.

  private:
    _::PointerBuilder builder;
    friend class Orphanage;
    friend class CapBuilderContext;
  };

  class Pipeline {
  public:
    inline Pipeline(decltype(nullptr)) {}
    inline explicit Pipeline(kj::Own<const PipelineHook>&& hook): hook(kj::mv(hook)) {}

    Pipeline noop() const;
    // Just make a copy.

    Pipeline getPointerField(uint16_t pointerIndex) const;
    // Return a new Promise representing a sub-object of the result.  `pointerIndex` is the index
    // of the sub-object within the pointer section of the result (the result must be a struct).
    //
    // TODO(kenton):  On GCC 4.8 / Clang 3.3, use rvalue qualifiers to avoid the need for copies.
    //   Also make `ops` into a Vector to optimize this.

    kj::Own<const ClientHook> asCap() const;
    // Expect that the result is a capability and construct a pipelined version of it now.

    inline kj::Own<const PipelineHook> releasePipelineHook() { return kj::mv(hook); }
    // For use by RPC implementations.

  private:
    kj::Own<const PipelineHook> hook;
    kj::Array<PipelineOp> ops;

    inline Pipeline(kj::Own<const PipelineHook>&& hook, kj::Array<PipelineOp>&& ops)
        : hook(kj::mv(hook)), ops(kj::mv(ops)) {}
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

  template <typename T>
  inline Orphan(Orphan<T>&& other): builder(kj::mv(other.builder)) {}
  template <typename T>
  inline Orphan& operator=(Orphan<T>&& other) { builder = kj::mv(other.builder); }
  // Cast from typed orphan.

  // It's not possible to get an ObjectPointer::{Reader,Builder} directly since there is no
  // underlying pointer (the pointer would normally live in the parent, but this object is
  // orphaned).  It is possible, however, to request typed readers/builders.

  template <typename T>
  inline BuilderFor<T> getAs();
  template <typename T>
  inline BuilderFor<T> getAs(StructSchema schema);
  template <typename T>
  inline BuilderFor<T> getAs(ListSchema schema);
  template <typename T>
  inline typename T::Client getAs(InterfaceSchema schema);
  template <typename T>
  inline ReaderFor<T> getAsReader() const;
  template <typename T>
  inline ReaderFor<T> getAsReader(StructSchema schema) const;
  template <typename T>
  inline ReaderFor<T> getAsReader(ListSchema schema) const;
  template <typename T>
  inline typename T::Client getAsReader(InterfaceSchema schema) const;

  template <typename T>
  inline Orphan<T> releaseAs();
  template <typename T>
  inline Orphan<T> releaseAs(StructSchema schema);
  template <typename T>
  inline Orphan<T> releaseAs(ListSchema schema);
  template <typename T>
  inline Orphan<T> releaseAs(InterfaceSchema schema);
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

inline size_t ObjectPointer::Reader::targetSizeInWords() const {
  return reader.targetSize() / WORDS;
}

inline bool ObjectPointer::Reader::isNull() const {
  return reader.isNull();
}

template <typename T>
inline ReaderFor<T> ObjectPointer::Reader::getAs() const {
  return _::PointerHelpers<T>::get(reader);
}

inline size_t ObjectPointer::Builder::targetSizeInWords() const {
  return asReader().targetSizeInWords();
}

inline bool ObjectPointer::Builder::isNull() {
  return builder.isNull();
}

inline void ObjectPointer::Builder::clear() {
  return builder.clear();
}

template <typename T>
inline BuilderFor<T> ObjectPointer::Builder::getAs() {
  return _::PointerHelpers<T>::get(builder);
}

template <typename T>
inline BuilderFor<T> ObjectPointer::Builder::initAs() {
  return _::PointerHelpers<T>::init(builder);
}

template <typename T>
inline BuilderFor<T> ObjectPointer::Builder::initAs(uint elementCount) {
  return _::PointerHelpers<T>::init(builder, elementCount);
}

template <typename T>
inline void ObjectPointer::Builder::setAs(ReaderFor<T> value) {
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

template <>
struct Orphanage::GetInnerBuilder<ObjectPointer, Kind::UNKNOWN> {
  static inline _::PointerBuilder apply(ObjectPointer::Builder& t) {
    return t.builder;
  }
};

template <typename T>
inline BuilderFor<T> Orphan<ObjectPointer>::getAs() {
  return _::OrphanGetImpl<T>::apply(builder);
}
template <typename T>
inline ReaderFor<T> Orphan<ObjectPointer>::getAsReader() const {
  return _::OrphanGetImpl<T>::applyReader(builder);
}
template <typename T>
inline Orphan<T> Orphan<ObjectPointer>::releaseAs() {
  return Orphan<T>(kj::mv(builder));
}

// Using ObjectPointer as the template type should work...

template <>
inline typename ObjectPointer::Reader ObjectPointer::Reader::getAs<ObjectPointer>() const {
  return *this;
}
template <>
inline typename ObjectPointer::Builder ObjectPointer::Builder::getAs<ObjectPointer>() {
  return *this;
}
template <>
inline typename ObjectPointer::Builder ObjectPointer::Builder::initAs<ObjectPointer>() {
  clear();
  return *this;
}
template <>
inline void ObjectPointer::Builder::setAs<ObjectPointer>(ObjectPointer::Reader value) {
  return builder.copyFrom(value.reader);
}
template <>
inline void ObjectPointer::Builder::adopt<ObjectPointer>(Orphan<ObjectPointer>&& orphan) {
  builder.adopt(kj::mv(orphan.builder));
}
template <>
inline Orphan<ObjectPointer> ObjectPointer::Builder::disownAs<ObjectPointer>() {
  return Orphan<ObjectPointer>(builder.disown());
}
template <>
inline Orphan<ObjectPointer> Orphan<ObjectPointer>::releaseAs() {
  return kj::mv(*this);
}

}  // namespace capnp

#endif  // CAPNP_OBJECT_H_
