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
#if KJ_BENCHMARK_MALLOC
#include <dlfcn.h>
#endif

#if KJ_HAS_COROUTINE

namespace capnp {
namespace {

// =======================================================================================
// Metrics-gathering
//
// TODO(cleanup): Generalize for other benchmarks?

static size_t globalMallocCount = 0;
static size_t globalMallocBytes = 0;

#if KJ_BENCHMARK_MALLOC
// If KJ_BENCHMARK_MALLOC is defined then we are instructed to override malloc() in order to
// measure total allocations. We are careful only to define this when the build is set up in a
// way that this won't cause build failures (e.g., we must not be statically linking a malloc
// implementation).

extern "C" {

void* malloc(size_t size) {
  typedef void* Malloc(size_t);
  static Malloc* realMalloc = reinterpret_cast<Malloc*>(dlsym(RTLD_NEXT, "malloc"));

  ++globalMallocCount;
  globalMallocBytes += size;
  return realMalloc(size);
}

}  // extern "C"

#endif  // KJ_BENCHMARK_MALLOC

class Metrics {
public:
  Metrics()
      : startMallocCount(globalMallocCount), startMallocBytes(globalMallocBytes),
        upBandwidth(0), downBandwidth(0),
        clientReadCount(0), clientWriteCount(0),
        serverReadCount(0), serverWriteCount(0) {}
  ~Metrics() noexcept(false) {
  #if KJ_BENCHMARK_MALLOC
    size_t mallocCount = globalMallocCount - startMallocCount;
    size_t mallocBytes = globalMallocBytes - startMallocBytes;
    KJ_LOG(WARNING, mallocCount, mallocBytes);
  #endif

    if (hadStreamPair) {
      KJ_LOG(WARNING, upBandwidth, downBandwidth,
          clientReadCount, clientWriteCount, serverReadCount, serverWriteCount);
    }
  }

  enum Side { CLIENT, SERVER };

  class StreamWrapper final: public kj::AsyncIoStream {
    // Wrap a stream and count metrics.

  public:
    StreamWrapper(Metrics& metrics, kj::AsyncIoStream& inner, Side side)
        : metrics(metrics), inner(inner), side(side) {}

    ~StreamWrapper() noexcept(false) {
      switch (side) {
        case CLIENT:
          metrics.clientReadCount += readCount;
          metrics.clientWriteCount += writeCount;
          metrics.upBandwidth += writeBytes;
          metrics.downBandwidth += readBytes;
          break;
        case SERVER:
          metrics.serverReadCount += readCount;
          metrics.serverWriteCount += writeCount;
          break;
      }
    }

    kj::Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
      return inner.read(buffer, minBytes, maxBytes)
          .then([this](size_t n) {
        ++readCount;
        readBytes += n;
        return n;
      });
    }
    kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
      return inner.tryRead(buffer, minBytes, maxBytes)
          .then([this](size_t n) {
        ++readCount;
        readBytes += n;
        return n;
      });
    }

    kj::Maybe<uint64_t> tryGetLength() override {
      return inner.tryGetLength();
    }

    kj::Promise<void> write(const void* buffer, size_t size)  override {
      ++writeCount;
      writeBytes += size;
      return inner.write(buffer, size);
    }
    kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
      ++writeCount;
      for (auto& piece: pieces) {
        writeBytes += piece.size();
      }
      return inner.write(pieces);
    }

    kj::Promise<uint64_t> pumpTo(
        kj::AsyncOutputStream& output, uint64_t amount = kj::maxValue) override {
      // Our benchmarks don't depend on this currently. If they do we need to think about how to
      // apply it.
      KJ_UNIMPLEMENTED("pump metrics");
    }
    kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(
        AsyncInputStream& input, uint64_t amount = kj::maxValue) override {
      // Our benchmarks don't depend on this currently. If they do we need to think about how to
      // apply it.
      KJ_UNIMPLEMENTED("pump metrics");
    }

    kj::Promise<void> whenWriteDisconnected() override {
      return inner.whenWriteDisconnected();
    }

    void shutdownWrite() override {
      inner.shutdownWrite();
    }
    void abortRead() override {
      inner.abortRead();
    }

  private:
    Metrics& metrics;
    kj::AsyncIoStream& inner;
    Side side;

    size_t readCount = 0;
    size_t readBytes = 0;
    size_t writeCount = 0;
    size_t writeBytes = 0;
  };

  struct StreamPair {
    kj::TwoWayPipe pipe;
    StreamWrapper client;
    StreamWrapper server;

    StreamPair(Metrics& metrics)
        : pipe(kj::newTwoWayPipe()),
          client(metrics, *pipe.ends[0], CLIENT),
          server(metrics, *pipe.ends[1], SERVER) {
      metrics.hadStreamPair = true;
    }
  };

private:
  size_t startMallocCount KJ_UNUSED;
  size_t startMallocBytes KJ_UNUSED;
  size_t upBandwidth;
  size_t downBandwidth;
  size_t clientReadCount;
  size_t clientWriteCount;
  size_t serverReadCount;
  size_t serverWriteCount;

  bool hadStreamPair = false;
};

// =======================================================================================

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
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  Metrics metrics;

  kj::HttpHeaderTable::Builder headerTableBuilder;
  MockService service(headerTableBuilder);
  MockSender sender(headerTableBuilder);
  auto headerTable = headerTableBuilder.build();

  doBenchmark([&]() {
    sender.sendRequest(service).wait(waitScope);
  });
}

KJ_TEST("Benchmark KJ HTTP client wrapper") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  Metrics metrics;

  kj::HttpHeaderTable::Builder headerTableBuilder;
  MockService service(headerTableBuilder);
  MockSender sender(headerTableBuilder);
  auto headerTable = headerTableBuilder.build();

  auto client = kj::newHttpClient(service);

  doBenchmark([&]() {
    sender.sendRequest(*client).wait(waitScope);
  });
}

KJ_TEST("Benchmark KJ HTTP full protocol") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  Metrics metrics;
  Metrics::StreamPair pair(metrics);
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  kj::HttpHeaderTable::Builder headerTableBuilder;
  MockService service(headerTableBuilder);
  MockSender sender(headerTableBuilder);
  auto headerTable = headerTableBuilder.build();

  kj::HttpServer server(timer, *headerTable, service);
  auto listenLoop = server.listenHttp({&pair.server, kj::NullDisposer::instance})
      .eagerlyEvaluate([](kj::Exception&& e) noexcept { kj::throwFatalException(kj::mv(e)); });
  auto client = kj::newHttpClient(*headerTable, pair.client);

  doBenchmark([&]() {
    sender.sendRequest(*client).wait(waitScope);
  });
}

KJ_TEST("Benchmark HTTP-over-capnp local call") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  Metrics metrics;

  kj::HttpHeaderTable::Builder headerTableBuilder;
  MockService service(headerTableBuilder);
  MockSender sender(headerTableBuilder);
  HttpOverCapnpFactory::HeaderIdBundle headerIds(headerTableBuilder);
  auto headerTable = headerTableBuilder.build();

  // Client and server use different HttpOverCapnpFactory instances to block path-shortening.
  ByteStreamFactory bsFactory;
  HttpOverCapnpFactory hocFactory(bsFactory, headerIds.clone(), HttpOverCapnpFactory::LEVEL_2);
  ByteStreamFactory bsFactory2;
  HttpOverCapnpFactory hocFactory2(bsFactory2, kj::mv(headerIds), HttpOverCapnpFactory::LEVEL_2);

  auto cap = hocFactory.kjToCapnp(kj::attachRef(service));
  auto roundTrip = hocFactory2.capnpToKj(kj::mv(cap));

  doBenchmark([&]() {
    sender.sendRequest(*roundTrip).wait(waitScope);
  });
}

KJ_TEST("Benchmark HTTP-over-capnp full RPC") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  Metrics metrics;
  Metrics::StreamPair pair(metrics);

  kj::HttpHeaderTable::Builder headerTableBuilder;
  MockService service(headerTableBuilder);
  MockSender sender(headerTableBuilder);
  HttpOverCapnpFactory::HeaderIdBundle headerIds(headerTableBuilder);
  auto headerTable = headerTableBuilder.build();

  // Client and server use different HttpOverCapnpFactory instances to block path-shortening.
  ByteStreamFactory bsFactory;
  HttpOverCapnpFactory hocFactory(bsFactory, headerIds.clone(), HttpOverCapnpFactory::LEVEL_2);
  ByteStreamFactory bsFactory2;
  HttpOverCapnpFactory hocFactory2(bsFactory2, kj::mv(headerIds), HttpOverCapnpFactory::LEVEL_2);

  TwoPartyServer server(hocFactory.kjToCapnp(kj::attachRef(service)));

  auto pipe = kj::newTwoWayPipe();
  auto listenLoop = server.accept(pair.server);

  TwoPartyClient client(pair.client);

  auto roundTrip = hocFactory2.capnpToKj(client.bootstrap().castAs<capnp::HttpService>());

  doBenchmark([&]() {
    sender.sendRequest(*roundTrip).wait(waitScope);
  });
}

}  // namespace
}  // namespace capnp

#endif  // KJ_HAS_COROUTINE
