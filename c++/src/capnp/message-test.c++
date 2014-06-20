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

#include "message.h"
#include <gtest/gtest.h>

namespace capnp {
namespace _ {  // private
namespace {

TEST(Message, MallocBuilderWithFirstSegment) {
  word scratch[16];
  memset(scratch, 0, sizeof(scratch));
  MallocMessageBuilder builder(kj::arrayPtr(scratch, 16), AllocationStrategy::FIXED_SIZE);

  kj::ArrayPtr<word> segment = builder.allocateSegment(1);
  EXPECT_EQ(scratch, segment.begin());
  EXPECT_EQ(16u, segment.size());

  segment = builder.allocateSegment(1);
  EXPECT_NE(scratch, segment.begin());
  EXPECT_EQ(16u, segment.size());

  segment = builder.allocateSegment(1);
  EXPECT_NE(scratch, segment.begin());
  EXPECT_EQ(16u, segment.size());
}

// TODO(test):  More tests.

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
