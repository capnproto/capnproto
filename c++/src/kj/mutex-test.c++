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

#include "mutex.h"
#include "debug.h"
#include "thread.h"
#include <kj/compat/gtest.h>

#if _WIN32
#define NOGDI  // NOGDI is needed to make EXPECT_EQ(123u, *lock) compile for some reason
#include <windows.h>
#undef NOGDI
#else
#include <pthread.h>
#include <unistd.h>
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

#if !_WIN32  // Not checked on win32.
  EXPECT_DEBUG_ANY_THROW(value.getAlreadyLockedExclusive());
  EXPECT_DEBUG_ANY_THROW(value.getAlreadyLockedShared());
#endif
  EXPECT_EQ(321u, value.getWithoutLock());
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

}  // namespace
}  // namespace kj
