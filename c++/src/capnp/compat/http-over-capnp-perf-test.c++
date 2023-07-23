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

#include "http-over-capnp.h"
#include <kj/compat/http.h>
#include <kj/test.h>
#include <kj/debug.h>
#include <capnp/rpc-twoparty.h>
#include <stdlib.h>

#if KJ_HAS_COROUTINE

namespace capnp {
namespace {

static constexpr auto HELLO_WORLD = "Hello, world!"_kj;

class NullInputStream final: public kj::AsyncInputStream {
public:
  NullInputStream(kj::Maybe<size_t> expectedLength = size_t(0))
      : expectedLength(expectedLength) {}

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return size_t(0);
  }

  kj::Maybe<uint64_t> tryGetLength() override {
    return expectedLength;
  }

  kj::Promise<uint64_t> pumpTo(kj::AsyncOutputStream& output, uint64_t amount) override {
    return uint64_t(0);
  }

private:
  kj::Maybe<size_t> expectedLength;
};

class VectorOutputStream: public kj::AsyncOutputStream {
public:
  kj::String consume() {
    chars.add('\0');
    return kj::String(chars.releaseAsArray());
  }

  kj::Promise<void> write(const void* buffer, size_t size) override {
    chars.addAll(kj::arrayPtr(reinterpret_cast<const char*>(buffer), size));
    return kj::READY_NOW;
  }

  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    for (auto piece: pieces) {
      chars.addAll(piece.asChars());
    }
    return kj::READY_NOW;
  }

  kj::Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }

private:
  kj::Vector<char> chars;
};

class MockService: public kj::HttpService {
public:
  MockService(kj::HttpHeaderTable::Builder& headerTableBuilder)
      : headerTable(headerTableBuilder.getFutureTable()),
        customHeaderId(headerTableBuilder.add("X-Custom-Header")) {}

  kj::Promise<void> request(
      kj::HttpMethod method, kj::StringPtr url, const kj::HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    KJ_ASSERT(method == kj::HttpMethod::GET);
    KJ_ASSERT(url == "http://foo"_kj);
    KJ_ASSERT(headers.get(customHeaderId) == "corge"_kj);

    kj::HttpHeaders responseHeaders(headerTable);
    responseHeaders.set(kj::HttpHeaderId::CONTENT_TYPE, "text/plain");
    responseHeaders.set(customHeaderId, "foobar"_kj);
    auto stream = response.send(200, "OK", responseHeaders);
    auto promise = stream->write(HELLO_WORLD.begin(), HELLO_WORLD.size());
    return promise.attach(kj::mv(stream));
  }

private:
  const kj::HttpHeaderTable& headerTable;
  kj::HttpHeaderId customHeaderId;
};

class MockSender: private kj::HttpService::Response {
public:
  MockSender(kj::HttpHeaderTable::Builder& headerTableBuilder)
      : headerTable(headerTableBuilder.getFutureTable()),
        customHeaderId(headerTableBuilder.add("X-Custom-Header")) {}

  kj::Promise<void> sendRequest(kj::HttpClient& client) {
    kj::HttpHeaders headers(headerTable);
    headers.set(customHeaderId, "corge"_kj);
    auto req = client.request(kj::HttpMethod::GET, "http://foo"_kj, headers);
    req.body = nullptr;
    auto resp = co_await req.response;
    KJ_ASSERT(resp.statusCode == 200);
    KJ_ASSERT(resp.statusText == "OK"_kj);
    KJ_ASSERT(resp.headers->get(customHeaderId) == "foobar"_kj);

    auto body = co_await resp.body->readAllText();
    KJ_ASSERT(body == HELLO_WORLD);
  }

  kj::Promise<void> sendRequest(kj::HttpService& service) {
    kj::HttpHeaders headers(headerTable);
    headers.set(customHeaderId, "corge"_kj);
    NullInputStream requestBody;
    co_await service.request(kj::HttpMethod::GET, "http://foo"_kj, headers, requestBody, *this);
    KJ_ASSERT(responseBody.consume() == HELLO_WORLD);
  }

private:
  const kj::HttpHeaderTable& headerTable;
  kj::HttpHeaderId customHeaderId;

  VectorOutputStream responseBody;

  kj::Own<kj::AsyncOutputStream> send(
      uint statusCode, kj::StringPtr statusText, const kj::HttpHeaders& headers,
      kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
    KJ_ASSERT(statusCode == 200);
    KJ_ASSERT(statusText == "OK"_kj);
    KJ_ASSERT(headers.get(customHeaderId) == "foobar"_kj);

    return kj::attachRef(responseBody);
  }

  kj::Own<kj::WebSocket> acceptWebSocket(const kj::HttpHeaders& headers) override {
    KJ_UNIMPLEMENTED("no WebSockets here");
  }
};

KJ_TEST("Benchmark baseline") {
  auto io = kj::setupAsyncIo();

  kj::HttpHeaderTable::Builder headerTableBuilder;
  MockService service(headerTableBuilder);
  MockSender sender(headerTableBuilder);
  auto headerTable = headerTableBuilder.build();

  doBenchmark([&]() {
    sender.sendRequest(service).wait(io.waitScope);
  });
}

KJ_TEST("Benchmark KJ HTTP client wrapper") {
  auto io = kj::setupAsyncIo();

  kj::HttpHeaderTable::Builder headerTableBuilder;
  MockService service(headerTableBuilder);
  MockSender sender(headerTableBuilder);
  auto headerTable = headerTableBuilder.build();

  auto client = kj::newHttpClient(service);

  doBenchmark([&]() {
    sender.sendRequest(*client).wait(io.waitScope);
  });
}

KJ_TEST("Benchmark KJ HTTP full protocol") {
  auto io = kj::setupAsyncIo();

  kj::HttpHeaderTable::Builder headerTableBuilder;
  MockService service(headerTableBuilder);
  MockSender sender(headerTableBuilder);
  auto headerTable = headerTableBuilder.build();

  kj::HttpServer server(io.provider->getTimer(), *headerTable, service);

  auto pipe = kj::newCapabilityPipe();
  kj::CapabilityStreamConnectionReceiver receiver(*pipe.ends[0]);
  kj::CapabilityStreamNetworkAddress addr(nullptr, *pipe.ends[1]);

  auto listenLoop = server.listenHttp(receiver)
      .eagerlyEvaluate([](kj::Exception&& e) noexcept { kj::throwFatalException(kj::mv(e)); });
  auto client = kj::newHttpClient(io.provider->getTimer(), *headerTable, addr);

  doBenchmark([&]() {
    sender.sendRequest(*client).wait(io.waitScope);
  });
}

KJ_TEST("Benchmark HTTP-over-capnp local call") {
  auto io = kj::setupAsyncIo();

  kj::HttpHeaderTable::Builder headerTableBuilder;
  MockService service(headerTableBuilder);
  MockSender sender(headerTableBuilder);
  HttpOverCapnpFactory::HeaderIdBundle headerIds(headerTableBuilder);
  auto headerTable = headerTableBuilder.build();

  // Client and server use different HttpOverCapnpFactory instances to block path-shortening.
  ByteStreamFactory bsFactory;
  HttpOverCapnpFactory hocFactory(bsFactory, headerIds.clone());
  ByteStreamFactory bsFactory2;
  HttpOverCapnpFactory hocFactory2(bsFactory2, kj::mv(headerIds));

  auto cap = hocFactory.kjToCapnp(kj::attachRef(service));
  auto roundTrip = hocFactory2.capnpToKj(kj::mv(cap));

  doBenchmark([&]() {
    sender.sendRequest(*roundTrip).wait(io.waitScope);
  });
}

KJ_TEST("Benchmark HTTP-over-capnp full RPC") {
  auto io = kj::setupAsyncIo();

  kj::HttpHeaderTable::Builder headerTableBuilder;
  MockService service(headerTableBuilder);
  MockSender sender(headerTableBuilder);
  HttpOverCapnpFactory::HeaderIdBundle headerIds(headerTableBuilder);
  auto headerTable = headerTableBuilder.build();

  // Client and server use different HttpOverCapnpFactory instances to block path-shortening.
  ByteStreamFactory bsFactory;
  HttpOverCapnpFactory hocFactory(bsFactory, headerIds.clone());
  ByteStreamFactory bsFactory2;
  HttpOverCapnpFactory hocFactory2(bsFactory2, kj::mv(headerIds));

  TwoPartyServer server(hocFactory.kjToCapnp(kj::attachRef(service)));

  auto pipe = kj::newTwoWayPipe();
  auto listenLoop = server.accept(*pipe.ends[0]);

  TwoPartyClient client(*pipe.ends[1]);

  auto roundTrip = hocFactory2.capnpToKj(client.bootstrap().castAs<capnp::HttpService>());

  doBenchmark([&]() {
    sender.sendRequest(*roundTrip).wait(io.waitScope);
  });
}

}  // namespace
}  // namespace capnp

#endif  // KJ_HAS_COROUTINE
