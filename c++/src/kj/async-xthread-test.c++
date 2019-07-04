// Copyright (c) 2019 Cloudflare, Inc. and contributors
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

#include "async.h"
#include "debug.h"
#include "thread.h"
#include "mutex.h"
#include <kj/test.h>

#if _WIN32
#define WIN32_LEAN_AND_MEAN 1  // lolz
#include <windows.h>
#include "windows-sanity.h"
inline void delay() { Sleep(10); }
#else
#include <unistd.h>
inline void delay() { usleep(10000); }
#endif

// This file is #included from async-unix-xthread-test.c++ and async-win32-xthread-test.c++ after
// defining KJ_XTHREAD_TEST_SETUP_LOOP to set up a loop with the corresponding EventPort.
#ifndef KJ_XTHREAD_TEST_SETUP_LOOP
#define KJ_XTHREAD_TEST_SETUP_LOOP \
  EventLoop loop; \
  WaitScope waitScope(loop)
#endif

namespace kj {
namespace {

KJ_TEST("synchonous simple cross-thread events") {
  MutexGuarded<kj::Maybe<const Executor&>> executor;  // to get the Executor from the other thread
  Own<PromiseFulfiller<uint>> fulfiller;  // accessed only from the subthread
  thread_local bool isChild = false;  // to assert which thread we're in

  // We use `noexcept` so that any uncaught exceptions immediately terminate the process without
  // unwinding. Otherwise, the unwind would likely deadlock waiting for some synchronization with
  // the other thread.
  Thread thread([&]() noexcept {
    isChild = true;

    KJ_XTHREAD_TEST_SETUP_LOOP;

    auto paf = newPromiseAndFulfiller<uint>();
    fulfiller = kj::mv(paf.fulfiller);

    *executor.lockExclusive() = getCurrentThreadExecutor();

    KJ_ASSERT(paf.promise.wait(waitScope) == 123);

    // Wait until parent thread sets executor to null, as a way to tell us to quit.
    executor.lockExclusive().wait([](auto& val) { return val == nullptr; });
  });

  ([&]() noexcept {
    const Executor* exec;
    {
      auto lock = executor.lockExclusive();
      lock.wait([&](kj::Maybe<const Executor&> value) { return value != nullptr; });
      exec = &KJ_ASSERT_NONNULL(*lock);
    }

    KJ_ASSERT(!isChild);

    KJ_EXPECT_THROW_MESSAGE("test exception", exec->executeSync([&]() {
      KJ_ASSERT(isChild);
      KJ_FAIL_ASSERT("test exception") { break; }
    }));

    uint i = exec->executeSync([&]() {
      KJ_ASSERT(isChild);
      fulfiller->fulfill(123);
      return 456;
    });
    KJ_EXPECT(i == 456);

    *executor.lockExclusive() = nullptr;
  })();
}

KJ_TEST("asynchonous simple cross-thread events") {
  MutexGuarded<kj::Maybe<const Executor&>> executor;  // to get the Executor from the other thread
  Own<PromiseFulfiller<uint>> fulfiller;  // accessed only from the subthread
  thread_local bool isChild = false;  // to assert which thread we're in

  // We use `noexcept` so that any uncaught exceptions immediately terminate the process without
  // unwinding. Otherwise, the unwind would likely deadlock waiting for some synchronization with
  // the other thread.
  Thread thread([&]() noexcept {
    isChild = true;

    KJ_XTHREAD_TEST_SETUP_LOOP;

    auto paf = newPromiseAndFulfiller<uint>();
    fulfiller = kj::mv(paf.fulfiller);

    *executor.lockExclusive() = getCurrentThreadExecutor();

    KJ_ASSERT(paf.promise.wait(waitScope) == 123);

    // Wait until parent thread sets executor to null, as a way to tell us to quit.
    executor.lockExclusive().wait([](auto& val) { return val == nullptr; });
  });

  ([&]() noexcept {
    KJ_XTHREAD_TEST_SETUP_LOOP;

    const Executor* exec;
    {
      auto lock = executor.lockExclusive();
      lock.wait([&](kj::Maybe<const Executor&> value) { return value != nullptr; });
      exec = &KJ_ASSERT_NONNULL(*lock);
    }

    KJ_ASSERT(!isChild);

    KJ_EXPECT_THROW_MESSAGE("test exception", exec->executeAsync([&]() {
      KJ_ASSERT(isChild);
      KJ_FAIL_ASSERT("test exception") { break; }
    }).wait(waitScope));

    Promise<uint> promise = exec->executeAsync([&]() {
      KJ_ASSERT(isChild);
      fulfiller->fulfill(123);
      return 456u;
    });
    KJ_EXPECT(promise.wait(waitScope) == 456);

    *executor.lockExclusive() = nullptr;
  })();
}

KJ_TEST("synchonous promise cross-thread events") {
  MutexGuarded<kj::Maybe<const Executor&>> executor;  // to get the Executor from the other thread
  Own<PromiseFulfiller<uint>> fulfiller;  // accessed only from the subthread
  Promise<uint> promise = nullptr;  // accessed only from the subthread
  thread_local bool isChild = false;  // to assert which thread we're in

  // We use `noexcept` so that any uncaught exceptions immediately terminate the process without
  // unwinding. Otherwise, the unwind would likely deadlock waiting for some synchronization with
  // the other thread.
  Thread thread([&]() noexcept {
    isChild = true;

    KJ_XTHREAD_TEST_SETUP_LOOP;

    auto paf = newPromiseAndFulfiller<uint>();
    fulfiller = kj::mv(paf.fulfiller);

    auto paf2 = newPromiseAndFulfiller<uint>();
    promise = kj::mv(paf2.promise);

    *executor.lockExclusive() = getCurrentThreadExecutor();

    KJ_ASSERT(paf.promise.wait(waitScope) == 123);

    paf2.fulfiller->fulfill(321);

    // Make sure reply gets sent.
    loop.run();

    // Wait until parent thread sets executor to null, as a way to tell us to quit.
    executor.lockExclusive().wait([](auto& val) { return val == nullptr; });
  });

  ([&]() noexcept {
    const Executor* exec;
    {
      auto lock = executor.lockExclusive();
      lock.wait([&](kj::Maybe<const Executor&> value) { return value != nullptr; });
      exec = &KJ_ASSERT_NONNULL(*lock);
    }

    KJ_ASSERT(!isChild);

    KJ_EXPECT_THROW_MESSAGE("test exception", exec->executeSync([&]() {
      KJ_ASSERT(isChild);
      return kj::Promise<void>(KJ_EXCEPTION(FAILED, "test exception"));
    }));

    uint i = exec->executeSync([&]() {
      KJ_ASSERT(isChild);
      fulfiller->fulfill(123);
      return kj::mv(promise);
    });
    KJ_EXPECT(i == 321);

    *executor.lockExclusive() = nullptr;
  })();
}

KJ_TEST("asynchonous promise cross-thread events") {
  MutexGuarded<kj::Maybe<const Executor&>> executor;  // to get the Executor from the other thread
  Own<PromiseFulfiller<uint>> fulfiller;  // accessed only from the subthread
  Promise<uint> promise = nullptr;  // accessed only from the subthread
  thread_local bool isChild = false;  // to assert which thread we're in

  // We use `noexcept` so that any uncaught exceptions immediately terminate the process without
  // unwinding. Otherwise, the unwind would likely deadlock waiting for some synchronization with
  // the other thread.
  Thread thread([&]() noexcept {
    isChild = true;

    KJ_XTHREAD_TEST_SETUP_LOOP;

    auto paf = newPromiseAndFulfiller<uint>();
    fulfiller = kj::mv(paf.fulfiller);

    auto paf2 = newPromiseAndFulfiller<uint>();
    promise = kj::mv(paf2.promise);

    *executor.lockExclusive() = getCurrentThreadExecutor();

    KJ_ASSERT(paf.promise.wait(waitScope) == 123);

    paf2.fulfiller->fulfill(321);

    // Make sure reply gets sent.
    loop.run();

    // Wait until parent thread sets executor to null, as a way to tell us to quit.
    executor.lockExclusive().wait([](auto& val) { return val == nullptr; });
  });

  ([&]() noexcept {
    KJ_XTHREAD_TEST_SETUP_LOOP;

    const Executor* exec;
    {
      auto lock = executor.lockExclusive();
      lock.wait([&](kj::Maybe<const Executor&> value) { return value != nullptr; });
      exec = &KJ_ASSERT_NONNULL(*lock);
    }

    KJ_ASSERT(!isChild);

    KJ_EXPECT_THROW_MESSAGE("test exception", exec->executeAsync([&]() {
      KJ_ASSERT(isChild);
      return kj::Promise<void>(KJ_EXCEPTION(FAILED, "test exception"));
    }).wait(waitScope));

    Promise<uint> promise2 = exec->executeAsync([&]() {
      KJ_ASSERT(isChild);
      fulfiller->fulfill(123);
      return kj::mv(promise);
    });
    KJ_EXPECT(promise2.wait(waitScope) == 321);

    *executor.lockExclusive() = nullptr;
  })();
}

KJ_TEST("cancel cross-thread event before it runs") {
  MutexGuarded<kj::Maybe<const Executor&>> executor;  // to get the Executor from the other thread

  // We use `noexcept` so that any uncaught exceptions immediately terminate the process without
  // unwinding. Otherwise, the unwind would likely deadlock waiting for some synchronization with
  // the other thread.
  Thread thread([&]() noexcept {
    KJ_XTHREAD_TEST_SETUP_LOOP;

    *executor.lockExclusive() = getCurrentThreadExecutor();

    // We never run the loop here, so that when the event is canceled, it's still queued.

    // Wait until parent thread sets executor to null, as a way to tell us to quit.
    executor.lockExclusive().wait([](auto& val) { return val == nullptr; });
  });

  ([&]() noexcept {
    KJ_XTHREAD_TEST_SETUP_LOOP;

    const Executor* exec;
    {
      auto lock = executor.lockExclusive();
      lock.wait([&](kj::Maybe<const Executor&> value) { return value != nullptr; });
      exec = &KJ_ASSERT_NONNULL(*lock);
    }

    {
      Promise<uint> promise = exec->executeAsync([&]() { return 123u; });
      delay();
      KJ_EXPECT(!promise.poll(waitScope));
    }

    *executor.lockExclusive() = nullptr;
  })();
}

KJ_TEST("cancel cross-thread event while it runs") {
  MutexGuarded<kj::Maybe<const Executor&>> executor;  // to get the Executor from the other thread
  Own<PromiseFulfiller<void>> fulfiller;  // accessed only from the subthread

  // We use `noexcept` so that any uncaught exceptions immediately terminate the process without
  // unwinding. Otherwise, the unwind would likely deadlock waiting for some synchronization with
  // the other thread.
  Thread thread([&]() noexcept {
    KJ_XTHREAD_TEST_SETUP_LOOP;

    auto paf = newPromiseAndFulfiller<void>();
    fulfiller = kj::mv(paf.fulfiller);

    *executor.lockExclusive() = getCurrentThreadExecutor();

    paf.promise.wait(waitScope);

    // Wait until parent thread sets executor to null, as a way to tell us to quit.
    executor.lockExclusive().wait([](auto& val) { return val == nullptr; });
  });

  ([&]() noexcept {
    KJ_XTHREAD_TEST_SETUP_LOOP;

    const Executor* exec;
    {
      auto lock = executor.lockExclusive();
      lock.wait([&](kj::Maybe<const Executor&> value) { return value != nullptr; });
      exec = &KJ_ASSERT_NONNULL(*lock);
    }

    {
      Promise<uint> promise = exec->executeAsync([&]() -> kj::Promise<uint> {
        return kj::NEVER_DONE;
      });
      delay();
      KJ_EXPECT(!promise.poll(waitScope));
    }

    exec->executeSync([&]() { fulfiller->fulfill(); });

    *executor.lockExclusive() = nullptr;
  })();
}

KJ_TEST("cross-thread cancellation in both directions at once") {
  MutexGuarded<kj::Maybe<const Executor&>> childExecutor;
  MutexGuarded<kj::Maybe<const Executor&>> parentExecutor;

  MutexGuarded<uint> readyCount;

  thread_local bool isChild = false;

  // Code to execute simultaneously in two threads...
  // We mark this noexcept so that any exceptions thrown will immediately invoke the termination
  // handler, skipping any destructors that would deadlock.
  auto simultaneous = [&](MutexGuarded<kj::Maybe<const Executor&>>& selfExecutor,
                          MutexGuarded<kj::Maybe<const Executor&>>& otherExecutor) noexcept {
    KJ_XTHREAD_TEST_SETUP_LOOP;

    *selfExecutor.lockExclusive() = getCurrentThreadExecutor();

    const Executor* exec;
    {
      auto lock = otherExecutor.lockExclusive();
      lock.wait([&](kj::Maybe<const Executor&> value) { return value != nullptr; });
      exec = &KJ_ASSERT_NONNULL(*lock);
    }

    // Create a ton of cross-thread promises to cancel.
    Vector<Promise<void>> promises;
    for (uint i = 0; i < 1000; i++) {
      promises.add(exec->executeAsync([&]() -> kj::Promise<void> {
        return kj::Promise<void>(kj::NEVER_DONE)
            .attach(kj::defer([wasChild = isChild]() {
          // Make sure destruction happens in the correct thread.
          KJ_ASSERT(isChild == wasChild);
        }));
      }));
    }

    // Signal other thread that we're done queueing, and wait for it to signal same.
    {
      auto lock = readyCount.lockExclusive();
      ++*lock;
      lock.wait([](uint i) { return i == 2; });
    }

    // Run event loop to start all executions queued by the other thread.
    waitScope.poll();
    loop.run();

    // Signal other thread that we've run the loop, and wait for it to signal same.
    {
      auto lock = readyCount.lockExclusive();
      ++*lock;
      lock.wait([](uint i) { return i == 4; });
    }

    // Cancel all the promises.
    promises.clear();

    // All our cancellations completed, but the other thread may still be waiting for some
    // cancellations from us. We need to pump our event loop to make sure we continue handling
    // those cancellation requests. In particular we'll queue a function to the other thread and
    // wait for it to complete. The other thread will queue its own function to this thread just
    // before completing the function we queued to it.
    exec->executeAsync([]() {}).wait(waitScope);

    // To be safe, make sure we've actually executed the function that the other thread queued to
    // us by running the loop one last time.
    loop.run();

    // OK, signal other that we're all done.
    *otherExecutor.lockExclusive() = nullptr;

    // Wait until other thread sets executor to null, as a way to tell us to quit.
    selfExecutor.lockExclusive().wait([](auto& val) { return val == nullptr; });
  };

  Thread thread([&]() {
    isChild = true;
    simultaneous(childExecutor, parentExecutor);
  });

  simultaneous(parentExecutor, childExecutor);
}

KJ_TEST("call own thread's executor") {
  KJ_XTHREAD_TEST_SETUP_LOOP;

  auto& executor = getCurrentThreadExecutor();

  {
    uint i = executor.executeSync([]() {
      return 123u;
    });
    KJ_EXPECT(i == 123);
  }

  KJ_EXPECT_THROW_MESSAGE(
      "can't call executeSync() on own thread's executor with a promise-returning function",
      executor.executeSync([]() { return kj::evalLater([]() {}); }));

  {
    uint i = executor.executeAsync([]() {
      return 123u;
    }).wait(waitScope);
    KJ_EXPECT(i == 123);
  }
}

}  // namespace
}  // namespace kj
