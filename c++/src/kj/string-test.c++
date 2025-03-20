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
#include <locale.h>
#include <stdint.h>

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
  EXPECT_EQ("-128 -32768 -2147483648 -9223372036854775808",
      str((signed char)-128, ' ', (signed short)-32768, ' ',
          ((int)-2147483647) - 1, ' ', ((long long)-9223372036854775807ll) - 1))
  EXPECT_EQ("ff ffff ffffffff ffffffffffffffff",
      str(hex((uint8_t)0xff), ' ', hex((uint16_t)0xffff), ' ', hex((uint32_t)0xffffffffu), ' ',
          hex((uint64_t)0xffffffffffffffffull)));

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
  EXPECT_TRUE("foobar"_kj.startsWith("foo"));
  EXPECT_FALSE("foobar"_kj.startsWith("bar"));
  EXPECT_FALSE("foobar"_kj.endsWith("foo"));
  EXPECT_TRUE("foobar"_kj.endsWith("bar"));

  EXPECT_FALSE("fo"_kj.startsWith("foo"));
  EXPECT_FALSE("fo"_kj.endsWith("foo"));

  EXPECT_TRUE("foobar"_kj.startsWith(""));
  EXPECT_TRUE("foobar"_kj.endsWith(""));
}

TEST(String, parseAs) {
  EXPECT_EQ("0"_kj.parseAs<double>(), 0.0);
  EXPECT_EQ("0.0"_kj.parseAs<double>(), 0.0);
  EXPECT_EQ("1"_kj.parseAs<double>(), 1.0);
  EXPECT_EQ("1.0"_kj.parseAs<double>(), 1.0);
  EXPECT_EQ("1e100"_kj.parseAs<double>(), 1e100);
  EXPECT_EQ("inf"_kj.parseAs<double>(), inf());
  EXPECT_EQ("infinity"_kj.parseAs<double>(), inf());
  EXPECT_EQ("INF"_kj.parseAs<double>(), inf());
  EXPECT_EQ("INFINITY"_kj.parseAs<double>(), inf());
  EXPECT_EQ("1e100000"_kj.parseAs<double>(), inf());
  EXPECT_EQ("-inf"_kj.parseAs<double>(), -inf());
  EXPECT_EQ("-infinity"_kj.parseAs<double>(), -inf());
  EXPECT_EQ("-INF"_kj.parseAs<double>(), -inf());
  EXPECT_EQ("-INFINITY"_kj.parseAs<double>(), -inf());
  EXPECT_EQ("-1e100000"_kj.parseAs<double>(), -inf());
  EXPECT_TRUE(isNaN("nan"_kj.parseAs<double>()));
  EXPECT_TRUE(isNaN("NAN"_kj.parseAs<double>()));
  EXPECT_TRUE(isNaN("NaN"_kj.parseAs<double>()));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", ""_kj.parseAs<double>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", "a"_kj.parseAs<double>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", "1a"_kj.parseAs<double>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", "+-1"_kj.parseAs<double>());

  EXPECT_EQ("1"_kj.parseAs<float>(), 1.0);

  EXPECT_EQ("1"_kj.parseAs<int64_t>(), 1);
  EXPECT_EQ("9223372036854775807"_kj.parseAs<int64_t>(), 9223372036854775807LL);
  EXPECT_EQ("-9223372036854775808"_kj.parseAs<int64_t>(), -9223372036854775808ULL);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", "9223372036854775808"_kj.parseAs<int64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", "-9223372036854775809"_kj.parseAs<int64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", ""_kj.parseAs<int64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", "a"_kj.parseAs<int64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", "1a"_kj.parseAs<int64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", "+-1"_kj.parseAs<int64_t>());
  EXPECT_EQ("010"_kj.parseAs<int64_t>(), 10);
  EXPECT_EQ("0010"_kj.parseAs<int64_t>(), 10);
  EXPECT_EQ("0x10"_kj.parseAs<int64_t>(), 16);
  EXPECT_EQ("0X10"_kj.parseAs<int64_t>(), 16);
  EXPECT_EQ("-010"_kj.parseAs<int64_t>(), -10);
  EXPECT_EQ("-0x10"_kj.parseAs<int64_t>(), -16);
  EXPECT_EQ("-0X10"_kj.parseAs<int64_t>(), -16);

  EXPECT_EQ("1"_kj.parseAs<uint64_t>(), 1);
  EXPECT_EQ("0"_kj.parseAs<uint64_t>(), 0);
  EXPECT_EQ("18446744073709551615"_kj.parseAs<uint64_t>(), 18446744073709551615ULL);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", "-1"_kj.parseAs<uint64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", "18446744073709551616"_kj.parseAs<uint64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", ""_kj.parseAs<uint64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", "a"_kj.parseAs<uint64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", "1a"_kj.parseAs<uint64_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("not contain valid", "+-1"_kj.parseAs<uint64_t>());

  EXPECT_EQ("1"_kj.parseAs<int32_t>(), 1);
  EXPECT_EQ("2147483647"_kj.parseAs<int32_t>(), 2147483647);
  EXPECT_EQ("-2147483648"_kj.parseAs<int32_t>(), -2147483648);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", "2147483648"_kj.parseAs<int32_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", "-2147483649"_kj.parseAs<int32_t>());

  EXPECT_EQ("1"_kj.parseAs<uint32_t>(), 1);
  EXPECT_EQ("0"_kj.parseAs<uint32_t>(), 0U);
  EXPECT_EQ("4294967295"_kj.parseAs<uint32_t>(), 4294967295U);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", "-1"_kj.parseAs<uint32_t>());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("out-of-range", "4294967296"_kj.parseAs<uint32_t>());

  EXPECT_EQ("1"_kj.parseAs<int16_t>(), 1);
  EXPECT_EQ("1"_kj.parseAs<uint16_t>(), 1);
  EXPECT_EQ("1"_kj.parseAs<int8_t>(), 1);
  EXPECT_EQ("1"_kj.parseAs<uint8_t>(), 1);
  EXPECT_EQ("1"_kj.parseAs<char>(), 1);
  EXPECT_EQ("1"_kj.parseAs<signed char>(), 1);
  EXPECT_EQ("1"_kj.parseAs<unsigned char>(), 1);
  EXPECT_EQ("1"_kj.parseAs<short>(), 1);
  EXPECT_EQ("1"_kj.parseAs<unsigned short>(), 1);
  EXPECT_EQ("1"_kj.parseAs<int>(), 1);
  EXPECT_EQ("1"_kj.parseAs<unsigned>(), 1);
  EXPECT_EQ("1"_kj.parseAs<long>(), 1);
  EXPECT_EQ("1"_kj.parseAs<unsigned long>(), 1);
  EXPECT_EQ("1"_kj.parseAs<long long>(), 1);
  EXPECT_EQ("1"_kj.parseAs<unsigned long long>(), 1);

  EXPECT_EQ(heapString("1").parseAs<int>(), 1);
}

TEST(String, tryParseAs) {
  KJ_EXPECT("0"_kj.tryParseAs<double>() == 0.0);
  KJ_EXPECT("0"_kj.tryParseAs<double>() == 0.0);
  KJ_EXPECT("0.0"_kj.tryParseAs<double>() == 0.0);
  KJ_EXPECT("1"_kj.tryParseAs<double>() == 1.0);
  KJ_EXPECT("1.0"_kj.tryParseAs<double>() == 1.0);
  KJ_EXPECT("1e100"_kj.tryParseAs<double>() == 1e100);
  KJ_EXPECT("inf"_kj.tryParseAs<double>() == inf());
  KJ_EXPECT("infinity"_kj.tryParseAs<double>() == inf());
  KJ_EXPECT("INF"_kj.tryParseAs<double>() == inf());
  KJ_EXPECT("INFINITY"_kj.tryParseAs<double>() == inf());
  KJ_EXPECT("1e100000"_kj.tryParseAs<double>() == inf());
  KJ_EXPECT("-inf"_kj.tryParseAs<double>() == -inf());
  KJ_EXPECT("-infinity"_kj.tryParseAs<double>() == -inf());
  KJ_EXPECT("-INF"_kj.tryParseAs<double>() == -inf());
  KJ_EXPECT("-INFINITY"_kj.tryParseAs<double>() == -inf());
  KJ_EXPECT("-1e100000"_kj.tryParseAs<double>() == -inf());
  KJ_EXPECT(isNaN("nan"_kj.tryParseAs<double>().orDefault(0.0)) == true);
  KJ_EXPECT(isNaN("NAN"_kj.tryParseAs<double>().orDefault(0.0)) == true);
  KJ_EXPECT(isNaN("NaN"_kj.tryParseAs<double>().orDefault(0.0)) == true);
  KJ_EXPECT(""_kj.tryParseAs<double>() == kj::none);
  KJ_EXPECT("a"_kj.tryParseAs<double>() == kj::none);
  KJ_EXPECT("1a"_kj.tryParseAs<double>() == kj::none);
  KJ_EXPECT("+-1"_kj.tryParseAs<double>() == kj::none);

  KJ_EXPECT("1"_kj.tryParseAs<float>() == 1.0);

  KJ_EXPECT("1"_kj.tryParseAs<int64_t>() == 1);
  KJ_EXPECT("9223372036854775807"_kj.tryParseAs<int64_t>() == 9223372036854775807LL);
  KJ_EXPECT("-9223372036854775808"_kj.tryParseAs<int64_t>() == -9223372036854775808ULL);
  KJ_EXPECT("9223372036854775808"_kj.tryParseAs<int64_t>() == kj::none);
  KJ_EXPECT("-9223372036854775809"_kj.tryParseAs<int64_t>() == kj::none);
  KJ_EXPECT(""_kj.tryParseAs<int64_t>() == kj::none);
  KJ_EXPECT("a"_kj.tryParseAs<int64_t>() == kj::none);
  KJ_EXPECT("1a"_kj.tryParseAs<int64_t>() == kj::none);
  KJ_EXPECT("+-1"_kj.tryParseAs<int64_t>() == kj::none);
  KJ_EXPECT("010"_kj.tryParseAs<int64_t>() == 10);
  KJ_EXPECT("0010"_kj.tryParseAs<int64_t>() == 10);
  KJ_EXPECT("0x10"_kj.tryParseAs<int64_t>() == 16);
  KJ_EXPECT("0X10"_kj.tryParseAs<int64_t>() == 16);
  KJ_EXPECT("-010"_kj.tryParseAs<int64_t>() == -10);
  KJ_EXPECT("-0x10"_kj.tryParseAs<int64_t>() == -16);
  KJ_EXPECT("-0X10"_kj.tryParseAs<int64_t>() == -16);

  KJ_EXPECT("1"_kj.tryParseAs<uint64_t>() == 1);
  KJ_EXPECT("0"_kj.tryParseAs<uint64_t>() == 0);
  KJ_EXPECT("18446744073709551615"_kj.tryParseAs<uint64_t>() == 18446744073709551615ULL);
  KJ_EXPECT("-1"_kj.tryParseAs<uint64_t>() == kj::none);
  KJ_EXPECT("18446744073709551616"_kj.tryParseAs<uint64_t>() == kj::none);
  KJ_EXPECT(""_kj.tryParseAs<uint64_t>() == kj::none);
  KJ_EXPECT("a"_kj.tryParseAs<uint64_t>() == kj::none);
  KJ_EXPECT("1a"_kj.tryParseAs<uint64_t>() == kj::none);
  KJ_EXPECT("+-1"_kj.tryParseAs<uint64_t>() == kj::none);

  KJ_EXPECT("1"_kj.tryParseAs<int32_t>() == 1);
  KJ_EXPECT("2147483647"_kj.tryParseAs<int32_t>() == 2147483647);
  KJ_EXPECT("-2147483648"_kj.tryParseAs<int32_t>() == -2147483648);
  KJ_EXPECT("2147483648"_kj.tryParseAs<int32_t>() == kj::none);
  KJ_EXPECT("-2147483649"_kj.tryParseAs<int32_t>() == kj::none);

  KJ_EXPECT("1"_kj.tryParseAs<uint32_t>() == 1);
  KJ_EXPECT("0"_kj.tryParseAs<uint32_t>() == 0U);
  KJ_EXPECT("4294967295"_kj.tryParseAs<uint32_t>() == 4294967295U);
  KJ_EXPECT("-1"_kj.tryParseAs<uint32_t>() == kj::none);
  KJ_EXPECT("4294967296"_kj.tryParseAs<uint32_t>() == kj::none);

  KJ_EXPECT("1"_kj.tryParseAs<int16_t>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<uint16_t>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<int8_t>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<uint8_t>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<char>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<signed char>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<unsigned char>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<short>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<unsigned short>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<int>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<unsigned>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<long>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<unsigned long>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<long long>() == 1);
  KJ_EXPECT("1"_kj.tryParseAs<unsigned long long>() == 1);

  KJ_EXPECT(heapString("1").tryParseAs<int>() == 1);
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

KJ_TEST("StringPtr constructors") {
  KJ_EXPECT(StringPtr("") == "");
  KJ_EXPECT(StringPtr(nullptr) == "");
  KJ_EXPECT(StringPtr("abc") == "abc");
  KJ_EXPECT(StringPtr("abc", 3) == "abc");

#ifdef KJ_DEBUG
  KJ_EXPECT_THROW_MESSAGE("StringPtr must be NUL-terminated", StringPtr("abc", 2));
#endif  
}

KJ_TEST("string literals with _kj suffix") {
  static constexpr StringPtr FOO = "foo"_kj;
  KJ_EXPECT(FOO == "foo", FOO);
  KJ_EXPECT(FOO[3] == 0);

  KJ_EXPECT("foo\0bar"_kj == StringPtr("foo\0bar", 7));

  static constexpr ArrayPtr<const char> ARR = "foo"_kj;
  KJ_EXPECT(ARR.size() == 3);
  KJ_EXPECT(kj::str(ARR) == "foo");
}

KJ_TEST("kj::delimited() and kj::strPreallocated()") {
  int rawArray[] = {1, 23, 456, 78};
  ArrayPtr<int> array = rawArray;
  KJ_EXPECT(str(delimited(array, "::")) == "1::23::456::78");

  {
    char buffer[256]{};
    KJ_EXPECT(strPreallocated(buffer, delimited(array, "::"), 'x')
        == "1::23::456::78x");
    KJ_EXPECT(strPreallocated(buffer, "foo", 123, true) == "foo123true");
  }

  {
    char buffer[5]{};
    KJ_EXPECT(strPreallocated(buffer, delimited(array, "::"), 'x') == "1::2");
    KJ_EXPECT(strPreallocated(buffer, "foo", 123, true) == "foo1");
  }
}

KJ_TEST("parsing 'nan' returns canonical NaN value") {
  // There are many representations of NaN. We would prefer that parsing "NaN" produces exactly the
  // same bits that kj::nan() returns.
  {
    double parsedNan = "NaN"_kj.parseAs<double>();
    double canonicalNan = kj::nan();
    KJ_EXPECT(kj::arrayPtr(parsedNan).asBytes() == kj::arrayPtr(canonicalNan).asBytes());
  }
  {
    float parsedNan = "NaN"_kj.parseAs<float>();
    float canonicalNan = kj::nan();
    KJ_EXPECT(kj::arrayPtr(parsedNan).asBytes() == kj::arrayPtr(canonicalNan).asBytes());
  }
}

KJ_TEST("stringify array-of-array") {
  int arr1[] = {1, 23};
  int arr2[] = {456, 7890};
  ArrayPtr<int> arr3[] = {arr1, arr2};
  ArrayPtr<ArrayPtr<int>> array = arr3;

  KJ_EXPECT(str(array) == "1, 23, 456, 7890");
}

KJ_TEST("ArrayPtr == StringPtr") {
  StringPtr s = "foo"_kj;
  ArrayPtr<const char> a = s;

  KJ_EXPECT(a == s);
#if __cplusplus >= 202000L
  KJ_EXPECT(s == a);
#endif
}

KJ_TEST("String == String") {
  String a = kj::str("foo");
  String b = kj::str("foo");
  String c = kj::str("bar");

  // We're trying to trigger the -Wambiguous-reversed-operator warning in Clang, but it seems
  // magic asserts inadvertently squelch it. So, we use an alternate macro with no magic.
#define KJ_EXPECT_NOMAGIC(cond) \
    if (cond) {} else { KJ_FAIL_ASSERT("expected " #cond); }

  KJ_EXPECT_NOMAGIC(a == a);
  KJ_EXPECT_NOMAGIC(a == b);
  KJ_EXPECT_NOMAGIC(a != c);
}

KJ_TEST("float stringification and parsing is not locale-dependent") {
  // Remember the old locale, set it back when we're done.
  char* oldLocaleCstr = setlocale(LC_NUMERIC, nullptr);
  KJ_ASSERT(oldLocaleCstr != nullptr);
  auto oldLocale = kj::str(oldLocaleCstr);
  KJ_DEFER(setlocale(LC_NUMERIC, oldLocale.cStr()));

  // Set the locale to "C".
  KJ_ASSERT(setlocale(LC_NUMERIC, "C") != nullptr);

  KJ_ASSERT(kj::str(1.5) == "1.5");
  KJ_ASSERT(kj::str(1.5f) == "1.5");
  KJ_EXPECT("1.5"_kj.parseAs<float>() == 1.5);
  KJ_EXPECT("1.5"_kj.parseAs<double>() == 1.5);

  if (setlocale(LC_NUMERIC, "es_ES") == nullptr &&
      setlocale(LC_NUMERIC, "es_ES.utf8") == nullptr &&
      setlocale(LC_NUMERIC, "es_ES.UTF-8") == nullptr) {
    // Some systems may not have the desired locale available.
    KJ_LOG(WARNING, "Couldn't set locale to es_ES. Skipping this test.");
  } else {
    KJ_EXPECT(kj::str(1.5) == "1.5");
    KJ_EXPECT(kj::str(1.5f) == "1.5");
    KJ_EXPECT("1.5"_kj.parseAs<float>() == 1.5);
    KJ_EXPECT("1.5"_kj.parseAs<double>() == 1.5);
  }
}

KJ_TEST("ConstString literal operator") {
  kj::ConstString theString = "it's a const string!"_kjc;
  KJ_EXPECT(theString == "it's a const string!");
}

KJ_TEST("ConstString promotion") {
  kj::StringPtr theString = "it's a const string!";
  kj::ConstString constString = theString.attach();
  KJ_EXPECT(constString == "it's a const string!");
}

struct DestructionOrderRecorder {
  DestructionOrderRecorder(uint& counter, uint& recordTo)
    : counter(counter), recordTo(recordTo) {}
  ~DestructionOrderRecorder() {
    recordTo = ++counter;
  }

  uint& counter;
  uint& recordTo;
};

KJ_TEST("ConstString attachment lifetimes") {
  uint counter = 0;
  uint destroyed1 = 0;
  uint destroyed2 = 0;
  uint destroyed3 = 0;

  auto obj1 = kj::heap<DestructionOrderRecorder>(counter, destroyed1);
  auto obj2 = kj::heap<DestructionOrderRecorder>(counter, destroyed2);
  auto obj3 = kj::heap<DestructionOrderRecorder>(counter, destroyed3);

  StringPtr theString = "it's a string!";
  const char* ptr = theString.begin();

  ConstString combined = theString.attach(kj::mv(obj1), kj::mv(obj2), kj::mv(obj3));

  KJ_EXPECT(combined.begin() == ptr);

  KJ_EXPECT(obj1.get() == nullptr);
  KJ_EXPECT(obj2.get() == nullptr);
  KJ_EXPECT(obj3.get() == nullptr);
  KJ_EXPECT(destroyed1 == 0);
  KJ_EXPECT(destroyed2 == 0);
  KJ_EXPECT(destroyed3 == 0);

  combined = nullptr;

  KJ_EXPECT(destroyed1 == 1, destroyed1);
  KJ_EXPECT(destroyed2 == 2, destroyed2);
  KJ_EXPECT(destroyed3 == 3, destroyed3);
}

KJ_TEST("StringPtr find") {
  // Empty string doesn't find anything
  StringPtr empty("");
  KJ_EXPECT(empty.find("") == 0);
  KJ_EXPECT(empty.find("foo") == kj::none);

  StringPtr foobar("foobar"_kj);
  KJ_EXPECT(foobar.find("") == 0);
  KJ_EXPECT(foobar.find("baz") == kj::none);
  KJ_EXPECT(foobar.find("foobar") == 0);
  KJ_EXPECT(foobar.find("f") == 0);
  KJ_EXPECT(foobar.find("oobar") == 1);
  KJ_EXPECT(foobar.find("ar") == 4);
  KJ_EXPECT(foobar.find("o") == 1);
  KJ_EXPECT(foobar.find("oo") == 1);
  KJ_EXPECT(foobar.find("r") == 5);
  KJ_EXPECT(foobar.find("foobar!") == kj::none);

  // Self pointers shouldn't cause issues, but it's worth testing.
  KJ_EXPECT(foobar.find(foobar) == 0);
  KJ_EXPECT(foobar.find(foobar.slice(1)) == 1);
  KJ_EXPECT(foobar.slice(1).find(foobar.slice(1)) == 0);
  KJ_EXPECT(foobar.slice(2).find(foobar.slice(1)) == kj::none);
}

KJ_TEST("StringPtr contains") {
  // Empty string doesn't find anything
  StringPtr empty("");
  KJ_EXPECT(empty.contains("") == true);
  KJ_EXPECT(empty.contains("foo") == false);

  StringPtr foobar("foobar"_kj);
  KJ_EXPECT(foobar.contains("") == true);
  KJ_EXPECT(foobar.contains("baz") == false);
  KJ_EXPECT(foobar.contains("foobar") == true);
  KJ_EXPECT(foobar.contains("f") == true);
  KJ_EXPECT(foobar.contains("oobar") == true);
  KJ_EXPECT(foobar.contains("ar") == true);
  KJ_EXPECT(foobar.contains("o") == true);
  KJ_EXPECT(foobar.contains("oo") == true);
  KJ_EXPECT(foobar.contains("r") == true);
  KJ_EXPECT(foobar.contains("foobar!") == false);

  // Self pointers shouldn't cause issues, but it's worth testing.
  KJ_EXPECT(foobar.contains(foobar) == true);
  KJ_EXPECT(foobar.contains(foobar.slice(1)) == true);
  KJ_EXPECT(foobar.slice(1).contains(foobar.slice(1)) == true);
  KJ_EXPECT(foobar.slice(2).contains(foobar.slice(1)) == false);
}

struct Std {
  static std::string from(const String* str) {
    return std::string(str->cStr());
  }

  static std::string from(const StringPtr* str) {
    return std::string(str->cStr());
  }
};

KJ_TEST("as<Std>") {
  String str = kj::str("foo"_kj);
  std::string stdStr = str.as<Std>();
  KJ_EXPECT(stdStr == "foo");

  StringPtr ptr = "bar"_kj;
  std::string stdPtr = ptr.as<Std>();
  KJ_EXPECT(stdPtr == "bar");
}

// Supports constexpr
constexpr const StringPtr HELLO_WORLD = "hello world"_kj;
static_assert(HELLO_WORLD.size() == 11);
static_assert(HELLO_WORLD.startsWith("hello"_kj));
static_assert(HELLO_WORLD.endsWith("world"_kj));
static_assert(HELLO_WORLD[0] == *"h");
static_assert(HELLO_WORLD.asArray().size() == 11);
static_assert(*HELLO_WORLD.asArray().begin() == 'h');
static_assert(HELLO_WORLD.asArray().front() == 'h');
static_assert(HELLO_WORLD.first(2).size() == 2);
static_assert(HELLO_WORLD.slice(5).size() == 6);
static_assert(StringPtr().size() == 0);
static_assert(StringPtr(nullptr).size() == 0);
static_assert(StringPtr(HELLO_WORLD.begin(), HELLO_WORLD.size()).size() == 11);
static_assert(HELLO_WORLD > StringPtr());
static_assert(StringPtr("const"_kj).size() == 5);

}  // namespace
}  // namespace _ (private)
}  // namespace kj
