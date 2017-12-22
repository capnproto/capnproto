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

#include "refcount.h"
#include "debug.h"

#if _MSC_VER
// Annoyingly, MSVC only implements the C++ atomic libs, not the C libs, so the only useful
// thing we can get from <atomic> seems to be atomic_thread_fence... but that one function is
// indeed not implemented by the intrinsics, so...
#include <atomic>
#endif

namespace kj {

// =======================================================================================
// Non-atomic (thread-unsafe) refcounting

Refcounted::~Refcounted() noexcept(false) {
  KJ_ASSERT(refcount == 0, "Refcounted object deleted with non-zero refcount.");
}

void Refcounted::disposeImpl(void* pointer) const {
  if (--refcount == 0) {
    delete this;
  }
}

// =======================================================================================
// Atomic (thread-safe) refcounting

AtomicRefcounted::~AtomicRefcounted() noexcept(false) {
  KJ_ASSERT(refcount == 0, "Refcounted object deleted with non-zero refcount.");
}

void AtomicRefcounted::disposeImpl(void* pointer) const {
#if _MSC_VER
  if (KJ_MSVC_INTERLOCKED(Decrement, rel)(&refcount) == 0) {
    std::atomic_thread_fence(std::memory_order_acquire);
    delete this;
  }
#else
  if (__atomic_sub_fetch(&refcount, 1, __ATOMIC_RELEASE) == 0) {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    delete this;
  }
#endif
}

bool AtomicRefcounted::addRefWeakInternal() const {
#if _MSC_VER
  long orig = refcount;

  for (;;) {
    if (orig == 0) {
      // Refcount already hit zero. Destructor is already running so we can't revive the object.
      return false;
    }

    unsigned long old = KJ_MSVC_INTERLOCKED(CompareExchange, nf)(&refcount, orig + 1, orig);
    if (old == orig) {
      return true;
    }
    orig = old;
  }
#else
  uint orig = __atomic_load_n(&refcount, __ATOMIC_RELAXED);

  for (;;) {
    if (orig == 0) {
      // Refcount already hit zero. Destructor is already running so we can't revive the object.
      return false;
    }

    if (__atomic_compare_exchange_n(&refcount, &orig, orig + 1, true,
        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
      // Successfully incremented refcount without letting it hit zero.
      return true;
    }
  }
#endif
}

}  // namespace kj
