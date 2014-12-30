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

#include "message.h"
#include "dynamic.h"
#include "pretty-print.h"
#include <kj/debug.h>
#include <kj/compat/gtest.h>
#include "test-util.h"

namespace capnp {
namespace _ {  // private
namespace {

TEST(Stringify, KjStringification) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  // This test got ugly after printing was changed to always print primitives even when they have
  // default values...

  EXPECT_EQ("("
      "voidField = void, "
      "boolField = false, "
      "int8Field = 0, "
      "int16Field = 0, "
      "int32Field = 0, "
      "int64Field = 0, "
      "uInt8Field = 0, "
      "uInt16Field = 0, "
      "uInt32Field = 0, "
      "uInt64Field = 0, "
      "float32Field = 0, "
      "float64Field = 0, "
      "enumField = foo, "
      "interfaceField = void)",
      kj::str(root));

  initTestMessage(root);

  EXPECT_EQ("("
      "voidField = void, "
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
          "voidField = void, "
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
              "voidField = void, "
              "boolField = false, "
              "int8Field = 0, "
              "int16Field = 0, "
              "int32Field = 0, "
              "int64Field = 0, "
              "uInt8Field = 0, "
              "uInt16Field = 0, "
              "uInt32Field = 0, "
              "uInt64Field = 0, "
              "float32Field = 0, "
              "float64Field = 0, "
              "textField = \"nested\", "
              "structField = ("
                "voidField = void, "
                "boolField = false, "
                "int8Field = 0, "
                "int16Field = 0, "
                "int32Field = 0, "
                "int64Field = 0, "
                "uInt8Field = 0, "
                "uInt16Field = 0, "
                "uInt32Field = 0, "
                "uInt64Field = 0, "
                "float32Field = 0, "
                "float64Field = 0, "
                "textField = \"really nested\", "
                "enumField = foo, "
                "interfaceField = void), "
              "enumField = foo, "
              "interfaceField = void), "
          "enumField = baz, "
          "interfaceField = void, "
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
              "("
                "voidField = void, "
                "boolField = false, "
                "int8Field = 0, "
                "int16Field = 0, "
                "int32Field = 0, "
                "int64Field = 0, "
                "uInt8Field = 0, "
                "uInt16Field = 0, "
                "uInt32Field = 0, "
                "uInt64Field = 0, "
                "float32Field = 0, "
                "float64Field = 0, "
                "textField = \"x structlist 1\", "
                "enumField = foo, "
                "interfaceField = void), "
              "("
                "voidField = void, "
                "boolField = false, "
                "int8Field = 0, "
                "int16Field = 0, "
                "int32Field = 0, "
                "int64Field = 0, "
                "uInt8Field = 0, "
                "uInt16Field = 0, "
                "uInt32Field = 0, "
                "uInt64Field = 0, "
                "float32Field = 0, "
                "float64Field = 0, "
                "textField = \"x structlist 2\", "
                "enumField = foo, "
                "interfaceField = void), "
              "("
                "voidField = void, "
                "boolField = false, "
                "int8Field = 0, "
                "int16Field = 0, "
                "int32Field = 0, "
                "int64Field = 0, "
                "uInt8Field = 0, "
                "uInt16Field = 0, "
                "uInt32Field = 0, "
                "uInt64Field = 0, "
                "float32Field = 0, "
                "float64Field = 0, "
                "textField = \"x structlist 3\", "
                "enumField = foo, "
                "interfaceField = void)], "
          "enumList = [qux, bar, grault]), "
      "enumField = corge, "
      "interfaceField = void, "
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
      "structList = ["
          "("
            "voidField = void, "
            "boolField = false, "
            "int8Field = 0, "
            "int16Field = 0, "
            "int32Field = 0, "
            "int64Field = 0, "
            "uInt8Field = 0, "
            "uInt16Field = 0, "
            "uInt32Field = 0, "
            "uInt64Field = 0, "
            "float32Field = 0, "
            "float64Field = 0, "
            "textField = \"structlist 1\", "
            "enumField = foo, "
            "interfaceField = void), "
          "("
            "voidField = void, "
            "boolField = false, "
            "int8Field = 0, "
            "int16Field = 0, "
            "int32Field = 0, "
            "int64Field = 0, "
            "uInt8Field = 0, "
            "uInt16Field = 0, "
            "uInt32Field = 0, "
            "uInt64Field = 0, "
            "float32Field = 0, "
            "float64Field = 0, "
            "textField = \"structlist 2\", "
            "enumField = foo, "
            "interfaceField = void), "
          "("
            "voidField = void, "
            "boolField = false, "
            "int8Field = 0, "
            "int16Field = 0, "
            "int32Field = 0, "
            "int64Field = 0, "
            "uInt8Field = 0, "
            "uInt16Field = 0, "
            "uInt32Field = 0, "
            "uInt64Field = 0, "
            "float32Field = 0, "
            "float64Field = 0, "
            "textField = \"structlist 3\", "
            "enumField = foo, "
            "interfaceField = void)], "
      "enumList = [foo, garply])",
      kj::str(root));
}

TEST(Stringify, PrettyPrint) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  EXPECT_EQ(
      "( voidField = void,\n"
      "  boolField = false,\n"
      "  int8Field = 0,\n"
      "  int16Field = 0,\n"
      "  int32Field = 0,\n"
      "  int64Field = 0,\n"
      "  uInt8Field = 0,\n"
      "  uInt16Field = 0,\n"
      "  uInt32Field = 0,\n"
      "  uInt64Field = 0,\n"
      "  float32Field = 0,\n"
      "  float64Field = 0,\n"
      "  enumField = foo,\n"
      "  interfaceField = void )", prettyPrint(root).flatten());

  initTestMessage(root);

  EXPECT_EQ(
      "( voidField = void,\n"
      "  boolField = true,\n"
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
      "    voidField = void,\n"
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
      "      voidField = void,\n"
      "      boolField = false,\n"
      "      int8Field = 0,\n"
      "      int16Field = 0,\n"
      "      int32Field = 0,\n"
      "      int64Field = 0,\n"
      "      uInt8Field = 0,\n"
      "      uInt16Field = 0,\n"
      "      uInt32Field = 0,\n"
      "      uInt64Field = 0,\n"
      "      float32Field = 0,\n"
      "      float64Field = 0,\n"
      "      textField = \"nested\",\n"
      "      structField = (\n"
      "        voidField = void,\n"
      "        boolField = false,\n"
      "        int8Field = 0,\n"
      "        int16Field = 0,\n"
      "        int32Field = 0,\n"
      "        int64Field = 0,\n"
      "        uInt8Field = 0,\n"
      "        uInt16Field = 0,\n"
      "        uInt32Field = 0,\n"
      "        uInt64Field = 0,\n"
      "        float32Field = 0,\n"
      "        float64Field = 0,\n"
      "        textField = \"really nested\",\n"
      "        enumField = foo,\n"
      "        interfaceField = void ),\n"
      "      enumField = foo,\n"
      "      interfaceField = void ),\n"
      "    enumField = baz,\n"
      "    interfaceField = void,\n"
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
      "      ( voidField = void,\n"
      "        boolField = false,\n"
      "        int8Field = 0,\n"
      "        int16Field = 0,\n"
      "        int32Field = 0,\n"
      "        int64Field = 0,\n"
      "        uInt8Field = 0,\n"
      "        uInt16Field = 0,\n"
      "        uInt32Field = 0,\n"
      "        uInt64Field = 0,\n"
      "        float32Field = 0,\n"
      "        float64Field = 0,\n"
      "        textField = \"x structlist 1\",\n"
      "        enumField = foo,\n"
      "        interfaceField = void ),\n"
      "      ( voidField = void,\n"
      "        boolField = false,\n"
      "        int8Field = 0,\n"
      "        int16Field = 0,\n"
      "        int32Field = 0,\n"
      "        int64Field = 0,\n"
      "        uInt8Field = 0,\n"
      "        uInt16Field = 0,\n"
      "        uInt32Field = 0,\n"
      "        uInt64Field = 0,\n"
      "        float32Field = 0,\n"
      "        float64Field = 0,\n"
      "        textField = \"x structlist 2\",\n"
      "        enumField = foo,\n"
      "        interfaceField = void ),\n"
      "      ( voidField = void,\n"
      "        boolField = false,\n"
      "        int8Field = 0,\n"
      "        int16Field = 0,\n"
      "        int32Field = 0,\n"
      "        int64Field = 0,\n"
      "        uInt8Field = 0,\n"
      "        uInt16Field = 0,\n"
      "        uInt32Field = 0,\n"
      "        uInt64Field = 0,\n"
      "        float32Field = 0,\n"
      "        float64Field = 0,\n"
      "        textField = \"x structlist 3\",\n"
      "        enumField = foo,\n"
      "        interfaceField = void ) ],\n"
      "    enumList = [qux, bar, grault] ),\n"
      "  enumField = corge,\n"
      "  interfaceField = void,\n"
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
      "    ( voidField = void,\n"
      "      boolField = false,\n"
      "      int8Field = 0,\n"
      "      int16Field = 0,\n"
      "      int32Field = 0,\n"
      "      int64Field = 0,\n"
      "      uInt8Field = 0,\n"
      "      uInt16Field = 0,\n"
      "      uInt32Field = 0,\n"
      "      uInt64Field = 0,\n"
      "      float32Field = 0,\n"
      "      float64Field = 0,\n"
      "      textField = \"structlist 1\",\n"
      "      enumField = foo,\n"
      "      interfaceField = void ),\n"
      "    ( voidField = void,\n"
      "      boolField = false,\n"
      "      int8Field = 0,\n"
      "      int16Field = 0,\n"
      "      int32Field = 0,\n"
      "      int64Field = 0,\n"
      "      uInt8Field = 0,\n"
      "      uInt16Field = 0,\n"
      "      uInt32Field = 0,\n"
      "      uInt64Field = 0,\n"
      "      float32Field = 0,\n"
      "      float64Field = 0,\n"
      "      textField = \"structlist 2\",\n"
      "      enumField = foo,\n"
      "      interfaceField = void ),\n"
      "    ( voidField = void,\n"
      "      boolField = false,\n"
      "      int8Field = 0,\n"
      "      int16Field = 0,\n"
      "      int32Field = 0,\n"
      "      int64Field = 0,\n"
      "      uInt8Field = 0,\n"
      "      uInt16Field = 0,\n"
      "      uInt32Field = 0,\n"
      "      uInt64Field = 0,\n"
      "      float32Field = 0,\n"
      "      float64Field = 0,\n"
      "      textField = \"structlist 3\",\n"
      "      enumField = foo,\n"
      "      interfaceField = void ) ],\n"
      "  enumList = [foo, garply] )",
      prettyPrint(root).flatten());
}

TEST(Stringify, PrettyPrintAdvanced) {
  MallocMessageBuilder builder;

  {
    auto root = builder.initRoot<test::TestPrintInlineStructs>();

    auto list = root.initStructList(3);
    list[0].setInt32Field(123);
    list[0].setTextField("foo");
    list[1].setInt32Field(456);
    list[1].setTextField("bar");
    list[2].setInt32Field(789);
    list[2].setTextField("baz");

    EXPECT_EQ(
        "( structList = [\n"
        "    (int32Field = 123, textField = \"foo\"),\n"
        "    (int32Field = 456, textField = \"bar\"),\n"
        "    (int32Field = 789, textField = \"baz\") ] )",
        prettyPrint(root).flatten());

    root.setSomeText("foo");

    EXPECT_EQ(
        "( someText = \"foo\",\n"
        "  structList = [\n"
        "    (int32Field = 123, textField = \"foo\"),\n"
        "    (int32Field = 456, textField = \"bar\"),\n"
        "    (int32Field = 789, textField = \"baz\") ] )",
        prettyPrint(root).flatten());
  }

  {
    auto root = builder.initRoot<test::TestLists>();
    auto ll = root.initInt32ListList(3);
    ll.set(0, {123, 456, 789, 1234567890});
    ll.set(1, {234, 567, 891, 1234567890});
    ll.set(2, {345, 678, 912, 1234567890});

    EXPECT_EQ(
        "[ [123, 456, 789, 1234567890],\n"
        "  [234, 567, 891, 1234567890],\n"
        "  [345, 678, 912, 1234567890] ]",
        prettyPrint(ll).flatten());

    EXPECT_EQ(
        "( int32ListList = [\n"
        "    [123, 456, 789, 1234567890],\n"
        "    [234, 567, 891, 1234567890],\n"
        "    [345, 678, 912, 1234567890] ] )",
        prettyPrint(root).flatten());

    root.initList8(0);

    EXPECT_EQ(
        "( list8 = [],\n"
        "  int32ListList = [\n"
        "    [123, 456, 789, 1234567890],\n"
        "    [234, 567, 891, 1234567890],\n"
        "    [345, 678, 912, 1234567890] ] )",
        prettyPrint(root).flatten());

    auto l8 = root.initList8(1);
    l8[0].setF(12);

    EXPECT_EQ(
        "( list8 = [(f = 12)],\n"
        "  int32ListList = [\n"
        "    [123, 456, 789, 1234567890],\n"
        "    [234, 567, 891, 1234567890],\n"
        "    [345, 678, 912, 1234567890] ] )",
        prettyPrint(root).flatten());

    l8 = root.initList8(2);
    l8[0].setF(12);
    l8[1].setF(34);

    EXPECT_EQ(
        "( list8 = [(f = 12), (f = 34)],\n"
        "  int32ListList = [\n"
        "    [123, 456, 789, 1234567890],\n"
        "    [234, 567, 891, 1234567890],\n"
        "    [345, 678, 912, 1234567890] ] )",
        prettyPrint(root).flatten());
  }

  {
    auto root = builder.initRoot<test::TestStructUnion>();

    auto s = root.getUn().initStruct();
    EXPECT_EQ(
        "(un = (struct = ()))",
        prettyPrint(root).flatten());

    s.setSomeText("foo");
    EXPECT_EQ(
        "( un = (\n"
        "    struct = (someText = \"foo\") ) )",
        prettyPrint(root).flatten());

    s.setMoreText("baaaaaaaaaaaaaaaaaaaaaaaaaaaaaar");
    EXPECT_EQ(
        "( un = (\n"
        "    struct = (\n"
        "      someText = \"foo\",\n"
        "      moreText = \"baaaaaaaaaaaaaaaaaaaaaaaaaaaaaar\" ) ) )",
        prettyPrint(root).flatten());
  }
}

TEST(Stringify, Unions) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestUnion>();

  root.getUnion0().setU0f0s16(123);
  root.getUnion1().setU1f0sp("foo");
  root.getUnion2().setU2f0s1(true);
  root.getUnion3().setU3f0s64(123456789012345678ll);

  EXPECT_EQ("("
      "union0 = (u0f0s16 = 123), "
      "union1 = (u1f0sp = \"foo\"), "
      "union2 = (u2f0s1 = true), "
      "union3 = (u3f0s64 = 123456789012345678), "
      "bit0 = false, bit2 = false, bit3 = false, bit4 = false, bit5 = false, "
      "bit6 = false, bit7 = false, byte0 = 0)",
      kj::str(root));

  EXPECT_EQ("(u0f0s16 = 123)", kj::str(root.getUnion0()));
  EXPECT_EQ("(u1f0sp = \"foo\")", kj::str(root.getUnion1()));
  EXPECT_EQ("(u2f0s1 = true)", kj::str(root.getUnion2()));
  EXPECT_EQ("(u3f0s64 = 123456789012345678)", kj::str(root.getUnion3()));
}

TEST(Stringify, UnionDefaults) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestUnion>();

  root.getUnion0().setU0f0s16(0);     // Non-default field has default value.
  root.getUnion1().setU1f0sp("foo");  // Non-default field has non-default value.
  root.getUnion2().setU2f0s1(false);  // Default field has default value.
  root.getUnion3().setU3f0s1(true);   // Default field has non-default value.

  EXPECT_EQ("("
      "union0 = (u0f0s16 = 0), "
      "union1 = (u1f0sp = \"foo\"), "
      "union2 = (u2f0s1 = false), "
      "union3 = (u3f0s1 = true), "
      "bit0 = false, bit2 = false, bit3 = false, bit4 = false, bit5 = false, "
      "bit6 = false, bit7 = false, byte0 = 0)",
      kj::str(root));

  EXPECT_EQ("(u0f0s16 = 0)", kj::str(root.getUnion0()));
  EXPECT_EQ("(u1f0sp = \"foo\")", kj::str(root.getUnion1()));
  EXPECT_EQ("(u2f0s1 = false)", kj::str(root.getUnion2()));
  EXPECT_EQ("(u3f0s1 = true)", kj::str(root.getUnion3()));
}

TEST(Stringify, UnnamedUnions) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestUnnamedUnion>();

  root.setBar(123);

  EXPECT_EQ("(middle = 0, bar = 123)", kj::str(root));
  EXPECT_EQ("(middle = 0, bar = 123)", prettyPrint(root).flatten());

  root.setAfter("foooooooooooooooooooooooooooooooo");

  EXPECT_EQ("(middle = 0, bar = 123, after = \"foooooooooooooooooooooooooooooooo\")",
            kj::str(root));
  EXPECT_EQ(
      "( middle = 0,\n"
      "  bar = 123,\n"
      "  after = \"foooooooooooooooooooooooooooooooo\" )",
      prettyPrint(root).flatten());

  root.setBefore("before");

  EXPECT_EQ("(before = \"before\", middle = 0, bar = 123, "
      "after = \"foooooooooooooooooooooooooooooooo\")", kj::str(root));
  EXPECT_EQ(
      "( before = \"before\",\n"
      "  middle = 0,\n"
      "  bar = 123,\n"
      "  after = \"foooooooooooooooooooooooooooooooo\" )",
      prettyPrint(root).flatten());

  root.setFoo(0);

  EXPECT_EQ(
      "(before = \"before\", foo = 0, middle = 0, after = \"foooooooooooooooooooooooooooooooo\")",
      kj::str(root));
  EXPECT_EQ(
      "( before = \"before\",\n"
      "  foo = 0,\n"
      "  middle = 0,\n"
      "  after = \"foooooooooooooooooooooooooooooooo\" )",
      prettyPrint(root).flatten());
}

TEST(Stringify, StructUnions) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestStructUnion>();

  auto s = root.getUn().initStruct();
  s.setSomeText("foo");
  s.setMoreText("bar");

  EXPECT_EQ("(un = (struct = (someText = \"foo\", moreText = \"bar\")))", kj::str(root));
}

TEST(Stringify, MoreValues) {
  EXPECT_EQ("123", kj::str(DynamicValue::Reader(123)));
  EXPECT_EQ("1.23e47", kj::str(DynamicValue::Reader(123e45)));
  EXPECT_EQ("\"foo\"", kj::str(DynamicValue::Reader("foo")));
  EXPECT_EQ("\"\\a\\b\\n\\t\\\"\"", kj::str(DynamicValue::Reader("\a\b\n\t\"")));

  EXPECT_EQ("foo", kj::str(DynamicValue::Reader(TestEnum::FOO)));
  EXPECT_EQ("(123)", kj::str(DynamicValue::Reader(static_cast<TestEnum>(123))));
}

TEST(Stringify, Generics) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestGenerics<Text, List<uint32_t>>::Inner>();
  root.setFoo("abcd");
  auto l = root.initBar(2);
  l.set(0, 123);
  l.set(1, 456);

  EXPECT_EQ("(foo = \"abcd\", bar = [123, 456])", kj::str(root));
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
