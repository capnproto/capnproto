// Copyright (c) 2017 Sandstorm Development Group, Inc. and contributors
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

#pragma once

#include "string.h"
#include "vector.h"
#include "async-io.h"
#include <stdint.h>

struct sockaddr;
struct sockaddr_un;

namespace kj {
namespace _ {  // private

// =======================================================================================

#if !_WIN32
kj::ArrayPtr<const char> safeUnixPath(const struct sockaddr_un* addr, uint addrlen);
// sockaddr_un::sun_path is not required to have a NUL terminator! Thus to be safe unix address
// paths MUST be read using this function.
#endif

class CidrRange {
public:
  CidrRange(StringPtr pattern);

  static CidrRange inet4(ArrayPtr<const byte> bits, uint bitCount);
  static CidrRange inet6(ArrayPtr<const uint16_t> prefix, ArrayPtr<const uint16_t> suffix,
                         uint bitCount);
  // Zeros are inserted between `prefix` and `suffix` to extend the address to 128 bits.

  uint getSpecificity() const { return bitCount; }

  bool matches(const struct sockaddr* addr) const;
  bool matchesFamily(int family) const;

  String toString() const;

private:
  int family;
  byte bits[16];
  uint bitCount;    // how many bits in `bits` need to match

  CidrRange(int family, ArrayPtr<const byte> bits, uint bitCount);

  void zeroIrrelevantBits();
};

class NetworkFilter: public LowLevelAsyncIoProvider::NetworkFilter {
public:
  NetworkFilter();
  NetworkFilter(ArrayPtr<const StringPtr> allow, ArrayPtr<const StringPtr> deny,
                NetworkFilter& next);

  bool shouldAllow(const struct sockaddr* addr, uint addrlen) override;
  bool shouldAllowParse(const struct sockaddr* addr, uint addrlen);

private:
  Vector<CidrRange> allowCidrs;
  Vector<CidrRange> denyCidrs;
  bool allowUnix;
  bool allowAbstractUnix;

  kj::Maybe<NetworkFilter&> next;
};

}  // namespace _ (private)
}  // namespace kj
