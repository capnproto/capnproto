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

#ifndef KJ_ARRAY_H_
#define KJ_ARRAY_H_

#include "common.h"
#include "memory.h"
#include <string.h>

namespace kj {

// =======================================================================================
// Array

template <typename T>
class Array {
  // An owned array which will automatically be deleted in the destructor.  Can be moved, but not
  // copied.

public:
  inline Array(): ptr(nullptr), size_(0) {}
  inline Array(decltype(nullptr)): ptr(nullptr), size_(0) {}
  inline Array(Array&& other) noexcept: ptr(other.ptr), size_(other.size_) {
    other.ptr = nullptr;
    other.size_ = 0;
  }

  KJ_DISALLOW_COPY(Array);
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

  inline size_t size() const { return size_; }
  inline T& operator[](size_t index) const {
    KJ_INLINE_DPRECOND(index < size_, "Out-of-bounds Array access.");
    return ptr[index];
  }

  inline T* begin() const { return ptr; }
  inline T* end() const { return ptr + size_; }
  inline T& front() const { return *ptr; }
  inline T& back() const { return *(ptr + size_ - 1); }

  inline ArrayPtr<T> slice(size_t start, size_t end) {
    KJ_INLINE_DPRECOND(start <= end && end <= size_, "Out-of-bounds Array::slice().");
    return ArrayPtr<T>(ptr + start, end - start);
  }
  inline ArrayPtr<const T> slice(size_t start, size_t end) const {
    KJ_INLINE_DPRECOND(start <= end && end <= size_, "Out-of-bounds Array::slice().");
    return ArrayPtr<const T>(ptr + start, end - start);
  }

  inline bool operator==(decltype(nullptr)) const { return size_ == 0; }
  inline bool operator!=(decltype(nullptr)) const { return size_ != 0; }

  inline Array& operator=(decltype(nullptr)) {
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

  inline explicit Array(size_t size): ptr(new T[size]), size_(size) {}
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

// =======================================================================================
// ArrayBuilder

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
    KJ_INLINE_DPRECOND(pos < endPtr, "Added too many elements to ArrayBuilder.");
    new(&pos->value) T(kj::fwd<Params>(params)...);
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
    KJ_INLINE_DPRECOND(pos == endPtr, "ArrayBuilder::finish() called prematurely.");
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

}  // namespace kj

#endif  // KJ_ARRAY_H_
