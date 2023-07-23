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
#include <kj/win32-api-version.h>
#endif

#include "debug.h"
#include "cidr.h"

#if _WIN32
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#include <kj/windows-sanity.h>
#define inet_pton InetPtonA
#define inet_ntop InetNtopA
#include <io.h>
#define dup _dup
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

namespace kj {

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

}  // namespace kj
