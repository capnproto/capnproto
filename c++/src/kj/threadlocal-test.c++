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

#include "threadlocal.h"
#include "debug.h"
#include "thread.h"
#include <kj/compat/gtest.h>

namespace kj {
namespace {

KJ_THREADLOCAL_PTR(uint) tls1 = nullptr;
KJ_THREADLOCAL_PTR(uint) tls2;

TEST(ThreadLocal, Basic) {
  // Verify that both started out null.
  uint* p = tls1;
  EXPECT_EQ(nullptr, p);
  p = tls2;
  EXPECT_EQ(nullptr, p);

  // Set tls1, then verify that only tls1 changed, not tls2.
  uint i = 123;
  tls1 = &i;

  p = tls1;
  EXPECT_EQ(&i, p);
  p = tls2;
  EXPECT_EQ(nullptr, p);

  // Check that in another thread, tls1 starts null but can be changed.
  uint j = 456;
  bool threadDone = false;
  Thread([&]() {
    p = tls1;
    EXPECT_EQ(nullptr, p);
    tls1 = &j;

    p = tls1;
    EXPECT_EQ(&j, p);
    threadDone = true;
  });
  EXPECT_TRUE(threadDone);

  // tls1 didn't change in this thread.
  p = tls1;
  EXPECT_EQ(&i, p);
}

}  // namespace
}  // namespace kj
