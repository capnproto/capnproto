// Copyright (c) 2026 Cloudflare, Inc. and contributors
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

#include <kj/common.h>
#include <string.h>
#include <cstring>

struct Pod {
  int a;
  char b[8];
};

#define CLEAR(x) memset(&(x), 0, sizeof(x))

void calls(Pod& pod, char* buf, size_t n) {
  memset(&pod, 0, sizeof(pod));  // CHECK-MATCH
  memset(buf, 'x', n);  // CHECK-MATCH
  ::memset(buf, 0, n);  // CHECK-MATCH
  std::memset(buf, 0, n);  // CHECK-MATCH

  // Calls in macro expansions.
  CLEAR(pod);  // CHECK-MATCH

  // The result is used.
  void* result = memset(buf, 0, n);  // CHECK-MATCH
}

template <typename T>
void templated(T& t) {
  memset(&t, 0, sizeof(t));  // CHECK-MATCH
}

template void templated<Pod>(Pod&);
