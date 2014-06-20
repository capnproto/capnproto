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
#include "async-unix.h"
#include "debug.h"
#include "thread.h"
#include "io.h"
#include <unistd.h>
#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stddef.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <set>

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

void setCloseOnExec(int fd) {
  int flags;
  KJ_SYSCALL(flags = fcntl(fd, F_GETFD));
  if ((flags & FD_CLOEXEC) == 0) {
    KJ_SYSCALL(fcntl(fd, F_SETFD, flags | FD_CLOEXEC));
  }
}

static constexpr uint NEW_FD_FLAGS =
#if __linux__
    LowLevelAsyncIoProvider::ALREADY_CLOEXEC | LowLevelAsyncIoProvider::ALREADY_NONBLOCK |
#endif
    LowLevelAsyncIoProvider::TAKE_OWNERSHIP;
// We always try to open FDs with CLOEXEC and NONBLOCK already set on Linux, but on other platforms
// this is not possible.

class OwnedFileDescriptor {
public:
  OwnedFileDescriptor(int fd, uint flags): fd(fd), flags(flags) {
    if (flags & LowLevelAsyncIoProvider::ALREADY_NONBLOCK) {
      KJ_DREQUIRE(fcntl(fd, F_GETFL) & O_NONBLOCK, "You claimed you set NONBLOCK, but you didn't.");
    } else {
      setNonblocking(fd);
    }

    if (flags & LowLevelAsyncIoProvider::TAKE_OWNERSHIP) {
      if (flags & LowLevelAsyncIoProvider::ALREADY_CLOEXEC) {
        KJ_DREQUIRE(fcntl(fd, F_GETFD) & FD_CLOEXEC,
                    "You claimed you set CLOEXEC, but you didn't.");
      } else {
        setCloseOnExec(fd);
      }
    }
  }

  ~OwnedFileDescriptor() noexcept(false) {
    // Don't use SYSCALL() here because close() should not be repeated on EINTR.
    if ((flags & LowLevelAsyncIoProvider::TAKE_OWNERSHIP) && close(fd) < 0) {
      KJ_FAIL_SYSCALL("close", errno, fd) {
        // Recoverable exceptions are safe in destructors.
        break;
      }
    }
  }

protected:
  const int fd;

private:
  uint flags;
};

// =======================================================================================

class AsyncStreamFd: public OwnedFileDescriptor, public AsyncIoStream {
public:
  AsyncStreamFd(UnixEventPort& eventPort, int fd, uint flags)
      : OwnedFileDescriptor(fd, flags), eventPort(eventPort) {}
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
    KJ_NONBLOCKING_SYSCALL(writeResult = ::write(fd, buffer, size)) {
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

    return eventPort.onFdEvent(fd, POLLOUT).then([=](short) {
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
    KJ_SYSCALL(shutdown(fd, SHUT_WR));
  }

private:
  UnixEventPort& eventPort;
  bool gotHup = false;

  Promise<size_t> tryReadInternal(void* buffer, size_t minBytes, size_t maxBytes,
                                  size_t alreadyRead) {
    // `alreadyRead` is the number of bytes we have already received via previous reads -- minBytes,
    // maxBytes, and buffer have already been adjusted to account for them, but this count must
    // be included in the final return value.

    ssize_t n;
    KJ_NONBLOCKING_SYSCALL(n = ::read(fd, buffer, maxBytes)) {
      return alreadyRead;
    }

    if (n < 0) {
      // Read would block.
      return eventPort.onFdEvent(fd, POLLIN | POLLRDHUP).then([=](short events) {
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
        return eventPort.onFdEvent(fd, POLLIN | POLLRDHUP).then([=](short events) {
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
    KJ_NONBLOCKING_SYSCALL(writeResult = ::writev(fd, iov.begin(), iov.size())) {
      // Error.

      // We can't "return kj::READY_NOW;" inside this block because it causes a memory leak due to
      // a bug that exists in both Clang and GCC:
      //   http://gcc.gnu.org/bugzilla/show_bug.cgi?id=33799
      //   http://llvm.org/bugs/show_bug.cgi?id=12286
      goto error;
    }
    if (false) {
    error:
      return kj::READY_NOW;
    }

    // A negative result means EAGAIN, which we can treat the same as having written zero bytes.
    size_t n = writeResult < 0 ? 0 : writeResult;

    // Discard all data that was written, then issue a new write for what's left (if any).
    for (;;) {
      if (n < firstPiece.size()) {
        // Only part of the first piece was consumed.  Wait for POLLOUT and then write again.
        firstPiece = firstPiece.slice(n, firstPiece.size());
        return eventPort.onFdEvent(fd, POLLOUT).then([=](short) {
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

// =======================================================================================

class SocketAddress {
public:
  SocketAddress(const void* sockaddr, uint len): addrlen(len) {
    KJ_REQUIRE(len <= sizeof(addr), "Sorry, your sockaddr is too big for me.");
    memcpy(&addr.generic, sockaddr, len);
  }

  bool operator<(const SocketAddress& other) const {
    // So we can use std::set<SocketAddress>...  see DNS lookup code.

    if (wildcard < other.wildcard) return true;
    if (wildcard > other.wildcard) return false;

    if (addrlen < other.addrlen) return true;
    if (addrlen > other.addrlen) return false;

    return memcmp(&addr.generic, &other.addr.generic, addrlen) < 0;
  }

  int socket(int type) const {
    bool isStream = type == SOCK_STREAM;

    int result;
#if __linux__
    type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
#endif
    KJ_SYSCALL(result = ::socket(addr.generic.sa_family, type, 0));

    if (isStream && (addr.generic.sa_family == AF_INET ||
                     addr.generic.sa_family == AF_INET6)) {
      // TODO(0.5):  As a hack for the 0.4 release we are always setting
      //   TCP_NODELAY because Nagle's algorithm pretty much kills Cap'n Proto's
      //   RPC protocol.  Later, we should extend the interface to provide more
      //   control over this.  Perhaps write() should have a flag which
      //   specifies whether to pass MSG_MORE.
      int one = 1;
      KJ_SYSCALL(setsockopt(
          result, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one)));
    }

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

  static Promise<Array<SocketAddress>> lookupHost(
      LowLevelAsyncIoProvider& lowLevel, kj::String host, kj::String service, uint portHint);
  // Perform a DNS lookup.

  static Promise<Array<SocketAddress>> parse(
      LowLevelAsyncIoProvider& lowLevel, StringPtr str, uint portHint) {
    // TODO(someday):  Allow commas in `str`.

    SocketAddress result;

    if (str.startsWith("unix:")) {
      StringPtr path = str.slice(strlen("unix:"));
      KJ_REQUIRE(path.size() < sizeof(addr.unixDomain.sun_path),
                 "Unix domain socket address is too long.", str);
      result.addr.unixDomain.sun_family = AF_UNIX;
      strcpy(result.addr.unixDomain.sun_path, path.cStr());
      result.addrlen = offsetof(struct sockaddr_un, sun_path) + path.size() + 1;
      auto array = kj::heapArrayBuilder<SocketAddress>(1);
      array.add(result);
      return array.finish();
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
        // Not a number.  Maybe it's a service name.  Fall back to DNS.
        return lookupHost(lowLevel, kj::heapString(addrPart), kj::heapString(*portText), portHint);
      }
      KJ_REQUIRE(port < 65536, "Port number too large.");
    } else {
      port = portHint;
    }

    // Check for wildcard.
    if (addrPart.size() == 1 && addrPart[0] == '*') {
      result.wildcard = true;
      result.addrlen = sizeof(addr.inet6);
      result.addr.inet6.sin6_family = AF_INET6;
      result.addr.inet6.sin6_port = htons(port);
      auto array = kj::heapArrayBuilder<SocketAddress>(1);
      array.add(result);
      return array.finish();
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
      case 1: {
        // success.
        auto array = kj::heapArrayBuilder<SocketAddress>(1);
        array.add(result);
        return array.finish();
      }
      case 0:
        // It's apparently not a simple address...  fall back to DNS.
        return lookupHost(lowLevel, kj::heapString(addrPart), nullptr, port);
      default:
        KJ_FAIL_SYSCALL("inet_pton", errno, af, addrPart);
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

  struct LookupParams;
  class LookupReader;
};

class SocketAddress::LookupReader {
  // Reads SocketAddresses off of a pipe coming from another thread that is performing
  // getaddrinfo.

public:
  LookupReader(kj::Own<Thread>&& thread, kj::Own<AsyncInputStream>&& input)
      : thread(kj::mv(thread)), input(kj::mv(input)) {}

  ~LookupReader() {
    if (thread) thread->detach();
  }

  Promise<Array<SocketAddress>> read() {
    return input->tryRead(&current, sizeof(current), sizeof(current)).then(
        [this](size_t n) -> Promise<Array<SocketAddress>> {
      if (n < sizeof(current)) {
        thread = nullptr;
        // getaddrinfo()'s docs seem to say it will never return an empty list, but let's check
        // anyway.
        KJ_REQUIRE(addresses.size() > 0, "DNS lookup returned no addresses.") { break; }
        return addresses.releaseAsArray();
      } else {
        // getaddrinfo() can return multiple copies of the same address for several reasons.
        // A major one is that we don't give it a socket type (SOCK_STREAM vs. SOCK_DGRAM), so
        // it may return two copies of the same address, one for each type, unless it explicitly
        // knows that the service name given is specific to one type.  But we can't tell it a type,
        // because we don't actually know which one the user wants, and if we specify SOCK_STREAM
        // while the user specified a UDP service name then they'll get a resolution error which
        // is lame.  (At least, I think that's how it works.)
        //
        // So we instead resort to de-duping results.
        if (alreadySeen.insert(current).second) {
          addresses.add(current);
        }
        return read();
      }
    });
  }

private:
  kj::Own<Thread> thread;
  kj::Own<AsyncInputStream> input;
  SocketAddress current;
  kj::Vector<SocketAddress> addresses;
  std::set<SocketAddress> alreadySeen;
};

struct SocketAddress::LookupParams {
  kj::String host;
  kj::String service;
};

Promise<Array<SocketAddress>> SocketAddress::lookupHost(
    LowLevelAsyncIoProvider& lowLevel, kj::String host, kj::String service, uint portHint) {
  // This shitty function spawns a thread to run getaddrinfo().  Unfortunately, getaddrinfo() is
  // the only cross-platform DNS API and it is blocking.
  //
  // TODO(perf):  Use a thread pool?  Maybe kj::Thread should use a thread pool automatically?
  //   Maybe use the various platform-specific asynchronous DNS libraries?  Please do not implement
  //   a custom DNS resolver...

  int fds[2];
#if __linux__
  KJ_SYSCALL(pipe2(fds, O_NONBLOCK | O_CLOEXEC));
#else
  KJ_SYSCALL(pipe(fds));
#endif

  auto input = lowLevel.wrapInputFd(fds[0], NEW_FD_FLAGS);

  int outFd = fds[1];

  LookupParams params = { kj::mv(host), kj::mv(service) };

  auto thread = heap<Thread>(kj::mvCapture(params, [outFd,portHint](LookupParams&& params) {
    FdOutputStream output((AutoCloseFd(outFd)));

    struct addrinfo* list;
    int status = getaddrinfo(
        params.host == "*" ? nullptr : params.host.cStr(),
        params.service == nullptr ? nullptr : params.service.cStr(),
        nullptr, &list);
    if (status == 0) {
      KJ_DEFER(freeaddrinfo(list));

      struct addrinfo* cur = list;
      while (cur != nullptr) {
        if (params.service == nullptr) {
          switch (cur->ai_addr->sa_family) {
            case AF_INET:
              ((struct sockaddr_in*)cur->ai_addr)->sin_port = htons(portHint);
              break;
            case AF_INET6:
              ((struct sockaddr_in6*)cur->ai_addr)->sin6_port = htons(portHint);
              break;
            default:
              break;
          }
        }

        SocketAddress addr;
        memset(&addr, 0, sizeof(addr));  // mollify valgrind
        if (params.host == "*") {
          // Set up a wildcard SocketAddress.  Only use the port number returned by getaddrinfo().
          addr.wildcard = true;
          addr.addrlen = sizeof(addr.addr.inet6);
          addr.addr.inet6.sin6_family = AF_INET6;
          switch (cur->ai_addr->sa_family) {
            case AF_INET:
              addr.addr.inet6.sin6_port = ((struct sockaddr_in*)cur->ai_addr)->sin_port;
              break;
            case AF_INET6:
              addr.addr.inet6.sin6_port = ((struct sockaddr_in6*)cur->ai_addr)->sin6_port;
              break;
            default:
              addr.addr.inet6.sin6_port = portHint;
              break;
          }
        } else {
          addr.addrlen = cur->ai_addrlen;
          memcpy(&addr.addr.generic, cur->ai_addr, cur->ai_addrlen);
        }
        static_assert(canMemcpy<SocketAddress>(), "Can't write() SocketAddress...");
        output.write(&addr, sizeof(addr));
        cur = cur->ai_next;
      }
    } else if (status == EAI_SYSTEM) {
      KJ_FAIL_SYSCALL("getaddrinfo", errno, params.host, params.service) {
        return;
      }
    } else {
      KJ_FAIL_REQUIRE("DNS lookup failed.",
                      params.host, params.service, gai_strerror(status)) {
        return;
      }
    }
  }));

  auto reader = heap<LookupReader>(kj::mv(thread), kj::mv(input));
  return reader->read().attach(kj::mv(reader));
}

// =======================================================================================

class FdConnectionReceiver final: public ConnectionReceiver, public OwnedFileDescriptor {
public:
  FdConnectionReceiver(UnixEventPort& eventPort, int fd, uint flags)
      : OwnedFileDescriptor(fd, flags), eventPort(eventPort) {}

  Promise<Own<AsyncIoStream>> accept() override {
    int newFd;

  retry:
#if __linux__
    newFd = ::accept4(fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
    newFd = ::accept(fd, nullptr, nullptr);
#endif

    if (newFd >= 0) {
      return Own<AsyncIoStream>(heap<AsyncStreamFd>(eventPort, newFd, NEW_FD_FLAGS));
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
        case EHOSTDOWN:
        case EHOSTUNREACH:
        case ENETUNREACH:
        case ECONNABORTED:
        case ETIMEDOUT:
          // According to the Linux man page, accept() may report an error if the accepted
          // connection is already broken.  In this case, we really ought to just ignore it and
          // keep waiting.  But it's hard to say exactly what errors are such network errors and
          // which ones are permanent errors.  We've made a guess here.
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

class TimerImpl final: public Timer {
public:
  TimerImpl(UnixEventPort& eventPort): eventPort(eventPort) {}

  TimePoint now() override { return eventPort.steadyTime(); }

  Promise<void> atTime(TimePoint time) override {
    return eventPort.atSteadyTime(time);
  }

  Promise<void> afterDelay(Duration delay) override {
    return eventPort.atSteadyTime(eventPort.steadyTime() + delay);
  }

private:
  UnixEventPort& eventPort;
};

class LowLevelAsyncIoProviderImpl final: public LowLevelAsyncIoProvider {
public:
  LowLevelAsyncIoProviderImpl()
      : eventLoop(eventPort), timer(eventPort), waitScope(eventLoop) {}

  inline WaitScope& getWaitScope() { return waitScope; }

  Own<AsyncInputStream> wrapInputFd(int fd, uint flags = 0) override {
    return heap<AsyncStreamFd>(eventPort, fd, flags);
  }
  Own<AsyncOutputStream> wrapOutputFd(int fd, uint flags = 0) override {
    return heap<AsyncStreamFd>(eventPort, fd, flags);
  }
  Own<AsyncIoStream> wrapSocketFd(int fd, uint flags = 0) override {
    return heap<AsyncStreamFd>(eventPort, fd, flags);
  }
  Promise<Own<AsyncIoStream>> wrapConnectingSocketFd(int fd, uint flags = 0) override {
    auto result = heap<AsyncStreamFd>(eventPort, fd, flags);
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
  Own<ConnectionReceiver> wrapListenSocketFd(int fd, uint flags = 0) override {
    return heap<FdConnectionReceiver>(eventPort, fd, flags);
  }

  Timer& getTimer() override { return timer; }

  UnixEventPort& getEventPort() { return eventPort; }

private:
  UnixEventPort eventPort;
  EventLoop eventLoop;
  TimerImpl timer;
  WaitScope waitScope;
};

// =======================================================================================

class NetworkAddressImpl final: public NetworkAddress {
public:
  NetworkAddressImpl(LowLevelAsyncIoProvider& lowLevel, Array<SocketAddress> addrs)
      : lowLevel(lowLevel), addrs(kj::mv(addrs)) {}

  Promise<Own<AsyncIoStream>> connect() override {
    return connectImpl(0);
  }

  Own<ConnectionReceiver> listen() override {
    if (addrs.size() > 1) {
      KJ_LOG(WARNING, "Bind address resolved to multiple addresses.  Only the first address will "
          "be used.  If this is incorrect, specify the address numerically.  This may be fixed "
          "in the future.", addrs[0].toString());
    }

    int fd = addrs[0].socket(SOCK_STREAM);

    {
      KJ_ON_SCOPE_FAILURE(close(fd));

      // We always enable SO_REUSEADDR because having to take your server down for five minutes
      // before it can restart really sucks.
      int optval = 1;
      KJ_SYSCALL(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)));

      addrs[0].bind(fd);

      // TODO(someday):  Let queue size be specified explicitly in string addresses.
      KJ_SYSCALL(::listen(fd, SOMAXCONN));
    }

    return lowLevel.wrapListenSocketFd(fd, NEW_FD_FLAGS);
  }

  String toString() override {
    return strArray(KJ_MAP(addr, addrs) { return addr.toString(); }, ",");
  }

private:
  LowLevelAsyncIoProvider& lowLevel;
  Array<SocketAddress> addrs;

  Promise<Own<AsyncIoStream>> connectImpl(uint index) {
    KJ_ASSERT(index < addrs.size());

    int fd = addrs[index].socket(SOCK_STREAM);

    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
      addrs[index].connect(fd);
    })) {
      // Connect failed.
      close(fd);
      if (index + 1 < addrs.size()) {
        // Try the next address instead.
        return connectImpl(index + 1);
      } else {
        // No more addresses to try, so propagate the exception.
        return kj::mv(*exception);
      }
    }

    return lowLevel.wrapConnectingSocketFd(fd, NEW_FD_FLAGS).then(
        [](Own<AsyncIoStream>&& stream) -> Promise<Own<AsyncIoStream>> {
      // Success, pass along.
      return kj::mv(stream);
    }, [this,index](Exception&& exception) -> Promise<Own<AsyncIoStream>> {
      // Connect failed.
      if (index + 1 < addrs.size()) {
        // Try the next address instead.
        return connectImpl(index + 1);
      } else {
        // No more addresses to try, so propagate the exception.
        return kj::mv(exception);
      }
    });
  }
};

class SocketNetwork final: public Network {
public:
  explicit SocketNetwork(LowLevelAsyncIoProvider& lowLevel): lowLevel(lowLevel) {}

  Promise<Own<NetworkAddress>> parseAddress(StringPtr addr, uint portHint = 0) override {
    auto& lowLevelCopy = lowLevel;
    return evalLater(mvCapture(heapString(addr),
        [&lowLevelCopy,portHint](String&& addr) {
      return SocketAddress::parse(lowLevelCopy, addr, portHint);
    })).then([&lowLevelCopy](Array<SocketAddress> addresses) -> Own<NetworkAddress> {
      return heap<NetworkAddressImpl>(lowLevelCopy, kj::mv(addresses));
    });
  }

  Own<NetworkAddress> getSockaddr(const void* sockaddr, uint len) override {
    auto array = kj::heapArrayBuilder<SocketAddress>(1);
    array.add(SocketAddress(sockaddr, len));
    return Own<NetworkAddress>(heap<NetworkAddressImpl>(lowLevel, array.finish()));
  }

private:
  LowLevelAsyncIoProvider& lowLevel;
};

// =======================================================================================

class AsyncIoProviderImpl final: public AsyncIoProvider {
public:
  AsyncIoProviderImpl(LowLevelAsyncIoProvider& lowLevel)
      : lowLevel(lowLevel), network(lowLevel) {}

  OneWayPipe newOneWayPipe() override {
    int fds[2];
#if __linux__
    KJ_SYSCALL(pipe2(fds, O_NONBLOCK | O_CLOEXEC));
#else
    KJ_SYSCALL(pipe(fds));
#endif
    return OneWayPipe {
      lowLevel.wrapInputFd(fds[0], NEW_FD_FLAGS),
      lowLevel.wrapOutputFd(fds[1], NEW_FD_FLAGS)
    };
  }

  TwoWayPipe newTwoWayPipe() override {
    int fds[2];
    int type = SOCK_STREAM;
#if __linux__
    type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
#endif
    KJ_SYSCALL(socketpair(AF_UNIX, type, 0, fds));
    return TwoWayPipe { {
      lowLevel.wrapSocketFd(fds[0], NEW_FD_FLAGS),
      lowLevel.wrapSocketFd(fds[1], NEW_FD_FLAGS)
    } };
  }

  Network& getNetwork() override {
    return network;
  }

  PipeThread newPipeThread(
      Function<void(AsyncIoProvider&, AsyncIoStream&, WaitScope&)> startFunc) override {
    int fds[2];
    int type = SOCK_STREAM;
#if __linux__
    type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
#endif
    KJ_SYSCALL(socketpair(AF_UNIX, type, 0, fds));

    int threadFd = fds[1];
    KJ_ON_SCOPE_FAILURE(close(threadFd));

    auto pipe = lowLevel.wrapSocketFd(fds[0], NEW_FD_FLAGS);

    auto thread = heap<Thread>(kj::mvCapture(startFunc,
        [threadFd](Function<void(AsyncIoProvider&, AsyncIoStream&, WaitScope&)>&& startFunc) {
      LowLevelAsyncIoProviderImpl lowLevel;
      auto stream = lowLevel.wrapSocketFd(threadFd, NEW_FD_FLAGS);
      AsyncIoProviderImpl ioProvider(lowLevel);
      startFunc(ioProvider, *stream, lowLevel.getWaitScope());
    }));

    return { kj::mv(thread), kj::mv(pipe) };
  }

  Timer& getTimer() override { return lowLevel.getTimer(); }

private:
  LowLevelAsyncIoProvider& lowLevel;
  SocketNetwork network;
};

}  // namespace

Promise<void> AsyncInputStream::read(void* buffer, size_t bytes) {
  return read(buffer, bytes, bytes).then([](size_t) {});
}

Own<AsyncIoProvider> newAsyncIoProvider(LowLevelAsyncIoProvider& lowLevel) {
  return kj::heap<AsyncIoProviderImpl>(lowLevel);
}

AsyncIoContext setupAsyncIo() {
  auto lowLevel = heap<LowLevelAsyncIoProviderImpl>();
  auto ioProvider = kj::heap<AsyncIoProviderImpl>(*lowLevel);
  auto& waitScope = lowLevel->getWaitScope();
  auto& eventPort = lowLevel->getEventPort();
  return { kj::mv(lowLevel), kj::mv(ioProvider), waitScope, eventPort };
}

}  // namespace kj
