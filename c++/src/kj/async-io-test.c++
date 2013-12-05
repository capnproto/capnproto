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
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();

  Own<ConnectionReceiver> listener;
  Own<AsyncIoStream> server;
  Own<AsyncIoStream> client;

  char receiveBuffer[4];

  auto port = newPromiseAndFulfiller<uint>();

  port.promise.then([&](uint portnum) {
    return network.parseAddress("localhost", portnum);
  }).then([&](Own<NetworkAddress>&& result) {
    return result->connect();
  }).then([&](Own<AsyncIoStream>&& result) {
    client = kj::mv(result);
    return client->write("foo", 3);
  }).detach([](kj::Exception&& exception) {
    ADD_FAILURE() << kj::str(exception).cStr();
  });

  kj::String result = network.parseAddress("*").then([&](Own<NetworkAddress>&& result) {
    listener = result->listen();
    port.fulfiller->fulfill(listener->getPort());
    return listener->accept();
  }).then([&](Own<AsyncIoStream>&& result) {
    server = kj::mv(result);
    return server->tryRead(receiveBuffer, 3, 4);
  }).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return heapString(receiveBuffer, n);
  }).wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
}

String tryParse(WaitScope& waitScope, Network& network, StringPtr text, uint portHint = 0) {
  return network.parseAddress(text, portHint).wait(waitScope)->toString();
}

TEST(AsyncIo, AddressParsing) {
  auto ioContext = setupAsyncIo();
  auto& w = ioContext.waitScope;
  auto& network = ioContext.provider->getNetwork();

  EXPECT_EQ("*:0", tryParse(w, network, "*"));
  EXPECT_EQ("*:123", tryParse(w, network, "*:123"));
  EXPECT_EQ("[::]:123", tryParse(w, network, "0::0", 123));
  EXPECT_EQ("0.0.0.0:0", tryParse(w, network, "0.0.0.0"));
  EXPECT_EQ("1.2.3.4:5678", tryParse(w, network, "1.2.3.4", 5678));
  EXPECT_EQ("[12ab:cd::34]:321", tryParse(w, network, "[12ab:cd:0::0:34]:321", 432));

  EXPECT_EQ("unix:foo/bar/baz", tryParse(w, network, "unix:foo/bar/baz"));

  // We can parse services by name...
  EXPECT_EQ("1.2.3.4:80", tryParse(w, network, "1.2.3.4:http", 5678));
  EXPECT_EQ("[::]:80", tryParse(w, network, "[::]:http", 5678));
  EXPECT_EQ("[12ab:cd::34]:80", tryParse(w, network, "[12ab:cd::34]:http", 5678));
  EXPECT_EQ("*:80", tryParse(w, network, "*:http", 5678));

  // It would be nice to test DNS lookup here but the test would not be very hermetic.  Even
  // localhost can map to different addresses depending on whether IPv6 is enabled.  We do
  // connect to "localhost" in a different test, though.
}

TEST(AsyncIo, OneWayPipe) {
  auto ioContext = setupAsyncIo();

  auto pipe = ioContext.provider->newOneWayPipe();
  char receiveBuffer[4];

  pipe.out->write("foo", 3).detach([](kj::Exception&& exception) {
    ADD_FAILURE() << kj::str(exception).cStr();
  });

  kj::String result = pipe.in->tryRead(receiveBuffer, 3, 4).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return heapString(receiveBuffer, n);
  }).wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
}

TEST(AsyncIo, TwoWayPipe) {
  auto ioContext = setupAsyncIo();

  auto pipe = ioContext.provider->newTwoWayPipe();
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
  }).wait(ioContext.waitScope);

  kj::String result2 = promise.wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
  EXPECT_EQ("bar", result2);
}

TEST(AsyncIo, PipeThread) {
  auto ioContext = setupAsyncIo();

  auto pipeThread = ioContext.provider->newPipeThread(
      [](AsyncIoProvider& ioProvider, AsyncIoStream& stream, WaitScope& waitScope) {
    char buf[4];
    stream.write("foo", 3).wait(waitScope);
    EXPECT_EQ(3u, stream.tryRead(buf, 3, 4).wait(waitScope));
    EXPECT_EQ("bar", heapString(buf, 3));

    // Expect disconnect.
    EXPECT_EQ(0, stream.tryRead(buf, 1, 1).wait(waitScope));
  });

  char buf[4];
  pipeThread.pipe->write("bar", 3).wait(ioContext.waitScope);
  EXPECT_EQ(3u, pipeThread.pipe->tryRead(buf, 3, 4).wait(ioContext.waitScope));
  EXPECT_EQ("foo", heapString(buf, 3));
}

TEST(AsyncIo, PipeThreadDisconnects) {
  // Like above, but in this case we expect the main thread to detect the pipe thread disconnecting.

  auto ioContext = setupAsyncIo();

  auto pipeThread = ioContext.provider->newPipeThread(
      [](AsyncIoProvider& ioProvider, AsyncIoStream& stream, WaitScope& waitScope) {
    char buf[4];
    stream.write("foo", 3).wait(waitScope);
    EXPECT_EQ(3u, stream.tryRead(buf, 3, 4).wait(waitScope));
    EXPECT_EQ("bar", heapString(buf, 3));
  });

  char buf[4];
  EXPECT_EQ(3u, pipeThread.pipe->tryRead(buf, 3, 4).wait(ioContext.waitScope));
  EXPECT_EQ("foo", heapString(buf, 3));

  pipeThread.pipe->write("bar", 3).wait(ioContext.waitScope);

  // Expect disconnect.
  EXPECT_EQ(0, pipeThread.pipe->tryRead(buf, 1, 1).wait(ioContext.waitScope));
}

}  // namespace
}  // namespace kj
