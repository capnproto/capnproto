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
#include <gtest/gtest.h>

namespace kj {
namespace {

TEST(Async, EvalVoid) {
  SimpleEventLoop loop;

  bool done = false;

  Promise<void> promise = loop.evalLater([&]() { done = true; });
  EXPECT_FALSE(done);
  loop.wait(kj::mv(promise));
  EXPECT_TRUE(done);
}

TEST(Async, EvalInt) {
  SimpleEventLoop loop;

  bool done = false;

  Promise<int> promise = loop.evalLater([&]() { done = true; return 123; });
  EXPECT_FALSE(done);
  EXPECT_EQ(123, loop.wait(kj::mv(promise)));
  EXPECT_TRUE(done);
}

TEST(Async, There) {
  SimpleEventLoop loop;

  Promise<int> a = 123;
  bool done = false;

  Promise<int> promise = loop.there(kj::mv(a), [&](int ai) { done = true; return ai + 321; });
  EXPECT_FALSE(done);
  EXPECT_EQ(444, loop.wait(kj::mv(promise)));
  EXPECT_TRUE(done);
}

TEST(Async, ThereVoid) {
  SimpleEventLoop loop;

  Promise<int> a = 123;
  int value = 0;

  Promise<void> promise = loop.there(kj::mv(a), [&](int ai) { value = ai; });
  EXPECT_EQ(0, value);
  loop.wait(kj::mv(promise));
  EXPECT_EQ(123, value);
}

TEST(Async, Exception) {
  SimpleEventLoop loop;

  Promise<int> promise = loop.evalLater([&]() -> int { KJ_FAIL_ASSERT("foo") { return 123; } });
  EXPECT_TRUE(kj::runCatchingExceptions([&]() {
    // wait() only returns when compiling with -fno-exceptions.
    EXPECT_EQ(123, loop.wait(kj::mv(promise)));
  }) != nullptr);
}

TEST(Async, HandleException) {
  SimpleEventLoop loop;

  Promise<int> promise = loop.evalLater([&]() -> int { KJ_FAIL_ASSERT("foo") { return 123; } });
  int line = __LINE__ - 1;

  promise = loop.there(kj::mv(promise),
      [](int i) { return i + 1; },
      [&](Exception&& e) { EXPECT_EQ(line, e.getLine()); return 345; });

  EXPECT_EQ(345, loop.wait(kj::mv(promise)));
}

TEST(Async, PropagateException) {
  SimpleEventLoop loop;

  Promise<int> promise = loop.evalLater([&]() -> int { KJ_FAIL_ASSERT("foo") { return 123; } });
  int line = __LINE__ - 1;

  promise = loop.there(kj::mv(promise),
      [](int i) { return i + 1; });

  promise = loop.there(kj::mv(promise),
      [](int i) { return i + 2; },
      [&](Exception&& e) { EXPECT_EQ(line, e.getLine()); return 345; });

  EXPECT_EQ(345, loop.wait(kj::mv(promise)));
}

TEST(Async, PropagateExceptionTypeChange) {
  SimpleEventLoop loop;

  Promise<int> promise = loop.evalLater([&]() -> int { KJ_FAIL_ASSERT("foo") { return 123; } });
  int line = __LINE__ - 1;

  Promise<StringPtr> promise2 = loop.there(kj::mv(promise),
      [](int i) -> StringPtr { return "foo"; });

  promise2 = loop.there(kj::mv(promise2),
      [](StringPtr s) -> StringPtr { return "bar"; },
      [&](Exception&& e) -> StringPtr { EXPECT_EQ(line, e.getLine()); return "baz"; });

  EXPECT_EQ("baz", loop.wait(kj::mv(promise2)));
}

TEST(Async, Then) {
  SimpleEventLoop loop;

  Promise<int> promise = nullptr;

  bool outerDone = false;
  bool innerDone = false;

  loop.wait(loop.evalLater([&]() {
    outerDone = true;
    promise = Promise<int>(123).then([&](int i) {
      EXPECT_EQ(&loop, &EventLoop::current());
      innerDone = true;
      return i + 321;
    });
  }));

  EXPECT_TRUE(outerDone);
  EXPECT_FALSE(innerDone);

  EXPECT_EQ(444, loop.wait(kj::mv(promise)));

  EXPECT_TRUE(innerDone);
}

TEST(Async, Chain) {
  SimpleEventLoop loop;

  Promise<int> promise = loop.evalLater([&]() -> int { return 123; });
  Promise<int> promise2 = loop.evalLater([&]() -> int { return 321; });

  auto promise3 = loop.there(kj::mv(promise),
      [&](int i) {
        EXPECT_EQ(&loop, &EventLoop::current());
        return promise2.then([&loop,i](int j) {
          EXPECT_EQ(&loop, &EventLoop::current());
          return i + j;
        });
      });

  EXPECT_EQ(444, loop.wait(kj::mv(promise3)));
}

TEST(Async, SeparateFulfiller) {
  SimpleEventLoop loop;

  auto pair = newPromiseAndFulfiller<int>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.fulfiller->fulfill(123);
  EXPECT_FALSE(pair.fulfiller->isWaiting());

  EXPECT_EQ(123, loop.wait(kj::mv(pair.promise)));
}

TEST(Async, SeparateFulfillerVoid) {
  SimpleEventLoop loop;

  auto pair = newPromiseAndFulfiller<void>();

  EXPECT_TRUE(pair.fulfiller->isWaiting());
  pair.fulfiller->fulfill();
  EXPECT_FALSE(pair.fulfiller->isWaiting());

  loop.wait(kj::mv(pair.promise));
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

  EXPECT_EQ(123, loop.wait(kj::mv(pair.promise)));
}

#if KJ_NO_EXCEPTIONS
#undef EXPECT_ANY_THROW
#define EXPECT_ANY_THROW(code) EXPECT_DEATH(code, ".")
#endif

TEST(Async, Threads) {
  EXPECT_ANY_THROW(EventLoop::current());

  SimpleEventLoop loop1;
  SimpleEventLoop loop2;

  auto exitThread = newPromiseAndFulfiller<void>();

  Promise<int> promise = loop1.evalLater([]() { return 123; });
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

  Thread thread([&]() {
    EXPECT_ANY_THROW(EventLoop::current());
    loop2.wait(kj::mv(exitThread.promise));
    EXPECT_ANY_THROW(EventLoop::current());
  });

  // Make sure the thread will exit.
  KJ_DEFER(exitThread.fulfiller->fulfill());

  EXPECT_EQ(100544, loop1.wait(kj::mv(promise)));

  EXPECT_ANY_THROW(EventLoop::current());
}

TEST(Async, Ordering) {
  SimpleEventLoop loop1;
  SimpleEventLoop loop2;

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

  auto exitThread = newPromiseAndFulfiller<void>();

  Thread thread([&]() {
    EXPECT_ANY_THROW(EventLoop::current());
    loop2.wait(kj::mv(exitThread.promise));
    EXPECT_ANY_THROW(EventLoop::current());
  });

  // Make sure the thread will exit.
  KJ_DEFER(exitThread.fulfiller->fulfill());

  for (auto i: indices(promises)) {
    loop1.wait(kj::mv(promises[i]));
  }

  EXPECT_EQ(7, counter);
}

TEST(Async, Fork) {
  SimpleEventLoop loop;

  auto outer = loop.evalLater([&]() {
    Promise<int> promise = loop.evalLater([&]() { return 123; });

    auto fork = promise.fork();

    auto branch1 = fork->addBranch().then([](int i) {
      EXPECT_EQ(123, i);
      return 456;
    });
    auto branch2 = fork->addBranch().then([](int i) {
      EXPECT_EQ(123, i);
      return 789;
    });

    {
      auto releaseFork = kj::mv(fork);
    }

    EXPECT_EQ(456, loop.wait(kj::mv(branch1)));
    EXPECT_EQ(789, loop.wait(kj::mv(branch2)));
  });

  loop.wait(kj::mv(outer));
}

struct RefcountedInt: public Refcounted {
  RefcountedInt(int i): i(i) {}
  int i;
  Own<const RefcountedInt> addRef() const { return kj::addRef(*this); }
};

TEST(Async, ForkRef) {
  SimpleEventLoop loop;

  auto outer = loop.evalLater([&]() {
    Promise<Own<RefcountedInt>> promise = loop.evalLater([&]() {
      return refcounted<RefcountedInt>(123);
    });

    auto fork = promise.fork();

    auto branch1 = fork->addBranch().then([](Own<const RefcountedInt>&& i) {
      EXPECT_EQ(123, i->i);
      return 456;
    });
    auto branch2 = fork->addBranch().then([](Own<const RefcountedInt>&& i) {
      EXPECT_EQ(123, i->i);
      return 789;
    });

    {
      auto releaseFork = kj::mv(fork);
    }

    EXPECT_EQ(456, loop.wait(kj::mv(branch1)));
    EXPECT_EQ(789, loop.wait(kj::mv(branch2)));
  });

  loop.wait(kj::mv(outer));
}

}  // namespace
}  // namespace kj
