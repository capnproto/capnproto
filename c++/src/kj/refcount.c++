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

#include "refcount.h"
#include "debug.h"
#include <memory>

namespace kj {

Refcounted::~Refcounted() noexcept(false) {
  KJ_ASSERT(refcount == 0, "Refcounted object deleted with non-zero refcount.");
}

void Refcounted::disposeImpl(void* pointer) const {
  // Need to do a "release" decrement in order to release the object's state to any other thread
  // which seeks to destroy it.
  if (__atomic_sub_fetch(&refcount, 1, __ATOMIC_RELEASE) == 0) {
    // This was the last reference.  Acquire the memory so that we can destroy it.
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    delete this;
  }
}

bool Refcounted::tryAddRefInternal() const {
  // We want to increment the refcount, but only if it is non-zero.  We have to use a cmpxchg for
  // this.

  uint old = __atomic_load_n(&refcount, __ATOMIC_RELAXED);
  for (;;) {
    if (old == 0) {
      return false;
    }
    if (__atomic_compare_exchange_n(&refcount, &old, old + 1, true,
                                    __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
      return true;
    }
  }
}

}  // namespace kj
