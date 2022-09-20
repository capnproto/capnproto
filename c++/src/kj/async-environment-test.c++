// Copyright (c) 2022 Cloudflare, Inc. and contributors
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
#include "common.h"
#include "test.h"

namespace kj {
namespace {

struct TestEnvironment {
  TestEnvironment(bool& finalDestructorRan, String stuff)
      : finalDestructorRan(finalDestructorRan), stuff(kj::mv(stuff)) {}
  ~TestEnvironment() noexcept(false) {
    // If `stuff` is empty, assume we are moved-from.
    if (stuff.size() != 0) {
      KJ_ASSERT(!finalDestructorRan);
      finalDestructorRan = true;
    }
  }
  KJ_DISALLOW_COPY(TestEnvironment);
  TestEnvironment(TestEnvironment&&) = default;

  bool& finalDestructorRan;
  // Just for testing.

  String stuff;
  // The simulated user state.
};

void expectEnvironment() {
  auto maybeEnvironment = tryGetEnvironment<TestEnvironment>();
  KJ_EXPECT(maybeEnvironment != nullptr);
  if (maybeEnvironment != nullptr) {
    // We want to test the `getEnvironment()` function, too, so no need for KJ_IF_MAYBE.
    auto& env = getEnvironment<TestEnvironment>();
    KJ_EXPECT(env.stuff == "foobar"_kj);
  }
}

struct DeferredExpectEnvironment {
  // Helper to ensure lambdas and attachments are destroyed with environments.

  DeferredExpectEnvironment() = default;
  ~DeferredExpectEnvironment() noexcept(false) {
    expectEnvironment();
  }
  KJ_DISALLOW_COPY(DeferredExpectEnvironment);
};

Own<DeferredExpectEnvironment> dee() {
  return heap<DeferredExpectEnvironment>();
}

template <typename Func>
void testCase(Func&& func) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool finalDestructorRan = false;

  KJ_EXPECT(tryGetEnvironment<TestEnvironment>() == nullptr);

  {
    auto promise = runInEnvironment(
        TestEnvironment(finalDestructorRan, heapString("foobar"_kj)),
        FunctionParam<Promise<void>()>(fwd<Func>(func)));

    KJ_EXPECT(tryGetEnvironment<TestEnvironment>() == nullptr);
    if (promise.poll(waitScope)) {
      KJ_EXPECT(tryGetEnvironment<TestEnvironment>() == nullptr);
      KJ_EXPECT(!finalDestructorRan);
      promise.wait(waitScope);

      // The Environment is destroyed after the promise is done.
      KJ_EXPECT(finalDestructorRan);
      KJ_EXPECT(tryGetEnvironment<TestEnvironment>() == nullptr);
    } else {
      // Test case suspended forever. Just verify that our environment's destructor runs.
      KJ_EXPECT(!finalDestructorRan);
      promise = nullptr;
      KJ_EXPECT(finalDestructorRan);
    }
  }
}

KJ_TEST("Environments are available to synchronous callbacks") {
  testCase([]() -> kj::Promise<void> {
    expectEnvironment();
    return READY_NOW;
  });
}

KJ_TEST("Environments are available to evalLater() callbacks") {
  testCase([]() -> kj::Promise<void> {
    return evalLater([d=dee()]() {
      expectEnvironment();
    });
  });
}

KJ_TEST("Environments are available to evalLast() callbacks") {
  testCase([]() -> kj::Promise<void> {
    return evalLast([d=dee()]() {
      expectEnvironment();
    });
  });
}

KJ_TEST("Environments are available in fibers") {
  testCase([]() -> Promise<void> {
    return startFiber(64 * 1024, [d=dee()](WaitScope& ws2) {
      expectEnvironment();
      _::yield().wait(ws2);
      expectEnvironment();
    });
  });
}

KJ_TEST("Environments are available in ForkedPromise branch callbacks") {
  testCase([]() -> Promise<void> {
    auto paf = newPromiseAndFulfiller<void>();

    auto forked = paf.promise.fork();
    auto left = forked.addBranch().then([d=dee()]() mutable {
      expectEnvironment();
    });
    auto right = forked.addBranch().then([d=dee(),
                                          left = kj::mv(left)]() mutable {
      expectEnvironment();
      return kj::mv(left);
    });

    paf.fulfiller->fulfill();

    return kj::mv(right);
  });
}

KJ_TEST("Environments are available in .then() callbacks and errorbacks") {
  testCase([]() -> Promise<void> {
    auto paf = newPromiseAndFulfiller<void>();

    auto promise = paf.promise.then([d=dee()]() mutable {
      expectEnvironment();
    }, [d=dee()](kj::Exception&& exception) {
      expectEnvironment();
      KJ_FAIL_EXPECT("should not see exception", exception);
    });

    paf.fulfiller->fulfill();

    return kj::mv(promise);
  });

  testCase([]() -> Promise<void> {
    auto paf = newPromiseAndFulfiller<void>();

    auto promise = paf.promise.then([d=dee()]() mutable {
      expectEnvironment();
      KJ_FAIL_EXPECT("should not see success");
    }, [d=dee()](kj::Exception&& exception) {
      expectEnvironment();
    });

    paf.fulfiller->reject(KJ_EXCEPTION(FAILED, "nope"));

    return kj::mv(promise);
  });
}

KJ_TEST("Environments are available in .catch_() and eagerlyEvaluate() errorbacks") {
  testCase([]() -> Promise<void> {
    auto paf = newPromiseAndFulfiller<void>();

    auto promise = paf.promise.catch_([d=dee()](kj::Exception&& exception) {
      expectEnvironment();
    });

    paf.fulfiller->reject(KJ_EXCEPTION(FAILED, "nope"));

    return kj::mv(promise);
  });

  testCase([]() -> Promise<void> {
    auto paf = newPromiseAndFulfiller<void>();

    auto promise = paf.promise.eagerlyEvaluate([d=dee()](kj::Exception&& exception) {
      expectEnvironment();
    });

    paf.fulfiller->reject(KJ_EXCEPTION(FAILED, "nope"));

    return kj::mv(promise);
  });
}

KJ_TEST("Environments are available in .ignoreResult() destructors") {
  testCase([]() -> Promise<void> {
    auto paf = newPromiseAndFulfiller<Own<DeferredExpectEnvironment>>();

    auto promise = paf.promise.ignoreResult();

    paf.fulfiller->fulfill(heap<DeferredExpectEnvironment>());

    return kj::mv(promise);
  });
}

KJ_TEST("Environments are available in .attach() destructors") {
  testCase([]() -> Promise<void> {
    return Promise<void>(READY_NOW).attach(heap<DeferredExpectEnvironment>());
  });
}

#if KJ_HAS_COROUTINE

KJ_TEST("Environments are available in coroutines") {
  testCase([]() -> Promise<void> {
    expectEnvironment();
    co_await Promise<void>(READY_NOW);
    expectEnvironment();
    co_await evalLater([]() {
      expectEnvironment();
    });
    expectEnvironment();
  });
}

KJ_TEST("Environments are available in suspended coroutine destructors") {
  testCase([]() -> Promise<void> {
    // Explode if we're destroyed outside an Environment.
    DeferredExpectEnvironment dee;

    expectEnvironment();

    // Make sure we don't run to completion, which is the coroutine happy path.
    co_await Promise<void>(NEVER_DONE);
    KJ_UNREACHABLE;
  });
}

#endif  // KJ_HAS_COROUTINE

}  // namespace
}  // namespace kj
