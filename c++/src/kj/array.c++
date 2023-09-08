// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "array.h"
#include "exception.h"

namespace kj {

void ExceptionSafeArrayUtil::construct(size_t count, void (*constructElement)(void*)) {
  while (count > 0) {
    constructElement(pos);
    pos += elementSize;
    ++constructedElementCount;
    --count;
  }
}

void ExceptionSafeArrayUtil::destroyAll() {
  while (constructedElementCount > 0) {
    pos -= elementSize;
    --constructedElementCount;
    destroyElement(pos);
  }
}

const DestructorOnlyArrayDisposer DestructorOnlyArrayDisposer::instance =
    DestructorOnlyArrayDisposer();

void DestructorOnlyArrayDisposer::disposeImpl(
    void* firstElement, size_t elementSize, size_t elementCount,
    size_t capacity, void (*destroyElement)(void*)) const {
  if (destroyElement != nullptr) {
    ExceptionSafeArrayUtil guard(firstElement, elementSize, elementCount, destroyElement);
    guard.destroyAll();
  }
}

const NullArrayDisposer NullArrayDisposer::instance = NullArrayDisposer();

void NullArrayDisposer::disposeImpl(
    void* firstElement, size_t elementSize, size_t elementCount,
    size_t capacity, void (*destroyElement)(void*)) const {}

namespace _ {  // private

struct AutoDeleter {
  void* ptr;
  inline void* release() { void* result = ptr; ptr = nullptr; return result; }
  inline AutoDeleter(void* ptr): ptr(ptr) {}
  inline ~AutoDeleter() { operator delete(ptr); }
};

void* HeapArrayDisposer::allocateImpl(size_t elementSize, size_t elementCount, size_t capacity,
                                      void (*constructElement)(void*),
                                      void (*destroyElement)(void*)) {
  AutoDeleter result(operator new(elementSize * capacity));

  if (constructElement == nullptr) {
    // Nothing to do.
  } else if (destroyElement == nullptr) {
    byte* pos = reinterpret_cast<byte*>(result.ptr);
    while (elementCount > 0) {
      constructElement(pos);
      pos += elementSize;
      --elementCount;
    }
  } else {
    ExceptionSafeArrayUtil guard(result.ptr, elementSize, 0, destroyElement);
    guard.construct(elementCount, constructElement);
    guard.release();
  }

  return result.release();
}

void HeapArrayDisposer::disposeImpl(
    void* firstElement, size_t elementSize, size_t elementCount, size_t capacity,
    void (*destroyElement)(void*)) const {
  // Note that capacity is ignored since operator delete() doesn't care about it.
  AutoDeleter deleter(firstElement);

  if (destroyElement != nullptr) {
    ExceptionSafeArrayUtil guard(firstElement, elementSize, elementCount, destroyElement);
    guard.destroyAll();
  }
}

const HeapArrayDisposer HeapArrayDisposer::instance = HeapArrayDisposer();

}  // namespace _ (private)
}  // namespace kj
