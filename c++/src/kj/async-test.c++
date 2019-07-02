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

#include "async.h"
#include "debug.h"
#include "thread.h"
#include "mutex.h"
#include <kj/compat/gtest.h>

#if _WIN32
#define WIN32_LEAN_AND_MEAN 1  // lolz
#include <windows.h>
inline void delay() { Sleep(10); }
#else
#include <unistd.h>
inline void delay() { usleep(10000); }
#endif

namespace kj {
namespace {

#if !_MSC_VER
// TODO(msvc): GetFunctorStartAddress is not supported on MSVC currently, so skip the test.
TEST(Async, GetFunctorStartAddress) {
  EXPECT_TRUE(nullptr != _::GetFunctorStartAddress<>::apply([](){return 0;}));
}
#endif

TEST(Async, EvalVoid) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool done = false;

  Promise<void> promise = evalLater([&]() { done = true; });
  EXPECT_FALSE(done);
  promise.wait(waitScope);
  EXPECT_TRUE(done);
}

TEST(Async, EvalInt) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool done = false;

  Promise<int> promise = evalLater([&]() { done = true; return 123; });
  EXPECT_FALSE(done);
  EXPECT_EQ(123, promise.wait(waitScope));
  EXPECT_TRUE(done);
}

TEST(Async, There) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> a = 123;
  bool done = false;

  Promise<int> promise = a.then([&](int ai) { done = true; return ai + 321; });
  EXPECT_FALSE(done);
  EXPECT_EQ(444, promise.wait(waitScope));
  EXPECT_TRUE(done);
}

TEST(Async, ThereVoid) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> a = 123;
  int value = 0;

  Promise<void> promise = a.then([&](int ai) { value = ai; });
  EXPECT_EQ(0, value);
  promise.wait(waitScope);
  EXPECT_EQ(123, value);
}

TEST(Async, Exception) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater(
      [&]() -> int { KJ_FAIL_ASSERT("foo") { return 123; } });
  EXPECT_TRUE(kj::runCatchingExceptions([&]() {
    // wait() only returns when compiling with -fno-exceptions.
    EXPECT_EQ(123, promise.wait(waitScope));
  }) != nullptr);
}

TEST(Async, HandleException) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater(
      [&]() -> int { KJ_FAIL_ASSERT("foo") { return 123; } });
  int line = __LINE__ - 1;

  promise = promise.then(
      [](int i) { return i + 1; },
      [&](Exception&& e) { EXPECT_EQ(line, e.getLine()); return 345; });

  EXPECT_EQ(345, promise.wait(waitScope));
}

TEST(Async, PropagateException) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater(
      [&]() -> int { KJ_FAIL_ASSERT("foo") { return 123; } });
  int line = __LINE__ - 1;

  promise = promise.then([](int i) { return i + 1; });

  promise = promise.then(
      [](int i) { return i + 2; },
      [&](Exception&& e) { EXPECT_EQ(line, e.getLine()); return 345; });

  EXPECT_EQ(345, promise.wait(waitScope));
}

TEST(Async, PropagateExceptionTypeChange) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater(
      [&]() -> int { KJ_FAIL_ASSERT("foo") { return 123; } });
  int line = __LINE__ - 1;

  Promise<StringPtr> promise2 = promise.then([](int i) -> StringPtr { return "foo"; });

  promise2 = promise2.then(
      [](StringPtr s) -> StringPtr { return "bar"; },
      [&](Exception&& e) -> StringPtr { EXPECT_EQ(line, e.getLine()); return "baz"; });

  EXPECT_EQ("baz", promise2.wait(waitScope));
}

TEST(Async, Then) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool done = false;

  Promise<int> promise = Promise<int>(123).then([&](int i) {
    done = true;
    return i + 321;
  });

  EXPECT_FALSE(done);

  EXPECT_EQ(444, promise.wait(waitScope));

  EXPECT_TRUE(done);
}

TEST(Async, Chain) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater([&]() -> int { return 123; });
  Promise<int> promise2 = evalLater([&]() -> int { return 321; });

  auto promise3 = promise.then([&](int i) {
    return promise2.then([i](int j) {
      return i + j;
    });
  });

  EXPECT_EQ(444, promise3.wait(waitScope));
}

TEST(Async, DeepChain) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<void> promise = NEVER_DONE;

  // Create a ridiculous chain of promises.
  for (uint i = 0; i < 1000; i++) {
    promise = evalLater(mvCapture(promise, [](Promise<void> promise) {
      return kj::mv(promise);
    }));
  }

  loop.run();

  auto trace = promise.trace();
  uint lines = 0;
  for (char c: trace) {
    lines += c == '\n';
  }

  // Chain nodes should have been collapsed such that instead of a chain of 1000 nodes, we have
  // 2-ish nodes.  We'll give a little room for implementation freedom.
  EXPECT_LT(lines, 5);
}

TEST(Async, DeepChain2) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<void> promise = nullptr;
  promise = evalLater([&]() {
    auto trace = promise.trace();
    uint lines = 0;
    for (char c: trace) {
      lines += c == '\n';
    }

    // Chain nodes should have been collapsed such that instead of a chain of 1000 nodes, we have
    // 2-ish nodes.  We'll give a little room for implementation freedom.
    EXPECT_LT(lines, 5);
  });

  // Create a ridiculous chain of promises.
  for (uint i = 0; i < 1000; i++) {
    promise = evalLater(mvCapture(promise, [](Promise<void> promise) {
      return kj::mv(promise);
    }));
  }

  promise.wait(waitScope);
}

Promise<void> makeChain(uint i) {
  if (i > 0) {
    return evalLater([i]() -> Promise<void> {
      return makeChain(i - 1);
    });
  } else {
    return NEVER_DONE;
  }
}

TEST(Async, DeepChain3) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<void> promise = makeChain(1000);

  loop.run();

  auto trace = promise.trace();
  uint lines = 0;
  for (char c: trace) {
    lines += c == '\n';
  }

  // Chain nodes should have been collapsed such that instead of a chain of 1000 nodes, we have
  // 2-ish nodes.  We'll give a little room for implementation freedom.
  EXPECT_LT(lines, 5);
}

Promise<void> makeChain2(uint i, Promise<void> promise) {
  if (i > 0) {
    return evalLater(mvCapture(promise, [i](Promise<void>&& promise) -> Promise<void> {
      return makeChain2(i - 1, kj::mv(promise));
    }));
  } else {
    return kj::mv(promise);
  }
}

TEST(Async, DeepChain4) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<void> promise = nullptr;
  promise = evalLater([&]() {
    auto trace = promise.trace();
    uint lines = 0;
    for (char c: trace) {
      lines += c == '\n';
    }

    // Chain nodes should have been collapsed such that instead of a chain of 1000 nodes, we have
    // 2-ish nodes.  We'll give a little room for implementation freedom.
    EXPECT_LT(lines, 5);
  });

  promise = makeChain2(1000, kj::mv(promise));

  promise.wait(waitScope);
}

TEST(Async, IgnoreResult) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool done = false;

  Promise<void> promise = Promise<int>(123).then([&](int i) {
    done = true;
    return i + 321;
  }).ignoreResult();

  EXPECT_FALSE(done);

  promise.wait(waitScope);

  EXPECT_TRUE(done);
}

TEST(Async, SeparateFulfiller) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto pair = newPromiseAndFulfiller<int>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.fulfiller->fulfill(123);
  EXPECT_FALSE(pair.fulfiller->isWaiting());

  EXPECT_EQ(123, pair.promise.wait(waitScope));
}

TEST(Async, SeparateFulfillerVoid) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto pair = newPromiseAndFulfiller<void>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.fulfiller->fulfill();
  EXPECT_FALSE(pair.fulfiller->isWaiting());

  pair.promise.wait(waitScope);
}

TEST(Async, SeparateFulfillerCanceled) {
  auto pair = newPromiseAndFulfiller<void>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.promise = nullptr;
  EXPECT_FALSE(pair.fulfiller->isWaiting());
}

TEST(Async, SeparateFulfillerChained) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto pair = newPromiseAndFulfiller<Promise<int>>();
  auto inner = newPromiseAndFulfiller<int>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.fulfiller->fulfill(kj::mv(inner.promise));
  EXPECT_FALSE(pair.fulfiller->isWaiting());

  inner.fulfiller->fulfill(123);

  EXPECT_EQ(123, pair.promise.wait(waitScope));
}

TEST(Async, SeparateFulfillerDiscarded) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto pair = newPromiseAndFulfiller<int>();
  pair.fulfiller = nullptr;

  EXPECT_ANY_THROW(pair.promise.wait(waitScope));
}

TEST(Async, SeparateFulfillerMemoryLeak) {
  auto paf = kj::newPromiseAndFulfiller<void>();
  paf.fulfiller->fulfill();
}

TEST(Async, Ordering) {
  EventLoop loop;
  WaitScope waitScope(loop);

  int counter = 0;
  Promise<void> promises[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

  promises[1] = evalLater([&]() {
    EXPECT_EQ(0, counter++);

    {
      // Use a promise and fulfiller so that we can fulfill the promise after waiting on it in
      // order to induce depth-first scheduling.
      auto paf = kj::newPromiseAndFulfiller<void>();
      promises[2] = paf.promise.then([&]() {
        EXPECT_EQ(1, counter++);
      }).eagerlyEvaluate(nullptr);
      paf.fulfiller->fulfill();
    }

    // .then() is scheduled breadth-first if the promise has already resolved, but depth-first
    // if the promise resolves later.
    promises[3] = Promise<void>(READY_NOW).then([&]() {
      EXPECT_EQ(4, counter++);
    }).then([&]() {
      EXPECT_EQ(5, counter++);
    }).eagerlyEvaluate(nullptr);

    {
      auto paf = kj::newPromiseAndFulfiller<void>();
      promises[4] = paf.promise.then([&]() {
        EXPECT_EQ(2, counter++);
      }).eagerlyEvaluate(nullptr);
      paf.fulfiller->fulfill();
    }

    // evalLater() is like READY_NOW.then().
    promises[5] = evalLater([&]() {
      EXPECT_EQ(6, counter++);
    }).eagerlyEvaluate(nullptr);
  }).eagerlyEvaluate(nullptr);

  promises[0] = evalLater([&]() {
    EXPECT_EQ(3, counter++);

    // Making this a chain should NOT cause it to preempt promises[1].  (This was a problem at one
    // point.)
    return Promise<void>(READY_NOW);
  }).eagerlyEvaluate(nullptr);

  for (auto i: indices(promises)) {
    kj::mv(promises[i]).wait(waitScope);
  }

  EXPECT_EQ(7, counter);
}

TEST(Async, Fork) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater([&]() { return 123; });

  auto fork = promise.fork();

  auto branch1 = fork.addBranch().then([](int i) {
    EXPECT_EQ(123, i);
    return 456;
  });
  auto branch2 = fork.addBranch().then([](int i) {
    EXPECT_EQ(123, i);
    return 789;
  });

  {
    auto releaseFork = kj::mv(fork);
  }

  EXPECT_EQ(456, branch1.wait(waitScope));
  EXPECT_EQ(789, branch2.wait(waitScope));
}

struct RefcountedInt: public Refcounted {
  RefcountedInt(int i): i(i) {}
  int i;
  Own<RefcountedInt> addRef() { return kj::addRef(*this); }
};

TEST(Async, ForkRef) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<Own<RefcountedInt>> promise = evalLater([&]() {
    return refcounted<RefcountedInt>(123);
  });

  auto fork = promise.fork();

  auto branch1 = fork.addBranch().then([](Own<RefcountedInt>&& i) {
    EXPECT_EQ(123, i->i);
    return 456;
  });
  auto branch2 = fork.addBranch().then([](Own<RefcountedInt>&& i) {
    EXPECT_EQ(123, i->i);
    return 789;
  });

  {
    auto releaseFork = kj::mv(fork);
  }

  EXPECT_EQ(456, branch1.wait(waitScope));
  EXPECT_EQ(789, branch2.wait(waitScope));
}

TEST(Async, Split) {
  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<Tuple<int, String, Promise<int>>> promise = evalLater([&]() {
    return kj::tuple(123, str("foo"), Promise<int>(321));
  });

  Tuple<Promise<int>, Promise<String>, Promise<int>> split = promise.split();

  EXPECT_EQ(123, get<0>(split).wait(waitScope));
  EXPECT_EQ("foo", get<1>(split).wait(waitScope));
  EXPECT_EQ(321, get<2>(split).wait(waitScope));
}

TEST(Async, ExclusiveJoin) {
  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([&]() { return 123; });
    auto right = newPromiseAndFulfiller<int>();  // never fulfilled

    EXPECT_EQ(123, left.exclusiveJoin(kj::mv(right.promise)).wait(waitScope));
  }

  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = newPromiseAndFulfiller<int>();  // never fulfilled
    auto right = evalLater([&]() { return 123; });

    EXPECT_EQ(123, left.promise.exclusiveJoin(kj::mv(right)).wait(waitScope));
  }

  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([&]() { return 123; });
    auto right = evalLater([&]() { return 456; });

    EXPECT_EQ(123, left.exclusiveJoin(kj::mv(right)).wait(waitScope));
  }

  {
    EventLoop loop;
    WaitScope waitScope(loop);

    auto left = evalLater([&]() { return 123; });
    auto right = evalLater([&]() { return 456; }).eagerlyEvaluate(nullptr);

    EXPECT_EQ(456, left.exclusiveJoin(kj::mv(right)).wait(waitScope));
  }
}

TEST(Async, ArrayJoin) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto builder = heapArrayBuilder<Promise<int>>(3);
  builder.add(123);
  builder.add(456);
  builder.add(789);

  Promise<Array<int>> promise = joinPromises(builder.finish());

  auto result = promise.wait(waitScope);

  ASSERT_EQ(3u, result.size());
  EXPECT_EQ(123, result[0]);
  EXPECT_EQ(456, result[1]);
  EXPECT_EQ(789, result[2]);
}

TEST(Async, ArrayJoinVoid) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto builder = heapArrayBuilder<Promise<void>>(3);
  builder.add(READY_NOW);
  builder.add(READY_NOW);
  builder.add(READY_NOW);

  Promise<void> promise = joinPromises(builder.finish());

  promise.wait(waitScope);
}

TEST(Async, Canceler) {
  EventLoop loop;
  WaitScope waitScope(loop);
  Canceler canceler;

  auto never = canceler.wrap(kj::Promise<void>(kj::NEVER_DONE));
  auto now = canceler.wrap(kj::Promise<void>(kj::READY_NOW));
  auto neverI = canceler.wrap(kj::Promise<void>(kj::NEVER_DONE).then([]() { return 123u; }));
  auto nowI = canceler.wrap(kj::Promise<uint>(123u));

  KJ_EXPECT(!never.poll(waitScope));
  KJ_EXPECT(now.poll(waitScope));
  KJ_EXPECT(!neverI.poll(waitScope));
  KJ_EXPECT(nowI.poll(waitScope));

  canceler.cancel("foobar");

  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("foobar", never.wait(waitScope));
  now.wait(waitScope);
  KJ_EXPECT_THROW_MESSAGE("foobar", neverI.wait(waitScope));
  KJ_EXPECT(nowI.wait(waitScope) == 123u);
}

class ErrorHandlerImpl: public TaskSet::ErrorHandler {
public:
  uint exceptionCount = 0;
  void taskFailed(kj::Exception&& exception) override {
    EXPECT_TRUE(exception.getDescription().endsWith("example TaskSet failure"));
    ++exceptionCount;
  }
};

TEST(Async, TaskSet) {
  EventLoop loop;
  WaitScope waitScope(loop);
  ErrorHandlerImpl errorHandler;
  TaskSet tasks(errorHandler);

  int counter = 0;

  tasks.add(evalLater([&]() {
    EXPECT_EQ(0, counter++);
  }));
  tasks.add(evalLater([&]() {
    EXPECT_EQ(1, counter++);
    KJ_FAIL_ASSERT("example TaskSet failure") { break; }
  }));
  tasks.add(evalLater([&]() {
    EXPECT_EQ(2, counter++);
  }));

  auto ignore KJ_UNUSED = evalLater([&]() {
    KJ_FAIL_EXPECT("Promise without waiter shouldn't execute.");
  });

  evalLater([&]() {
    EXPECT_EQ(3, counter++);
  }).wait(waitScope);

  EXPECT_EQ(4, counter);
  EXPECT_EQ(1u, errorHandler.exceptionCount);
}

TEST(Async, TaskSetOnEmpty) {
  EventLoop loop;
  WaitScope waitScope(loop);
  ErrorHandlerImpl errorHandler;
  TaskSet tasks(errorHandler);

  KJ_EXPECT(tasks.isEmpty());

  auto paf = newPromiseAndFulfiller<void>();
  tasks.add(kj::mv(paf.promise));
  tasks.add(evalLater([]() {}));

  KJ_EXPECT(!tasks.isEmpty());

  auto promise = tasks.onEmpty();
  KJ_EXPECT(!promise.poll(waitScope));
  KJ_EXPECT(!tasks.isEmpty());

  paf.fulfiller->fulfill();
  KJ_ASSERT(promise.poll(waitScope));
  KJ_EXPECT(tasks.isEmpty());
  promise.wait(waitScope);
}

class DestructorDetector {
public:
  DestructorDetector(bool& setTrue): setTrue(setTrue) {}
  ~DestructorDetector() { setTrue = true; }

private:
  bool& setTrue;
};

TEST(Async, Attach) {
  bool destroyed = false;

  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<int> promise = evalLater([&]() {
    EXPECT_FALSE(destroyed);
    return 123;
  }).attach(kj::heap<DestructorDetector>(destroyed));

  promise = promise.then([&](int i) {
    EXPECT_TRUE(destroyed);
    return i + 321;
  });

  EXPECT_FALSE(destroyed);
  EXPECT_EQ(444, promise.wait(waitScope));
  EXPECT_TRUE(destroyed);
}

TEST(Async, EagerlyEvaluate) {
  bool called = false;

  EventLoop loop;
  WaitScope waitScope(loop);

  Promise<void> promise = Promise<void>(READY_NOW).then([&]() {
    called = true;
  });
  evalLater([]() {}).wait(waitScope);

  EXPECT_FALSE(called);

  promise = promise.eagerlyEvaluate(nullptr);

  evalLater([]() {}).wait(waitScope);

  EXPECT_TRUE(called);
}

TEST(Async, Detach) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool ran1 = false;
  bool ran2 = false;
  bool ran3 = false;

  {
    // let returned promise be destroyed (canceled)
    auto ignore KJ_UNUSED = evalLater([&]() { ran1 = true; });
  }
  evalLater([&]() { ran2 = true; }).detach([](kj::Exception&&) { ADD_FAILURE(); });
  evalLater([]() { KJ_FAIL_ASSERT("foo"){break;} }).detach([&](kj::Exception&& e) { ran3 = true; });

  EXPECT_FALSE(ran1);
  EXPECT_FALSE(ran2);
  EXPECT_FALSE(ran3);

  evalLater([]() {}).wait(waitScope);

  EXPECT_FALSE(ran1);
  EXPECT_TRUE(ran2);
  EXPECT_TRUE(ran3);
}

class DummyEventPort: public EventPort {
public:
  bool runnable = false;
  int callCount = 0;

  bool wait() override { KJ_FAIL_ASSERT("Nothing to wait for."); }
  bool poll() override { return false; }
  void setRunnable(bool runnable) override {
    this->runnable = runnable;
    ++callCount;
  }
};

TEST(Async, SetRunnable) {
  DummyEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  EXPECT_FALSE(port.runnable);
  EXPECT_EQ(0, port.callCount);

  {
    auto promise = evalLater([]() {}).eagerlyEvaluate(nullptr);

    EXPECT_TRUE(port.runnable);
    loop.run(1);
    EXPECT_FALSE(port.runnable);
    EXPECT_EQ(2, port.callCount);

    promise.wait(waitScope);
    EXPECT_FALSE(port.runnable);
    EXPECT_EQ(4, port.callCount);
  }

  {
    auto paf = newPromiseAndFulfiller<void>();
    auto promise = paf.promise.then([]() {}).eagerlyEvaluate(nullptr);
    EXPECT_FALSE(port.runnable);

    auto promise2 = evalLater([]() {}).eagerlyEvaluate(nullptr);
    paf.fulfiller->fulfill();

    EXPECT_TRUE(port.runnable);
    loop.run(1);
    EXPECT_TRUE(port.runnable);
    loop.run(10);
    EXPECT_FALSE(port.runnable);

    promise.wait(waitScope);
    EXPECT_FALSE(port.runnable);

    EXPECT_EQ(8, port.callCount);
  }
}

TEST(Async, Poll) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto paf = newPromiseAndFulfiller<void>();
  KJ_ASSERT(!paf.promise.poll(waitScope));
  paf.fulfiller->fulfill();
  KJ_ASSERT(paf.promise.poll(waitScope));
  paf.promise.wait(waitScope);
}

KJ_TEST("exclusiveJoin both events complete simultaneously") {
  // Previously, if both branches of an exclusiveJoin() completed simultaneously, then the parent
  // event could be armed twice. This is an error, but the exact results of this error depend on
  // the parent PromiseNode type. One case where it matters is ArrayJoinPromiseNode, which counts
  // events and decides it is done when it has received exactly the number of events expected.

  EventLoop loop;
  WaitScope waitScope(loop);

  auto builder = kj::heapArrayBuilder<kj::Promise<uint>>(2);
  builder.add(kj::Promise<uint>(123).exclusiveJoin(kj::Promise<uint>(456)));
  builder.add(kj::NEVER_DONE);
  auto joined = kj::joinPromises(builder.finish());

  KJ_EXPECT(!joined.poll(waitScope));
}

KJ_TEST("synchonous simple cross-thread events") {
  MutexGuarded<kj::Maybe<const Executor&>> executor;  // to get the Executor from the other thread
  Own<PromiseFulfiller<uint>> fulfiller;  // accessed only from the subthread
  thread_local bool isChild = false;  // to assert which thread we're in

  // We use `noexcept` so that any uncaught exceptions immediately terminate the process without
  // unwinding. Otherwise, the unwind would likely deadlock waiting for some synchronization with
  // the other thread.
  Thread thread([&]() noexcept {
    isChild = true;

    EventLoop loop;
    WaitScope waitScope(loop);

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

    EventLoop loop;
    WaitScope waitScope(loop);

    auto paf = newPromiseAndFulfiller<uint>();
    fulfiller = kj::mv(paf.fulfiller);

    *executor.lockExclusive() = getCurrentThreadExecutor();

    KJ_ASSERT(paf.promise.wait(waitScope) == 123);

    // Wait until parent thread sets executor to null, as a way to tell us to quit.
    executor.lockExclusive().wait([](auto& val) { return val == nullptr; });
  });

  ([&]() noexcept {
    EventLoop loop;
    WaitScope waitScope(loop);

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

    EventLoop loop;
    WaitScope waitScope(loop);

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

    EventLoop loop;
    WaitScope waitScope(loop);

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
    EventLoop loop;
    WaitScope waitScope(loop);

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
    EventLoop loop;
    WaitScope waitScope(loop);

    *executor.lockExclusive() = getCurrentThreadExecutor();

    // We never run the loop here, so that when the event is canceled, it's still queued.

    // Wait until parent thread sets executor to null, as a way to tell us to quit.
    executor.lockExclusive().wait([](auto& val) { return val == nullptr; });
  });

  ([&]() noexcept {
    EventLoop loop;
    WaitScope waitScope(loop);

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
    EventLoop loop;
    WaitScope waitScope(loop);

    auto paf = newPromiseAndFulfiller<void>();
    fulfiller = kj::mv(paf.fulfiller);

    *executor.lockExclusive() = getCurrentThreadExecutor();

    paf.promise.wait(waitScope);

    // Wait until parent thread sets executor to null, as a way to tell us to quit.
    executor.lockExclusive().wait([](auto& val) { return val == nullptr; });
  });

  ([&]() noexcept {
    EventLoop loop;
    WaitScope waitScope(loop);

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

}  // namespace
}  // namespace kj
