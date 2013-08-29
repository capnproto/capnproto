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

#include "blob.h"
#include <gtest/gtest.h>
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
