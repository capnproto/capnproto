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

#include "blob.h"
#include <kj/compat/gtest.h>
#include <iostream>
#include <string>
#include "test-util.h"

// TODO(test):  This test is outdated -- it predates the retrofit of Text and Data on top of
//   kj::ArrayPtr/kj::StringPtr.  Clean it up.

namespace capnp {
namespace {

TEST(Blob, Text) {
  std::string str = "foo";
  Text::Reader text = str.c_str();

  EXPECT_EQ("foo", text);
  EXPECT_STREQ("foo", text.cStr());
  EXPECT_STREQ("foo", text.begin());
  EXPECT_EQ(3u, text.size());

  Text::Reader text2 = "bar";
  EXPECT_EQ("bar", text2);

  char c[4] = "baz";
  Text::Reader text3(c);
  EXPECT_EQ("baz", text3);

  Text::Builder builder(c, 3);
  EXPECT_EQ("baz", builder);

  EXPECT_EQ(kj::arrayPtr("az", 2), builder.slice(1, 3));
}

Data::Reader dataLit(const char* str) {
  return Data::Reader(reinterpret_cast<const byte*>(str), strlen(str));
}

TEST(Blob, Data) {
  Data::Reader data = dataLit("foo");

  EXPECT_EQ(dataLit("foo"), data);
  EXPECT_EQ(3u, data.size());

  Data::Reader data2 = dataLit("bar");
  EXPECT_EQ(dataLit("bar"), data2);

  byte c[4] = "baz";
  Data::Reader data3(c, 3);
  EXPECT_EQ(dataLit("baz"), data3);

  Data::Builder builder(c, 3);
  EXPECT_EQ(dataLit("baz"), builder);

  EXPECT_EQ(dataLit("az"), builder.slice(1, 3));
}

TEST(Blob, Compare) {
  EXPECT_TRUE (Text::Reader("foo") == Text::Reader("foo"));
  EXPECT_FALSE(Text::Reader("foo") != Text::Reader("foo"));
  EXPECT_TRUE (Text::Reader("foo") <= Text::Reader("foo"));
  EXPECT_TRUE (Text::Reader("foo") >= Text::Reader("foo"));
  EXPECT_FALSE(Text::Reader("foo") <  Text::Reader("foo"));
  EXPECT_FALSE(Text::Reader("foo") >  Text::Reader("foo"));

  EXPECT_FALSE(Text::Reader("foo") == Text::Reader("bar"));
  EXPECT_TRUE (Text::Reader("foo") != Text::Reader("bar"));
  EXPECT_FALSE(Text::Reader("foo") <= Text::Reader("bar"));
  EXPECT_TRUE (Text::Reader("foo") >= Text::Reader("bar"));
  EXPECT_FALSE(Text::Reader("foo") <  Text::Reader("bar"));
  EXPECT_TRUE (Text::Reader("foo") >  Text::Reader("bar"));

  EXPECT_FALSE(Text::Reader("bar") == Text::Reader("foo"));
  EXPECT_TRUE (Text::Reader("bar") != Text::Reader("foo"));
  EXPECT_TRUE (Text::Reader("bar") <= Text::Reader("foo"));
  EXPECT_FALSE(Text::Reader("bar") >= Text::Reader("foo"));
  EXPECT_TRUE (Text::Reader("bar") <  Text::Reader("foo"));
  EXPECT_FALSE(Text::Reader("bar") >  Text::Reader("foo"));

  EXPECT_FALSE(Text::Reader("foobar") == Text::Reader("foo"));
  EXPECT_TRUE (Text::Reader("foobar") != Text::Reader("foo"));
  EXPECT_FALSE(Text::Reader("foobar") <= Text::Reader("foo"));
  EXPECT_TRUE (Text::Reader("foobar") >= Text::Reader("foo"));
  EXPECT_FALSE(Text::Reader("foobar") <  Text::Reader("foo"));
  EXPECT_TRUE (Text::Reader("foobar") >  Text::Reader("foo"));

  EXPECT_FALSE(Text::Reader("foo") == Text::Reader("foobar"));
  EXPECT_TRUE (Text::Reader("foo") != Text::Reader("foobar"));
  EXPECT_TRUE (Text::Reader("foo") <= Text::Reader("foobar"));
  EXPECT_FALSE(Text::Reader("foo") >= Text::Reader("foobar"));
  EXPECT_TRUE (Text::Reader("foo") <  Text::Reader("foobar"));
  EXPECT_FALSE(Text::Reader("foo") >  Text::Reader("foobar"));
}

#if KJ_COMPILER_SUPPORTS_STL_STRING_INTEROP
TEST(Blob, StlInterop) {
  std::string foo = "foo";
  Text::Reader reader = foo;

  EXPECT_EQ("foo", reader);

  std::string bar = reader;
  EXPECT_EQ("foo", bar);
}
#endif

}  // namespace
}  // namespace capnp
