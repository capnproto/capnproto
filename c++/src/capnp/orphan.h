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

  inline BuilderFor<T> get();
  // Get the underlying builder.  If the orphan is null, this will allocate and return a default
  // object rather than crash.  This is done for security -- otherwise, you might enable a DoS
  // attack any time you disown a field and fail to check if it is null.  In the case of structs,
  // this means that the orphan is no longer null after get() returns.  In the case of lists,
  // no actual object is allocated since a simple empty ListBuilder can be returned.

  inline ReaderFor<T> getReader() const;

  inline bool operator==(decltype(nullptr)) const { return builder == nullptr; }
  inline bool operator!=(decltype(nullptr)) const { return builder != nullptr; }

  inline void truncate(uint size);
  // Truncate the object (which must be a list or a blob) down to the given size. The object's
  // current size must be larger than this. The object stays in its current position. If the object
  // is the last object in its segment (which is always true if the object is the last thing that
  // was allocated in the message) then the truncated space can be reclaimed. Otherwise, the space
  // is zero'd out but otherwise lost, like an abandoned orphan.
  //
  // Any existing readers or builders pointing at the object are invalidated by this call.  You
  // must call `get()` or `getReader()` again to get the new, valid pointer.

private:
  _::OrphanBuilder builder;

  inline Orphan(_::OrphanBuilder&& builder): builder(kj::mv(builder)) {}

  template <typename, Kind>
  friend struct _::PointerHelpers;
  template <typename, Kind>
  friend struct List;
  template <typename U>
  friend class Orphan;
  friend class Orphanage;
  friend class MessageBuilder;
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
  template <typename Reader>
  Orphan<FromReader<Reader>> newOrphanCopy(Reader& copyFrom) const;
  // Allocate a new orphaned object (struct, list, or blob) and initialize it as a copy of the
  // given object.

  Orphan<Data> referenceExternalData(Data::Reader data) const;
  // Creates an Orphan<Data> that points at an existing region of memory (e.g. from another message)
  // without copying it.  There are some SEVERE restrictions on how this can be used:
  // - The memory must remain valid until the `MessageBuilder` is destroyed (even if the orphan is
  //   abandoned).
  // - Because the data is const, you will not be allowed to obtain a `Data::Builder`
  //   for this blob.  Any call which would return such a builder will throw an exception.  You
  //   can, however, obtain a Reader, e.g. via orphan.getReader() or from a parent Reader (once
  //   the orphan is adopted).  It is your responsibility to make sure your code can deal with
  //   these problems when using this optimization; if you can't, allocate a copy instead.
  // - `data.begin()` must be aligned to a machine word boundary (32-bit or 64-bit depending on
  //   the CPU).  Any pointer returned by malloc() as well as any data blob obtained from another
  //   Cap'n Proto message satisfies this.
  // - If `data.size()` is not a multiple of 8, extra bytes past data.end() up until the next 8-byte
  //   boundary will be visible in the raw message when it is written out.  Thus, there must be no
  //   secrets in these bytes.  Data blobs obtained from other Cap'n Proto messages should be safe
  //   as these bytes should be zero (unless the sender had the same problem).
  //
  // The array will actually become one of the message's segments.  The data can thus be adopted
  // into the message tree without copying it.  This is particularly useful when referencing very
  // large blobs, such as whole mmap'd files.

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

template <typename T>
struct OrphanGetImpl<T, Kind::INTERFACE> {
  static inline typename T::Client apply(_::OrphanBuilder& builder) {
    return typename T::Client(builder.asCapability());
  }
  static inline typename T::Client applyReader(const _::OrphanBuilder& builder) {
    return typename T::Client(builder.asCapability());
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
inline BuilderFor<T> Orphan<T>::get() {
  return _::OrphanGetImpl<T>::apply(builder);
}

template <typename T>
inline ReaderFor<T> Orphan<T>::getReader() const {
  return _::OrphanGetImpl<T>::applyReader(builder);
}

template <typename T>
inline void Orphan<T>::truncate(uint size) {
  builder.truncate(size * ELEMENTS, false);
}

template <>
inline void Orphan<Text>::truncate(uint size) {
  builder.truncate(size * ELEMENTS, true);
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
inline Orphan<FromReader<Reader>> Orphanage::newOrphanCopy(const Reader& copyFrom) const {
  return Orphan<FromReader<Reader>>(_::OrphanBuilder::copy(
      arena, GetInnerReader<FromReader<Reader>>::apply(copyFrom)));
}
template <typename Reader>
inline Orphan<FromReader<Reader>> Orphanage::newOrphanCopy(Reader& copyFrom) const {
  return newOrphanCopy(kj::implicitCast<const Reader&>(copyFrom));
}

inline Orphan<Data> Orphanage::referenceExternalData(Data::Reader data) const {
  return Orphan<Data>(_::OrphanBuilder::referenceExternalData(arena, data));
}

}  // namespace capnp

#endif  // CAPNP_ORPHAN_H_
