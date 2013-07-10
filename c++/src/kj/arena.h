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

#ifndef KJ_ARENA_H_
#define KJ_ARENA_H_

#include "memory.h"
#include "array.h"
#include "string.h"

namespace kj {

class Arena {
  // A class which allows several objects to be allocated in contiguous chunks of memory, then
  // frees them all at once.

public:
  Arena(size_t chunkSize = 1024);
  // Create an Arena that tries to allocate in chunks of the given size.  It may deviate from the
  // size if an object is too large to fit or to avoid excessive fragmentation.  Each chunk has
  // a one-word header which is included in the chunk size.

  Arena(ArrayPtr<byte> scratch, size_t chunkSize = 1024);
  // Allocates from the given scratch space first, only resorting to the heap when it runs out.

  KJ_DISALLOW_COPY(Arena);
  ~Arena() noexcept(false);

  template <typename T, typename... Params>
  T& allocate(Params&&... params);
  template <typename T>
  ArrayPtr<T> allocateArray(size_t size);
  // Allocate an object or array of type T.  If T has a non-trivial destructor, that destructor
  // will be run during the Arena's destructor.  Such destructors are run in opposite order of
  // allocation.  Note that these methods must maintain a list of destructors to call, which has
  // overhead, but this overhead only applies if T has a non-trivial destructor.

  template <typename T, typename... Params>
  Own<T> allocateOwn(Params&&... params);
  template <typename T>
  Array<T> allocateOwnArray(size_t size);
  template <typename T>
  ArrayBuilder<T> allocateOwnArrayBuilder(size_t capacity);
  // Allocate an object or array of type T.  Destructors are executed when the returned Own<T>
  // or Array<T> goes out-of-scope, which must happen before the Arena is destroyed.  This variant
  // is useful when you need to control when the destructor is called.  This variant also avoids
  // the need for the Arena itself to keep track of destructors to call later, which may make it
  // slightly more efficient.

  template <typename T>
  inline T& copy(T&& value) { return allocate<Decay<T>>(kj::fwd<T>(value)); }
  // Allocate a copy of the given value in the arena.  This is just a shortcut for calling the
  // type's copy (or move) constructor.

  StringPtr copyString(StringPtr content);
  // Make a copy of the given string inside the arena, and return a pointer to the copy.

private:
  struct ChunkHeader {
    ChunkHeader* next;
  };
  struct ObjectHeader {
    void (*destructor)(void*);
    ObjectHeader* next;
  };

  struct State {
    size_t chunkSize;
    ChunkHeader* chunkList;
    ObjectHeader* objectList;
    byte* pos;
    byte* chunkEnd;

    inline State(size_t chunkSize)
        : chunkSize(chunkSize), chunkList(nullptr), objectList(nullptr),
          pos(nullptr), chunkEnd(nullptr) {}
    inline ~State() noexcept(false) { cleanup(); }

    void cleanup();
    // Run all destructors, leaving the above pointers null.  If a destructor throws, the State is
    // left in a consistent state, such that if cleanup() is called again, it will pick up where
    // it left off.
  };
  State state;

  void* allocateBytes(size_t amount, uint alignment, bool hasDisposer);
  // Allocate the given number of bytes.  `hasDisposer` must be true if `setDisposer()` may be
  // called on this pointer later.

  void setDestructor(void* ptr, void (*destructor)(void*));
  // Schedule the given destructor to be executed when the Arena is destroyed.  `ptr` must be a
  // pointer previously returned by an `allocateBytes()` call for which `hasDisposer` was true.

  template <typename T>
  class DisposerImpl: public Disposer {
  public:
    static const DisposerImpl instance;

    void disposeImpl(void* pointer) const override {
      reinterpret_cast<T*>(pointer)->~T();
    }
  };

  class ArrayDisposerImpl: public ArrayDisposer {
  public:
    static const ArrayDisposerImpl instance;

    void disposeImpl(void* firstElement, size_t elementSize, size_t elementCount,
                     size_t capacity, void (*destroyElement)(void*)) const override;
  };

  template <typename T>
  static void destroyArray(void* pointer) {
    size_t elementCount = *reinterpret_cast<size_t*>(pointer);
    constexpr size_t prefixSize = kj::max(alignof(T), sizeof(size_t));
    ArrayDisposerImpl::instance.disposeImpl(
        reinterpret_cast<byte*>(pointer) + prefixSize,
        sizeof(T), elementCount, elementCount, &destroyObject<T>);
  }

  template <typename T>
  static void destroyObject(void* pointer) {
    dtor(*reinterpret_cast<T*>(pointer));
  }
};

// =======================================================================================
// Inline implementation details

template <typename T, typename... Params>
T& Arena::allocate(Params&&... params) {
  T& result = *reinterpret_cast<T*>(allocateBytes(
      sizeof(T), alignof(T), !__has_trivial_destructor(T)));
  if (!__has_trivial_constructor(T) || sizeof...(Params) > 0) {
    ctor(result, kj::fwd<Params>(params)...);
  }
  if (!__has_trivial_destructor(T)) {
    setDestructor(&result, &destroyObject<T>);
  }
  return result;
}

template <typename T>
ArrayPtr<T> Arena::allocateArray(size_t size) {
  if (__has_trivial_destructor(T)) {
    ArrayPtr<T> result =
        arrayPtr(reinterpret_cast<T*>(allocateBytes(
            sizeof(T) * size, alignof(T), false)), size);
    if (!__has_trivial_constructor(T)) {
      for (size_t i = 0; i < size; i++) {
        ctor(result[i]);
      }
    }
    return result;
  } else {
    // Allocate with a 64-bit prefix in which we store the array size.
    constexpr size_t prefixSize = kj::max(alignof(T), sizeof(size_t));
    void* base = allocateBytes(sizeof(T) * size + prefixSize, alignof(T), true);
    size_t& tag = *reinterpret_cast<size_t*>(base);
    ArrayPtr<T> result =
        arrayPtr(reinterpret_cast<T*>(reinterpret_cast<byte*>(base) + prefixSize), size);
    setDestructor(base, &destroyArray<T>);

    if (__has_trivial_constructor(T)) {
      tag = size;
    } else {
      // In case of constructor exceptions, we need the tag to end up storing the number of objects
      // that were successfully constructed, so that they'll be properly destroyed.
      tag = 0;
      for (size_t i = 0; i < size; i++) {
        ctor(result[i]);
        tag = i + 1;
      }
    }
    return result;
  }
}

template <typename T, typename... Params>
Own<T> Arena::allocateOwn(Params&&... params) {
  T& result = *reinterpret_cast<T*>(allocateBytes(sizeof(T), alignof(T), false));
  if (!__has_trivial_constructor(T) || sizeof...(Params) > 0) {
    ctor(result, kj::fwd<Params>(params)...);
  }
  return Own<T>(&result, DisposerImpl<T>::instance);
}

template <typename T>
Array<T> Arena::allocateOwnArray(size_t size) {
  ArrayBuilder<T> result = allocateOwnArrayBuilder<T>(size);
  for (size_t i = 0; i < size; i++) {
    result.add();
  }
  return result.finish();
}

template <typename T>
ArrayBuilder<T> Arena::allocateOwnArrayBuilder(size_t capacity) {
  return ArrayBuilder<T>(
      reinterpret_cast<T*>(allocateBytes(sizeof(T) * capacity, alignof(T), false)),
      capacity, ArrayDisposerImpl::instance);
}

template <typename T>
const Arena::DisposerImpl<T> Arena::DisposerImpl<T>::instance = Arena::DisposerImpl<T>();

}  // namespace kj

#endif  // KJ_ARENA_H_
