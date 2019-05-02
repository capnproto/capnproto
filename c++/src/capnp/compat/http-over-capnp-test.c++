// Copyright (c) 2019 Cloudflare, Inc. and contributors
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
#include <kj/test.h>

namespace capnp {
namespace {

KJ_TEST("KJ and RPC HTTP method enums match") {
#define EXPECT_MATCH(METHOD) \
  KJ_EXPECT(static_cast<uint>(kj::HttpMethod::METHOD) == \
            static_cast<uint>(capnp::HttpMethod::METHOD));

  KJ_HTTP_FOR_EACH_METHOD(EXPECT_MATCH);
#undef EXPECT_MATCH
}

// =======================================================================================

kj::Promise<void> expectRead(kj::AsyncInputStream& in, kj::StringPtr expected) {
  if (expected.size() == 0) return kj::READY_NOW;

  auto buffer = kj::heapArray<char>(expected.size());

  auto promise = in.tryRead(buffer.begin(), 1, buffer.size());
  return promise.then(kj::mvCapture(buffer, [&in,expected](kj::Array<char> buffer, size_t amount) {
    if (amount == 0) {
      KJ_FAIL_ASSERT("expected data never sent", expected);
    }

    auto actual = buffer.slice(0, amount);
    if (memcmp(actual.begin(), expected.begin(), actual.size()) != 0) {
      KJ_FAIL_ASSERT("data from stream doesn't match expected", expected, actual);
    }

    return expectRead(in, expected.slice(amount));
  }));
}

enum Direction {
  CLIENT_TO_SERVER,
  SERVER_TO_CLIENT
};

struct TestStep {
  Direction direction;
  kj::StringPtr send;
  kj::StringPtr receive;

  constexpr TestStep(Direction direction, kj::StringPtr send, kj::StringPtr receive)
      : direction(direction), send(send), receive(receive) {}
  constexpr TestStep(Direction direction, kj::StringPtr data)
      : direction(direction), send(data), receive(data) {}
};

constexpr TestStep TEST_STEPS[] = {
  // Test basic request.
  {
    CLIENT_TO_SERVER,

    "GET / HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "\r\n"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 3\r\n"
    "\r\n"
    "foo"_kj
  },

  // Try PUT, vary path, vary status
  {
    CLIENT_TO_SERVER,

    "PUT /foo/bar HTTP/1.1\r\n"
    "Content-Length: 5\r\n"
    "Host: example.com\r\n"
    "\r\n"
    "corge"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "HTTP/1.1 403 Unauthorized\r\n"
    "Content-Length: 4\r\n"
    "\r\n"
    "nope"_kj
  },

  // HEAD request
  {
    CLIENT_TO_SERVER,

    "HEAD /foo/bar HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "\r\n"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 4\r\n"
    "\r\n"_kj
  },

  // Empty-body response
  {
    CLIENT_TO_SERVER,

    "GET /foo/bar HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "\r\n"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "HTTP/1.1 304 Not Modified\r\n"
    "Server: foo\r\n"
    "\r\n"_kj
  },

  // Chonky body
  {
    CLIENT_TO_SERVER,

    "POST / HTTP/1.1\r\n"
    "Transfer-Encoding: chunked\r\n"
    "Host: example.com\r\n"
    "\r\n"
    "3\r\n"
    "foo\r\n"
    "5\r\n"
    "corge\r\n"
    "0\r\n"
    "\r\n"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "6\r\n"
    "barbaz\r\n"
    "6\r\n"
    "garply\r\n"
    "0\r\n"
    "\r\n"_kj
  },

  // Streaming
  {
    CLIENT_TO_SERVER,

    "POST / HTTP/1.1\r\n"
    "Content-Length: 9\r\n"
    "Host: example.com\r\n"
    "\r\n"_kj,
  },
  {
    CLIENT_TO_SERVER,

    "foo"_kj,
  },
  {
    CLIENT_TO_SERVER,

    "bar"_kj,
  },
  {
    CLIENT_TO_SERVER,

    "baz"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "6\r\n"
    "barbaz\r\n"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "6\r\n"
    "garply\r\n"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "0\r\n"
    "\r\n"_kj
  },

  // Bidirectional.
  {
    CLIENT_TO_SERVER,

    "POST / HTTP/1.1\r\n"
    "Content-Length: 9\r\n"
    "Host: example.com\r\n"
    "\r\n"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"_kj,
  },
  {
    CLIENT_TO_SERVER,

    "foo"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "6\r\n"
    "barbaz\r\n"_kj,
  },
  {
    CLIENT_TO_SERVER,

    "bar"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "6\r\n"
    "garply\r\n"_kj,
  },
  {
    CLIENT_TO_SERVER,

    "baz"_kj,
  },
  {
    SERVER_TO_CLIENT,

    "0\r\n"
    "\r\n"_kj
  },

  // Test headers being re-ordered by KJ. This isn't necessary behavior, but it does prove that
  // we're not testing a pure streaming pass-through...
  {
    CLIENT_TO_SERVER,

    "GET / HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "Accept: text/html\r\n"
    "Foo-Header: 123\r\n"
    "User-Agent: kj\r\n"
    "Accept-Language: en\r\n"
    "\r\n"_kj,

    "GET / HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "Accept-Language: en\r\n"
    "Accept: text/html\r\n"
    "User-Agent: kj\r\n"
    "Foo-Header: 123\r\n"
    "\r\n"_kj
  },
  {
    SERVER_TO_CLIENT,

    "HTTP/1.1 200 OK\r\n"
    "Server: kj\r\n"
    "Bar: 321\r\n"
    "Content-Length: 3\r\n"
    "\r\n"
    "foo"_kj,

    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 3\r\n"
    "Server: kj\r\n"
    "Bar: 321\r\n"
    "\r\n"
    "foo"_kj
  },

  // We finish up a request with no response, to test cancellation.
  {
    CLIENT_TO_SERVER,

    "GET / HTTP/1.1\r\n"
    "Host: example.com\r\n"
    "\r\n"_kj,
  },
};

class OneConnectNetworkAddress final: public kj::NetworkAddress {
public:
  OneConnectNetworkAddress(kj::Own<kj::AsyncIoStream> stream)
      : stream(kj::mv(stream)) {}

  kj::Promise<kj::Own<kj::AsyncIoStream>> connect() override {
    auto result = KJ_ASSERT_NONNULL(kj::mv(stream));
    stream = nullptr;
    return kj::mv(result);
  }

  kj::Own<kj::ConnectionReceiver> listen() override { KJ_UNIMPLEMENTED("test"); }
  kj::Own<kj::NetworkAddress> clone() override { KJ_UNIMPLEMENTED("test"); }
  kj::String toString() override { KJ_UNIMPLEMENTED("test"); }

private:
  kj::Maybe<kj::Own<kj::AsyncIoStream>> stream;
};

void runEndToEndTests(kj::Timer& timer, kj::HttpHeaderTable& headerTable,
                      HttpOverCapnpFactory& clientFactory, HttpOverCapnpFactory& serverFactory,
                      kj::WaitScope& waitScope) {
  auto clientPipe = kj::newTwoWayPipe();
  auto serverPipe = kj::newTwoWayPipe();

  OneConnectNetworkAddress oneConnectAddr(kj::mv(serverPipe.ends[0]));

  auto backHttp = kj::newHttpClient(timer, headerTable, oneConnectAddr);
  auto backCapnp = serverFactory.kjToCapnp(kj::newHttpService(*backHttp));
  auto frontCapnp = clientFactory.capnpToKj(backCapnp);
  kj::HttpServer frontKj(timer, headerTable, *frontCapnp);
  auto listenTask = frontKj.listenHttp(kj::mv(clientPipe.ends[1]))
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  for (auto& step: TEST_STEPS) {
    KJ_CONTEXT(step.send);

    kj::AsyncOutputStream* out;
    kj::AsyncInputStream* in;

    switch (step.direction) {
      case CLIENT_TO_SERVER:
        out = clientPipe.ends[0];
        in = serverPipe.ends[1];
        break;
      case SERVER_TO_CLIENT:
        out = serverPipe.ends[1];
        in = clientPipe.ends[0];
        break;
    }

    auto writePromise = out->write(step.send.begin(), step.send.size());
    auto readPromise = expectRead(*in, step.receive);
    if (!writePromise.poll(waitScope)) {
      if (readPromise.poll(waitScope)) {
        readPromise.wait(waitScope);
        KJ_FAIL_ASSERT("write hung, read worked fine");
      } else {
        KJ_FAIL_ASSERT("write and read both hung");
      }
    }

    writePromise.wait(waitScope);
    KJ_ASSERT(readPromise.poll(waitScope), "read hung");
    readPromise.wait(waitScope);
  }

  // The last test message was a request with no response. If we now close the client end, this
  // should propagate all the way through to close the server end!
  clientPipe.ends[0] = nullptr;
  auto lastRead = serverPipe.ends[1]->readAllText();
  KJ_ASSERT(lastRead.poll(waitScope), "last read hung");
  KJ_EXPECT(lastRead.wait(waitScope) == 0);
}

KJ_TEST("HTTP-over-Cap'n-Proto E2E, no path shortening") {
  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  ByteStreamFactory streamFactory1;
  ByteStreamFactory streamFactory2;
  kj::HttpHeaderTable::Builder tableBuilder;
  HttpOverCapnpFactory factory1(streamFactory1, tableBuilder);
  HttpOverCapnpFactory factory2(streamFactory2, tableBuilder);
  auto headerTable = tableBuilder.build();

  runEndToEndTests(timer, *headerTable, factory1, factory2, waitScope);
}

KJ_TEST("HTTP-over-Cap'n-Proto E2E, with path shortening") {
  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  ByteStreamFactory streamFactory;
  kj::HttpHeaderTable::Builder tableBuilder;
  HttpOverCapnpFactory factory(streamFactory, tableBuilder);
  auto headerTable = tableBuilder.build();

  runEndToEndTests(timer, *headerTable, factory, factory, waitScope);
}

// =======================================================================================

class WebSocketAccepter final: public kj::HttpService {
public:
  WebSocketAccepter(kj::HttpHeaderTable& headerTable,
                    kj::Own<kj::PromiseFulfiller<kj::Own<kj::WebSocket>>> fulfiller,
                    kj::Promise<void> done)
      : headerTable(headerTable), fulfiller(kj::mv(fulfiller)), done(kj::mv(done)) {}

  kj::Promise<void> request(
      kj::HttpMethod method, kj::StringPtr url, const kj::HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) {
    kj::HttpHeaders respHeaders(headerTable);
    respHeaders.add("X-Foo", "bar");
    fulfiller->fulfill(response.acceptWebSocket(respHeaders));
    return kj::mv(done);
  }

private:
  kj::HttpHeaderTable& headerTable;
  kj::Own<kj::PromiseFulfiller<kj::Own<kj::WebSocket>>> fulfiller;
  kj::Promise<void> done;
};

void runWebSocketTests(kj::HttpHeaderTable& headerTable,
                       HttpOverCapnpFactory& clientFactory, HttpOverCapnpFactory& serverFactory,
                       kj::WaitScope& waitScope) {
  // We take a different approach here, because writing out raw WebSocket frames is a pain.
  // It's easier to test WebSockets at the KJ API level.

  auto wsPaf = kj::newPromiseAndFulfiller<kj::Own<kj::WebSocket>>();
  auto donePaf = kj::newPromiseAndFulfiller<void>();

  auto back = serverFactory.kjToCapnp(kj::heap<WebSocketAccepter>(
    headerTable, kj::mv(wsPaf.fulfiller), kj::mv(donePaf.promise)));
  auto front = clientFactory.capnpToKj(back);
  auto client = kj::newHttpClient(*front);

  auto resp = client->openWebSocket("/ws", kj::HttpHeaders(headerTable)).wait(waitScope);
  KJ_ASSERT(resp.webSocketOrBody.is<kj::Own<kj::WebSocket>>());

  auto clientWs = kj::mv(resp.webSocketOrBody.get<kj::Own<kj::WebSocket>>());
  auto serverWs = wsPaf.promise.wait(waitScope);

  {
    auto promise = clientWs->send("foo"_kj);
    auto message = serverWs->receive().wait(waitScope);
    promise.wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "foo");
  }

  {
    auto promise = serverWs->send("bar"_kj.asBytes());
    auto message = clientWs->receive().wait(waitScope);
    promise.wait(waitScope);
    KJ_ASSERT(message.is<kj::Array<kj::byte>>());
    KJ_EXPECT(kj::str(message.get<kj::Array<kj::byte>>().asChars()) == "bar");
  }

  {
    auto promise = clientWs->close(1234, "baz"_kj);
    auto message = serverWs->receive().wait(waitScope);
    promise.wait(waitScope);
    KJ_ASSERT(message.is<kj::WebSocket::Close>());
    KJ_EXPECT(message.get<kj::WebSocket::Close>().code == 1234);
    KJ_EXPECT(message.get<kj::WebSocket::Close>().reason == "baz");
  }

  {
    auto promise = serverWs->disconnect();
    auto receivePromise = clientWs->receive();
    KJ_EXPECT(receivePromise.poll(waitScope));
    KJ_EXPECT_THROW(DISCONNECTED, receivePromise.wait(waitScope));
    promise.wait(waitScope);
  }
}

KJ_TEST("HTTP-over-Cap'n Proto WebSocket, no path shortening") {
  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory streamFactory1;
  ByteStreamFactory streamFactory2;
  kj::HttpHeaderTable::Builder tableBuilder;
  HttpOverCapnpFactory factory1(streamFactory1, tableBuilder);
  HttpOverCapnpFactory factory2(streamFactory2, tableBuilder);
  auto headerTable = tableBuilder.build();

  runWebSocketTests(*headerTable, factory1, factory2, waitScope);
}

KJ_TEST("HTTP-over-Cap'n Proto WebSocket, with path shortening") {
  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory streamFactory;
  kj::HttpHeaderTable::Builder tableBuilder;
  HttpOverCapnpFactory factory(streamFactory, tableBuilder);
  auto headerTable = tableBuilder.build();

  runWebSocketTests(*headerTable, factory, factory, waitScope);
}

}  // namespace
}  // namespace capnp
