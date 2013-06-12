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
#include <string.h>
#include <initializer_list>

namespace kj {

// =======================================================================================
// ArrayDisposer -- Implementation details.

class ArrayDisposer {
  // Much like Disposer from memory.h.

protected:
  // Do not declare a destructor, as doing so will force a global initializer for
  // HeapArrayDisposer::instance.

  virtual void disposeImpl(void* firstElement, size_t elementSize, size_t elementCount,
                           size_t capacity, void (*destroyElement)(void*)) const = 0;
  // Disposes of the array.  `destroyElement` invokes the destructor of each element, or is nullptr
  // if the elements have trivial destructors.  `capacity` is the amount of space that was
  // allocated while `elementCount` is the number of elements that were actually constructed;
  // these are always the same number for Array<T> but may be different when using ArrayBuilder<T>.

public:

  template <typename T>
  void dispose(T* firstElement, size_t elementCount, size_t capacity) const;
  // Helper wrapper around disposeImpl().
  //
  // Callers must not call dispose() on the same array twice, even if the first call throws
  // an exception.

private:
  template <typename T, bool hasTrivialDestructor = __has_trivial_destructor(T)>
  struct Dispose_;
};

// =======================================================================================
// Array

template <typename T>
class Array {
  // An owned array which will automatically be disposed of (using an ArrayDisposer) in the
  // destructor.  Can be moved, but not copied.  Much like Own<T>, but for arrays rather than
  // single objects.

public:
  inline Array(): ptr(nullptr), size_(0) {}
  inline Array(decltype(nullptr)): ptr(nullptr), size_(0) {}
  inline Array(Array&& other) noexcept
      : ptr(other.ptr), size_(other.size_), disposer(other.disposer) {
    other.ptr = nullptr;
    other.size_ = 0;
  }
  inline Array(Array<RemoveConstOrBogus<T>>&& other) noexcept
      : ptr(other.ptr), size_(other.size_), disposer(other.disposer) {
    other.ptr = nullptr;
    other.size_ = 0;
  }
  inline Array(T* firstElement, size_t size, const ArrayDisposer& disposer)
      : ptr(firstElement), size_(size), disposer(&disposer) {}

  KJ_DISALLOW_COPY(Array);
  inline ~Array() noexcept { dispose(); }

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
    KJ_IREQUIRE(index < size_, "Out-of-bounds Array access.");
    return ptr[index];
  }

  inline const T* begin() const { return ptr; }
  inline const T* end() const { return ptr + size_; }
  inline const T& front() const { return *ptr; }
  inline const T& back() const { return *(ptr + size_ - 1); }
  inline T* begin() { return ptr; }
  inline T* end() { return ptr + size_; }
  inline T& front() { return *ptr; }
  inline T& back() { return *(ptr + size_ - 1); }

  inline ArrayPtr<T> slice(size_t start, size_t end) {
    KJ_IREQUIRE(start <= end && end <= size_, "Out-of-bounds Array::slice().");
    return ArrayPtr<T>(ptr + start, end - start);
  }
  inline ArrayPtr<const T> slice(size_t start, size_t end) const {
    KJ_IREQUIRE(start <= end && end <= size_, "Out-of-bounds Array::slice().");
    return ArrayPtr<const T>(ptr + start, end - start);
  }

  inline bool operator==(decltype(nullptr)) const { return size_ == 0; }
  inline bool operator!=(decltype(nullptr)) const { return size_ != 0; }

  inline Array& operator=(decltype(nullptr)) {
    dispose();
    return *this;
  }

  inline Array& operator=(Array&& other) {
    dispose();
    ptr = other.ptr;
    size_ = other.size_;
    disposer = other.disposer;
    other.ptr = nullptr;
    other.size_ = 0;
    return *this;
  }

private:
  T* ptr;
  size_t size_;
  const ArrayDisposer* disposer;

  inline void dispose() {
    // Make sure that if an exception is thrown, we are left with a null ptr, so we won't possibly
    // dispose again.
    T* ptrCopy = ptr;
    size_t sizeCopy = size_;
    if (ptrCopy != nullptr) {
      ptr = nullptr;
      size_ = 0;
      disposer->dispose(ptrCopy, sizeCopy, sizeCopy);
    }
  }

  template <typename U>
  friend class Array;
};

namespace _ {  // private

class HeapArrayDisposer final: public ArrayDisposer {
public:
  template <typename T>
  static T* allocate(size_t count);
  template <typename T>
  static T* allocateUninitialized(size_t count);

  static const HeapArrayDisposer instance;

private:
  static void* allocateImpl(size_t elementSize, size_t elementCount, size_t capacity,
                            void (*constructElement)(void*), void (*destroyElement)(void*));
  // Allocates and constructs the array.  Both function pointers are null if the constructor is
  // trivial, otherwise destroyElement is null if the constructor doesn't throw.

  virtual void disposeImpl(void* firstElement, size_t elementSize, size_t elementCount,
                           size_t capacity, void (*destroyElement)(void*)) const override;

  template <typename T, bool hasTrivialConstructor = __has_trivial_constructor(T),
                        bool hasNothrowConstructor = __has_nothrow_constructor(T)>
  struct Allocate_;

  struct ExceptionGuard;
};

}  // namespace _ (private)

template <typename T>
inline Array<T> heapArray(size_t size) {
  // Much like `heap<T>()` from memory.h, allocates a new array on the heap.

  return Array<T>(_::HeapArrayDisposer::allocate<T>(size), size,
                  _::HeapArrayDisposer::instance);
}

template <typename T> Array<T> heapArray(const T* content, size_t size);
template <typename T> Array<T> heapArray(ArrayPtr<const T> content);
template <typename T, typename Iterator> Array<T> heapArray(Iterator begin, Iterator end);
template <typename T> Array<T> heapArray(std::initializer_list<T> init);
// Allocate a heap array containing a copy of the given content.

template <typename T, typename Container>
Array<T> heapArrayFromIterable(Container&& a) { return heapArray(a.begin(), a.end()); }
template <typename T>
Array<T> heapArrayFromIterable(Array<T>&& a) { return mv(a); }

// =======================================================================================
// ArrayBuilder

template <typename T>
class ArrayBuilder {
  // Class which lets you build an Array<T> specifying the exact constructor arguments for each
  // element, rather than starting by default-constructing them.

public:
  ArrayBuilder(): ptr(nullptr), pos(nullptr), endPtr(nullptr) {}
  ArrayBuilder(decltype(nullptr)): ptr(nullptr), pos(nullptr), endPtr(nullptr) {}
  explicit ArrayBuilder(RemoveConst<T>* firstElement, size_t capacity,
                        const ArrayDisposer& disposer)
      : ptr(firstElement), pos(firstElement), endPtr(firstElement + capacity),
        disposer(&disposer) {}
  ArrayBuilder(ArrayBuilder&& other)
      : ptr(other.ptr), pos(other.pos), endPtr(other.endPtr), disposer(other.disposer) {
    other.ptr = nullptr;
    other.pos = nullptr;
    other.endPtr = nullptr;
  }
  KJ_DISALLOW_COPY(ArrayBuilder);
  inline ~ArrayBuilder() noexcept(false) { dispose(); }

  inline operator ArrayPtr<T>() {
    return arrayPtr(ptr, pos);
  }
  inline operator ArrayPtr<const T>() const {
    return arrayPtr(ptr, pos);
  }
  inline ArrayPtr<T> asPtr() {
    return arrayPtr(ptr, pos);
  }
  inline ArrayPtr<const T> asPtr() const {
    return arrayPtr(ptr, pos);
  }

  inline size_t size() const { return pos - ptr; }
  inline size_t capacity() const { return endPtr - ptr; }
  inline T& operator[](size_t index) const {
    KJ_IREQUIRE(index < pos - ptr, "Out-of-bounds Array access.");
    return ptr[index];
  }

  inline const T* begin() const { return ptr; }
  inline const T* end() const { return pos; }
  inline const T& front() const { return *ptr; }
  inline const T& back() const { return *(pos - 1); }
  inline T* begin() { return ptr; }
  inline T* end() { return pos; }
  inline T& front() { return *ptr; }
  inline T& back() { return *(pos - 1); }

  ArrayBuilder& operator=(ArrayBuilder&& other) {
    dispose();
    ptr = other.ptr;
    pos = other.pos;
    endPtr = other.endPtr;
    disposer = other.disposer;
    other.ptr = nullptr;
    other.pos = nullptr;
    other.endPtr = nullptr;
    return *this;
  }
  ArrayBuilder& operator=(decltype(nullptr)) {
    dispose();
    return *this;
  }

  template <typename... Params>
  void add(Params&&... params) {
    KJ_IREQUIRE(pos < endPtr, "Added too many elements to ArrayBuilder.");
    ctor(*pos, kj::fwd<Params>(params)...);
    ++pos;
  }

  template <typename Container>
  void addAll(Container&& container) {
    addAll(container.begin(), container.end());
  }

  template <typename Iterator>
  void addAll(Iterator start, Iterator end);

  Array<T> finish() {
    // We could safely remove this check as long as HeapArrayDisposer relies on operator delete
    // (which doesn't need to know the original capacity) or if we created a custom disposer for
    // ArrayBuilder which stores the capacity in a prefix.  But that would mean we can't allow
    // arbitrary disposers with ArrayBuilder in the future, and anyway this check might catch bugs.
    // Probably we should just create a new Vector-like data structure if we want to allow building
    // of arrays without knowing the final size in advance.
    KJ_IREQUIRE(pos == endPtr, "ArrayBuilder::finish() called prematurely.");
    Array<T> result(reinterpret_cast<T*>(ptr), pos - ptr, _::HeapArrayDisposer::instance);
    ptr = nullptr;
    pos = nullptr;
    endPtr = nullptr;
    return result;
  }

  inline bool isFull() const {
    return pos == endPtr;
  }

private:
  T* ptr;
  RemoveConst<T>* pos;
  T* endPtr;
  const ArrayDisposer* disposer;

  inline void dispose() {
    // Make sure that if an exception is thrown, we are left with a null ptr, so we won't possibly
    // dispose again.
    T* ptrCopy = ptr;
    T* posCopy = pos;
    T* endCopy = endPtr;
    if (ptrCopy != nullptr) {
      ptr = nullptr;
      pos = nullptr;
      endPtr = nullptr;
      disposer->dispose(ptrCopy, posCopy - ptrCopy, endCopy - ptrCopy);
    }
  }
};

template <typename T>
inline ArrayBuilder<T> heapArrayBuilder(size_t size) {
  // Like `heapArray<T>()` but does not default-construct the elements.  You must construct them
  // manually by calling `add()`.

  return ArrayBuilder<T>(_::HeapArrayDisposer::allocateUninitialized<RemoveConst<T>>(size),
                         size, _::HeapArrayDisposer::instance);
}

// =======================================================================================
// Inline Arrays

template <typename T, size_t fixedSize>
class FixedArray {
  // A fixed-width array whose storage is allocated inline rather than on the heap.

public:
  inline size_t size() const { return fixedSize; }
  inline T* begin() { return content; }
  inline T* end() { return content + fixedSize; }
  inline const T* begin() const { return content; }
  inline const T* end() const { return content + fixedSize; }

  inline operator ArrayPtr<T>() {
    return arrayPtr(content, fixedSize);
  }
  inline operator ArrayPtr<const T>() const {
    return arrayPtr(content, fixedSize);
  }

  inline T& operator[](size_t index) { return content[index]; }
  inline const T& operator[](size_t index) const { return content[index]; }

private:
  T content[fixedSize];
};

template <typename T, size_t fixedSize>
class CappedArray {
  // Like `FixedArray` but can be dynamically resized as long as the size does not exceed the limit
  // specified by the template parameter.
  //
  // TODO(someday):  Don't construct elements past currentSize?

public:
  inline constexpr CappedArray(): currentSize(fixedSize) {}
  inline explicit constexpr CappedArray(size_t s): currentSize(s) {}

  inline size_t size() const { return currentSize; }
  inline void setSize(size_t s) { currentSize = s; }
  inline T* begin() { return content; }
  inline T* end() { return content + currentSize; }
  inline const T* begin() const { return content; }
  inline const T* end() const { return content + currentSize; }

  inline operator ArrayPtr<T>() {
    return arrayPtr(content, currentSize);
  }
  inline operator ArrayPtr<const T>() const {
    return arrayPtr(content, currentSize);
  }

  inline T& operator[](size_t index) { return content[index]; }
  inline const T& operator[](size_t index) const { return content[index]; }

private:
  size_t currentSize;
  T content[fixedSize];
};

// =======================================================================================
// Inline implementation details

template <typename T>
struct ArrayDisposer::Dispose_<T, true> {
  static void dispose(T* firstElement, size_t elementCount, size_t capacity,
                      const ArrayDisposer& disposer) {
    disposer.disposeImpl(const_cast<RemoveConst<T>*>(firstElement),
                         sizeof(T), elementCount, capacity, nullptr);
  }
};
template <typename T>
struct ArrayDisposer::Dispose_<T, false> {
  static void destruct(void* ptr) {
    kj::dtor(*reinterpret_cast<T*>(ptr));
  }

  static void dispose(T* firstElement, size_t elementCount, size_t capacity,
                      const ArrayDisposer& disposer) {
    disposer.disposeImpl(firstElement, sizeof(T), elementCount, capacity, &destruct);
  }
};

template <typename T>
void ArrayDisposer::dispose(T* firstElement, size_t elementCount, size_t capacity) const {
  Dispose_<T>::dispose(firstElement, elementCount, capacity, *this);
}

namespace _ {  // private

template <typename T>
struct HeapArrayDisposer::Allocate_<T, true, true> {
  static T* allocate(size_t elementCount, size_t capacity) {
    return reinterpret_cast<T*>(allocateImpl(
        sizeof(T), elementCount, capacity, nullptr, nullptr));
  }
};
template <typename T>
struct HeapArrayDisposer::Allocate_<T, false, true> {
  static void construct(void* ptr) {
    kj::ctor(*reinterpret_cast<T*>(ptr));
  }
  static T* allocate(size_t elementCount, size_t capacity) {
    return reinterpret_cast<T*>(allocateImpl(
        sizeof(T), elementCount, capacity, &construct, nullptr));
  }
};
template <typename T>
struct HeapArrayDisposer::Allocate_<T, false, false> {
  static void construct(void* ptr) {
    kj::ctor(*reinterpret_cast<T*>(ptr));
  }
  static void destruct(void* ptr) {
    kj::dtor(*reinterpret_cast<T*>(ptr));
  }
  static T* allocate(size_t elementCount, size_t capacity) {
    return reinterpret_cast<T*>(allocateImpl(
        sizeof(T), elementCount, capacity, &construct, &destruct));
  }
};

template <typename T>
T* HeapArrayDisposer::allocate(size_t count) {
  return Allocate_<T>::allocate(count, count);
}

template <typename T>
T* HeapArrayDisposer::allocateUninitialized(size_t count) {
  return Allocate_<T, true, true>::allocate(0, count);
}

template <typename Element, typename Iterator,
          bool trivial = __has_trivial_copy(Element) && __has_trivial_assign(Element)>
struct CopyConstructArray_;

template <typename T>
struct CopyConstructArray_<T, T*, true> {
  static inline T* apply(T* __restrict__ pos, T* start, T* end) {
    memcpy(pos, start, reinterpret_cast<byte*>(end) - reinterpret_cast<byte*>(start));
    return pos + (end - start);
  }
};

template <typename T>
struct CopyConstructArray_<T, const T*, true> {
  static inline T* apply(T* __restrict__ pos, const T* start, const T* end) {
    memcpy(pos, start, reinterpret_cast<const byte*>(end) - reinterpret_cast<const byte*>(start));
    return pos + (end - start);
  }
};

template <typename T, typename Iterator>
struct CopyConstructArray_<T, Iterator, true> {
  static inline T* apply(T* __restrict__ pos, Iterator start, Iterator end) {
    // Since both the copy constructor and assignment operator are trivial, we know that assignment
    // is equivalent to copy-constructing.  So we can make this case somewhat easier for the
    // compiler to optimize.
    while (start != end) {
      *pos++ = *start++;
    }
    return pos;
  }
};

template <typename T, typename Iterator>
struct CopyConstructArray_<T, Iterator, false> {
  struct ExceptionGuard {
    T* start;
    T* pos;
    inline explicit ExceptionGuard(T* pos): start(pos), pos(pos) {}
    ~ExceptionGuard() noexcept(false) {
      while (pos > start) {
        dtor(*--pos);
      }
    }
  };

  static T* apply(T* __restrict__ pos, Iterator start, Iterator end) {
    if (noexcept(T(instance<const T&>()))) {
      while (start != end) {
        ctor(*pos++, implicitCast<const T&>(*start++));
      }
      return pos;
    } else {
      // Crap.  This is complicated.
      ExceptionGuard guard(pos);
      while (start != end) {
        ctor(*guard.pos, implicitCast<const T&>(*start++));
        ++guard.pos;
      }
      guard.start = guard.pos;
      return guard.pos;
    }
  }
};

template <typename T, typename Iterator>
inline T* copyConstructArray(T* dst, Iterator start, Iterator end) {
  return CopyConstructArray_<T, Decay<Iterator>>::apply(dst, start, end);
}

}  // namespace _ (private)

template <typename T>
template <typename Iterator>
void ArrayBuilder<T>::addAll(Iterator start, Iterator end) {
  pos = _::copyConstructArray(pos, start, end);
}

template <typename T>
Array<T> heapArray(const T* content, size_t size) {
  ArrayBuilder<T> builder = heapArrayBuilder<T>(size);
  builder.addAll(content, content + size);
  return builder.finish();
}

template <typename T>
Array<T> heapArray(ArrayPtr<const T> content) {
  ArrayBuilder<T> builder = heapArrayBuilder<T>(content.size());
  builder.addAll(content);
  return builder.finish();
}

template <typename T, typename Iterator> Array<T>
heapArray(Iterator begin, Iterator end) {
  ArrayBuilder<T> builder = heapArrayBuilder<T>(end - begin);
  builder.addAll(begin, end);
  return builder.finish();
}

template <typename T>
inline Array<T> heapArray(std::initializer_list<T> init) {
  return heapArray<T>(init.begin(), init.end());
}

}  // namespace kj

#endif  // KJ_ARRAY_H_
