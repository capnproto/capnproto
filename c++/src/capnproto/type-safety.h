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

// This file contains types which are intended to help detect incorrect usage at compile
// time, but should then be optimized down to basic primitives (usually, integers) by the
// compiler.

#ifndef CAPNPROTO_TYPE_SAFETY_H_
#define CAPNPROTO_TYPE_SAFETY_H_

#include "macros.h"
#include <cstddef>
#include <string.h>

namespace capnproto {

typedef unsigned int uint;

enum class Void {
  // Type used for Void fields.  There is only one value.  Using C++'s "void" type creates a bunch
  // of issues since it behaves differently from other types.
  VOID
};
template <typename T>
inline T& operator<<(T& os, Void) { return os << "void"; }

template <typename T>
struct NoInfer {
  // Use NoInfer<T>::Type in place of T for a template function parameter to prevent inference of
  // the type based on the parameter value.  There's something in the standard library for this but
  // I didn't want to #include type_traits or whatever.
  typedef T Type;
};

template <typename T> struct RemoveReference_ { typedef T Type; };
template <typename T> struct RemoveReference_<T&> { typedef T Type; };
template <typename> struct IsLvalueReference { static constexpr bool value = false; };
template <typename T> struct IsLvalueReference<T&> { static constexpr bool value = true; };

template <typename T>
using RemoveReference = typename RemoveReference_<T>::Type;

// #including <utility> just for std::move() and std::forward() is excessive.  Instead, we
// re-define them here.

template<typename T> constexpr T&& move(T& t) noexcept { return static_cast<T&&>(t); }

template<typename T>
constexpr T&& forward(RemoveReference<T>& t) noexcept {
  return static_cast<T&&>(t);
}
template<typename T> constexpr T&& forward(RemoveReference<T>&& t) noexcept {
  static_assert(!IsLvalueReference<T>::value, "Attempting to forward rvalue as lvalue reference.");
  return static_cast<T&&>(t);
}

template <typename T>
T instance() noexcept;
// Like std::declval, but doesn't transform T into an rvalue reference.  If you want that, specify
// instance<T&&>().

// #including <new> pulls in a lot of crap, but we want placement news.  But operator new cannot
// be defined in a namespace, and defining it globally conflicts with the standard library
// definition.  So...

namespace internal {
struct PlacementNew {};
}  // namespace internal;
} // namespace capnproto

inline void* operator new(std::size_t, capnproto::internal::PlacementNew, void* __p) noexcept {
  return __p;
}

namespace capnproto {

template <typename T, typename... Params>
void constructAt(T* location, Params&&... params) {
  new (internal::PlacementNew(), location) T(capnproto::forward<Params>(params)...);
}

// =======================================================================================
// Maybe

template <typename T>
class Maybe {
public:
  Maybe(): isSet(false) {}
  Maybe(T&& t)
      : isSet(true) {
    constructAt(&value, capnproto::move(t));
  }
  Maybe(const T& t)
      : isSet(true) {
    constructAt(&value, t);
  }
  Maybe(Maybe&& other) noexcept(noexcept(T(capnproto::move(other.value))))
      : isSet(other.isSet) {
    if (isSet) {
      constructAt(&value, capnproto::move(other.value));
    }
  }
  Maybe(const Maybe& other)
      : isSet(other.isSet) {
    if (isSet) {
      constructAt(&value, other.value);
    }
  }
  template <typename U>
  Maybe(Maybe<U>&& other) noexcept(noexcept(T(capnproto::move(other.value))))
      : isSet(other.isSet) {
    if (isSet) {
      constructAt(&value, capnproto::move(other.value));
    }
  }
  template <typename U>
  Maybe(const Maybe<U>& other)
      : isSet(other.isSet) {
    if (isSet) {
      constructAt(&value, other.value);
    }
  }
  template <typename U>
  Maybe(const Maybe<U&>& other)
      : isSet(other.isSet) {
    if (isSet) {
      constructAt(&value, *other.ptr);
    }
  }
  Maybe(std::nullptr_t): isSet(false) {}

  ~Maybe() {
    if (isSet) {
      value.~T();
    }
  }

  template <typename... Params>
  inline void init(Params&&... params) {
    if (isSet) {
      value.~T();
    }
    isSet = true;
    constructAt(&value, capnproto::forward(params)...);
  }

  inline T& operator*() { return value; }
  inline const T& operator*() const { return value; }
  inline T* operator->() { return &value; }
  inline const T* operator->() const { return &value; }

  inline Maybe& operator=(Maybe&& other) {
    if (&other != this) {
      if (isSet) {
        value.~T();
      }
      isSet = other.isSet;
      if (isSet) {
        constructAt(&value, capnproto::move(other.value));
      }
    }
    return *this;
  }

  inline Maybe& operator=(const Maybe& other) {
    if (&other != this) {
      if (isSet) {
        value.~T();
      }
      isSet = other.isSet;
      if (isSet) {
        constructAt(&value, other.value);
      }
    }
    return *this;
  }

  bool operator==(const Maybe& other) const {
    if (isSet == other.isSet) {
      if (isSet) {
        return value == other.value;
      } else {
        return true;
      }
    }
    return false;
  }
  inline bool operator!=(const Maybe& other) const { return !(*this == other); }

  inline bool operator==(std::nullptr_t) const { return !isSet; }
  inline bool operator!=(std::nullptr_t) const { return isSet; }

  template <typename Func>
  auto map(const Func& func) const -> Maybe<decltype(func(instance<const T&>()))> {
    // Construct a new Maybe by applying the given function to the Maybe's value.

    if (isSet) {
      return func(value);
    } else {
      return nullptr;
    }
  }

  template <typename Func>
  auto map(const Func& func) -> Maybe<decltype(func(instance<T&>()))> {
    // Construct a new Maybe by applying the given function to the Maybe's value.

    if (isSet) {
      return func(value);
    } else {
      return nullptr;
    }
  }

  template <typename Func>
  auto moveMap(const Func& func) -> Maybe<decltype(func(instance<T&&>()))> {
    // Like map() but allows the function to take an rvalue reference to the value.

    if (isSet) {
      return func(capnproto::move(value));
    } else {
      return nullptr;
    }
  }

private:
  bool isSet;
  union {
    T value;
  };

  template <typename U>
  friend class Maybe;
};

template <typename T>
class Maybe<T&> {
public:
  Maybe(): ptr(nullptr) {}
  Maybe(T& t): ptr(&t) {}
  Maybe(std::nullptr_t): ptr(nullptr) {}
  template <typename U>
  Maybe(const Maybe<U>& other) {
    if (other == nullptr) {
      ptr = nullptr;
    } else {
      ptr = other.operator->();
    }
  }

  ~Maybe() noexcept {}

  inline T& operator*() { return *ptr; }
  inline const T& operator*() const { return *ptr; }
  inline T* operator->() { return ptr; }
  inline const T* operator->() const { return ptr; }

  inline bool operator==(const Maybe& other) const { return ptr == other.ptr; }
  inline bool operator!=(const Maybe& other) const { return ptr != other.ptr; }
  inline bool operator==(std::nullptr_t) const { return ptr == nullptr; }
  inline bool operator!=(std::nullptr_t) const { return ptr != nullptr; }

private:
  T* ptr;

  template <typename U>
  friend class Maybe;
};

// =======================================================================================
// Own<T> -- An owned pointer.

class Disposer {
  // Abstract interface for a thing that disposes of some other object.  Often, it makes sense to
  // decouple an object from the knowledge of how to dispose of it.

protected:
  virtual ~Disposer();

public:
  virtual void dispose(void* interiorPointer) = 0;
  // Disposes of the object that this Disposer owns, and possibly disposes of the disposer itself.
  //
  // Callers must assume that the Disposer itself is no longer valid once this returns -- e.g. it
  // might delete itself.  Callers must in particular be sure not to call the Disposer again even
  // when dispose() throws an exception.
  //
  // `interiorPointer` points somewhere inside of the object -- NOT necessarily at the beginning,
  // especially in the presence of multiple inheritance.  Most implementations should ignore the
  // pointer, though a tricky memory allocator could get away with sharing one Disposer among
  // multiple objects if it can figure out how to find the beginning of the object given an
  // arbitrary interior pointer.
};

template <typename T>
class Own {
  // A transferrable title to a T.  When an Own<T> goes out of scope, the object's Disposer is
  // called to dispose of it.  An Own<T> can be efficiently passed by move, without relocating the
  // underlying object; this transfers ownership.
  //
  // This is much like std::unique_ptr, except:
  // - You cannot release().  An owned object is not necessarily allocated with new (see next
  //   point), so it would be hard to use release() correctly.
  // - The deleter is made polymorphic by virtual call rather than by template.  This is a much
  //   more powerful default -- it allows any random module to decide to use a custom allocator.
  //   This could be accomplished with unique_ptr by forcing everyone to use e.g.
  //   std::unique_ptr<T, capnproto::Disposer&>, but at that point we've lost basically any benefit
  //   of interoperating with std::unique_ptr anyway.

public:
  Own(const Own& other) = delete;
  inline Own(Own&& other) noexcept
      : disposer(other.disposer), ptr(other.ptr) { other.ptr = nullptr; }
  template <typename U>
  inline Own(Own<U>&& other) noexcept
      : disposer(other.disposer), ptr(other.ptr) { other.ptr = nullptr; }
  inline Own(T* ptr, Disposer* disposer) noexcept: disposer(disposer), ptr(ptr) {}

  ~Own() noexcept { dispose(); }

  inline Own& operator=(Own&& other) {
    dispose();
    disposer = other.disposer;
    ptr = other.ptr;
    other.ptr = nullptr;
    return *this;
  }

  inline T* operator->() { return ptr; }
  inline const T* operator->() const { return ptr; }
  inline T& operator*() { return *ptr; }
  inline const T& operator*() const { return *ptr; }
  inline T* get() { return ptr; }
  inline const T* get() const { return ptr; }
  inline operator T*() { return ptr; }
  inline operator const T*() const { return ptr; }

private:
  Disposer* disposer;  // Only valid if ptr != nullptr.
  T* ptr;

  inline void dispose() {
    // Make sure that if an exception is thrown, we are left with a null ptr, so we won't possibly
    // dispose again.
    void* ptrCopy = ptr;
    if (ptrCopy != nullptr) {
      ptr = nullptr;
      disposer->dispose(ptrCopy);
    }
  }
};

namespace internal {

template <typename T>
class HeapValue final: public Disposer {
public:
  template <typename... Params>
  inline HeapValue(Params&&... params): value(capnproto::forward<Params>(params)...) {}

  virtual void dispose(void*) override { delete this; }

  T value;
};

}  // namespace internal

template <typename T, typename... Params>
Own<T> heap(Params&&... params) {
  // heap<T>(...) allocates a T on the heap, forwarding the parameters to its constructor.  The
  // exact heap implementation is unspecified -- for now it is operator new, but you should not
  // assume anything.

  auto result = new internal::HeapValue<T>(capnproto::forward<Params>(params)...);
  return Own<T>(&result->value, result);
}

// =======================================================================================
// ArrayPtr

template <typename T>
class ArrayPtr {
  // A pointer to an array.  Includes a size.  Like any pointer, it doesn't own the target data,
  // and passing by value only copies the pointer, not the target.

public:
  inline constexpr ArrayPtr(): ptr(nullptr), size_(0) {}
  inline constexpr ArrayPtr(std::nullptr_t): ptr(nullptr), size_(0) {}
  inline constexpr ArrayPtr(T* ptr, std::size_t size): ptr(ptr), size_(size) {}
  inline constexpr ArrayPtr(T* begin, T* end): ptr(begin), size_(end - begin) {}

  inline operator ArrayPtr<const T>() {
    return ArrayPtr<const T>(ptr, size_);
  }

  inline std::size_t size() const { return size_; }
  inline T& operator[](std::size_t index) const {
    CAPNPROTO_INLINE_DPRECOND(index < size_, "Out-of-bounds ArrayPtr access.");
    return ptr[index];
  }

  inline T* begin() const { return ptr; }
  inline T* end() const { return ptr + size_; }
  inline T& front() const { return *ptr; }
  inline T& back() const { return *(ptr + size_ - 1); }

  inline ArrayPtr slice(size_t start, size_t end) {
    CAPNPROTO_INLINE_DPRECOND(start <= end && end <= size_, "Out-of-bounds ArrayPtr::slice().");
    return ArrayPtr(ptr + start, end - start);
  }

  inline bool operator==(std::nullptr_t) { return size_ == 0; }
  inline bool operator!=(std::nullptr_t) { return size_ != 0; }

private:
  T* ptr;
  std::size_t size_;
};

template <typename T>
inline constexpr ArrayPtr<T> arrayPtr(T* ptr, size_t size) {
  // Use this function to construct ArrayPtrs without writing out the type name.
  return ArrayPtr<T>(ptr, size);
}

template <typename T>
inline constexpr ArrayPtr<T> arrayPtr(T* begin, T* end) {
  // Use this function to construct ArrayPtrs without writing out the type name.
  return ArrayPtr<T>(begin, end);
}

inline constexpr ArrayPtr<const char> arrayPtr(const char* s) {
  // Use this function to construct an ArrayPtr from a NUL-terminated string, especially a literal.
  return arrayPtr(s, strlen(s));
}

template <typename T>
class Array {
  // An owned array which will automatically be deleted in the destructor.  Can be moved, but not
  // copied.

public:
  inline Array(): ptr(nullptr), size_(0) {}
  inline Array(std::nullptr_t): ptr(nullptr), size_(0) {}
  inline Array(Array&& other) noexcept: ptr(other.ptr), size_(other.size_) {
    other.ptr = nullptr;
    other.size_ = 0;
  }

  CAPNPROTO_DISALLOW_COPY(Array);
  inline ~Array() noexcept { delete[] ptr; }

  inline operator ArrayPtr<T>() {
    return ArrayPtr<T>(ptr, size_);
  }
  inline operator ArrayPtr<const T>() const {
    return ArrayPtr<T>(ptr, size_);
  }
  inline ArrayPtr<T> asPtr() {
    return ArrayPtr<T>(ptr, size_);
  }

  inline std::size_t size() const { return size_; }
  inline T& operator[](std::size_t index) const {
    CAPNPROTO_INLINE_DPRECOND(index < size_, "Out-of-bounds Array access.");
    return ptr[index];
  }

  inline T* begin() const { return ptr; }
  inline T* end() const { return ptr + size_; }
  inline T& front() const { return *ptr; }
  inline T& back() const { return *(ptr + size_ - 1); }

  inline ArrayPtr<T> slice(size_t start, size_t end) {
    CAPNPROTO_INLINE_DPRECOND(start <= end && end <= size_, "Out-of-bounds Array::slice().");
    return ArrayPtr<T>(ptr + start, end - start);
  }
  inline ArrayPtr<const T> slice(size_t start, size_t end) const {
    CAPNPROTO_INLINE_DPRECOND(start <= end && end <= size_, "Out-of-bounds Array::slice().");
    return ArrayPtr<const T>(ptr + start, end - start);
  }

  inline bool operator==(std::nullptr_t) const { return size_ == 0; }
  inline bool operator!=(std::nullptr_t) const { return size_ != 0; }

  inline Array& operator=(std::nullptr_t) {
    delete[] ptr;
    ptr = nullptr;
    size_ = 0;
    return *this;
  }

  inline Array& operator=(Array&& other) {
    delete[] ptr;
    ptr = other.ptr;
    size_ = other.size_;
    other.ptr = nullptr;
    other.size_ = 0;
    return *this;
  }

private:
  T* ptr;
  size_t size_;

  inline explicit Array(std::size_t size): ptr(new T[size]), size_(size) {}
  inline Array(T* ptr, size_t size): ptr(ptr), size_(size) {}

  template <typename U>
  friend Array<U> newArray(size_t size);

  template <typename U>
  friend class ArrayBuilder;
};

template <typename T>
inline Array<T> newArray(size_t size) {
  return Array<T>(size);
}

template <typename T>
class ArrayBuilder {
  // TODO(cleanup):  This class doesn't work for non-primitive types because Slot is not
  //   constructable.  Giving Slot a constructor/destructor means arrays of it have to be tagged
  //   so operator delete can run the destructors.  If we reinterpret_cast the array to an array
  //   of T and delete it as that type, operator delete gets very upset.
  //
  //   Perhaps we should bite the bullet and make the Array family do manual memory allocation,
  //   bypassing the rather-stupid C++ array new/delete operators which store a redundant copy of
  //   the size anyway.

  union Slot {
    T value;
    char dummy;
  };
  static_assert(sizeof(Slot) == sizeof(T), "union is bigger than content?");

public:
  explicit ArrayBuilder(size_t size): ptr(new Slot[size]), pos(ptr), endPtr(ptr + size) {}
  ~ArrayBuilder() {
    for (Slot* p = ptr; p < pos; ++p) {
      p->value.~T();
    }
    delete [] ptr;
  }

  template <typename... Params>
  void add(Params&&... params) {
    CAPNPROTO_INLINE_DPRECOND(pos < endPtr, "Added too many elements to ArrayBuilder.");
    new(&pos->value) T(forward<Params>(params)...);
    ++pos;
  }

  template <typename Container>
  void addAll(Container&& container) {
    Slot* __restrict__ pos_ = pos;
    auto i = container.begin();
    auto end = container.end();
    while (i != end) {
      pos_++->value = *i++;
    }
    pos = pos_;
  }

  Array<T> finish() {
    // We could allow partial builds if Array<T> used a deleter callback, but that would make
    // Array<T> bigger for no benefit most of the time.
    CAPNPROTO_INLINE_DPRECOND(pos == endPtr, "ArrayBuilder::finish() called prematurely.");
    Array<T> result(reinterpret_cast<T*>(ptr), pos - ptr);
    ptr = nullptr;
    pos = nullptr;
    endPtr = nullptr;
    return result;
  }

private:
  Slot* ptr;
  Slot* pos;
  Slot* endPtr;
};

// =======================================================================================
// String -- Just a NUL-terminated Array<char>.

class String {
public:
  String() = default;
  String(const char* value);
  String(const char* value, size_t length);

  inline ArrayPtr<char> asArray();
  inline ArrayPtr<const char> asArray() const;
  inline const char* cStr() const { return content == nullptr ? "" : content.begin(); }

  inline size_t size() const { return content == nullptr ? 0 : content.size() - 1; }

  inline char* begin() { return content == nullptr ? nullptr : content.begin(); }
  inline char* end() { return content == nullptr ? nullptr : content.end() - 1; }
  inline const char* begin() const { return content == nullptr ? nullptr : content.begin(); }
  inline const char* end() const { return content == nullptr ? nullptr : content.end() - 1; }

private:
  Array<char> content;
};

inline ArrayPtr<char> String::asArray() {
  return content == nullptr ? ArrayPtr<char>(nullptr) : content.slice(0, content.size() - 1);
}
inline ArrayPtr<const char> String::asArray() const {
  return content == nullptr ? ArrayPtr<char>(nullptr) : content.slice(0, content.size() - 1);
}

// =======================================================================================
// IDs

template <typename UnderlyingType, typename Label>
struct Id {
  // A type-safe numeric ID.  `UnderlyingType` is the underlying integer representation.  `Label`
  // distinguishes this Id from other Id types.  Sample usage:
  //
  //   class Foo;
  //   typedef Id<uint, Foo> FooId;
  //
  //   class Bar;
  //   typedef Id<uint, Bar> BarId;
  //
  // You can now use the FooId and BarId types without any possibility of accidentally using a
  // FooId when you really wanted a BarId or vice-versa.

  UnderlyingType value;

  inline constexpr Id(): value(0) {}
  inline constexpr explicit Id(int value): value(value) {}

  inline constexpr bool operator==(const Id& other) { return value == other.value; }
  inline constexpr bool operator!=(const Id& other) { return value != other.value; }
  inline constexpr bool operator<=(const Id& other) { return value <= other.value; }
  inline constexpr bool operator>=(const Id& other) { return value >= other.value; }
  inline constexpr bool operator< (const Id& other) { return value <  other.value; }
  inline constexpr bool operator> (const Id& other) { return value >  other.value; }
};

// =======================================================================================
// Units

template <typename T> constexpr bool isIntegral() { return false; }
template <> constexpr bool isIntegral<char>() { return true; }
template <> constexpr bool isIntegral<signed char>() { return true; }
template <> constexpr bool isIntegral<short>() { return true; }
template <> constexpr bool isIntegral<int>() { return true; }
template <> constexpr bool isIntegral<long>() { return true; }
template <> constexpr bool isIntegral<long long>() { return true; }
template <> constexpr bool isIntegral<unsigned char>() { return true; }
template <> constexpr bool isIntegral<unsigned short>() { return true; }
template <> constexpr bool isIntegral<unsigned int>() { return true; }
template <> constexpr bool isIntegral<unsigned long>() { return true; }
template <> constexpr bool isIntegral<unsigned long long>() { return true; }

template <typename Number, typename Unit1, typename Unit2>
class UnitRatio {
  // A multiplier used to convert Quantities of one unit to Quantities of another unit.  See
  // Quantity, below.
  //
  // Construct this type by dividing one Quantity by another of a different unit.  Use this type
  // by multiplying it by a Quantity, or dividing a Quantity by it.

  static_assert(isIntegral<Number>(), "Underlying type for UnitRatio must be integer.");

public:
  inline UnitRatio() {}

  constexpr explicit UnitRatio(Number unit1PerUnit2): unit1PerUnit2(unit1PerUnit2) {}
  // This constructor was intended to be private, but GCC complains about it being private in a
  // bunch of places that don't appear to even call it, so I made it public.  Oh well.

  template <typename OtherNumber>
  inline constexpr UnitRatio(const UnitRatio<OtherNumber, Unit1, Unit2>& other)
      : unit1PerUnit2(other.unit1PerUnit2) {}

  template <typename OtherNumber>
  inline constexpr UnitRatio<decltype(Number(1)+OtherNumber(1)), Unit1, Unit2>
      operator+(UnitRatio<OtherNumber, Unit1, Unit2> other) {
    return UnitRatio<decltype(Number(1)+OtherNumber(1)), Unit1, Unit2>(
        unit1PerUnit2 + other.unit1PerUnit2);
  }
  template <typename OtherNumber>
  inline constexpr UnitRatio<decltype(Number(1)-OtherNumber(1)), Unit1, Unit2>
      operator-(UnitRatio<OtherNumber, Unit1, Unit2> other) {
    return UnitRatio<decltype(Number(1)-OtherNumber(1)), Unit1, Unit2>(
        unit1PerUnit2 - other.unit1PerUnit2);
  }

  template <typename OtherNumber, typename Unit3>
  inline constexpr UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit3, Unit2>
      operator*(UnitRatio<OtherNumber, Unit3, Unit1> other) {
    // U1 / U2 * U3 / U1 = U3 / U2
    return UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit3, Unit2>(
        unit1PerUnit2 * other.unit1PerUnit2);
  }
  template <typename OtherNumber, typename Unit3>
  inline constexpr UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit1, Unit3>
      operator*(UnitRatio<OtherNumber, Unit2, Unit3> other) {
    // U1 / U2 * U2 / U3 = U1 / U3
    return UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit1, Unit3>(
        unit1PerUnit2 * other.unit1PerUnit2);
  }

  template <typename OtherNumber, typename Unit3>
  inline constexpr UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit3, Unit2>
      operator/(UnitRatio<OtherNumber, Unit1, Unit3> other) {
    // (U1 / U2) / (U1 / U3) = U3 / U2
    return UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit3, Unit2>(
        unit1PerUnit2 / other.unit1PerUnit2);
  }
  template <typename OtherNumber, typename Unit3>
  inline constexpr UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit1, Unit3>
      operator/(UnitRatio<OtherNumber, Unit3, Unit2> other) {
    // (U1 / U2) / (U3 / U2) = U1 / U3
    return UnitRatio<decltype(Number(1)*OtherNumber(1)), Unit1, Unit3>(
        unit1PerUnit2 / other.unit1PerUnit2);
  }

  template <typename OtherNumber>
  inline decltype(Number(1) / OtherNumber(1))
      operator/(UnitRatio<OtherNumber, Unit1, Unit2> other) const {
    return unit1PerUnit2 / other.unit1PerUnit2;
  }

  inline bool operator==(UnitRatio other) const { return unit1PerUnit2 == other.unit1PerUnit2; }
  inline bool operator!=(UnitRatio other) const { return unit1PerUnit2 != other.unit1PerUnit2; }

private:
  Number unit1PerUnit2;

  template <typename OtherNumber, typename OtherUnit>
  friend class Quantity;
  template <typename OtherNumber, typename OtherUnit1, typename OtherUnit2>
  friend class UnitRatio;

  template <typename N1, typename N2, typename U1, typename U2>
  friend inline constexpr UnitRatio<decltype(N1(1) * N2(1)), U1, U2>
      operator*(N1, UnitRatio<N2, U1, U2>);
};

template <typename N1, typename N2, typename U1, typename U2>
inline constexpr UnitRatio<decltype(N1(1) * N2(1)), U1, U2>
    operator*(N1 n, UnitRatio<N2, U1, U2> r) {
  return UnitRatio<decltype(N1(1) * N2(1)), U1, U2>(n * r.unit1PerUnit2);
}

template <typename Number, typename Unit>
class Quantity {
  // A type-safe numeric quantity, specified in terms of some unit.  Two Quantities cannot be used
  // in arithmetic unless they use the same unit.  The `Unit` type parameter is only used to prevent
  // accidental mixing of units; this type is never instantiated and can very well be incomplete.
  // `Number` is the underlying primitive numeric type.
  //
  // Quantities support most basic arithmetic operators, intelligently handling units, and
  // automatically casting the underlying type in the same way that the compiler would.
  //
  // To convert a primitive number to a Quantity, multiply it by unit<Quantity<N, U>>().
  // To convert a Quantity to a primitive number, divide it by unit<Quantity<N, U>>().
  // To convert a Quantity of one unit to another unit, multiply or divide by a UnitRatio.
  //
  // The Quantity class is not well-suited to hardcore physics as it does not allow multiplying
  // one quantity by another.  For example, multiplying meters by meters won't get you square
  // meters; it will get you a compiler error.  It would be interesting to see if template
  // metaprogramming could properly deal with such things but this isn't needed for the present
  // use case.
  //
  // Sample usage:
  //
  //   class SecondsLabel;
  //   typedef Quantity<double, SecondsLabel> Seconds;
  //   constexpr Seconds SECONDS = unit<Seconds>();
  //
  //   class MinutesLabel;
  //   typedef Quantity<double, MinutesLabel> Minutes;
  //   constexpr Minutes MINUTES = unit<Minutes>();
  //
  //   constexpr UnitRatio<double, SecondsLabel, MinutesLabel> SECONDS_PER_MINUTE =
  //       60 * SECONDS / MINUTES;
  //
  //   void waitFor(Seconds seconds) {
  //     sleep(seconds / SECONDS);
  //   }
  //   void waitFor(Minutes minutes) {
  //     waitFor(minutes * SECONDS_PER_MINUTE);
  //   }
  //
  //   void waitThreeMinutes() {
  //     waitFor(3 * MINUTES);
  //   }

  static_assert(isIntegral<Number>(), "Underlying type for Quantity must be integer.");

public:
  inline constexpr Quantity() {}

  inline explicit constexpr Quantity(Number value): value(value) {}
  // This constructor was intended to be private, but GCC complains about it being private in a
  // bunch of places that don't appear to even call it, so I made it public.  Oh well.

  template <typename OtherNumber>
  inline constexpr Quantity(const Quantity<OtherNumber, Unit>& other)
      : value(other.value) {}

  template <typename OtherNumber>
  inline constexpr Quantity<decltype(Number(1) + OtherNumber(1)), Unit>
      operator+(const Quantity<OtherNumber, Unit>& other) const {
    return Quantity<decltype(Number(1) + OtherNumber(1)), Unit>(value + other.value);
  }
  template <typename OtherNumber>
  inline constexpr Quantity<decltype(Number(1) - OtherNumber(1)), Unit>
      operator-(const Quantity<OtherNumber, Unit>& other) const {
    return Quantity<decltype(Number(1) - OtherNumber(1)), Unit>(value - other.value);
  }
  template <typename OtherNumber>
  inline constexpr Quantity<decltype(Number(1) * OtherNumber(1)), Unit>
      operator*(OtherNumber other) const {
    static_assert(isIntegral<OtherNumber>(), "Multiplied Quantity by non-integer.");
    return Quantity<decltype(Number(1) * other), Unit>(value * other);
  }
  template <typename OtherNumber>
  inline constexpr Quantity<decltype(Number(1) / OtherNumber(1)), Unit>
      operator/(OtherNumber other) const {
    static_assert(isIntegral<OtherNumber>(), "Divided Quantity by non-integer.");
    return Quantity<decltype(Number(1) / other), Unit>(value / other);
  }
  template <typename OtherNumber>
  inline constexpr decltype(Number(1) / OtherNumber(1))
      operator/(const Quantity<OtherNumber, Unit>& other) const {
    return value / other.value;
  }
  template <typename OtherNumber>
  inline constexpr decltype(Number(1) % OtherNumber(1))
      operator%(const Quantity<OtherNumber, Unit>& other) const {
    return value % other.value;
  }

  template <typename OtherNumber, typename OtherUnit>
  inline constexpr Quantity<decltype(Number(1) * OtherNumber(1)), OtherUnit>
      operator*(const UnitRatio<OtherNumber, OtherUnit, Unit>& ratio) const {
    return Quantity<decltype(Number(1) * OtherNumber(1)), OtherUnit>(
        value * ratio.unit1PerUnit2);
  }
  template <typename OtherNumber, typename OtherUnit>
  inline constexpr Quantity<decltype(Number(1) / OtherNumber(1)), OtherUnit>
      operator/(const UnitRatio<OtherNumber, Unit, OtherUnit>& ratio) const {
    return Quantity<decltype(Number(1) / OtherNumber(1)), OtherUnit>(
        value / ratio.unit1PerUnit2);
  }
  template <typename OtherNumber, typename OtherUnit>
  inline constexpr Quantity<decltype(Number(1) % OtherNumber(1)), Unit>
      operator%(const UnitRatio<OtherNumber, Unit, OtherUnit>& ratio) const {
    return Quantity<decltype(Number(1) % OtherNumber(1)), Unit>(
        value % ratio.unit1PerUnit2);
  }
  template <typename OtherNumber, typename OtherUnit>
  inline constexpr UnitRatio<decltype(Number(1) / OtherNumber(1)), Unit, OtherUnit>
      operator/(const Quantity<OtherNumber, OtherUnit>& other) const {
    return UnitRatio<decltype(Number(1) / OtherNumber(1)), Unit, OtherUnit>(value / other.value);
  }

  template <typename OtherNumber>
  inline constexpr bool operator==(const Quantity<OtherNumber, Unit>& other) const {
    return value == other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator!=(const Quantity<OtherNumber, Unit>& other) const {
    return value != other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator<=(const Quantity<OtherNumber, Unit>& other) const {
    return value <= other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator>=(const Quantity<OtherNumber, Unit>& other) const {
    return value >= other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator<(const Quantity<OtherNumber, Unit>& other) const {
    return value < other.value;
  }
  template <typename OtherNumber>
  inline constexpr bool operator>(const Quantity<OtherNumber, Unit>& other) const {
    return value > other.value;
  }

  template <typename OtherNumber>
  inline Quantity& operator+=(const Quantity<OtherNumber, Unit>& other) {
    value += other.value;
    return *this;
  }
  template <typename OtherNumber>
  inline Quantity& operator-=(const Quantity<OtherNumber, Unit>& other) {
    value -= other.value;
    return *this;
  }
  template <typename OtherNumber>
  inline Quantity& operator*=(OtherNumber other) {
    value *= other;
    return *this;
  }
  template <typename OtherNumber>
  inline Quantity& operator/=(OtherNumber other) {
    value /= other.value;
    return *this;
  }

private:
  Number value;

  template <typename OtherNumber, typename OtherUnit>
  friend class Quantity;

  template <typename Number1, typename Number2, typename Unit2>
  friend inline constexpr auto operator*(Number1 a, Quantity<Number2, Unit2> b)
      -> Quantity<decltype(Number1(1) * Number2(1)), Unit2>;

  template <typename T>
  friend inline constexpr T unit();
};

template <typename T>
inline constexpr T unit() { return T(1); }
// unit<Quantity<T, U>>() returns a Quantity of value 1.  It also, intentionally, works on basic
// numeric types.

template <typename Number1, typename Number2, typename Unit>
inline constexpr auto operator*(Number1 a, Quantity<Number2, Unit> b)
    -> Quantity<decltype(Number1(1) * Number2(1)), Unit> {
  return Quantity<decltype(Number1(1) * Number2(1)), Unit>(a * b.value);
}

template <typename Number1, typename Number2, typename Unit, typename Unit2>
inline constexpr auto operator*(UnitRatio<Number1, Unit2, Unit> ratio,
    Quantity<Number2, Unit> measure)
    -> decltype(measure * ratio) {
  return measure * ratio;
}

// =======================================================================================
// Raw memory types and measures

class byte { uint8_t  content; CAPNPROTO_DISALLOW_COPY(byte); public: byte() = default; };
class word { uint64_t content; CAPNPROTO_DISALLOW_COPY(word); public: word() = default; };
// byte and word are opaque types with sizes of 8 and 64 bits, respectively.  These types are useful
// only to make pointer arithmetic clearer.  Since the contents are private, the only way to access
// them is to first reinterpret_cast to some other pointer type.
//
// Coping is disallowed because you should always use memcpy().  Otherwise, you may run afoul of
// aliasing rules (particularly when copying words).
//
// A pointer of type word* should always be word-aligned even if won't actually be dereferenced as
// that type.

static_assert(sizeof(byte) == 1, "uint8_t is not one byte?");
static_assert(sizeof(word) == 8, "uint64_t is not 8 bytes?");

namespace internal { class BitLabel; class ElementLabel; class WirePointer; }

#ifndef CAPNPROTO_DEBUG_TYPES
#define CAPNPROTO_DEBUG_TYPES 1
// Set this to zero to degrade all the "count" types below to being plain integers.  All the code
// should still operate exactly the same, we just lose compile-time checking.  Note that this will
// also change symbol names, so it's important that the Cap'n proto library and any clients be
// compiled with the same setting here.
//
// TODO(soon):  Decide policy on this.  It may make sense to only use CAPNPROTO_DEBUG_TYPES when
//   compiling Cap'n Proto's own tests, but disable it for all real builds, as clients may find
//   this safety tiring.  Also, need to benchmark to verify there really is no perf hit.

#endif

#if CAPNPROTO_DEBUG_TYPES

typedef Quantity<uint, internal::BitLabel> BitCount;
typedef Quantity<uint8_t, internal::BitLabel> BitCount8;
typedef Quantity<uint16_t, internal::BitLabel> BitCount16;
typedef Quantity<uint32_t, internal::BitLabel> BitCount32;
typedef Quantity<uint64_t, internal::BitLabel> BitCount64;

typedef Quantity<uint, byte> ByteCount;
typedef Quantity<uint8_t, byte> ByteCount8;
typedef Quantity<uint16_t, byte> ByteCount16;
typedef Quantity<uint32_t, byte> ByteCount32;
typedef Quantity<uint64_t, byte> ByteCount64;

typedef Quantity<uint, word> WordCount;
typedef Quantity<uint8_t, word> WordCount8;
typedef Quantity<uint16_t, word> WordCount16;
typedef Quantity<uint32_t, word> WordCount32;
typedef Quantity<uint64_t, word> WordCount64;

typedef Quantity<uint, internal::ElementLabel> ElementCount;
typedef Quantity<uint8_t, internal::ElementLabel> ElementCount8;
typedef Quantity<uint16_t, internal::ElementLabel> ElementCount16;
typedef Quantity<uint32_t, internal::ElementLabel> ElementCount32;
typedef Quantity<uint64_t, internal::ElementLabel> ElementCount64;

typedef Quantity<uint, internal::WirePointer> WirePointerCount;
typedef Quantity<uint8_t, internal::WirePointer> WirePointerCount8;
typedef Quantity<uint16_t, internal::WirePointer> WirePointerCount16;
typedef Quantity<uint32_t, internal::WirePointer> WirePointerCount32;
typedef Quantity<uint64_t, internal::WirePointer> WirePointerCount64;

#else

typedef uint BitCount;
typedef uint8_t BitCount8;
typedef uint16_t BitCount16;
typedef uint32_t BitCount32;
typedef uint64_t BitCount64;

typedef uint ByteCount;
typedef uint8_t ByteCount8;
typedef uint16_t ByteCount16;
typedef uint32_t ByteCount32;
typedef uint64_t ByteCount64;

typedef uint WordCount;
typedef uint8_t WordCount8;
typedef uint16_t WordCount16;
typedef uint32_t WordCount32;
typedef uint64_t WordCount64;

typedef uint ElementCount;
typedef uint8_t ElementCount8;
typedef uint16_t ElementCount16;
typedef uint32_t ElementCount32;
typedef uint64_t ElementCount64;

typedef uint WirePointerCount;
typedef uint8_t WirePointerCount8;
typedef uint16_t WirePointerCount16;
typedef uint32_t WirePointerCount32;
typedef uint64_t WirePointerCount64;

#endif

constexpr BitCount BITS = unit<BitCount>();
constexpr ByteCount BYTES = unit<ByteCount>();
constexpr WordCount WORDS = unit<WordCount>();
constexpr ElementCount ELEMENTS = unit<ElementCount>();
constexpr WirePointerCount POINTERS = unit<WirePointerCount>();

constexpr auto BITS_PER_BYTE = 8 * BITS / BYTES;
constexpr auto BITS_PER_WORD = 64 * BITS / WORDS;
constexpr auto BYTES_PER_WORD = 8 * BYTES / WORDS;

constexpr auto BITS_PER_POINTER = 64 * BITS / POINTERS;
constexpr auto BYTES_PER_POINTER = 8 * BYTES / POINTERS;
constexpr auto WORDS_PER_POINTER = 1 * WORDS / POINTERS;

constexpr WordCount POINTER_SIZE_IN_WORDS = 1 * POINTERS * WORDS_PER_POINTER;

template <typename T>
inline constexpr decltype(BYTES / ELEMENTS) bytesPerElement() {
  return sizeof(T) * BYTES / ELEMENTS;
}

template <typename T>
inline constexpr decltype(BITS / ELEMENTS) bitsPerElement() {
  return sizeof(T) * 8 * BITS / ELEMENTS;
}

#ifndef __CDT_PARSER__

template <typename T, typename U>
inline constexpr U* operator+(U* ptr, Quantity<T, U> offset) {
  return ptr + offset / unit<Quantity<T, U>>();
}
template <typename T, typename U>
inline constexpr const U* operator+(const U* ptr, Quantity<T, U> offset) {
  return ptr + offset / unit<Quantity<T, U>>();
}
template <typename T, typename U>
inline constexpr U* operator+=(U*& ptr, Quantity<T, U> offset) {
  return ptr = ptr + offset / unit<Quantity<T, U>>();
}
template <typename T, typename U>
inline constexpr const U* operator+=(const U*& ptr, Quantity<T, U> offset) {
  return ptr = ptr + offset / unit<Quantity<T, U>>();
}

template <typename T, typename U>
inline constexpr U* operator-(U* ptr, Quantity<T, U> offset) {
  return ptr - offset / unit<Quantity<T, U>>();
}
template <typename T, typename U>
inline constexpr const U* operator-(const U* ptr, Quantity<T, U> offset) {
  return ptr - offset / unit<Quantity<T, U>>();
}
template <typename T, typename U>
inline constexpr U* operator-=(U*& ptr, Quantity<T, U> offset) {
  return ptr = ptr - offset / unit<Quantity<T, U>>();
}
template <typename T, typename U>
inline constexpr const U* operator-=(const U*& ptr, Quantity<T, U> offset) {
  return ptr = ptr - offset / unit<Quantity<T, U>>();
}

#endif

inline constexpr ByteCount intervalLength(const byte* a, const byte* b) {
  return uint(b - a) * BYTES;
}
inline constexpr WordCount intervalLength(const word* a, const word* b) {
  return uint(b - a) * WORDS;
}

}  // namespace capnproto

#endif  // CAPNPROTO_TYPE_SAFETY_H_
