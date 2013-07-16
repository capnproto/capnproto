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
#include "dynamic.h"
#include "pretty-print.h"
#include <kj/debug.h>
#include <gtest/gtest.h>
#include "test-util.h"

namespace kj {
  inline std::ostream& operator<<(std::ostream& os, const kj::String& s) {
    return os.write(s.begin(), s.size());
  }
}

namespace capnp {
namespace _ {  // private
namespace {

TEST(Stringify, KjStringification) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  EXPECT_EQ("()", kj::str(root));

  initTestMessage(root);

  EXPECT_EQ("("
      "boolField = true, "
      "int8Field = -123, "
      "int16Field = -12345, "
      "int32Field = -12345678, "
      "int64Field = -123456789012345, "
      "uInt8Field = 234, "
      "uInt16Field = 45678, "
      "uInt32Field = 3456789012, "
      "uInt64Field = 12345678901234567890, "
      "float32Field = 1234.5, "
      "float64Field = -1.23e47, "
      "textField = \"foo\", "
      "dataField = \"bar\", "
      "structField = ("
          "boolField = true, "
          "int8Field = -12, "
          "int16Field = 3456, "
          "int32Field = -78901234, "
          "int64Field = 56789012345678, "
          "uInt8Field = 90, "
          "uInt16Field = 1234, "
          "uInt32Field = 56789012, "
          "uInt64Field = 345678901234567890, "
          "float32Field = -1.25e-10, "
          "float64Field = 345, "
          "textField = \"baz\", "
          "dataField = \"qux\", "
          "structField = ("
              "textField = \"nested\", "
              "structField = (textField = \"really nested\")), "
          "enumField = baz, "
          "voidList = [void, void, void], "
          "boolList = [false, true, false, true, true], "
          "int8List = [12, -34, -128, 127], "
          "int16List = [1234, -5678, -32768, 32767], "
          "int32List = [12345678, -90123456, -2147483648, 2147483647], "
          "int64List = [123456789012345, -678901234567890, "
                       "-9223372036854775808, 9223372036854775807], "
          "uInt8List = [12, 34, 0, 255], "
          "uInt16List = [1234, 5678, 0, 65535], "
          "uInt32List = [12345678, 90123456, 0, 4294967295], "
          "uInt64List = [123456789012345, 678901234567890, 0, 18446744073709551615], "
          "float32List = [0, 1234567, 1e37, -1e37, 1e-37, -1e-37], "
          "float64List = [0, 123456789012345, 1e306, -1e306, 1e-306, -1e-306], "
          "textList = [\"quux\", \"corge\", \"grault\"], "
          "dataList = [\"garply\", \"waldo\", \"fred\"], "
          "structList = ["
              "(textField = \"x structlist 1\"), "
              "(textField = \"x structlist 2\"), "
              "(textField = \"x structlist 3\")], "
          "enumList = [qux, bar, grault]), "
      "enumField = corge, "
      "voidList = [void, void, void, void, void, void], "
      "boolList = [true, false, false, true], "
      "int8List = [111, -111], "
      "int16List = [11111, -11111], "
      "int32List = [111111111, -111111111], "
      "int64List = [1111111111111111111, -1111111111111111111], "
      "uInt8List = [111, 222], "
      "uInt16List = [33333, 44444], "
      "uInt32List = [3333333333], "
      "uInt64List = [11111111111111111111], "
      "float32List = [5555.5, inf, -inf, nan], "
      "float64List = [7777.75, inf, -inf, nan], "
      "textList = [\"plugh\", \"xyzzy\", \"thud\"], "
      "dataList = [\"oops\", \"exhausted\", \"rfc3092\"], "
      "structList = [(textField = \"structlist 1\"), "
                    "(textField = \"structlist 2\"), "
                    "(textField = \"structlist 3\")], "
      "enumList = [foo, garply])",
      kj::str(root));
}

TEST(Stringify, PrettyPrint) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  EXPECT_EQ("()", prettyPrint(root));

  initTestMessage(root);

  EXPECT_EQ(
      "( boolField = true,\n"
      "  int8Field = -123,\n"
      "  int16Field = -12345,\n"
      "  int32Field = -12345678,\n"
      "  int64Field = -123456789012345,\n"
      "  uInt8Field = 234,\n"
      "  uInt16Field = 45678,\n"
      "  uInt32Field = 3456789012,\n"
      "  uInt64Field = 12345678901234567890,\n"
      "  float32Field = 1234.5,\n"
      "  float64Field = -1.23e47,\n"
      "  textField = \"foo\",\n"
      "  dataField = \"bar\",\n"
      "  structField = (\n"
      "    boolField = true,\n"
      "    int8Field = -12,\n"
      "    int16Field = 3456,\n"
      "    int32Field = -78901234,\n"
      "    int64Field = 56789012345678,\n"
      "    uInt8Field = 90,\n"
      "    uInt16Field = 1234,\n"
      "    uInt32Field = 56789012,\n"
      "    uInt64Field = 345678901234567890,\n"
      "    float32Field = -1.25e-10,\n"
      "    float64Field = 345,\n"
      "    textField = \"baz\",\n"
      "    dataField = \"qux\",\n"
      "    structField = (\n"
      "      textField = \"nested\",\n"
      "      structField = (textField = \"really nested\")),\n"
      "    enumField = baz,\n"
      "    voidList = [void, void, void],\n"
      "    boolList = [false, true, false, true, true],\n"
      "    int8List = [12, -34, -128, 127],\n"
      "    int16List = [1234, -5678, -32768, 32767],\n"
      "    int32List = [12345678, -90123456, -2147483648, 2147483647],\n"
      "    int64List = [123456789012345, -678901234567890, "
                       "-9223372036854775808, 9223372036854775807],\n"
      "    uInt8List = [12, 34, 0, 255],\n"
      "    uInt16List = [1234, 5678, 0, 65535],\n"
      "    uInt32List = [12345678, 90123456, 0, 4294967295],\n"
      "    uInt64List = [123456789012345, 678901234567890, 0, 18446744073709551615],\n"
      "    float32List = [0, 1234567, 1e37, -1e37, 1e-37, -1e-37],\n"
      "    float64List = [0, 123456789012345, 1e306, -1e306, 1e-306, -1e-306],\n"
      "    textList = [\"quux\", \"corge\", \"grault\"],\n"
      "    dataList = [\"garply\", \"waldo\", \"fred\"],\n"
      "    structList = [\n"
      "      (textField = \"x structlist 1\"),\n"
      "      (textField = \"x structlist 2\"),\n"
      "      (textField = \"x structlist 3\")],\n"
      "    enumList = [qux, bar, grault]),\n"
      "  enumField = corge,\n"
      "  voidList = [void, void, void, void, void, void],\n"
      "  boolList = [true, false, false, true],\n"
      "  int8List = [111, -111],\n"
      "  int16List = [11111, -11111],\n"
      "  int32List = [111111111, -111111111],\n"
      "  int64List = [1111111111111111111, -1111111111111111111],\n"
      "  uInt8List = [111, 222],\n"
      "  uInt16List = [33333, 44444],\n"
      "  uInt32List = [3333333333],\n"
      "  uInt64List = [11111111111111111111],\n"
      "  float32List = [5555.5, inf, -inf, nan],\n"
      "  float64List = [7777.75, inf, -inf, nan],\n"
      "  textList = [\"plugh\", \"xyzzy\", \"thud\"],\n"
      "  dataList = [\"oops\", \"exhausted\", \"rfc3092\"],\n"
      "  structList = [\n"
      "    (textField = \"structlist 1\"),\n"
      "    (textField = \"structlist 2\"),\n"
      "    (textField = \"structlist 3\")],\n"
      "  enumList = [foo, garply])",
      prettyPrint(root));
}

TEST(Stringify, PrettyPrintAdvanced) {
  MallocMessageBuilder builder;

  {
    auto root = builder.initRoot<TestAllTypes>();

    auto list = root.initStructList(3);
    list[0].setInt32Field(123);
    list[0].setTextField("foo");
    list[1].setInt32Field(456);
    list[1].setTextField("bar");
    list[2].setInt32Field(789);
    list[2].setTextField("baz");

    EXPECT_EQ(
        "(structList = [\n"
        "  ( int32Field = 123,\n"
        "    textField = \"foo\"),\n"
        "  ( int32Field = 456,\n"
        "    textField = \"bar\"),\n"
        "  ( int32Field = 789,\n"
        "    textField = \"baz\")])",
        prettyPrint(root));

    root.setInt32Field(55);

    EXPECT_EQ(
        "( int32Field = 55,\n"
        "  structList = [\n"
        "    ( int32Field = 123,\n"
        "      textField = \"foo\"),\n"
        "    ( int32Field = 456,\n"
        "      textField = \"bar\"),\n"
        "    ( int32Field = 789,\n"
        "      textField = \"baz\")])",
        prettyPrint(root));
  }

  {
    auto root = builder.initRoot<test::TestLists>();
    auto ll = root.initInt32ListList(3);
    ll.set(0, {123, 456, 789});
    ll.set(1, {234, 567, 891});
    ll.set(2, {345, 678, 912});

    EXPECT_EQ(
        "[ [123, 456, 789],\n"
        "  [234, 567, 891],\n"
        "  [345, 678, 912]]",
        prettyPrint(ll));

    EXPECT_EQ(
        "(int32ListList = [\n"
        "  [123, 456, 789],\n"
        "  [234, 567, 891],\n"
        "  [345, 678, 912]])",
        prettyPrint(root));

    root.initList8(0);

    EXPECT_EQ(
        "( list8 = [],\n"
        "  int32ListList = [\n"
        "    [123, 456, 789],\n"
        "    [234, 567, 891],\n"
        "    [345, 678, 912]])",
        prettyPrint(root));

    auto l8 = root.initList8(1);
    l8[0].setF(12);

    EXPECT_EQ(
        "( list8 = [(f = 12)],\n"
        "  int32ListList = [\n"
        "    [123, 456, 789],\n"
        "    [234, 567, 891],\n"
        "    [345, 678, 912]])",
        prettyPrint(root));

    l8 = root.initList8(2);
    l8[0].setF(12);
    l8[1].setF(34);

    EXPECT_EQ(
        "( list8 = [\n"
        "    (f = 12),\n"
        "    (f = 34)],\n"
        "  int32ListList = [\n"
        "    [123, 456, 789],\n"
        "    [234, 567, 891],\n"
        "    [345, 678, 912]])",
        prettyPrint(root));
  }

  {
    auto root = builder.initRoot<test::TestStructUnion>();

    auto s = root.getUn().initAllTypes();
    EXPECT_EQ(
        "(un = allTypes())",
        prettyPrint(root));

    s.setInt32Field(123);
    EXPECT_EQ(
        "(un = allTypes(int32Field = 123))",
        prettyPrint(root));

    s.setTextField("foo");
    EXPECT_EQ(
        "(un = allTypes(\n"
        "  int32Field = 123,\n"
        "  textField = \"foo\"))",
        prettyPrint(root));
  }
}

TEST(Stringify, Unions) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestUnion>();

  root.getUnion0().setU0f0s16(321);
  root.getUnion1().setU1f0sp("foo");
  root.getUnion2().setU2f0s1(true);
  root.getUnion3().setU3f0s64(123456789012345678ll);

  EXPECT_EQ("("
      "union0 = u0f0s16(321), "
      "union1 = u1f0sp(\"foo\"), "
      "union2 = u2f0s1(true), "
      "union3 = u3f0s64(123456789012345678))",
      kj::str(root));

  EXPECT_EQ("u0f0s16(321)", kj::str(root.getUnion0()));
  EXPECT_EQ("u1f0sp(\"foo\")", kj::str(root.getUnion1()));
  EXPECT_EQ("u2f0s1(true)", kj::str(root.getUnion2()));
  EXPECT_EQ("u3f0s64(123456789012345678)", kj::str(root.getUnion3()));
}

TEST(Stringify, StructUnions) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestStructUnion>();

  auto allTypes = root.getUn().initAllTypes();
  allTypes.setUInt32Field(12345);
  allTypes.setTextField("foo");

  EXPECT_EQ("(un = allTypes(uInt32Field = 12345, textField = \"foo\"))", kj::str(root));
}

TEST(Stringify, MoreValues) {
  EXPECT_EQ("123", kj::str(DynamicValue::Reader(123)));
  EXPECT_EQ("1.23e47", kj::str(DynamicValue::Reader(123e45)));
  EXPECT_EQ("\"foo\"", kj::str(DynamicValue::Reader("foo")));
  EXPECT_EQ("\"\\a\\b\\n\\t\\\"\"", kj::str(DynamicValue::Reader("\a\b\n\t\"")));

  EXPECT_EQ("foo", kj::str(DynamicValue::Reader(TestEnum::FOO)));
  EXPECT_EQ("123", kj::str(DynamicValue::Reader(static_cast<TestEnum>(123))));
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
