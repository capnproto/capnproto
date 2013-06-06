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
