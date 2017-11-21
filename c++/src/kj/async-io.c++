// Copyright (c) 2013-2017 Sandstorm Development Group, Inc. and contributors
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
#include "vector.h"
#include "io.h"

#if _WIN32
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#include "windows-sanity.h"
#define inet_pton InetPtonA
#define inet_ntop InetNtopA
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/un.h>
#endif

namespace kj {

Promise<void> AsyncInputStream::read(void* buffer, size_t bytes) {
  return read(buffer, bytes, bytes).then([](size_t) {});
}

Promise<size_t> AsyncInputStream::read(void* buffer, size_t minBytes, size_t maxBytes) {
  return tryRead(buffer, minBytes, maxBytes).then([=](size_t result) {
    KJ_REQUIRE(result >= minBytes, "Premature EOF") {
      // Pretend we read zeros from the input.
      memset(reinterpret_cast<byte*>(buffer) + result, 0, minBytes - result);
      return minBytes;
    }
    return result;
  });
}

Maybe<uint64_t> AsyncInputStream::tryGetLength() { return nullptr; }

namespace {

class AsyncPump {
public:
  AsyncPump(AsyncInputStream& input, AsyncOutputStream& output, uint64_t limit)
      : input(input), output(output), limit(limit) {}

  Promise<uint64_t> pump() {
    // TODO(perf): This could be more efficient by reading half a buffer at a time and then
    //   starting the next read concurrent with writing the data from the previous read.

    uint64_t n = kj::min(limit - doneSoFar, sizeof(buffer));
    if (n == 0) return doneSoFar;

    return input.tryRead(buffer, 1, sizeof(buffer))
        .then([this](size_t amount) -> Promise<uint64_t> {
      if (amount == 0) return doneSoFar;  // EOF
      doneSoFar += amount;
      return output.write(buffer, amount)
          .then([this]() {
        return pump();
      });
    });
  }

private:
  AsyncInputStream& input;
  AsyncOutputStream& output;
  uint64_t limit;
  uint64_t doneSoFar = 0;
  byte buffer[4096];
};

}  // namespace

Promise<uint64_t> AsyncInputStream::pumpTo(
    AsyncOutputStream& output, uint64_t amount) {
  // See if output wants to dispatch on us.
  KJ_IF_MAYBE(result, output.tryPumpFrom(*this, amount)) {
    return kj::mv(*result);
  }

  // OK, fall back to naive approach.
  auto pump = heap<AsyncPump>(*this, output, amount);
  auto promise = pump->pump();
  return promise.attach(kj::mv(pump));
}

namespace {

class AllReader {
public:
  AllReader(AsyncInputStream& input): input(input) {}

  Promise<Array<byte>> readAllBytes() {
    return loop().then([this](uint64_t size) {
      auto out = heapArray<byte>(size);
      copyInto(out);
      return out;
    });
  }

  Promise<String> readAllText() {
    return loop().then([this](uint64_t size) {
      auto out = heapArray<char>(size + 1);
      copyInto(out.slice(0, out.size() - 1).asBytes());
      out.back() = '\0';
      return String(kj::mv(out));
    });
  }

private:
  AsyncInputStream& input;
  Vector<Array<byte>> parts;

  Promise<uint64_t> loop(uint64_t total = 0) {
    auto part = heapArray<byte>(4096);
    auto partPtr = part.asPtr();
    parts.add(kj::mv(part));
    return input.tryRead(partPtr.begin(), partPtr.size(), partPtr.size())
        .then([this,KJ_CPCAP(partPtr),total](size_t amount) -> Promise<uint64_t> {
      uint64_t newTotal = total + amount;
      if (amount < partPtr.size()) {
        return newTotal;
      } else {
        return loop(newTotal);
      }
    });
  }

  void copyInto(ArrayPtr<byte> out) {
    size_t pos = 0;
    for (auto& part: parts) {
      size_t n = kj::min(part.size(), out.size() - pos);
      memcpy(out.begin() + pos, part.begin(), n);
      pos += n;
    }
  }
};

}  // namespace

Promise<Array<byte>> AsyncInputStream::readAllBytes() {
  auto reader = kj::heap<AllReader>(*this);
  auto promise = reader->readAllBytes();
  return promise.attach(kj::mv(reader));
}

Promise<String> AsyncInputStream::readAllText() {
  auto reader = kj::heap<AllReader>(*this);
  auto promise = reader->readAllText();
  return promise.attach(kj::mv(reader));
}

Maybe<Promise<uint64_t>> AsyncOutputStream::tryPumpFrom(
    AsyncInputStream& input, uint64_t amount) {
  return nullptr;
}

Promise<Own<AsyncCapabilityStream>> AsyncCapabilityStream::receiveStream() {
  return tryReceiveStream()
      .then([](Maybe<Own<AsyncCapabilityStream>>&& result)
            -> Promise<Own<AsyncCapabilityStream>> {
    KJ_IF_MAYBE(r, result) {
      return kj::mv(*r);
    } else {
      return KJ_EXCEPTION(FAILED, "EOF when expecting to receive capability");
    }
  });
}

Promise<AutoCloseFd> AsyncCapabilityStream::receiveFd() {
  return tryReceiveFd().then([](Maybe<AutoCloseFd>&& result) -> Promise<AutoCloseFd> {
    KJ_IF_MAYBE(r, result) {
      return kj::mv(*r);
    } else {
      return KJ_EXCEPTION(FAILED, "EOF when expecting to receive capability");
    }
  });
}
Promise<Maybe<AutoCloseFd>> AsyncCapabilityStream::tryReceiveFd() {
  return KJ_EXCEPTION(UNIMPLEMENTED, "this stream cannot receive file descriptors");
}
Promise<void> AsyncCapabilityStream::sendFd(int fd) {
  return KJ_EXCEPTION(UNIMPLEMENTED, "this stream cannot send file descriptors");
}

void AsyncIoStream::getsockopt(int level, int option, void* value, uint* length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void AsyncIoStream::setsockopt(int level, int option, const void* value, uint length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void AsyncIoStream::getsockname(struct sockaddr* addr, uint* length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void AsyncIoStream::getpeername(struct sockaddr* addr, uint* length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void ConnectionReceiver::getsockopt(int level, int option, void* value, uint* length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void ConnectionReceiver::setsockopt(int level, int option, const void* value, uint length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void DatagramPort::getsockopt(int level, int option, void* value, uint* length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void DatagramPort::setsockopt(int level, int option, const void* value, uint length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
Own<DatagramPort> NetworkAddress::bindDatagramPort() {
  KJ_UNIMPLEMENTED("Datagram sockets not implemented.");
}
Own<DatagramPort> LowLevelAsyncIoProvider::wrapDatagramSocketFd(
    Fd fd, LowLevelAsyncIoProvider::NetworkFilter& filter, uint flags) {
  KJ_UNIMPLEMENTED("Datagram sockets not implemented.");
}
#if !_WIN32
Own<AsyncCapabilityStream> LowLevelAsyncIoProvider::wrapUnixSocketFd(Fd fd, uint flags) {
  KJ_UNIMPLEMENTED("Unix socket with FD passing not implemented.");
}
#endif
CapabilityPipe AsyncIoProvider::newCapabilityPipe() {
  KJ_UNIMPLEMENTED("Capability pipes not implemented.");
}

Own<AsyncInputStream> LowLevelAsyncIoProvider::wrapInputFd(OwnFd&& fd, uint flags) {
  return wrapInputFd(reinterpret_cast<Fd>(fd.release()), flags | TAKE_OWNERSHIP);
}
Own<AsyncOutputStream> LowLevelAsyncIoProvider::wrapOutputFd(OwnFd&& fd, uint flags) {
  return wrapOutputFd(reinterpret_cast<Fd>(fd.release()), flags | TAKE_OWNERSHIP);
}
Own<AsyncIoStream> LowLevelAsyncIoProvider::wrapSocketFd(OwnFd&& fd, uint flags) {
  return wrapSocketFd(reinterpret_cast<Fd>(fd.release()), flags | TAKE_OWNERSHIP);
}
#if !_WIN32
Own<AsyncCapabilityStream> LowLevelAsyncIoProvider::wrapUnixSocketFd(OwnFd&& fd, uint flags) {
  return wrapUnixSocketFd(reinterpret_cast<Fd>(fd.release()), flags | TAKE_OWNERSHIP);
}
#endif
Promise<Own<AsyncIoStream>> LowLevelAsyncIoProvider::wrapConnectingSocketFd(
    OwnFd&& fd, const struct sockaddr* addr, uint addrlen, uint flags) {
  return wrapConnectingSocketFd(reinterpret_cast<Fd>(fd.release()), addr, addrlen,
                                flags | TAKE_OWNERSHIP);
}
Own<ConnectionReceiver> LowLevelAsyncIoProvider::wrapListenSocketFd(
    OwnFd&& fd, NetworkFilter& filter, uint flags) {
  return wrapListenSocketFd(reinterpret_cast<Fd>(fd.release()), filter, flags | TAKE_OWNERSHIP);
}
Own<ConnectionReceiver> LowLevelAsyncIoProvider::wrapListenSocketFd(OwnFd&& fd, uint flags) {
  return wrapListenSocketFd(reinterpret_cast<Fd>(fd.release()), flags | TAKE_OWNERSHIP);
}
Own<DatagramPort> LowLevelAsyncIoProvider::wrapDatagramSocketFd(
    OwnFd&& fd, NetworkFilter& filter, uint flags) {
  return wrapDatagramSocketFd(reinterpret_cast<Fd>(fd.release()), filter, flags | TAKE_OWNERSHIP);
}
Own<DatagramPort> LowLevelAsyncIoProvider::wrapDatagramSocketFd(OwnFd&& fd, uint flags) {
  return wrapDatagramSocketFd(reinterpret_cast<Fd>(fd.release()), flags | TAKE_OWNERSHIP);
}

namespace {

class DummyNetworkFilter: public kj::LowLevelAsyncIoProvider::NetworkFilter {
public:
  bool shouldAllow(const struct sockaddr* addr, uint addrlen) override { return true; }
};

}  // namespace

LowLevelAsyncIoProvider::NetworkFilter& LowLevelAsyncIoProvider::NetworkFilter::getAllAllowed() {
  static DummyNetworkFilter result;
  return result;
}

// =======================================================================================
// Convenience adapters.

Promise<Own<AsyncIoStream>> CapabilityStreamConnectionReceiver::accept() {
  return inner.receiveStream()
      .then([](Own<AsyncCapabilityStream>&& stream) -> Own<AsyncIoStream> {
    return kj::mv(stream);
  });
}

uint CapabilityStreamConnectionReceiver::getPort() {
  return 0;
}

Promise<Own<AsyncIoStream>> CapabilityStreamNetworkAddress::connect() {
  auto pipe = provider.newCapabilityPipe();
  auto result = kj::mv(pipe.ends[0]);
  return inner.sendStream(kj::mv(pipe.ends[1]))
      .then(kj::mvCapture(result, [](Own<AsyncIoStream>&& result) {
    return kj::mv(result);
  }));
}
Own<ConnectionReceiver> CapabilityStreamNetworkAddress::listen() {
  return kj::heap<CapabilityStreamConnectionReceiver>(inner);
}

Own<NetworkAddress> CapabilityStreamNetworkAddress::clone() {
  KJ_UNIMPLEMENTED("can't clone CapabilityStreamNetworkAddress");
}
String CapabilityStreamNetworkAddress::toString() {
  return kj::str("<CapabilityStreamNetworkAddress>");
}

// =======================================================================================

namespace _ {  // private

#if !_WIN32

kj::ArrayPtr<const char> safeUnixPath(const struct sockaddr_un* addr, uint addrlen) {
  KJ_REQUIRE(addr->sun_family == AF_UNIX, "not a unix address");
  KJ_REQUIRE(addrlen >= offsetof(sockaddr_un, sun_path), "invalid unix address");

  size_t maxPathlen = addrlen - offsetof(sockaddr_un, sun_path);

  size_t pathlen;
  if (maxPathlen > 0 && addr->sun_path[0] == '\0') {
    // Linux "abstract" unix address
    pathlen = strnlen(addr->sun_path + 1, maxPathlen - 1) + 1;
  } else {
    pathlen = strnlen(addr->sun_path, maxPathlen);
  }
  return kj::arrayPtr(addr->sun_path, pathlen);
}

#endif  // !_WIN32

CidrRange::CidrRange(StringPtr pattern) {
  size_t slashPos = KJ_REQUIRE_NONNULL(pattern.findFirst('/'), "invalid CIDR", pattern);

  bitCount = pattern.slice(slashPos + 1).parseAs<uint>();

  KJ_STACK_ARRAY(char, addr, slashPos + 1, 128, 128);
  memcpy(addr.begin(), pattern.begin(), slashPos);
  addr[slashPos] = '\0';

  if (pattern.findFirst(':') == nullptr) {
    family = AF_INET;
    KJ_REQUIRE(bitCount <= 32, "invalid CIDR", pattern);
  } else {
    family = AF_INET6;
    KJ_REQUIRE(bitCount <= 128, "invalid CIDR", pattern);
  }

  KJ_ASSERT(inet_pton(family, addr.begin(), bits) > 0, "invalid CIDR", pattern);
  zeroIrrelevantBits();
}

CidrRange::CidrRange(int family, ArrayPtr<const byte> bits, uint bitCount)
    : family(family), bitCount(bitCount) {
  if (family == AF_INET) {
    KJ_REQUIRE(bitCount <= 32);
  } else {
    KJ_REQUIRE(bitCount <= 128);
  }
  KJ_REQUIRE(bits.size() * 8 >= bitCount);
  size_t byteCount = (bitCount + 7) / 8;
  memcpy(this->bits, bits.begin(), byteCount);
  memset(this->bits + byteCount, 0, sizeof(this->bits) - byteCount);

  zeroIrrelevantBits();
}

CidrRange CidrRange::inet4(ArrayPtr<const byte> bits, uint bitCount) {
  return CidrRange(AF_INET, bits, bitCount);
}
CidrRange CidrRange::inet6(
    ArrayPtr<const uint16_t> prefix, ArrayPtr<const uint16_t> suffix,
    uint bitCount) {
  KJ_REQUIRE(prefix.size() + suffix.size() <= 8);

  byte bits[16] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, };

  for (size_t i: kj::indices(prefix)) {
    bits[i * 2] = prefix[i] >> 8;
    bits[i * 2 + 1] = prefix[i] & 0xff;
  }

  byte* suffixBits = bits + (16 - suffix.size() * 2);
  for (size_t i: kj::indices(suffix)) {
    suffixBits[i * 2] = suffix[i] >> 8;
    suffixBits[i * 2 + 1] = suffix[i] & 0xff;
  }

  return CidrRange(AF_INET6, bits, bitCount);
}

bool CidrRange::matches(const struct sockaddr* addr) const {
  const byte* otherBits;

  switch (family) {
    case AF_INET:
      if (addr->sa_family == AF_INET6) {
        otherBits = reinterpret_cast<const struct sockaddr_in6*>(addr)->sin6_addr.s6_addr;
        static constexpr byte V6MAPPED[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
        if (memcmp(otherBits, V6MAPPED, sizeof(V6MAPPED)) == 0) {
          // We're an ipv4 range and the address is ipv6, but it's a "v6 mapped" address, meaning
          // it's equivalent to an ipv4 address. Try to match against the ipv4 part.
          otherBits = otherBits + sizeof(V6MAPPED);
        } else {
          return false;
        }
      } else if (addr->sa_family == AF_INET) {
        otherBits = reinterpret_cast<const byte*>(
            &reinterpret_cast<const struct sockaddr_in*>(addr)->sin_addr.s_addr);
      } else {
        return false;
      }

      break;

    case AF_INET6:
      if (addr->sa_family != AF_INET6) return false;

      otherBits = reinterpret_cast<const struct sockaddr_in6*>(addr)->sin6_addr.s6_addr;
      break;

    default:
      KJ_UNREACHABLE;
  }

  if (memcmp(bits, otherBits, bitCount / 8) != 0) return false;

  return bitCount == 128 ||
      bits[bitCount / 8] == (otherBits[bitCount / 8] & (0xff00 >> (bitCount % 8)));
}

bool CidrRange::matchesFamily(int family) const {
  switch (family) {
    case AF_INET:
      return this->family == AF_INET;
    case AF_INET6:
      // Even if we're a v4 CIDR, we can match v6 addresses in the v4-mapped range.
      return true;
    default:
      return false;
  }
}

String CidrRange::toString() const {
  char result[128];
  KJ_ASSERT(inet_ntop(family, (void*)bits, result, sizeof(result)) == result);
  return kj::str(result, '/', bitCount);
}

void CidrRange::zeroIrrelevantBits() {
  // Mask out insignificant bits of partial byte.
  if (bitCount < 128) {
    bits[bitCount / 8] &= 0xff00 >> (bitCount % 8);

    // Zero the remaining bytes.
    size_t n = bitCount / 8 + 1;
    memset(bits + n, 0, sizeof(bits) - n);
  }
}

// -----------------------------------------------------------------------------

ArrayPtr<const CidrRange> localCidrs() {
  static const CidrRange result[] = {
    // localhost
    "127.0.0.0/8"_kj,
    "::1/128"_kj,

    // Trying to *connect* to 0.0.0.0 on many systems is equivalent to connecting to localhost.
    // (wat)
    "0.0.0.0/32"_kj,
    "::/128"_kj,
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents result from implicitly
  //   casting to our return type.
  return kj::arrayPtr(result, kj::size(result));
}

ArrayPtr<const CidrRange> privateCidrs() {
  static const CidrRange result[] = {
    "10.0.0.0/8"_kj,            // RFC1918 reserved for internal network
    "100.64.0.0/10"_kj,         // RFC6598 "shared address space" for carrier-grade NAT
    "169.254.0.0/16"_kj,        // RFC3927 "link local" (auto-configured LAN in absence of DHCP)
    "172.16.0.0/12"_kj,         // RFC1918 reserved for internal network
    "192.168.0.0/16"_kj,        // RFC1918 reserved for internal network

    "fc00::/7"_kj,              // RFC4193 unique private network
    "fe80::/10"_kj,             // RFC4291 "link local" (auto-configured LAN in absence of DHCP)
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents result from implicitly
  //   casting to our return type.
  return kj::arrayPtr(result, kj::size(result));
}

ArrayPtr<const CidrRange> reservedCidrs() {
  static const CidrRange result[] = {
    "192.0.0.0/24"_kj,          // RFC6890 reserved for special protocols
    "224.0.0.0/4"_kj,           // RFC1112 multicast
    "240.0.0.0/4"_kj,           // RFC1112 multicast / reserved for future use
    "255.255.255.255/32"_kj,    // RFC0919 broadcast address

    "2001::/23"_kj,             // RFC2928 reserved for special protocols
    "ff00::/8"_kj,              // RFC4291 multicast
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents result from implicitly
  //   casting to our return type.
  return kj::arrayPtr(result, kj::size(result));
}

ArrayPtr<const CidrRange> exampleAddresses() {
  static const CidrRange result[] = {
    "192.0.2.0/24"_kj,          // RFC5737 "example address" block 1 -- like example.com for IPs
    "198.51.100.0/24"_kj,       // RFC5737 "example address" block 2 -- like example.com for IPs
    "203.0.113.0/24"_kj,        // RFC5737 "example address" block 3 -- like example.com for IPs
    "2001:db8::/32"_kj,         // RFC3849 "example address" block -- like example.com for IPs
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents result from implicitly
  //   casting to our return type.
  return kj::arrayPtr(result, kj::size(result));
}

NetworkFilter::NetworkFilter()
    : allowUnix(true), allowAbstractUnix(true) {
  allowCidrs.add(CidrRange::inet4({0,0,0,0}, 0));
  allowCidrs.add(CidrRange::inet6({}, {}, 0));
  denyCidrs.addAll(reservedCidrs());
}

NetworkFilter::NetworkFilter(ArrayPtr<const StringPtr> allow, ArrayPtr<const StringPtr> deny,
                             NetworkFilter& next)
    : allowUnix(false), allowAbstractUnix(false), next(next) {
  for (auto rule: allow) {
    if (rule == "local") {
      allowCidrs.addAll(localCidrs());
    } else if (rule == "network") {
      allowCidrs.add(CidrRange::inet4({0,0,0,0}, 0));
      allowCidrs.add(CidrRange::inet6({}, {}, 0));
      denyCidrs.addAll(localCidrs());
    } else if (rule == "private") {
      allowCidrs.addAll(privateCidrs());
      allowCidrs.addAll(localCidrs());
    } else if (rule == "public") {
      allowCidrs.add(CidrRange::inet4({0,0,0,0}, 0));
      allowCidrs.add(CidrRange::inet6({}, {}, 0));
      denyCidrs.addAll(privateCidrs());
      denyCidrs.addAll(localCidrs());
    } else if (rule == "unix") {
      allowUnix = true;
    } else if (rule == "unix-abstract") {
      allowAbstractUnix = true;
    } else {
      allowCidrs.add(CidrRange(rule));
    }
  }

  for (auto rule: deny) {
    if (rule == "local") {
      denyCidrs.addAll(localCidrs());
    } else if (rule == "network") {
      KJ_FAIL_REQUIRE("don't deny 'network', allow 'local' instead");
    } else if (rule == "private") {
      denyCidrs.addAll(privateCidrs());
    } else if (rule == "public") {
      // Tricky: What if we allow 'network' and deny 'public'?
      KJ_FAIL_REQUIRE("don't deny 'public', allow 'private' instead");
    } else if (rule == "unix") {
      allowUnix = false;
    } else if (rule == "unix-abstract") {
      allowAbstractUnix = false;
    } else {
      denyCidrs.add(CidrRange(rule));
    }
  }
}

bool NetworkFilter::shouldAllow(const struct sockaddr* addr, uint addrlen) {
  KJ_REQUIRE(addrlen >= sizeof(addr->sa_family));

#if !_WIN32
  if (addr->sa_family == AF_UNIX) {
    auto path = safeUnixPath(reinterpret_cast<const struct sockaddr_un*>(addr), addrlen);
    if (path.size() > 0 && path[0] == '\0') {
      return allowAbstractUnix;
    } else {
      return allowUnix;
    }
  }
#endif

  bool allowed = false;
  uint allowSpecificity = 0;
  for (auto& cidr: allowCidrs) {
    if (cidr.matches(addr)) {
      allowSpecificity = kj::max(allowSpecificity, cidr.getSpecificity());
      allowed = true;
    }
  }
  if (!allowed) return false;
  for (auto& cidr: denyCidrs) {
    if (cidr.matches(addr)) {
      if (cidr.getSpecificity() >= allowSpecificity) return false;
    }
  }

  KJ_IF_MAYBE(n, next) {
    return n->shouldAllow(addr, addrlen);
  } else {
    return true;
  }
}

bool NetworkFilter::shouldAllowParse(const struct sockaddr* addr, uint addrlen) {
  bool matched = false;
#if !_WIN32
  if (addr->sa_family == AF_UNIX) {
    auto path = safeUnixPath(reinterpret_cast<const struct sockaddr_un*>(addr), addrlen);
    if (path.size() > 0 && path[0] == '\0') {
      if (allowAbstractUnix) matched = true;
    } else {
      if (allowUnix) matched = true;
    }
  } else {
#endif
    for (auto& cidr: allowCidrs) {
      if (cidr.matchesFamily(addr->sa_family)) {
        matched = true;
      }
    }
#if !_WIN32
  }
#endif

  if (matched) {
    KJ_IF_MAYBE(n, next) {
      return n->shouldAllowParse(addr, addrlen);
    } else {
      return true;
    }
  } else {
    // No allow rule matches this address family, so don't even allow parsing it.
    return false;
  }
}

}  // namespace _ (private)
}  // namespace kj
