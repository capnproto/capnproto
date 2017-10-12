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

#include "string.h"
#include <kj/compat/gtest.h>
#include <string>
#include "vector.h"

namespace kj {
namespace _ {  // private
namespace {

TEST(String, Str) {
  EXPECT_EQ("foobar", str("foo", "bar"));
  EXPECT_EQ("1 2 3 4", str(1, " ", 2u, " ", 3l, " ", 4ll));
  EXPECT_EQ("1.5 foo 1e15 bar -3", str(1.5f, " foo ", 1e15, " bar ", -3));
  EXPECT_EQ("foo", str('f', 'o', 'o'));
  EXPECT_EQ("123 234 -123 e7",
            str((int8_t)123, " ", (uint8_t)234, " ", (int8_t)-123, " ", hex((uint8_t)0xe7)));

  char buf[3] = {'f', 'o', 'o'};
  ArrayPtr<char> a = buf;
  ArrayPtr<const char> ca = a;
  Vector<char> v;
  v.addAll(a);
  FixedArray<char, 3> f;
  memcpy(f.begin(), buf, 3);

  EXPECT_EQ("foo", str(a));
  EXPECT_EQ("foo", str(ca));
  EXPECT_EQ("foo", str(v));
  EXPECT_EQ("foo", str(f));
  EXPECT_EQ("foo", str(mv(a)));
  EXPECT_EQ("foo", str(mv(ca)));
  EXPECT_EQ("foo", str(mv(v)));
  EXPECT_EQ("foo", str(mv(f)));
}

TEST(String, Nullptr) {
  EXPECT_EQ(String(nullptr), "");
  EXPECT_EQ(StringPtr(String(nullptr)).size(), 0u);
  EXPECT_EQ(StringPtr(String(nullptr))[0], '\0');
}

TEST(String, StartsEndsWith) {
  EXPECT_TRUE(StringPtr("foobar").startsWith("foo"));
  EXPECT_FALSE(StringPtr("foobar").startsWith("bar"));
  EXPECT_FALSE(StringPtr("foobar").endsWith("foo"));
  EXPECT_TRUE(StringPtr("foobar").endsWith("bar"));

  EXPECT_FALSE(StringPtr("fo").startsWith("foo"));
  EXPECT_FALSE(StringPtr("fo").endsWith("foo"));

  EXPECT_TRUE(StringPtr("foobar").startsWith(""));
  EXPECT_TRUE(StringPtr("foobar").endsWith(""));
}

TEST(String, parseAs) {
  EXPECT_EQ(StringPtr("0").parseAs<double>(), 0.0);
  EXPECT_EQ(StringPtr("0.0").parseAs<double>(), 0.0);
  EXPECT_EQ(StringPtr("1").parseAs<double>(), 1.0);
  EXPECT_EQ(StringPtr("1.0").parseAs<double>(), 1.0);
  EXPECT_EQ(StringPtr("1e100").parseAs<double>(), 1e100);
  EXPECT_EQ(StringPtr("inf").parseAs<double>(), inf());
  EXPECT_EQ(StringPtr("infinity").parseAs<double>(), inf());
  EXPECT_EQ(StringPtr("INF").parseAs<double>(), inf());
  EXPECT_EQ(StringPtr("INFINITY").parseAs<double>(), inf());
  EXPECT_EQ(StringPtr("1e100000").parseAs<double>(), inf());
  EXPECT_EQ(StringPtr("-inf").parseAs<double>(), -inf());
  EXPECT_EQ(StringPtr("-infinity").parseAs<double>(), -inf());
  EXPECT_EQ(StringPtr("-INF").parseAs<double>(), -inf());
  EXPECT_EQ(StringPtr("-INFINITY").parseAs<double>(), -inf());
  EXPECT_EQ(StringPtr("-1e100000").parseAs<double>(), -inf());
  EXPECT_TRUE(isNaN(StringPtr("nan").parseAs<double>()));
  EXPECT_TRUE(isNaN(StringPtr("NAN").parseAs<double>()));
  EXPECT_TRUE(isNaN(StringPtr("NaN").parseAs<double>()));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("").parseAs<double>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("a").parseAs<double>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("1a").parseAs<double>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("+-1").parseAs<double>());

  EXPECT_EQ(StringPtr("1").parseAs<float>(), 1.0);

  EXPECT_EQ(StringPtr("1").parseAs<int64_t>(), 1);
  EXPECT_EQ(StringPtr("9223372036854775807").parseAs<int64_t>(), 9223372036854775807LL);
  EXPECT_EQ(StringPtr("-9223372036854775808").parseAs<int64_t>(), -9223372036854775808ULL);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("9223372036854775808").parseAs<int64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("-9223372036854775809").parseAs<int64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("").parseAs<int64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("a").parseAs<int64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("1a").parseAs<int64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("+-1").parseAs<int64_t>());
  EXPECT_EQ(StringPtr("010").parseAs<int64_t>(), 10);
  EXPECT_EQ(StringPtr("0010").parseAs<int64_t>(), 10);
  EXPECT_EQ(StringPtr("0x10").parseAs<int64_t>(), 16);
  EXPECT_EQ(StringPtr("0X10").parseAs<int64_t>(), 16);
  EXPECT_EQ(StringPtr("-010").parseAs<int64_t>(), -10);
  EXPECT_EQ(StringPtr("-0x10").parseAs<int64_t>(), -16);
  EXPECT_EQ(StringPtr("-0X10").parseAs<int64_t>(), -16);

  EXPECT_EQ(StringPtr("1").parseAs<uint64_t>(), 1);
  EXPECT_EQ(StringPtr("0").parseAs<uint64_t>(), 0);
  EXPECT_EQ(StringPtr("18446744073709551615").parseAs<uint64_t>(), 18446744073709551615ULL);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("-1").parseAs<uint64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("18446744073709551616").parseAs<uint64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("").parseAs<uint64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("a").parseAs<uint64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("1a").parseAs<uint64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", StringPtr("+-1").parseAs<uint64_t>());

  EXPECT_EQ(StringPtr("1").parseAs<int32_t>(), 1);
  EXPECT_EQ(StringPtr("2147483647").parseAs<int32_t>(), 2147483647);
  EXPECT_EQ(StringPtr("-2147483648").parseAs<int32_t>(), -2147483648);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("2147483648").parseAs<int32_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("-2147483649").parseAs<int32_t>());

  EXPECT_EQ(StringPtr("1").parseAs<uint32_t>(), 1);
  EXPECT_EQ(StringPtr("0").parseAs<uint32_t>(), 0U);
  EXPECT_EQ(StringPtr("4294967295").parseAs<uint32_t>(), 4294967295U);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("-1").parseAs<uint32_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", StringPtr("4294967296").parseAs<uint32_t>());

  EXPECT_EQ(StringPtr("1").parseAs<int16_t>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<uint16_t>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<int8_t>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<uint8_t>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<char>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<signed char>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<unsigned char>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<short>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<unsigned short>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<int>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<unsigned>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<long>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<unsigned long>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<long long>(), 1);
  EXPECT_EQ(StringPtr("1").parseAs<unsigned long long>(), 1);

  EXPECT_EQ(heapString("1").parseAs<int>(), 1);
}

#if KJ_COMPILER_SUPPORTS_STL_STRING_INTEROP
TEST(String, StlInterop) {
  std::string foo = "foo";
  StringPtr ptr = foo;
  EXPECT_EQ("foo", ptr);

  std::string bar = ptr;
  EXPECT_EQ("foo", bar);

  EXPECT_EQ("foo", kj::str(foo));
  EXPECT_EQ("foo", kj::heapString(foo));
}

struct Stringable {
  kj::StringPtr toString() { return "foo"; }
};

TEST(String, ToString) {
  EXPECT_EQ("foo", kj::str(Stringable()));
}
#endif

KJ_TEST("string literals with _kj suffix") {
  static constexpr StringPtr FOO = "foo"_kj;
  KJ_EXPECT(FOO == "foo", FOO);
  KJ_EXPECT(FOO[3] == 0);

  KJ_EXPECT("foo\0bar"_kj == StringPtr("foo\0bar", 7));

  static constexpr ArrayPtr<const char> ARR = "foo"_kj;
  KJ_EXPECT(ARR.size() == 3);
  KJ_EXPECT(kj::str(ARR) == "foo");
}

}  // namespace
}  // namespace _ (private)
}  // namespace kj
