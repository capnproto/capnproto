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

#include "async.h"
#include "debug.h"
#include <gtest/gtest.h>

namespace kj {
namespace {

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
    return promise2.then([&loop,i](int j) {
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

#if KJ_NO_EXCEPTIONS
#undef EXPECT_ANY_THROW
#define EXPECT_ANY_THROW(code) EXPECT_DEATH(code, ".")
#endif

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

  (void)evalLater([&]() {
    ADD_FAILURE() << "Promise without waiter shouldn't execute.";
  });

  evalLater([&]() {
    EXPECT_EQ(3, counter++);
  }).wait(waitScope);

  EXPECT_EQ(4, counter);
  EXPECT_EQ(1u, errorHandler.exceptionCount);
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

  evalLater([&]() { ran1 = true; });
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

  void wait() override { KJ_FAIL_ASSERT("Nothing to wait for."); }
  void poll() override {}
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

}  // namespace
}  // namespace kj
