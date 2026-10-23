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

using Buffer = kj::byte[16];

#define SCRATCH(name) char name[16]

void locals() {
  int ints[3];  // CHECK-MATCH
  char chars[16];  // CHECK-MATCH
  kj::byte bytes[4096];  // CHECK-MATCH
  void* ptrs[16];  // CHECK-MATCH
  int matrix[2][3];  // CHECK-MATCH
  alignas(8) char aligned[64];  // CHECK-MATCH
  const char* strs[2];  // CHECK-MATCH
  int a[2], b[2];  // CHECK-MATCH

  // Array types hidden behind aliases.
  Buffer buffer;  // CHECK-MATCH

  // Declarations in macro expansions.
  SCRATCH(scratch);  // CHECK-MATCH

  // Every use of KJ_STACK_ARRAY_UNINITIALIZED needs a NOLINT justification.
  KJ_STACK_ARRAY_UNINITIALIZED(int, stackArray, ints[0], 16, 32);  // CHECK-MATCH

  // Nested scopes.
  if (ints[0] == 0) {
    int nested[2];  // CHECK-MATCH
  }
  for (int i = 0; i < 2; i++) {
    int inLoop[2];  // CHECK-MATCH
  }

  // Lambda bodies.
  auto lambda = []() {
    int inLambda[2];  // CHECK-MATCH
  };

  // Initialized arrays in a positive test must not be reported.
  int initialized[3]{};
}

struct Class {
  void method() {
    int inMethod[4];  // CHECK-MATCH
  }
};

// Templates are checked as written, so arrays of a dependent element type are reported even
// if every instantiation uses a class type; `{}` is correct for any element type.
template <typename T>
void templated() {
  T items[3];  // CHECK-MATCH
  int ints[3];  // CHECK-MATCH
}

template void templated<int>();
