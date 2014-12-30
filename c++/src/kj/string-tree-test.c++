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

#include "string-tree.h"
#include <kj/compat/gtest.h>

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
    tree.visit([&](ArrayPtr<const char> part) { ++pieceCount; EXPECT_EQ(3u, part.size()); });
    EXPECT_EQ(3u, pieceCount);
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
