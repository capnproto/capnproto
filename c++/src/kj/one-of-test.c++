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

#include "one-of.h"
#include "string.h"
#include <kj/compat/gtest.h>

namespace kj {

TEST(OneOf, Basic) {
  OneOf<int, float, String> var;

  EXPECT_FALSE(var.is<int>());
  EXPECT_FALSE(var.is<float>());
  EXPECT_FALSE(var.is<String>());
  EXPECT_TRUE(var.tryGet<int>() == nullptr);
  EXPECT_TRUE(var.tryGet<float>() == nullptr);
  EXPECT_TRUE(var.tryGet<String>() == nullptr);

  var.init<int>(123);

  EXPECT_TRUE(var.is<int>());
  EXPECT_FALSE(var.is<float>());
  EXPECT_FALSE(var.is<String>());

  EXPECT_EQ(123, var.get<int>());
#if !KJ_NO_EXCEPTIONS && defined(KJ_DEBUG)
  EXPECT_ANY_THROW(var.get<float>());
  EXPECT_ANY_THROW(var.get<String>());
#endif

  EXPECT_EQ(123, KJ_ASSERT_NONNULL(var.tryGet<int>()));
  EXPECT_TRUE(var.tryGet<float>() == nullptr);
  EXPECT_TRUE(var.tryGet<String>() == nullptr);

  var.init<String>(kj::str("foo"));

  EXPECT_FALSE(var.is<int>());
  EXPECT_FALSE(var.is<float>());
  EXPECT_TRUE(var.is<String>());

  EXPECT_EQ("foo", var.get<String>());

  EXPECT_TRUE(var.tryGet<int>() == nullptr);
  EXPECT_TRUE(var.tryGet<float>() == nullptr);
  EXPECT_EQ("foo", KJ_ASSERT_NONNULL(var.tryGet<String>()));

  OneOf<int, float, String> var2 = kj::mv(var);
  EXPECT_EQ("", var.get<String>());
  EXPECT_EQ("foo", var2.get<String>());

  var = kj::mv(var2);
  EXPECT_EQ("foo", var.get<String>());
  EXPECT_EQ("", var2.get<String>());

  if (false) {
    var.allHandled<3>();
    // var.allHandled<2>();  // doesn't compile
  }
}

TEST(OneOf, Copy) {
  OneOf<int, float, const char*> var;

  OneOf<int, float, const char*> var2 = var;
  EXPECT_FALSE(var2.is<int>());
  EXPECT_FALSE(var2.is<float>());
  EXPECT_FALSE(var2.is<const char*>());

  var.init<int>(123);

  var2 = var;
  EXPECT_TRUE(var2.is<int>());
  EXPECT_EQ(123, var2.get<int>());

  var.init<const char*>("foo");

  var2 = var;
  EXPECT_TRUE(var2.is<const char*>());
  EXPECT_STREQ("foo", var2.get<const char*>());
}

TEST(OneOf, Switch) {
  OneOf<int, float, const char*> var;
  var = "foo";
  uint count = 0;

  {
    KJ_SWITCH_ONEOF(var) {
      KJ_CASE_ONEOF(i, int) {
        KJ_FAIL_ASSERT("expected char*, got int", i);
      }
      KJ_CASE_ONEOF(s, const char*) {
        KJ_EXPECT(kj::StringPtr(s) == "foo");
        ++count;
      }
      KJ_CASE_ONEOF(n, float) {
        KJ_FAIL_ASSERT("expected char*, got float", n);
      }
    }
  }

  KJ_EXPECT(count == 1);

  {
    KJ_SWITCH_ONEOF(kj::cp(var)) {
      KJ_CASE_ONEOF(i, int) {
        KJ_FAIL_ASSERT("expected char*, got int", i);
      }
      KJ_CASE_ONEOF(s, const char*) {
        KJ_EXPECT(kj::StringPtr(s) == "foo");
      }
      KJ_CASE_ONEOF(n, float) {
        KJ_FAIL_ASSERT("expected char*, got float", n);
      }
    }
  }

  {
    // At one time this failed to compile.
    const auto& constVar = var;
    KJ_SWITCH_ONEOF(constVar) {
      KJ_CASE_ONEOF(i, int) {
        KJ_FAIL_ASSERT("expected char*, got int", i);
      }
      KJ_CASE_ONEOF(s, const char*) {
        KJ_EXPECT(kj::StringPtr(s) == "foo");
      }
      KJ_CASE_ONEOF(n, float) {
        KJ_FAIL_ASSERT("expected char*, got float", n);
      }
    }
  }
}

}  // namespace kj
