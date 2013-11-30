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
  pair.promise = nullptr;
  EXPECT_FALSE(pair.fulfiller->isWaiting());
}

TEST(Async, SeparateFulfillerChained) {
  SimpleEventLoop loop;

  auto pair = newPromiseAndFulfiller<Promise<int>>();
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

TEST(Async, SeparateFulfillerMemoryLeak) {
  auto paf = kj::newPromiseAndFulfiller<void>();
  paf.fulfiller->fulfill();
}

TEST(Async, Ordering) {
  SimpleEventLoop loop;

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
      });
      promises[2].eagerlyEvaluate();
      paf.fulfiller->fulfill();
    }

    // .then() is scheduled breadth-first if the promise has already resolved, but depth-first
    // if the promise resolves later.
    promises[3] = Promise<void>(READY_NOW).then([&]() {
      EXPECT_EQ(4, counter++);
    }).then([&]() {
      EXPECT_EQ(5, counter++);
    });
    promises[3].eagerlyEvaluate();

    {
      auto paf = kj::newPromiseAndFulfiller<void>();
      promises[4] = paf.promise.then([&]() {
        EXPECT_EQ(2, counter++);
      });
      promises[4].eagerlyEvaluate();
      paf.fulfiller->fulfill();
    }

    // evalLater() is like READY_NOW.then().
    promises[5] = evalLater([&]() {
      EXPECT_EQ(6, counter++);
    });
    promises[5].eagerlyEvaluate();
  });
  promises[1].eagerlyEvaluate();

  promises[0] = evalLater([&]() {
    EXPECT_EQ(3, counter++);

    // Making this a chain should NOT cause it to preempt promises[1].  (This was a problem at one
    // point.)
    return Promise<void>(READY_NOW);
  });
  promises[0].eagerlyEvaluate();

  for (auto i: indices(promises)) {
    kj::mv(promises[i]).wait();
  }

  EXPECT_EQ(7, counter);
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
  Own<RefcountedInt> addRef() { return kj::addRef(*this); }
};

TEST(Async, ForkRef) {
  SimpleEventLoop loop;

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

    auto left = evalLater([&]() { return 123; });
    auto right = evalLater([&]() { return 456; });

    right.eagerlyEvaluate();

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
  }).wait();

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

  promise.eagerlyEvaluate();

  evalLater([]() {}).wait();

  EXPECT_TRUE(called);
}

TEST(Async, Daemonize) {
  SimpleEventLoop loop;

  bool ran1 = false;
  bool ran2 = false;
  bool ran3 = false;

  evalLater([&]() { ran1 = true; });
  evalLater([&]() { ran2 = true; }).daemonize([](kj::Exception&&) { ADD_FAILURE(); });
  evalLater([]() { KJ_FAIL_ASSERT("foo"); }).daemonize([&](kj::Exception&& e) { ran3 = true; });

  EXPECT_FALSE(ran1);
  EXPECT_FALSE(ran2);
  EXPECT_FALSE(ran3);

  evalLater([]() {}).wait();

  EXPECT_FALSE(ran1);
  EXPECT_TRUE(ran2);
  EXPECT_TRUE(ran3);
}

}  // namespace
}  // namespace kj
