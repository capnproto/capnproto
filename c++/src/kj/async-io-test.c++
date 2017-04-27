// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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

#include "async-io.h"
#include "debug.h"
#include <kj/compat/gtest.h>
#include <sys/types.h>
#if _WIN32
#include <ws2tcpip.h>
#include "windows-sanity.h"
#else
#include <netdb.h>
#endif

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
    KJ_FAIL_EXPECT(exception);
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

bool hasIpv6() {
  // Can getaddrinfo() parse ipv6 addresses? This is only true if ipv6 is configured on at least
  // one interface. (The loopback interface usually has it even if others don't... but not always.)
  struct addrinfo* list;
  int status = getaddrinfo("::", nullptr, nullptr, &list);
  if (status == 0) {
    freeaddrinfo(list);
    return true;
  } else {
    return false;
  }
}

TEST(AsyncIo, AddressParsing) {
  auto ioContext = setupAsyncIo();
  auto& w = ioContext.waitScope;
  auto& network = ioContext.provider->getNetwork();

  EXPECT_EQ("*:0", tryParse(w, network, "*"));
  EXPECT_EQ("*:123", tryParse(w, network, "*:123"));
  EXPECT_EQ("0.0.0.0:0", tryParse(w, network, "0.0.0.0"));
  EXPECT_EQ("1.2.3.4:5678", tryParse(w, network, "1.2.3.4", 5678));

#if !_WIN32
  EXPECT_EQ("unix:foo/bar/baz", tryParse(w, network, "unix:foo/bar/baz"));
#endif

  // We can parse services by name...
#if !__ANDROID__  // Service names not supported on Android for some reason?
  EXPECT_EQ("1.2.3.4:80", tryParse(w, network, "1.2.3.4:http", 5678));
  EXPECT_EQ("*:80", tryParse(w, network, "*:http", 5678));
#endif

  // IPv6 tests. Annoyingly, these don't work on machines that don't have IPv6 configured on any
  // interfaces.
  if (hasIpv6()) {
    EXPECT_EQ("[::]:123", tryParse(w, network, "0::0", 123));
    EXPECT_EQ("[12ab:cd::34]:321", tryParse(w, network, "[12ab:cd:0::0:34]:321", 432));
#if !__ANDROID__  // Service names not supported on Android for some reason?
    EXPECT_EQ("[::]:80", tryParse(w, network, "[::]:http", 5678));
    EXPECT_EQ("[12ab:cd::34]:80", tryParse(w, network, "[12ab:cd::34]:http", 5678));
#endif
  }

  // It would be nice to test DNS lookup here but the test would not be very hermetic.  Even
  // localhost can map to different addresses depending on whether IPv6 is enabled.  We do
  // connect to "localhost" in a different test, though.
}

TEST(AsyncIo, OneWayPipe) {
  auto ioContext = setupAsyncIo();

  auto pipe = ioContext.provider->newOneWayPipe();
  char receiveBuffer[4];

  pipe.out->write("foo", 3).detach([](kj::Exception&& exception) {
    KJ_FAIL_EXPECT(exception);
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

TEST(AsyncIo, Timeouts) {
  auto ioContext = setupAsyncIo();

  Timer& timer = ioContext.provider->getTimer();

  auto promise1 = timer.timeoutAfter(10 * MILLISECONDS, kj::Promise<void>(kj::NEVER_DONE));
  auto promise2 = timer.timeoutAfter(100 * MILLISECONDS, kj::Promise<int>(123));

  EXPECT_TRUE(promise1.then([]() { return false; }, [](kj::Exception&& e) { return true; })
      .wait(ioContext.waitScope));
  EXPECT_EQ(123, promise2.wait(ioContext.waitScope));
}

#if !_WIN32  // datagrams not implemented on win32 yet

TEST(AsyncIo, Udp) {
  auto ioContext = setupAsyncIo();

  auto addr = ioContext.provider->getNetwork().parseAddress("127.0.0.1").wait(ioContext.waitScope);

  auto port1 = addr->bindDatagramPort();
  auto port2 = addr->bindDatagramPort();

  auto addr1 = ioContext.provider->getNetwork().parseAddress("127.0.0.1", port1->getPort())
      .wait(ioContext.waitScope);
  auto addr2 = ioContext.provider->getNetwork().parseAddress("127.0.0.1", port2->getPort())
      .wait(ioContext.waitScope);

  Own<NetworkAddress> receivedAddr;

  {
    // Send a message and receive it.
    EXPECT_EQ(3, port1->send("foo", 3, *addr2).wait(ioContext.waitScope));
    auto receiver = port2->makeReceiver();

    receiver->receive().wait(ioContext.waitScope);
    {
      auto content = receiver->getContent();
      EXPECT_EQ("foo", kj::heapString(content.value.asChars()));
      EXPECT_FALSE(content.isTruncated);
    }
    receivedAddr = receiver->getSource().clone();
    EXPECT_EQ(addr1->toString(), receivedAddr->toString());
    {
      auto ancillary = receiver->getAncillary();
      EXPECT_EQ(0, ancillary.value.size());
      EXPECT_FALSE(ancillary.isTruncated);
    }

    // Receive a second message with the same receiver.
    {
      auto promise = receiver->receive();  // This time, start receiving before sending
      EXPECT_EQ(6, port1->send("barbaz", 6, *addr2).wait(ioContext.waitScope));
      promise.wait(ioContext.waitScope);
      auto content = receiver->getContent();
      EXPECT_EQ("barbaz", kj::heapString(content.value.asChars()));
      EXPECT_FALSE(content.isTruncated);
    }
  }

  DatagramReceiver::Capacity capacity;
  capacity.content = 8;
  capacity.ancillary = 1024;

  {
    // Send a reply that will be truncated.
    EXPECT_EQ(16, port2->send("0123456789abcdef", 16, *receivedAddr).wait(ioContext.waitScope));
    auto recv1 = port1->makeReceiver(capacity);

    recv1->receive().wait(ioContext.waitScope);
    {
      auto content = recv1->getContent();
      EXPECT_EQ("01234567", kj::heapString(content.value.asChars()));
      EXPECT_TRUE(content.isTruncated);
    }
    EXPECT_EQ(addr2->toString(), recv1->getSource().toString());
    {
      auto ancillary = recv1->getAncillary();
      EXPECT_EQ(0, ancillary.value.size());
      EXPECT_FALSE(ancillary.isTruncated);
    }

#if defined(IP_PKTINFO) && !__CYGWIN__
    // Set IP_PKTINFO header and try to receive it.
    // Doesn't work on Cygwin; see: https://cygwin.com/ml/cygwin/2009-01/msg00350.html
    // TODO(someday): Might work on more-recent Cygwin; I'm still testing against 1.7.
    int one = 1;
    port1->setsockopt(IPPROTO_IP, IP_PKTINFO, &one, sizeof(one));

    EXPECT_EQ(3, port2->send("foo", 3, *addr1).wait(ioContext.waitScope));

    recv1->receive().wait(ioContext.waitScope);
    {
      auto content = recv1->getContent();
      EXPECT_EQ("foo", kj::heapString(content.value.asChars()));
      EXPECT_FALSE(content.isTruncated);
    }
    EXPECT_EQ(addr2->toString(), recv1->getSource().toString());
    {
      auto ancillary = recv1->getAncillary();
      EXPECT_FALSE(ancillary.isTruncated);
      ASSERT_EQ(1, ancillary.value.size());

      auto message = ancillary.value[0];
      EXPECT_EQ(IPPROTO_IP, message.getLevel());
      EXPECT_EQ(IP_PKTINFO, message.getType());
      EXPECT_EQ(sizeof(struct in_pktinfo), message.asArray<byte>().size());
      auto& pktinfo = KJ_ASSERT_NONNULL(message.as<struct in_pktinfo>());
      EXPECT_EQ(htonl(0x7F000001), pktinfo.ipi_addr.s_addr);  // 127.0.0.1
    }

    // See what happens if there's not quite enough space for in_pktinfo.
    capacity.ancillary = CMSG_SPACE(sizeof(struct in_pktinfo)) - 8;
    recv1 = port1->makeReceiver(capacity);

    EXPECT_EQ(3, port2->send("bar", 3, *addr1).wait(ioContext.waitScope));

    recv1->receive().wait(ioContext.waitScope);
    {
      auto content = recv1->getContent();
      EXPECT_EQ("bar", kj::heapString(content.value.asChars()));
      EXPECT_FALSE(content.isTruncated);
    }
    EXPECT_EQ(addr2->toString(), recv1->getSource().toString());
    {
      auto ancillary = recv1->getAncillary();
      EXPECT_TRUE(ancillary.isTruncated);

      // We might get a message, but it will be truncated.
      if (ancillary.value.size() != 0) {
        EXPECT_EQ(1, ancillary.value.size());

        auto message = ancillary.value[0];
        EXPECT_EQ(IPPROTO_IP, message.getLevel());
        EXPECT_EQ(IP_PKTINFO, message.getType());

        EXPECT_TRUE(message.as<struct in_pktinfo>() == nullptr);
        EXPECT_LT(message.asArray<byte>().size(), sizeof(struct in_pktinfo));
      }
    }

    // See what happens if there's not enough space even for the cmsghdr.
    capacity.ancillary = CMSG_SPACE(0) - 8;
    recv1 = port1->makeReceiver(capacity);

    EXPECT_EQ(3, port2->send("baz", 3, *addr1).wait(ioContext.waitScope));

    recv1->receive().wait(ioContext.waitScope);
    {
      auto content = recv1->getContent();
      EXPECT_EQ("baz", kj::heapString(content.value.asChars()));
      EXPECT_FALSE(content.isTruncated);
    }
    EXPECT_EQ(addr2->toString(), recv1->getSource().toString());
    {
      auto ancillary = recv1->getAncillary();
      EXPECT_TRUE(ancillary.isTruncated);
      EXPECT_EQ(0, ancillary.value.size());
    }
#endif
  }
}

#endif  // !_WIN32

}  // namespace
}  // namespace kj
