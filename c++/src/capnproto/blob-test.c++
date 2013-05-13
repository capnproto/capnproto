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

namespace capnproto {
namespace {

TEST(Blob, Text) {
  std::string str = "foo";
  Text::Reader text = str;

  EXPECT_EQ("foo", text);
  EXPECT_STREQ("foo", text.c_str());
  EXPECT_STREQ("foo", text.data());
  EXPECT_EQ(3u, text.size());

  std::string str2 = text.as<std::string>();
  EXPECT_EQ("foo", str2);

  Text::Reader text2 = "bar";
  EXPECT_EQ("bar", text2);

  char c[4] = "baz";
  Text::Reader text3(c);
  EXPECT_EQ("baz", text3);

  Text::Builder builder(c, 3);
  EXPECT_EQ("baz", builder);

  EXPECT_EQ(Data::Reader("az"), builder.slice(1, 3));
}

TEST(Blob, Data) {
  std::string str = "foo";
  Data::Reader data = str;

  EXPECT_EQ("foo", data);
  EXPECT_EQ(3u, data.size());

  std::string str2 = data.as<std::string>();
  EXPECT_EQ("foo", str2);

  Data::Reader data2 = "bar";
  EXPECT_EQ("bar", data2.as<std::string>());

  char c[4] = "baz";
  Data::Reader data3(c);
  EXPECT_EQ("baz", data3.as<std::string>());

  Data::Builder builder(c, 3);
  EXPECT_EQ("baz", builder.as<std::string>());

  EXPECT_EQ(Data::Reader("az"), builder.slice(1, 3));
}

TEST(Blob, Compare) {
  EXPECT_TRUE (Data::Reader("foo") == Data::Reader("foo"));
  EXPECT_FALSE(Data::Reader("foo") != Data::Reader("foo"));
  EXPECT_TRUE (Data::Reader("foo") <= Data::Reader("foo"));
  EXPECT_TRUE (Data::Reader("foo") >= Data::Reader("foo"));
  EXPECT_FALSE(Data::Reader("foo") <  Data::Reader("foo"));
  EXPECT_FALSE(Data::Reader("foo") >  Data::Reader("foo"));

  EXPECT_FALSE(Data::Reader("foo") == Data::Reader("bar"));
  EXPECT_TRUE (Data::Reader("foo") != Data::Reader("bar"));
  EXPECT_FALSE(Data::Reader("foo") <= Data::Reader("bar"));
  EXPECT_TRUE (Data::Reader("foo") >= Data::Reader("bar"));
  EXPECT_FALSE(Data::Reader("foo") <  Data::Reader("bar"));
  EXPECT_TRUE (Data::Reader("foo") >  Data::Reader("bar"));

  EXPECT_FALSE(Data::Reader("bar") == Data::Reader("foo"));
  EXPECT_TRUE (Data::Reader("bar") != Data::Reader("foo"));
  EXPECT_TRUE (Data::Reader("bar") <= Data::Reader("foo"));
  EXPECT_FALSE(Data::Reader("bar") >= Data::Reader("foo"));
  EXPECT_TRUE (Data::Reader("bar") <  Data::Reader("foo"));
  EXPECT_FALSE(Data::Reader("bar") >  Data::Reader("foo"));

  EXPECT_FALSE(Data::Reader("foobar") == Data::Reader("foo"));
  EXPECT_TRUE (Data::Reader("foobar") != Data::Reader("foo"));
  EXPECT_FALSE(Data::Reader("foobar") <= Data::Reader("foo"));
  EXPECT_TRUE (Data::Reader("foobar") >= Data::Reader("foo"));
  EXPECT_FALSE(Data::Reader("foobar") <  Data::Reader("foo"));
  EXPECT_TRUE (Data::Reader("foobar") >  Data::Reader("foo"));

  EXPECT_FALSE(Data::Reader("foo") == Data::Reader("foobar"));
  EXPECT_TRUE (Data::Reader("foo") != Data::Reader("foobar"));
  EXPECT_TRUE (Data::Reader("foo") <= Data::Reader("foobar"));
  EXPECT_FALSE(Data::Reader("foo") >= Data::Reader("foobar"));
  EXPECT_TRUE (Data::Reader("foo") <  Data::Reader("foobar"));
  EXPECT_FALSE(Data::Reader("foo") >  Data::Reader("foobar"));
}

}  // namespace
}  // namespace capnproto
