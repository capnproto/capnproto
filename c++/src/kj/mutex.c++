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

#include "mutex.h"
#include "debug.h"

#if KJ_USE_FUTEX
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <limits.h>
#endif

namespace kj {
namespace _ {  // private

#if KJ_USE_FUTEX
// =======================================================================================
// Futex-based implementation (Linux-only)

Mutex::Mutex(): futex(0) {}
Mutex::~Mutex() {
  // This will crash anyway, might as well crash with a nice error message.
  KJ_ASSERT(futex == 0, "Mutex destroyed while locked.") { break; }
}

void Mutex::lock(Exclusivity exclusivity) {
  switch (exclusivity) {
    case EXCLUSIVE:
      for (;;) {
        uint state = 0;
        if (KJ_LIKELY(__atomic_compare_exchange_n(&futex, &state, EXCLUSIVE_HELD, false,
                                                  __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))) {
          // Acquired.
          break;
        }

        // The mutex is contended.  Set the exclusive-requested bit and wait.
        if ((state & EXCLUSIVE_REQUESTED) == 0) {
          if (!__atomic_compare_exchange_n(&futex, &state, state | EXCLUSIVE_REQUESTED, false,
                                           __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            // Oops, the state changed before we could set the request bit.  Start over.
            continue;
          }

          state |= EXCLUSIVE_REQUESTED;
        }

        syscall(SYS_futex, &futex, FUTEX_WAIT_PRIVATE, state, NULL, NULL, 0);
      }
      break;
    case SHARED: {
      uint state = __atomic_add_fetch(&futex, 1, __ATOMIC_ACQUIRE);
      for (;;) {
        if (KJ_LIKELY((state & EXCLUSIVE_HELD) == 0)) {
          // Acquired.
          break;
        }

        // The mutex is exclusively locked by another thread.  Since we incremented the counter
        // already, we just have to wait for it to be unlocked.
        syscall(SYS_futex, &futex, FUTEX_WAIT_PRIVATE, state, NULL, NULL, 0);
        state = __atomic_load_n(&futex, __ATOMIC_ACQUIRE);
      }
      break;
    }
  }
}

void Mutex::unlock(Exclusivity exclusivity) {
  switch (exclusivity) {
    case EXCLUSIVE: {
      KJ_DASSERT(futex & EXCLUSIVE_HELD, "Unlocked a mutex that wasn't locked.");
      uint oldState = __atomic_fetch_and(
          &futex, ~(EXCLUSIVE_HELD | EXCLUSIVE_REQUESTED), __ATOMIC_RELEASE);

      if (KJ_UNLIKELY(oldState & ~EXCLUSIVE_HELD)) {
        // Other threads are waiting.  If there are any shared waiters, they now collectively hold
        // the lock, and we must wake them up.  If there are any exclusive waiters, we must wake
        // them up even if readers are waiting so that at the very least they may re-establish the
        // EXCLUSIVE_REQUESTED bit that we just removed.
        syscall(SYS_futex, &futex, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
      }
      break;
    }

    case SHARED: {
      KJ_DASSERT(futex & SHARED_COUNT_MASK, "Unshared a mutex that wasn't shared.");
      uint state = __atomic_sub_fetch(&futex, 1, __ATOMIC_RELEASE);

      // The only case where anyone is waiting is if EXCLUSIVE_REQUESTED is set, and the only time
      // it makes sense to wake up that waiter is if the shared count has reached zero.
      if (KJ_UNLIKELY(state == EXCLUSIVE_REQUESTED)) {
        if (__atomic_compare_exchange_n(
            &futex, &state, 0, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
          // Wake all exclusive waiters.  We have to wake all of them because one of them will
          // grab the lock while the others will re-establish the exclusive-requested bit.
          syscall(SYS_futex, &futex, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
        }
      }
      break;
    }
  }
}

void Once::runOnce(Initializer& init) {
  uint state = UNINITIALIZED;
  if (__atomic_compare_exchange_n(&futex, &state, INITIALIZING, false,
                                  __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    // It's our job to initialize!
    init.run();
    if (__atomic_exchange_n(&futex, INITIALIZED, __ATOMIC_RELEASE) ==
        INITIALIZING_WITH_WAITERS) {
      // Someone was waiting for us to finish.
      syscall(SYS_futex, &futex, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
    }
  } else {
    for (;;) {
      if (state == INITIALIZED) {
        break;
      } else if (state == INITIALIZING) {
        // Initialization is taking place in another thread.  Indicate that we're waiting.
        if (!__atomic_compare_exchange_n(&futex, &state, INITIALIZING_WITH_WAITERS, true,
                                         __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
          // State changed, retry.
          continue;
        }
      }

      // Wait for initialization.
      syscall(SYS_futex, &futex, FUTEX_WAIT_PRIVATE, INITIALIZING_WITH_WAITERS, NULL, NULL, 0);
      state = __atomic_load_n(&futex, __ATOMIC_ACQUIRE);
    }

    // The docs for __atomic_compare_exchange_n claim that the memmodel for the failure case cannot
    // be stronger than the success case.  That's disappointing, because what we really want is
    // for the two cmpxchg calls above to do an acquire barrier in the failure case only, while
    // being relaxed if successful, so that once the state is INITIALIZED we know we've acquired
    // it.  Oh well, we'll just do an acquire barrier on the way out instead.
    KJ_ASSERT(__atomic_load_n(&futex, __ATOMIC_ACQUIRE) == INITIALIZED);
  }
}

#else
// =======================================================================================
// Generic pthreads-based implementation

#define KJ_PTHREAD_CALL(code) \
  { \
    int pthreadError = code; \
    if (pthreadError != 0) { \
      KJ_FAIL_SYSCALL(#code, pthreadError); \
    } \
  }

#define KJ_PTHREAD_CLEANUP(code) \
  { \
    int pthreadError = code; \
    if (pthreadError != 0) { \
      KJ_LOG(ERROR, #code, strerror(pthreadError)); \
    } \
  }

Mutex::Mutex() {
  KJ_PTHREAD_CALL(pthread_rwlock_init(&mutex, nullptr));
}
Mutex::~Mutex() {
  KJ_PTHREAD_CLEANUP(pthread_rwlock_destroy(&mutex));
}

void Mutex::lock(Exclusivity exclusivity) {
  switch (exclusivity) {
    case EXCLUSIVE:
      KJ_PTHREAD_CALL(pthread_rwlock_wrlock(&mutex));
      break;
    case SHARED:
      KJ_PTHREAD_CALL(pthread_rwlock_rdlock(&mutex));
      break;
  }
}

void Mutex::unlock(Exclusivity exclusivity) {
  KJ_PTHREAD_CALL(pthread_rwlock_unlock(&mutex));
}

Once::Once(): initialized(false) {
  KJ_PTHREAD_CALL(pthread_mutex_init(&mutex, nullptr));
}
Once::~Once() {
  KJ_PTHREAD_CLEANUP(pthread_mutex_destroy(&mutex));
}

void Once::runOnce(Initializer& init) {
  KJ_PTHREAD_CALL(pthread_mutex_lock(&mutex));
  KJ_DEFER(KJ_PTHREAD_CALL(pthread_mutex_unlock(&mutex)));

  if (initialized) {
    return;
  }

  init.run();

  __atomic_store_n(&initialized, true, __ATOMIC_RELEASE);
}

#endif

}  // namespace _ (private)
}  // namespace kj
