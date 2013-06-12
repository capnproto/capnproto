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

#include "array.h"

namespace kj {
namespace _ {  // private

struct HeapArrayDisposer::ExceptionGuard {
  byte* pos;
  size_t elementSize;
  size_t elementCount;
  size_t constructedCount;
  void (*destroyElement)(void*);

  ExceptionGuard(void* ptr, size_t elementSize, size_t elementCount,
                 void (*destroyElement)(void*))
      : pos(reinterpret_cast<byte*>(ptr) + elementSize * elementCount),
        elementSize(elementSize), elementCount(elementCount),
        destroyElement(destroyElement) {}

  ~ExceptionGuard() noexcept(false) {
    if (pos != nullptr) {
      destroyAll();
      operator delete(pos);
    }
  }

  void destroyAll() {
    while (elementCount > 0) {
      pos -= elementSize;
      --elementCount;
      destroyElement(pos);
    }
  }
};

void* HeapArrayDisposer::allocateImpl(size_t elementSize, size_t elementCount, size_t capacity,
                                      void (*constructElement)(void*),
                                      void (*destroyElement)(void*)) {
  void* result = operator new(elementSize * capacity);

  if (constructElement == nullptr) {
    // Nothing to do.
  } else if (destroyElement == nullptr) {
    byte* pos = reinterpret_cast<byte*>(result);
    while (elementCount > 0) {
      constructElement(pos);
      pos += elementSize;
      --elementCount;
    }
  } else {
    ExceptionGuard guard(result, elementSize, 0, destroyElement);
    while (guard.elementCount < elementCount) {
      constructElement(guard.pos);
      guard.pos += elementSize;
      ++guard.elementCount;
    }
    guard.pos = nullptr;
  }

  return result;
}

void HeapArrayDisposer::disposeImpl(
    void* firstElement, size_t elementSize, size_t elementCount, size_t capacity,
    void (*destroyElement)(void*)) const {
  // Note that capacity is ignored since operator delete() doesn't care about it.

  if (destroyElement == nullptr) {
    operator delete(firstElement);
  } else {
    ExceptionGuard guard(firstElement, elementSize, elementCount, destroyElement);
    guard.destroyAll();

    // If an exception is thrown, we'll continue the destruction process in ExceptionGuard's
    // destructor.  If _that_ throws an exception, the program terminates according to C++ rules.
  }
}

const HeapArrayDisposer HeapArrayDisposer::instance = HeapArrayDisposer();

}  // namespace _ (private)
}  // namespace kj
