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

#ifndef CAPNP_ORPHAN_H_
#define CAPNP_ORPHAN_H_

#include "layout.h"

namespace capnp {

class StructSchema;
class ListSchema;
struct DynamicStruct;
struct DynamicList;

template <typename T>
class Orphan {
  // Represents an object which is allocated within some message builder but has no pointers
  // pointing at it.  An Orphan can later be "adopted" by some other object as one of that object's
  // fields, without having to copy the orphan.  For a field `foo` of pointer type, the generated
  // code will define builder methods `void adoptFoo(Orphan<T>)` and `Orphan<T> disownFoo()`.
  // Orphans can also be created independently of any parent using an Orphanage.
  //
  // `Orphan<T>` can be moved but not copied, like `Own<T>`, so that it is impossible for one
  // orphan to be adopted multiple times.  If an orphan is destroyed without being adopted, its
  // contents are zero'd out (and possibly reused, if we ever implement the ability to reuse space
  // in a message arena).

public:
  Orphan() = default;
  KJ_DISALLOW_COPY(Orphan);
  Orphan(Orphan&&) = default;
  Orphan& operator=(Orphan&&) = default;

  inline typename T::Builder get();
  inline typename T::Reader getReader() const;

  inline bool operator==(decltype(nullptr)) { return builder == nullptr; }
  inline bool operator!=(decltype(nullptr)) { return builder == nullptr; }

private:
  _::OrphanBuilder builder;

  inline Orphan(_::OrphanBuilder&& builder): builder(kj::mv(builder)) {}

  template <typename, Kind>
  friend struct _::PointerHelpers;
  template <typename, Kind>
  friend struct List;
  friend class Orphanage;
};

class Orphanage: private kj::DisallowConstCopy {
  // Use to directly allocate Orphan objects, without having a parent object allocate and then
  // disown the object.

public:
  inline Orphanage(): arena(nullptr) {}

  template <typename BuilderType>
  static Orphanage getForMessageContaining(BuilderType builder);
  // Construct an Orphanage that allocates within the message containing the given Builder.  This
  // allows the constructed Orphans to be adopted by objects within said message.
  //
  // This constructor takes the builder rather than having the builder have a getOrphanage() method
  // because this is an advanced feature and we don't want to pollute the builder APIs with it.
  //
  // Note that if you have a direct pointer to the `MessageBuilder`, you can simply call its
  // `getOrphanage()` method.

  template <typename RootType>
  Orphan<RootType> newOrphan() const;
  // Allocate a new orphaned struct.

  template <typename RootType>
  Orphan<RootType> newOrphan(uint size) const;
  // Allocate a new orphaned list or blob.

  Orphan<DynamicStruct> newOrphan(StructSchema schema) const;
  // Dynamically create an orphan struct with the given schema.  You must
  // #include <capnp/dynamic.h> to use this.

  Orphan<DynamicList> newOrphan(ListSchema schema, uint size) const;
  // Dynamically create an orphan list with the given schema.  You must #include <capnp/dynamic.h>
  // to use this.

  template <typename Reader>
  Orphan<FromReader<Reader>> newOrphanCopy(const Reader& copyFrom) const;
  // Allocate a new orphaned object (struct, list, or blob) and initialize it as a copy of the
  // given object.

private:
  _::BuilderArena* arena;

  inline explicit Orphanage(_::BuilderArena* arena): arena(arena) {}

  template <typename T, Kind = kind<T>()>
  struct GetInnerBuilder;
  template <typename T, Kind = kind<T>()>
  struct GetInnerReader;
  template <typename T>
  struct NewOrphanListImpl;

  friend class MessageBuilder;
};

// =======================================================================================
// Inline implementation details.

namespace _ {  // private

template <typename T, Kind = kind<T>()>
struct OrphanGetImpl;

template <typename T>
struct OrphanGetImpl<T, Kind::STRUCT> {
  static inline typename T::Builder apply(_::OrphanBuilder& builder) {
    return typename T::Builder(builder.asStruct(_::structSize<T>()));
  }
  static inline typename T::Reader applyReader(const _::OrphanBuilder& builder) {
    return typename T::Reader(builder.asStructReader(_::structSize<T>()));
  }
};

template <typename T, Kind k>
struct OrphanGetImpl<List<T, k>, Kind::LIST> {
  static inline typename List<T>::Builder apply(_::OrphanBuilder& builder) {
    return typename List<T>::Builder(builder.asList(_::ElementSizeForType<T>::value));
  }
  static inline typename List<T>::Reader applyReader(const _::OrphanBuilder& builder) {
    return typename List<T>::Reader(builder.asListReader(_::ElementSizeForType<T>::value));
  }
};

template <typename T>
struct OrphanGetImpl<List<T, Kind::STRUCT>, Kind::LIST> {
  static inline typename List<T>::Builder apply(_::OrphanBuilder& builder) {
    return typename List<T>::Builder(builder.asStructList(_::structSize<T>()));
  }
  static inline typename List<T>::Reader applyReader(const _::OrphanBuilder& builder) {
    return typename List<T>::Reader(builder.asListReader(_::ElementSizeForType<T>::value));
  }
};

template <>
struct OrphanGetImpl<Text, Kind::BLOB> {
  static inline Text::Builder apply(_::OrphanBuilder& builder) {
    return Text::Builder(builder.asText());
  }
  static inline Text::Reader applyReader(const _::OrphanBuilder& builder) {
    return Text::Reader(builder.asTextReader());
  }
};

template <>
struct OrphanGetImpl<Data, Kind::BLOB> {
  static inline Data::Builder apply(_::OrphanBuilder& builder) {
    return Data::Builder(builder.asData());
  }
  static inline Data::Reader applyReader(const _::OrphanBuilder& builder) {
    return Data::Reader(builder.asDataReader());
  }
};

}  // namespace _ (private)

template <typename T>
inline typename T::Builder Orphan<T>::get() {
  return _::OrphanGetImpl<T>::apply(builder);
}

template <typename T>
inline typename T::Reader Orphan<T>::getReader() const {
  return _::OrphanGetImpl<T>::applyReader(builder);
}

template <typename T>
struct Orphanage::GetInnerBuilder<T, Kind::STRUCT> {
  static inline _::StructBuilder apply(typename T::Builder& t) {
    return t._builder;
  }
};

template <typename T>
struct Orphanage::GetInnerBuilder<T, Kind::LIST> {
  static inline _::ListBuilder apply(typename T::Builder& t) {
    return t.builder;
  }
};

template <typename BuilderType>
Orphanage Orphanage::getForMessageContaining(BuilderType builder) {
  return Orphanage(GetInnerBuilder<FromBuilder<BuilderType>>::apply(builder).getArena());
}

template <typename RootType>
Orphan<RootType> Orphanage::newOrphan() const {
  return Orphan<RootType>(_::OrphanBuilder::initStruct(arena, _::structSize<RootType>()));
}

template <typename T, Kind k>
struct Orphanage::NewOrphanListImpl<List<T, k>> {
  static inline _::OrphanBuilder apply(_::BuilderArena* arena, uint size) {
    return _::OrphanBuilder::initList(arena, size * ELEMENTS, _::ElementSizeForType<T>::value);
  }
};

template <typename T>
struct Orphanage::NewOrphanListImpl<List<T, Kind::STRUCT>> {
  static inline _::OrphanBuilder apply(_::BuilderArena* arena, uint size) {
    return _::OrphanBuilder::initStructList(arena, size * ELEMENTS, _::structSize<T>());
  }
};

template <>
struct Orphanage::NewOrphanListImpl<Text> {
  static inline _::OrphanBuilder apply(_::BuilderArena* arena, uint size) {
    return _::OrphanBuilder::initText(arena, size * BYTES);
  }
};

template <>
struct Orphanage::NewOrphanListImpl<Data> {
  static inline _::OrphanBuilder apply(_::BuilderArena* arena, uint size) {
    return _::OrphanBuilder::initData(arena, size * BYTES);
  }
};

template <typename RootType>
Orphan<RootType> Orphanage::newOrphan(uint size) const {
  return Orphan<RootType>(NewOrphanListImpl<RootType>::apply(arena, size));
}

template <typename T>
struct Orphanage::GetInnerReader<T, Kind::STRUCT> {
  static inline _::StructReader apply(const typename T::Reader& t) {
    return t._reader;
  }
};

template <typename T>
struct Orphanage::GetInnerReader<T, Kind::LIST> {
  static inline _::ListReader apply(const typename T::Reader& t) {
    return t.reader;
  }
};

template <typename T>
struct Orphanage::GetInnerReader<T, Kind::BLOB> {
  static inline const typename T::Reader& apply(const typename T::Reader& t) {
    return t;
  }
};

template <typename Reader>
Orphan<FromReader<Reader>> Orphanage::newOrphanCopy(const Reader& copyFrom) const {
  return Orphan<FromReader<Reader>>(_::OrphanBuilder::copy(
      arena, GetInnerReader<FromReader<Reader>>::apply(copyFrom)));
}

}  // namespace capnp

#endif  // CAPNP_ORPHAN_H_
