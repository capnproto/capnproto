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
#include "win32-api-version.h"
#define NOGDI  // NOGDI is needed to make EXPECT_EQ(123u, *lock) compile for some reason
#endif

#include "time.h"

#define KJ_MUTEX_TEST 1

#include "mutex.h"
#include "debug.h"
#include "thread.h"
#include <kj/compat/gtest.h>
#include <stdlib.h>

#if _WIN32
#include <windows.h>
#undef NOGDI
#else
#include <pthread.h>
#include <unistd.h>
#endif

#ifdef KJ_CONTENTION_WARNING_THRESHOLD
#include <vector>
#endif

#if KJ_TRACK_LOCK_BLOCKING
#include <syscall.h>
#include <signal.h>
#include <time.h>
#include <atomic>
#endif

namespace kj {
namespace {

#if _WIN32
inline void delay() { Sleep(10); }
#else
inline void delay() { usleep(10000); }
#endif

TEST(Mutex, MutexGuarded) {
  MutexGuarded<uint> value(123);

  {
    Locked<uint> lock = value.lockExclusive();
    EXPECT_EQ(123u, *lock);
    EXPECT_EQ(123u, value.getAlreadyLockedExclusive());

#if KJ_USE_FUTEX
    auto timeout = MILLISECONDS * 50;

    auto startTime = systemPreciseMonotonicClock().now();
    EXPECT_TRUE(value.lockExclusiveWithTimeout(timeout) == nullptr);
    auto duration = startTime - systemPreciseMonotonicClock().now();
    EXPECT_TRUE(duration < timeout);

    startTime = systemPreciseMonotonicClock().now();
    EXPECT_TRUE(value.lockSharedWithTimeout(timeout) == nullptr);
    duration = startTime - systemPreciseMonotonicClock().now();
    EXPECT_TRUE(duration < timeout);

    // originally, upon timing out, the exclusive requested flag would be removed
    // from the futex state. if we did remove the exclusive request flag this test
    // would hang.
    Thread lockTimeoutThread([&]() {
      // try to timeout during 10 ms delay
      Maybe<Locked<uint>> maybeLock = value.lockExclusiveWithTimeout(MILLISECONDS * 8);
      EXPECT_TRUE(maybeLock == nullptr);
    });
#endif

    Thread thread([&]() {
      Locked<uint> threadLock = value.lockExclusive();
      EXPECT_EQ(456u, *threadLock);
      *threadLock = 789;
    });

    delay();
    EXPECT_EQ(123u, *lock);
    *lock = 456;
    auto earlyRelease = kj::mv(lock);
  }

#if KJ_USE_FUTEX
  EXPECT_EQ(789u, *KJ_ASSERT_NONNULL(value.lockExclusiveWithTimeout(MILLISECONDS * 50)));
  EXPECT_EQ(789u, *KJ_ASSERT_NONNULL(value.lockSharedWithTimeout(MILLISECONDS * 50)));
#endif

  EXPECT_EQ(789u, *value.lockExclusive());

  {
    auto rlock1 = value.lockShared();
    EXPECT_EQ(789u, *rlock1);
    EXPECT_EQ(789u, value.getAlreadyLockedShared());

    {
      auto rlock2 = value.lockShared();
      EXPECT_EQ(789u, *rlock2);
      auto rlock3 = value.lockShared();
      EXPECT_EQ(789u, *rlock3);
      auto rlock4 = value.lockShared();
      EXPECT_EQ(789u, *rlock4);
    }

    Thread thread2([&]() {
      Locked<uint> threadLock = value.lockExclusive();
      *threadLock = 321;
    });

#if KJ_USE_FUTEX
    // So, it turns out that pthread_rwlock on BSD "prioritizes" readers over writers.  The result
    // is that if one thread tries to take multiple read locks, but another thread happens to
    // request a write lock it between, you get a deadlock.  This seems to contradict the man pages
    // and common sense, but this is how it is.  The futex-based implementation doesn't currently
    // have this problem because it does not prioritize writers.  Perhaps it will in the future,
    // but we'll leave this test here until then to make sure we notice the change.

    delay();
    EXPECT_EQ(789u, *rlock1);

    {
      auto rlock2 = value.lockShared();
      EXPECT_EQ(789u, *rlock2);
      auto rlock3 = value.lockShared();
      EXPECT_EQ(789u, *rlock3);
      auto rlock4 = value.lockShared();
      EXPECT_EQ(789u, *rlock4);
    }
#endif

    delay();
    EXPECT_EQ(789u, *rlock1);
    auto earlyRelease = kj::mv(rlock1);
  }

  EXPECT_EQ(321u, *value.lockExclusive());

#if !_WIN32 && !__CYGWIN__  // Not checked on win32.
  EXPECT_DEBUG_ANY_THROW(value.getAlreadyLockedExclusive());
  EXPECT_DEBUG_ANY_THROW(value.getAlreadyLockedShared());
#endif
  EXPECT_EQ(321u, value.getWithoutLock());
}

TEST(Mutex, When) {
  MutexGuarded<uint> value(123);

  {
    uint m = value.when([](uint n) { return n < 200; }, [](uint& n) {
      ++n;
      return n + 2;
    });
    KJ_EXPECT(m == 126);

    KJ_EXPECT(*value.lockShared() == 124);
  }

  {
    kj::Thread thread([&]() {
      delay();
      *value.lockExclusive() = 321;
    });

    uint m = value.when([](uint n) { return n > 200; }, [](uint& n) {
      ++n;
      return n + 2;
    });
    KJ_EXPECT(m == 324);

    KJ_EXPECT(*value.lockShared() == 322);
  }

  {
    // Stress test. 100 threads each wait for a value and then set the next value.
    *value.lockExclusive() = 0;

    auto threads = kj::heapArrayBuilder<kj::Own<kj::Thread>>(100);
    for (auto i: kj::zeroTo(100)) {
      threads.add(kj::heap<kj::Thread>([i,&value]() {
        if (i % 2 == 0) delay();
        uint m = value.when([i](const uint& n) { return n == i; },
            [](uint& n) { return n++; });
        KJ_ASSERT(m == i);
      }));
    }

    uint m = value.when([](uint n) { return n == 100; }, [](uint& n) {
      return n++;
    });
    KJ_EXPECT(m == 100);

    KJ_EXPECT(*value.lockShared() == 101);
  }

#if !KJ_NO_EXCEPTIONS
  {
    // Throw from predicate.
    KJ_EXPECT_THROW_MESSAGE("oops threw", value.when([](uint n) -> bool {
      KJ_FAIL_ASSERT("oops threw");
    }, [](uint& n) {
      KJ_FAIL_EXPECT("shouldn't get here");
    }));

    // Throw from predicate later on.
    kj::Thread thread([&]() {
      delay();
      *value.lockExclusive() = 321;
    });

    KJ_EXPECT_THROW_MESSAGE("oops threw", value.when([](uint n) -> bool {
      KJ_ASSERT(n != 321, "oops threw");
      return false;
    }, [](uint& n) {
      KJ_FAIL_EXPECT("shouldn't get here");
    }));
  }

  {
    // Verify the exceptions didn't break the mutex.
    uint m = value.when([](uint n) { return n > 0; }, [](uint& n) {
      return n;
    });
    KJ_EXPECT(m == 321);

    kj::Thread thread([&]() {
      delay();
      *value.lockExclusive() = 654;
    });

    m = value.when([](uint n) { return n > 500; }, [](uint& n) {
      return n;
    });
    KJ_EXPECT(m == 654);
  }
#endif
}

TEST(Mutex, WhenWithTimeout) {
  auto& clock = systemPreciseMonotonicClock();
  MutexGuarded<uint> value(123);

  // A timeout that won't expire.
  static constexpr Duration LONG_TIMEOUT = 10 * kj::SECONDS;

  {
    uint m = value.when([](uint n) { return n < 200; }, [](uint& n) {
      ++n;
      return n + 2;
    }, LONG_TIMEOUT);
    KJ_EXPECT(m == 126);

    KJ_EXPECT(*value.lockShared() == 124);
  }

  {
    kj::Thread thread([&]() {
      delay();
      *value.lockExclusive() = 321;
    });

    uint m = value.when([](uint n) { return n > 200; }, [](uint& n) {
      ++n;
      return n + 2;
    }, LONG_TIMEOUT);
    KJ_EXPECT(m == 324);

    KJ_EXPECT(*value.lockShared() == 322);
  }

  {
    // Stress test. 100 threads each wait for a value and then set the next value.
    *value.lockExclusive() = 0;

    auto threads = kj::heapArrayBuilder<kj::Own<kj::Thread>>(100);
    for (auto i: kj::zeroTo(100)) {
      threads.add(kj::heap<kj::Thread>([i,&value]() {
        if (i % 2 == 0) delay();
        uint m = value.when([i](const uint& n) { return n == i; },
            [](uint& n) { return n++; }, LONG_TIMEOUT);
        KJ_ASSERT(m == i);
      }));
    }

    uint m = value.when([](uint n) { return n == 100; }, [](uint& n) {
      return n++;
    }, LONG_TIMEOUT);
    KJ_EXPECT(m == 100);

    KJ_EXPECT(*value.lockShared() == 101);
  }

  {
    auto start = clock.now();
    uint m = value.when([](uint n) { return n == 0; }, [&](uint& n) {
      KJ_ASSERT(n == 101);
      auto t = clock.now() - start;
      KJ_EXPECT(t >= 10 * kj::MILLISECONDS, t);
      return 12;
    }, 10 * kj::MILLISECONDS);
    KJ_EXPECT(m == 12);

    m = value.when([](uint n) { return n == 0; }, [&](uint& n) {
      KJ_ASSERT(n == 101);
      auto t = clock.now() - start;
      KJ_EXPECT(t >= 20 * kj::MILLISECONDS, t);
      return 34;
    }, 10 * kj::MILLISECONDS);
    KJ_EXPECT(m == 34);

    m = value.when([](uint n) { return n > 0; }, [&](uint& n) {
      KJ_ASSERT(n == 101);
      return 56;
    }, LONG_TIMEOUT);
    KJ_EXPECT(m == 56);
  }

#if !KJ_NO_EXCEPTIONS
  {
    // Throw from predicate.
    KJ_EXPECT_THROW_MESSAGE("oops threw", value.when([](uint n) -> bool {
      KJ_FAIL_ASSERT("oops threw");
    }, [](uint& n) {
      KJ_FAIL_EXPECT("shouldn't get here");
    }, LONG_TIMEOUT));

    // Throw from predicate later on.
    kj::Thread thread([&]() {
      delay();
      *value.lockExclusive() = 321;
    });

    KJ_EXPECT_THROW_MESSAGE("oops threw", value.when([](uint n) -> bool {
      KJ_ASSERT(n != 321, "oops threw");
      return false;
    }, [](uint& n) {
      KJ_FAIL_EXPECT("shouldn't get here");
    }, LONG_TIMEOUT));
  }

  {
    // Verify the exceptions didn't break the mutex.
    uint m = value.when([](uint n) { return n > 0; }, [](uint& n) {
      return n;
    }, LONG_TIMEOUT);
    KJ_EXPECT(m == 321);

    auto start = clock.now();
    m = value.when([](uint n) { return n == 0; }, [&](uint& n) {
      KJ_EXPECT(clock.now() - start >= 10 * kj::MILLISECONDS);
      return n + 1;
    }, 10 * kj::MILLISECONDS);
    KJ_EXPECT(m == 322);

    kj::Thread thread([&]() {
      delay();
      *value.lockExclusive() = 654;
    });

    m = value.when([](uint n) { return n > 500; }, [](uint& n) {
      return n;
    }, LONG_TIMEOUT);
    KJ_EXPECT(m == 654);
  }
#endif
}

TEST(Mutex, WhenWithTimeoutPreciseTiming) {
  // Test that MutexGuarded::when() with a timeout sleeps for precisely the right amount of time.

  auto& clock = systemPreciseMonotonicClock();

  for (uint retryCount = 0; retryCount < 20; retryCount++) {
    MutexGuarded<uint> value(123);

    auto start = clock.now();
    uint m = value.when([&value](uint n) {
      // HACK: Reset the value as a way of testing what happens when the waiting thread is woken
      //   up but then finds it's not ready yet.
      value.getWithoutLock() = 123;
      return n == 321;
    }, [](uint& n) {
      return 456;
    }, 100 * kj::MILLISECONDS);

    KJ_EXPECT(m == 456);

    auto t = clock.now() - start;
    KJ_EXPECT(t >= 100 * kj::MILLISECONDS);
    // Provide a large margin of error here because some operating systems (e.g. Windows) can have
    // long timeslices (13ms) and won't schedule more precisely than a timeslice.
    if (t <= 120 * kj::MILLISECONDS) {
      return;
    }
  }
  KJ_FAIL_ASSERT("time not within expected bounds even after retries");
}

TEST(Mutex, WhenWithTimeoutPreciseTimingAfterInterrupt) {
  // Test that MutexGuarded::when() with a timeout sleeps for precisely the right amount of time,
  // even if the thread is spuriously woken in the middle.

  auto& clock = systemPreciseMonotonicClock();

  for (uint retryCount = 0; retryCount < 20; retryCount++) {
    MutexGuarded<uint> value(123);

    kj::Thread thread([&]() {
      delay();
      value.lockExclusive().induceSpuriousWakeupForTest();
    });

    auto start = clock.now();
    uint m = value.when([](uint n) {
      return n == 321;
    }, [](uint& n) {
      return 456;
    }, 100 * kj::MILLISECONDS);

    KJ_EXPECT(m == 456);

    auto t = clock.now() - start;
    KJ_EXPECT(t >= 100 * kj::MILLISECONDS, t / kj::MILLISECONDS);
    // Provide a large margin of error here because some operating systems (e.g. Windows) can have
    // long timeslices (13ms) and won't schedule more precisely than a timeslice.
    if (t <= 120 * kj::MILLISECONDS) {
      return;
    }
  }
  KJ_FAIL_ASSERT("time not within expected bounds even after retries");
}

KJ_TEST("wait()s wake each other") {
  MutexGuarded<uint> value(0);

  {
    kj::Thread thread([&]() {
      auto lock = value.lockExclusive();
      ++*lock;
      lock.wait([](uint value) { return value == 2; });
      ++*lock;
      lock.wait([](uint value) { return value == 4; });
    });

    {
      auto lock = value.lockExclusive();
      lock.wait([](uint value) { return value == 1; });
      ++*lock;
      lock.wait([](uint value) { return value == 3; });
      ++*lock;
    }
  }
}

TEST(Mutex, Lazy) {
  Lazy<uint> lazy;
  volatile bool initStarted = false;

  Thread thread([&]() {
    EXPECT_EQ(123u, lazy.get([&](SpaceFor<uint>& space) -> Own<uint> {
      initStarted = true;
      delay();
      return space.construct(123);
    }));
  });

  // Spin until the initializer has been entered in the thread.
  while (!initStarted) {
#if _WIN32
    Sleep(0);
#else
    sched_yield();
#endif
  }

  EXPECT_EQ(123u, lazy.get([](SpaceFor<uint>& space) { return space.construct(456); }));
  EXPECT_EQ(123u, lazy.get([](SpaceFor<uint>& space) { return space.construct(789); }));
}

TEST(Mutex, LazyException) {
  Lazy<uint> lazy;

  auto exception = kj::runCatchingExceptions([&]() {
    lazy.get([&](SpaceFor<uint>& space) -> Own<uint> {
          KJ_FAIL_ASSERT("foo") { break; }
          return space.construct(123);
        });
  });
  EXPECT_TRUE(exception != nullptr);

  uint i = lazy.get([&](SpaceFor<uint>& space) -> Own<uint> {
        return space.construct(456);
      });

  // Unfortunately, the results differ depending on whether exceptions are enabled.
  // TODO(someday):  Fix this?  Does it matter?
#if KJ_NO_EXCEPTIONS
  EXPECT_EQ(123, i);
#else
  EXPECT_EQ(456, i);
#endif
}

class OnlyTouchUnderLock {
public:
  OnlyTouchUnderLock(): ptr(nullptr) {}
  OnlyTouchUnderLock(MutexGuarded<uint>& ref): ptr(&ref) {
    ptr->getAlreadyLockedExclusive()++;
  }
  OnlyTouchUnderLock(OnlyTouchUnderLock&& other): ptr(other.ptr) {
    other.ptr = nullptr;
    if (ptr) {
      // Just verify it's locked. Don't increment because different compilers may or may not
      // elide moves.
      ptr->getAlreadyLockedExclusive();
    }
  }
  OnlyTouchUnderLock& operator=(OnlyTouchUnderLock&& other) {
    if (ptr) {
      ptr->getAlreadyLockedExclusive()++;
    }
    ptr = other.ptr;
    other.ptr = nullptr;
    if (ptr) {
      // Just verify it's locked. Don't increment because different compilers may or may not
      // elide moves.
      ptr->getAlreadyLockedExclusive();
    }
    return *this;
  }
  ~OnlyTouchUnderLock() noexcept(false) {
    if (ptr != nullptr) {
      ptr->getAlreadyLockedExclusive()++;
    }
  }

  void frob() {
    ptr->getAlreadyLockedExclusive()++;
  }

private:
  MutexGuarded<uint>* ptr;
};

KJ_TEST("ExternalMutexGuarded<T> destroy after release") {
  MutexGuarded<uint> guarded(0);

  {
    ExternalMutexGuarded<OnlyTouchUnderLock> ext;

    {
      auto lock = guarded.lockExclusive();
      ext.set(lock, guarded);
      KJ_EXPECT(*lock == 1, *lock);
      ext.get(lock).frob();
      KJ_EXPECT(*lock == 2, *lock);
    }

    {
      auto lock = guarded.lockExclusive();
      auto released = ext.release(lock);
      KJ_EXPECT(*lock == 2, *lock);
      released.frob();
      KJ_EXPECT(*lock == 3, *lock);
    }
  }

  {
    auto lock = guarded.lockExclusive();
    KJ_EXPECT(*lock == 4, *lock);
  }
}

KJ_TEST("ExternalMutexGuarded<T> destroy without release") {
  MutexGuarded<uint> guarded(0);

  {
    ExternalMutexGuarded<OnlyTouchUnderLock> ext;

    {
      auto lock = guarded.lockExclusive();
      ext.set(lock, guarded);
      KJ_EXPECT(*lock == 1);
      ext.get(lock).frob();
      KJ_EXPECT(*lock == 2);
    }
  }

  {
    auto lock = guarded.lockExclusive();
    KJ_EXPECT(*lock == 3);
  }
}

KJ_TEST("condvar wait with flapping predicate") {
  // This used to deadlock under some implementations due to a wait() checking its own predicate
  // as part of unlock()ing the mutex. Adding `waiterToSkip` fixed this (and also eliminated a
  // redundant call to the predicate).

  MutexGuarded<uint> guarded(0);

  Thread thread([&]() {
    delay();
    *guarded.lockExclusive() = 1;
  });

  {
    auto lock = guarded.lockExclusive();
    bool flap = true;
    lock.wait([&](uint i) {
      flap = !flap;
      return i == 1 || flap;
    });
  }
}

#if KJ_TRACK_LOCK_BLOCKING
#if !__GLIBC_PREREQ(2, 30)
#ifndef SYS_gettid
#error SYS_gettid is unavailable on this system
#endif

#define gettid() ((pid_t)syscall(SYS_gettid))
#endif

KJ_TEST("tracking blocking on mutex acquisition") {
  // SIGEV_THREAD is supposed to be "private" to the pthreads implementation, but, as
  // usual, the higher-level POSIX API that we're supposed to use sucks: the "handler" runs on
  // some other thread, which means the stack trace it prints won't be useful.
  //
  // So, we cheat and work around libc.
  MutexGuarded<int> foo(5);
  auto lock = foo.lockExclusive();

  struct BlockDetected {
    volatile bool blockedOnMutexAcquisition;
    SourceLocation blockLocation;
  } blockingInfo = {};

  struct sigaction handler;
  memset(&handler, 0, sizeof(handler));
  handler.sa_sigaction = [](int, siginfo_t* info, void*) {
    auto& blockage = *reinterpret_cast<BlockDetected *>(info->si_value.sival_ptr);
    KJ_IF_MAYBE(r, blockedReason()) {
      KJ_SWITCH_ONEOF(*r) {
        KJ_CASE_ONEOF(b, BlockedOnMutexAcquisition) {
          blockage.blockedOnMutexAcquisition = true;
          blockage.blockLocation = b.origin;
        }
        KJ_CASE_ONEOF_DEFAULT {}
      }
    }
  };
  handler.sa_flags = SA_SIGINFO | SA_RESTART;

  sigaction(SIGINT, &handler, nullptr);

  timer_t timer;
  struct sigevent event;
  memset(&event, 0, sizeof(event));
  event.sigev_notify = SIGEV_THREAD_ID;
  event.sigev_signo = SIGINT;
  event.sigev_value.sival_ptr = &blockingInfo;
  KJ_SYSCALL(event._sigev_un._tid = gettid());
  KJ_SYSCALL(timer_create(CLOCK_MONOTONIC, &event, &timer));
  KJ_DEFER(timer_delete(timer));

  kj::Duration timeout = 50 * MILLISECONDS;
  struct itimerspec spec;
  memset(&spec, 0, sizeof(spec));
  spec.it_value.tv_sec = timeout / kj::SECONDS;
  spec.it_value.tv_nsec = timeout % kj::SECONDS / kj::NANOSECONDS;
  // We can't use KJ_SYSCALL() because it is not async-signal-safe.
  KJ_REQUIRE(-1 != timer_settime(timer, 0, &spec, nullptr));

  kj::SourceLocation expectedBlockLocation;
  KJ_REQUIRE(foo.lockSharedWithTimeout(100 * MILLISECONDS, expectedBlockLocation) == nullptr);

  KJ_EXPECT(blockingInfo.blockedOnMutexAcquisition);
  KJ_EXPECT(blockingInfo.blockLocation == expectedBlockLocation);
}

KJ_TEST("tracking blocked on CondVar::wait") {
  // SIGEV_THREAD is supposed to be "private" to the pthreads implementation, but, as
  // usual, the higher-level POSIX API that we're supposed to use sucks: the "handler" runs on
  // some other thread, which means the stack trace it prints won't be useful.
  //
  // So, we cheat and work around libc.
  MutexGuarded<int> foo(5);
  auto lock = foo.lockExclusive();

  struct BlockDetected {
    volatile bool blockedOnCondVar;
    SourceLocation blockLocation;
  } blockingInfo = {};

  struct sigaction handler;
  memset(&handler, 0, sizeof(handler));
  handler.sa_sigaction = [](int, siginfo_t* info, void*) {
    auto& blockage = *reinterpret_cast<BlockDetected *>(info->si_value.sival_ptr);
    KJ_IF_MAYBE(r, blockedReason()) {
      KJ_SWITCH_ONEOF(*r) {
        KJ_CASE_ONEOF(b, BlockedOnCondVarWait) {
          blockage.blockedOnCondVar = true;
          blockage.blockLocation = b.origin;
        }
        KJ_CASE_ONEOF_DEFAULT {}
      }
    }
  };
  handler.sa_flags = SA_SIGINFO | SA_RESTART;

  sigaction(SIGINT, &handler, nullptr);

  timer_t timer;
  struct sigevent event;
  memset(&event, 0, sizeof(event));
  event.sigev_notify = SIGEV_THREAD_ID;
  event.sigev_signo = SIGINT;
  event.sigev_value.sival_ptr = &blockingInfo;
  KJ_SYSCALL(event._sigev_un._tid = gettid());
  KJ_SYSCALL(timer_create(CLOCK_MONOTONIC, &event, &timer));
  KJ_DEFER(timer_delete(timer));

  kj::Duration timeout = 50 * MILLISECONDS;
  struct itimerspec spec;
  memset(&spec, 0, sizeof(spec));
  spec.it_value.tv_sec = timeout / kj::SECONDS;
  spec.it_value.tv_nsec = timeout % kj::SECONDS / kj::NANOSECONDS;
  // We can't use KJ_SYSCALL() because it is not async-signal-safe.
  KJ_REQUIRE(-1 != timer_settime(timer, 0, &spec, nullptr));

  SourceLocation waitLocation;

  lock.wait([](const int& value) {
    return false;
  }, 100 * MILLISECONDS, waitLocation);

  KJ_EXPECT(blockingInfo.blockedOnCondVar);
  KJ_EXPECT(blockingInfo.blockLocation == waitLocation);
}

KJ_TEST("tracking blocked on Once::init") {
  // SIGEV_THREAD is supposed to be "private" to the pthreads implementation, but, as
  // usual, the higher-level POSIX API that we're supposed to use sucks: the "handler" runs on
  // some other thread, which means the stack trace it prints won't be useful.
  //
  // So, we cheat and work around libc.
  struct BlockDetected {
    volatile bool blockedOnOnceInit;
    SourceLocation blockLocation;
  } blockingInfo = {};

  struct sigaction handler;
  memset(&handler, 0, sizeof(handler));
  handler.sa_sigaction = [](int, siginfo_t* info, void*) {
    auto& blockage = *reinterpret_cast<BlockDetected *>(info->si_value.sival_ptr);
    KJ_IF_MAYBE(r, blockedReason()) {
      KJ_SWITCH_ONEOF(*r) {
        KJ_CASE_ONEOF(b, BlockedOnOnceInit) {
          blockage.blockedOnOnceInit = true;
          blockage.blockLocation = b.origin;
        }
        KJ_CASE_ONEOF_DEFAULT {}
      }
    }
  };
  handler.sa_flags = SA_SIGINFO | SA_RESTART;

  sigaction(SIGINT, &handler, nullptr);

  timer_t timer;
  struct sigevent event;
  memset(&event, 0, sizeof(event));
  event.sigev_notify = SIGEV_THREAD_ID;
  event.sigev_signo = SIGINT;
  event.sigev_value.sival_ptr = &blockingInfo;
  KJ_SYSCALL(event._sigev_un._tid = gettid());
  KJ_SYSCALL(timer_create(CLOCK_MONOTONIC, &event, &timer));
  KJ_DEFER(timer_delete(timer));

  Lazy<int> once;
  MutexGuarded<bool> onceInitializing(false);

  Thread backgroundInit([&] {
    once.get([&](SpaceFor<int>& x) {
      *onceInitializing.lockExclusive() = true;
      usleep(100 * 1000);  // 100 ms
      return x.construct(5);
    });
  });

  kj::Duration timeout = 50 * MILLISECONDS;
  struct itimerspec spec;
  memset(&spec, 0, sizeof(spec));
  spec.it_value.tv_sec = timeout / kj::SECONDS;
  spec.it_value.tv_nsec = timeout % kj::SECONDS / kj::NANOSECONDS;
  // We can't use KJ_SYSCALL() because it is not async-signal-safe.
  KJ_REQUIRE(-1 != timer_settime(timer, 0, &spec, nullptr));

  kj::SourceLocation onceInitializingBlocked;

  onceInitializing.lockExclusive().wait([](const bool& initializing) {
    return initializing;
  });

  once.get([](SpaceFor<int>& x) {
      return x.construct(5);
  }, onceInitializingBlocked);

  KJ_EXPECT(blockingInfo.blockedOnOnceInit);
  KJ_EXPECT(blockingInfo.blockLocation == onceInitializingBlocked);
}

#if KJ_SAVE_ACQUIRED_LOCK_INFO
KJ_TEST("get location of exclusive mutex") {
  _::Mutex mutex;
  kj::SourceLocation lockAcquisition;
  mutex.lock(_::Mutex::EXCLUSIVE, nullptr, lockAcquisition);
  KJ_DEFER(mutex.unlock(_::Mutex::EXCLUSIVE));

  const auto& lockedInfo = mutex.lockedInfo();
  const auto& lockInfo = lockedInfo.get<_::HoldingExclusively>();
  EXPECT_EQ(gettid(), lockInfo.threadHoldingLock());
  KJ_EXPECT(lockInfo.lockAcquiredAt() == lockAcquisition);
}

KJ_TEST("get location of shared mutex") {
  _::Mutex mutex;
  kj::SourceLocation lockLocation;
  mutex.lock(_::Mutex::SHARED, nullptr, lockLocation);
  KJ_DEFER(mutex.unlock(_::Mutex::SHARED));

  const auto& lockedInfo = mutex.lockedInfo();
  const auto& lockInfo = lockedInfo.get<_::HoldingShared>();
  KJ_EXPECT(lockInfo.lockAcquiredAt() == lockLocation);
}
#endif

#endif

#ifdef KJ_CONTENTION_WARNING_THRESHOLD
KJ_TEST("make sure contended mutex warns") {
  class Expectation final: public ExceptionCallback {
  public:
    Expectation(LogSeverity severity, StringPtr substring) :
        severity(severity), substring(substring), seen(false) {}

    void logMessage(LogSeverity severity, const char* file, int line, int contextDepth,
                    String&& text) override {
      if (!seen && severity == this->severity) {
        if (_::hasSubstring(text, substring)) {
          // Match. Ignore it.
          seen = true;
          return;
        }
      }

      // Pass up the chain.
      ExceptionCallback::logMessage(severity, file, line, contextDepth, kj::mv(text));
    }

    bool hasSeen() const {
      return seen;
    }

  private:
    LogSeverity severity;
    StringPtr substring;
    bool seen;
    UnwindDetector unwindDetector;
  };

  _::Mutex mutex;
  LockSourceLocation exclusiveLockLocation;
  mutex.lock(_::Mutex::EXCLUSIVE, nullptr, exclusiveLockLocation);

  bool seenContendedLockLog = false;

  auto threads = kj::heapArrayBuilder<kj::Own<kj::Thread>>(KJ_CONTENTION_WARNING_THRESHOLD);
  for (auto i: kj::zeroTo(KJ_CONTENTION_WARNING_THRESHOLD)) {
    (void)i;
    threads.add(kj::heap<kj::Thread>([&mutex, &seenContendedLockLog]() {
      Expectation expectation(LogSeverity::WARNING, "Acquired contended lock");
      LockSourceLocation sharedLockLocation;
      mutex.lock(_::Mutex::SHARED, nullptr, sharedLockLocation);
      seenContendedLockLog = seenContendedLockLog || expectation.hasSeen();
      mutex.unlock(_::Mutex::SHARED);
    }));
  }

  while (mutex.numReadersWaitingForTest() < KJ_CONTENTION_WARNING_THRESHOLD) {
    usleep(5 * kj::MILLISECONDS / kj::MICROSECONDS);
  }

  {
    KJ_EXPECT_LOG(WARNING, "excessively many readers were waiting on this lock");
    mutex.unlock(_::Mutex::EXCLUSIVE);
  }

  threads.clear();

  KJ_ASSERT(seenContendedLockLog);
}
#endif
}  // namespace
}  // namespace kj
