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

#include <kj/array.h>

void sliceZero(kj::ArrayPtr<int> ptr, kj::ArrayPtr<const char> constPtr,
               const kj::ArrayPtr<int>& constRef, kj::Array<int>& array, size_t n) {
  ptr.slice(0, 2);  // CHECK-MATCH
  ptr.slice(0, n);  // CHECK-MATCH
  ptr.slice(0u, n);  // CHECK-MATCH
  ptr.slice((0), n);  // CHECK-MATCH
  constPtr.slice(0, n);  // CHECK-MATCH
  constRef.slice(0, n);  // CHECK-MATCH
  array.asPtr().slice(0, n);  // CHECK-MATCH
  ptr.slice(0, n).slice(0, 1);  // CHECK-MATCH

  // Multi-line calls are reported where the expression starts.
  ptr  // CHECK-MATCH
      .slice(0, n);

  // Non-matching calls in a positive test must not be reported.
  ptr.slice(1, n);
}
