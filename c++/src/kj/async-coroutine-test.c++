// Copyright (c) 2020 Cloudflare, Inc. and contributors
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

#include <kj/async.h>
#include <kj/array.h>
#include <kj/compat/http.h>
#include <kj/debug.h>
#include <kj/test.h>

namespace kj {
namespace {

#ifdef KJ_HAS_COROUTINE

template <typename T>
Promise<kj::Decay<T>> identity(T&& value) {
  co_return kj::fwd<T>(value);
}
// Work around a bonkers MSVC ICE with a separate overload.
Promise<const char*> identity(const char* value) {
  co_return value;
}

KJ_TEST("Identity coroutine") {
  EventLoop loop;
  WaitScope waitScope(loop);

  KJ_EXPECT(identity(123).wait(waitScope) == 123);
  KJ_EXPECT(*identity(kj::heap(456)).wait(waitScope) == 456);

  {
    auto p = identity("we can cancel the coroutine");
  }
}

template <typename T>
Promise<T> simpleCoroutine(kj::Promise<T> result, kj::Promise<bool> dontThrow = true) {
  KJ_ASSERT(co_await dontThrow);
  co_return co_await result;
}

KJ_TEST("Simple coroutine test") {
  EventLoop loop;
  WaitScope waitScope(loop);

  simpleCoroutine(kj::Promise<void>(kj::READY_NOW)).wait(waitScope);

  KJ_EXPECT(simpleCoroutine(kj::Promise<int>(123)).wait(waitScope) == 123);
}

struct Counter {
  size_t& wind;
  size_t& unwind;
  Counter(size_t& wind, size_t& unwind): wind(wind), unwind(unwind) { ++wind; }
  ~Counter() { ++unwind; }
  KJ_DISALLOW_COPY_AND_MOVE(Counter);
};

kj::Promise<void> countAroundAwait(size_t& wind, size_t& unwind, kj::Promise<void> promise) {
  Counter counter1(wind, unwind);
  co_await promise;
  Counter counter2(wind, unwind);
  co_return;
};

KJ_TEST("co_awaiting initial immediate promises suspends even if event loop is empty and running") {
  // The coroutine PromiseNode implementation contains an optimization which allows us to avoid
  // suspending the coroutine and instead immediately call PromiseNode::get() and proceed with
  // execution, but only if the coroutine has suspended at least once. This test verifies that the
  // optimization is disabled for this initial suspension.

  EventLoop loop;
  WaitScope waitScope(loop);

  // The immediate-execution optimization is only enabled when the event loop is running, so use an
  // eagerly-evaluated evalLater() to perform the test from within the event loop. (If we didn't
  // eagerly-evaluate the promise, the result would be extracted after the loop finished, which
  // would disable the optimization anyway.)
  kj::evalLater([&]() {
    size_t wind = 0, unwind = 0;

    auto promise = kj::Promise<void>(kj::READY_NOW);
    auto coroPromise = countAroundAwait(wind, unwind, kj::READY_NOW);

    // `coro` has not completed.
    KJ_EXPECT(wind == 1);
    KJ_EXPECT(unwind == 0);
  }).eagerlyEvaluate(nullptr).wait(waitScope);

  kj::evalLater([&]() {
    // If there are no background tasks in the queue, coroutines execute through an evalLater()
    // without suspending.

    size_t wind = 0, unwind = 0;
    bool evalLaterRan = false;

    auto promise = kj::evalLater([&]() { evalLaterRan = true; });
    auto coroPromise = countAroundAwait(wind, unwind, kj::mv(promise));

    KJ_EXPECT(evalLaterRan == false);
    KJ_EXPECT(wind == 1);
    KJ_EXPECT(unwind == 0);
  }).eagerlyEvaluate(nullptr).wait(waitScope);
}

KJ_TEST("co_awaiting an immediate promise suspends if the event loop is not running") {
  // We only want to enable the immediate-execution optimization if the event loop is running, or
  // else a whole bunch of RPC tests break, because some .then()s get evaluated on promise
  // construction, before any .wait() call.

  EventLoop loop;
  WaitScope waitScope(loop);

  size_t wind = 0, unwind = 0;

  auto promise = kj::Promise<void>(kj::READY_NOW);
  auto coroPromise = countAroundAwait(wind, unwind, kj::READY_NOW);

  // In the previous test, this exact same code executed immediately because the event loop was
  // running.
  KJ_EXPECT(wind == 1);
  KJ_EXPECT(unwind == 0);
}

KJ_TEST("co_awaiting immediate promises suspends if the event loop is not empty") {
  // We want to make sure that we can still return to the event loop when we need to.

  EventLoop loop;
  WaitScope waitScope(loop);

  // The immediate-execution optimization is only enabled when the event loop is running, so use an
  // eagerly-evaluated evalLater() to perform the test from within the event loop. (If we didn't
  // eagerly-evaluate the promise, the result would be extracted after the loop finished.)
  kj::evalLater([&]() {
    size_t wind = 0, unwind = 0;

    // We need to enqueue an Event on the event loop to inhibit the immediate-execution
    // optimization. Creating and then immediately fulfilling an EagerPromiseNode is a convenient
    // way to do so.
    auto paf = newPromiseAndFulfiller<void>();
    paf.promise = paf.promise.eagerlyEvaluate(nullptr);
    paf.fulfiller->fulfill();

    auto promise = kj::Promise<void>(kj::READY_NOW);
    auto coroPromise = countAroundAwait(wind, unwind, kj::READY_NOW);

    // We didn't immediately extract the READY_NOW.
    KJ_EXPECT(wind == 1);
    KJ_EXPECT(unwind == 0);
  }).eagerlyEvaluate(nullptr).wait(waitScope);

  kj::evalLater([&]() {
    size_t wind = 0, unwind = 0;
    bool evalLaterRan = false;

    // We need to enqueue an Event on the event loop to inhibit the immediate-execution
    // optimization. Creating and then immediately fulfilling an EagerPromiseNode is a convenient
    // way to do so.
    auto paf = newPromiseAndFulfiller<void>();
    paf.promise = paf.promise.eagerlyEvaluate(nullptr);
    paf.fulfiller->fulfill();

    auto promise = kj::evalLater([&]() { evalLaterRan = true; });
    auto coroPromise = countAroundAwait(wind, unwind, kj::mv(promise));

    // We didn't continue through the evalLater() promise, because the background promise's
    // continuation was next in the event loop's queue.
    KJ_EXPECT(evalLaterRan == false);
    // No Counter destructor has run.
    KJ_EXPECT(wind == 1);
    KJ_EXPECT(unwind == 0);
  }).eagerlyEvaluate(nullptr).wait(waitScope);
}

KJ_TEST("Exceptions propagate through layered coroutines") {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto throwy = simpleCoroutine(kj::Promise<int>(kj::NEVER_DONE), false);

  KJ_EXPECT_THROW_RECOVERABLE(FAILED, simpleCoroutine(kj::mv(throwy)).wait(waitScope));
}

KJ_TEST("Exceptions before the first co_await don't escape, but reject the promise") {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto throwEarly = []() -> Promise<void> {
    KJ_FAIL_ASSERT("test exception");
#ifdef __GNUC__
// Yes, this `co_return` is unreachable. But without it, this function is no longer a coroutine.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunreachable-code"
#endif  // __GNUC__
    co_return;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif  // __GNUC__
  };

  auto throwy = throwEarly();

  KJ_EXPECT_THROW_RECOVERABLE(FAILED, throwy.wait(waitScope));
}

KJ_TEST("Coroutines can catch exceptions from co_await") {
  EventLoop loop;
  WaitScope waitScope(loop);

  kj::String description;

  auto tryCatch = [&](kj::Promise<void> promise) -> kj::Promise<kj::String> {
    try {
      co_await promise;
    } catch (const kj::Exception& exception) {
      co_return kj::str(exception.getDescription());
    }
    KJ_FAIL_EXPECT("should have thrown");
    KJ_UNREACHABLE;
  };

  {
    // Immediately ready case.
    auto promise = kj::Promise<void>(KJ_EXCEPTION(FAILED, "catch me"));
    KJ_EXPECT(tryCatch(kj::mv(promise)).wait(waitScope) == "catch me");
  }

  {
    // Ready later case.
    auto promise = kj::evalLater([]() -> kj::Promise<void> {
      return KJ_EXCEPTION(FAILED, "catch me");
    });
    KJ_EXPECT(tryCatch(kj::mv(promise)).wait(waitScope) == "catch me");
  }
}

KJ_TEST("Coroutines can be canceled while suspended") {
  EventLoop loop;
  WaitScope waitScope(loop);

  size_t wind = 0, unwind = 0;

  auto coro = [&](kj::Promise<int> promise) -> kj::Promise<void> {
    Counter counter1(wind, unwind);
    co_await kj::evalLater([](){});
    Counter counter2(wind, unwind);
    co_await promise;
  };

  {
    auto neverDone = kj::Promise<int>(kj::NEVER_DONE);
    neverDone = neverDone.attach(kj::heap<Counter>(wind, unwind));
    auto promise = coro(kj::mv(neverDone));
    KJ_EXPECT(!promise.poll(waitScope));
  }

  // Stack variables on both sides of a co_await, plus coroutine arguments are destroyed.
  KJ_EXPECT(wind == 3);
  KJ_EXPECT(unwind == 3);
}

kj::Promise<void> deferredThrowCoroutine(kj::Promise<void> awaitMe) {
  KJ_DEFER(kj::throwFatalException(KJ_EXCEPTION(FAILED, "thrown during unwind")));
  co_await awaitMe;
  co_return;
};

KJ_TEST("Exceptions during suspended coroutine frame-unwind propagate via destructor") {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
    deferredThrowCoroutine(kj::NEVER_DONE);
  }));

  KJ_EXPECT(exception.getDescription() == "thrown during unwind");
};

KJ_TEST("Exceptions during suspended coroutine frame-unwind do not cause a memory leak") {
  EventLoop loop;
  WaitScope waitScope(loop);

  // We can't easily test for memory leaks without hooking operator new and delete. However, we can
  // arrange for the test to crash on failure, by having the coroutine suspend at a promise that we
  // later fulfill, thus arming the Coroutine's Event. If we fail to destroy the coroutine in this
  // state, EventLoop will throw on destruction because it can still see the Event in its list.

  auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
    auto paf = kj::newPromiseAndFulfiller<void>();

    auto coroPromise = deferredThrowCoroutine(kj::mv(paf.promise));

    // Arm the Coroutine's Event.
    paf.fulfiller->fulfill();

    // If destroying `coroPromise` does not run ~Event(), then ~EventLoop() will crash later.
  }));

  KJ_EXPECT(exception.getDescription() == "thrown during unwind");
};

KJ_TEST("Exceptions during completed coroutine frame-unwind propagate via returned Promise") {
  EventLoop loop;
  WaitScope waitScope(loop);

  {
    // First, prove that exceptions don't escape the destructor of a completed coroutine.
    auto promise = deferredThrowCoroutine(kj::READY_NOW);
    KJ_EXPECT(promise.poll(waitScope));
  }

  {
    // Next, prove that they show up via the returned Promise.
    auto promise = deferredThrowCoroutine(kj::READY_NOW);
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("thrown during unwind", promise.wait(waitScope));
  }
}

KJ_TEST("Coroutine destruction exceptions are ignored if there is another exception in flight") {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
    auto promise = deferredThrowCoroutine(kj::NEVER_DONE);
    kj::throwFatalException(KJ_EXCEPTION(FAILED, "thrown before destroying throwy promise"));
  }));

  KJ_EXPECT(exception.getDescription() == "thrown before destroying throwy promise");
}

KJ_TEST("co_await only sees coroutine destruction exceptions if promise was not rejected") {
  EventLoop loop;
  WaitScope waitScope(loop);

  // throwyDtorPromise is an immediate void promise that will throw when it's destroyed, which
  // we expect to be able to catch from a coroutine which co_awaits it.
  auto throwyDtorPromise = kj::Promise<void>(kj::READY_NOW)
      .attach(kj::defer([]() {
    kj::throwFatalException(KJ_EXCEPTION(FAILED, "thrown during unwind"));
  }));

  // rejectedThrowyDtorPromise is a rejected promise. When co_awaited in a coroutine,
  // Awaiter::await_resume() will throw that exception for us to catch, but before we can catch it,
  // the temporary promise will be destroyed. The exception it throws during unwind will be ignored,
  // and the caller of the coroutine will see only the "thrown during execution" exception.
  auto rejectedThrowyDtorPromise = kj::evalNow([&]() -> kj::Promise<void> {
    kj::throwFatalException(KJ_EXCEPTION(FAILED, "thrown during execution"));
  }).attach(kj::defer([]() {
    kj::throwFatalException(KJ_EXCEPTION(FAILED, "thrown during unwind"));
  }));

  auto awaitPromise = [](kj::Promise<void> promise) -> kj::Promise<void> {
    co_await promise;
  };

  KJ_EXPECT_THROW_MESSAGE("thrown during unwind",
      awaitPromise(kj::mv(throwyDtorPromise)).wait(waitScope));

  KJ_EXPECT_THROW_MESSAGE("thrown during execution",
      awaitPromise(kj::mv(rejectedThrowyDtorPromise)).wait(waitScope));
}

#if !_MSC_VER  && !__aarch64__
uint countLines(StringPtr s) {
  uint lines = 0;
  for (char c: s) {
    lines += c == '\n';
  }
  return lines;
}

// TODO(msvc): This test relies on GetFunctorStartAddress, which is not supported on MSVC currently,
//   so skip the test.
// TODO(someday): Test is flakey on arm64, depending on how it's compiled. I haven't had a chance to
//   investigate much, but noticed that it failed in a debug build, but passed in a local opt build.
KJ_TEST("Can trace through coroutines") {
  // This verifies that async traces, generated either from promises or from events, can see through
  // coroutines.
  //
  // This test may be a bit brittle because it depends on specific trace counts.

  // Enable stack traces, even in release mode.
  class EnableFullStackTrace: public ExceptionCallback {
  public:
    StackTraceMode stackTraceMode() override { return StackTraceMode::FULL; }
  };
  EnableFullStackTrace exceptionCallback;

  EventLoop loop;
  WaitScope waitScope(loop);

  auto paf = newPromiseAndFulfiller<void>();

  // Get an async trace when the promise is fulfilled. We eagerlyEvaluate() to make sure the
  // continuation executes while the event loop is running.
  paf.promise = paf.promise.then([]() {
    auto trace = getAsyncTrace();
    // We expect one entry for waitImpl(), one for the coroutine, and one for this continuation.
    // When building in debug mode with CMake, I observed this count can be 2. The missing frame is
    // probably this continuation. Let's just expect a range.
    auto count = countLines(trace);
    KJ_EXPECT(0 < count && count <= 3);
  }).eagerlyEvaluate(nullptr);

  auto coroPromise = [&]() -> kj::Promise<void> {
    co_await paf.promise;
  }();

  {
    auto trace = coroPromise.trace();
    // One for the Coroutine PromiseNode, one for paf.promise.
    KJ_EXPECT(countLines(trace) >= 2);
  }

  paf.fulfiller->fulfill();

  coroPromise.wait(waitScope);
}
#endif  // !_MSC_VER || defined(__clang__)

Promise<void> sendData(Promise<Own<NetworkAddress>> addressPromise) {
  auto address = co_await addressPromise;
  auto client = co_await address->connect();
  co_await client->write("foo", 3);
}

Promise<String> receiveDataCoroutine(Own<ConnectionReceiver> listener) {
  auto server = co_await listener->accept();
  char buffer[4];
  auto n = co_await server->read(buffer, 3, 4);
  KJ_EXPECT(3u == n);
  co_return heapString(buffer, n);
}

KJ_TEST("Simple network test with coroutine") {
  auto io = setupAsyncIo();
  auto& network = io.provider->getNetwork();

  Own<NetworkAddress> serverAddress = network.parseAddress("*", 0).wait(io.waitScope);
  Own<ConnectionReceiver> listener = serverAddress->listen();

  sendData(network.parseAddress("localhost", listener->getPort()))
      .detach([](Exception&& exception) {
    KJ_FAIL_EXPECT(exception);
  });

  String result = receiveDataCoroutine(kj::mv(listener)).wait(io.waitScope);

  KJ_EXPECT("foo" == result);
}

Promise<Own<AsyncIoStream>> httpClientConnect(AsyncIoContext& io) {
  auto addr = co_await io.provider->getNetwork().parseAddress("capnproto.org", 80);
  co_return co_await addr->connect();
}

Promise<void> httpClient(Own<AsyncIoStream> connection) {
  // Borrowed and rewritten from compat/http-test.c++.

  HttpHeaderTable table;
  auto client = newHttpClient(table, *connection);

  HttpHeaders headers(table);
  headers.set(HttpHeaderId::HOST, "capnproto.org");

  auto response = co_await client->request(HttpMethod::GET, "/", headers).response;
  KJ_EXPECT(response.statusCode / 100 == 3);
  auto location = KJ_ASSERT_NONNULL(response.headers->get(HttpHeaderId::LOCATION));
  KJ_EXPECT(location == "https://capnproto.org/");

  auto body = co_await response.body->readAllText();
}

KJ_TEST("HttpClient to capnproto.org with a coroutine") {
  auto io = setupAsyncIo();

  auto promise = httpClientConnect(io).then([](Own<AsyncIoStream> connection) {
    return httpClient(kj::mv(connection));
  }, [](Exception&&) {
    KJ_LOG(WARNING, "skipping test because couldn't connect to capnproto.org");
  });

  promise.wait(io.waitScope);
}

// =======================================================================================
// coCapture() tests

KJ_TEST("Verify coCapture() functors can only be run once") {
  auto io = kj::setupAsyncIo();

  auto functor = coCapture([](kj::Timer& timer) -> kj::Promise<void> {
    co_await timer.afterDelay(1 * kj::MILLISECONDS);
  });

  auto promise = functor(io.lowLevelProvider->getTimer());
  KJ_EXPECT_THROW(FAILED, functor(io.lowLevelProvider->getTimer()));

  promise.wait(io.waitScope);
}

auto makeDelayedIntegerFunctor(size_t i) {
  return [i](kj::Timer& timer) -> kj::Promise<size_t> {
    co_await timer.afterDelay(1 * kj::MILLISECONDS);
    co_return i;
  };
}

KJ_TEST("Verify coCapture() with local scoped functors") {
  auto io = kj::setupAsyncIo();

  constexpr size_t COUNT = 100;
  kj::Vector<kj::Promise<size_t>> promises;
  for (size_t i = 0; i < COUNT; ++i) {
    auto functor = coCapture(makeDelayedIntegerFunctor(i));
    promises.add(functor(io.lowLevelProvider->getTimer()));
  }

  for (size_t i = COUNT; i > 0 ; --i) {
    auto j = i-1;
    auto result = promises[j].wait(io.waitScope);
    KJ_REQUIRE(result == j);
  }
}

auto makeCheckThenDelayedIntegerFunctor(kj::Timer& timer, size_t i) {
  return [&timer, i](size_t val) -> kj::Promise<size_t> {
    KJ_REQUIRE(val == i);
    co_await timer.afterDelay(1 * kj::MILLISECONDS);
    co_return i;
  };
}

KJ_TEST("Verify coCapture() with continuation functors") {
  // This test usually works locally without `coCapture()()`. It does however, fail in
  // ASAN.
  auto io = kj::setupAsyncIo();

  constexpr size_t COUNT = 100;
  kj::Vector<kj::Promise<size_t>> promises;
  for (size_t i = 0; i < COUNT; ++i) {
    auto promise = io.lowLevelProvider->getTimer().afterDelay(1 * kj::MILLISECONDS).then([i]() {
      return i;
    });
    promise = promise.then(coCapture(
        makeCheckThenDelayedIntegerFunctor(io.lowLevelProvider->getTimer(), i)));
    promises.add(kj::mv(promise));
  }

  for (size_t i = COUNT; i > 0 ; --i) {
    auto j = i-1;
    auto result = promises[j].wait(io.waitScope);
    KJ_REQUIRE(result == j);
  }
}

#endif  // KJ_HAS_COROUTINE

}  // namespace
}  // namespace kj
