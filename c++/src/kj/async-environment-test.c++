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
  TestEnvironment(bool& finalDestructorRan, StringPtr stuff)
      : finalDestructorRan(finalDestructorRan), stuff(kj::heapString(stuff)) {}
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

void expectEnvironment(kj::StringPtr expectedStuff = "foobar"_kj) {
  // We don't crash or anything if we try to get a nonexistent environment.
  struct NotAnEnvironment {};
  KJ_EXPECT(tryGetEnvironment<NotAnEnvironment>() == nullptr);

  auto maybeEnvironment = tryGetEnvironment<TestEnvironment>();
  KJ_EXPECT(maybeEnvironment != nullptr);
  KJ_IF_MAYBE(environment, maybeEnvironment) {
    auto& env = getEnvironment<TestEnvironment>();
    KJ_ASSERT(&env == environment);
    KJ_EXPECT(env.stuff == expectedStuff);
  }
}

struct DeferredExpectEnvironment {
  // Helper to ensure lambdas and attachments are destroyed with environments.

  DeferredExpectEnvironment(kj::StringPtr stuff = "foobar"_kj): expectedStuff(stuff) {};
  ~DeferredExpectEnvironment() noexcept(false) {
    expectEnvironment(expectedStuff);
  }
  KJ_DISALLOW_COPY(DeferredExpectEnvironment);
  kj::StringPtr expectedStuff;
};

Own<DeferredExpectEnvironment> dee() {
  return heap<DeferredExpectEnvironment>();
}

template <typename Func>
void syncTestCase(Func&& func) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool finalDestructorRan = false;

  KJ_EXPECT(tryGetEnvironment<TestEnvironment>() == nullptr);

  auto promise = runInEnvironment(
      TestEnvironment(finalDestructorRan, "foobar"_kj),
      fwd<Func>(func));

  // Purely synchronous functions don't capture the Environment.
  KJ_EXPECT(finalDestructorRan);
  KJ_EXPECT(tryGetEnvironment<TestEnvironment>() == nullptr);
  KJ_EXPECT(promise.poll(waitScope));
  promise.wait(waitScope);
}

template <typename Func>
void asyncTestCase(Func&& func) {
  EventLoop loop;
  WaitScope waitScope(loop);

  bool finalDestructorRan = false;

  KJ_EXPECT(tryGetEnvironment<TestEnvironment>() == nullptr);

  {
    auto promise = runInEnvironment(
        TestEnvironment(finalDestructorRan, "foobar"_kj),
        fwd<Func>(func));

    // Asynchronous functions capture the Environment. Eager promises will drop the Environment
    // after .poll(), and non-eager ones after .wait().
    KJ_EXPECT(!finalDestructorRan);
    KJ_EXPECT(tryGetEnvironment<TestEnvironment>() == nullptr);
    if (promise.poll(waitScope)) {
      KJ_EXPECT(tryGetEnvironment<TestEnvironment>() == nullptr);
      promise.wait(waitScope);

      // The Environment is destroyed after the promise is done.
      KJ_EXPECT(finalDestructorRan);
    } else {
      // Test case suspended forever. Just verify that our environment's destructor runs.
      KJ_EXPECT(!finalDestructorRan);
      promise = nullptr;
      KJ_EXPECT(finalDestructorRan);
    }
  }

  KJ_EXPECT(tryGetEnvironment<TestEnvironment>() == nullptr);
}

KJ_TEST("Environments are available to synchronous callbacks") {
  syncTestCase([]() -> kj::Promise<void> {
    expectEnvironment();
    return READY_NOW;
  });
}

KJ_TEST("Environments are available to evalLater() callbacks") {
  asyncTestCase([]() -> kj::Promise<void> {
    return evalLater([d=dee()]() {
      expectEnvironment();
    });
  });
}

KJ_TEST("Environments are available to evalLast() callbacks") {
  asyncTestCase([]() -> kj::Promise<void> {
    return evalLast([d=dee()]() {
      expectEnvironment();
    });
  });
}

KJ_TEST("Environments are available in ForkedPromise branch callbacks") {
  asyncTestCase([]() -> Promise<void> {
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

KJ_TEST("Environments are available in .then() callbacks") {
  asyncTestCase([]() -> Promise<void> {
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
}

KJ_TEST("Environments are available in .then() errorbacks") {
  asyncTestCase([]() -> Promise<void> {
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

KJ_TEST("Environments are overridable and overlapping") {
  bool overrideEnvironmentDestructorRan = false;
  asyncTestCase([&]() -> Promise<void> {
    auto paf = newPromiseAndFulfiller<void>();

    auto promise = paf.promise.then([&,d=dee()]() mutable {
      expectEnvironment();
      return runInEnvironment(
          TestEnvironment(overrideEnvironmentDestructorRan, "bazqux"_kj),
          []() -> Promise<void> {
        expectEnvironment("bazqux"_kj);
        return evalLater([]() {
          expectEnvironment("bazqux"_kj);
          return runInEnvironment(double(1.23), []() {
            // We have the previous environment.
            expectEnvironment("bazqux"_kj);
            // And an overlapping environment.
            KJ_EXPECT(tryGetEnvironment<double>() != nullptr);
            KJ_EXPECT(getEnvironment<double>() == 1.23);
            // We can override the double again and the TestEnvironment stays.
            return runInEnvironment(4.56, []() {
              return evalLater([]() {
                expectEnvironment("bazqux"_kj);
                KJ_EXPECT(tryGetEnvironment<double>() != nullptr);
                KJ_EXPECT(getEnvironment<double>() == 4.56);
                return Promise<void>(READY_NOW);
              });
            });
          });
        });
      });
    }, [d=dee()](kj::Exception&& exception) {
      expectEnvironment();
      KJ_FAIL_EXPECT("should not see exception", exception);
    }).then([]() {
      expectEnvironment();
    });

    paf.fulfiller->fulfill();

    return kj::mv(promise);
  });
}

KJ_TEST("Environments are available in .catch_() errorbacks") {
  asyncTestCase([]() -> Promise<void> {
    auto paf = newPromiseAndFulfiller<void>();

    auto promise = paf.promise.catch_([d=dee()](kj::Exception&& exception) {
      expectEnvironment();
    });

    paf.fulfiller->reject(KJ_EXCEPTION(FAILED, "nope"));

    return kj::mv(promise);
  });
}

KJ_TEST("Environments are available in .eagerlyEvaluate() errorbacks") {
  asyncTestCase([]() -> Promise<void> {
    auto paf = newPromiseAndFulfiller<void>();

    auto promise = paf.promise.eagerlyEvaluate([d=dee()](kj::Exception&& exception) {
      expectEnvironment();
    });

    paf.fulfiller->reject(KJ_EXCEPTION(FAILED, "nope"));

    return kj::mv(promise);
  });
}

KJ_TEST("Environments are available in .ignoreResult() destructors") {
  asyncTestCase([]() -> Promise<void> {
    auto paf = newPromiseAndFulfiller<Own<DeferredExpectEnvironment>>();

    auto promise = paf.promise.ignoreResult();

    paf.fulfiller->fulfill(heap<DeferredExpectEnvironment>());

    return kj::mv(promise);
  });
}

KJ_TEST("Environments are available in .attach() destructors") {
  asyncTestCase([]() -> Promise<void> {
    return Promise<void>(READY_NOW).attach(heap<DeferredExpectEnvironment>());
  });
}

KJ_TEST("Environments are available in fibers") {
  asyncTestCase([]() -> Promise<void> {
    return startFiber(64 * 1024, [d=dee()](WaitScope& ws2) {
      expectEnvironment();
      _::yield().wait(ws2);
      expectEnvironment();
    });
  });
}

KJ_TEST("Environments are available in suspended fiber destructors") {
  asyncTestCase([]() -> Promise<void> {
    return startFiber(64 * 1024, [d=dee()](WaitScope& ws2) {
      // Explode if we're destroyed outside an Environment.
      DeferredExpectEnvironment dee;

      expectEnvironment();

      // Make sure we don't run to completion, which is the fiber happy path.
      Promise<void>(NEVER_DONE).wait(ws2);
      KJ_UNREACHABLE;
    });
  });
}

#if KJ_HAS_COROUTINE

KJ_TEST("Environments are available in coroutines") {
  asyncTestCase([]() -> Promise<void> {
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
  asyncTestCase([]() -> Promise<void> {
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
