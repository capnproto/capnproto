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

#ifndef CAPNPROTO_LIST_H_
#define CAPNPROTO_LIST_H_

#include "layout.h"
#include <initializer_list>

namespace capnproto {
namespace internal {

template <typename T, Kind kind = kind<T>()>
struct MaybeReaderBuilder {
  typedef typename T::Reader Reader;
  typedef typename T::Builder Builder;
};
template <typename T>
struct MaybeReaderBuilder<T, Kind::PRIMITIVE> {
  typedef T Reader;
  typedef T Builder;
};

template <typename t>
struct PointerHelpers;

}  // namespace internal

template <typename T, Kind kind = kind<T>()>
struct List;

template <typename T>
using ReaderFor = typename internal::MaybeReaderBuilder<T>::Reader;
// The type returned by List<T>::Reader::operator[].

template <typename T>
using BuilderFor = typename internal::MaybeReaderBuilder<T>::Builder;
// The type returned by List<T>::Builder::operator[].

namespace internal {

template <typename T, Kind k> struct KindOf<List<T, k>> { static constexpr Kind kind = Kind::LIST; };

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
      FieldSize::REFERENCE;
};

// Void and bool are special.
template <> struct FieldSizeForType<Void> { static constexpr FieldSize value = FieldSize::VOID; };
template <> struct FieldSizeForType<bool> { static constexpr FieldSize value = FieldSize::BIT; };

// Lists and blobs are references, not structs.
template <typename T, bool b> struct FieldSizeForType<List<T, b>> {
  static constexpr FieldSize value = FieldSize::REFERENCE;
};
template <> struct FieldSizeForType<Text> {
  static constexpr FieldSize value = FieldSize::REFERENCE;
};
template <> struct FieldSizeForType<Data> {
  static constexpr FieldSize value = FieldSize::REFERENCE;
};

template <typename T>
class TemporaryPointer {
  // This class is a little hack which lets us define operator->() in cases where it needs to
  // return a pointer to a temporary value.  We instead construct a TemporaryPointer and return that
  // (by value).  The compiler then invokes operator->() on the TemporaryPointer, which itself is
  // able to return a real pointer to its member.

public:
  TemporaryPointer(T&& value): value(move(value)) {}
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
  inline IndexingIterator(Container* container, uint index): container(container), index(index) {}
};

}  // namespace internal

template <typename T>
struct List<T, Kind::PRIMITIVE> {
  // List of primitives.

  class Reader {
  public:
    Reader() = default;
    inline explicit Reader(internal::ListReader reader): reader(reader) {}

    inline uint size() { return reader.size() / ELEMENTS; }
    inline T operator[](uint index) { return reader.template getDataElement<T>(index * ELEMENTS); }

    typedef internal::IndexingIterator<Reader, T> iterator;
    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

  private:
    internal::ListReader reader;
  };

  class Builder {
  public:
    Builder() = default;
    inline explicit Builder(internal::ListBuilder builder): builder(builder) {}

    inline uint size() { return builder.size() / ELEMENTS; }
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

    typedef internal::IndexingIterator<Builder, T> iterator;
    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

    template <typename Other>
    void copyFrom(const Other& other) {
      auto i = other.begin();
      auto end = other.end();
      uint pos = 0;
      for (; i != end && pos < size(); ++i) {
        set(pos, *i);
      }
      CAPNPROTO_INLINE_DPRECOND(pos == size() && i == end,
                               "List::copyFrom() argument had different size.");
    }
    void copyFrom(std::initializer_list<T> other) {
      CAPNPROTO_INLINE_DPRECOND(other.size() == size(),
                               "List::copyFrom() argument had different size.");
      for (uint i = 0; i < other.size(); i++) {
        set(i, other.begin()[i]);
      }
    }

  private:
    internal::ListBuilder builder;
  };

private:
  inline static internal::ListBuilder initAsElementOf(
      internal::ListBuilder& builder, uint index, uint size) {
    return builder.initListElement(
        index * ELEMENTS, internal::FieldSizeForType<T>::value, size * ELEMENTS);
  }
  inline static internal::ListBuilder getAsElementOf(
      internal::ListBuilder& builder, uint index) {
    return builder.getListElement(index * ELEMENTS);
  }
  inline static internal::ListReader getAsElementOf(
      internal::ListReader& reader, uint index) {
    return reader.getListElement(index * ELEMENTS, internal::FieldSizeForType<T>::value);
  }

  inline static internal::ListBuilder initAsFieldOf(
      internal::StructBuilder& builder, WireReferenceCount index, uint size) {
    return builder.initListField(index, internal::FieldSizeForType<T>::value, size * ELEMENTS);
  }
  inline static internal::ListBuilder getAsFieldOf(
      internal::StructBuilder& builder, WireReferenceCount index) {
    return builder.getListField(index, nullptr);
  }
  inline static internal::ListReader getAsFieldOf(
      internal::StructReader& reader, WireReferenceCount index) {
    return reader.getListField(index, internal::FieldSizeForType<T>::value, nullptr);
  }

  template <typename U, Kind k>
  friend class List;
  template <typename U>
  friend struct internal::PointerHelpers;
};

template <typename T>
struct List<T, Kind::ENUM>: public List<T, Kind::PRIMITIVE> {};

template <typename T>
struct List<T, Kind::STRUCT> {
  // List of structs.

  class Reader {
  public:
    Reader() = default;
    inline explicit Reader(internal::ListReader reader): reader(reader) {}

    inline uint size() { return reader.size() / ELEMENTS; }
    inline typename T::Reader operator[](uint index) {
      return typename T::Reader(reader.getStructElement(index * ELEMENTS));
    }

    typedef internal::IndexingIterator<Reader, typename T::Reader> iterator;
    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

  private:
    internal::ListReader reader;
  };

  class Builder {
  public:
    Builder() = default;
    inline explicit Builder(internal::ListBuilder builder): builder(builder) {}

    inline uint size() { return builder.size() / ELEMENTS; }
    inline typename T::Builder operator[](uint index) {
      return typename T::Builder(builder.getStructElement(
          index * ELEMENTS, internal::structSize<T>()));
    }

    typedef internal::IndexingIterator<Builder, typename T::Builder> iterator;
    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

    template <typename Other>
    void copyFrom(const Other& other);
    void copyFrom(std::initializer_list<typename T::Reader> other);
    // TODO

  private:
    internal::ListBuilder builder;
  };

private:
  inline static internal::ListBuilder initAsElementOf(
      internal::ListBuilder& builder, uint index, uint size) {
    return builder.initStructListElement(
        index * ELEMENTS, size * ELEMENTS, internal::structSize<T>());
  }
  inline static internal::ListBuilder getAsElementOf(
      internal::ListBuilder& builder, uint index) {
    return builder.getListElement(index * ELEMENTS);
  }
  inline static internal::ListReader getAsElementOf(
      internal::ListReader& reader, uint index) {
    return reader.getListElement(index * ELEMENTS, internal::FieldSize::INLINE_COMPOSITE);
  }

  inline static internal::ListBuilder initAsFieldOf(
      internal::StructBuilder& builder, WireReferenceCount index, uint size) {
    return builder.initStructListField(index, size * ELEMENTS, internal::structSize<T>());
  }
  inline static internal::ListBuilder getAsFieldOf(
      internal::StructBuilder& builder, WireReferenceCount index) {
    return builder.getListField(index, nullptr);
  }
  inline static internal::ListReader getAsFieldOf(
      internal::StructReader& reader, WireReferenceCount index) {
    return reader.getListField(index, internal::FieldSize::INLINE_COMPOSITE, nullptr);
  }

  template <typename U, Kind k>
  friend class List;
  template <typename U>
  friend struct internal::PointerHelpers;
};

template <typename T>
struct List<List<T>, Kind::LIST> {
  // List of lists.

  class Reader {
  public:
    Reader() = default;
    inline explicit Reader(internal::ListReader reader): reader(reader) {}

    inline uint size() { return reader.size() / ELEMENTS; }
    inline typename List<T>::Reader operator[](uint index) {
      return typename List<T>::Reader(List<T>::getAsElementOf(reader, index));
    }

    typedef internal::IndexingIterator<Reader, typename List<T>::Reader> iterator;
    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

  private:
    internal::ListReader reader;
  };

  class Builder {
  public:
    Builder() = default;
    inline explicit Builder(internal::ListBuilder builder): builder(builder) {}

    inline uint size() { return builder.size() / ELEMENTS; }
    inline typename List<T>::Builder operator[](uint index) {
      return typename List<T>::Builder(List<T>::getAsElementOf(builder, index));
    }
    inline typename List<T>::Builder init(uint index, uint size) {
      return typename List<T>::Builder(List<T>::initAsElementOf(builder, index, size));
    }

    typedef internal::IndexingIterator<Builder, typename List<T>::Builder> iterator;
    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

    template <typename Other>
    void copyFrom(const Other& other);
    void copyFrom(std::initializer_list<typename List<T>::Reader> other);
    // TODO

  private:
    internal::ListBuilder builder;
  };

private:
  inline static internal::ListBuilder initAsElementOf(
      internal::ListBuilder& builder, uint index, uint size) {
    return builder.initListElement(
        index * ELEMENTS, internal::FieldSize::REFERENCE, size * ELEMENTS);
  }
  inline static internal::ListBuilder getAsElementOf(
      internal::ListBuilder& builder, uint index) {
    return builder.getListElement(index * ELEMENTS);
  }
  inline static internal::ListReader getAsElementOf(
      internal::ListReader& reader, uint index) {
    return reader.getListElement(index * ELEMENTS, internal::FieldSize::REFERENCE);
  }

  inline static internal::ListBuilder initAsFieldOf(
      internal::StructBuilder& builder, WireReferenceCount index, uint size) {
    return builder.initListField(index, internal::FieldSize::REFERENCE, size * ELEMENTS);
  }
  inline static internal::ListBuilder getAsFieldOf(
      internal::StructBuilder& builder, WireReferenceCount index) {
    return builder.getListField(index, nullptr);
  }
  inline static internal::ListReader getAsFieldOf(
      internal::StructReader& reader, WireReferenceCount index) {
    return reader.getListField(index, internal::FieldSize::REFERENCE, nullptr);
  }

  template <typename U, Kind k>
  friend class List;
  template <typename U>
  friend struct internal::PointerHelpers;
};

template <typename T>
struct List<T, Kind::BLOB> {
  class Reader {
  public:
    Reader() = default;
    inline explicit Reader(internal::ListReader reader): reader(reader) {}

    inline uint size() { return reader.size() / ELEMENTS; }
    inline typename T::Reader operator[](uint index) {
      return reader.getBlobElement<T>(index * ELEMENTS);
    }

    typedef internal::IndexingIterator<Reader, typename T::Reader> iterator;
    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

  private:
    internal::ListReader reader;
  };

  class Builder {
  public:
    Builder() = default;
    inline explicit Builder(internal::ListBuilder builder): builder(builder) {}

    inline uint size() { return builder.size() / ELEMENTS; }
    inline typename T::Builder operator[](uint index) {
      return builder.getBlobElement<T>(index * ELEMENTS);
    }
    inline void set(uint index, typename T::Reader value) {
      builder.setBlobElement<T>(index * ELEMENTS, value);
    }
    inline typename T::Builder init(uint index, uint size) {
      return builder.initBlobElement<T>(index * ELEMENTS, size * BYTES);
    }

    typedef internal::IndexingIterator<Builder, typename T::Builder> iterator;
    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

    template <typename Other>
    void copyFrom(const Other& other) {
      auto i = other.begin();
      auto end = other.end();
      uint pos = 0;
      for (; i != end && pos < size(); ++i) {
        set(pos, *i);
      }
      CAPNPROTO_INLINE_DPRECOND(pos == size() && i == end,
                                "List::copyFrom() argument had different size.");
    }
    void copyFrom(std::initializer_list<typename T::Reader> other) {
      CAPNPROTO_INLINE_DPRECOND(other.size() == size(),
                                "List::copyFrom() argument had different size.");
      for (uint i = 0; i < other.size(); i++) {
        set(i, other.begin()[i]);
      }
    }

  private:
    internal::ListBuilder builder;
  };

private:
  inline static internal::ListBuilder initAsElementOf(
      internal::ListBuilder& builder, uint index, uint size) {
    return builder.initListElement(
        index * ELEMENTS, internal::FieldSize::REFERENCE, size * ELEMENTS);
  }
  inline static internal::ListBuilder getAsElementOf(
      internal::ListBuilder& builder, uint index) {
    return builder.getListElement(index * ELEMENTS);
  }
  inline static internal::ListReader getAsElementOf(
      internal::ListReader& reader, uint index) {
    return reader.getListElement(index * ELEMENTS, internal::FieldSize::REFERENCE);
  }

  inline static internal::ListBuilder initAsFieldOf(
      internal::StructBuilder& builder, WireReferenceCount index, uint size) {
    return builder.initListField(index, internal::FieldSize::REFERENCE, size * ELEMENTS);
  }
  inline static internal::ListBuilder getAsFieldOf(
      internal::StructBuilder& builder, WireReferenceCount index) {
    return builder.getListField(index, nullptr);
  }
  inline static internal::ListReader getAsFieldOf(
      internal::StructReader& reader, WireReferenceCount index) {
    return reader.getListField(index, internal::FieldSize::REFERENCE, nullptr);
  }

  template <typename U, Kind k>
  friend class List;
  template <typename U>
  friend struct internal::PointerHelpers;
};

}  // namespace capnproto

#endif  // CAPNPROTO_LIST_H_
