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

#ifndef CAPNP_ANY_H_
#define CAPNP_ANY_H_

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
struct PipelineOp;

// =======================================================================================
// AnyPointer!

struct AnyPointer {
  // Reader/Builder for the `AnyPointer` field type, i.e. a pointer that can point to an arbitrary
  // object.

  class Reader {
  public:
    typedef AnyPointer Reads;

    Reader() = default;
    inline Reader(_::PointerReader reader): reader(reader) {}

    inline MessageSize targetSize() const;
    // Get the total size of the target object and all its children.

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

    kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const;
    // Used by RPC system to implement pipelining.  Applications generally shouldn't use this
    // directly.

  private:
    _::PointerReader reader;
    friend struct AnyPointer;
    friend class Orphanage;
    friend class CapReaderContext;
  };

  class Builder {
  public:
    typedef AnyPointer Builds;

    Builder() = delete;
    inline Builder(decltype(nullptr)) {}
    inline Builder(_::PointerBuilder builder): builder(builder) {}

    inline MessageSize targetSize() const;
    // Get the total size of the target object and all its children.

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
    // Set to a copy of another AnyPointer.

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

    inline Orphan<AnyPointer> disown();
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
    inline explicit Pipeline(kj::Own<PipelineHook>&& hook): hook(kj::mv(hook)) {}

    Pipeline noop();
    // Just make a copy.

    Pipeline getPointerField(uint16_t pointerIndex);
    // Return a new Promise representing a sub-object of the result.  `pointerIndex` is the index
    // of the sub-object within the pointer section of the result (the result must be a struct).
    //
    // TODO(kenton):  On GCC 4.8 / Clang 3.3, use rvalue qualifiers to avoid the need for copies.
    //   Also make `ops` into a Vector to optimize this.

    kj::Own<ClientHook> asCap();
    // Expect that the result is a capability and construct a pipelined version of it now.

    inline kj::Own<PipelineHook> releasePipelineHook() { return kj::mv(hook); }
    // For use by RPC implementations.

  private:
    kj::Own<PipelineHook> hook;
    kj::Array<PipelineOp> ops;

    inline Pipeline(kj::Own<PipelineHook>&& hook, kj::Array<PipelineOp>&& ops)
        : hook(kj::mv(hook)), ops(kj::mv(ops)) {}

    friend class LocalClient;
    friend class PipelineHook;
  };
};

template <>
class Orphan<AnyPointer> {
  // An orphaned object of unknown type.

public:
  Orphan() = default;
  KJ_DISALLOW_COPY(Orphan);
  Orphan(Orphan&&) = default;
  Orphan& operator=(Orphan&&) = default;

  template <typename T>
  inline Orphan(Orphan<T>&& other): builder(kj::mv(other.builder)) {}
  template <typename T>
  inline Orphan& operator=(Orphan<T>&& other) { builder = kj::mv(other.builder); return *this; }
  // Cast from typed orphan.

  // It's not possible to get an AnyPointer::{Reader,Builder} directly since there is no
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
  friend class AnyPointer::Builder;
};

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
  virtual kj::Own<PipelineHook> addRef() = 0;
  // Increment this object's reference count.

  virtual kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) = 0;
  // Extract a promised Capability from the results.

  virtual kj::Own<ClientHook> getPipelinedCap(kj::Array<PipelineOp>&& ops);
  // Version of getPipelinedCap() passing the array by move.  May avoid a copy in some cases.
  // Default implementation just calls the other version.

  static inline kj::Own<PipelineHook> from(AnyPointer::Pipeline&& pipeline) {
    return kj::mv(pipeline.hook);
  }
};

// =======================================================================================
// Inline implementation details

inline MessageSize AnyPointer::Reader::targetSize() const {
  return reader.targetSize().asPublic();
}

inline bool AnyPointer::Reader::isNull() const {
  return reader.isNull();
}

template <typename T>
inline ReaderFor<T> AnyPointer::Reader::getAs() const {
  return _::PointerHelpers<T>::get(reader);
}

inline MessageSize AnyPointer::Builder::targetSize() const {
  return asReader().targetSize();
}

inline bool AnyPointer::Builder::isNull() {
  return builder.isNull();
}

inline void AnyPointer::Builder::clear() {
  return builder.clear();
}

template <typename T>
inline BuilderFor<T> AnyPointer::Builder::getAs() {
  return _::PointerHelpers<T>::get(builder);
}

template <typename T>
inline BuilderFor<T> AnyPointer::Builder::initAs() {
  return _::PointerHelpers<T>::init(builder);
}

template <typename T>
inline BuilderFor<T> AnyPointer::Builder::initAs(uint elementCount) {
  return _::PointerHelpers<T>::init(builder, elementCount);
}

template <typename T>
inline void AnyPointer::Builder::setAs(ReaderFor<T> value) {
  return _::PointerHelpers<T>::set(builder, value);
}

template <typename T>
inline void AnyPointer::Builder::setAs(
    std::initializer_list<ReaderFor<ListElementType<T>>> list) {
  return _::PointerHelpers<T>::set(builder, list);
}

template <typename T>
inline void AnyPointer::Builder::adopt(Orphan<T>&& orphan) {
  _::PointerHelpers<T>::adopt(builder, kj::mv(orphan));
}

template <typename T>
inline Orphan<T> AnyPointer::Builder::disownAs() {
  return _::PointerHelpers<T>::disown(builder);
}

inline Orphan<AnyPointer> AnyPointer::Builder::disown() {
  return Orphan<AnyPointer>(builder.disown());
}

template <> struct ReaderFor_ <AnyPointer, Kind::UNKNOWN> { typedef AnyPointer::Reader Type; };
template <> struct BuilderFor_<AnyPointer, Kind::UNKNOWN> { typedef AnyPointer::Builder Type; };

template <>
struct Orphanage::GetInnerReader<AnyPointer, Kind::UNKNOWN> {
  static inline _::PointerReader apply(const AnyPointer::Reader& t) {
    return t.reader;
  }
};

template <>
struct Orphanage::GetInnerBuilder<AnyPointer, Kind::UNKNOWN> {
  static inline _::PointerBuilder apply(AnyPointer::Builder& t) {
    return t.builder;
  }
};

template <typename T>
inline BuilderFor<T> Orphan<AnyPointer>::getAs() {
  return _::OrphanGetImpl<T>::apply(builder);
}
template <typename T>
inline ReaderFor<T> Orphan<AnyPointer>::getAsReader() const {
  return _::OrphanGetImpl<T>::applyReader(builder);
}
template <typename T>
inline Orphan<T> Orphan<AnyPointer>::releaseAs() {
  return Orphan<T>(kj::mv(builder));
}

// Using AnyPointer as the template type should work...

template <>
inline typename AnyPointer::Reader AnyPointer::Reader::getAs<AnyPointer>() const {
  return *this;
}
template <>
inline typename AnyPointer::Builder AnyPointer::Builder::getAs<AnyPointer>() {
  return *this;
}
template <>
inline typename AnyPointer::Builder AnyPointer::Builder::initAs<AnyPointer>() {
  clear();
  return *this;
}
template <>
inline void AnyPointer::Builder::setAs<AnyPointer>(AnyPointer::Reader value) {
  return builder.copyFrom(value.reader);
}
template <>
inline void AnyPointer::Builder::adopt<AnyPointer>(Orphan<AnyPointer>&& orphan) {
  builder.adopt(kj::mv(orphan.builder));
}
template <>
inline Orphan<AnyPointer> AnyPointer::Builder::disownAs<AnyPointer>() {
  return Orphan<AnyPointer>(builder.disown());
}
template <>
inline Orphan<AnyPointer> Orphan<AnyPointer>::releaseAs() {
  return kj::mv(*this);
}

}  // namespace capnp

#endif  // CAPNP_ANY_H_
