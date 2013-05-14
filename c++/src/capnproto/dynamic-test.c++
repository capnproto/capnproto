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

#define CAPNPROTO_PRIVATE
#include "dynamic.h"
#include "message.h"
#include "logging.h"
#include <gtest/gtest.h>
#include "test-util.h"

namespace capnproto {
namespace internal {
namespace {

void dynamicInitTestMessage(DynamicStruct::Builder builder) {
  builder.set("voidField", Void::VOID);
  builder.set("boolField", true);
  builder.set("int8Field", -123);
  builder.set("int16Field", -12345);
  builder.set("int32Field", -12345678);
  builder.set("int64Field", -123456789012345ll);
  builder.set("uInt8Field", 234u);
  builder.set("uInt16Field", 45678u);
  builder.set("uInt32Field", 3456789012u);
  builder.set("uInt64Field", 12345678901234567890ull);
  builder.set("float32Field", 1234.5);
  builder.set("float64Field", -123e45);
  builder.set("textField", "foo");
  builder.set("dataField", "bar");
  {
    auto subBuilder = builder.init("structField").as<DynamicStruct>();
    subBuilder.set("voidField", Void::VOID);
    subBuilder.set("boolField", true);
    subBuilder.set("int8Field", -12);
    subBuilder.set("int16Field", 3456);
    subBuilder.set("int32Field", -78901234);
    subBuilder.set("int64Field", 56789012345678ll);
    subBuilder.set("uInt8Field", 90u);
    subBuilder.set("uInt16Field", 1234u);
    subBuilder.set("uInt32Field", 56789012u);
    subBuilder.set("uInt64Field", 345678901234567890ull);
    subBuilder.set("float32Field", -1.25e-10);
    subBuilder.set("float64Field", 345);
    subBuilder.set("textField", "baz");
    subBuilder.set("dataField", "qux");
    {
      auto subSubBuilder = subBuilder.init("structField").as<DynamicStruct>();
      subSubBuilder.set("textField", "nested");
      subSubBuilder.init("structField").as<DynamicStruct>().set("textField", "really nested");
    }
    subBuilder.set("enumField", TestEnum::BAZ);

    subBuilder.set("voidList", {Void::VOID, Void::VOID, Void::VOID});
    subBuilder.set("boolList", {false, true, false, true, true});
    subBuilder.set("int8List", {12, -34, -0x80, 0x7f});
    subBuilder.set("int16List", {1234, -5678, -0x8000, 0x7fff});
    subBuilder.set("int32List", {12345678, -90123456, -0x8000000, 0x7ffffff});
    // gcc warns on -0x800...ll and the only work-around I could find was to do -0x7ff...ll-1.
    subBuilder.set("int64List", {123456789012345ll, -678901234567890ll, -0x7fffffffffffffffll-1, 0x7fffffffffffffffll});
    subBuilder.set("uInt8List", {12u, 34u, 0u, 0xffu});
    subBuilder.set("uInt16List", {1234u, 5678u, 0u, 0xffffu});
    subBuilder.set("uInt32List", {12345678u, 90123456u, 0u, 0xffffffffu});
    subBuilder.set("uInt64List", {123456789012345ull, 678901234567890ull, 0ull, 0xffffffffffffffffull});
    subBuilder.set("float32List", {0, 1234567, 1e37, -1e37, 1e-37, -1e-37});
    subBuilder.set("float64List", {0, 123456789012345, 1e306, -1e306, 1e-306, -1e-306});
    subBuilder.set("textList", {"quux", "corge", "grault"});
    subBuilder.set("dataList", {"garply", "waldo", "fred"});
    {
      auto listBuilder = subBuilder.init("structList", 3).as<DynamicList>();
      listBuilder[0].as<DynamicStruct>().set("textField", "x structlist 1");
      listBuilder[1].as<DynamicStruct>().set("textField", "x structlist 2");
      listBuilder[2].as<DynamicStruct>().set("textField", "x structlist 3");
    }
    subBuilder.set("enumList", {TestEnum::QUX, TestEnum::BAR, TestEnum::GRAULT});
  }
  builder.set("enumField", TestEnum::CORGE);

  builder.init("voidList", 6);
  builder.set("boolList", {true, false, false, true});
  builder.set("int8List", {111, -111});
  builder.set("int16List", {11111, -11111});
  builder.set("int32List", {111111111, -111111111});
  builder.set("int64List", {1111111111111111111ll, -1111111111111111111ll});
  builder.set("uInt8List", {111u, 222u});
  builder.set("uInt16List", {33333u, 44444u});
  builder.set("uInt32List", {3333333333u});
  builder.set("uInt64List", {11111111111111111111ull});
  builder.set("float32List", {5555.5,
                          std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()});
  builder.set("float64List", {7777.75,
                          std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()});
  builder.set("textList", {"plugh", "xyzzy", "thud"});
  builder.set("dataList", {"oops", "exhausted", "rfc3092"});
  {
    auto listBuilder = builder.init("structList", 3).as<DynamicList>();
    listBuilder[0].as<DynamicStruct>().set("textField", "structlist 1");
    listBuilder[1].as<DynamicStruct>().set("textField", "structlist 2");
    listBuilder[2].as<DynamicStruct>().set("textField", "structlist 3");
  }
  builder.set("enumList", {TestEnum::FOO, TestEnum::GARPLY});
}

template <typename T> void expectPrimitiveEq(T a, T b) { EXPECT_EQ(a, b); }
void expectPrimitiveEq(float a, float b) { EXPECT_FLOAT_EQ(a, b); }
void expectPrimitiveEq(double a, double b) { EXPECT_DOUBLE_EQ(a, b); }
void expectPrimitiveEq(Text::Reader a, Text::Builder b) { EXPECT_EQ(a, b); }
void expectPrimitiveEq(Data::Reader a, Data::Builder b) { EXPECT_EQ(a, b); }

// Hack because as<>() is a template-parameter-dependent lookup everywhere below...
#define as template as

template <typename Element, typename T>
void checkList(T reader, std::initializer_list<ReaderFor<Element>> expected) {
  auto list = reader.as<DynamicList>();
  ASSERT_EQ(expected.size(), list.size());
  for (uint i = 0; i < expected.size(); i++) {
    expectPrimitiveEq(expected.begin()[i], list[i].as<Element>());
  }

  auto typed = reader.as<List<Element>>();
  ASSERT_EQ(expected.size(), typed.size());
  for (uint i = 0; i < expected.size(); i++) {
    expectPrimitiveEq(expected.begin()[i], typed[i]);
  }
}

inline bool isNaN(float f) { return f != f; }
inline bool isNaN(double f) { return f != f; }

template <typename Reader>
void dynamicCheckTestMessage(Reader reader) {
  EXPECT_EQ(Void::VOID, reader.get("voidField").as<Void>());
  EXPECT_EQ(true, reader.get("boolField").as<bool>());
  EXPECT_EQ(-123, reader.get("int8Field").as<int8_t>());
  EXPECT_EQ(-12345, reader.get("int16Field").as<int16_t>());
  EXPECT_EQ(-12345678, reader.get("int32Field").as<int32_t>());
  EXPECT_EQ(-123456789012345ll, reader.get("int64Field").as<int64_t>());
  EXPECT_EQ(234u, reader.get("uInt8Field").as<uint8_t>());
  EXPECT_EQ(45678u, reader.get("uInt16Field").as<uint16_t>());
  EXPECT_EQ(3456789012u, reader.get("uInt32Field").as<uint32_t>());
  EXPECT_EQ(12345678901234567890ull, reader.get("uInt64Field").as<uint64_t>());
  EXPECT_FLOAT_EQ(1234.5f, reader.get("float32Field").as<float>());
  EXPECT_DOUBLE_EQ(-123e45, reader.get("float64Field").as<double>());
  EXPECT_EQ("foo", reader.get("textField").as<Text>());
  EXPECT_EQ("bar", reader.get("dataField").as<Data>());
  {
    auto subReader = reader.get("structField").as<DynamicStruct>();
    EXPECT_EQ(Void::VOID, subReader.get("voidField").as<Void>());
    EXPECT_EQ(true, subReader.get("boolField").as<bool>());
    EXPECT_EQ(-12, subReader.get("int8Field").as<int8_t>());
    EXPECT_EQ(3456, subReader.get("int16Field").as<int16_t>());
    EXPECT_EQ(-78901234, subReader.get("int32Field").as<int32_t>());
    EXPECT_EQ(56789012345678ll, subReader.get("int64Field").as<int64_t>());
    EXPECT_EQ(90u, subReader.get("uInt8Field").as<uint8_t>());
    EXPECT_EQ(1234u, subReader.get("uInt16Field").as<uint16_t>());
    EXPECT_EQ(56789012u, subReader.get("uInt32Field").as<uint32_t>());
    EXPECT_EQ(345678901234567890ull, subReader.get("uInt64Field").as<uint64_t>());
    EXPECT_FLOAT_EQ(-1.25e-10f, subReader.get("float32Field").as<float>());
    EXPECT_DOUBLE_EQ(345, subReader.get("float64Field").as<double>());
    EXPECT_EQ("baz", subReader.get("textField").as<Text>());
    EXPECT_EQ("qux", subReader.get("dataField").as<Data>());
    {
      auto subSubReader = subReader.get("structField").as<DynamicStruct>();
      EXPECT_EQ("nested", subSubReader.get("textField").as<Text>());
      EXPECT_EQ("really nested", subSubReader.get("structField").as<DynamicStruct>()
                                             .get("textField").as<Text>());
    }
    EXPECT_EQ(TestEnum::BAZ, subReader.get("enumField").as<TestEnum>());

    checkList<Void>(subReader.get("voidList"), {Void::VOID, Void::VOID, Void::VOID});
    checkList<bool>(subReader.get("boolList"), {false, true, false, true, true});
    checkList<int8_t>(subReader.get("int8List"), {12, -34, -0x80, 0x7f});
    checkList<int16_t>(subReader.get("int16List"), {1234, -5678, -0x8000, 0x7fff});
    checkList<int32_t>(subReader.get("int32List"), {12345678, -90123456, -0x8000000, 0x7ffffff});
    // gcc warns on -0x800...ll and the only work-around I could find was to do -0x7ff...ll-1.
    checkList<int64_t>(subReader.get("int64List"), {123456789012345ll, -678901234567890ll, -0x7fffffffffffffffll-1, 0x7fffffffffffffffll});
    checkList<uint8_t>(subReader.get("uInt8List"), {12u, 34u, 0u, 0xffu});
    checkList<uint16_t>(subReader.get("uInt16List"), {1234u, 5678u, 0u, 0xffffu});
    checkList<uint32_t>(subReader.get("uInt32List"), {12345678u, 90123456u, 0u, 0xffffffffu});
    checkList<uint64_t>(subReader.get("uInt64List"), {123456789012345ull, 678901234567890ull, 0ull, 0xffffffffffffffffull});
    checkList<float>(subReader.get("float32List"), {0.0f, 1234567.0f, 1e37f, -1e37f, 1e-37f, -1e-37f});
    checkList<double>(subReader.get("float64List"), {0.0, 123456789012345.0, 1e306, -1e306, 1e-306, -1e-306});
    checkList<Text>(subReader.get("textList"), {"quux", "corge", "grault"});
    checkList<Data>(subReader.get("dataList"), {"garply", "waldo", "fred"});
    {
      auto listReader = subReader.get("structList").as<DynamicList>();
      ASSERT_EQ(3u, listReader.size());
      EXPECT_EQ("x structlist 1", listReader[0].as<DynamicStruct>().get("textField").as<Text>());
      EXPECT_EQ("x structlist 2", listReader[1].as<DynamicStruct>().get("textField").as<Text>());
      EXPECT_EQ("x structlist 3", listReader[2].as<DynamicStruct>().get("textField").as<Text>());
    }
    checkList<TestEnum>(subReader.get("enumList"), {TestEnum::QUX, TestEnum::BAR, TestEnum::GRAULT});
  }
  EXPECT_EQ(TestEnum::CORGE, reader.get("enumField").as<TestEnum>());

  EXPECT_EQ(6u, reader.get("voidList").as<DynamicList>().size());
  checkList<bool>(reader.get("boolList"), {true, false, false, true});
  checkList<int8_t>(reader.get("int8List"), {111, -111});
  checkList<int16_t>(reader.get("int16List"), {11111, -11111});
  checkList<int32_t>(reader.get("int32List"), {111111111, -111111111});
  checkList<int64_t>(reader.get("int64List"), {1111111111111111111ll, -1111111111111111111ll});
  checkList<uint8_t>(reader.get("uInt8List"), {111u, 222u});
  checkList<uint16_t>(reader.get("uInt16List"), {33333u, 44444u});
  checkList<uint32_t>(reader.get("uInt32List"), {3333333333u});
  checkList<uint64_t>(reader.get("uInt64List"), {11111111111111111111ull});
  {
    auto listReader = reader.get("float32List").as<DynamicList>();
    ASSERT_EQ(4u, listReader.size());
    EXPECT_EQ(5555.5f, listReader[0].as<float>());
    EXPECT_EQ(std::numeric_limits<float>::infinity(), listReader[1].as<float>());
    EXPECT_EQ(-std::numeric_limits<float>::infinity(), listReader[2].as<float>());
    EXPECT_TRUE(isNaN(listReader[3].as<float>()));
  }
  {
    auto listReader = reader.get("float64List").as<DynamicList>();
    ASSERT_EQ(4u, listReader.size());
    EXPECT_EQ(7777.75, listReader[0].as<double>());
    EXPECT_EQ(std::numeric_limits<double>::infinity(), listReader[1].as<double>());
    EXPECT_EQ(-std::numeric_limits<double>::infinity(), listReader[2].as<double>());
    EXPECT_TRUE(isNaN(listReader[3].as<double>()));
  }
  checkList<Text>(reader.get("textList"), {"plugh", "xyzzy", "thud"});
  checkList<Data>(reader.get("dataList"), {"oops", "exhausted", "rfc3092"});
  {
    auto listReader = reader.get("structList").as<DynamicList>();
    ASSERT_EQ(3u, listReader.size());
    EXPECT_EQ("structlist 1", listReader[0].as<DynamicStruct>().get("textField").as<Text>());
    EXPECT_EQ("structlist 2", listReader[1].as<DynamicStruct>().get("textField").as<Text>());
    EXPECT_EQ("structlist 3", listReader[2].as<DynamicStruct>().get("textField").as<Text>());
  }
  checkList<TestEnum>(reader.get("enumList"), {TestEnum::FOO, TestEnum::GARPLY});
}

template <typename Reader>
void dynamicCheckTestMessageAllZero(Reader reader) {
  EXPECT_EQ(Void::VOID, reader.get("voidField").as<Void>());
  EXPECT_EQ(false, reader.get("boolField").as<bool>());
  EXPECT_EQ(0, reader.get("int8Field").as<int8_t>());
  EXPECT_EQ(0, reader.get("int16Field").as<int16_t>());
  EXPECT_EQ(0, reader.get("int32Field").as<int32_t>());
  EXPECT_EQ(0, reader.get("int64Field").as<int64_t>());
  EXPECT_EQ(0u, reader.get("uInt8Field").as<uint8_t>());
  EXPECT_EQ(0u, reader.get("uInt16Field").as<uint16_t>());
  EXPECT_EQ(0u, reader.get("uInt32Field").as<uint32_t>());
  EXPECT_EQ(0u, reader.get("uInt64Field").as<uint64_t>());
  EXPECT_FLOAT_EQ(0, reader.get("float32Field").as<float>());
  EXPECT_DOUBLE_EQ(0, reader.get("float64Field").as<double>());
  EXPECT_EQ("", reader.get("textField").as<Text>());
  EXPECT_EQ("", reader.get("dataField").as<Data>());
  {
    auto subReader = reader.get("structField").as<DynamicStruct>();
    EXPECT_EQ(Void::VOID, subReader.get("voidField").as<Void>());
    EXPECT_EQ(false, subReader.get("boolField").as<bool>());
    EXPECT_EQ(0, subReader.get("int8Field").as<int8_t>());
    EXPECT_EQ(0, subReader.get("int16Field").as<int16_t>());
    EXPECT_EQ(0, subReader.get("int32Field").as<int32_t>());
    EXPECT_EQ(0, subReader.get("int64Field").as<int64_t>());
    EXPECT_EQ(0u, subReader.get("uInt8Field").as<uint8_t>());
    EXPECT_EQ(0u, subReader.get("uInt16Field").as<uint16_t>());
    EXPECT_EQ(0u, subReader.get("uInt32Field").as<uint32_t>());
    EXPECT_EQ(0u, subReader.get("uInt64Field").as<uint64_t>());
    EXPECT_FLOAT_EQ(0, subReader.get("float32Field").as<float>());
    EXPECT_DOUBLE_EQ(0, subReader.get("float64Field").as<double>());
    EXPECT_EQ("", subReader.get("textField").as<Text>());
    EXPECT_EQ("", subReader.get("dataField").as<Data>());
    {
      auto subSubReader = subReader.get("structField").as<DynamicStruct>();
      EXPECT_EQ("", subSubReader.get("textField").as<Text>());
      EXPECT_EQ("", subSubReader.get("structField").as<DynamicStruct>()
                                .get("textField").as<Text>());
    }

    EXPECT_EQ(0u, subReader.get("voidList").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("boolList").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("int8List").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("int16List").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("int32List").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("int64List").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("uInt8List").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("uInt16List").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("uInt32List").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("uInt64List").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("float32List").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("float64List").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("textList").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("dataList").as<DynamicList>().size());
    EXPECT_EQ(0u, subReader.get("structList").as<DynamicList>().size());
  }

  EXPECT_EQ(0u, reader.get("voidList").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("boolList").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("int8List").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("int16List").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("int32List").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("int64List").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("uInt8List").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("uInt16List").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("uInt32List").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("uInt64List").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("float32List").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("float64List").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("textList").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("dataList").as<DynamicList>().size());
  EXPECT_EQ(0u, reader.get("structList").as<DynamicList>().size());
}

#undef as

void dynamicInitListDefaults(DynamicStruct::Builder builder) {
  auto lists = builder.init("lists").as<DynamicStruct>();

  lists.init("list0", 2);
  lists.init("list1", 4);
  lists.init("list8", 2);
  lists.init("list16", 2);
  lists.init("list32", 2);
  lists.init("list64", 2);
  lists.init("listP", 2);

  lists.get("list0").as<DynamicList>()[0].as<DynamicStruct>().set("f", Void::VOID);
  lists.get("list0").as<DynamicList>()[1].as<DynamicStruct>().set("f", Void::VOID);
  lists.get("list1").as<DynamicList>()[0].as<DynamicStruct>().set("f", true);
  lists.get("list1").as<DynamicList>()[1].as<DynamicStruct>().set("f", false);
  lists.get("list1").as<DynamicList>()[2].as<DynamicStruct>().set("f", true);
  lists.get("list1").as<DynamicList>()[3].as<DynamicStruct>().set("f", true);
  lists.get("list8").as<DynamicList>()[0].as<DynamicStruct>().set("f", 123u);
  lists.get("list8").as<DynamicList>()[1].as<DynamicStruct>().set("f", 45u);
  lists.get("list16").as<DynamicList>()[0].as<DynamicStruct>().set("f", 12345u);
  lists.get("list16").as<DynamicList>()[1].as<DynamicStruct>().set("f", 6789u);
  lists.get("list32").as<DynamicList>()[0].as<DynamicStruct>().set("f", 123456789u);
  lists.get("list32").as<DynamicList>()[1].as<DynamicStruct>().set("f", 234567890u);
  lists.get("list64").as<DynamicList>()[0].as<DynamicStruct>().set("f", 1234567890123456u);
  lists.get("list64").as<DynamicList>()[1].as<DynamicStruct>().set("f", 2345678901234567u);
  lists.get("listP").as<DynamicList>()[0].as<DynamicStruct>().set("f", "foo");
  lists.get("listP").as<DynamicList>()[1].as<DynamicStruct>().set("f", "bar");

  {
    auto l = lists.init("int32ListList", 3).as<DynamicList>();
    l.init(0, 3).as<DynamicList>().copyFrom({1, 2, 3});
    l.init(1, 2).as<DynamicList>().copyFrom({4, 5});
    l.init(2, 1).as<DynamicList>().copyFrom({12341234});
  }

  {
    auto l = lists.init("textListList", 3).as<DynamicList>();
    l.init(0, 2).as<DynamicList>().copyFrom({"foo", "bar"});
    l.init(1, 1).as<DynamicList>().copyFrom({"baz"});
    l.init(2, 2).as<DynamicList>().copyFrom({"qux", "corge"});
  }

  {
    auto l = lists.init("structListList", 2).as<DynamicList>();
    auto e = l.init(0, 2).as<DynamicList>();
    e[0].as<TestAllTypes>().setInt32Field(123);
    e[1].as<TestAllTypes>().setInt32Field(456);
    e = l.init(1, 1).as<DynamicList>();
    e[0].as<TestAllTypes>().setInt32Field(789);
  }
}

// Hack because as<>() is a template-parameter-dependent lookup everywhere below...
#define as template as

template <typename Reader>
void dynamicCheckListDefaults(Reader reader) {
  auto lists = reader.get("lists").as<DynamicStruct>();

  ASSERT_EQ(2u, lists.get("list0").as<DynamicList>().size());
  ASSERT_EQ(4u, lists.get("list1").as<DynamicList>().size());
  ASSERT_EQ(2u, lists.get("list8").as<DynamicList>().size());
  ASSERT_EQ(2u, lists.get("list16").as<DynamicList>().size());
  ASSERT_EQ(2u, lists.get("list32").as<DynamicList>().size());
  ASSERT_EQ(2u, lists.get("list64").as<DynamicList>().size());
  ASSERT_EQ(2u, lists.get("listP").as<DynamicList>().size());

  EXPECT_EQ(Void::VOID, lists.get("list0").as<DynamicList>()[0].as<DynamicStruct>().get("f").as<Void>());
  EXPECT_EQ(Void::VOID, lists.get("list0").as<DynamicList>()[1].as<DynamicStruct>().get("f").as<Void>());
  EXPECT_TRUE(lists.get("list1").as<DynamicList>()[0].as<DynamicStruct>().get("f").as<bool>());
  EXPECT_FALSE(lists.get("list1").as<DynamicList>()[1].as<DynamicStruct>().get("f").as<bool>());
  EXPECT_TRUE(lists.get("list1").as<DynamicList>()[2].as<DynamicStruct>().get("f").as<bool>());
  EXPECT_TRUE(lists.get("list1").as<DynamicList>()[3].as<DynamicStruct>().get("f").as<bool>());
  EXPECT_EQ(123u, lists.get("list8").as<DynamicList>()[0].as<DynamicStruct>().get("f").as<uint8_t>());
  EXPECT_EQ(45u, lists.get("list8").as<DynamicList>()[1].as<DynamicStruct>().get("f").as<uint8_t>());
  EXPECT_EQ(12345u, lists.get("list16").as<DynamicList>()[0].as<DynamicStruct>().get("f").as<uint16_t>());
  EXPECT_EQ(6789u, lists.get("list16").as<DynamicList>()[1].as<DynamicStruct>().get("f").as<uint16_t>());
  EXPECT_EQ(123456789u, lists.get("list32").as<DynamicList>()[0].as<DynamicStruct>().get("f").as<uint32_t>());
  EXPECT_EQ(234567890u, lists.get("list32").as<DynamicList>()[1].as<DynamicStruct>().get("f").as<uint32_t>());
  EXPECT_EQ(1234567890123456u, lists.get("list64").as<DynamicList>()[0].as<DynamicStruct>().get("f").as<uint64_t>());
  EXPECT_EQ(2345678901234567u, lists.get("list64").as<DynamicList>()[1].as<DynamicStruct>().get("f").as<uint64_t>());
  EXPECT_EQ("foo", lists.get("listP").as<DynamicList>()[0].as<DynamicStruct>().get("f").as<Text>());
  EXPECT_EQ("bar", lists.get("listP").as<DynamicList>()[1].as<DynamicStruct>().get("f").as<Text>());

  {
    auto l = lists.get("int32ListList").as<DynamicList>();
    ASSERT_EQ(3u, l.size());
    checkList<int32_t>(l[0], {1, 2, 3});
    checkList<int32_t>(l[1], {4, 5});
    checkList<int32_t>(l[2], {12341234});
  }

  {
    auto l = lists.get("textListList").as<DynamicList>();
    ASSERT_EQ(3u, l.size());
    checkList<Text>(l[0], {"foo", "bar"});
    checkList<Text>(l[1], {"baz"});
    checkList<Text>(l[2], {"qux", "corge"});
  }

  {
    auto l = lists.get("structListList").as<DynamicList>();
    ASSERT_EQ(2u, l.size());
    auto e = l[0].as<DynamicList>();
    ASSERT_EQ(2u, e.size());
    EXPECT_EQ(123, e[0].as<TestAllTypes>().getInt32Field());
    EXPECT_EQ(456, e[1].as<TestAllTypes>().getInt32Field());
    e = l[1].as<DynamicList>();
    ASSERT_EQ(1u, e.size());
    EXPECT_EQ(789, e[0].as<TestAllTypes>().getInt32Field());
  }
}

#undef as

TEST(DynamicApi, Build) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestAllTypes>());

  dynamicInitTestMessage(root);
  checkTestMessage(root.asReader().as<TestAllTypes>());

  dynamicCheckTestMessage(root.asReader());
  dynamicCheckTestMessage(root);
}

TEST(DynamicApi, Read) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  initTestMessage(root);

  dynamicCheckTestMessage(toDynamic(root.asReader()));
  dynamicCheckTestMessage(toDynamic(root).asReader());
  dynamicCheckTestMessage(toDynamic(root));
}

TEST(DynamicApi, Defaults) {
  AlignedData<1> nullRoot = {{0, 0, 0, 0, 0, 0, 0, 0}};
  ArrayPtr<const word> segments[1] = {arrayPtr(nullRoot.words, 1)};
  SegmentArrayMessageReader reader(arrayPtr(segments, 1));
  auto root = reader.getRoot<DynamicStruct>(Schema::from<TestDefaults>());
  dynamicCheckTestMessage(root);
}

TEST(DynamicApi, DefaultsBuilder) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestDefaults>());

  checkTestMessage(root.asReader().as<TestDefaults>());
  dynamicCheckTestMessage(root.asReader());

  // This will initialize the whole message, replacing null pointers with copies of defaults.
  dynamicCheckTestMessage(root);

  // Check again now that the message is initialized.
  checkTestMessage(root.asReader().as<TestDefaults>());
  dynamicCheckTestMessage(root.asReader());
  dynamicCheckTestMessage(root);
}

TEST(DynamicApi, Zero) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestAllTypes>());

  dynamicCheckTestMessageAllZero(root.asReader());
  checkTestMessageAllZero(root.asReader().as<TestAllTypes>());
  dynamicCheckTestMessageAllZero(root);
  checkTestMessageAllZero(root.asReader().as<TestAllTypes>());
}

TEST(DynamicApi, ListListsBuild) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestListDefaults>());

  dynamicInitListDefaults(root);
  checkTestMessage(root.asReader().as<TestListDefaults>());

  dynamicCheckListDefaults(root.asReader());
  dynamicCheckListDefaults(root);
}

TEST(DynamicApi, ListListsRead) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestListDefaults>();

  initTestMessage(root);

  dynamicCheckListDefaults(toDynamic(root.asReader()));
  dynamicCheckListDefaults(toDynamic(root).asReader());
  dynamicCheckListDefaults(toDynamic(root));
}

TEST(DynamicApi, GenericObjects) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<test::TestObject>();

  dynamicInitTestMessage(root.initObjectField<DynamicStruct>(Schema::from<TestAllTypes>()));
  checkTestMessage(root.asReader().getObjectField<TestAllTypes>());

  dynamicCheckTestMessage(
      root.asReader().getObjectField<DynamicStruct>(Schema::from<TestAllTypes>()));
  dynamicCheckTestMessage(root.getObjectField<DynamicStruct>(Schema::from<TestAllTypes>()));

  {
    {
      auto list = root.initObjectField<DynamicList>(Schema::from<List<uint32_t>>(), 4);
      list.set(0, 123);
      list.set(1, 456);
      list.set(2, 789);
      list.set(3, 123456789);
    }

    {
      auto list = root.asReader().getObjectField<List<uint32_t>>();
      ASSERT_EQ(4u, list.size());
      EXPECT_EQ(123u, list[0]);
      EXPECT_EQ(456u, list[1]);
      EXPECT_EQ(789u, list[2]);
      EXPECT_EQ(123456789u, list[3]);
    }

    checkList<uint32_t>(root.asReader().getObjectField<DynamicList>(Schema::from<List<uint32_t>>()),
                        {123u, 456u, 789u, 123456789u});
    checkList<uint32_t>(root.getObjectField<DynamicList>(Schema::from<List<uint32_t>>()),
                        {123u, 456u, 789u, 123456789u});
  }
}

TEST(DynamicApi, DynamicGenericObjects) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<DynamicStruct>(Schema::from<test::TestObject>());

  dynamicInitTestMessage(root.initObject("objectField", Schema::from<TestAllTypes>()));
  checkTestMessage(root.asReader().as<test::TestObject>().getObjectField<TestAllTypes>());

  dynamicCheckTestMessage(
      root.asReader().get("objectField").as<DynamicObject>().as(Schema::from<TestAllTypes>()));
  dynamicCheckTestMessage(
      root.get("objectField").as<DynamicObject>().as(Schema::from<TestAllTypes>()));
  dynamicCheckTestMessage(
      root.getObject("objectField", Schema::from<TestAllTypes>()));

  {
    {
      auto list = root.initObject("objectField", Schema::from<List<uint32_t>>(), 4);
      list.set(0, 123);
      list.set(1, 456);
      list.set(2, 789);
      list.set(3, 123456789);
    }

    {
      auto list = root.asReader().as<test::TestObject>().getObjectField<List<uint32_t>>();
      ASSERT_EQ(4u, list.size());
      EXPECT_EQ(123u, list[0]);
      EXPECT_EQ(456u, list[1]);
      EXPECT_EQ(789u, list[2]);
      EXPECT_EQ(123456789u, list[3]);
    }

    checkList<uint32_t>(
        root.asReader().get("objectField").as<DynamicObject>().as(Schema::from<List<uint32_t>>()),
        {123u, 456u, 789u, 123456789u});
    checkList<uint32_t>(
        root.get("objectField").as<DynamicObject>().as(Schema::from<List<uint32_t>>()),
        {123u, 456u, 789u, 123456789u});
    checkList<uint32_t>(
        root.getObject("objectField", Schema::from<List<uint32_t>>()),
        {123u, 456u, 789u, 123456789u});
  }
}

TEST(DynamicApi, UnionsRead) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestUnion>();

  root.getUnion0().setU0f1s32(1234567);
  root.getUnion1().setU1f1sp("foo");
  root.getUnion2().setU2f0s1(true);
  root.getUnion3().setU3f0s64(1234567890123456789ll);

  {
    auto dynamic = toDynamic(root.asReader());
    {
      auto u = dynamic.get("union0").as<DynamicUnion>();
      ASSERT_TRUE(u.which() != nullptr);
      EXPECT_EQ("u0f1s32", u.which()->getProto().getName());
      EXPECT_EQ(1234567, u.get().as<int32_t>());
    }
    {
      auto u = dynamic.get("union1").as<DynamicUnion>();
      ASSERT_TRUE(u.which() != nullptr);
      EXPECT_EQ("u1f1sp", u.which()->getProto().getName());
      EXPECT_EQ("foo", u.get().as<Text>());
    }
    {
      auto u = dynamic.get("union2").as<DynamicUnion>();
      ASSERT_TRUE(u.which() != nullptr);
      EXPECT_EQ("u2f0s1", u.which()->getProto().getName());
      EXPECT_TRUE(u.get().as<bool>());
    }
    {
      auto u = dynamic.get("union3").as<DynamicUnion>();
      ASSERT_TRUE(u.which() != nullptr);
      EXPECT_EQ("u3f0s64", u.which()->getProto().getName());
      EXPECT_EQ(1234567890123456789ll, u.get().as<int64_t>());
    }
  }

  {
    // Again as a builder.
    auto dynamic = toDynamic(root);
    {
      auto u = dynamic.get("union0").as<DynamicUnion>();
      ASSERT_TRUE(u.which() != nullptr);
      EXPECT_EQ("u0f1s32", u.which()->getProto().getName());
      EXPECT_EQ(1234567, u.get().as<int32_t>());
    }
    {
      auto u = dynamic.get("union1").as<DynamicUnion>();
      ASSERT_TRUE(u.which() != nullptr);
      EXPECT_EQ("u1f1sp", u.which()->getProto().getName());
      EXPECT_EQ("foo", u.get().as<Text>());
    }
    {
      auto u = dynamic.get("union2").as<DynamicUnion>();
      ASSERT_TRUE(u.which() != nullptr);
      EXPECT_EQ("u2f0s1", u.which()->getProto().getName());
      EXPECT_TRUE(u.get().as<bool>());
    }
    {
      auto u = dynamic.get("union3").as<DynamicUnion>();
      ASSERT_TRUE(u.which() != nullptr);
      EXPECT_EQ("u3f0s64", u.which()->getProto().getName());
      EXPECT_EQ(1234567890123456789ll, u.get().as<int64_t>());
    }
  }
}

TEST(DynamicApi, UnionsWrite) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestUnion>());

  root.get("union0").as<DynamicUnion>().set("u0f1s32", 1234567);
  root.get("union1").as<DynamicUnion>().set("u1f1sp", "foo");
  root.get("union2").as<DynamicUnion>().set("u2f0s1", true);
  root.get("union3").as<DynamicUnion>().set("u3f0s64", 1234567890123456789ll);

  auto reader = root.asReader().as<TestUnion>();
  ASSERT_EQ(TestUnion::Union0::U0F1S32, reader.getUnion0().which());
  EXPECT_EQ(1234567, reader.getUnion0().getU0f1s32());

  ASSERT_EQ(TestUnion::Union1::U1F1SP, reader.getUnion1().which());
  EXPECT_EQ("foo", reader.getUnion1().getU1f1sp());

  ASSERT_EQ(TestUnion::Union2::U2F0S1, reader.getUnion2().which());
  EXPECT_TRUE(reader.getUnion2().getU2f0s1());

  ASSERT_EQ(TestUnion::Union3::U3F0S64, reader.getUnion3().which());
  EXPECT_EQ(1234567890123456789ll, reader.getUnion3().getU3f0s64());
}

TEST(DynamicApi, ConversionFailures) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestAllTypes>());

  root.set("int8Field", 123);
  EXPECT_ANY_THROW(root.set("int8Field", 1234));

  root.set("uInt32Field", 1);
  EXPECT_ANY_THROW(root.set("uInt32Field", -1));

  root.set("int16Field", 5);
  EXPECT_ANY_THROW(root.set("int16Field", 0.5));

  root.set("boolField", true);
  EXPECT_ANY_THROW(root.set("boolField", 1));
}

}  // namespace
}  // namespace internal
}  // namespace capnproto
