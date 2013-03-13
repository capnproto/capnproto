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

#include "wire-format.h"
#include "descriptor.h"     // only for FieldSize; TODO:  Eliminate this

namespace capnproto {

namespace internal {
template <typename T> struct IsPrimitive;
}  // namespace internal

template <typename T, bool isPrimitive = internal::IsPrimitive<T>::value>
struct List;

namespace internal {

template <typename T> struct IsPrimitive { static constexpr bool value = false; };

template <> struct IsPrimitive<void>     { static constexpr bool value = true; };
template <> struct IsPrimitive<bool>     { static constexpr bool value = true; };
template <> struct IsPrimitive<int8_t>   { static constexpr bool value = true; };
template <> struct IsPrimitive<int16_t>  { static constexpr bool value = true; };
template <> struct IsPrimitive<int32_t>  { static constexpr bool value = true; };
template <> struct IsPrimitive<int64_t>  { static constexpr bool value = true; };
template <> struct IsPrimitive<uint8_t>  { static constexpr bool value = true; };
template <> struct IsPrimitive<uint16_t> { static constexpr bool value = true; };
template <> struct IsPrimitive<uint32_t> { static constexpr bool value = true; };
template <> struct IsPrimitive<uint64_t> { static constexpr bool value = true; };
template <> struct IsPrimitive<float>    { static constexpr bool value = true; };
template <> struct IsPrimitive<double>   { static constexpr bool value = true; };
template <typename T, bool b> struct IsPrimitive<List<T, b>> {
  static constexpr bool value = IsPrimitive<T>::value;
};

template <typename T> struct FieldSizeForType { static constexpr FieldSize value = FieldSize::INLINE_COMPOSITE; };

template <> struct FieldSizeForType<void>     { static constexpr FieldSize value = FieldSize::VOID; };
template <> struct FieldSizeForType<bool>     { static constexpr FieldSize value = FieldSize::BIT; };
template <> struct FieldSizeForType<int8_t>   { static constexpr FieldSize value = FieldSize::BYTE; };
template <> struct FieldSizeForType<int16_t>  { static constexpr FieldSize value = FieldSize::TWO_BYTES; };
template <> struct FieldSizeForType<int32_t>  { static constexpr FieldSize value = FieldSize::FOUR_BYTES; };
template <> struct FieldSizeForType<int64_t>  { static constexpr FieldSize value = FieldSize::EIGHT_BYTES; };
template <> struct FieldSizeForType<uint8_t>  { static constexpr FieldSize value = FieldSize::BYTE; };
template <> struct FieldSizeForType<uint16_t> { static constexpr FieldSize value = FieldSize::TWO_BYTES; };
template <> struct FieldSizeForType<uint32_t> { static constexpr FieldSize value = FieldSize::FOUR_BYTES; };
template <> struct FieldSizeForType<uint64_t> { static constexpr FieldSize value = FieldSize::EIGHT_BYTES; };
template <> struct FieldSizeForType<float>    { static constexpr FieldSize value = FieldSize::FOUR_BYTES; };
template <> struct FieldSizeForType<double>   { static constexpr FieldSize value = FieldSize::EIGHT_BYTES; };
template <typename T, bool b> struct FieldSizeForType<List<T, b>> {
  static constexpr FieldSize value = FieldSize::REFERENCE;
};

template<typename T> constexpr T&& move(T& t) noexcept { return static_cast<T&&>(t); }
// Like std::move.  Unfortunately, #including <utility> brings in tons of unnecessary stuff.

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

  inline Element operator*() { return (*container)[index]; }
  inline TemporaryPointer<Element> operator->() {
    return TemporaryPointer<Element>((*container)[index]);
  }
  inline Element operator[]( int off) { return (*container)[index]; }
  inline Element operator[](uint off) { return (*container)[index]; }

  inline IndexingIterator& operator++() { ++index; return *this; }
  inline IndexingIterator operator++(int) { IndexingIterator other = *this; ++index; return other; }
  inline IndexingIterator& operator--() { --index; return *this; }
  inline IndexingIterator operator--(int) { IndexingIterator other = *this; --index; return other; }

  inline IndexingIterator operator+(uint amount) { return IndexingIterator(container, index + amount); }
  inline IndexingIterator operator-(uint amount) { return IndexingIterator(container, index - amount); }
  inline IndexingIterator operator+( int amount) { return IndexingIterator(container, index + amount); }
  inline IndexingIterator operator-( int amount) { return IndexingIterator(container, index - amount); }

  inline int operator-(const IndexingIterator& other) { return index - other.index; }

  inline IndexingIterator& operator+=(uint amount) { index += amount; return *this; }
  inline IndexingIterator& operator-=(uint amount) { index -= amount; return *this; }
  inline IndexingIterator& operator+=( int amount) { index += amount; return *this; }
  inline IndexingIterator& operator-=( int amount) { index -= amount; return *this; }

  // STL says comparing iterators of different containers is not allowed, so we only compare
  // indices here.
  inline bool operator==(const IndexingIterator& other) { return index == other.index; }
  inline bool operator!=(const IndexingIterator& other) { return index != other.index; }
  inline bool operator<=(const IndexingIterator& other) { return index <= other.index; }
  inline bool operator>=(const IndexingIterator& other) { return index >= other.index; }
  inline bool operator< (const IndexingIterator& other) { return index <  other.index; }
  inline bool operator> (const IndexingIterator& other) { return index >  other.index; }

private:
  Container* container;
  uint index;

  friend Container;
  inline IndexingIterator(Container* container, uint index): container(container), index(index) {}
};

}  // namespace internal

template <typename T>
struct List<T, true> {
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

  private:
    internal::ListBuilder builder;
  };
};

template <typename T>
struct List<T, false> {
  class Reader {
  public:
    Reader() = default;
    inline explicit Reader(internal::ListReader reader): reader(reader) {}

    inline uint size() { return reader.size() / ELEMENTS; }
    inline typename T::Reader operator[](uint index) {
      return typename T::Reader(reader.getStructElement(index * ELEMENTS, T::DEFAULT.words));
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
      return typename T::Builder(builder.getStructElement(index * ELEMENTS,
          (T::DATA_SIZE + T::REFERENCE_COUNT * WORDS_PER_REFERENCE) / ELEMENTS,
          T::DATA_SIZE));
    }

    typedef internal::IndexingIterator<Builder, typename T::Builder> iterator;
    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

  private:
    internal::ListBuilder builder;
  };
};

template <typename T>
struct List<List<T>, true> {
  class Reader {
  public:
    Reader() = default;
    inline explicit Reader(internal::ListReader reader): reader(reader) {}

    inline uint size() { return reader.size() / ELEMENTS; }
    inline typename List<T>::Reader operator[](uint index) {
      return typename List<T>::Reader(reader.getListElement(index * REFERENCES,
          internal::FieldSizeForType<T>::value));
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
      return typename List<T>::Builder(builder.getListElement(index * REFERENCES));
    }
    inline typename List<T>::Builder init(uint index, uint size) {
      return typename List<T>::Builder(builder.initListElement(
          index * REFERENCES, internal::FieldSizeForType<T>::value, size * ELEMENTS));
    }

    typedef internal::IndexingIterator<Builder, typename List<T>::Builder> iterator;
    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

  private:
    internal::ListBuilder builder;
  };
};

template <typename T>
struct List<List<T>, false> {
  class Reader {
  public:
    Reader() = default;
    inline explicit Reader(internal::ListReader reader): reader(reader) {}

    inline uint size() { return reader.size() / ELEMENTS; }
    inline typename List<T>::Reader operator[](uint index) {
      return typename List<T>::Reader(reader.getListElement(index * REFERENCES,
          internal::FieldSizeForType<T>::value));
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
      return typename List<T>::Builder(builder.getListElement(index * REFERENCES));
    }
    inline typename List<T>::Builder init(uint index, uint size) {
      return typename List<T>::Builder(builder.initStructListElement(
          index * REFERENCES, size * ELEMENTS, T::DEFAULT.words));
    }

    typedef internal::IndexingIterator<Builder, typename List<T>::Builder> iterator;
    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

  private:
    internal::ListBuilder builder;
  };
};

}  // namespace capnproto

#endif  // CAPNPROTO_LIST_H_
