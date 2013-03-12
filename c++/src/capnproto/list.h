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

namespace capnproto {

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

template <typename Container, typename Element, typename Reference>
class IndexingIterator {
public:
  IndexingIterator() = default;

  inline Reference operator*() { return (*container)[index]; }
  inline Reference operator->() { return TemporaryPointer<Element>((*container)[index]); }
  inline Reference operator[]( int off) { return (*container)[index]; }
  inline Reference operator[](uint off) { return (*container)[index]; }

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
  IndexingIterator(Container* builder, uint index): container(container), index(index) {}
};

}  // namespace internal

template <typename T, bool isPrimitive = internal::IsPrimitive<T>::value>
struct List;

template <typename T>
struct List<T, true> {
  class Reader {
  public:
    Reader() = default;
    inline explicit Reader(internal::ListReader reader): reader(reader) {}

    typedef internal::IndexingIterator<Reader, T, T> iterator;

    inline T operator[](uint index) { return reader.template getDataElement<T>(index * ELEMENTS); }
    inline uint size() { return reader.size() / ELEMENTS; }

    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

  private:
    internal::ListReader reader;
  };

  class Builder {
  public:
    Builder() = default;
    inline explicit Builder(internal::ListBuilder builder): builder(builder) {}

    class reference {
    public:
      reference() = default;

      inline operator T() { return (*builder)[index]; }
      inline reference& operator=(T value) {
        builder->builder.template setDataElement<T>(index * ELEMENTS, value);
        return *this;
      }

      T* operator&() {
        static_assert(sizeof(T) < 0,
            "You can't take the address of a list member because they are not stored in memory "
            "in a directly-usable format.");
        return nullptr;
      }

    private:
      Builder* builder;
      uint index;

      friend class Builder;
      reference(Builder* builder, uint index): builder(builder), index(index) {}
    };

    typedef internal::IndexingIterator<Builder, T, reference> iterator;

    inline reference operator[](uint index) { return reference(this, index); }
    inline uint size() { return builder.size() / ELEMENTS; }

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

    typedef internal::IndexingIterator<Reader, typename T::Reader, typename T::Reader> iterator;

    inline typename T::Reader operator[](uint index) {
      return typename T::Reader(reader.getStructElement(index * ELEMENTS, T::DEFAULT.words));
    }
    inline uint size() { return reader.size() / ELEMENTS; }

    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

  private:
    internal::ListReader reader;
  };

  class Builder {
  public:
    Builder() = default;
    inline explicit Builder(internal::ListBuilder builder): builder(builder) {}

    class reference {
    public:
      reference() = default;

      inline operator typename T::Builder() { return (*builder)[index]; }

      // TODO:  operator= to accept ownership transfer.

      T* operator&() {
        static_assert(sizeof(T) < 0,
            "You can't take the address of a list member because they are not stored in memory "
            "in a directly-usable format.");
        return nullptr;
      }

    private:
      Builder* builder;
      uint index;

      friend class Builder;
      reference(Builder* builder, uint index): builder(builder), index(index) {}
    };

    typedef internal::IndexingIterator<Builder, typename T::Builder, reference> iterator;

    inline typename T::Builder operator[](uint index) {
      return typename T::Builder(builder.getStructElement(index * ELEMENTS,
          (T::DATA_SIZE + T::REFERENCE_COUNT * WORDS_PER_REFERENCE) / ELEMENTS,
          T::DATA_SIZE));
    }
    inline uint size() { return builder.size() / ELEMENTS; }

    inline iterator begin() { return iterator(this, 0); }
    inline iterator end() { return iterator(this, size()); }

  private:
    internal::ListBuilder builder;
  };
};

}  // namespace capnproto

#endif  // CAPNPROTO_LIST_H_
