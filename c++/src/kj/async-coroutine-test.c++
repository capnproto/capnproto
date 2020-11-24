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
    co_return;
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

struct Counter {
  size_t& count;
  Counter(size_t& count): count(count) {}
  ~Counter() { ++count; }
  KJ_DISALLOW_COPY(Counter);
};

KJ_TEST("Coroutines can be canceled while suspended") {
  EventLoop loop;
  WaitScope waitScope(loop);

  size_t count = 0;

  auto coro = [&](kj::Promise<int> promise) -> kj::Promise<void> {
    Counter counter1(count);
    co_await kj::evalLater([](){});
    Counter counter2(count);
    co_await promise;
  };

  {
    auto neverDone = kj::Promise<int>(kj::NEVER_DONE);
    neverDone = neverDone.attach(kj::heap<Counter>(count));
    auto promise = coro(kj::mv(neverDone));
    KJ_EXPECT(!promise.poll(waitScope));
  }

  // Stack variables on both sides of a co_await, plus coroutine arguments are destroyed.
  KJ_EXPECT(count == 3);
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
  // later fulfill, thus arming the Coroutine's Event. If we destroy the coroutine in this state,
  // EventLoop will throw on destruction because it can still see the Event in its list.

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

Promise<void> sendData(Promise<Own<NetworkAddress>> addressPromise) {
  auto address = co_await addressPromise;
  auto client = co_await address->connect();
  co_await client->write("foo", 3);
}

Promise<String> receiveDataCoroutine(Promise<Own<NetworkAddress>> addressPromise) {
  auto address = co_await addressPromise;
  auto listener = address->listen();
  auto server = co_await listener->accept();
  char buffer[4];
  auto n = co_await server->read(buffer, 3, 4);
  KJ_EXPECT(3u == n);
  co_return heapString(buffer, n);
}

KJ_TEST("Simple network test with coroutine") {
  auto io = setupAsyncIo();
  auto& network = io.provider->getNetwork();

  constexpr uint port = 5500;

  sendData(network.parseAddress("localhost", port)).detach([](Exception&& exception) {
    KJ_FAIL_EXPECT(exception);
  });

  String result = receiveDataCoroutine(network.parseAddress("*", port)).wait(io.waitScope);

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

#endif  // KJ_HAS_COROUTINE

}  // namespace
}  // namespace kj
