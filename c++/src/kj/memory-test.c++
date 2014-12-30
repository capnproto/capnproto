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

#include "memory.h"
#include <kj/compat/gtest.h>
#include "debug.h"

namespace kj {
namespace {

TEST(Memory, OwnConst) {
  Own<int> i = heap<int>(2);
  EXPECT_EQ(2, *i);

  Own<const int> ci = mv(i);
  EXPECT_EQ(2, *ci);

  Own<const int> ci2 = heap<const int>(3);
  EXPECT_EQ(3, *ci2);
}

TEST(Memory, CanConvert) {
  struct Super { virtual ~Super() {} };
  struct Sub: public Super {};

  static_assert(canConvert<Own<Sub>, Own<Super>>(), "failure");
  static_assert(!canConvert<Own<Super>, Own<Sub>>(), "failure");
}

struct Nested {
  Nested(bool& destroyed): destroyed(destroyed) {}
  ~Nested() { destroyed = true; }

  bool& destroyed;
  Own<Nested> nested;
};

TEST(Memory, AssignNested) {
  bool destroyed1 = false, destroyed2 = false;
  auto nested = heap<Nested>(destroyed1);
  nested->nested = heap<Nested>(destroyed2);
  EXPECT_FALSE(destroyed1 || destroyed2);
  nested = kj::mv(nested->nested);
  EXPECT_TRUE(destroyed1);
  EXPECT_FALSE(destroyed2);
  nested = nullptr;
  EXPECT_TRUE(destroyed1 && destroyed2);
}

// TODO(test):  More tests.

}  // namespace
}  // namespace kj
