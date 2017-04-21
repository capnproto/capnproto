// Copyright (c) 2016 Sandstorm Development Group, Inc. and contributors
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
// For Unix implementation, see async-io-unix.c++.

// Request Vista-level APIs.
#define WINVER 0x0600
#define _WIN32_WINNT 0x0600

#include "async-io.h"
#include "async-win32.h"
#include "debug.h"
#include "thread.h"
#include "io.h"
#include "vector.h"
#include <set>

#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <stdlib.h>

#ifndef IPV6_V6ONLY
// MinGW's headers are missing this.
#define IPV6_V6ONLY 27
#endif

namespace kj {

namespace _ {  // private

struct WinsockInitializer {
  WinsockInitializer() {
    WSADATA dontcare;
    int result = WSAStartup(MAKEWORD(2, 2), &dontcare);
    if (result != 0) {
      KJ_FAIL_WIN32("WSAStartup()", result);
    }
  }
};

void initWinsockOnce() {
  static WinsockInitializer initializer;
}

int win32Socketpair(SOCKET socks[2]) {
  // This function from: https://github.com/ncm/selectable-socketpair/blob/master/socketpair.c
  //
  // Copyright notice:
  //
  //    Copyright 2007, 2010 by Nathan C. Myers <ncm@cantrip.org>
  //    Redistribution and use in source and binary forms, with or without modification,
  //    are permitted provided that the following conditions are met:
  //
  //        Redistributions of source code must retain the above copyright notice, this
  //        list of conditions and the following disclaimer.
  //
  //        Redistributions in binary form must reproduce the above copyright notice,
  //        this list of conditions and the following disclaimer in the documentation
  //        and/or other materials provided with the distribution.
  //
  //        The name of the author must not be used to endorse or promote products
  //        derived from this software without specific prior written permission.
  //
  //    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  //    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  //    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  //    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  //    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  //    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  //    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  //    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  //    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  //    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  // Note: This function is called from some Cap'n Proto unit tests, despite not having a public
  //   header declaration.
  // TODO(cleanup): Consider putting this somewhere public? Note that since it depends on Winsock,
  //   it needs to be in the kj-async library.

  initWinsockOnce();

  union {
    struct sockaddr_in inaddr;
    struct sockaddr addr;
  } a;
  SOCKET listener;
  int e;
  socklen_t addrlen = sizeof(a.inaddr);
  int reuse = 1;

  if (socks == 0) {
    WSASetLastError(WSAEINVAL);
    return SOCKET_ERROR;
  }
  socks[0] = socks[1] = -1;

  listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == -1)
    return SOCKET_ERROR;

  memset(&a, 0, sizeof(a));
  a.inaddr.sin_family = AF_INET;
  a.inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.inaddr.sin_port = 0;

  for (;;) {
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
           (char*) &reuse, (socklen_t) sizeof(reuse)) == -1)
      break;
    if  (bind(listener, &a.addr, sizeof(a.inaddr)) == SOCKET_ERROR)
      break;

    memset(&a, 0, sizeof(a));
    if  (getsockname(listener, &a.addr, &addrlen) == SOCKET_ERROR)
      break;
    // win32 getsockname may only set the port number, p=0.0005.
    // ( http://msdn.microsoft.com/library/ms738543.aspx ):
    a.inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.inaddr.sin_family = AF_INET;

    if (listen(listener, 1) == SOCKET_ERROR)
      break;

    socks[0] = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (socks[0] == -1)
      break;
    if (connect(socks[0], &a.addr, sizeof(a.inaddr)) == SOCKET_ERROR)
      break;

    socks[1] = accept(listener, NULL, NULL);
    if (socks[1] == -1)
      break;

    closesocket(listener);
    return 0;
  }

  e = WSAGetLastError();
  closesocket(listener);
  closesocket(socks[0]);
  closesocket(socks[1]);
  WSASetLastError(e);
  socks[0] = socks[1] = -1;
  return SOCKET_ERROR;
}

}  // namespace _

namespace {

bool detectWine() {
  HMODULE hntdll = GetModuleHandle("ntdll.dll");
  if(hntdll == NULL) return false;
  return GetProcAddress(hntdll, "wine_get_version") != nullptr;
}

bool isWine() {
  static bool result = detectWine();
  return result;
}

// =======================================================================================

static constexpr uint NEW_FD_FLAGS = LowLevelAsyncIoProvider::TAKE_OWNERSHIP;

class OwnedFd {
public:
  OwnedFd(SOCKET fd, uint flags): fd(fd), flags(flags) {
    // TODO(perf): Maybe use SetFileCompletionNotificationModes() to tell Windows not to bother
    //   delivering an event when the operation completes inline. Not currently implemented on
    //   Wine, though.
  }

  ~OwnedFd() noexcept(false) {
    if (flags & LowLevelAsyncIoProvider::TAKE_OWNERSHIP) {
      KJ_WINSOCK(closesocket(fd)) { break; }
    }
  }

protected:
  SOCKET fd;

private:
  uint flags;
};

// =======================================================================================

class AsyncStreamFd: public OwnedFd, public AsyncIoStream {
public:
  AsyncStreamFd(Win32EventPort& eventPort, SOCKET fd, uint flags)
      : OwnedFd(fd, flags),
        observer(eventPort.observeIo(reinterpret_cast<HANDLE>(fd))) {}
  virtual ~AsyncStreamFd() noexcept(false) {}

  Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return tryRead(buffer, minBytes, maxBytes).then([=](size_t result) {
      KJ_REQUIRE(result >= minBytes, "Premature EOF") {
        // Pretend we read zeros from the input.
        memset(reinterpret_cast<byte*>(buffer) + result, 0, minBytes - result);
        return minBytes;
      }
      return result;
    });
  }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    auto bufs = heapArray<WSABUF>(1);
    bufs[0].buf = reinterpret_cast<char*>(buffer);
    bufs[0].len = maxBytes;

    ArrayPtr<WSABUF> ref = bufs;
    return tryReadInternal(ref, minBytes, 0).attach(kj::mv(bufs));
  }

  Promise<void> write(const void* buffer, size_t size) override {
    auto bufs = heapArray<WSABUF>(1);
    bufs[0].buf = const_cast<char*>(reinterpret_cast<const char*>(buffer));
    bufs[0].len = size;

    ArrayPtr<WSABUF> ref = bufs;
    return writeInternal(ref).attach(kj::mv(bufs));
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    auto bufs = heapArray<WSABUF>(pieces.size());
    for (auto i: kj::indices(pieces)) {
      bufs[i].buf = const_cast<char*>(pieces[i].asChars().begin());
      bufs[i].len = pieces[i].size();
    }

    ArrayPtr<WSABUF> ref = bufs;
    return writeInternal(ref).attach(kj::mv(bufs));
  }

  kj::Promise<void> connect(const struct sockaddr* addr, uint addrlen) {
    // In order to connect asynchronously, we need the ConnectEx() function. Apparently, we have
    // to query the socket for it dynamically, I guess because of the insanity in which winsock
    // can be implemented in userspace and old implementations may not support it.
    GUID guid = WSAID_CONNECTEX;
    LPFN_CONNECTEX connectEx = nullptr;
    DWORD n = 0;
    KJ_WINSOCK(WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
                        &connectEx, sizeof(connectEx), &n, NULL, NULL)) {
      goto fail;  // avoid memory leak due to compiler bugs
    }
    if (false) {
    fail:
      return kj::READY_NOW;
    }

    // OK, phew, we now have our ConnectEx function pointer. Call it.
    auto op = observer->newOperation(0);

    if (!connectEx(fd, addr, addrlen, NULL, 0, NULL, op->getOverlapped())) {
      DWORD error = WSAGetLastError();
      if (error != ERROR_IO_PENDING) {
        KJ_FAIL_WIN32("ConnectEx()", error) { break; }
        return kj::READY_NOW;
      }
    }

    return op->onComplete().then([this](Win32EventPort::IoResult result) {
      if (result.errorCode != ERROR_SUCCESS) {
        KJ_FAIL_WIN32("ConnectEx()", result.errorCode) { return; }
      }

      // Enable shutdown() to work.
      setsockopt(SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
    });
  }

  void shutdownWrite() override {
    // There's no legitimate way to get an AsyncStreamFd that isn't a socket through the
    // Win32AsyncIoProvider interface.
    KJ_WINSOCK(shutdown(fd, SD_SEND));
  }

  void abortRead() override {
    // There's no legitimate way to get an AsyncStreamFd that isn't a socket through the
    // Win32AsyncIoProvider interface.
    KJ_WINSOCK(shutdown(fd, SD_RECEIVE));
  }

  void getsockopt(int level, int option, void* value, uint* length) override {
    socklen_t socklen = *length;
    KJ_WINSOCK(::getsockopt(fd, level, option,
                            reinterpret_cast<char*>(value), &socklen));
    *length = socklen;
  }

  void setsockopt(int level, int option, const void* value, uint length) override {
    KJ_WINSOCK(::setsockopt(fd, level, option,
                            reinterpret_cast<const char*>(value), length));
  }

  void getsockname(struct sockaddr* addr, uint* length) override {
    socklen_t socklen = *length;
    KJ_WINSOCK(::getsockname(fd, addr, &socklen));
    *length = socklen;
  }

  void getpeername(struct sockaddr* addr, uint* length) override {
    socklen_t socklen = *length;
    KJ_WINSOCK(::getpeername(fd, addr, &socklen));
    *length = socklen;
  }

private:
  Own<Win32EventPort::IoObserver> observer;

  Promise<size_t> tryReadInternal(ArrayPtr<WSABUF> bufs, size_t minBytes, size_t alreadyRead) {
    // `bufs` will remain valid until the promise completes and may be freely modified.
    //
    // `alreadyRead` is the number of bytes we have already received via previous reads -- minBytes
    // and buffer have already been adjusted to account for them, but this count must be included
    // in the final return value.

    auto op = observer->newOperation(0);

    DWORD flags = 0;
    if (WSARecv(fd, bufs.begin(), bufs.size(), NULL, &flags,
                op->getOverlapped(), NULL) == SOCKET_ERROR) {
      DWORD error = WSAGetLastError();
      if (error != WSA_IO_PENDING) {
        KJ_FAIL_WIN32("WSARecv()", error) { break; }
        return alreadyRead;
      }
    }

    return op->onComplete()
        .then([this,KJ_CPCAP(bufs),minBytes,alreadyRead](Win32IocpEventPort::IoResult result) mutable
              -> Promise<size_t> {
      if (result.errorCode != ERROR_SUCCESS) {
        if (alreadyRead > 0) {
          // Report what we already read.
          return alreadyRead;
        } else {
          KJ_FAIL_WIN32("WSARecv()", result.errorCode) { break; }
          return size_t(0);
        }
      }

      if (result.bytesTransferred == 0) {
        return alreadyRead;
      }

      alreadyRead += result.bytesTransferred;
      if (result.bytesTransferred >= minBytes) {
        // We can stop here.
        return alreadyRead;
      }
      minBytes -= result.bytesTransferred;

      while (result.bytesTransferred >= bufs[0].len) {
        result.bytesTransferred -= bufs[0].len;
        bufs = bufs.slice(1, bufs.size());
      }

      if (result.bytesTransferred > 0) {
        bufs[0].buf += result.bytesTransferred;
        bufs[0].len -= result.bytesTransferred;
      }

      return tryReadInternal(bufs, minBytes, alreadyRead);
    }).attach(kj::mv(bufs));
  }

  Promise<void> writeInternal(ArrayPtr<WSABUF> bufs) {
    // `bufs` will remain valid until the promise completes and may be freely modified.

    auto op = observer->newOperation(0);

    if (WSASend(fd, bufs.begin(), bufs.size(), NULL, 0,
                op->getOverlapped(), NULL) == SOCKET_ERROR) {
      DWORD error = WSAGetLastError();
      if (error != WSA_IO_PENDING) {
        KJ_FAIL_WIN32("WSASend()", error) { break; }
        return kj::READY_NOW;
      }
    }

    return op->onComplete()
        .then([this,KJ_CPCAP(bufs)](Win32IocpEventPort::IoResult result) mutable -> Promise<void> {
      if (result.errorCode != ERROR_SUCCESS) {
        KJ_FAIL_WIN32("WSASend()", result.errorCode) { break; }
        return kj::READY_NOW;
      }

      while (bufs.size() > 0 && result.bytesTransferred >= bufs[0].len) {
        result.bytesTransferred -= bufs[0].len;
        bufs = bufs.slice(1, bufs.size());
      }

      if (result.bytesTransferred > 0) {
        bufs[0].buf += result.bytesTransferred;
        bufs[0].len -= result.bytesTransferred;
      }

      if (bufs.size() > 0) {
        return writeInternal(bufs);
      } else {
        return kj::READY_NOW;
      }
    }).attach(kj::mv(bufs));
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

  const struct sockaddr* getRaw() const { return &addr.generic; }
  int getRawSize() const { return addrlen; }

  SOCKET socket(int type) const {
    bool isStream = type == SOCK_STREAM;

    SOCKET result = ::socket(addr.generic.sa_family, type, 0);

    if (result == INVALID_SOCKET) {
      KJ_FAIL_WIN32("WSASocket()", WSAGetLastError()) { return INVALID_SOCKET; }
    }

    if (isStream && (addr.generic.sa_family == AF_INET ||
                     addr.generic.sa_family == AF_INET6)) {
      // TODO(perf):  As a hack for the 0.4 release we are always setting
      //   TCP_NODELAY because Nagle's algorithm pretty much kills Cap'n Proto's
      //   RPC protocol.  Later, we should extend the interface to provide more
      //   control over this.  Perhaps write() should have a flag which
      //   specifies whether to pass MSG_MORE.
      BOOL one = TRUE;
      KJ_WINSOCK(setsockopt(result, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one)));
    }

    return result;
  }

  void bind(SOCKET sockfd) const {
    if (wildcard) {
      // Disable IPV6_V6ONLY because we want to handle both ipv4 and ipv6 on this socket.  (The
      // default value of this option varies across platforms.)
      DWORD value = 0;
      KJ_WINSOCK(setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY,
                            reinterpret_cast<char*>(&value), sizeof(value)));
    }

    KJ_WINSOCK(::bind(sockfd, &addr.generic, addrlen), toString());
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
        char buffer[16];
        if (InetNtopA(addr.inet4.sin_family, const_cast<struct in_addr*>(&addr.inet4.sin_addr),
                      buffer, sizeof(buffer)) == nullptr) {
          KJ_FAIL_WIN32("InetNtop", WSAGetLastError()) { break; }
          return heapString("(inet_ntop error)");
        }
        return str(buffer, ':', ntohs(addr.inet4.sin_port));
      }
      case AF_INET6: {
        char buffer[46];
        if (InetNtopA(addr.inet6.sin6_family, const_cast<struct in6_addr*>(&addr.inet6.sin6_addr),
                      buffer, sizeof(buffer)) == nullptr) {
          KJ_FAIL_WIN32("InetNtop", WSAGetLastError()) { break; }
          return heapString("(inet_ntop error)");
        }
        return str('[', buffer, "]:", ntohs(addr.inet6.sin6_port));
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
      // Create an ip6 socket and set IPV6_V6ONLY to 0 later.
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
    char buffer[64];
    KJ_REQUIRE(addrPart.size() < sizeof(buffer) - 1, "IP address too long.", addrPart);
    memcpy(buffer, addrPart.begin(), addrPart.size());
    buffer[addrPart.size()] = '\0';

    // OK, parse it!
    switch (InetPtonA(af, buffer, addrTarget)) {
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
        KJ_FAIL_WIN32("InetPton", WSAGetLastError(), af, addrPart);
    }
  }

  static SocketAddress getLocalAddress(int sockfd) {
    SocketAddress result;
    result.addrlen = sizeof(addr);
    KJ_WINSOCK(getsockname(sockfd, &result.addr.generic, &result.addrlen));
    return result;
  }

  static SocketAddress getWildcardForFamily(int family) {
    SocketAddress result;
    switch (family) {
      case AF_INET:
        result.addrlen = sizeof(addr.inet4);
        result.addr.inet4.sin_family = AF_INET;
        return result;
      case AF_INET6:
        result.addrlen = sizeof(addr.inet6);
        result.addr.inet6.sin6_family = AF_INET6;
        return result;
      default:
        KJ_FAIL_REQUIRE("unknown address family", family);
    }
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
  // TODO(perf): Use GetAddrInfoEx(). But there are problems:
  // - Not implemented in Wine.
  // - Doesn't seem compatible with I/O completion ports, in particular because it's not associated
  //   with a handle. Could signal completion as an APC instead, but that requires the IOCP code
  //   to use GetQueuedCompletionStatusEx() which it doesn't right now becaues it's not available
  //   in Wine.
  // - Requires Unicode, for some reason. Only GetAddrInfoExW() supports async, according to the
  //   docs. Never mind that DNS itself is ASCII...

  SOCKET fds[2];
  KJ_WINSOCK(_::win32Socketpair(fds));

  auto input = lowLevel.wrapInputFd(fds[0], NEW_FD_FLAGS);

  int outFd = fds[1];

  LookupParams params = { kj::mv(host), kj::mv(service) };

  auto thread = heap<Thread>(kj::mvCapture(params, [outFd,portHint](LookupParams&& params) {
    KJ_DEFER(closesocket(outFd));

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
        KJ_ASSERT_CAN_MEMCPY(SocketAddress);

        const char* data = reinterpret_cast<const char*>(&addr);
        size_t size = sizeof(addr);
        while (size > 0) {
          int n;
          KJ_WINSOCK(n = send(outFd, data, size, 0));
          data += n;
          size -= n;
        }

        cur = cur->ai_next;
      }
    } else {
      KJ_FAIL_WIN32("getaddrinfo()", status, params.host, params.service) {
        return;
      }
    }
  }));

  auto reader = heap<LookupReader>(kj::mv(thread), kj::mv(input));
  return reader->read().attach(kj::mv(reader));
}

// =======================================================================================

class FdConnectionReceiver final: public ConnectionReceiver, public OwnedFd {
public:
  FdConnectionReceiver(Win32EventPort& eventPort, SOCKET fd, uint flags)
      : OwnedFd(fd, flags), eventPort(eventPort),
        observer(eventPort.observeIo(reinterpret_cast<HANDLE>(fd))),
        address(SocketAddress::getLocalAddress(fd)) {
    // In order to accept asynchronously, we need the AcceptEx() function. Apparently, we have
    // to query the socket for it dynamically, I guess because of the insanity in which winsock
    // can be implemented in userspace and old implementations may not support it.
    GUID guid = WSAID_ACCEPTEX;
    DWORD n = 0;
    KJ_WINSOCK(WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
                        &acceptEx, sizeof(acceptEx), &n, NULL, NULL)) {
      acceptEx = nullptr;
      return;
    }
  }

  Promise<Own<AsyncIoStream>> accept() override {
    SOCKET newFd = address.socket(SOCK_STREAM);
    KJ_ASSERT(newFd != INVALID_SOCKET);
    auto result = heap<AsyncStreamFd>(eventPort, newFd, NEW_FD_FLAGS);

    auto scratch = heapArray<byte>(256);
    DWORD dummy;
    auto op = observer->newOperation(0);
    if (!acceptEx(fd, newFd, scratch.begin(), 0, 128, 128, &dummy, op->getOverlapped())) {
      DWORD error = WSAGetLastError();
      if (error != ERROR_IO_PENDING) {
        KJ_FAIL_WIN32("AcceptEx()", error) { break; }
        return Own<AsyncIoStream>(kj::mv(result));  // dummy, won't be used
      }
    }

    return op->onComplete().attach(kj::mv(scratch)).then(mvCapture(result,
        [this](Own<AsyncIoStream> stream, Win32EventPort::IoResult ioResult) {
      if (ioResult.errorCode != ERROR_SUCCESS) {
        KJ_FAIL_WIN32("AcceptEx()", ioResult.errorCode) { break; }
      } else {
        SOCKET me = fd;
        stream->setsockopt(SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                           reinterpret_cast<char*>(&me), sizeof(me));
      }
      return kj::mv(stream);
    }));
  }

  uint getPort() override {
    return address.getPort();
  }

  void getsockopt(int level, int option, void* value, uint* length) override {
    socklen_t socklen = *length;
    KJ_WINSOCK(::getsockopt(fd, level, option,
                            reinterpret_cast<char*>(value), &socklen));
    *length = socklen;
  }
  void setsockopt(int level, int option, const void* value, uint length) override {
    KJ_WINSOCK(::setsockopt(fd, level, option,
                            reinterpret_cast<const char*>(value), length));
  }

public:
  Win32EventPort& eventPort;
  Own<Win32EventPort::IoObserver> observer;
  LPFN_ACCEPTEX acceptEx = nullptr;
  SocketAddress address;
};

// TODO(someday): DatagramPortImpl

class LowLevelAsyncIoProviderImpl final: public LowLevelAsyncIoProvider {
public:
  LowLevelAsyncIoProviderImpl()
      : eventLoop(eventPort), waitScope(eventLoop) {}

  inline WaitScope& getWaitScope() { return waitScope; }

  Own<AsyncInputStream> wrapInputFd(SOCKET fd, uint flags = 0) override {
    return heap<AsyncStreamFd>(eventPort, fd, flags);
  }
  Own<AsyncOutputStream> wrapOutputFd(SOCKET fd, uint flags = 0) override {
    return heap<AsyncStreamFd>(eventPort, fd, flags);
  }
  Own<AsyncIoStream> wrapSocketFd(SOCKET fd, uint flags = 0) override {
    return heap<AsyncStreamFd>(eventPort, fd, flags);
  }
  Promise<Own<AsyncIoStream>> wrapConnectingSocketFd(
      SOCKET fd, const struct sockaddr* addr, uint addrlen, uint flags = 0) override {
    auto result = heap<AsyncStreamFd>(eventPort, fd, flags);

    // ConnectEx requires that the socket be bound, for some reason. Bind to an arbitrary port.
    SocketAddress::getWildcardForFamily(addr->sa_family).bind(fd);

    auto connected = result->connect(addr, addrlen);
    return connected.then(kj::mvCapture(result, [](Own<AsyncIoStream>&& result) {
      return kj::mv(result);
    }));
  }
  Own<ConnectionReceiver> wrapListenSocketFd(SOCKET fd, uint flags = 0) override {
    return heap<FdConnectionReceiver>(eventPort, fd, flags);
  }

  Timer& getTimer() override { return eventPort.getTimer(); }

  Win32EventPort& getEventPort() { return eventPort; }

private:
  Win32IocpEventPort eventPort;
  EventLoop eventLoop;
  WaitScope waitScope;
};

// =======================================================================================

class NetworkAddressImpl final: public NetworkAddress {
public:
  NetworkAddressImpl(LowLevelAsyncIoProvider& lowLevel, Array<SocketAddress> addrs)
      : lowLevel(lowLevel), addrs(kj::mv(addrs)) {}

  Promise<Own<AsyncIoStream>> connect() override {
    auto addrsCopy = heapArray(addrs.asPtr());
    auto promise = connectImpl(lowLevel, addrsCopy);
    return promise.attach(kj::mv(addrsCopy));
  }

  Own<ConnectionReceiver> listen() override {
    if (addrs.size() > 1) {
      KJ_LOG(WARNING, "Bind address resolved to multiple addresses.  Only the first address will "
          "be used.  If this is incorrect, specify the address numerically.  This may be fixed "
          "in the future.", addrs[0].toString());
    }

    int fd = addrs[0].socket(SOCK_STREAM);

    {
      KJ_ON_SCOPE_FAILURE(closesocket(fd));

      // We always enable SO_REUSEADDR because having to take your server down for five minutes
      // before it can restart really sucks.
      int optval = 1;
      KJ_WINSOCK(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                            reinterpret_cast<char*>(&optval), sizeof(optval)));

      addrs[0].bind(fd);

      // TODO(someday):  Let queue size be specified explicitly in string addresses.
      KJ_WINSOCK(::listen(fd, SOMAXCONN));
    }

    return lowLevel.wrapListenSocketFd(fd, NEW_FD_FLAGS);
  }

  Own<DatagramPort> bindDatagramPort() override {
    if (addrs.size() > 1) {
      KJ_LOG(WARNING, "Bind address resolved to multiple addresses.  Only the first address will "
          "be used.  If this is incorrect, specify the address numerically.  This may be fixed "
          "in the future.", addrs[0].toString());
    }

    int fd = addrs[0].socket(SOCK_DGRAM);

    {
      KJ_ON_SCOPE_FAILURE(closesocket(fd));

      // We always enable SO_REUSEADDR because having to take your server down for five minutes
      // before it can restart really sucks.
      int optval = 1;
      KJ_WINSOCK(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                            reinterpret_cast<char*>(&optval), sizeof(optval)));

      addrs[0].bind(fd);
    }

    return lowLevel.wrapDatagramSocketFd(fd, NEW_FD_FLAGS);
  }

  Own<NetworkAddress> clone() override {
    return kj::heap<NetworkAddressImpl>(lowLevel, kj::heapArray(addrs.asPtr()));
  }

  String toString() override {
    return strArray(KJ_MAP(addr, addrs) { return addr.toString(); }, ",");
  }

  const SocketAddress& chooseOneAddress() {
    KJ_REQUIRE(addrs.size() > 0, "No addresses available.");
    return addrs[counter++ % addrs.size()];
  }

private:
  LowLevelAsyncIoProvider& lowLevel;
  Array<SocketAddress> addrs;
  uint counter = 0;

  static Promise<Own<AsyncIoStream>> connectImpl(
      LowLevelAsyncIoProvider& lowLevel, ArrayPtr<SocketAddress> addrs) {
    KJ_ASSERT(addrs.size() > 0);

    int fd = addrs[0].socket(SOCK_STREAM);

    return kj::evalNow([&]() {
      return lowLevel.wrapConnectingSocketFd(
          fd, addrs[0].getRaw(), addrs[0].getRawSize(), NEW_FD_FLAGS);
    }).then([](Own<AsyncIoStream>&& stream) -> Promise<Own<AsyncIoStream>> {
      // Success, pass along.
      return kj::mv(stream);
    }, [&lowLevel,KJ_CPCAP(addrs)](Exception&& exception) mutable -> Promise<Own<AsyncIoStream>> {
      // Connect failed.
      if (addrs.size() > 1) {
        // Try the next address instead.
        return connectImpl(lowLevel, addrs.slice(1, addrs.size()));
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
    SOCKET fds[2];
    KJ_WINSOCK(_::win32Socketpair(fds));
    auto in = lowLevel.wrapSocketFd(fds[0], NEW_FD_FLAGS);
    auto out = lowLevel.wrapOutputFd(fds[1], NEW_FD_FLAGS);
    in->shutdownWrite();
    return { kj::mv(in), kj::mv(out) };
  }

  TwoWayPipe newTwoWayPipe() override {
    SOCKET fds[2];
    KJ_WINSOCK(_::win32Socketpair(fds));
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
    SOCKET fds[2];
    KJ_WINSOCK(_::win32Socketpair(fds));

    int threadFd = fds[1];
    KJ_ON_SCOPE_FAILURE(closesocket(threadFd));

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

Own<AsyncIoProvider> newAsyncIoProvider(LowLevelAsyncIoProvider& lowLevel) {
  return kj::heap<AsyncIoProviderImpl>(lowLevel);
}

AsyncIoContext setupAsyncIo() {
  _::initWinsockOnce();

  auto lowLevel = heap<LowLevelAsyncIoProviderImpl>();
  auto ioProvider = kj::heap<AsyncIoProviderImpl>(*lowLevel);
  auto& waitScope = lowLevel->getWaitScope();
  auto& eventPort = lowLevel->getEventPort();
  return { kj::mv(lowLevel), kj::mv(ioProvider), waitScope, eventPort };
}

}  // namespace kj

#endif  // _WIN32
