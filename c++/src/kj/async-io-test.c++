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

#include "async-io.h"
#include "async-unix.h"
#include "debug.h"
#include <gtest/gtest.h>

namespace kj {
namespace {

TEST(AsyncIo, SimpleNetwork) {
  UnixEventLoop loop;
  auto network = Network::newSystemNetwork();

  Own<ConnectionReceiver> listener;
  Own<AsyncIoStream> server;
  Own<AsyncIoStream> client;

  char receiveBuffer[4];

  auto port = newPromiseAndFulfiller<uint>();

  port.promise.then([&](uint portnum) {
    return network->parseRemoteAddress("127.0.0.1", portnum);
  }).then([&](Own<RemoteAddress>&& result) {
    return result->connect();
  }).then([&](Own<AsyncIoStream>&& result) {
    client = kj::mv(result);
    return client->write("foo", 3);
  }).daemonize([](kj::Exception&& exception) {
    ADD_FAILURE() << kj::str(exception).cStr();
  });

  kj::String result = network->parseLocalAddress("*").then([&](Own<LocalAddress>&& result) {
    listener = result->listen();
    port.fulfiller->fulfill(listener->getPort());
    return listener->accept();
  }).then([&](Own<AsyncIoStream>&& result) {
    server = kj::mv(result);
    return server->tryRead(receiveBuffer, 3, 4);
  }).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return heapString(receiveBuffer, n);
  }).wait();

  EXPECT_EQ("foo", result);
}

String tryParseLocal(EventLoop& loop, Network& network, StringPtr text, uint portHint = 0) {
  return network.parseLocalAddress(text, portHint).wait()->toString();
}

String tryParseRemote(EventLoop& loop, Network& network, StringPtr text, uint portHint = 0) {
  return network.parseRemoteAddress(text, portHint).wait()->toString();
}

TEST(AsyncIo, AddressParsing) {
  UnixEventLoop loop;
  auto network = Network::newSystemNetwork();

  EXPECT_EQ("*:0", tryParseLocal(loop, *network, "*"));
  EXPECT_EQ("*:123", tryParseLocal(loop, *network, "123"));
  EXPECT_EQ("*:123", tryParseLocal(loop, *network, ":123"));
  EXPECT_EQ("[::]:123", tryParseLocal(loop, *network, "0::0", 123));
  EXPECT_EQ("0.0.0.0:0", tryParseLocal(loop, *network, "0.0.0.0"));
  EXPECT_EQ("1.2.3.4:5678", tryParseRemote(loop, *network, "1.2.3.4", 5678));
  EXPECT_EQ("[12ab:cd::34]:321", tryParseRemote(loop, *network, "[12ab:cd:0::0:34]:321", 432));

  EXPECT_EQ("unix:foo/bar/baz", tryParseLocal(loop, *network, "unix:foo/bar/baz"));
  EXPECT_EQ("unix:foo/bar/baz", tryParseRemote(loop, *network, "unix:foo/bar/baz"));
}

TEST(AsyncIo, OneWayPipe) {
  UnixEventLoop loop;

  auto pipe = newOneWayPipe();
  char receiveBuffer[4];

  pipe.out->write("foo", 3).daemonize([](kj::Exception&& exception) {
    ADD_FAILURE() << kj::str(exception).cStr();
  });

  kj::String result = pipe.in->tryRead(receiveBuffer, 3, 4).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return heapString(receiveBuffer, n);
  }).wait();

  EXPECT_EQ("foo", result);
}

TEST(AsyncIo, TwoWayPipe) {
  UnixEventLoop loop;

  auto pipe = newTwoWayPipe();
  char receiveBuffer1[4];
  char receiveBuffer2[4];

  auto promise = pipe.ends[0]->write("foo", 3).then([&]() {
    return pipe.ends[0]->tryRead(receiveBuffer1, 3, 4);
  }).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return heapString(receiveBuffer1, n);
  });

  kj::String result = pipe.ends[1]->write("bar", 3).then([&]() {
    return pipe.ends[1]->tryRead(receiveBuffer2, 3, 4);
  }).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return heapString(receiveBuffer2, n);
  }).wait();

  kj::String result2 = promise.wait();

  EXPECT_EQ("foo", result);
  EXPECT_EQ("bar", result2);
}

TEST(AsyncIo, RunIoEventLoop) {
  auto pipe = newOneWayPipe();
  char receiveBuffer[4];

  String result = runIoEventLoop([&]() {
    auto promise1 = pipe.out->write("foo", 3);

    auto promise2 = pipe.in->tryRead(receiveBuffer, 3, 4)
      .then([&](size_t n) {
        EXPECT_EQ(3u, n);
        return heapString(receiveBuffer, n);
      });

    return promise1.then(mvCapture(promise2, [](Promise<String> promise2) { return promise2; }));
  });

  EXPECT_EQ("foo", result);
}

}  // namespace
}  // namespace kj
