// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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

#include "function.h"
#include <kj/compat/gtest.h>

namespace kj {
namespace {

TEST(Function, Lambda) {
  int i = 0;

  Function<int(int, int)> f = [&](int a, int b) { return a + b + i++; };

  EXPECT_EQ(123 + 456, f(123, 456));
  EXPECT_EQ(7 + 8 + 1, f(7, 8));
  EXPECT_EQ(9 + 2 + 2, f(2, 9));

  EXPECT_EQ(i, 3);
}

struct TestType {
  int callCount;

  TestType(int callCount = 0): callCount(callCount) {}

  ~TestType() { callCount = 1234; }
  // Make sure we catch invalid post-destruction uses.

  int foo(int a, int b) {
    return a + b + callCount++;
  }
};

TEST(Function, Method) {
  TestType obj;
  Function<int(int, int)> f = KJ_BIND_METHOD(obj, foo);
  Function<uint(uint, uint)> f2 = KJ_BIND_METHOD(obj, foo);

  EXPECT_EQ(123 + 456, f(123, 456));
  EXPECT_EQ(7 + 8 + 1, f(7, 8));
  EXPECT_EQ(9u + 2u + 2u, f2(2, 9));

  EXPECT_EQ(3, obj.callCount);

  // Bind to a temporary.
  f = KJ_BIND_METHOD(TestType(10), foo);

  EXPECT_EQ(123 + 456 + 10, f(123, 456));
  EXPECT_EQ(7 + 8 + 11, f(7, 8));
  EXPECT_EQ(9 + 2 + 12, f(2, 9));

  // Bind to a move.
  f = KJ_BIND_METHOD(kj::mv(obj), foo);
  obj.callCount = 1234;

  EXPECT_EQ(123 + 456 + 3, f(123, 456));
  EXPECT_EQ(7 + 8 + 4, f(7, 8));
  EXPECT_EQ(9 + 2 + 5, f(2, 9));
}

struct TestConstType {
  mutable int callCount;

  TestConstType(int callCount = 0): callCount(callCount) {}

  ~TestConstType() { callCount = 1234; }
  // Make sure we catch invalid post-destruction uses.

  int foo(int a, int b) const {
    return a + b + callCount++;
  }
};

TEST(ConstFunction, Method) {
  TestConstType obj;
  ConstFunction<int(int, int)> f = KJ_BIND_METHOD(obj, foo);
  ConstFunction<uint(uint, uint)> f2 = KJ_BIND_METHOD(obj, foo);

  EXPECT_EQ(123 + 456, f(123, 456));
  EXPECT_EQ(7 + 8 + 1, f(7, 8));
  EXPECT_EQ(9u + 2u + 2u, f2(2, 9));

  EXPECT_EQ(3, obj.callCount);

  // Bind to a temporary.
  f = KJ_BIND_METHOD(TestConstType(10), foo);

  EXPECT_EQ(123 + 456 + 10, f(123, 456));
  EXPECT_EQ(7 + 8 + 11, f(7, 8));
  EXPECT_EQ(9 + 2 + 12, f(2, 9));

  // Bind to a move.
  f = KJ_BIND_METHOD(kj::mv(obj), foo);
  obj.callCount = 1234;

  EXPECT_EQ(123 + 456 + 3, f(123, 456));
  EXPECT_EQ(7 + 8 + 4, f(7, 8));
  EXPECT_EQ(9 + 2 + 5, f(2, 9));
}

} // namespace
} // namespace kj
