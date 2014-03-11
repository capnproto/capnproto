// Copyright (c) 2014, Jason Choy <jjwchoy@gmail.com>
// Copyright (c) 2014, Kenton Varda <temporal@gmail.com>
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

#ifndef KJ_THREADLOCAL_H_
#define KJ_THREADLOCAL_H_
// This file declares a macro `KJ_THREADLOCAL_PTR` for declaring thread-local pointer-typed
// variables.  Use like:
//     KJ_THREADLOCAL_PTR(MyType) foo = nullptr;
// This is equivalent to:
//     thread_local MyType* foo = nullptr;
// This can only be used at the global scope.
//
// AVOID USING THIS.  Use of thread-locals is discouraged because they often have many of the same
// properties as singletons: http://www.object-oriented-security.org/lets-argue/singletons
//
// Also, thread-locals tend to be hostile to event-driven code, which can be particularly
// surprising when using fibers (all fibers in the same thread will share the same threadlocals,
// even though they do not share a stack).
//
// That said, thread-locals are sometimes needed for runtime logistics in the KJ framework.  For
// example, the current exception callback and current EventLoop are stored as thread-local
// pointers.  Since KJ only ever needs to store pointers, not values, we avoid the question of
// whether these values' destructors need to be run, and we avoid the need for heap allocation.

#include "common.h"

#if !defined(KJ_USE_PTHREAD_THREADLOCAL) && defined(__APPLE__)
#include "TargetConditionals.h"
#if TARGET_OS_IPHONE
// iOS apparently does not support __thread (nor C++11 thread_local).
#define KJ_USE_PTHREAD_TLS 1
#endif
#endif

#if KJ_USE_PTHREAD_TLS
#include <pthread.h>
#endif

namespace kj {

#if KJ_USE_PTHREAD_TLS
// If __thread is unavailable, we'll fall back to pthreads.

#define KJ_THREADLOCAL_PTR(type) \
  namespace { struct KJ_UNIQUE_NAME(_kj_TlpTag); } \
  static ::kj::_::ThreadLocalPtr< type, KJ_UNIQUE_NAME(_kj_TlpTag)>
// Hack:  In order to ensure each thread-local results in a unique template instance, we declare
//   a one-off dummy type to use as the second type parameter.

namespace _ {  // private

template <typename T, typename>
class ThreadLocalPtr {
  // Hacky type to emulate __thread T*.  We need a separate instance of the ThreadLocalPtr template
  // for every thread-local variable, because we don't want to require a global constructor, and in
  // order to initialize the TLS on first use we need to use a local static variable (in getKey()).
  // Each template instance will get a separate such local static variable, fulfilling our need.

public:
  ThreadLocalPtr() = default;
  constexpr ThreadLocalPtr(decltype(nullptr)) {}
  // Allow initialization to nullptr without a global constructor.

  inline ThreadLocalPtr& operator=(T* val) {
    pthread_setspecific(getKey(), val);
    return *this;
  }

  inline operator T*() const {
    return get();
  }

  inline T& operator*() const {
    return *get();
  }

  inline T* operator->() const {
    return get();
  }

private:
  inline T* get() const {
    return reinterpret_cast<T*>(pthread_getspecific(getKey()));
  }

  inline static pthread_key_t getKey() {
    static pthread_key_t key = createKey();
    return key;
  }

  static pthread_key_t createKey() {
    pthread_key_t key;
    pthread_key_create(&key, 0);
    return key;
  }
};

}  // namespace _ (private)

#else

#define KJ_THREADLOCAL_PTR(type) static __thread type*
// For

#endif // KJ_USE_PTHREAD_TLS

}  // namespace kj

#endif  // KJ_THREADLOCAL_H_
