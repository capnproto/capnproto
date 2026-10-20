// Copyright (c) 2026 Cloudflare, Inc. and contributors
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

#include "promises-unordered.h"

#include <kj/debug.h>
#include <kj/test.h>
#include <kj/vector.h>

namespace kj {
namespace {

KJ_TEST("PromisesUnordered yields promises in completion order") {
  EventLoop loop;
  WaitScope waitScope(loop);
  PromisesUnordered<int> promises;

  auto first = newPromiseAndFulfiller<int>();
  auto second = newPromiseAndFulfiller<int>();
  auto third = newPromiseAndFulfiller<int>();
  promises.add(kj::mv(first.promise));
  promises.add(kj::mv(second.promise));
  promises.add(kj::mv(third.promise));
  KJ_EXPECT(promises.size() == 3);

  third.fulfiller->fulfill(3);
  first.fulfiller->fulfill(1);
  second.fulfiller->fulfill(2);

  KJ_EXPECT(promises.pop().wait(waitScope) == 3);
  KJ_EXPECT(promises.size() == 2);
  KJ_EXPECT(promises.pop().wait(waitScope) == 1);
  KJ_EXPECT(promises.size() == 1);
  KJ_EXPECT(promises.pop().wait(waitScope) == 2);
  KJ_EXPECT(promises.isEmpty());
  KJ_EXPECT(promises.size() == 0);
}

KJ_TEST("PromisesUnordered yields failures without stopping other promises") {
  EventLoop loop;
  WaitScope waitScope(loop);
  PromisesUnordered<int> promises;

  auto failed = newPromiseAndFulfiller<int>();
  promises.add(kj::mv(failed.promise));
  failed.fulfiller->reject(KJ_EXCEPTION(FAILED, "expected failure"));

  KJ_EXPECT_THROW_MESSAGE("expected failure", promises.pop().wait(waitScope));

  promises.add(123);
  KJ_EXPECT(promises.pop().wait(waitScope) == 123);
  KJ_EXPECT(promises.isEmpty());
}

KJ_TEST("PromisesUnordered does not wait for earlier promises") {
  EventLoop loop;
  WaitScope waitScope(loop);
  PromisesUnordered<int> promises;

  promises.add(Promise<int>(NEVER_DONE));
  promises.add(321);

  KJ_EXPECT(promises.pop().wait(waitScope) == 321);
  KJ_EXPECT(!promises.isEmpty());
}

KJ_TEST("PromisesUnordered reports an input destruction failure once") {
  EventLoop loop;
  WaitScope waitScope(loop);
  PromisesUnordered<int> promises;

  promises.add(Promise<int>(123).attach(kj::defer([]() {
    KJ_FAIL_ASSERT("input cleanup failed");
  })));

  KJ_EXPECT_THROW_MESSAGE("input cleanup failed", promises.pop().wait(waitScope));
  KJ_EXPECT(promises.isEmpty());
}

KJ_TEST("PromisesUnordered includes promises added while consuming results") {
  EventLoop loop;
  WaitScope waitScope(loop);
  PromisesUnordered<int> promises;
  Vector<int> results;

  promises.add(1);
  while (!promises.isEmpty()) {
    auto value = promises.pop().wait(waitScope);
    results.add(value);
    if (value < 3) {
      promises.add(value + 1);
    }
  }

  KJ_EXPECT(results.size() == 3);
  KJ_EXPECT(results[0] == 1, results);
  KJ_EXPECT(results[1] == 2, results);
  KJ_EXPECT(results[2] == 3, results);
}

KJ_TEST("canceling a PromisesUnordered waiter does not cancel inputs") {
  EventLoop loop;
  WaitScope waitScope(loop);
  PromisesUnordered<int> promises;
  auto paf = newPromiseAndFulfiller<int>();

  promises.add(kj::mv(paf.promise));
  {
    auto canceledWaiter = promises.pop();
  }

  paf.fulfiller->fulfill(42);
  KJ_EXPECT(promises.pop().wait(waitScope) == 42);
  KJ_EXPECT(promises.isEmpty());
}

KJ_TEST("canceling a PromisesUnordered waiter with a result discards the result") {
  EventLoop loop;
  WaitScope waitScope(loop);
  PromisesUnordered<int> promises;
  bool inputDestroyed = false;

  promises.add(Promise<int>(42).attach(kj::defer([&]() {
    inputDestroyed = true;
  })));
  while (!inputDestroyed) {
    loop.run();
  }

  {
    auto canceledWaiter = promises.pop();
  }
  KJ_EXPECT(promises.isEmpty());
}

KJ_TEST("PromisesUnordered permits only one outstanding waiter") {
  EventLoop loop;
  WaitScope waitScope(loop);
  PromisesUnordered<int> promises;
  auto paf = newPromiseAndFulfiller<int>();

  promises.add(kj::mv(paf.promise));
  auto firstWaiter = promises.pop();
  KJ_EXPECT_THROW_MESSAGE(
      "a promise returned by pop() already exists",
      promises.pop().wait(waitScope));

  firstWaiter = nullptr;
  paf.fulfiller->fulfill(42);
  KJ_EXPECT(promises.pop().wait(waitScope) == 42);
}

KJ_TEST("PromisesUnordered permits another pop after delivering a result") {
  EventLoop loop;
  WaitScope waitScope(loop);
  PromisesUnordered<int> promises;

  promises.add(1);
  promises.add(2);
  auto first = promises.pop();
  KJ_EXPECT(first.poll(waitScope));

  KJ_EXPECT(promises.pop().wait(waitScope) == 2);
  KJ_EXPECT(first.wait(waitScope) == 1);
}

KJ_TEST("a yielded result can outlive PromisesUnordered") {
  EventLoop loop;
  WaitScope waitScope(loop);
  Promise<int> result = nullptr;

  {
    PromisesUnordered<int> promises;
    bool inputDestroyed = false;
    promises.add(Promise<int>(42).attach(kj::defer([&]() {
      inputDestroyed = true;
    })));
    while (!inputDestroyed) {
      loop.run();
    }
    result = promises.pop();
  }

  KJ_EXPECT(result.wait(waitScope) == 42);
}

KJ_TEST("destroying PromisesUnordered rejects an outstanding waiter") {
  EventLoop loop;
  WaitScope waitScope(loop);
  Promise<int> result = nullptr;

  {
    PromisesUnordered<int> promises;
    promises.add(Promise<int>(NEVER_DONE));
    result = promises.pop();
  }

  KJ_EXPECT_THROW_MESSAGE("PromisesUnordered was destroyed", result.wait(waitScope));
}

KJ_TEST("PromisesUnordered supports void promises") {
  EventLoop loop;
  WaitScope waitScope(loop);
  PromisesUnordered<void> promises;

  promises.add(READY_NOW);
  promises.pop().wait(waitScope);
  KJ_EXPECT(promises.isEmpty());
}

KJ_TEST("destroying PromisesUnordered cancels unfinished promises") {
  EventLoop loop;
  WaitScope waitScope(loop);
  bool canceled = false;

  {
    PromisesUnordered<void> promises;
    promises.add(Promise<void>(NEVER_DONE).attach(kj::defer([&]() {
      canceled = true;
    })));
    KJ_EXPECT(!promises.isEmpty());
    KJ_EXPECT(promises.trace().size() > 0);
  }

  KJ_EXPECT(canceled);
}

}  // namespace
}  // namespace kj
