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
#include "mutex.h"
#include "debug.h"
#include "thread.h"
#include <sched.h>
#include <gtest/gtest.h>

namespace kj {
namespace {

TEST(Async, EvalVoid) {
  SimpleEventLoop loop;

  bool done = false;

  Promise<void> promise = evalLater([&]() { done = true; });
  EXPECT_FALSE(done);
  promise.wait();
  EXPECT_TRUE(done);
}

TEST(Async, EvalInt) {
  SimpleEventLoop loop;

  bool done = false;

  Promise<int> promise = evalLater([&]() { done = true; return 123; });
  EXPECT_FALSE(done);
  EXPECT_EQ(123, promise.wait());
  EXPECT_TRUE(done);
}

TEST(Async, There) {
  SimpleEventLoop loop;

  Promise<int> a = 123;
  bool done = false;

  Promise<int> promise = a.then([&](int ai) { done = true; return ai + 321; });
  EXPECT_FALSE(done);
  EXPECT_EQ(444, promise.wait());
  EXPECT_TRUE(done);
}

TEST(Async, ThereVoid) {
  SimpleEventLoop loop;

  Promise<int> a = 123;
  int value = 0;

  Promise<void> promise = a.then([&](int ai) { value = ai; });
  EXPECT_EQ(0, value);
  promise.wait();
  EXPECT_EQ(123, value);
}

TEST(Async, Exception) {
  SimpleEventLoop loop;

  Promise<int> promise = evalLater(
      [&]() -> int { KJ_FAIL_ASSERT("foo") { return 123; } });
  EXPECT_TRUE(kj::runCatchingExceptions([&]() {
    // wait() only returns when compiling with -fno-exceptions.
    EXPECT_EQ(123, promise.wait());
  }) != nullptr);
}

TEST(Async, HandleException) {
  SimpleEventLoop loop;

  Promise<int> promise = evalLater(
      [&]() -> int { KJ_FAIL_ASSERT("foo") { return 123; } });
  int line = __LINE__ - 1;

  promise = promise.then(
      [](int i) { return i + 1; },
      [&](Exception&& e) { EXPECT_EQ(line, e.getLine()); return 345; });

  EXPECT_EQ(345, promise.wait());
}

TEST(Async, PropagateException) {
  SimpleEventLoop loop;

  Promise<int> promise = evalLater(
      [&]() -> int { KJ_FAIL_ASSERT("foo") { return 123; } });
  int line = __LINE__ - 1;

  promise = promise.then([](int i) { return i + 1; });

  promise = promise.then(
      [](int i) { return i + 2; },
      [&](Exception&& e) { EXPECT_EQ(line, e.getLine()); return 345; });

  EXPECT_EQ(345, promise.wait());
}

TEST(Async, PropagateExceptionTypeChange) {
  SimpleEventLoop loop;

  Promise<int> promise = evalLater(
      [&]() -> int { KJ_FAIL_ASSERT("foo") { return 123; } });
  int line = __LINE__ - 1;

  Promise<StringPtr> promise2 = promise.then([](int i) -> StringPtr { return "foo"; });

  promise2 = promise2.then(
      [](StringPtr s) -> StringPtr { return "bar"; },
      [&](Exception&& e) -> StringPtr { EXPECT_EQ(line, e.getLine()); return "baz"; });

  EXPECT_EQ("baz", promise2.wait());
}

TEST(Async, Then) {
  SimpleEventLoop loop;

  bool done = false;

  Promise<int> promise = Promise<int>(123).then([&](int i) {
    EXPECT_EQ(&loop, &EventLoop::current());
    done = true;
    return i + 321;
  });

  EXPECT_FALSE(done);

  EXPECT_EQ(444, promise.wait());

  EXPECT_TRUE(done);
}

TEST(Async, ThenInAnyThread) {
  SimpleEventLoop loop;

  Promise<int> a = 123;
  bool done = false;

  Promise<int> promise = a.thenInAnyThread([&](int ai) { done = true; return ai + 321; });
  EXPECT_FALSE(done);
  EXPECT_EQ(444, promise.wait());
  EXPECT_TRUE(done);
}

TEST(Async, Chain) {
  SimpleEventLoop loop;

  Promise<int> promise = evalLater([&]() -> int { return 123; });
  Promise<int> promise2 = evalLater([&]() -> int { return 321; });

  auto promise3 = promise.then([&](int i) {
    EXPECT_EQ(&loop, &EventLoop::current());
    return promise2.then([&loop,i](int j) {
      EXPECT_EQ(&loop, &EventLoop::current());
      return i + j;
    });
  });

  EXPECT_EQ(444, promise3.wait());
}

TEST(Async, SeparateFulfiller) {
  SimpleEventLoop loop;

  auto pair = newPromiseAndFulfiller<int>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.fulfiller->fulfill(123);
  EXPECT_FALSE(pair.fulfiller->isWaiting());

  EXPECT_EQ(123, pair.promise.wait());
}

TEST(Async, SeparateFulfillerVoid) {
  SimpleEventLoop loop;

  auto pair = newPromiseAndFulfiller<void>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.fulfiller->fulfill();
  EXPECT_FALSE(pair.fulfiller->isWaiting());

  pair.promise.wait();
}

TEST(Async, SeparateFulfillerCanceled) {
  auto pair = newPromiseAndFulfiller<void>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.promise.absolve();
  EXPECT_FALSE(pair.fulfiller->isWaiting());
}

TEST(Async, SeparateFulfillerChained) {
  SimpleEventLoop loop;

  auto pair = newPromiseAndFulfiller<Promise<int>>(loop);
  auto inner = newPromiseAndFulfiller<int>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.fulfiller->fulfill(kj::mv(inner.promise));
  EXPECT_FALSE(pair.fulfiller->isWaiting());

  inner.fulfiller->fulfill(123);

  EXPECT_EQ(123, pair.promise.wait());
}

#if KJ_NO_EXCEPTIONS
#undef EXPECT_ANY_THROW
#define EXPECT_ANY_THROW(code) EXPECT_DEATH(code, ".")
#endif

TEST(Async, SeparateFulfillerDiscarded) {
  SimpleEventLoop loop;

  auto pair = newPromiseAndFulfiller<int>();
  pair.fulfiller = nullptr;

  EXPECT_ANY_THROW(pair.promise.wait());
}

TEST(Async, Threads) {
  EXPECT_ANY_THROW(EventLoop::current());

  {
    SimpleEventLoop loop1;

    auto getThreadLoop = newPromiseAndFulfiller<const EventLoop*>();
    auto exitThread = newPromiseAndFulfiller<void>();

    Thread thread([&]() {
      EXPECT_ANY_THROW(EventLoop::current());
      {
        SimpleEventLoop threadLoop;
        getThreadLoop.fulfiller->fulfill(&threadLoop);
        exitThread.promise.wait();
      }
      EXPECT_ANY_THROW(EventLoop::current());
    });

    // Make sure the thread will exit.
    KJ_DEFER(exitThread.fulfiller->fulfill());

    const EventLoop& loop2 = *loop1.wait(kj::mv(getThreadLoop.promise));

    Promise<int> promise = evalLater([]() { return 123; });
    promise = loop2.there(kj::mv(promise), [](int ai) { return ai + 321; });

    for (uint i = 0; i < 100; i++) {
      promise = loop1.there(kj::mv(promise), [&](int ai) {
        EXPECT_EQ(&loop1, &EventLoop::current());
        return ai + 1;
      });
      promise = loop2.there(kj::mv(promise), [&](int ai) {
        EXPECT_EQ(&loop2, &EventLoop::current());
        return ai + 1000;
      });
    }

    EXPECT_EQ(100544, loop1.wait(kj::mv(promise)));
  }

  EXPECT_ANY_THROW(EventLoop::current());
}

TEST(Async, Ordering) {
  SimpleEventLoop loop1;

  auto getThreadLoop = newPromiseAndFulfiller<const EventLoop*>();
  auto exitThread = newPromiseAndFulfiller<void>();

  Thread thread([&]() {
    EXPECT_ANY_THROW(EventLoop::current());
    {
      SimpleEventLoop threadLoop;
      getThreadLoop.fulfiller->fulfill(&threadLoop);
      exitThread.promise.wait();
    }
    EXPECT_ANY_THROW(EventLoop::current());
  });

  // Make sure the thread will exit.
  KJ_DEFER(exitThread.fulfiller->fulfill());

  const EventLoop& loop2 = *loop1.wait(kj::mv(getThreadLoop.promise));

  int counter = 0;
  Promise<void> promises[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

  promises[1] = loop2.evalLater([&]() {
    EXPECT_EQ(0, counter++);
    promises[2] = Promise<void>(READY_NOW).then([&]() {
      EXPECT_EQ(1, counter++);
      return Promise<void>(READY_NOW);  // Force proactive evaluation by faking a chain.
    });
    promises[3] = loop2.evalLater([&]() {
      EXPECT_EQ(4, counter++);
      return Promise<void>(READY_NOW).then([&]() {
        EXPECT_EQ(5, counter++);
      });
    });
    promises[4] = Promise<void>(READY_NOW).then([&]() {
      EXPECT_EQ(2, counter++);
      return Promise<void>(READY_NOW);  // Force proactive evaluation by faking a chain.
    });
    promises[5] = loop2.evalLater([&]() {
      EXPECT_EQ(6, counter++);
    });
  });

  promises[0] = loop2.evalLater([&]() {
    EXPECT_EQ(3, counter++);

    // Making this a chain should NOT cause it to preempt promises[1].  (This was a problem at one
    // point.)
    return Promise<void>(READY_NOW);
  });

  for (auto i: indices(promises)) {
    loop1.wait(kj::mv(promises[i]));
  }

  EXPECT_EQ(7, counter);
}

TEST(Async, Spark) {
  // Tests that EventLoop::there() only runs eagerly when queued cross-thread.

  SimpleEventLoop loop;

  auto notification = newPromiseAndFulfiller<void>();;
  Promise<void> unsparked = nullptr;
  Promise<void> then = nullptr;
  Promise<void> later = nullptr;
  Promise<void> sparked = nullptr;

  Thread([&]() {
    // `sparked` will evaluate eagerly, even though we never wait on it, because there() is being
    // called from outside the event loop.
    sparked = loop.there(Promise<void>(READY_NOW), [&]() {
      // `unsparked` will never execute because it's attached to the current loop and we never wait
      // on it.
      unsparked = loop.there(Promise<void>(READY_NOW), [&]() {
        ADD_FAILURE() << "This continuation shouldn't happen because no one waits on it.";
      });
      // `then` will similarly never execute.
      then = Promise<void>(READY_NOW).then([&]() {
        ADD_FAILURE() << "This continuation shouldn't happen because no one waits on it.";
      });

      // `evalLater` *does* eagerly execute even when queued to the same loop.
      later = loop.evalLater([&]() {
        notification.fulfiller->fulfill();
      });
    });
  });

  notification.promise.wait();
}

TEST(Async, Fork) {
  SimpleEventLoop loop;

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

  EXPECT_EQ(456, branch1.wait());
  EXPECT_EQ(789, branch2.wait());
}

struct RefcountedInt: public Refcounted {
  RefcountedInt(int i): i(i) {}
  int i;
  Own<const RefcountedInt> addRef() const { return kj::addRef(*this); }
};

TEST(Async, ForkRef) {
  SimpleEventLoop loop;

  Promise<Own<RefcountedInt>> promise = evalLater([&]() {
    return refcounted<RefcountedInt>(123);
  });

  auto fork = promise.fork();

  auto branch1 = fork.addBranch().then([](Own<const RefcountedInt>&& i) {
    EXPECT_EQ(123, i->i);
    return 456;
  });
  auto branch2 = fork.addBranch().then([](Own<const RefcountedInt>&& i) {
    EXPECT_EQ(123, i->i);
    return 789;
  });

  {
    auto releaseFork = kj::mv(fork);
  }

  EXPECT_EQ(456, branch1.wait());
  EXPECT_EQ(789, branch2.wait());
}

TEST(Async, ExclusiveJoin) {
  {
    SimpleEventLoop loop;

    auto left = evalLater([&]() { return 123; });
    auto right = newPromiseAndFulfiller<int>();  // never fulfilled

    left.exclusiveJoin(kj::mv(right.promise));

    EXPECT_EQ(123, left.wait());
  }

  {
    SimpleEventLoop loop;

    auto left = newPromiseAndFulfiller<int>();  // never fulfilled
    auto right = evalLater([&]() { return 123; });

    left.promise.exclusiveJoin(kj::mv(right));

    EXPECT_EQ(123, left.promise.wait());
  }

  {
    SimpleEventLoop loop;

    auto left = evalLater([&]() { return 123; });
    auto right = evalLater([&]() { return 456; });

    left.exclusiveJoin(kj::mv(right));

    EXPECT_EQ(123, left.wait());
  }

  {
    SimpleEventLoop loop;

    auto right = evalLater([&]() { return 456; });
    auto left = evalLater([&]() { return 123; });

    left.exclusiveJoin(kj::mv(right));

    EXPECT_EQ(456, left.wait());
  }
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
  SimpleEventLoop loop;
  ErrorHandlerImpl errorHandler;
  TaskSet tasks(loop, errorHandler);

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
  }).wait();

  EXPECT_EQ(4, counter);
  EXPECT_EQ(1u, errorHandler.exceptionCount);
}

TEST(Async, EventLoopGuarded) {
  SimpleEventLoop loop;
  EventLoopGuarded<int> guarded(loop, 123);

  {
    EXPECT_EQ(123, guarded.getValue());

    kj::Promise<const char*> promise = nullptr;

    Thread([&]() {
      // We're not in the event loop, so the function will be applied later.
      promise = guarded.applyNow([](int& i) -> const char* {
        EXPECT_EQ(123, i);
        i = 234;
        return "foo";
      });
    });

    EXPECT_EQ(123, guarded.getValue());

    EXPECT_STREQ("foo", promise.wait());

    EXPECT_EQ(234, guarded.getValue());
  }

  {
    auto promise = evalLater([&]() {
      EXPECT_EQ(234, guarded.getValue());

      // Since we're in the event loop, applyNow() will apply synchronously.
      auto promise = guarded.applyNow([](int& i) -> const char* {
        EXPECT_EQ(234, i);
        i = 345;
        return "bar";
      });

      EXPECT_EQ(345, guarded.getValue());  // already applied

      return kj::mv(promise);
    });

    EXPECT_STREQ("bar", promise.wait());

    EXPECT_EQ(345, guarded.getValue());
  }

  {
    auto promise = evalLater([&]() {
      EXPECT_EQ(345, guarded.getValue());

      // applyLater() is never synchronous.
      auto promise = guarded.applyLater([](int& i) -> const char* {
        EXPECT_EQ(345, i);
        i = 456;
        return "baz";
      });

      EXPECT_EQ(345, guarded.getValue());

      return kj::mv(promise);
    });

    EXPECT_STREQ("baz", promise.wait());

    EXPECT_EQ(456, guarded.getValue());
  }
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

  SimpleEventLoop loop;

  Promise<int> promise = evalLater([&]() {
    EXPECT_FALSE(destroyed);
    return 123;
  });

  promise.attach(kj::heap<DestructorDetector>(destroyed));

  promise = promise.then([&](int i) {
    EXPECT_TRUE(destroyed);
    return i + 321;
  });

  EXPECT_FALSE(destroyed);
  EXPECT_EQ(444, promise.wait());
  EXPECT_TRUE(destroyed);
}

TEST(Async, EagerlyEvaluate) {
  bool called = false;

  SimpleEventLoop loop;

  Promise<void> promise = Promise<void>(READY_NOW).then([&]() {
    called = true;
  });
  evalLater([]() {}).wait();

  EXPECT_FALSE(called);

  promise.eagerlyEvaluate(loop);

  evalLater([]() {}).wait();

  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace kj
