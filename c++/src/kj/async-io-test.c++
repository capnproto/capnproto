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
#include "win32-api-version.h"
#elif !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "async-io.h"
#include "async-io-internal.h"
#include "debug.h"
#include "io.h"
#include "cidr.h"
#include "miniposix.h"
#include <kj/compat/gtest.h>
#include <kj/time.h>
#include <sys/types.h>
#include <kj/filesystem.h>
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

#if !_WIN32  // TODO(someday): Implement NetworkPeerIdentity for Win32.
TEST(AsyncIo, SimpleNetworkAuthentication) {
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();

  Own<ConnectionReceiver> listener;
  Own<AsyncIoStream> server;
  Own<AsyncIoStream> client;

  char receiveBuffer[4];

  auto port = newPromiseAndFulfiller<uint>();

  port.promise.then([&](uint portnum) {
    return network.parseAddress("localhost", portnum);
  }).then([&](Own<NetworkAddress>&& addr) {
    auto promise = addr->connectAuthenticated();
    return promise.then([&,addr=kj::mv(addr)](AuthenticatedStream result) mutable {
      auto id = result.peerIdentity.downcast<NetworkPeerIdentity>();

      // `addr` was resolved from `localhost` and may contain multiple addresses, but
      // result.peerIdentity tells us the specific address that was used. So it should be one
      // of the ones on the list, but only one.
      KJ_EXPECT(strstr(addr->toString().cStr(), id->getAddress().toString().cStr()) != nullptr);
      KJ_EXPECT(id->getAddress().toString().findFirst(',') == nullptr);

      client = kj::mv(result.stream);

      // `id` should match client->getpeername().
      union {
        struct sockaddr generic;
        struct sockaddr_in ip4;
        struct sockaddr_in6 ip6;
      } rawAddr;
      uint len = sizeof(rawAddr);
      client->getpeername(&rawAddr.generic, &len);
      auto peername = network.getSockaddr(&rawAddr.generic, len);
      KJ_EXPECT(id->toString() == peername->toString());

      return client->write("foo", 3);
    });
  }).detach([](kj::Exception&& exception) {
    KJ_FAIL_EXPECT(exception);
  });

  kj::String result = network.parseAddress("*").then([&](Own<NetworkAddress>&& result) {
    listener = result->listen();
    port.fulfiller->fulfill(listener->getPort());
    return listener->acceptAuthenticated();
  }).then([&](AuthenticatedStream result) {
    auto id = result.peerIdentity.downcast<NetworkPeerIdentity>();
    server = kj::mv(result.stream);

    // `id` should match server->getpeername().
    union {
      struct sockaddr generic;
      struct sockaddr_in ip4;
      struct sockaddr_in6 ip6;
    } addr;
    uint len = sizeof(addr);
    server->getpeername(&addr.generic, &len);
    auto peername = network.getSockaddr(&addr.generic, len);
    KJ_EXPECT(id->toString() == peername->toString());

    return server->tryRead(receiveBuffer, 3, 4);
  }).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return heapString(receiveBuffer, n);
  }).wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
}
#endif

#if !_WIN32 && !__CYGWIN__  // TODO(someday): Debug why this deadlocks on Cygwin.

#if __ANDROID__
#define TMPDIR "/data/local/tmp"
#else
#define TMPDIR "/tmp"
#endif

TEST(AsyncIo, UnixSocket) {
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();

  auto path = kj::str(TMPDIR "/kj-async-io-test.", getpid());
  KJ_DEFER(unlink(path.cStr()));

  Own<ConnectionReceiver> listener;
  Own<AsyncIoStream> server;
  Own<AsyncIoStream> client;

  char receiveBuffer[4];

  auto ready = newPromiseAndFulfiller<void>();

  ready.promise.then([&]() {
    return network.parseAddress(kj::str("unix:", path));
  }).then([&](Own<NetworkAddress>&& addr) {
    auto promise = addr->connectAuthenticated();
    return promise.then([&,addr=kj::mv(addr)](AuthenticatedStream result) mutable {
      auto id = result.peerIdentity.downcast<LocalPeerIdentity>();
      auto creds = id->getCredentials();
      KJ_IF_MAYBE(p, creds.pid) {
        KJ_EXPECT(*p == getpid());
#if __linux__ || __APPLE__
      } else {
        KJ_FAIL_EXPECT("LocalPeerIdentity for unix socket had null PID");
#endif
      }
      KJ_IF_MAYBE(u, creds.uid) {
        KJ_EXPECT(*u == getuid());
      } else {
        KJ_FAIL_EXPECT("LocalPeerIdentity for unix socket had null UID");
      }

      client = kj::mv(result.stream);
      return client->write("foo", 3);
    });
  }).detach([](kj::Exception&& exception) {
    KJ_FAIL_EXPECT(exception);
  });

  kj::String result = network.parseAddress(kj::str("unix:", path))
      .then([&](Own<NetworkAddress>&& result) {
    listener = result->listen();
    ready.fulfiller->fulfill();
    return listener->acceptAuthenticated();
  }).then([&](AuthenticatedStream result) {
    auto id = result.peerIdentity.downcast<LocalPeerIdentity>();
    auto creds = id->getCredentials();
    KJ_IF_MAYBE(p, creds.pid) {
      KJ_EXPECT(*p == getpid());
#if __linux__ || __APPLE__
    } else {
      KJ_FAIL_EXPECT("LocalPeerIdentity for unix socket had null PID");
#endif
    }
    KJ_IF_MAYBE(u, creds.uid) {
      KJ_EXPECT(*u == getuid());
    } else {
      KJ_FAIL_EXPECT("LocalPeerIdentity for unix socket had null UID");
    }

    server = kj::mv(result.stream);
    return server->tryRead(receiveBuffer, 3, 4);
  }).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return heapString(receiveBuffer, n);
  }).wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
}

TEST(AsyncIo, AncillaryMessageHandlerNoMsg) {
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();

  Own<ConnectionReceiver> listener;
  Own<AsyncIoStream> server;
  Own<AsyncIoStream> client;

  char receiveBuffer[4];

  bool clientHandlerCalled = false;
  kj::Function<void(kj::ArrayPtr<AncillaryMessage>)> clientHandler =
    [&](kj::ArrayPtr<AncillaryMessage>) {
    clientHandlerCalled = true;
  };
  bool serverHandlerCalled = false;
  kj::Function<void(kj::ArrayPtr<AncillaryMessage>)> serverHandler =
    [&](kj::ArrayPtr<AncillaryMessage>) {
    serverHandlerCalled = true;
  };

  auto port = newPromiseAndFulfiller<uint>();

  port.promise.then([&](uint portnum) {
    return network.parseAddress("localhost", portnum);
  }).then([&](Own<NetworkAddress>&& addr) {
    auto promise = addr->connectAuthenticated();
    return promise.then([&,addr=kj::mv(addr)](AuthenticatedStream result) mutable {
      client = kj::mv(result.stream);
      client->registerAncillaryMessageHandler(kj::mv(clientHandler));
      return client->write("foo", 3);
    });
  }).detach([](kj::Exception&& exception) {
    KJ_FAIL_EXPECT(exception);
  });

  kj::String result = network.parseAddress("*").then([&](Own<NetworkAddress>&& result) {
    listener = result->listen();
    port.fulfiller->fulfill(listener->getPort());
    return listener->acceptAuthenticated();
  }).then([&](AuthenticatedStream result) {
    server = kj::mv(result.stream);
    server->registerAncillaryMessageHandler(kj::mv(serverHandler));
    return server->tryRead(receiveBuffer, 3, 4);
  }).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return heapString(receiveBuffer, n);
  }).wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
  EXPECT_FALSE(clientHandlerCalled);
  EXPECT_FALSE(serverHandlerCalled);
}
#endif

// This test uses SO_TIMESTAMP on a SOCK_STREAM, which is only supported by Linux. Ideally we'd
// rewrite the test to use some other message type that is widely supported on streams. But for
// now we just limit the test to Linux. Also, it doesn't work on Android for some reason, and it
// isn't worth investigating, so we skip it there.
#if __linux__ && !__ANDROID__
TEST(AsyncIo, AncillaryMessageHandler) {
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();

  Own<ConnectionReceiver> listener;
  Own<AsyncIoStream> server;
  Own<AsyncIoStream> client;

  char receiveBuffer[4];

  bool clientHandlerCalled = false;
  kj::Function<void(kj::ArrayPtr<AncillaryMessage>)> clientHandler =
    [&](kj::ArrayPtr<AncillaryMessage>) {
    clientHandlerCalled = true;
  };
  bool serverHandlerCalled = false;
  kj::Function<void(kj::ArrayPtr<AncillaryMessage>)> serverHandler =
    [&](kj::ArrayPtr<AncillaryMessage> msgs) {
    serverHandlerCalled = true;
    EXPECT_EQ(1, msgs.size());
    EXPECT_EQ(SOL_SOCKET, msgs[0].getLevel());
    EXPECT_EQ(SO_TIMESTAMP, msgs[0].getType());
  };

  auto port = newPromiseAndFulfiller<uint>();

  port.promise.then([&](uint portnum) {
    return network.parseAddress("localhost", portnum);
  }).then([&](Own<NetworkAddress>&& addr) {
    auto promise = addr->connectAuthenticated();
    return promise.then([&,addr=kj::mv(addr)](AuthenticatedStream result) mutable {
      client = kj::mv(result.stream);
      client->registerAncillaryMessageHandler(kj::mv(clientHandler));
      return client->write("foo", 3);
    });
  }).detach([](kj::Exception&& exception) {
    KJ_FAIL_EXPECT(exception);
  });

  kj::String result = network.parseAddress("*").then([&](Own<NetworkAddress>&& result) {
    listener = result->listen();
    // Register interest in having the timestamp delivered via cmsg on each recvmsg.
    int yes = 1;
    listener->setsockopt(SOL_SOCKET, SO_TIMESTAMP, &yes, sizeof(yes));
    port.fulfiller->fulfill(listener->getPort());
    return listener->acceptAuthenticated();
  }).then([&](AuthenticatedStream result) {
    server = kj::mv(result.stream);
    server->registerAncillaryMessageHandler(kj::mv(serverHandler));
    return server->tryRead(receiveBuffer, 3, 4);
  }).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return heapString(receiveBuffer, n);
  }).wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
  EXPECT_FALSE(clientHandlerCalled);
  EXPECT_TRUE(serverHandlerCalled);
}
#endif

String tryParse(WaitScope& waitScope, Network& network, StringPtr text, uint portHint = 0) {
  return network.parseAddress(text, portHint).wait(waitScope)->toString();
}

bool systemSupportsAddress(StringPtr addr, StringPtr service = nullptr) {
  // Can getaddrinfo() parse this addresses? This is only true if the address family (e.g., ipv6)
  // is configured on at least one interface. (The loopback interface usually has both ipv4 and
  // ipv6 configured, but not always.)
  struct addrinfo hints;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = 0;
  hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
  hints.ai_protocol = 0;
  hints.ai_canonname = nullptr;
  hints.ai_addr = nullptr;
  hints.ai_next = nullptr;
  struct addrinfo* list;
  int status = getaddrinfo(
      addr.cStr(), service == nullptr ? nullptr : service.cStr(), &hints, &list);
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

TEST(AsyncIo, InMemoryCapabilityPipe) {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto pipe = newCapabilityPipe();
  auto pipe2 = newCapabilityPipe();
  char receiveBuffer1[4];
  char receiveBuffer2[4];

  // Expect to receive a stream, then read "foo" from it, then write "bar" to it.
  Own<AsyncCapabilityStream> receivedStream;
  auto promise = pipe2.ends[1]->receiveStream()
      .then([&](Own<AsyncCapabilityStream> stream) {
    receivedStream = kj::mv(stream);
    return receivedStream->tryRead(receiveBuffer2, 3, 4);
  }).then([&](size_t n) {
    EXPECT_EQ(3u, n);
    return receivedStream->write("bar", 3).then([&receiveBuffer2,n]() {
      return heapString(receiveBuffer2, n);
    });
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
  }).wait(waitScope);

  kj::String result2 = promise.wait(waitScope);

  EXPECT_EQ("bar", result);
  EXPECT_EQ("foo", result2);
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

TEST(AsyncIo, CapabilityPipeBlockedSendStream) {
  // Check for a bug that existed at one point where if a sendStream() call couldn't complete
  // immediately, it would fail.

  auto io = setupAsyncIo();

  auto pipe = io.provider->newCapabilityPipe();

  Promise<void> promise = nullptr;
  Own<AsyncIoStream> endpoint1;
  uint nonBlockedCount = 0;
  for (;;) {
    auto pipe2 = io.provider->newCapabilityPipe();
    promise = pipe.ends[0]->sendStream(kj::mv(pipe2.ends[0]));
    if (promise.poll(io.waitScope)) {
      // Send completed immediately, because there was enough space in the stream.
      ++nonBlockedCount;
      promise.wait(io.waitScope);
    } else {
      // Send blocked! Let's continue with this promise then!
      endpoint1 = kj::mv(pipe2.ends[1]);
      break;
    }
  }

  for (uint i KJ_UNUSED: kj::zeroTo(nonBlockedCount)) {
    // Receive and ignore all the streams that were sent without blocking.
    pipe.ends[1]->receiveStream().wait(io.waitScope);
  }

  // Now that write that blocked should have been able to complete.
  promise.wait(io.waitScope);

  // Now get the one that blocked.
  auto endpoint2 = pipe.ends[1]->receiveStream().wait(io.waitScope);

  endpoint1->write("foo", 3).wait(io.waitScope);
  endpoint1->shutdownWrite();
  KJ_EXPECT(endpoint2->readAllText().wait(io.waitScope) == "foo");
}

TEST(AsyncIo, CapabilityPipeMultiStreamMessage) {
  auto ioContext = setupAsyncIo();

  auto pipe = ioContext.provider->newCapabilityPipe();
  auto pipe2 = ioContext.provider->newCapabilityPipe();
  auto pipe3 = ioContext.provider->newCapabilityPipe();

  auto streams = heapArrayBuilder<Own<AsyncCapabilityStream>>(2);
  streams.add(kj::mv(pipe2.ends[0]));
  streams.add(kj::mv(pipe3.ends[0]));

  ArrayPtr<const byte> secondBuf = "bar"_kj.asBytes();
  pipe.ends[0]->writeWithStreams("foo"_kj.asBytes(), arrayPtr(&secondBuf, 1), streams.finish())
      .wait(ioContext.waitScope);

  char receiveBuffer[7];
  Own<AsyncCapabilityStream> receiveStreams[3];
  auto result = pipe.ends[1]->tryReadWithStreams(receiveBuffer, 6, 7, receiveStreams, 3)
      .wait(ioContext.waitScope);

  KJ_EXPECT(result.byteCount == 6);
  receiveBuffer[6] = '\0';
  KJ_EXPECT(kj::StringPtr(receiveBuffer) == "foobar");

  KJ_ASSERT(result.capCount == 2);

  receiveStreams[0]->write("baz", 3).wait(ioContext.waitScope);
  receiveStreams[0] = nullptr;
  KJ_EXPECT(pipe2.ends[1]->readAllText().wait(ioContext.waitScope) == "baz");

  pipe3.ends[1]->write("qux", 3).wait(ioContext.waitScope);
  pipe3.ends[1] = nullptr;
  KJ_EXPECT(receiveStreams[1]->readAllText().wait(ioContext.waitScope) == "qux");
}

TEST(AsyncIo, ScmRightsTruncatedOdd) {
  // Test that if we send two FDs over a unix socket, but the receiving end only receives one, we
  // don't leak the other FD.

  auto io = setupAsyncIo();

  auto capPipe = io.provider->newCapabilityPipe();

  int pipeFds[2];
  KJ_SYSCALL(miniposix::pipe(pipeFds));
  kj::AutoCloseFd in1(pipeFds[0]);
  kj::AutoCloseFd out1(pipeFds[1]);

  KJ_SYSCALL(miniposix::pipe(pipeFds));
  kj::AutoCloseFd in2(pipeFds[0]);
  kj::AutoCloseFd out2(pipeFds[1]);

  {
    AutoCloseFd sendFds[2] = { kj::mv(out1), kj::mv(out2) };
    capPipe.ends[0]->writeWithFds("foo"_kj.asBytes(), nullptr, sendFds).wait(io.waitScope);
  }

  {
    char buffer[4];
    AutoCloseFd fdBuffer[1];
    auto result = capPipe.ends[1]->tryReadWithFds(buffer, 3, 3, fdBuffer, 1).wait(io.waitScope);
    KJ_ASSERT(result.capCount == 1);
    kj::FdOutputStream(fdBuffer[0].get()).write("bar", 3);
  }

  // We want to carefully verify that out1 and out2 were closed, without deadlocking if they
  // weren't. So we manually set nonblocking mode and then issue read()s.
  KJ_SYSCALL(fcntl(in1, F_SETFL, O_NONBLOCK));
  KJ_SYSCALL(fcntl(in2, F_SETFL, O_NONBLOCK));

  char buffer[4];
  ssize_t n;

  // First we read "bar" from in1.
  KJ_NONBLOCKING_SYSCALL(n = read(in1, buffer, 4));
  KJ_ASSERT(n == 3);
  buffer[3] = '\0';
  KJ_ASSERT(kj::StringPtr(buffer) == "bar");

  // Now it should be EOF.
  KJ_NONBLOCKING_SYSCALL(n = read(in1, buffer, 4));
  if (n < 0) {
    KJ_FAIL_ASSERT("out1 was not closed");
  }
  KJ_ASSERT(n == 0);

  // Second pipe should have been closed implicitly because we didn't provide space to receive it.
  KJ_NONBLOCKING_SYSCALL(n = read(in2, buffer, 4));
  if (n < 0) {
    KJ_FAIL_ASSERT("out2 was not closed. This could indicate that your operating system kernel is "
        "buggy and leaks file descriptors when an SCM_RIGHTS message is truncated. FreeBSD was "
        "known to do this until late 2018, while MacOS still has this bug as of this writing in "
        "2019. However, KJ works around the problem on those platforms. You need to enable the "
        "same work-around for your OS -- search for 'SCM_RIGHTS' in src/kj/async-io-unix.c++.");
  }
  KJ_ASSERT(n == 0);
}

#if !__aarch64__
// This test fails under qemu-user, probably due to a bug in qemu's syscall emulation rather than
// a bug in the kernel. We don't have a good way to detect qemu so we just skip the test on aarch64
// in general.

TEST(AsyncIo, ScmRightsTruncatedEven) {
  // Test that if we send three FDs over a unix socket, but the receiving end only receives two, we
  // don't leak the third FD. This is different from the send-two-receive-one case in that
  // CMSG_SPACE() on many systems rounds up such that there is always space for an even number of
  // FDs. In that case the other test only verifies that our userspace code to close unwanted FDs
  // is correct, whereas *this* test really verifies that the *kernel* properly closes truncated
  // FDs.

  auto io = setupAsyncIo();

  auto capPipe = io.provider->newCapabilityPipe();

  int pipeFds[2];
  KJ_SYSCALL(miniposix::pipe(pipeFds));
  kj::AutoCloseFd in1(pipeFds[0]);
  kj::AutoCloseFd out1(pipeFds[1]);

  KJ_SYSCALL(miniposix::pipe(pipeFds));
  kj::AutoCloseFd in2(pipeFds[0]);
  kj::AutoCloseFd out2(pipeFds[1]);

  KJ_SYSCALL(miniposix::pipe(pipeFds));
  kj::AutoCloseFd in3(pipeFds[0]);
  kj::AutoCloseFd out3(pipeFds[1]);

  {
    AutoCloseFd sendFds[3] = { kj::mv(out1), kj::mv(out2), kj::mv(out3) };
    capPipe.ends[0]->writeWithFds("foo"_kj.asBytes(), nullptr, sendFds).wait(io.waitScope);
  }

  {
    char buffer[4];
    AutoCloseFd fdBuffer[2];
    auto result = capPipe.ends[1]->tryReadWithFds(buffer, 3, 3, fdBuffer, 2).wait(io.waitScope);
    KJ_ASSERT(result.capCount == 2);
    kj::FdOutputStream(fdBuffer[0].get()).write("bar", 3);
    kj::FdOutputStream(fdBuffer[1].get()).write("baz", 3);
  }

  // We want to carefully verify that out1, out2, and out3 were closed, without deadlocking if they
  // weren't. So we manually set nonblocking mode and then issue read()s.
  KJ_SYSCALL(fcntl(in1, F_SETFL, O_NONBLOCK));
  KJ_SYSCALL(fcntl(in2, F_SETFL, O_NONBLOCK));
  KJ_SYSCALL(fcntl(in3, F_SETFL, O_NONBLOCK));

  char buffer[4];
  ssize_t n;

  // First we read "bar" from in1.
  KJ_NONBLOCKING_SYSCALL(n = read(in1, buffer, 4));
  KJ_ASSERT(n == 3);
  buffer[3] = '\0';
  KJ_ASSERT(kj::StringPtr(buffer) == "bar");

  // Now it should be EOF.
  KJ_NONBLOCKING_SYSCALL(n = read(in1, buffer, 4));
  if (n < 0) {
    KJ_FAIL_ASSERT("out1 was not closed");
  }
  KJ_ASSERT(n == 0);

  // Next we read "baz" from in2.
  KJ_NONBLOCKING_SYSCALL(n = read(in2, buffer, 4));
  KJ_ASSERT(n == 3);
  buffer[3] = '\0';
  KJ_ASSERT(kj::StringPtr(buffer) == "baz");

  // Now it should be EOF.
  KJ_NONBLOCKING_SYSCALL(n = read(in2, buffer, 4));
  if (n < 0) {
    KJ_FAIL_ASSERT("out2 was not closed");
  }
  KJ_ASSERT(n == 0);

  // Third pipe should have been closed implicitly because we didn't provide space to receive it.
  KJ_NONBLOCKING_SYSCALL(n = read(in3, buffer, 4));
  if (n < 0) {
    KJ_FAIL_ASSERT("out3 was not closed. This could indicate that your operating system kernel is "
        "buggy and leaks file descriptors when an SCM_RIGHTS message is truncated. FreeBSD was "
        "known to do this until late 2018, while MacOS still has this bug as of this writing in "
        "2019. However, KJ works around the problem on those platforms. You need to enable the "
        "same work-around for your OS -- search for 'SCM_RIGHTS' in src/kj/async-io-unix.c++.");
  }
  KJ_ASSERT(n == 0);
}

#endif  // !__aarch64__

#endif  // !_WIN32 && !__CYGWIN__

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

#if __APPLE__
// On MacOS, `CMSG_SPACE(0)` triggers a bogus warning.
#pragma GCC diagnostic ignored "-Wnull-pointer-arithmetic"
#endif
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
  auto elapsedSinceEpoch = systemPreciseMonotonicClock().now() - kj::origin<TimePoint>();
  auto address = kj::str("unix-abstract:foo", getpid(), elapsedSinceEpoch / kj::NANOSECONDS);

  Own<NetworkAddress> addr = network.parseAddress(address).wait(ioContext.waitScope);

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
  KJ_EXPECT(CidrRange("1.2.3.4/16").toString() == "1.2.0.0/16");
  KJ_EXPECT(CidrRange("1.2.255.4/18").toString() == "1.2.192.0/18");
  KJ_EXPECT(CidrRange("1234::abcd:ffff:ffff/98").toString() == "1234::abcd:c000:0/98");

  KJ_EXPECT(CidrRange::inet4({1,2,255,4}, 18).toString() == "1.2.192.0/18");
  KJ_EXPECT(CidrRange::inet6({0x1234, 0x5678}, {0xabcd, 0xffff, 0xffff}, 98).toString() ==
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
    KJ_EXPECT(CidrRange("1.2.255.255/18").matches(&addr));
    KJ_EXPECT(!CidrRange("1.2.255.255/19").matches(&addr));
    KJ_EXPECT(CidrRange("1.2.0.0/16").matches(&addr));
    KJ_EXPECT(!CidrRange("1.3.0.0/16").matches(&addr));
    KJ_EXPECT(CidrRange("1.2.223.255/32").matches(&addr));
    KJ_EXPECT(CidrRange("0.0.0.0/0").matches(&addr));
    KJ_EXPECT(!CidrRange("::/0").matches(&addr));
  }

  {
    addr4.sin_family = AF_INET6;
    byte bytes[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    memcpy(addr6.sin6_addr.s6_addr, bytes, 16);
    KJ_EXPECT(CidrRange("0102:03ff::/24").matches(&addr));
    KJ_EXPECT(!CidrRange("0102:02ff::/24").matches(&addr));
    KJ_EXPECT(CidrRange("0102:02ff::/23").matches(&addr));
    KJ_EXPECT(CidrRange("0102:0304:0506:0708:090a:0b0c:0d0e:0f10/128").matches(&addr));
    KJ_EXPECT(CidrRange("::/0").matches(&addr));
    KJ_EXPECT(!CidrRange("0.0.0.0/0").matches(&addr));
  }

  {
    addr4.sin_family = AF_INET6;
    inet_pton(AF_INET6, "::ffff:1.2.223.255", &addr6.sin6_addr);
    KJ_EXPECT(CidrRange("1.2.255.255/18").matches(&addr));
    KJ_EXPECT(!CidrRange("1.2.255.255/19").matches(&addr));
    KJ_EXPECT(CidrRange("1.2.0.0/16").matches(&addr));
    KJ_EXPECT(!CidrRange("1.3.0.0/16").matches(&addr));
    KJ_EXPECT(CidrRange("1.2.223.255/32").matches(&addr));
    KJ_EXPECT(CidrRange("0.0.0.0/0").matches(&addr));
    KJ_EXPECT(CidrRange("::/0").matches(&addr));
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

  // Test combinations of public/private/network/local. At one point these were buggy.
  {
    _::NetworkFilter filter({"public", "private"}, {}, base);

    KJ_EXPECT(allowed4(filter, "8.8.8.8"));
    KJ_EXPECT(!allowed4(filter, "240.1.2.3"));

    KJ_EXPECT(allowed4(filter, "192.168.0.1"));
    KJ_EXPECT(allowed4(filter, "10.1.2.3"));
    KJ_EXPECT(allowed4(filter, "127.0.0.1"));
    KJ_EXPECT(allowed4(filter, "0.0.0.0"));

    KJ_EXPECT(allowed6(filter, "2400:cb00:2048:1::c629:d7a2"));
    KJ_EXPECT(allowed6(filter, "fc00::1234"));
    KJ_EXPECT(allowed6(filter, "::1"));
    KJ_EXPECT(allowed6(filter, "::"));
  }

  {
    _::NetworkFilter filter({"network", "local"}, {}, base);

    KJ_EXPECT(allowed4(filter, "8.8.8.8"));
    KJ_EXPECT(!allowed4(filter, "240.1.2.3"));

    KJ_EXPECT(allowed4(filter, "192.168.0.1"));
    KJ_EXPECT(allowed4(filter, "10.1.2.3"));
    KJ_EXPECT(allowed4(filter, "127.0.0.1"));
    KJ_EXPECT(allowed4(filter, "0.0.0.0"));

    KJ_EXPECT(allowed6(filter, "2400:cb00:2048:1::c629:d7a2"));
    KJ_EXPECT(allowed6(filter, "fc00::1234"));
    KJ_EXPECT(allowed6(filter, "::1"));
    KJ_EXPECT(allowed6(filter, "::"));
  }

  {
    _::NetworkFilter filter({"public", "local"}, {}, base);

    KJ_EXPECT(allowed4(filter, "8.8.8.8"));
    KJ_EXPECT(!allowed4(filter, "240.1.2.3"));

    KJ_EXPECT(!allowed4(filter, "192.168.0.1"));
    KJ_EXPECT(!allowed4(filter, "10.1.2.3"));
    KJ_EXPECT(allowed4(filter, "127.0.0.1"));
    KJ_EXPECT(allowed4(filter, "0.0.0.0"));

    KJ_EXPECT(allowed6(filter, "2400:cb00:2048:1::c629:d7a2"));
    KJ_EXPECT(!allowed6(filter, "fc00::1234"));
    KJ_EXPECT(allowed6(filter, "::1"));
    KJ_EXPECT(allowed6(filter, "::"));
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
  return promise.then([&in,expected,buffer=kj::mv(buffer)](size_t amount) {
    if (amount == 0) {
      KJ_FAIL_ASSERT("expected data never sent", expected);
    }

    auto actual = buffer.slice(0, amount);
    if (memcmp(actual.begin(), expected.begin(), actual.size()) != 0) {
      KJ_FAIL_ASSERT("data from stream doesn't match expected", expected, actual);
    }

    return expectRead(in, expected.slice(amount));
  });
}

class MockAsyncInputStream final: public AsyncInputStream {
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
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
      "abortRead() has been called", pipe.out->write("baz", 3).wait(ws));
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
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
      "abortRead() has been called", pipe.out->write("baz", 3).wait(ws));
}

KJ_TEST("Userland pipe pump into zero-limited pipe, no data to pump") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe(uint64_t(0));
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  expectRead(*pipe2.in, "");
  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 0);
}

KJ_TEST("Userland pipe pump into zero-limited pipe, data is pumped") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe(uint64_t(0));
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  expectRead(*pipe2.in, "");
  auto writePromise = pipe.out->write("foo", 3);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("abortRead() has been called", pumpPromise.wait(ws));
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

KJ_TEST("Userland pipe EOF fulfills pumpFrom promise") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  auto writePromise = pipe.out->write("foobar", 6);
  KJ_EXPECT(!writePromise.poll(ws));
  auto pipe3 = newOneWayPipe();
  auto pumpPromise2 = pipe2.in->pumpTo(*pipe3.out);
  KJ_EXPECT(!pumpPromise2.poll(ws));
  expectRead(*pipe3.in, "foobar").wait(ws);
  writePromise.wait(ws);

  KJ_EXPECT(!pumpPromise.poll(ws));
  pipe.out = nullptr;
  KJ_EXPECT(pumpPromise.wait(ws) == 6);

  KJ_EXPECT(!pumpPromise2.poll(ws));
  pipe2.out = nullptr;
  KJ_EXPECT(pumpPromise2.wait(ws) == 6);
}

KJ_TEST("Userland pipe tryPumpFrom to pumpTo for same amount fulfills simultaneously") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in, 6));

  auto writePromise = pipe.out->write("foobar", 6);
  KJ_EXPECT(!writePromise.poll(ws));
  auto pipe3 = newOneWayPipe();
  auto pumpPromise2 = pipe2.in->pumpTo(*pipe3.out, 6);
  KJ_EXPECT(!pumpPromise2.poll(ws));
  expectRead(*pipe3.in, "foobar").wait(ws);
  writePromise.wait(ws);

  KJ_EXPECT(pumpPromise.wait(ws) == 6);
  KJ_EXPECT(pumpPromise2.wait(ws) == 6);
}

KJ_TEST("Userland pipe multi-part write doesn't quit early") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  auto readPromise = expectRead(*pipe.in, "foo");

  kj::ArrayPtr<const byte> pieces[2] = { "foobar"_kj.asBytes(), "baz"_kj.asBytes() };
  auto writePromise = pipe.out->write(pieces);

  readPromise.wait(ws);
  KJ_EXPECT(!writePromise.poll(ws));
  expectRead(*pipe.in, "bar").wait(ws);
  KJ_EXPECT(!writePromise.poll(ws));
  expectRead(*pipe.in, "baz").wait(ws);
  writePromise.wait(ws);
}

KJ_TEST("Userland pipe BlockedRead gets empty tryPumpFrom") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto pipe2 = newOneWayPipe();

  // First start a read from the back end.
  char buffer[4];
  auto readPromise = pipe2.in->tryRead(buffer, 1, 4);

  // Now arrange a pump between the pipes, using tryPumpFrom().
  auto pumpPromise = KJ_ASSERT_NONNULL(pipe2.out->tryPumpFrom(*pipe.in));

  // Disconnect the front pipe, causing EOF on the pump.
  pipe.out = nullptr;

  // The pump should have produced zero bytes.
  KJ_EXPECT(pumpPromise.wait(ws) == 0);

  // The read is incomplete.
  KJ_EXPECT(!readPromise.poll(ws));

  // A subsequent write() completes the read.
  pipe2.out->write("foo", 3).wait(ws);
  KJ_EXPECT(readPromise.wait(ws) == 3);
  buffer[3] = '\0';
  KJ_EXPECT(kj::StringPtr(buffer, 3) == "foo");
}

constexpr static auto TEE_MAX_CHUNK_SIZE = 1 << 14;
// AsyncTee::MAX_CHUNK_SIZE, 16k as of this writing

KJ_TEST("Userland tee") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto tee = newTee(kj::mv(pipe.in));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  auto writePromise = pipe.out->write("foobar", 6);

  expectRead(*left, "foobar").wait(ws);
  writePromise.wait(ws);
  expectRead(*right, "foobar").wait(ws);
}

KJ_TEST("Userland nested tee") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto tee = newTee(kj::mv(pipe.in));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  auto tee2 = newTee(kj::mv(right));
  auto rightLeft = kj::mv(tee2.branches[0]);
  auto rightRight = kj::mv(tee2.branches[1]);

  auto writePromise = pipe.out->write("foobar", 6);

  expectRead(*left, "foobar").wait(ws);
  writePromise.wait(ws);
  expectRead(*rightLeft, "foobar").wait(ws);
  expectRead(*rightRight, "foo").wait(ws);

  auto tee3 = newTee(kj::mv(rightRight));
  auto rightRightLeft = kj::mv(tee3.branches[0]);
  auto rightRightRight = kj::mv(tee3.branches[1]);
  expectRead(*rightRightLeft, "bar").wait(ws);
  expectRead(*rightRightRight, "b").wait(ws);

  auto tee4 = newTee(kj::mv(rightRightRight));
  auto rightRightRightLeft = kj::mv(tee4.branches[0]);
  auto rightRightRightRight = kj::mv(tee4.branches[1]);
  expectRead(*rightRightRightLeft, "ar").wait(ws);
  expectRead(*rightRightRightRight, "ar").wait(ws);
}

KJ_TEST("Userland tee concurrent read") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto tee = newTee(kj::mv(pipe.in));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  uint8_t leftBuf[6] = { 0 };
  uint8_t rightBuf[6] = { 0 };
  auto leftPromise = left->tryRead(leftBuf, 6, 6);
  auto rightPromise = right->tryRead(rightBuf, 6, 6);
  KJ_EXPECT(!leftPromise.poll(ws));
  KJ_EXPECT(!rightPromise.poll(ws));

  pipe.out->write("foobar", 6).wait(ws);

  KJ_EXPECT(leftPromise.wait(ws) == 6);
  KJ_EXPECT(rightPromise.wait(ws) == 6);

  KJ_EXPECT(memcmp(leftBuf, "foobar", 6) == 0);
  KJ_EXPECT(memcmp(leftBuf, "foobar", 6) == 0);
}

KJ_TEST("Userland tee cancel and restart read") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto tee = newTee(kj::mv(pipe.in));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  auto writePromise = pipe.out->write("foobar", 6);

  {
    // Initiate a read and immediately cancel it.
    uint8_t buf[6] = { 0 };
    auto promise = left->tryRead(buf, 6, 6);
  }

  // Subsequent reads still see the full data.
  expectRead(*left, "foobar").wait(ws);
  writePromise.wait(ws);
  expectRead(*right, "foobar").wait(ws);
}

KJ_TEST("Userland tee cancel read and destroy branch then read other branch") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto tee = newTee(kj::mv(pipe.in));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  auto writePromise = pipe.out->write("foobar", 6);

  {
    // Initiate a read and immediately cancel it.
    uint8_t buf[6] = { 0 };
    auto promise = left->tryRead(buf, 6, 6);
  }

  // And destroy the branch for good measure.
  left = nullptr;

  // Subsequent reads on the other branch still see the full data.
  expectRead(*right, "foobar").wait(ws);
  writePromise.wait(ws);
}

KJ_TEST("Userland tee subsequent other-branch reads are READY_NOW") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto tee = newTee(kj::mv(pipe.in));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  uint8_t leftBuf[6] = { 0 };
  auto leftPromise = left->tryRead(leftBuf, 6, 6);
  // This is the first read, so there should NOT be buffered data.
  KJ_EXPECT(!leftPromise.poll(ws));
  pipe.out->write("foobar", 6).wait(ws);
  leftPromise.wait(ws);
  KJ_EXPECT(memcmp(leftBuf, "foobar", 6) == 0);

  uint8_t rightBuf[6] = { 0 };
  auto rightPromise = right->tryRead(rightBuf, 6, 6);
  // The left read promise was fulfilled, so there SHOULD be buffered data.
  KJ_EXPECT(rightPromise.poll(ws));
  rightPromise.wait(ws);
  KJ_EXPECT(memcmp(rightBuf, "foobar", 6) == 0);
}

KJ_TEST("Userland tee read EOF propagation") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();
  auto writePromise = pipe.out->write("foobar", 6);
  auto tee = newTee(mv(pipe.in));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  // Lengthless pipe, so ...
  KJ_EXPECT(left->tryGetLength() == nullptr);
  KJ_EXPECT(right->tryGetLength() == nullptr);

  uint8_t leftBuf[7] = { 0 };
  auto leftPromise = left->tryRead(leftBuf, size(leftBuf), size(leftBuf));
  writePromise.wait(ws);
  // Destroying the output side should force a short read.
  pipe.out = nullptr;

  KJ_EXPECT(leftPromise.wait(ws) == 6);
  KJ_EXPECT(memcmp(leftBuf, "foobar", 6) == 0);

  // And we should see a short read here, too.
  uint8_t rightBuf[7] = { 0 };
  auto rightPromise = right->tryRead(rightBuf, size(rightBuf), size(rightBuf));
  KJ_EXPECT(rightPromise.wait(ws) == 6);
  KJ_EXPECT(memcmp(rightBuf, "foobar", 6) == 0);

  // Further reads should all be short.
  KJ_EXPECT(left->tryRead(leftBuf, 1, size(leftBuf)).wait(ws) == 0);
  KJ_EXPECT(right->tryRead(rightBuf, 1, size(rightBuf)).wait(ws) == 0);
}

KJ_TEST("Userland tee read exception propagation") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  // Make a pipe expecting to read more than we're actually going to write. This will force a "pipe
  // ended prematurely" exception when we destroy the output side early.
  auto pipe = newOneWayPipe(7);
  auto writePromise = pipe.out->write("foobar", 6);
  auto tee = newTee(mv(pipe.in));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  // Test tryGetLength() while we're at it.
  KJ_EXPECT(KJ_ASSERT_NONNULL(left->tryGetLength()) == 7);
  KJ_EXPECT(KJ_ASSERT_NONNULL(right->tryGetLength()) == 7);

  uint8_t leftBuf[7] = { 0 };
  auto leftPromise = left->tryRead(leftBuf, 6, size(leftBuf));
  writePromise.wait(ws);
  // Destroying the output side should force a fulfillment of the read (since we reached minBytes).
  pipe.out = nullptr;
  KJ_EXPECT(leftPromise.wait(ws) == 6);
  KJ_EXPECT(memcmp(leftBuf, "foobar", 6) == 0);

  // The next read sees the exception.
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("pipe ended prematurely",
      left->tryRead(leftBuf, 1, size(leftBuf)).ignoreResult().wait(ws));

  // Test tryGetLength() here -- the unread branch still sees the original length value.
  KJ_EXPECT(KJ_ASSERT_NONNULL(left->tryGetLength()) == 1);
  KJ_EXPECT(KJ_ASSERT_NONNULL(right->tryGetLength()) == 7);

  // We should see the buffered data on the other side, even though we don't reach our minBytes.
  uint8_t rightBuf[7] = { 0 };
  auto rightPromise = right->tryRead(rightBuf, size(rightBuf), size(rightBuf));
  KJ_EXPECT(rightPromise.wait(ws) == 6);
  KJ_EXPECT(memcmp(rightBuf, "foobar", 6) == 0);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("pipe ended prematurely",
      right->tryRead(rightBuf, 1, size(leftBuf)).ignoreResult().wait(ws));

  // Further reads should all see the exception again.
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("pipe ended prematurely",
      left->tryRead(leftBuf, 1, size(leftBuf)).ignoreResult().wait(ws));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("pipe ended prematurely",
      right->tryRead(rightBuf, 1, size(leftBuf)).ignoreResult().wait(ws));
}

KJ_TEST("Userland tee read exception propagation w/ data loss") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  // Make a pipe expecting to read more than we're actually going to write. This will force a "pipe
  // ended prematurely" exception once the pipe sees a short read.
  auto pipe = newOneWayPipe(7);
  auto writePromise = pipe.out->write("foobar", 6);
  auto tee = newTee(mv(pipe.in));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  uint8_t leftBuf[7] = { 0 };
  auto leftPromise = left->tryRead(leftBuf, 7, 7);
  writePromise.wait(ws);
  // Destroying the output side should force an exception, since we didn't reach our minBytes.
  pipe.out = nullptr;
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
      "pipe ended prematurely", leftPromise.ignoreResult().wait(ws));

  // And we should see a short read here, too. In fact, we shouldn't see anything: the short read
  // above read all of the pipe's data, but then failed to buffer it because it encountered an
  // exception. It buffered the exception, instead.
  uint8_t rightBuf[7] = { 0 };
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("pipe ended prematurely",
      right->tryRead(rightBuf, 1, 1).ignoreResult().wait(ws));
}

KJ_TEST("Userland tee read into different buffer sizes") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto tee = newTee(heap<MockAsyncInputStream>("foo bar baz"_kj.asBytes(), 11));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  uint8_t leftBuf[5] = { 0 };
  uint8_t rightBuf[11] = { 0 };

  auto leftPromise = left->tryRead(leftBuf, 5, 5);
  auto rightPromise = right->tryRead(rightBuf, 11, 11);

  KJ_EXPECT(leftPromise.wait(ws) == 5);
  KJ_EXPECT(rightPromise.wait(ws) == 11);
}

KJ_TEST("Userland tee reads see max(minBytes...) and min(maxBytes...)") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto tee = newTee(heap<MockAsyncInputStream>("foo bar baz"_kj.asBytes(), 11));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  {
    uint8_t leftBuf[5] = { 0 };
    uint8_t rightBuf[11] = { 0 };

    // Subrange of another range. The smaller maxBytes should win.
    auto leftPromise = left->tryRead(leftBuf, 3, 5);
    auto rightPromise = right->tryRead(rightBuf, 1, 11);

    KJ_EXPECT(leftPromise.wait(ws) == 5);
    KJ_EXPECT(rightPromise.wait(ws) == 5);
  }

  {
    uint8_t leftBuf[5] = { 0 };
    uint8_t rightBuf[11] = { 0 };

    // Disjoint ranges. The larger minBytes should win.
    auto leftPromise = left->tryRead(leftBuf, 3, 5);
    auto rightPromise = right->tryRead(rightBuf, 6, 11);

    KJ_EXPECT(leftPromise.wait(ws) == 5);
    KJ_EXPECT(rightPromise.wait(ws) == 6);

    KJ_EXPECT(left->tryRead(leftBuf, 1, 2).wait(ws) == 1);
  }
}

KJ_TEST("Userland tee read stress test") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto bigText = strArray(kj::repeat("foo bar baz"_kj, 12345), ",");

  auto tee = newTee(heap<MockAsyncInputStream>(bigText.asBytes(), bigText.size()));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  auto leftBuffer = heapArray<byte>(bigText.size());

  {
    auto leftSlice = leftBuffer.slice(0, leftBuffer.size());
    while (leftSlice.size() > 0) {
      for (size_t blockSize: { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59 }) {
        if (leftSlice.size() == 0) break;
        auto maxBytes = min(blockSize, leftSlice.size());
        auto amount = left->tryRead(leftSlice.begin(), 1, maxBytes).wait(ws);
        leftSlice = leftSlice.slice(amount, leftSlice.size());
      }
    }
  }

  KJ_EXPECT(memcmp(leftBuffer.begin(), bigText.begin(), leftBuffer.size()) == 0);
  KJ_EXPECT(right->readAllText().wait(ws) == bigText);
}

KJ_TEST("Userland tee pump") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto bigText = strArray(kj::repeat("foo bar baz"_kj, 12345), ",");

  auto tee = newTee(heap<MockAsyncInputStream>(bigText.asBytes(), bigText.size()));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  auto leftPipe = newOneWayPipe();
  auto rightPipe = newOneWayPipe();

  auto leftPumpPromise = left->pumpTo(*leftPipe.out, 7);
  KJ_EXPECT(!leftPumpPromise.poll(ws));

  auto rightPumpPromise = right->pumpTo(*rightPipe.out);
  // Neither are ready yet, because the left pump's backpressure has blocked the AsyncTee's pull
  // loop until we read from leftPipe.
  KJ_EXPECT(!leftPumpPromise.poll(ws));
  KJ_EXPECT(!rightPumpPromise.poll(ws));

  expectRead(*leftPipe.in, "foo bar").wait(ws);
  KJ_EXPECT(leftPumpPromise.wait(ws) == 7);
  KJ_EXPECT(!rightPumpPromise.poll(ws));

  // We should be able to read up to how far the left side pumped, and beyond. The left side will
  // now have data in its buffer.
  expectRead(*rightPipe.in, "foo bar baz,foo bar baz,foo").wait(ws);

  // Consume the left side buffer.
  expectRead(*left, " baz,foo bar").wait(ws);

  // We can destroy the left branch entirely and the right branch will still see all data.
  left = nullptr;
  KJ_EXPECT(!rightPumpPromise.poll(ws));
  auto allTextPromise = rightPipe.in->readAllText();
  KJ_EXPECT(rightPumpPromise.wait(ws) == bigText.size());
  // Need to force an EOF in the right pipe to check the result.
  rightPipe.out = nullptr;
  KJ_EXPECT(allTextPromise.wait(ws) == bigText.slice(27));
}

KJ_TEST("Userland tee pump slows down reads") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto bigText = strArray(kj::repeat("foo bar baz"_kj, 12345), ",");

  auto tee = newTee(heap<MockAsyncInputStream>(bigText.asBytes(), bigText.size()));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  auto leftPipe = newOneWayPipe();
  auto leftPumpPromise = left->pumpTo(*leftPipe.out);
  KJ_EXPECT(!leftPumpPromise.poll(ws));

  // The left pump will cause some data to be buffered on the right branch, which we can read.
  auto rightExpectation0 = kj::str(bigText.slice(0, TEE_MAX_CHUNK_SIZE));
  expectRead(*right, rightExpectation0).wait(ws);

  // But the next right branch read is blocked by the left pipe's backpressure.
  auto rightExpectation1 = kj::str(bigText.slice(TEE_MAX_CHUNK_SIZE, TEE_MAX_CHUNK_SIZE + 10));
  auto rightPromise = expectRead(*right, rightExpectation1);
  KJ_EXPECT(!rightPromise.poll(ws));

  // The right branch read finishes when we relieve the pressure in the left pipe.
  auto allTextPromise = leftPipe.in->readAllText();
  rightPromise.wait(ws);
  KJ_EXPECT(leftPumpPromise.wait(ws) == bigText.size());
  leftPipe.out = nullptr;
  KJ_EXPECT(allTextPromise.wait(ws) == bigText);
}

KJ_TEST("Userland tee pump EOF propagation") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  {
    // EOF encountered by two pump operations.
    auto pipe = newOneWayPipe();
    auto writePromise = pipe.out->write("foo bar", 7);
    auto tee = newTee(mv(pipe.in));
    auto left = kj::mv(tee.branches[0]);
    auto right = kj::mv(tee.branches[1]);

    auto leftPipe = newOneWayPipe();
    auto rightPipe = newOneWayPipe();

    // Pump the first bit, and block.

    auto leftPumpPromise = left->pumpTo(*leftPipe.out);
    KJ_EXPECT(!leftPumpPromise.poll(ws));
    auto rightPumpPromise = right->pumpTo(*rightPipe.out);
    writePromise.wait(ws);
    KJ_EXPECT(!leftPumpPromise.poll(ws));
    KJ_EXPECT(!rightPumpPromise.poll(ws));

    // Induce an EOF. We should see it propagated to both pump promises.

    pipe.out = nullptr;

    // Relieve backpressure.
    auto leftAllPromise = leftPipe.in->readAllText();
    auto rightAllPromise = rightPipe.in->readAllText();
    KJ_EXPECT(leftPumpPromise.wait(ws) == 7);
    KJ_EXPECT(rightPumpPromise.wait(ws) == 7);

    // Make sure we got the data on the pipes that were being pumped to.
    KJ_EXPECT(!leftAllPromise.poll(ws));
    KJ_EXPECT(!rightAllPromise.poll(ws));
    leftPipe.out = nullptr;
    rightPipe.out = nullptr;
    KJ_EXPECT(leftAllPromise.wait(ws) == "foo bar");
    KJ_EXPECT(rightAllPromise.wait(ws) == "foo bar");
  }

  {
    // EOF encountered by a read and pump operation.
    auto pipe = newOneWayPipe();
    auto writePromise = pipe.out->write("foo bar", 7);
    auto tee = newTee(mv(pipe.in));
    auto left = kj::mv(tee.branches[0]);
    auto right = kj::mv(tee.branches[1]);

    auto leftPipe = newOneWayPipe();
    auto rightPipe = newOneWayPipe();

    // Pump one branch, read another.

    auto leftPumpPromise = left->pumpTo(*leftPipe.out);
    KJ_EXPECT(!leftPumpPromise.poll(ws));
    expectRead(*right, "foo bar").wait(ws);
    writePromise.wait(ws);
    uint8_t dummy = 0;
    auto rightReadPromise = right->tryRead(&dummy, 1, 1);

    // Induce an EOF. We should see it propagated to both the read and pump promises.

    pipe.out = nullptr;

    // Relieve backpressure in the tee to see the EOF.
    auto leftAllPromise = leftPipe.in->readAllText();
    KJ_EXPECT(leftPumpPromise.wait(ws) == 7);
    KJ_EXPECT(rightReadPromise.wait(ws) == 0);

    // Make sure we got the data on the pipe that was being pumped to.
    KJ_EXPECT(!leftAllPromise.poll(ws));
    leftPipe.out = nullptr;
    KJ_EXPECT(leftAllPromise.wait(ws) == "foo bar");
  }
}

KJ_TEST("Userland tee pump EOF on chunk boundary") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto bigText = strArray(kj::repeat("foo bar baz"_kj, 12345), ",");

  // Conjure an EOF right on the boundary of the tee's internal chunk.
  auto chunkText = kj::str(bigText.slice(0, TEE_MAX_CHUNK_SIZE));
  auto tee = newTee(heap<MockAsyncInputStream>(chunkText.asBytes(), chunkText.size()));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  auto leftPipe = newOneWayPipe();
  auto rightPipe = newOneWayPipe();

  auto leftPumpPromise = left->pumpTo(*leftPipe.out);
  auto rightPumpPromise = right->pumpTo(*rightPipe.out);
  KJ_EXPECT(!leftPumpPromise.poll(ws));
  KJ_EXPECT(!rightPumpPromise.poll(ws));

  auto leftAllPromise = leftPipe.in->readAllText();
  auto rightAllPromise = rightPipe.in->readAllText();

  // The pumps should see the EOF and stop.
  KJ_EXPECT(leftPumpPromise.wait(ws) == TEE_MAX_CHUNK_SIZE);
  KJ_EXPECT(rightPumpPromise.wait(ws) == TEE_MAX_CHUNK_SIZE);

  // Verify that we saw the data on the other end of the destination pipes.
  leftPipe.out = nullptr;
  rightPipe.out = nullptr;
  KJ_EXPECT(leftAllPromise.wait(ws) == chunkText);
  KJ_EXPECT(rightAllPromise.wait(ws) == chunkText);
}

KJ_TEST("Userland tee pump read exception propagation") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  {
    // Exception encountered by two pump operations.
    auto pipe = newOneWayPipe(14);
    auto writePromise = pipe.out->write("foo bar", 7);
    auto tee = newTee(mv(pipe.in));
    auto left = kj::mv(tee.branches[0]);
    auto right = kj::mv(tee.branches[1]);

    auto leftPipe = newOneWayPipe();
    auto rightPipe = newOneWayPipe();

    // Pump the first bit, and block.

    auto leftPumpPromise = left->pumpTo(*leftPipe.out);
    KJ_EXPECT(!leftPumpPromise.poll(ws));
    auto rightPumpPromise = right->pumpTo(*rightPipe.out);
    writePromise.wait(ws);
    KJ_EXPECT(!leftPumpPromise.poll(ws));
    KJ_EXPECT(!rightPumpPromise.poll(ws));

    // Induce a read exception. We should see it propagated to both pump promises.

    pipe.out = nullptr;

    // Both promises must exist before the backpressure in the tee is relieved, and the tee pull
    // loop actually sees the exception.
    auto leftAllPromise = leftPipe.in->readAllText();
    auto rightAllPromise = rightPipe.in->readAllText();
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
        "pipe ended prematurely", leftPumpPromise.ignoreResult().wait(ws));
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
        "pipe ended prematurely", rightPumpPromise.ignoreResult().wait(ws));

    // Make sure we got the data on the destination pipes.
    KJ_EXPECT(!leftAllPromise.poll(ws));
    KJ_EXPECT(!rightAllPromise.poll(ws));
    leftPipe.out = nullptr;
    rightPipe.out = nullptr;
    KJ_EXPECT(leftAllPromise.wait(ws) == "foo bar");
    KJ_EXPECT(rightAllPromise.wait(ws) == "foo bar");
  }

  {
    // Exception encountered by a read and pump operation.
    auto pipe = newOneWayPipe(14);
    auto writePromise = pipe.out->write("foo bar", 7);
    auto tee = newTee(mv(pipe.in));
    auto left = kj::mv(tee.branches[0]);
    auto right = kj::mv(tee.branches[1]);

    auto leftPipe = newOneWayPipe();
    auto rightPipe = newOneWayPipe();

    // Pump one branch, read another.

    auto leftPumpPromise = left->pumpTo(*leftPipe.out);
    KJ_EXPECT(!leftPumpPromise.poll(ws));
    expectRead(*right, "foo bar").wait(ws);
    writePromise.wait(ws);
    uint8_t dummy = 0;
    auto rightReadPromise = right->tryRead(&dummy, 1, 1);

    // Induce a read exception. We should see it propagated to both the read and pump promises.

    pipe.out = nullptr;

    // Relieve backpressure in the tee to see the exceptions.
    auto leftAllPromise = leftPipe.in->readAllText();
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
        "pipe ended prematurely", leftPumpPromise.ignoreResult().wait(ws));
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
        "pipe ended prematurely", rightReadPromise.ignoreResult().wait(ws));

    // Make sure we got the data on the destination pipe.
    KJ_EXPECT(!leftAllPromise.poll(ws));
    leftPipe.out = nullptr;
    KJ_EXPECT(leftAllPromise.wait(ws) == "foo bar");
  }
}

KJ_TEST("Userland tee pump write exception propagation") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto bigText = strArray(kj::repeat("foo bar baz"_kj, 12345), ",");

  auto tee = newTee(heap<MockAsyncInputStream>(bigText.asBytes(), bigText.size()));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  // Set up two pumps and let them block.
  auto leftPipe = newOneWayPipe();
  auto rightPipe = newOneWayPipe();
  auto leftPumpPromise = left->pumpTo(*leftPipe.out);
  auto rightPumpPromise = right->pumpTo(*rightPipe.out);
  KJ_EXPECT(!leftPumpPromise.poll(ws));
  KJ_EXPECT(!rightPumpPromise.poll(ws));

  // Induce a write exception in the right branch pump. It should propagate to the right pump
  // promise.
  rightPipe.in = nullptr;
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE(
      "read end of pipe was aborted", rightPumpPromise.ignoreResult().wait(ws));

  // The left pump promise does not see the right branch's write exception.
  KJ_EXPECT(!leftPumpPromise.poll(ws));
  auto allTextPromise = leftPipe.in->readAllText();
  KJ_EXPECT(leftPumpPromise.wait(ws) == bigText.size());
  leftPipe.out = nullptr;
  KJ_EXPECT(allTextPromise.wait(ws) == bigText);
}

KJ_TEST("Userland tee pump cancellation implies write cancellation") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto text = "foo bar baz"_kj;

  auto tee = newTee(heap<MockAsyncInputStream>(text.asBytes(), text.size()));
  auto left = kj::mv(tee.branches[0]);
  auto right = kj::mv(tee.branches[1]);

  auto leftPipe = newOneWayPipe();
  auto leftPumpPromise = left->pumpTo(*leftPipe.out);

  // Arrange to block the left pump on its write operation.
  expectRead(*right, "foo ").wait(ws);
  KJ_EXPECT(!leftPumpPromise.poll(ws));

  // Then cancel the pump, while it's still blocked.
  leftPumpPromise = nullptr;
  // It should cancel its write operations, so it should now be safe to destroy the output stream to
  // which it was pumping.
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    leftPipe.out = nullptr;
  })) {
    KJ_FAIL_EXPECT("write promises were not canceled", *exception);
  }
}

KJ_TEST("Userland tee buffer size limit") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto text = "foo bar baz"_kj;

  {
    // We can carefully read data to stay under our ridiculously low limit.

    auto tee = newTee(heap<MockAsyncInputStream>(text.asBytes(), text.size()), 2);
    auto left = kj::mv(tee.branches[0]);
    auto right = kj::mv(tee.branches[1]);

    expectRead(*left, "fo").wait(ws);
    expectRead(*right, "foo ").wait(ws);
    expectRead(*left, "o ba").wait(ws);
    expectRead(*right, "bar ").wait(ws);
    expectRead(*left, "r ba").wait(ws);
    expectRead(*right, "baz").wait(ws);
    expectRead(*left, "z").wait(ws);
  }

  {
    // Exceeding the limit causes both branches to see the exception after exhausting their buffers.

    auto tee = newTee(heap<MockAsyncInputStream>(text.asBytes(), text.size()), 2);
    auto left = kj::mv(tee.branches[0]);
    auto right = kj::mv(tee.branches[1]);

    expectRead(*left, "fo").wait(ws);
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("tee buffer size limit exceeded",
        expectRead(*left, "o").wait(ws));
    expectRead(*right, "fo").wait(ws);
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("tee buffer size limit exceeded",
        expectRead(*right, "o").wait(ws));
  }

  {
    // We guarantee that two pumps started simultaneously will never exceed our buffer size limit.

    auto tee = newTee(heap<MockAsyncInputStream>(text.asBytes(), text.size()), 2);
    auto left = kj::mv(tee.branches[0]);
    auto right = kj::mv(tee.branches[1]);
    auto leftPipe = kj::newOneWayPipe();
    auto rightPipe = kj::newOneWayPipe();

    auto leftPumpPromise = left->pumpTo(*leftPipe.out);
    auto rightPumpPromise = right->pumpTo(*rightPipe.out);
    KJ_EXPECT(!leftPumpPromise.poll(ws));
    KJ_EXPECT(!rightPumpPromise.poll(ws));

    uint8_t leftBuf[11] = { 0 };
    uint8_t rightBuf[11] = { 0 };

    // The first read on the left pipe will succeed.
    auto leftPromise = leftPipe.in->tryRead(leftBuf, 1, 11);
    KJ_EXPECT(leftPromise.wait(ws) == 2);
    KJ_EXPECT(memcmp(leftBuf, text.begin(), 2) == 0);

    // But the second will block until we relieve pressure on the right pipe.
    leftPromise = leftPipe.in->tryRead(leftBuf + 2, 1, 9);
    KJ_EXPECT(!leftPromise.poll(ws));

    // Relieve the right pipe pressure ...
    auto rightPromise = rightPipe.in->tryRead(rightBuf, 1, 11);
    KJ_EXPECT(rightPromise.wait(ws) == 2);
    KJ_EXPECT(memcmp(rightBuf, text.begin(), 2) == 0);

    // Now the second left pipe read will complete.
    KJ_EXPECT(leftPromise.wait(ws) == 2);
    KJ_EXPECT(memcmp(leftBuf, text.begin(), 4) == 0);

    // Leapfrog the left branch with the right. There should be 2 bytes in the buffer, so we can
    // demand a total of 4.
    rightPromise = rightPipe.in->tryRead(rightBuf + 2, 4, 9);
    KJ_EXPECT(rightPromise.wait(ws) == 4);
    KJ_EXPECT(memcmp(rightBuf, text.begin(), 6) == 0);

    // Leapfrog the right with the left. We demand the entire rest of the stream, so this should
    // block. Note that a regular read for this amount on one of the tee branches directly would
    // exceed our buffer size limit, but this one does not, because we have the pipe to regulate
    // backpressure for us.
    leftPromise = leftPipe.in->tryRead(leftBuf + 4, 7, 7);
    KJ_EXPECT(!leftPromise.poll(ws));

    // Ask for the entire rest of the stream on the right branch and wrap things up.
    rightPromise = rightPipe.in->tryRead(rightBuf + 6, 5, 5);

    KJ_EXPECT(leftPromise.wait(ws) == 7);
    KJ_EXPECT(memcmp(leftBuf, text.begin(), 11) == 0);

    KJ_EXPECT(rightPromise.wait(ws) == 5);
    KJ_EXPECT(memcmp(rightBuf, text.begin(), 11) == 0);
  }
}

KJ_TEST("Userspace OneWayPipe whenWriteDisconnected()") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newOneWayPipe();

  auto abortedPromise = pipe.out->whenWriteDisconnected();
  KJ_ASSERT(!abortedPromise.poll(ws));

  pipe.in = nullptr;

  KJ_ASSERT(abortedPromise.poll(ws));
  abortedPromise.wait(ws);
}

KJ_TEST("Userspace TwoWayPipe whenWriteDisconnected()") {
  kj::EventLoop loop;
  WaitScope ws(loop);

  auto pipe = newTwoWayPipe();

  auto abortedPromise = pipe.ends[0]->whenWriteDisconnected();
  KJ_ASSERT(!abortedPromise.poll(ws));

  pipe.ends[1] = nullptr;

  KJ_ASSERT(abortedPromise.poll(ws));
  abortedPromise.wait(ws);
}

#if !_WIN32  // We don't currently support detecting disconnect with IOCP.
#if !__CYGWIN__  // TODO(someday): Figure out why whenWriteDisconnected() doesn't work on Cygwin.

KJ_TEST("OS OneWayPipe whenWriteDisconnected()") {
  auto io = setupAsyncIo();

  auto pipe = io.provider->newOneWayPipe();

  pipe.out->write("foo", 3).wait(io.waitScope);
  auto abortedPromise = pipe.out->whenWriteDisconnected();
  KJ_ASSERT(!abortedPromise.poll(io.waitScope));

  pipe.in = nullptr;

  KJ_ASSERT(abortedPromise.poll(io.waitScope));
  abortedPromise.wait(io.waitScope);
}

KJ_TEST("OS TwoWayPipe whenWriteDisconnected()") {
  auto io = setupAsyncIo();

  auto pipe = io.provider->newTwoWayPipe();

  pipe.ends[0]->write("foo", 3).wait(io.waitScope);
  pipe.ends[1]->write("bar", 3).wait(io.waitScope);

  auto abortedPromise = pipe.ends[0]->whenWriteDisconnected();
  KJ_ASSERT(!abortedPromise.poll(io.waitScope));

  pipe.ends[1] = nullptr;

  KJ_ASSERT(abortedPromise.poll(io.waitScope));
  abortedPromise.wait(io.waitScope);

  char buffer[4];
  KJ_ASSERT(pipe.ends[0]->tryRead(&buffer, 3, 3).wait(io.waitScope) == 3);
  buffer[3] = '\0';
  KJ_EXPECT(buffer == "bar"_kj);

  // Note: Reading any further in pipe.ends[0] would throw "connection reset".
}

KJ_TEST("import socket FD that's already broken") {
  auto io = setupAsyncIo();

  int fds[2];
  KJ_SYSCALL(socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
  KJ_SYSCALL(write(fds[1], "foo", 3));
  KJ_SYSCALL(close(fds[1]));

  auto stream = io.lowLevelProvider->wrapSocketFd(fds[0], LowLevelAsyncIoProvider::TAKE_OWNERSHIP);

  auto abortedPromise = stream->whenWriteDisconnected();
  KJ_ASSERT(abortedPromise.poll(io.waitScope));
  abortedPromise.wait(io.waitScope);

  char buffer[4];
  KJ_ASSERT(stream->tryRead(&buffer, sizeof(buffer), sizeof(buffer)).wait(io.waitScope) == 3);
  buffer[3] = '\0';
  KJ_EXPECT(buffer == "foo"_kj);
}

#endif  // !__CYGWIN__
#endif  // !_WIN32

KJ_TEST("AggregateConnectionReceiver") {
  EventLoop loop;
  WaitScope ws(loop);

  auto pipe1 = newCapabilityPipe();
  auto pipe2 = newCapabilityPipe();

  auto receiversBuilder = kj::heapArrayBuilder<Own<ConnectionReceiver>>(2);
  receiversBuilder.add(kj::heap<CapabilityStreamConnectionReceiver>(*pipe1.ends[0]));
  receiversBuilder.add(kj::heap<CapabilityStreamConnectionReceiver>(*pipe2.ends[0]));

  auto aggregate = newAggregateConnectionReceiver(receiversBuilder.finish());

  CapabilityStreamNetworkAddress connector1(nullptr, *pipe1.ends[1]);
  CapabilityStreamNetworkAddress connector2(nullptr, *pipe2.ends[1]);

  auto connectAndWrite = [&](NetworkAddress& addr, kj::StringPtr text) {
    return addr.connect()
        .then([text](Own<AsyncIoStream> stream) {
      auto promise = stream->write(text.begin(), text.size());
      return promise.attach(kj::mv(stream));
    }).eagerlyEvaluate([](kj::Exception&& e) {
      KJ_LOG(ERROR, e);
    });
  };

  auto acceptAndRead = [&](ConnectionReceiver& socket, kj::StringPtr expected) {
    return socket
        .accept().then([](Own<AsyncIoStream> stream) {
      auto promise = stream->readAllText();
      return promise.attach(kj::mv(stream));
    }).then([expected](kj::String actual) {
      KJ_EXPECT(actual == expected);
    }).eagerlyEvaluate([](kj::Exception&& e) {
      KJ_LOG(ERROR, e);
    });
  };

  auto connectPromise1 = connectAndWrite(connector1, "foo");
  KJ_EXPECT(!connectPromise1.poll(ws));
  auto connectPromise2 = connectAndWrite(connector2, "bar");
  KJ_EXPECT(!connectPromise2.poll(ws));

  acceptAndRead(*aggregate, "foo").wait(ws);

  auto connectPromise3 = connectAndWrite(connector1, "baz");
  KJ_EXPECT(!connectPromise3.poll(ws));

  acceptAndRead(*aggregate, "bar").wait(ws);
  acceptAndRead(*aggregate, "baz").wait(ws);

  connectPromise1.wait(ws);
  connectPromise2.wait(ws);
  connectPromise3.wait(ws);

  auto acceptPromise1 = acceptAndRead(*aggregate, "qux");
  auto acceptPromise2 = acceptAndRead(*aggregate, "corge");
  auto acceptPromise3 = acceptAndRead(*aggregate, "grault");

  KJ_EXPECT(!acceptPromise1.poll(ws));
  KJ_EXPECT(!acceptPromise2.poll(ws));
  KJ_EXPECT(!acceptPromise3.poll(ws));

  // Cancel one of the acceptors...
  { auto drop = kj::mv(acceptPromise2); }

  connectAndWrite(connector2, "qux").wait(ws);
  connectAndWrite(connector1, "grault").wait(ws);

  acceptPromise1.wait(ws);
  acceptPromise3.wait(ws);
}

// =======================================================================================
// Tests for optimized pumpTo() between OS handles. Note that this is only even optimized on
// some OSes (only Linux as of this writing), but the behavior should still be the same on all
// OSes, so we run the tests regardless.

kj::String bigString(size_t size) {
  auto result = kj::heapString(size);
  for (auto i: kj::zeroTo(size)) {
    result[i] = 'a' + i % 26;
  }
  return result;
}

KJ_TEST("OS handle pumpTo") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0]);

  {
    auto readPromise = expectRead(*pipe2.ends[1], "foo");
    pipe1.ends[0]->write("foo", 3).wait(ws);
    readPromise.wait(ws);
  }

  {
    auto readPromise = expectRead(*pipe2.ends[1], "bar");
    pipe1.ends[0]->write("bar", 3).wait(ws);
    readPromise.wait(ws);
  }

  auto two = bigString(2000);
  auto four = bigString(4000);
  auto eight = bigString(8000);
  auto fiveHundred = bigString(500'000);

  {
    auto readPromise = expectRead(*pipe2.ends[1], two);
    pipe1.ends[0]->write(two.begin(), two.size()).wait(ws);
    readPromise.wait(ws);
  }

  {
    auto readPromise = expectRead(*pipe2.ends[1], four);
    pipe1.ends[0]->write(four.begin(), four.size()).wait(ws);
    readPromise.wait(ws);
  }

  {
    auto readPromise = expectRead(*pipe2.ends[1], eight);
    pipe1.ends[0]->write(eight.begin(), eight.size()).wait(ws);
    readPromise.wait(ws);
  }

  {
    auto readPromise = expectRead(*pipe2.ends[1], fiveHundred);
    pipe1.ends[0]->write(fiveHundred.begin(), fiveHundred.size()).wait(ws);
    readPromise.wait(ws);
  }

  KJ_EXPECT(!pump.poll(ws))
  pipe1.ends[0]->shutdownWrite();
  KJ_EXPECT(pump.wait(ws) == 6 + two.size() + four.size() + eight.size() + fiveHundred.size());
}

KJ_TEST("OS handle pumpTo small limit") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0], 500);

  auto text = bigString(1000);

  auto expected = kj::str(text.slice(0, 500));

  auto readPromise = expectRead(*pipe2.ends[1], expected);
  pipe1.ends[0]->write(text.begin(), text.size()).wait(ws);
  auto secondWritePromise = pipe1.ends[0]->write(text.begin(), text.size());
  readPromise.wait(ws);
  KJ_EXPECT(pump.wait(ws) == 500);

  expectRead(*pipe1.ends[1], text.slice(500)).wait(ws);
}

KJ_TEST("OS handle pumpTo small limit -- write first then read") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto text = bigString(1000);

  auto expected = kj::str(text.slice(0, 500));

  // Initiate the write first and let it put as much in the buffer as possible.
  auto writePromise = pipe1.ends[0]->write(text.begin(), text.size());
  writePromise.poll(ws);

  // Now start the pump.
  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0], 500);

  auto readPromise = expectRead(*pipe2.ends[1], expected);
  writePromise.wait(ws);
  auto secondWritePromise = pipe1.ends[0]->write(text.begin(), text.size());
  readPromise.wait(ws);
  KJ_EXPECT(pump.wait(ws) == 500);

  expectRead(*pipe1.ends[1], text.slice(500)).wait(ws);
}

KJ_TEST("OS handle pumpTo large limit") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0], 750'000);

  auto text = bigString(500'000);

  auto expected = kj::str(text, text.slice(0, 250'000));

  auto readPromise = expectRead(*pipe2.ends[1], expected);
  pipe1.ends[0]->write(text.begin(), text.size()).wait(ws);
  auto secondWritePromise = pipe1.ends[0]->write(text.begin(), text.size());
  readPromise.wait(ws);
  KJ_EXPECT(pump.wait(ws) == 750'000);

  expectRead(*pipe1.ends[1], text.slice(250'000)).wait(ws);
}

KJ_TEST("OS handle pumpTo large limit -- write first then read") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto text = bigString(500'000);

  auto expected = kj::str(text, text.slice(0, 250'000));

  // Initiate the write first and let it put as much in the buffer as possible.
  auto writePromise = pipe1.ends[0]->write(text.begin(), text.size());
  writePromise.poll(ws);

  // Now start the pump.
  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0], 750'000);

  auto readPromise = expectRead(*pipe2.ends[1], expected);
  writePromise.wait(ws);
  auto secondWritePromise = pipe1.ends[0]->write(text.begin(), text.size());
  readPromise.wait(ws);
  KJ_EXPECT(pump.wait(ws) == 750'000);

  expectRead(*pipe1.ends[1], text.slice(250'000)).wait(ws);
}

#if !_WIN32
kj::String fillWriteBuffer(int fd) {
  // Fill up the write buffer of the given FD and return the contents written. We need to use the
  // raw syscalls to do this because KJ doesn't have a way to know how many bytes made it into the
  // socket buffer.
  auto huge = bigString(2'000'000);

  size_t pos = 0;
  for (;;) {
    KJ_ASSERT(pos < huge.size(), "whoa, big buffer");
    ssize_t n;
    KJ_NONBLOCKING_SYSCALL(n = ::write(fd, huge.begin() + pos, huge.size() - pos));
    if (n < 0) break;
    pos += n;
  }

  return kj::str(huge.slice(0, pos));
}

KJ_TEST("OS handle pumpTo write buffer is full before pump") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto bufferContent = fillWriteBuffer(KJ_ASSERT_NONNULL(pipe2.ends[0]->getFd()));

  // Also prime the input pipe with some buffered bytes.
  auto writePromise = pipe1.ends[0]->write("foo", 3);
  writePromise.poll(ws);

  // Start the pump and let it get blocked.
  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0]);
  KJ_EXPECT(!pump.poll(ws));

  // Queue another write, even.
  writePromise = writePromise
      .then([&]() { return pipe1.ends[0]->write("bar", 3); });
  writePromise.poll(ws);

  // See it all go through.
  expectRead(*pipe2.ends[1], bufferContent).wait(ws);
  expectRead(*pipe2.ends[1], "foobar").wait(ws);

  writePromise.wait(ws);

  pipe1.ends[0]->shutdownWrite();
  KJ_EXPECT(pump.wait(ws) == 6);
  pipe2.ends[0]->shutdownWrite();
  KJ_EXPECT(pipe2.ends[1]->readAllText().wait(ws) == "");
}

KJ_TEST("OS handle pumpTo write buffer is full before pump -- and pump ends early") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto bufferContent = fillWriteBuffer(KJ_ASSERT_NONNULL(pipe2.ends[0]->getFd()));

  // Also prime the input pipe with some buffered bytes followed by EOF.
  auto writePromise = pipe1.ends[0]->write("foo", 3)
      .then([&]() { pipe1.ends[0]->shutdownWrite(); });
  writePromise.poll(ws);

  // Start the pump and let it get blocked.
  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0]);
  KJ_EXPECT(!pump.poll(ws));

  // See it all go through.
  expectRead(*pipe2.ends[1], bufferContent).wait(ws);
  expectRead(*pipe2.ends[1], "foo").wait(ws);

  writePromise.wait(ws);

  KJ_EXPECT(pump.wait(ws) == 3);
  pipe2.ends[0]->shutdownWrite();
  KJ_EXPECT(pipe2.ends[1]->readAllText().wait(ws) == "");
}

KJ_TEST("OS handle pumpTo write buffer is full before pump -- and pump hits limit early") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto bufferContent = fillWriteBuffer(KJ_ASSERT_NONNULL(pipe2.ends[0]->getFd()));

  // Also prime the input pipe with some buffered bytes followed by EOF.
  auto writePromise = pipe1.ends[0]->write("foo", 3);
  writePromise.poll(ws);

  // Start the pump and let it get blocked.
  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0], 3);
  KJ_EXPECT(!pump.poll(ws));

  // See it all go through.
  expectRead(*pipe2.ends[1], bufferContent).wait(ws);
  expectRead(*pipe2.ends[1], "foo").wait(ws);

  writePromise.wait(ws);

  KJ_EXPECT(pump.wait(ws) == 3);
  pipe2.ends[0]->shutdownWrite();
  KJ_EXPECT(pipe2.ends[1]->readAllText().wait(ws) == "");
}

KJ_TEST("OS handle pumpTo write buffer is full before pump -- and a lot of data is pumped") {
  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto pipe1 = ioContext.provider->newTwoWayPipe();
  auto pipe2 = ioContext.provider->newTwoWayPipe();

  auto bufferContent = fillWriteBuffer(KJ_ASSERT_NONNULL(pipe2.ends[0]->getFd()));

  // Also prime the input pipe with some buffered bytes followed by EOF.
  auto text = bigString(500'000);
  auto writePromise = pipe1.ends[0]->write(text.begin(), text.size());
  writePromise.poll(ws);

  // Start the pump and let it get blocked.
  auto pump = pipe1.ends[1]->pumpTo(*pipe2.ends[0]);
  KJ_EXPECT(!pump.poll(ws));

  // See it all go through.
  expectRead(*pipe2.ends[1], bufferContent).wait(ws);
  expectRead(*pipe2.ends[1], text).wait(ws);

  writePromise.wait(ws);

  pipe1.ends[0]->shutdownWrite();
  KJ_EXPECT(pump.wait(ws) == text.size());
  pipe2.ends[0]->shutdownWrite();
  KJ_EXPECT(pipe2.ends[1]->readAllText().wait(ws) == "");
}
#endif

KJ_TEST("pump file to socket") {
  // Tests sendfile() optimization

  auto ioContext = setupAsyncIo();
  auto& ws = ioContext.waitScope;

  auto doTest = [&](kj::Own<const File> file) {
    file->writeAll("foobar"_kj.asBytes());

    {
      FileInputStream input(*file);
      auto pipe = ioContext.provider->newTwoWayPipe();
      auto readPromise = pipe.ends[1]->readAllText();
      input.pumpTo(*pipe.ends[0]).wait(ws);
      pipe.ends[0]->shutdownWrite();
      KJ_EXPECT(readPromise.wait(ws) == "foobar");
      KJ_EXPECT(input.getOffset() == 6);
    }

    {
      FileInputStream input(*file);
      auto pipe = ioContext.provider->newTwoWayPipe();
      auto readPromise = pipe.ends[1]->readAllText();
      input.pumpTo(*pipe.ends[0], 3).wait(ws);
      pipe.ends[0]->shutdownWrite();
      KJ_EXPECT(readPromise.wait(ws) == "foo");
      KJ_EXPECT(input.getOffset() == 3);
    }

    {
      FileInputStream input(*file, 3);
      auto pipe = ioContext.provider->newTwoWayPipe();
      auto readPromise = pipe.ends[1]->readAllText();
      input.pumpTo(*pipe.ends[0]).wait(ws);
      pipe.ends[0]->shutdownWrite();
      KJ_EXPECT(readPromise.wait(ws) == "bar");
      KJ_EXPECT(input.getOffset() == 6);
    }

    auto big = bigString(500'000);
    file->writeAll(big);

    {
      FileInputStream input(*file);
      auto pipe = ioContext.provider->newTwoWayPipe();
      auto readPromise = pipe.ends[1]->readAllText();
      input.pumpTo(*pipe.ends[0]).wait(ws);
      pipe.ends[0]->shutdownWrite();
      // Extra parens here so that we don't write the big string to the console on failure...
      KJ_EXPECT((readPromise.wait(ws) == big));
      KJ_EXPECT(input.getOffset() == big.size());
    }
  };

  // Try with an in-memory file. No optimization is possible.
  doTest(kj::newInMemoryFile(kj::nullClock()));

  // Try with a disk file. Should use sendfile().
  auto fs = kj::newDiskFilesystem();
  doTest(fs->getCurrent().createTemporary());
}

}  // namespace
}  // namespace kj
