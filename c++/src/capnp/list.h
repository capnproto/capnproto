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

#ifndef CAPNP_LIST_H_
#define CAPNP_LIST_H_

#include "layout.h"
#include <initializer_list>

namespace capnp {
namespace _ {  // private

template <typename T, Kind k = kind<T>()>
struct PointerHelpers;

}  // namespace _ (private)

template <typename T, Kind k = kind<T>()>
struct List;

template <typename T, Kind k = kind<T>()> struct ReaderFor_ { typedef typename T::Reader Type; };
template <typename T> struct ReaderFor_<T, Kind::PRIMITIVE> { typedef T Type; };
template <typename T> struct ReaderFor_<T, Kind::ENUM> { typedef T Type; };
template <typename T> using ReaderFor = typename ReaderFor_<T>::Type;
// The type returned by List<T>::Reader::operator[].

template <typename T, Kind k = kind<T>()> struct BuilderFor_ { typedef typename T::Builder Type; };
template <typename T> struct BuilderFor_<T, Kind::PRIMITIVE> { typedef T Type; };
template <typename T> struct BuilderFor_<T, Kind::ENUM> { typedef T Type; };
template <typename T> using BuilderFor = typename BuilderFor_<T>::Type;
// The type returned by List<T>::Builder::operator[].

template <typename T>
using FromReader = typename kj::Decay<T>::Reads;
// FromReader<MyType::Reader> = MyType (for any Cap'n Proto type).

template <typename T>
using FromBuilder = typename kj::Decay<T>::Builds;
// FromBuilder<MyType::Builder> = MyType (for any Cap'n Proto type).

template <typename T, Kind k = kind<T>()> struct TypeIfEnum_;
template <typename T> struct TypeIfEnum_<T, Kind::ENUM> { typedef T Type; };

template <typename T>
using TypeIfEnum = typename TypeIfEnum_<kj::Decay<T>>::Type;

namespace _ {  // private

template <typename T, Kind k> struct Kind_<List<T, k>> { static constexpr Kind kind = Kind::LIST; };

template <size_t size> struct FieldSizeForByteSize;
template <> struct FieldSizeForByteSize<1> { static constexpr FieldSize value = FieldSize::BYTE; };
template <> struct FieldSizeForByteSize<2> { static constexpr FieldSize value = FieldSize::TWO_BYTES; };
template <> struct FieldSizeForByteSize<4> { static constexpr FieldSize value = FieldSize::FOUR_BYTES; };
template <> struct FieldSizeForByteSize<8> { static constexpr FieldSize value = FieldSize::EIGHT_BYTES; };

template <typename T> struct FieldSizeForType {
  static constexpr FieldSize value =
      // Primitive types that aren't special-cased below can be determined from sizeof().
      kind<T>() == Kind::PRIMITIVE ? FieldSizeForByteSize<sizeof(T)>::value :
      kind<T>() == Kind::ENUM ? FieldSize::TWO_BYTES :
      kind<T>() == Kind::STRUCT ? FieldSize::INLINE_COMPOSITE :

      // Everything else is a pointer.
      FieldSize::POINTER;
};

// Void and bool are special.
template <> struct FieldSizeForType<Void> { static constexpr FieldSize value = FieldSize::VOID; };
template <> struct FieldSizeForType<bool> { static constexpr FieldSize value = FieldSize::BIT; };

// Lists and blobs are pointers, not structs.
template <typename T, bool b> struct FieldSizeForType<List<T, b>> {
  static constexpr FieldSize value = FieldSize::POINTER;
};
template <> struct FieldSizeForType<Text> {
  static constexpr FieldSize value = FieldSize::POINTER;
};
template <> struct FieldSizeForType<Data> {
  static constexpr FieldSize value = FieldSize::POINTER;
};

template <typename T>
class TemporaryPointer {
  // This class is a little hack which lets us define operator->() in cases where it needs to
  // return a pointer to a temporary value.  We instead construct a TemporaryPointer and return that
  // (by value).  The compiler then invokes operator->() on the TemporaryPointer, which itself is
  // able to return a real pointer to its member.

public:
  TemporaryPointer(T&& value): value(kj::mv(value)) {}
  TemporaryPointer(const T& value): value(value) {}

  inline T* operator->() { return &value; }
private:
  T value;
};

template <typename Container, typename Element>
class IndexingIterator {
public:
  IndexingIterator() = default;

  inline Element operator*() const { return (*container)[index]; }
  inline TemporaryPointer<Element> operator->() const {
    return TemporaryPointer<Element>((*container)[index]);
  }
  inline Element operator[]( int off) const { return (*container)[index]; }
  inline Element operator[](uint off) const { return (*container)[index]; }

  inline IndexingIterator& operator++() { ++index; return *this; }
  inline IndexingIterator operator++(int) { IndexingIterator other = *this; ++index; return other; }
  inline IndexingIterator& operator--() { --index; return *this; }
  inline IndexingIterator operator--(int) { IndexingIterator other = *this; --index; return other; }

  inline IndexingIterator operator+(uint amount) const { return IndexingIterator(container, index + amount); }
  inline IndexingIterator operator-(uint amount) const { return IndexingIterator(container, index - amount); }
  inline IndexingIterator operator+( int amount) const { return IndexingIterator(container, index + amount); }
  inline IndexingIterator operator-( int amount) const { return IndexingIterator(container, index - amount); }

  inline int operator-(const IndexingIterator& other) const { return index - other.index; }

  inline IndexingIterator& operator+=(uint amount) { index += amount; return *this; }
  inline IndexingIterator& operator-=(uint amount) { index -= amount; return *this; }
  inline IndexingIterator& operator+=( int amount) { index += amount; return *this; }
  inline IndexingIterator& operator-=( int amount) { index -= amount; return *this; }

  // STL says comparing iterators of different containers is not allowed, so we only compare
  // indices here.
  inline bool operator==(const IndexingIterator& other) const { return index == other.index; }
  inline bool operator!=(const IndexingIterator& other) const { return index != other.index; }
  inline bool operator<=(const IndexingIterator& other) const { return index <= other.index; }
  inline bool operator>=(const IndexingIterator& other) const { return index >= other.index; }
  inline bool operator< (const IndexingIterator& other) const { return index <  other.index; }
  inline bool operator> (const IndexingIterator& other) const { return index >  other.index; }

private:
  Container* container;
  uint index;

  friend Container;
  inline IndexingIterator(Container* container, uint index)
      : container(container), index(index) {}
};

}  // namespace _ (private)

template <typename T>
struct List<T, Kind::PRIMITIVE> {
  // List of primitives.

  List() = delete;

  class Reader {
  public:
    typedef List<T> Reads;

    Reader() = default;
    inline explicit Reader(_::ListReader reader): reader(reader) {}

    inline uint size() const { return reader.size() / ELEMENTS; }
    inline T operator[](uint index) const {
      return reader.template getDataElement<T>(index * ELEMENTS);
    }

    typedef _::IndexingIterator<const Reader, T> Iterator;
    inline Iterator begin() const { return Iterator(this, 0); }
    inline Iterator end() const { return Iterator(this, size()); }

  private:
    _::ListReader reader;
    template <typename U, Kind K>
    friend struct _::PointerHelpers;
    template <typename U, Kind K>
    friend struct List;
  };

  class Builder {
  public:
    typedef List<T> Builds;

    Builder() = default;
    inline explicit Builder(_::ListBuilder builder): builder(builder) {}

    inline operator Reader() { return Reader(builder.asReader()); }
    inline Reader asReader() { return Reader(builder.asReader()); }

    inline uint size() const { return builder.size() / ELEMENTS; }
    inline T operator[](uint index) {
      return builder.template getDataElement<T>(index * ELEMENTS);
    }
    inline void set(uint index, T value) {
      // Alas, it is not possible to make operator[] return a reference to which you can assign,
      // since the encoded representation does not necessarily match the compiler's representation
      // of the type.  We can't even return a clever class that implements operator T() and
      // operator=() because it will lead to surprising behavior when using type inference (e.g.
      // calling a template function with inferred argument types, or using "auto" or "decltype").

      builder.template setDataElement<T>(index * ELEMENTS, value);
    }

    typedef _::IndexingIterator<Builder, T> Iterator;
    inline Iterator begin() { return Iterator(this, 0); }
    inline Iterator end() { return Iterator(this, size()); }

  private:
    _::ListBuilder builder;
  };

private:
  inline static _::ListBuilder initAsElementOf(
      _::ListBuilder& builder, uint index, uint size) {
    return builder.initListElement(
        index * ELEMENTS, _::FieldSizeForType<T>::value, size * ELEMENTS);
  }
  inline static _::ListBuilder getAsElementOf(
      _::ListBuilder& builder, uint index) {
    return builder.getListElement(index * ELEMENTS, _::FieldSizeForType<T>::value);
  }
  inline static _::ListReader getAsElementOf(
      const _::ListReader& reader, uint index) {
    return reader.getListElement(index * ELEMENTS, _::FieldSizeForType<T>::value);
  }

  inline static _::ListBuilder initAsFieldOf(
      _::StructBuilder& builder, WirePointerCount index, uint size) {
    return builder.initListField(index, _::FieldSizeForType<T>::value, size * ELEMENTS);
  }
  inline static _::ListBuilder getAsFieldOf(
      _::StructBuilder& builder, WirePointerCount index, const word* defaultValue) {
    return builder.getListField(index, _::FieldSizeForType<T>::value, defaultValue);
  }
  inline static _::ListReader getAsFieldOf(
      const _::StructReader& reader, WirePointerCount index, const word* defaultValue) {
    return reader.getListField(index, _::FieldSizeForType<T>::value, defaultValue);
  }

  template <typename U, Kind k>
  friend struct List;
  template <typename U, Kind K>
  friend struct _::PointerHelpers;
};

template <typename T>
struct List<T, Kind::ENUM>: public List<T, Kind::PRIMITIVE> {};

template <typename T>
struct List<T, Kind::STRUCT> {
  // List of structs.

  List() = delete;

  class Reader {
  public:
    typedef List<T> Reads;

    Reader() = default;
    inline explicit Reader(_::ListReader reader): reader(reader) {}

    inline uint size() const { return reader.size() / ELEMENTS; }
    inline typename T::Reader operator[](uint index) const {
      return typename T::Reader(reader.getStructElement(index * ELEMENTS));
    }

    typedef _::IndexingIterator<const Reader, typename T::Reader> Iterator;
    inline Iterator begin() const { return Iterator(this, 0); }
    inline Iterator end() const { return Iterator(this, size()); }

  private:
    _::ListReader reader;
    template <typename U, Kind K>
    friend struct _::PointerHelpers;
    template <typename U, Kind K>
    friend struct List;
  };

  class Builder {
  public:
    typedef List<T> Builds;

    Builder() = default;
    inline explicit Builder(_::ListBuilder builder): builder(builder) {}

    inline operator Reader() { return Reader(builder.asReader()); }
    inline Reader asReader() { return Reader(builder.asReader()); }

    inline uint size() const { return builder.size() / ELEMENTS; }
    inline typename T::Builder operator[](uint index) {
      return typename T::Builder(builder.getStructElement(index * ELEMENTS));
    }

    // There are no init() or set() methods for lists of structs because the elements of the list
    // are inlined and are initialized when the list is initialized.  This means that init() would
    // be redundant, and set() would risk data loss if the input struct were from a newer version
    // of teh protocol.

    typedef _::IndexingIterator<Builder, typename T::Builder> Iterator;
    inline Iterator begin() { return Iterator(this, 0); }
    inline Iterator end() { return Iterator(this, size()); }

  private:
    _::ListBuilder builder;
  };

private:
  inline static _::ListBuilder initAsElementOf(
      _::ListBuilder& builder, uint index, uint size) {
    return builder.initStructListElement(
        index * ELEMENTS, size * ELEMENTS, _::structSize<T>());
  }
  inline static _::ListBuilder getAsElementOf(
      _::ListBuilder& builder, uint index) {
    return builder.getStructListElement(index * ELEMENTS, _::structSize<T>());
  }
  inline static _::ListReader getAsElementOf(
      const _::ListReader& reader, uint index) {
    return reader.getListElement(index * ELEMENTS, _::FieldSize::INLINE_COMPOSITE);
  }

  inline static _::ListBuilder initAsFieldOf(
      _::StructBuilder& builder, WirePointerCount index, uint size) {
    return builder.initStructListField(index, size * ELEMENTS, _::structSize<T>());
  }
  inline static _::ListBuilder getAsFieldOf(
      _::StructBuilder& builder, WirePointerCount index, const word* defaultValue) {
    return builder.getStructListField(index, _::structSize<T>(), defaultValue);
  }
  inline static _::ListReader getAsFieldOf(
      const _::StructReader& reader, WirePointerCount index, const word* defaultValue) {
    return reader.getListField(index, _::FieldSize::INLINE_COMPOSITE, defaultValue);
  }

  template <typename U, Kind k>
  friend struct List;
  template <typename U, Kind K>
  friend struct _::PointerHelpers;
};

template <typename T>
struct List<List<T>, Kind::LIST> {
  // List of lists.

  List() = delete;

  class Reader {
  public:
    typedef List<List<T>> Reads;

    Reader() = default;
    inline explicit Reader(_::ListReader reader): reader(reader) {}

    inline uint size() const { return reader.size() / ELEMENTS; }
    inline typename List<T>::Reader operator[](uint index) const {
      return typename List<T>::Reader(List<T>::getAsElementOf(reader, index));
    }

    typedef _::IndexingIterator<const Reader, typename List<T>::Reader> Iterator;
    inline Iterator begin() const { return Iterator(this, 0); }
    inline Iterator end() const { return Iterator(this, size()); }

  private:
    _::ListReader reader;
    template <typename U, Kind K>
    friend struct _::PointerHelpers;
    template <typename U, Kind K>
    friend struct List;
  };

  class Builder {
  public:
    typedef List<List<T>> Builds;

    Builder() = default;
    inline explicit Builder(_::ListBuilder builder): builder(builder) {}

    inline operator Reader() { return Reader(builder.asReader()); }
    inline Reader asReader() { return Reader(builder.asReader()); }

    inline uint size() const { return builder.size() / ELEMENTS; }
    inline typename List<T>::Builder operator[](uint index) {
      return typename List<T>::Builder(List<T>::getAsElementOf(builder, index));
    }
    inline typename List<T>::Builder init(uint index, uint size) {
      return typename List<T>::Builder(List<T>::initAsElementOf(builder, index, size));
    }
    inline void set(uint index, typename List<T>::Reader value) {
      builder.setListElement(index * ELEMENTS, value.reader);
    }
    void set(uint index, std::initializer_list<ReaderFor<T>> value) {
      auto l = init(index, value.size());
      uint i = 0;
      for (auto& element: value) {
        l.set(i++, element);
      }
    }

    typedef _::IndexingIterator<Builder, typename List<T>::Builder> Iterator;
    inline Iterator begin() { return Iterator(this, 0); }
    inline Iterator end() { return Iterator(this, size()); }

  private:
    _::ListBuilder builder;
  };

private:
  inline static _::ListBuilder initAsElementOf(
      _::ListBuilder& builder, uint index, uint size) {
    return builder.initListElement(
        index * ELEMENTS, _::FieldSize::POINTER, size * ELEMENTS);
  }
  inline static _::ListBuilder getAsElementOf(
      _::ListBuilder& builder, uint index) {
    return builder.getListElement(index * ELEMENTS, _::FieldSize::POINTER);
  }
  inline static _::ListReader getAsElementOf(
      const _::ListReader& reader, uint index) {
    return reader.getListElement(index * ELEMENTS, _::FieldSize::POINTER);
  }

  inline static _::ListBuilder initAsFieldOf(
      _::StructBuilder& builder, WirePointerCount index, uint size) {
    return builder.initListField(index, _::FieldSize::POINTER, size * ELEMENTS);
  }
  inline static _::ListBuilder getAsFieldOf(
      _::StructBuilder& builder, WirePointerCount index, const word* defaultValue) {
    return builder.getListField(index, _::FieldSize::POINTER, defaultValue);
  }
  inline static _::ListReader getAsFieldOf(
      const _::StructReader& reader, WirePointerCount index, const word* defaultValue) {
    return reader.getListField(index, _::FieldSize::POINTER, defaultValue);
  }

  template <typename U, Kind k>
  friend struct List;
  template <typename U, Kind K>
  friend struct _::PointerHelpers;
};

template <typename T>
struct List<T, Kind::BLOB> {
  List() = delete;

  class Reader {
  public:
    typedef List<T> Reads;

    Reader() = default;
    inline explicit Reader(_::ListReader reader): reader(reader) {}

    inline uint size() const { return reader.size() / ELEMENTS; }
    inline typename T::Reader operator[](uint index) const {
      return reader.getBlobElement<T>(index * ELEMENTS);
    }

    typedef _::IndexingIterator<const Reader, typename T::Reader> Iterator;
    inline Iterator begin() const { return Iterator(this, 0); }
    inline Iterator end() const { return Iterator(this, size()); }

  private:
    _::ListReader reader;
    template <typename U, Kind K>
    friend struct _::PointerHelpers;
    template <typename U, Kind K>
    friend struct List;
  };

  class Builder {
  public:
    typedef List<T> Builds;

    Builder() = default;
    inline explicit Builder(_::ListBuilder builder): builder(builder) {}

    inline operator Reader() { return Reader(builder.asReader()); }
    inline Reader asReader() { return Reader(builder.asReader()); }

    inline uint size() const { return builder.size() / ELEMENTS; }
    inline typename T::Builder operator[](uint index) {
      return builder.getBlobElement<T>(index * ELEMENTS);
    }
    inline void set(uint index, typename T::Reader value) {
      builder.setBlobElement<T>(index * ELEMENTS, value);
    }
    inline typename T::Builder init(uint index, uint size) {
      return builder.initBlobElement<T>(index * ELEMENTS, size * BYTES);
    }

    typedef _::IndexingIterator<Builder, typename T::Builder> Iterator;
    inline Iterator begin() { return Iterator(this, 0); }
    inline Iterator end() { return Iterator(this, size()); }

  private:
    _::ListBuilder builder;
  };

private:
  inline static _::ListBuilder initAsElementOf(
      _::ListBuilder& builder, uint index, uint size) {
    return builder.initListElement(
        index * ELEMENTS, _::FieldSize::POINTER, size * ELEMENTS);
  }
  inline static _::ListBuilder getAsElementOf(
      _::ListBuilder& builder, uint index) {
    return builder.getListElement(index * ELEMENTS, _::FieldSize::POINTER);
  }
  inline static _::ListReader getAsElementOf(
      const _::ListReader& reader, uint index) {
    return reader.getListElement(index * ELEMENTS, _::FieldSize::POINTER);
  }

  inline static _::ListBuilder initAsFieldOf(
      _::StructBuilder& builder, WirePointerCount index, uint size) {
    return builder.initListField(index, _::FieldSize::POINTER, size * ELEMENTS);
  }
  inline static _::ListBuilder getAsFieldOf(
      _::StructBuilder& builder, WirePointerCount index, const word* defaultValue) {
    return builder.getListField(index, _::FieldSize::POINTER, defaultValue);
  }
  inline static _::ListReader getAsFieldOf(
      const _::StructReader& reader, WirePointerCount index, const word* defaultValue) {
    return reader.getListField(index, _::FieldSize::POINTER, defaultValue);
  }

  template <typename U, Kind k>
  friend struct List;
  template <typename U, Kind K>
  friend struct _::PointerHelpers;
};

}  // namespace capnp

#endif  // CAPNP_LIST_H_
