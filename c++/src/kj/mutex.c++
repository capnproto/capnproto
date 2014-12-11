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

#if _WIN32
#define WIN32_LEAN_AND_MEAN 1  // lolz
#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#endif

#include "mutex.h"
#include "debug.h"

#if KJ_USE_FUTEX
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <limits.h>

#ifndef SYS_futex
// Missing on Android/Bionic.
#define SYS_futex __NR_futex
#endif

#ifndef FUTEX_WAIT_PRIVATE
// Missing on Android/Bionic.
#define FUTEX_WAIT_PRIVATE FUTEX_WAIT
#define FUTEX_WAKE_PRIVATE FUTEX_WAKE
#endif

#elif _WIN32
#include <windows.h>
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

void Mutex::assertLockedByCaller(Exclusivity exclusivity) {
  switch (exclusivity) {
    case EXCLUSIVE:
      KJ_ASSERT(futex & EXCLUSIVE_HELD,
                "Tried to call getAlreadyLocked*() but lock is not held.");
      break;
    case SHARED:
      KJ_ASSERT(futex & SHARED_COUNT_MASK,
                "Tried to call getAlreadyLocked*() but lock is not held.");
      break;
  }
}

void Once::runOnce(Initializer& init) {
startOver:
  uint state = UNINITIALIZED;
  if (__atomic_compare_exchange_n(&futex, &state, INITIALIZING, false,
                                  __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    // It's our job to initialize!
    {
      KJ_ON_SCOPE_FAILURE({
        // An exception was thrown by the initializer.  We have to revert.
        if (__atomic_exchange_n(&futex, UNINITIALIZED, __ATOMIC_RELEASE) ==
            INITIALIZING_WITH_WAITERS) {
          // Someone was waiting for us to finish.
          syscall(SYS_futex, &futex, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
        }
      });

      init.run();
    }
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
                                         __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
          // State changed, retry.
          continue;
        }
      } else {
        KJ_DASSERT(state == INITIALIZING_WITH_WAITERS);
      }

      // Wait for initialization.
      syscall(SYS_futex, &futex, FUTEX_WAIT_PRIVATE, INITIALIZING_WITH_WAITERS, NULL, NULL, 0);
      state = __atomic_load_n(&futex, __ATOMIC_ACQUIRE);

      if (state == UNINITIALIZED) {
        // Oh hey, apparently whoever was trying to initialize gave up.  Let's take it from the
        // top.
        goto startOver;
      }
    }
  }
}

void Once::reset() {
  uint state = INITIALIZED;
  if (!__atomic_compare_exchange_n(&futex, &state, UNINITIALIZED,
                                   false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
    KJ_FAIL_REQUIRE("reset() called while not initialized.");
  }
}

#elif _WIN32
// =======================================================================================
// Win32 implementation

#define coercedSrwLock (*reinterpret_cast<SRWLOCK*>(&srwLock))
#define coercedInitOnce (*reinterpret_cast<INIT_ONCE*>(&initOnce))

Mutex::Mutex() {
  static_assert(sizeof(SRWLOCK) == sizeof(srwLock), "SRWLOCK is not a pointer?");
  InitializeSRWLock(&coercedSrwLock);
}
Mutex::~Mutex() {}

void Mutex::lock(Exclusivity exclusivity) {
  switch (exclusivity) {
    case EXCLUSIVE:
      AcquireSRWLockExclusive(&coercedSrwLock);
      break;
    case SHARED:
      AcquireSRWLockShared(&coercedSrwLock);
      break;
  }
}

void Mutex::unlock(Exclusivity exclusivity) {
  switch (exclusivity) {
    case EXCLUSIVE:
      ReleaseSRWLockExclusive(&coercedSrwLock);
      break;
    case SHARED:
      ReleaseSRWLockShared(&coercedSrwLock);
      break;
  }
}

void Mutex::assertLockedByCaller(Exclusivity exclusivity) {
  // We could use TryAcquireSRWLock*() here like we do with the pthread version. However, as of
  // this writing, my version of Wine (1.6.2) doesn't implement these functions and will abort if
  // they are called. Since we were only going to use them as a hacky way to check if the lock is
  // held for debug purposes anyway, we just don't bother.
}

static BOOL WINAPI nullInitializer(PINIT_ONCE initOnce, PVOID parameter, PVOID* context) {
  return true;
}

Once::Once(bool startInitialized) {
  static_assert(sizeof(INIT_ONCE) == sizeof(initOnce), "INIT_ONCE is not a pointer?");
  InitOnceInitialize(&coercedInitOnce);
  if (startInitialized) {
    InitOnceExecuteOnce(&coercedInitOnce, &nullInitializer, nullptr, nullptr);
  }
}
Once::~Once() {}

void Once::runOnce(Initializer& init) {
  BOOL needInit;
  while (!InitOnceBeginInitialize(&coercedInitOnce, 0, &needInit, nullptr)) {
    // Init was occurring in another thread, but then failed with an exception. Retry.
  }

  if (needInit) {
    {
      KJ_ON_SCOPE_FAILURE(InitOnceComplete(&coercedInitOnce, INIT_ONCE_INIT_FAILED, nullptr));
      init.run();
    }

    KJ_ASSERT(InitOnceComplete(&coercedInitOnce, 0, nullptr));
  }
}

bool Once::isInitialized() noexcept {
  BOOL junk;
  return InitOnceBeginInitialize(&coercedInitOnce, INIT_ONCE_CHECK_ONLY, &junk, nullptr);
}

void Once::reset() {
  InitOnceInitialize(&coercedInitOnce);
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

void Mutex::assertLockedByCaller(Exclusivity exclusivity) {
  switch (exclusivity) {
    case EXCLUSIVE:
      // A read lock should fail if the mutex is already held for writing.
      if (pthread_rwlock_tryrdlock(&mutex) == 0) {
        pthread_rwlock_unlock(&mutex);
        KJ_FAIL_ASSERT("Tried to call getAlreadyLocked*() but lock is not held.");
      }
      break;
    case SHARED:
      // A write lock should fail if the mutex is already held for reading or writing.  We don't
      // have any way to prove that the lock is held only for reading.
      if (pthread_rwlock_trywrlock(&mutex) == 0) {
        pthread_rwlock_unlock(&mutex);
        KJ_FAIL_ASSERT("Tried to call getAlreadyLocked*() but lock is not held.");
      }
      break;
  }
}

Once::Once(bool startInitialized): state(startInitialized ? INITIALIZED : UNINITIALIZED) {
  KJ_PTHREAD_CALL(pthread_mutex_init(&mutex, nullptr));
}
Once::~Once() {
  KJ_PTHREAD_CLEANUP(pthread_mutex_destroy(&mutex));
}

void Once::runOnce(Initializer& init) {
  KJ_PTHREAD_CALL(pthread_mutex_lock(&mutex));
  KJ_DEFER(KJ_PTHREAD_CALL(pthread_mutex_unlock(&mutex)));

  if (state != UNINITIALIZED) {
    return;
  }

  init.run();

  __atomic_store_n(&state, INITIALIZED, __ATOMIC_RELEASE);
}

void Once::reset() {
  State oldState = INITIALIZED;
  if (!__atomic_compare_exchange_n(&state, &oldState, UNINITIALIZED,
                                   false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
    KJ_FAIL_REQUIRE("reset() called while not initialized.");
  }
}

#endif

}  // namespace _ (private)
}  // namespace kj
