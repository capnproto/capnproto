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

KJ_TEST("Exceptions before the first co_await don't leak, but reject the promise") {
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

KJ_TEST("Coroutines can be canceled while suspended") {
  EventLoop loop;
  WaitScope waitScope(loop);

  size_t count = 0;

  struct Counter {
    size_t& count;
    Counter(size_t& count): count(count) {}
    ~Counter() { ++count; }
    KJ_DISALLOW_COPY(Counter);
  };

  {
    auto neverDone = kj::Promise<int>(kj::NEVER_DONE);
    neverDone = neverDone.attach(kj::heap<Counter>(count));
    auto promise = simpleCoroutine(kj::mv(neverDone));
  }

  KJ_EXPECT(count == 1);
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
