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

#ifndef KJ_MEMORY_H_
#define KJ_MEMORY_H_

#include "common.h"

namespace kj {

// =======================================================================================

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

// =======================================================================================
// Own<T> -- An owned pointer.

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
  //   std::unique_ptr<T, kj::Disposer&>, but at that point we've lost basically any benefit
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
  inline HeapValue(Params&&... params): value(kj::fwd<Params>(params)...) {}

  virtual void dispose(void*) override { delete this; }

  T value;
};

}  // namespace internal

template <typename T, typename... Params>
Own<T> heap(Params&&... params) {
  // heap<T>(...) allocates a T on the heap, forwarding the parameters to its constructor.  The
  // exact heap implementation is unspecified -- for now it is operator new, but you should not
  // assume anything.

  auto result = new internal::HeapValue<T>(kj::fwd<Params>(params)...);
  return Own<T>(&result->value, result);
}

}  // namespace kj

#endif  // KJ_MEMORY_H_
