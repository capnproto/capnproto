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

#if _WIN32
// Request Vista-level APIs.
#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#endif

#include "async-io.h"
#include "async-io-internal.h"
#include "debug.h"
#include <kj/compat/gtest.h>
#include <sys/types.h>
#if _WIN32
#include <ws2tcpip.h>
#include "windows-sanity.h"
#define inet_pton InetPtonA
#define inet_ntop InetNtopA
#else
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
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

bool systemSupportsAddress(StringPtr addr, StringPtr service = nullptr) {
  // Can getaddrinfo() parse this addresses? This is only true if the address family (e.g., ipv6)
  // is configured on at least one interface. (The loopback interface usually has both ipv4 and
  // ipv6 configured, but not always.)
  struct addrinfo* list;
  int status = getaddrinfo(
      addr.cStr(), service == nullptr ? nullptr : service.cStr(), nullptr, &list);
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
  EXPECT_EQ("unix-abstract:foo/bar/baz", tryParse(w, network, "unix-abstract:foo/bar/baz"));
#endif

  // We can parse services by name...
  //
  // For some reason, Android and some various Linux distros do not support service names.
  if (systemSupportsAddress("1.2.3.4", "http")) {
    EXPECT_EQ("1.2.3.4:80", tryParse(w, network, "1.2.3.4:http", 5678));
    EXPECT_EQ("*:80", tryParse(w, network, "*:http", 5678));
  } else {
    KJ_LOG(WARNING, "system does not support resolving service names on ipv4; skipping tests");
  }

  // IPv6 tests. Annoyingly, these don't work on machines that don't have IPv6 configured on any
  // interfaces.
  if (systemSupportsAddress("::")) {
    EXPECT_EQ("[::]:123", tryParse(w, network, "0::0", 123));
    EXPECT_EQ("[12ab:cd::34]:321", tryParse(w, network, "[12ab:cd:0::0:34]:321", 432));
    if (systemSupportsAddress("12ab:cd::34", "http")) {
      EXPECT_EQ("[::]:80", tryParse(w, network, "[::]:http", 5678));
      EXPECT_EQ("[12ab:cd::34]:80", tryParse(w, network, "[12ab:cd::34]:http", 5678));
    } else {
      KJ_LOG(WARNING, "system does not support resolving service names on ipv6; skipping tests");
    }
  } else {
    KJ_LOG(WARNING, "system does not support ipv6; skipping tests");
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

#if !_WIN32 && !__CYGWIN__
TEST(AsyncIo, CapabilityPipe) {
  auto ioContext = setupAsyncIo();

  auto pipe = ioContext.provider->newCapabilityPipe();
  auto pipe2 = ioContext.provider->newCapabilityPipe();
  char receiveBuffer1[4];
  char receiveBuffer2[4];

  // Expect to receive a stream, then write "bar" to it, then receive "foo" from it.
  Own<AsyncCapabilityStream> receivedStream;
  auto promise = pipe2.ends[1]->receiveStream()
      .then([&](Own<AsyncCapabilityStream> stream) {
    receivedStream = kj::mv(stream);
    return receivedStream->write("bar", 3);
  }).then([&]() {
    return receivedStream->tryRead(receiveBuffer2, 3, 4);
  }).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return heapString(receiveBuffer2, n);
  });

  // Send a stream, then write "foo" to the other end of the sent stream, then receive "bar"
  // from it.
  kj::String result = pipe2.ends[0]->sendStream(kj::mv(pipe.ends[1]))
      .then([&]() {
    return pipe.ends[0]->write("foo", 3);
  }).then([&]() {
    return pipe.ends[0]->tryRead(receiveBuffer1, 3, 4);
  }).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return heapString(receiveBuffer1, n);
  }).wait(ioContext.waitScope);

  kj::String result2 = promise.wait(ioContext.waitScope);

  EXPECT_EQ("bar", result);
  EXPECT_EQ("foo", result2);
}
#endif

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

bool isMsgTruncBroken() {
  // Detect if the kernel fails to set MSG_TRUNC on recvmsg(). This seems to be the case at least
  // when running an arm64 binary under qemu.

  int fd;
  KJ_SYSCALL(fd = socket(AF_INET, SOCK_DGRAM, 0));
  KJ_DEFER(close(fd));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(0x7f000001);
  KJ_SYSCALL(bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)));

  // Read back the assigned port.
  socklen_t len = sizeof(addr);
  KJ_SYSCALL(getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len));
  KJ_ASSERT(len == sizeof(addr));

  const char* message = "foobar";
  KJ_SYSCALL(sendto(fd, message, strlen(message), 0,
      reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)));

  char buf[4];
  struct iovec iov;
  iov.iov_base = buf;
  iov.iov_len = 3;
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  ssize_t n;
  KJ_SYSCALL(n = recvmsg(fd, &msg, 0));
  KJ_ASSERT(n == 3);

  buf[3] = 0;
  KJ_ASSERT(kj::StringPtr(buf) == "foo");

  return (msg.msg_flags & MSG_TRUNC) == 0;
}

TEST(AsyncIo, Udp) {
  bool msgTruncBroken = isMsgTruncBroken();

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
      EXPECT_TRUE(content.isTruncated || msgTruncBroken);
    }
    EXPECT_EQ(addr2->toString(), recv1->getSource().toString());
    {
      auto ancillary = recv1->getAncillary();
      EXPECT_EQ(0, ancillary.value.size());
      EXPECT_FALSE(ancillary.isTruncated);
    }

#if defined(IP_PKTINFO) && !__CYGWIN__ && !__aarch64__
    // Set IP_PKTINFO header and try to receive it.
    //
    // Doesn't work on Cygwin; see: https://cygwin.com/ml/cygwin/2009-01/msg00350.html
    // TODO(someday): Might work on more-recent Cygwin; I'm still testing against 1.7.
    //
    // Doesn't work when running arm64 binaries under QEMU -- in fact, it crashes QEMU. We don't
    // have a good way to test if we're under QEMU so we just skip this test on aarch64.
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
      EXPECT_TRUE(ancillary.isTruncated || msgTruncBroken);

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

#ifdef __linux__  // Abstract unix sockets are only supported on Linux

TEST(AsyncIo, AbstractUnixSocket) {
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();

  Own<NetworkAddress> addr = network.parseAddress("unix-abstract:foo").wait(ioContext.waitScope);

  Own<ConnectionReceiver> listener = addr->listen();
  // chdir proves no filesystem dependence. Test fails for regular unix socket
  // but passes for abstract unix socket.
  int originalDirFd;
  KJ_SYSCALL(originalDirFd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  KJ_DEFER(close(originalDirFd));
  KJ_SYSCALL(chdir("/"));
  KJ_DEFER(KJ_SYSCALL(fchdir(originalDirFd)));

  addr->connect().attach(kj::mv(listener)).wait(ioContext.waitScope);
}

#endif  // __linux__

KJ_TEST("CIDR parsing") {
  KJ_EXPECT(_::CidrRange("1.2.3.4/16").toString() == "1.2.0.0/16");
  KJ_EXPECT(_::CidrRange("1.2.255.4/18").toString() == "1.2.192.0/18");
  KJ_EXPECT(_::CidrRange("1234::abcd:ffff:ffff/98").toString() == "1234::abcd:c000:0/98");

  KJ_EXPECT(_::CidrRange::inet4({1,2,255,4}, 18).toString() == "1.2.192.0/18");
  KJ_EXPECT(_::CidrRange::inet6({0x1234, 0x5678}, {0xabcd, 0xffff, 0xffff}, 98).toString() ==
            "1234:5678::abcd:c000:0/98");

  union {
    struct sockaddr addr;
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
  };
  memset(&addr6, 0, sizeof(addr6));

  {
    addr4.sin_family = AF_INET;
    addr4.sin_addr.s_addr = htonl(0x0102dfff);
    KJ_EXPECT(_::CidrRange("1.2.255.255/18").matches(&addr));
    KJ_EXPECT(!_::CidrRange("1.2.255.255/19").matches(&addr));
    KJ_EXPECT(_::CidrRange("1.2.0.0/16").matches(&addr));
    KJ_EXPECT(!_::CidrRange("1.3.0.0/16").matches(&addr));
    KJ_EXPECT(_::CidrRange("1.2.223.255/32").matches(&addr));
    KJ_EXPECT(_::CidrRange("0.0.0.0/0").matches(&addr));
    KJ_EXPECT(!_::CidrRange("::/0").matches(&addr));
  }

  {
    addr4.sin_family = AF_INET6;
    byte bytes[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    memcpy(addr6.sin6_addr.s6_addr, bytes, 16);
    KJ_EXPECT(_::CidrRange("0102:03ff::/24").matches(&addr));
    KJ_EXPECT(!_::CidrRange("0102:02ff::/24").matches(&addr));
    KJ_EXPECT(_::CidrRange("0102:02ff::/23").matches(&addr));
    KJ_EXPECT(_::CidrRange("0102:0304:0506:0708:090a:0b0c:0d0e:0f10/128").matches(&addr));
    KJ_EXPECT(_::CidrRange("::/0").matches(&addr));
    KJ_EXPECT(!_::CidrRange("0.0.0.0/0").matches(&addr));
  }

  {
    addr4.sin_family = AF_INET6;
    inet_pton(AF_INET6, "::ffff:1.2.223.255", &addr6.sin6_addr);
    KJ_EXPECT(_::CidrRange("1.2.255.255/18").matches(&addr));
    KJ_EXPECT(!_::CidrRange("1.2.255.255/19").matches(&addr));
    KJ_EXPECT(_::CidrRange("1.2.0.0/16").matches(&addr));
    KJ_EXPECT(!_::CidrRange("1.3.0.0/16").matches(&addr));
    KJ_EXPECT(_::CidrRange("1.2.223.255/32").matches(&addr));
    KJ_EXPECT(_::CidrRange("0.0.0.0/0").matches(&addr));
    KJ_EXPECT(_::CidrRange("::/0").matches(&addr));
  }
}

bool allowed4(_::NetworkFilter& filter, StringPtr addrStr) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, addrStr.cStr(), &addr.sin_addr);
  return filter.shouldAllow(reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
}

bool allowed6(_::NetworkFilter& filter, StringPtr addrStr) {
  struct sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  inet_pton(AF_INET6, addrStr.cStr(), &addr.sin6_addr);
  return filter.shouldAllow(reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
}

KJ_TEST("NetworkFilter") {
  _::NetworkFilter base;

  KJ_EXPECT(allowed4(base, "8.8.8.8"));
  KJ_EXPECT(!allowed4(base, "240.1.2.3"));

  {
    _::NetworkFilter filter({"public"}, {}, base);

    KJ_EXPECT(allowed4(filter, "8.8.8.8"));
    KJ_EXPECT(!allowed4(filter, "240.1.2.3"));

    KJ_EXPECT(!allowed4(filter, "192.168.0.1"));
    KJ_EXPECT(!allowed4(filter, "10.1.2.3"));
    KJ_EXPECT(!allowed4(filter, "127.0.0.1"));
    KJ_EXPECT(!allowed4(filter, "0.0.0.0"));

    KJ_EXPECT(allowed6(filter, "2400:cb00:2048:1::c629:d7a2"));
    KJ_EXPECT(!allowed6(filter, "fc00::1234"));
    KJ_EXPECT(!allowed6(filter, "::1"));
    KJ_EXPECT(!allowed6(filter, "::"));
  }

  {
    _::NetworkFilter filter({"private"}, {"local"}, base);

    KJ_EXPECT(!allowed4(filter, "8.8.8.8"));
    KJ_EXPECT(!allowed4(filter, "240.1.2.3"));

    KJ_EXPECT(allowed4(filter, "192.168.0.1"));
    KJ_EXPECT(allowed4(filter, "10.1.2.3"));
    KJ_EXPECT(!allowed4(filter, "127.0.0.1"));
    KJ_EXPECT(!allowed4(filter, "0.0.0.0"));

    KJ_EXPECT(!allowed6(filter, "2400:cb00:2048:1::c629:d7a2"));
    KJ_EXPECT(allowed6(filter, "fc00::1234"));
    KJ_EXPECT(!allowed6(filter, "::1"));
    KJ_EXPECT(!allowed6(filter, "::"));
  }

  {
    _::NetworkFilter filter({"1.0.0.0/8", "1.2.3.0/24"}, {"1.2.0.0/16", "1.2.3.4/32"}, base);

    KJ_EXPECT(!allowed4(filter, "8.8.8.8"));
    KJ_EXPECT(!allowed4(filter, "240.1.2.3"));

    KJ_EXPECT(allowed4(filter, "1.0.0.1"));
    KJ_EXPECT(!allowed4(filter, "1.2.2.1"));
    KJ_EXPECT(allowed4(filter, "1.2.3.1"));
    KJ_EXPECT(!allowed4(filter, "1.2.3.4"));
  }
}

KJ_TEST("Network::restrictPeers()") {
  auto ioContext = setupAsyncIo();
  auto& w = ioContext.waitScope;
  auto& network = ioContext.provider->getNetwork();
  auto restrictedNetwork = network.restrictPeers({"public"});

  KJ_EXPECT(tryParse(w, *restrictedNetwork, "8.8.8.8") == "8.8.8.8:0");
#if !_WIN32
  KJ_EXPECT_THROW_MESSAGE("restrictPeers", tryParse(w, *restrictedNetwork, "unix:/foo"));
#endif

  auto addr = restrictedNetwork->parseAddress("127.0.0.1").wait(w);

  auto listener = addr->listen();
  auto acceptTask = listener->accept()
      .then([](kj::Own<kj::AsyncIoStream>) {
    KJ_FAIL_EXPECT("should not have received connection");
  }).eagerlyEvaluate(nullptr);

  KJ_EXPECT_THROW_MESSAGE("restrictPeers", addr->connect().wait(w));

  // We can connect to the listener but the connection will be immediately closed.
  auto addr2 = network.parseAddress("127.0.0.1", listener->getPort()).wait(w);
  auto conn = addr2->connect().wait(w);
  KJ_EXPECT(conn->readAllText().wait(w) == "");
}

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

class MockAsyncInputStream: public AsyncInputStream {
public:
  MockAsyncInputStream(kj::ArrayPtr<const byte> bytes, size_t blockSize)
      : bytes(bytes), blockSize(blockSize) {}

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    // Clamp max read to blockSize.
    size_t n = kj::min(blockSize, maxBytes);

    // Unless that's less than minBytes -- in which case, use minBytes.
    n = kj::max(n, minBytes);

    // But also don't read more data than we have.
    n = kj::min(n, bytes.size());

    memcpy(buffer, bytes.begin(), n);
    bytes = bytes.slice(n, bytes.size());
    return n;
  }

private:
  kj::ArrayPtr<const byte> bytes;
  size_t blockSize;
};

KJ_TEST("AsyncInputStream::readAllText() / readAllBytes()") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto bigText = strArray(kj::repeat("foo bar baz"_kj, 12345), ",");
  size_t inputSizes[] = { 0, 1, 256, 4096, 8191, 8192, 8193, 10000, bigText.size() };
  size_t blockSizes[] = { 1, 4, 256, 4096, 8192, bigText.size() };
  uint64_t limits[] = {
    0, 1, 256,
    bigText.size() / 2,
    bigText.size() - 1,
    bigText.size(),
    bigText.size() + 1,
    kj::maxValue
  };

  for (size_t inputSize: inputSizes) {
    for (size_t blockSize: blockSizes) {
      for (uint64_t limit: limits) {
        KJ_CONTEXT(inputSize, blockSize, limit);
        auto textSlice = bigText.asBytes().slice(0, inputSize);
        auto readAllText = [&]() {
          MockAsyncInputStream input(textSlice, blockSize);
          return input.readAllText(limit).wait(ws);
        };
        auto readAllBytes = [&]() {
          MockAsyncInputStream input(textSlice, blockSize);
          return input.readAllBytes(limit).wait(ws);
        };
        if (limit > inputSize) {
          KJ_EXPECT(readAllText().asBytes() == textSlice);
          KJ_EXPECT(readAllBytes() == textSlice);
        } else {
          KJ_EXPECT_THROW_MESSAGE("Reached limit before EOF.", readAllText());
          KJ_EXPECT_THROW_MESSAGE("Reached limit before EOF.", readAllBytes());
        }
      }
    }
  }
}

KJ_TEST("Userland pipe") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  auto promise = pipe.out->write("foo", 3);
  KJ_EXPECT(!promise.poll(ws));

  char buf[4];
  KJ_EXPECT(pipe.in->tryRead(buf, 1, 4).wait(ws) == 3);
  buf[3] = '\0';
  KJ_EXPECT(buf == "foo"_kj);

  promise.wait(ws);

  auto promise2 = pipe.in->readAllText();
  KJ_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  KJ_EXPECT(promise2.wait(ws) == "");
}

KJ_TEST("Userland pipe cancel write") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  auto promise = pipe.out->write("foobar", 6);
  KJ_EXPECT(!promise.poll(ws));

  expectRead(*pipe.in, "foo").wait(ws);
  KJ_EXPECT(!promise.poll(ws));
  promise = nullptr;

  promise = pipe.out->write("baz", 3);
  expectRead(*pipe.in, "baz").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  KJ_EXPECT(pipe.in->readAllText().wait(ws) == "");
}

KJ_TEST("Userland pipe cancel read") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  auto writeOp = pipe.out->write("foo", 3);
  auto readOp = expectRead(*pipe.in, "foobar");
  writeOp.wait(ws);
  KJ_EXPECT(!readOp.poll(ws));
  readOp = nullptr;

  auto writeOp2 = pipe.out->write("baz", 3);
  expectRead(*pipe.in, "baz").wait(ws);
}

KJ_TEST("Userland pipe pumpTo") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  auto promise = pipe.out->write("foo", 3);
  KJ_EXPECT(!promise.poll(ws));

  expectRead(*pipe2.in, "foo").wait(ws);

  promise.wait(ws);

  auto promise2 = pipe2.in->readAllText();
  KJ_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 3);
}

KJ_TEST("Userland pipe tryPumpFrom") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  auto promise = pipe.out->write("foo", 3);
  KJ_EXPECT(!promise.poll(ws));

  expectRead(*pipe2.in, "foo").wait(ws);

  promise.wait(ws);

  auto promise2 = pipe2.in->readAllText();
  KJ_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  KJ_EXPECT(!promise2.poll(ws));
  KJ_EXPECT(pumpPromise.wait(ws) == 3);
}

KJ_TEST("Userland pipe pumpTo cancel") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  auto promise = pipe.out->write("foobar", 3);
  KJ_EXPECT(!promise.poll(ws));

  expectRead(*pipe2.in, "foo").wait(ws);

  // Cancel pump.
  pumpPromise = nullptr;

  auto promise3 = pipe2.out->write("baz", 3);
  expectRead(*pipe2.in, "baz").wait(ws);
}

KJ_TEST("Userland pipe tryPumpFrom cancel") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  auto promise = pipe.out->write("foobar", 3);
  KJ_EXPECT(!promise.poll(ws));

  expectRead(*pipe2.in, "foo").wait(ws);

  // Cancel pump.
  pumpPromise = nullptr;

  auto promise3 = pipe2.out->write("baz", 3);
  expectRead(*pipe2.in, "baz").wait(ws);
}

KJ_TEST("Userland pipe with limit") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe(6);

  {
    auto promise = pipe.out->write("foo", 3);
    KJ_EXPECT(!promise.poll(ws));
    expectRead(*pipe.in, "foo").wait(ws);
    promise.wait(ws);
  }

  {
    auto promise = pipe.in->readAllText();
    KJ_EXPECT(!promise.poll(ws));
    auto promise2 = pipe.out->write("barbaz", 6);
    KJ_EXPECT(promise.wait(ws) == "bar");
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("read end of pipe was aborted", promise2.wait(ws));
  }

  // Further writes throw and reads return EOF.
  KJ_EXPECT_THROW_MESSAGE("abortRead() has been called", pipe.out->write("baz", 3).wait(ws));
  KJ_EXPECT(pipe.in->readAllText().wait(ws) == "");
}

KJ_TEST("Userland pipe pumpTo with limit") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe(6);
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  {
    auto promise = pipe.out->write("foo", 3);
    KJ_EXPECT(!promise.poll(ws));
    expectRead(*pipe2.in, "foo").wait(ws);
    promise.wait(ws);
  }

  {
    auto promise = expectRead(*pipe2.in, "bar");
    KJ_EXPECT(!promise.poll(ws));
    auto promise2 = pipe.out->write("barbaz", 6);
    promise.wait(ws);
    pumpPromise.wait(ws);
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("read end of pipe was aborted", promise2.wait(ws));
  }

  // Further writes throw.
  KJ_EXPECT_THROW_MESSAGE("abortRead() has been called", pipe.out->write("baz", 3).wait(ws));
}

KJ_TEST("Userland pipe gather write") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe.in, "foobar").wait(ws);
  promise.wait(ws);

  auto promise2 = pipe.in->readAllText();
  KJ_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  KJ_EXPECT(promise2.wait(ws) == "");
}

KJ_TEST("Userland pipe gather write split on buffer boundary") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe.in, "foo").wait(ws);
  expectRead(*pipe.in, "bar").wait(ws);
  promise.wait(ws);

  auto promise2 = pipe.in->readAllText();
  KJ_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  KJ_EXPECT(promise2.wait(ws) == "");
}

KJ_TEST("Userland pipe gather write split mid-first-buffer") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe.in, "fo").wait(ws);
  expectRead(*pipe.in, "obar").wait(ws);
  promise.wait(ws);

  auto promise2 = pipe.in->readAllText();
  KJ_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  KJ_EXPECT(promise2.wait(ws) == "");
}

KJ_TEST("Userland pipe gather write split mid-second-buffer") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe.in, "foob").wait(ws);
  expectRead(*pipe.in, "ar").wait(ws);
  promise.wait(ws);

  auto promise2 = pipe.in->readAllText();
  KJ_EXPECT(!promise2.poll(ws));

  pipe.out = nullptr;
  KJ_EXPECT(promise2.wait(ws) == "");
}

KJ_TEST("Userland pipe gather write pump") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 6);
}

KJ_TEST("Userland pipe gather write pump split on buffer boundary") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foo").wait(ws);
  expectRead(*pipe2.in, "bar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 6);
}

KJ_TEST("Userland pipe gather write pump split mid-first-buffer") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "fo").wait(ws);
  expectRead(*pipe2.in, "obar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 6);
}

KJ_TEST("Userland pipe gather write pump split mid-second-buffer") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out);

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foob").wait(ws);
  expectRead(*pipe2.in, "ar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 6);
}

KJ_TEST("Userland pipe gather write split pump on buffer boundary") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out, 3)
      .then([&](uint64_t i) {
    KJ_EXPECT(i == 3);
    return pipe.in->pumpTo(*pipe2.out, 3);
  });

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 3);
}

KJ_TEST("Userland pipe gather write split pump mid-first-buffer") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out, 2)
      .then([&](uint64_t i) {
    KJ_EXPECT(i == 2);
    return pipe.in->pumpTo(*pipe2.out, 4);
  });

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 4);
}

KJ_TEST("Userland pipe gather write split pump mid-second-buffer") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out, 4)
      .then([&](uint64_t i) {
    KJ_EXPECT(i == 4);
    return pipe.in->pumpTo(*pipe2.out, 2);
  });

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 2);
}

KJ_TEST("Userland pipe gather write pumpFrom") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  char c;
  auto eofPromise = pipe2.in->tryRead(&c, 1, 1);
  eofPromise.poll(ws);  // force pump to notice EOF
  KJ_EXPECT(pumpPromise.wait(ws) == 6);
  pipe2.out = nullptr;
  KJ_EXPECT(eofPromise.wait(ws) == 0);
}

KJ_TEST("Userland pipe gather write pumpFrom split on buffer boundary") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foo").wait(ws);
  expectRead(*pipe2.in, "bar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  char c;
  auto eofPromise = pipe2.in->tryRead(&c, 1, 1);
  eofPromise.poll(ws);  // force pump to notice EOF
  KJ_EXPECT(pumpPromise.wait(ws) == 6);
  pipe2.out = nullptr;
  KJ_EXPECT(eofPromise.wait(ws) == 0);
}

KJ_TEST("Userland pipe gather write pumpFrom split mid-first-buffer") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "fo").wait(ws);
  expectRead(*pipe2.in, "obar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  char c;
  auto eofPromise = pipe2.in->tryRead(&c, 1, 1);
  eofPromise.poll(ws);  // force pump to notice EOF
  KJ_EXPECT(pumpPromise.wait(ws) == 6);
  pipe2.out = nullptr;
  KJ_EXPECT(eofPromise.wait(ws) == 0);
}

KJ_TEST("Userland pipe gather write pumpFrom split mid-second-buffer") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foob").wait(ws);
  expectRead(*pipe2.in, "ar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  char c;
  auto eofPromise = pipe2.in->tryRead(&c, 1, 1);
  eofPromise.poll(ws);  // force pump to notice EOF
  KJ_EXPECT(pumpPromise.wait(ws) == 6);
  pipe2.out = nullptr;
  KJ_EXPECT(eofPromise.wait(ws) == 0);
}

KJ_TEST("Userland pipe gather write split pumpFrom on buffer boundary") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 3))
      .then([&](uint64_t i) {
    KJ_EXPECT(i == 3);
    return KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 3));
  });

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 3);
}

KJ_TEST("Userland pipe gather write split pumpFrom mid-first-buffer") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 2))
      .then([&](uint64_t i) {
    KJ_EXPECT(i == 2);
    return KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 4));
  });

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 4);
}

KJ_TEST("Userland pipe gather write split pumpFrom mid-second-buffer") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 4))
      .then([&](uint64_t i) {
    KJ_EXPECT(i == 4);
    return KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 2));
  });

  ArrayPtr<const byte> parts[] = { "foo"_kj.asBytes(), "bar"_kj.asBytes() };
  auto promise = pipe.out->write(parts);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 2);
}

KJ_TEST("Userland pipe pumpTo less than write amount") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = pipe.in->pumpTo(*pipe2.out, 1);

  auto pieces = kj::heapArray<ArrayPtr<const byte>>(2);
  byte a[1] = { 'a' };
  byte b[1] = { 'b' };
  pieces[0] = arrayPtr(a, 1);
  pieces[1] = arrayPtr(b, 1);

  auto writePromise = pipe.out->write(pieces);
  KJ_EXPECT(!writePromise.poll(ws));

  expectRead(*pipe2.in, "a").wait(ws);
  KJ_EXPECT(pumpPromise.wait(ws) == 1);
  KJ_EXPECT(!writePromise.poll(ws));

  pumpPromise = pipe.in->pumpTo(*pipe2.out, 1);

  expectRead(*pipe2.in, "b").wait(ws);
  KJ_EXPECT(pumpPromise.wait(ws) == 1);
  writePromise.wait(ws);
}

KJ_TEST("Userland pipe pumpFrom EOF on abortRead()") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  auto promise = pipe.out->write("foobar", 6);
  KJ_EXPECT(!promise.poll(ws));
  expectRead(*pipe2.in, "foobar").wait(ws);
  promise.wait(ws);

  KJ_EXPECT(!pumpPromise.poll(ws));
  pipe.out = nullptr;
  pipe2.in = nullptr;  // force pump to notice EOF
  KJ_EXPECT(pumpPromise.wait(ws) == 6);
  pipe2.out = nullptr;
}

}  // namespace
}  // namespace kj
