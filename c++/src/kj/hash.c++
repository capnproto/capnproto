// Copyright (c) 2018 Kenton Varda and contributors
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

#include "hash.h"

namespace kj {
namespace _ {  // private

uint HashCoder::operator*(ArrayPtr<const byte> s) const {
  // murmur2 adapted from libc++ source code.
  //
  // TODO(perf): Use CityHash or FarmHash on 64-bit machines? They seem optimized for x86-64; what
  //   about ARM? Ask Vlad for advice.

  constexpr uint m = 0x5bd1e995;
  constexpr uint r = 24;
  uint h = s.size();
  const byte* data = s.begin();
  uint len = s.size();
  for (; len >= 4; data += 4, len -= 4) {
    uint k;
    memcpy(&k, data, sizeof(k));
    k *= m;
    k ^= k >> r;
    k *= m;
    h *= m;
    h ^= k;
  }
  switch (len) {
  case 3:
    h ^= data[2] << 16;
    // fallthrough
  case 2:
    h ^= data[1] << 8;
    // fallthrough
  case 1:
    h ^= data[0];
    h *= m;
  }
  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;
  return h;
}

}  // namespace _ (private)
} // namespace kj
