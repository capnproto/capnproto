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
#include <kj/string.h>

// Arrays with static or thread storage duration are zero-initialized.
int global[3];
extern int externGlobal[3];
static int staticGlobal[3];
namespace {
int anonymousGlobal[3];
}
thread_local int threadLocalGlobal[3];

struct Struct {
  // Fields are not variables; their initialization is the constructor's job.
  int field[3];
  static int staticMember[3];
  static constexpr int constexprMember[2] = {1, 2};
};
int Struct::staticMember[3];

// Array parameters decay to pointers.
void param(int array[3]) {}

void locals(size_t n) {
  // Explicitly initialized.
  int braces[3]{};
  int equalsBraces[3] = {};
  int values[3] = {1, 2, 3};
  int deduced[] = {1, 2};
  char str[] = "abc";
  char strSized[8] = "abc";
  constexpr int constexprArray[2] = {1, 2};
  using Buffer = kj::byte[16];
  Buffer buffer{};

  // Static and thread storage duration.
  static int staticLocal[3];
  thread_local int threadLocal[3];

  // Elements of class type are initialized by their constructors.
  kj::String strings[2];
  kj::Maybe<int> maybes[2];
  kj::ArrayPtr<int> ptrs[2];

  // Not arrays.
  int scalar;
  int* pointer;
  kj::ArrayPtr<int> arrayPtr;

  // KJ_STACK_ARRAY value-initializes its elements.
  KJ_STACK_ARRAY(int, stackArray, n, 16, 32);
  KJ_STACK_ARRAY(kj::String, stringStackArray, n, 16, 32);

  // KJ_STACK_ARRAY_UNINITIALIZED with a justification.
  // NOLINTNEXTLINE(custom-uninitialized-array): every element is written before it is read.
  KJ_STACK_ARRAY_UNINITIALIZED(int, uninitializedStackArray, n, 16, 32);
}
