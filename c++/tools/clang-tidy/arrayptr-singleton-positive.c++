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

struct Struct {
  int field;
};

void singleton(int value, Struct s, int* ptr) {
  int ints[3] = {1, 2, 3};

  kj::arrayPtr(&value, 1);  // CHECK-MATCH
  kj::arrayPtr((&value), 1);  // CHECK-MATCH
  kj::arrayPtr(&s, 1);  // CHECK-MATCH
  kj::arrayPtr(&s.field, 1);  // CHECK-MATCH
  kj::arrayPtr(&ints[1], 1);  // CHECK-MATCH
  kj::arrayPtr(&*ptr, 1);  // CHECK-MATCH
  auto bytes = kj::arrayPtr(&value, 1).asBytes();  // CHECK-MATCH

  // Non-matching calls in a positive test must not be reported.
  kj::arrayPtr(value);
}
