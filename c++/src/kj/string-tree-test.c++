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

#include "string-tree.h"
#include <gtest/gtest.h>

namespace kj {
namespace _ {  // private
namespace {

TEST(StringTree, StrTree) {
  EXPECT_EQ("foobar", strTree("foo", "bar").flatten());
  EXPECT_EQ("1 2 3 4", strTree(1, " ", 2u, " ", 3l, " ", 4ll).flatten());
  EXPECT_EQ("1.5 foo 1e15 bar -3", strTree(1.5f, " foo ", 1e15, " bar ", -3).flatten());
  EXPECT_EQ("foo", strTree('f', 'o', 'o').flatten());

  {
    StringTree tree = strTree(strTree(str("foo"), str("bar")), "baz");
    EXPECT_EQ("foobarbaz", tree.flatten());

    uint pieceCount = 0;
    tree.visit([&](ArrayPtr<const char> part) { ++pieceCount; EXPECT_EQ(3, part.size()); });
    EXPECT_EQ(3, pieceCount);
  }

  EXPECT_EQ("<foobarbaz>", str('<', strTree(str("foo"), "bar", str("baz")), '>'));
}

TEST(StringTree, DelimitedArray) {
  Array<StringTree> arr = heapArray<StringTree>(4);
  arr[0] = strTree("foo");
  arr[1] = strTree("bar");
  arr[2] = strTree("baz");
  arr[3] = strTree("qux");

  EXPECT_EQ("foo, bar, baz, qux", StringTree(kj::mv(arr), ", ").flatten());
}

}  // namespace
}  // namespace _ (private)
}  // namespace kj
