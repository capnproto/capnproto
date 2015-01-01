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

#include "common.h"
#include "test.h"

namespace kj {
namespace _ {
namespace {

KJ_TEST("GlobFilter") {
  {
    GlobFilter filter("foo");

    KJ_EXPECT(filter.matches("foo"));
    KJ_EXPECT(!filter.matches("bar"));
    KJ_EXPECT(!filter.matches("foob"));
    KJ_EXPECT(!filter.matches("foobbb"));
    KJ_EXPECT(!filter.matches("fobbbb"));
    KJ_EXPECT(!filter.matches("bfoo"));
    KJ_EXPECT(!filter.matches("bbbbbfoo"));
    KJ_EXPECT(filter.matches("bbbbb/foo"));
    KJ_EXPECT(filter.matches("bar/baz/foo"));
  }

  {
    GlobFilter filter("foo*");

    KJ_EXPECT(filter.matches("foo"));
    KJ_EXPECT(!filter.matches("bar"));
    KJ_EXPECT(filter.matches("foob"));
    KJ_EXPECT(filter.matches("foobbb"));
    KJ_EXPECT(!filter.matches("fobbbb"));
    KJ_EXPECT(!filter.matches("bfoo"));
    KJ_EXPECT(!filter.matches("bbbbbfoo"));
    KJ_EXPECT(filter.matches("bbbbb/foo"));
    KJ_EXPECT(filter.matches("bar/baz/foo"));
  }

  {
    GlobFilter filter("foo*bar");

    KJ_EXPECT(filter.matches("foobar"));
    KJ_EXPECT(filter.matches("fooxbar"));
    KJ_EXPECT(filter.matches("fooxxxbar"));
    KJ_EXPECT(!filter.matches("foo/bar"));
    KJ_EXPECT(filter.matches("blah/fooxxxbar"));
    KJ_EXPECT(!filter.matches("blah/xxfooxxxbar"));
  }

  {
    GlobFilter filter("foo?bar");

    KJ_EXPECT(!filter.matches("foobar"));
    KJ_EXPECT(filter.matches("fooxbar"));
    KJ_EXPECT(!filter.matches("fooxxxbar"));
    KJ_EXPECT(!filter.matches("foo/bar"));
    KJ_EXPECT(filter.matches("blah/fooxbar"));
    KJ_EXPECT(!filter.matches("blah/xxfooxbar"));
  }
}

}  // namespace
}  // namespace _
}  // namespace kj
