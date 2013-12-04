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
#include "thread.h"
#include <unistd.h>
#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdlib.h>
#include <arpa/inet.h>

#ifndef POLLRDHUP
// Linux-only optimization.  If not available, define to 0, as this will make it a no-op.
#define POLLRDHUP 0
#endif

namespace kj {

namespace {

void setNonblocking(int fd) {
  int flags;
  KJ_SYSCALL(flags = fcntl(fd, F_GETFL));
  if ((flags & O_NONBLOCK) == 0) {
    KJ_SYSCALL(fcntl(fd, F_SETFL, flags | O_NONBLOCK));
  }
}

class OwnedFileDescriptor {
public:
  OwnedFileDescriptor(int fd): fd(fd) {
#if __linux__
    // Linux has alternate APIs allowing these flags to be set at FD creation; make sure we always
    // use them.
    KJ_DREQUIRE(fcntl(fd, F_GETFD) & FD_CLOEXEC, "You forgot to set CLOEXEC.");
    KJ_DREQUIRE(fcntl(fd, F_GETFL) & O_NONBLOCK, "You forgot to set NONBLOCK.");
#else
    // On non-Linux, we have to set the flags non-atomically.
    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
#endif
  }

  ~OwnedFileDescriptor() noexcept(false) {
    // Don't use SYSCALL() here because close() should not be repeated on EINTR.
    if (close(fd) < 0) {
      KJ_FAIL_SYSCALL("close", errno, fd) {
        // Recoverable exceptions are safe in destructors.
        break;
      }
    }
  }

protected:
  const int fd;
};

// =======================================================================================

class AsyncStreamFd: public AsyncIoStream {
public:
  AsyncStreamFd(UnixEventPort& eventPort, int readFd, int writeFd)
      : eventPort(eventPort), readFd(readFd), writeFd(writeFd) {}
  virtual ~AsyncStreamFd() noexcept(false) {}

  Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return tryReadInternal(buffer, minBytes, maxBytes, 0).then([=](size_t result) {
      KJ_REQUIRE(result >= minBytes, "Premature EOF") {
        // Pretend we read zeros from the input.
        memset(reinterpret_cast<byte*>(buffer) + result, 0, minBytes - result);
        return minBytes;
      }
      return result;
    });
  }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return tryReadInternal(buffer, minBytes, maxBytes, 0);
  }

  Promise<void> write(const void* buffer, size_t size) override {
    ssize_t writeResult;
    KJ_NONBLOCKING_SYSCALL(writeResult = ::write(writeFd, buffer, size)) {
      return READY_NOW;
    }

    // A negative result means EAGAIN, which we can treat the same as having written zero bytes.
    size_t n = writeResult < 0 ? 0 : writeResult;

    if (n == size) {
      return READY_NOW;
    } else {
      buffer = reinterpret_cast<const byte*>(buffer) + n;
      size -= n;
    }

    return eventPort.onFdEvent(writeFd, POLLOUT).then([=](short) {
      return write(buffer, size);
    });
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    if (pieces.size() == 0) {
      return writeInternal(nullptr, nullptr);
    } else {
      return writeInternal(pieces[0], pieces.slice(1, pieces.size()));
    }
  }

  void shutdownWrite() override {
    // There's no legitimate way to get an AsyncStreamFd that isn't a socket through the
    // UnixAsyncIoProvider interface.
    KJ_REQUIRE(readFd == writeFd, "shutdownWrite() is only implemented on sockets.");
    KJ_SYSCALL(shutdown(writeFd, SHUT_WR));
  }

private:
  UnixEventPort& eventPort;
  int readFd;
  int writeFd;
  bool gotHup = false;

  Promise<size_t> tryReadInternal(void* buffer, size_t minBytes, size_t maxBytes,
                                  size_t alreadyRead) {
    // `alreadyRead` is the number of bytes we have already received via previous reads -- minBytes,
    // maxBytes, and buffer have already been adjusted to account for them, but this count must
    // be included in the final return value.

    ssize_t n;
    KJ_NONBLOCKING_SYSCALL(n = ::read(readFd, buffer, maxBytes)) {
      return alreadyRead;
    }

    if (n < 0) {
      // Read would block.
      return eventPort.onFdEvent(readFd, POLLIN | POLLRDHUP).then([=](short events) {
        gotHup = events & (POLLHUP | POLLRDHUP);
        return tryReadInternal(buffer, minBytes, maxBytes, alreadyRead);
      });
    } else if (n == 0) {
      // EOF -OR- maxBytes == 0.
      return alreadyRead;
    } else if (implicitCast<size_t>(n) < minBytes) {
      // The kernel returned fewer bytes than we asked for (and fewer than we need).
      if (gotHup) {
        // We've already received an indication that the next read() will return EOF, so there's
        // nothing to wait for.
        return alreadyRead + n;
      } else {
        // We know that calling read() again will simply fail with EAGAIN (unless a new packet just
        // arrived, which is unlikely), so let's not bother to call read() again but instead just
        // go strait to polling.
        //
        // Note:  Actually, if we haven't done any polls yet, then we haven't had a chance to
        //   receive POLLRDHUP yet, so it's possible we're at EOF.  But that seems like a
        //   sufficiently unusual case that we're better off skipping straight to polling here.
        buffer = reinterpret_cast<byte*>(buffer) + n;
        minBytes -= n;
        maxBytes -= n;
        alreadyRead += n;
        return eventPort.onFdEvent(readFd, POLLIN | POLLRDHUP).then([=](short events) {
          gotHup = events & (POLLHUP | POLLRDHUP);
          return tryReadInternal(buffer, minBytes, maxBytes, alreadyRead);
        });
      }
    } else {
      // We read enough to stop here.
      return alreadyRead + n;
    }
  }

  Promise<void> writeInternal(ArrayPtr<const byte> firstPiece,
                              ArrayPtr<const ArrayPtr<const byte>> morePieces) {
    KJ_STACK_ARRAY(struct iovec, iov, 1 + morePieces.size(), 16, 128);

    // writev() interface is not const-correct.  :(
    iov[0].iov_base = const_cast<byte*>(firstPiece.begin());
    iov[0].iov_len = firstPiece.size();
    for (uint i = 0; i < morePieces.size(); i++) {
      iov[i + 1].iov_base = const_cast<byte*>(morePieces[i].begin());
      iov[i + 1].iov_len = morePieces[i].size();
    }

    ssize_t writeResult;
    KJ_NONBLOCKING_SYSCALL(writeResult = ::writev(writeFd, iov.begin(), iov.size())) {
      // error
      return READY_NOW;
    }

    // A negative result means EAGAIN, which we can treat the same as having written zero bytes.
    size_t n = writeResult < 0 ? 0 : writeResult;

    // Discard all data that was written, then issue a new write for what's left (if any).
    for (;;) {
      if (n < firstPiece.size()) {
        // Only part of the first piece was consumed.  Wait for POLLOUT and then write again.
        firstPiece = firstPiece.slice(n, firstPiece.size());
        return eventPort.onFdEvent(writeFd, POLLOUT).then([=](short) {
          return writeInternal(firstPiece, morePieces);
        });
      } else if (morePieces.size() == 0) {
        // First piece was fully-consumed and there are no more pieces, so we're done.
        KJ_DASSERT(n == firstPiece.size(), n);
        return READY_NOW;
      } else {
        // First piece was fully consumed, so move on to the next piece.
        n -= firstPiece.size();
        firstPiece = morePieces[0];
        morePieces = morePieces.slice(1, morePieces.size());
      }
    }
  }
};

class Socket final: public OwnedFileDescriptor, public AsyncStreamFd {
public:
  Socket(UnixEventPort& eventPort, int fd)
      : OwnedFileDescriptor(fd), AsyncStreamFd(eventPort, fd, fd) {}
};

class ThreadSocket final: public Thread, public OwnedFileDescriptor, public AsyncStreamFd {
  // Combination thread and socket.  The thread must be joined strictly after the socket is closed.

public:
  template <typename StartFunc>
  ThreadSocket(UnixEventPort& eventPort, int fd, StartFunc&& startFunc)
      : Thread(kj::fwd<StartFunc>(startFunc)),
        OwnedFileDescriptor(fd),
        AsyncStreamFd(eventPort, fd, fd) {}
};

// =======================================================================================

class SocketAddress {
public:
  SocketAddress(const void* sockaddr, uint len): addrlen(len) {
    KJ_REQUIRE(len <= sizeof(addr), "Sorry, your sockaddr is too big for me.");
    memcpy(&addr.generic, sockaddr, len);
  }

  int socket(int type) const {
    int result;
#if __linux__
    type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
#endif
    KJ_SYSCALL(result = ::socket(addr.generic.sa_family, type, 0));
    return result;
  }

  void bind(int sockfd) const {
    if (wildcard) {
      // Disable IPV6_V6ONLY because we want to handle both ipv4 and ipv6 on this socket.  (The
      // default value of this option varies across platforms.)
      int value = 0;
      KJ_SYSCALL(setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &value, sizeof(value)));
    }

    KJ_SYSCALL(::bind(sockfd, &addr.generic, addrlen), toString());
  }

  void connect(int sockfd) const {
    // Unfortunately connect() doesn't fit the mold of KJ_NONBLOCKING_SYSCALL, since it indicates
    // non-blocking using EINPROGRESS.
    for (;;) {
      if (::connect(sockfd, &addr.generic, addrlen) < 0) {
        int error = errno;
        if (error == EINPROGRESS) {
          return;
        } else if (error != EINTR) {
          KJ_FAIL_SYSCALL("connect()", error, toString()) {
            // Recover by returning, since reads/writes will simply fail.
            return;
          }
        }
      } else {
        // no error
        return;
      }
    }
  }

  uint getPort() const {
    switch (addr.generic.sa_family) {
      case AF_INET: return ntohs(addr.inet4.sin_port);
      case AF_INET6: return ntohs(addr.inet6.sin6_port);
      default: return 0;
    }
  }

  String toString() const {
    if (wildcard) {
      return str("*:", getPort());
    }

    switch (addr.generic.sa_family) {
      case AF_INET: {
        char buffer[INET6_ADDRSTRLEN];
        if (inet_ntop(addr.inet4.sin_family, &addr.inet4.sin_addr,
                      buffer, sizeof(buffer)) == nullptr) {
          KJ_FAIL_SYSCALL("inet_ntop", errno) { return heapString("(inet_ntop error)"); }
        }
        return str(buffer, ':', ntohs(addr.inet4.sin_port));
      }
      case AF_INET6: {
        char buffer[INET6_ADDRSTRLEN];
        if (inet_ntop(addr.inet6.sin6_family, &addr.inet6.sin6_addr,
                      buffer, sizeof(buffer)) == nullptr) {
          KJ_FAIL_SYSCALL("inet_ntop", errno) { return heapString("(inet_ntop error)"); }
        }
        return str('[', buffer, "]:", ntohs(addr.inet6.sin6_port));
      }
      case AF_UNIX: {
        return str("unix:", addr.unixDomain.sun_path);
      }
      default:
        return str("(unknown address family ", addr.generic.sa_family, ")");
    }
  }

  static SocketAddress parse(StringPtr str, uint portHint, bool requirePort = true) {
    SocketAddress result;

    if (str.startsWith("unix:")) {
      StringPtr path = str.slice(strlen("unix:"));
      KJ_REQUIRE(path.size() < sizeof(addr.unixDomain.sun_path),
                 "Unix domain socket address is too long.", str);
      result.addr.unixDomain.sun_family = AF_UNIX;
      strcpy(result.addr.unixDomain.sun_path, path.cStr());
      result.addrlen = offsetof(struct sockaddr_un, sun_path) + path.size() + 1;
      return result;
    }

    // Try to separate the address and port.
    ArrayPtr<const char> addrPart;
    Maybe<StringPtr> portPart;

    int af;

    if (str.startsWith("[")) {
      // Address starts with a bracket, which is a common way to write an ip6 address with a port,
      // since without brackets around the address part, the port looks like another segment of
      // the address.
      af = AF_INET6;
      size_t closeBracket = KJ_ASSERT_NONNULL(str.findLast(']'),
          "Unclosed '[' in address string.", str);

      addrPart = str.slice(1, closeBracket);
      if (str.size() > closeBracket + 1) {
        KJ_REQUIRE(str.slice(closeBracket + 1).startsWith(":"),
                   "Expected port suffix after ']'.", str);
        portPart = str.slice(closeBracket + 2);
      }
    } else {
      KJ_IF_MAYBE(colon, str.findFirst(':')) {
        if (str.slice(*colon + 1).findFirst(':') == nullptr) {
          // There is exactly one colon and no brackets, so it must be an ip4 address with port.
          af = AF_INET;
          addrPart = str.slice(0, *colon);
          portPart = str.slice(*colon + 1);
        } else {
          // There are two or more colons and no brackets, so the whole thing must be an ip6
          // address with no port.
          af = AF_INET6;
          addrPart = str;
        }
      } else {
        // No colons, so it must be an ip4 address without port.
        af = AF_INET;
        addrPart = str;
      }
    }

    // Parse the port.
    unsigned long port;
    KJ_IF_MAYBE(portText, portPart) {
      char* endptr;
      port = strtoul(portText->cStr(), &endptr, 0);
      if (portText->size() == 0 || *endptr != '\0') {
        KJ_FAIL_REQUIRE("Invalid IP port number.", *portText);
      }
      KJ_REQUIRE(port < 65536, "Port number too large.");
    } else {
      if (requirePort) {
        KJ_REQUIRE(portHint != 0, "You must specify a port with this address.", str);
      }
      port = portHint;
    }

    void* addrTarget;
    if (af == AF_INET6) {
      result.addrlen = sizeof(addr.inet6);
      result.addr.inet6.sin6_family = AF_INET6;
      result.addr.inet6.sin6_port = htons(port);
      addrTarget = &result.addr.inet6.sin6_addr;
    } else {
      result.addrlen = sizeof(addr.inet4);
      result.addr.inet4.sin_family = AF_INET;
      result.addr.inet4.sin_port = htons(port);
      addrTarget = &result.addr.inet4.sin_addr;
    }

    // addrPart is not necessarily NUL-terminated so we have to make a copy.  :(
    KJ_REQUIRE(addrPart.size() < INET6_ADDRSTRLEN - 1, "IP address too long.", addrPart);
    char buffer[INET6_ADDRSTRLEN];
    memcpy(buffer, addrPart.begin(), addrPart.size());
    buffer[addrPart.size()] = '\0';

    // OK, parse it!
    switch (inet_pton(af, buffer, addrTarget)) {
      case 1:
        // success.
        return result;
      case 0:
        KJ_FAIL_REQUIRE("Invalid IP address.", addrPart);
      default:
        KJ_FAIL_SYSCALL("inet_pton", errno, af, addrPart);
    }
  }

  static SocketAddress parseLocal(StringPtr str, uint portHint) {
    // If the address contains no colons, or only a leading colon, and no periods, then it is a
    // port only.  If is empty, then it is a total wildcard.  Otherwise, it is a full address
    // specified the same as any remote address.
    if (str == "*" ||
        (str.findLast(':').orDefault(0) <= 1 &&
         str.findFirst('.') == nullptr)) {
      unsigned long port;
      if (str == "*") {
        port = portHint;
      } else {
        if (str[0] == ':') {
          str = str.slice(1);
        }
        char* endptr;
        port = strtoul(str.cStr(), &endptr, 0);
        if (str.size() == 0 || *endptr != '\0') {
          KJ_FAIL_REQUIRE("Invalid IP port number.", str);
        }
        KJ_REQUIRE(port < 65536, "Port number too large.");
      }

      // Prepare to bind to ALL IP interfaces.  SocketAddress is zero'd by default.
      SocketAddress result;
      result.wildcard = true;
      result.addrlen = sizeof(addr.inet6);
      result.addr.inet6.sin6_family = AF_INET6;
      result.addr.inet6.sin6_port = htons(port);
      return result;
    } else {
      return parse(str, portHint, false);
    }
  }

  static SocketAddress getLocalAddress(int sockfd) {
    SocketAddress result;
    result.addrlen = sizeof(addr);
    KJ_SYSCALL(getsockname(sockfd, &result.addr.generic, &result.addrlen));
    return result;
  }

private:
  SocketAddress(): addrlen(0) {
    memset(&addr, 0, sizeof(addr));
  }

  socklen_t addrlen;
  bool wildcard = false;
  union {
    struct sockaddr generic;
    struct sockaddr_in inet4;
    struct sockaddr_in6 inet6;
    struct sockaddr_un unixDomain;
    struct sockaddr_storage storage;
  } addr;
};

// =======================================================================================

class FdConnectionReceiver final: public ConnectionReceiver, public OwnedFileDescriptor {
public:
  FdConnectionReceiver(UnixEventPort& eventPort, int fd)
      : OwnedFileDescriptor(fd), eventPort(eventPort) {}

  Promise<Own<AsyncIoStream>> accept() override {
    int newFd;

  retry:
#if __linux__
    newFd = ::accept4(fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
    newFd = ::accept(fd, nullptr, nullptr);
#endif

    if (newFd >= 0) {
      return Own<AsyncIoStream>(heap<Socket>(eventPort, newFd));
    } else {
      int error = errno;

      switch (error) {
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
          // Not ready yet.
          return eventPort.onFdEvent(fd, POLLIN).then([this](short) {
            return accept();
          });

        case EINTR:
        case ENETDOWN:
        case EPROTO:
        case ENOPROTOOPT:
        case EHOSTDOWN:
        case ENONET:
        case EHOSTUNREACH:
        case EOPNOTSUPP:
        case ENETUNREACH:
        case ECONNABORTED:
          // The incoming connection is dead-on-arrival.  Just ignore it.
          goto retry;

        default:
          KJ_FAIL_SYSCALL("accept", error);
      }

    }
  }

  uint getPort() override {
    return SocketAddress::getLocalAddress(fd).getPort();
  }

public:
  UnixEventPort& eventPort;
};

// =======================================================================================

class LocalSocketAddress final: public LocalAddress {
public:
  LocalSocketAddress(UnixEventPort& eventPort, SocketAddress addr)
      : eventPort(eventPort), addr(addr) {}

  Own<ConnectionReceiver> listen() override {
    int fd = addr.socket(SOCK_STREAM);
    auto result = heap<FdConnectionReceiver>(eventPort, fd);

    // We always enable SO_REUSEADDR because having to take your server down for five minutes
    // before it can restart really sucks.
    int optval = 1;
    KJ_SYSCALL(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)));

    addr.bind(fd);

    // TODO(someday):  Let queue size be specified explicitly in string addresses.
    KJ_SYSCALL(::listen(fd, SOMAXCONN));

    return kj::mv(result);
  }

  String toString() override {
    return addr.toString();
  }

private:
  UnixEventPort& eventPort;
  SocketAddress addr;
};

class RemoteSocketAddress final: public RemoteAddress {
public:
  RemoteSocketAddress(UnixEventPort& eventPort, SocketAddress addr)
      : eventPort(eventPort), addr(addr) {}

  Promise<Own<AsyncIoStream>> connect() override {
    int fd = addr.socket(SOCK_STREAM);
    auto result = heap<Socket>(eventPort, fd);
    addr.connect(fd);

    return eventPort.onFdEvent(fd, POLLOUT).then(kj::mvCapture(result,
        [fd](Own<AsyncIoStream>&& stream, short events) {
          int err;
          socklen_t errlen = sizeof(err);
          KJ_SYSCALL(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen));
          if (err != 0) {
            KJ_FAIL_SYSCALL("connect()", err) { break; }
          }
          return kj::mv(stream);
        }));
  }

  String toString() override {
    return addr.toString();
  }

private:
  UnixEventPort& eventPort;
  SocketAddress addr;
};

class SocketNetwork final: public Network {
public:
  explicit SocketNetwork(UnixEventPort& eventPort): eventPort(eventPort) {}

  Promise<Own<LocalAddress>> parseLocalAddress(StringPtr addr, uint portHint = 0) override {
    auto& eventPortCopy = eventPort;
    return evalLater(mvCapture(heapString(addr),
        [&eventPortCopy,portHint](String&& addr) -> Own<LocalAddress> {
          return heap<LocalSocketAddress>(eventPortCopy, SocketAddress::parseLocal(addr, portHint));
        }));
  }
  Promise<Own<RemoteAddress>> parseRemoteAddress(StringPtr addr, uint portHint = 0) override {
    auto& eventPortCopy = eventPort;
    return evalLater(mvCapture(heapString(addr),
        [&eventPortCopy,portHint](String&& addr) -> Own<RemoteAddress> {
          return heap<RemoteSocketAddress>(eventPortCopy, SocketAddress::parse(addr, portHint));
        }));
  }

  Own<LocalAddress> getLocalSockaddr(const void* sockaddr, uint len) override {
    return Own<LocalAddress>(heap<LocalSocketAddress>(eventPort, SocketAddress(sockaddr, len)));
  }
  Own<RemoteAddress> getRemoteSockaddr(const void* sockaddr, uint len) override {
    return Own<RemoteAddress>(heap<RemoteSocketAddress>(eventPort, SocketAddress(sockaddr, len)));
  }

private:
  UnixEventPort& eventPort;
};

// =======================================================================================

class UnixAsyncIoProvider final: public AsyncIoProvider {
public:
  UnixAsyncIoProvider()
      : eventLoop(eventPort), network(eventPort) {}

  OneWayPipe newOneWayPipe() override {
    int fds[2];
#if __linux__
    KJ_SYSCALL(pipe2(fds, O_NONBLOCK | O_CLOEXEC));
#else
    KJ_SYSCALL(pipe(fds));
#endif
    return OneWayPipe { heap<Socket>(eventPort, fds[0]), heap<Socket>(eventPort, fds[1]) };
  }

  TwoWayPipe newTwoWayPipe() override {
    int fds[2];
    int type = SOCK_STREAM;
#if __linux__
    type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
#endif
    KJ_SYSCALL(socketpair(AF_UNIX, type, 0, fds));
    return TwoWayPipe { { heap<Socket>(eventPort, fds[0]), heap<Socket>(eventPort, fds[1]) } };
  }

  Network& getNetwork() override {
    return network;
  }

  Own<AsyncIoStream> newPipeThread(
      Function<void(AsyncIoProvider& ioProvider, AsyncIoStream& stream)> startFunc) override {
    int fds[2];
    int type = SOCK_STREAM;
#if __linux__
    type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
#endif
    KJ_SYSCALL(socketpair(AF_UNIX, type, 0, fds));

    int threadFd = fds[1];

    return heap<ThreadSocket>(eventPort, fds[0], kj::mvCapture(startFunc,
        [threadFd](Function<void(AsyncIoProvider& ioProvider, AsyncIoStream& stream)>&& startFunc) {
      KJ_DEFER(KJ_SYSCALL(close(threadFd)));
      UnixAsyncIoProvider ioProvider;
      auto stream = ioProvider.wrapSocketFd(threadFd);
      startFunc(ioProvider, *stream);
    }));
  }

  Own<AsyncInputStream> wrapInputFd(int fd) override {
    setNonblocking(fd);
    return heap<AsyncStreamFd>(eventPort, fd, -1);
  }
  Own<AsyncOutputStream> wrapOutputFd(int fd) override {
    setNonblocking(fd);
    return heap<AsyncStreamFd>(eventPort, -1, fd);
  }
  Own<AsyncIoStream> wrapSocketFd(int fd) override {
    setNonblocking(fd);
    return heap<AsyncStreamFd>(eventPort, fd, fd);
  }
  Own<ConnectionReceiver> wrapListenSocketFd(int fd) override {
    setNonblocking(fd);
    return heap<FdConnectionReceiver>(eventPort, fd);
  }

private:
  UnixEventPort eventPort;
  EventLoop eventLoop;
  SocketNetwork network;
};

}  // namespace

Promise<void> AsyncInputStream::read(void* buffer, size_t bytes) {
  return read(buffer, bytes, bytes).then([](size_t) {});
}

Own<AsyncIoProvider> setupIoEventLoop() {
  return heap<UnixAsyncIoProvider>();
}

}  // namespace kj
