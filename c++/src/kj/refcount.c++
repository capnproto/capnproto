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
#include <memory>

namespace kj {

Refcounted::~Refcounted() noexcept(false) {}

void Refcounted::disposeImpl(void* pointer) const {
  // The load is a fast-path for the common case where this is the last reference.  An acquire-load
  // is just a regular load on x86.  If there is more than one reference, then we need to do a full
  // atomic decrement with full memory barrier, because:
  // - If this is the final decrement then we need to acquire the object state in order to destroy
  //   it.
  // - If this is not the final decrement then we need to release the object state so that another
  //   thread may destroy it.
  if (__atomic_load_n(&refcount, __ATOMIC_ACQUIRE) == 1 ||
      __atomic_sub_fetch(&refcount, 1, __ATOMIC_ACQ_REL) == 0) {
    delete this;
  }
}

}  // namespace kj
