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
#include "mutex.h"

namespace kj {

class Arena {
  // A class which allows several objects to be allocated in contiguous chunks of memory, then
  // frees them all at once.
  //
  // Allocating from the same Arena in multiple threads concurrently is safe but not particularly
  // performant due to contention.  The class could be optimized in the future to use per-thread
  // chunks to solve this.

public:
  explicit Arena(size_t chunkSizeHint = 1024);
  // Create an Arena.  `chunkSizeHint` hints at where to start when allocating chunks, but is only
  // a hint -- the Arena will, for example, allocate progressively larger chunks as time goes on,
  // in order to reduce overall allocation overhead.

  explicit Arena(ArrayPtr<byte> scratch);
  // Allocates from the given scratch space first, only resorting to the heap when it runs out.

  KJ_DISALLOW_COPY(Arena);
  ~Arena() noexcept(false);

  template <typename T, typename... Params>
  T& allocate(Params&&... params) const;
  template <typename T>
  ArrayPtr<T> allocateArray(size_t size) const;
  // Allocate an object or array of type T.  If T has a non-trivial destructor, that destructor
  // will be run during the Arena's destructor.  Such destructors are run in opposite order of
  // allocation.  Note that these methods must maintain a list of destructors to call, which has
  // overhead, but this overhead only applies if T has a non-trivial destructor.

  template <typename T, typename... Params>
  Own<T> allocateOwn(Params&&... params) const;
  template <typename T>
  Array<T> allocateOwnArray(size_t size) const;
  template <typename T>
  ArrayBuilder<T> allocateOwnArrayBuilder(size_t capacity) const;
  // Allocate an object or array of type T.  Destructors are executed when the returned Own<T>
  // or Array<T> goes out-of-scope, which must happen before the Arena is destroyed.  This variant
  // is useful when you need to control when the destructor is called.  This variant also avoids
  // the need for the Arena itself to keep track of destructors to call later, which may make it
  // slightly more efficient.

  template <typename T>
  inline T& copy(T&& value) const { return allocate<Decay<T>>(kj::fwd<T>(value)); }
  // Allocate a copy of the given value in the arena.  This is just a shortcut for calling the
  // type's copy (or move) constructor.

  StringPtr copyString(StringPtr content) const;
  // Make a copy of the given string inside the arena, and return a pointer to the copy.

private:
  struct ChunkHeader {
    ChunkHeader* next;
    byte* pos;  // first unallocated byte in this chunk
    byte* end;  // end of this chunk
  };
  struct ObjectHeader {
    void (*destructor)(void*);
    ObjectHeader* next;
  };

  struct State {
    size_t nextChunkSize;
    ChunkHeader* chunkList;
    mutable ObjectHeader* objectList;

    ChunkHeader* currentChunk;

    inline State(size_t nextChunkSize)
        : nextChunkSize(nextChunkSize), chunkList(nullptr),
          objectList(nullptr), currentChunk(nullptr) {}
    inline ~State() noexcept(false) { cleanup(); }

    void cleanup();
    // Run all destructors, leaving the above pointers null.  If a destructor throws, the State is
    // left in a consistent state, such that if cleanup() is called again, it will pick up where
    // it left off.
  };
  MutexGuarded<State> state;

  void* allocateBytes(size_t amount, uint alignment, bool hasDisposer) const;
  // Allocate the given number of bytes.  `hasDisposer` must be true if `setDisposer()` may be
  // called on this pointer later.

  void* allocateBytesLockless(size_t amount, uint alignment) const;
  // Try to allocate the given number of bytes without taking a lock.  Fails if and only if there
  // is no space left in the current chunk.

  void* allocateBytesFallback(size_t amount, uint alignment) const;
  // Fallback used when the current chunk is out of space.

  void setDestructor(void* ptr, void (*destructor)(void*)) const;
  // Schedule the given destructor to be executed when the Arena is destroyed.  `ptr` must be a
  // pointer previously returned by an `allocateBytes()` call for which `hasDisposer` was true.

  template <typename T>
  static void destroyArray(void* pointer) {
    size_t elementCount = *reinterpret_cast<size_t*>(pointer);
    constexpr size_t prefixSize = kj::max(alignof(T), sizeof(size_t));
    DestructorOnlyArrayDisposer::instance.disposeImpl(
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
T& Arena::allocate(Params&&... params) const {
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
ArrayPtr<T> Arena::allocateArray(size_t size) const {
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
Own<T> Arena::allocateOwn(Params&&... params) const {
  T& result = *reinterpret_cast<T*>(allocateBytes(sizeof(T), alignof(T), false));
  if (!__has_trivial_constructor(T) || sizeof...(Params) > 0) {
    ctor(result, kj::fwd<Params>(params)...);
  }
  return Own<T>(&result, DestructorOnlyDisposer<T>::instance);
}

template <typename T>
Array<T> Arena::allocateOwnArray(size_t size) const {
  ArrayBuilder<T> result = allocateOwnArrayBuilder<T>(size);
  for (size_t i = 0; i < size; i++) {
    result.add();
  }
  return result.finish();
}

template <typename T>
ArrayBuilder<T> Arena::allocateOwnArrayBuilder(size_t capacity) const {
  return ArrayBuilder<T>(
      reinterpret_cast<T*>(allocateBytes(sizeof(T) * capacity, alignof(T), false)),
      capacity, DestructorOnlyArrayDisposer::instance);
}

}  // namespace kj

#endif  // KJ_ARENA_H_
