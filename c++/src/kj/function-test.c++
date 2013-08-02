// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "function.h"
#include <gtest/gtest.h>

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

} // namespace
} // namespace kj
