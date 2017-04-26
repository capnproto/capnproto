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

#include "test-util.h"
#include <kj/debug.h>
#include <kj/compat/gtest.h>

namespace capnp {
namespace _ {  // private
namespace {

template <typename Builder>
void genericInitTestMessage(Builder builder) {
  builder.setVoidField(VOID);
  builder.setVoidField();  // Means the same as above.
  builder.setBoolField(true);
  builder.setInt8Field(-123);
  builder.setInt16Field(-12345);
  builder.setInt32Field(-12345678);
  builder.setInt64Field(-123456789012345ll);
  builder.setUInt8Field(234u);
  builder.setUInt16Field(45678u);
  builder.setUInt32Field(3456789012u);
  builder.setUInt64Field(12345678901234567890ull);
  builder.setFloat32Field(1234.5);
  builder.setFloat64Field(-123e45);
  builder.setTextField("foo");
  builder.setDataField(data("bar"));
  {
    auto subBuilder = builder.initStructField();
    subBuilder.setVoidField(VOID);
    subBuilder.setBoolField(true);
    subBuilder.setInt8Field(-12);
    subBuilder.setInt16Field(3456);
    subBuilder.setInt32Field(-78901234);
    subBuilder.setInt64Field(56789012345678ll);
    subBuilder.setUInt8Field(90u);
    subBuilder.setUInt16Field(1234u);
    subBuilder.setUInt32Field(56789012u);
    subBuilder.setUInt64Field(345678901234567890ull);
    subBuilder.setFloat32Field(-1.25e-10f);
    subBuilder.setFloat64Field(345);
    subBuilder.setTextField("baz");
    subBuilder.setDataField(data("qux"));
    {
      auto subSubBuilder = subBuilder.initStructField();
      subSubBuilder.setTextField("nested");
      subSubBuilder.initStructField().setTextField("really nested");
    }
    subBuilder.setEnumField(TestEnum::BAZ);

    subBuilder.setVoidList({VOID, VOID, VOID});
    subBuilder.setBoolList({false, true, false, true, true});
    subBuilder.setInt8List({12, -34, -0x80, 0x7f});
    subBuilder.setInt16List({1234, -5678, -0x8000, 0x7fff});
    // gcc warns on -0x800... and the only work-around I could find was to do -0x7ff...-1.
    subBuilder.setInt32List({12345678, -90123456, -0x7fffffff - 1, 0x7fffffff});
    subBuilder.setInt64List({123456789012345ll, -678901234567890ll, -0x7fffffffffffffffll-1, 0x7fffffffffffffffll});
    subBuilder.setUInt8List({12u, 34u, 0u, 0xffu});
    subBuilder.setUInt16List({1234u, 5678u, 0u, 0xffffu});
    subBuilder.setUInt32List({12345678u, 90123456u, 0u, 0xffffffffu});
    subBuilder.setUInt64List({123456789012345ull, 678901234567890ull, 0ull, 0xffffffffffffffffull});
    subBuilder.setFloat32List({0, 1234567, 1e37f, -1e37f, 1e-37f, -1e-37f});
    subBuilder.setFloat64List({0, 123456789012345, 1e306, -1e306, 1e-306, -1e-306});
    subBuilder.setTextList({"quux", "corge", "grault"});
    subBuilder.setDataList({data("garply"), data("waldo"), data("fred")});
    {
      auto listBuilder = subBuilder.initStructList(3);
      listBuilder[0].setTextField("x structlist 1");
      listBuilder[1].setTextField("x structlist 2");
      listBuilder[2].setTextField("x structlist 3");
    }
    subBuilder.setEnumList({TestEnum::QUX, TestEnum::BAR, TestEnum::GRAULT});
  }
  builder.setEnumField(TestEnum::CORGE);

  builder.initVoidList(6);
  builder.setBoolList({true, false, false, true});
  builder.setInt8List({111, -111});
  builder.setInt16List({11111, -11111});
  builder.setInt32List({111111111, -111111111});
  builder.setInt64List({1111111111111111111ll, -1111111111111111111ll});
  builder.setUInt8List({111u, 222u});
  builder.setUInt16List({33333u, 44444u});
  builder.setUInt32List({3333333333u});
  builder.setUInt64List({11111111111111111111ull});
  builder.setFloat32List({5555.5, kj::inf(), -kj::inf(), kj::nan()});
  builder.setFloat64List({7777.75, kj::inf(), -kj::inf(), kj::nan()});
  builder.setTextList({"plugh", "xyzzy", "thud"});
  builder.setDataList({data("oops"), data("exhausted"), data("rfc3092")});
  {
    auto listBuilder = builder.initStructList(3);
    listBuilder[0].setTextField("structlist 1");
    listBuilder[1].setTextField("structlist 2");
    listBuilder[2].setTextField("structlist 3");
  }
  builder.setEnumList({TestEnum::FOO, TestEnum::GARPLY});
}

#if !CAPNP_LITE

void dynamicInitTestMessage(DynamicStruct::Builder builder) {
  builder.set("voidField", VOID);
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
  builder.set("dataField", data("bar"));
  {
    auto subBuilder = builder.init("structField").as<DynamicStruct>();
    subBuilder.set("voidField", VOID);
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
    subBuilder.set("dataField", data("qux"));
    {
      auto subSubBuilder = subBuilder.init("structField").as<DynamicStruct>();
      subSubBuilder.set("textField", "nested");
      subSubBuilder.init("structField").as<DynamicStruct>().set("textField", "really nested");
    }
    subBuilder.set("enumField", "baz");

    subBuilder.set("voidList", {VOID, VOID, VOID});
    subBuilder.set("boolList", {false, true, false, true, true});
    subBuilder.set("int8List", {12, -34, -0x80, 0x7f});
    subBuilder.set("int16List", {1234, -5678, -0x8000, 0x7fff});
    // gcc warns on -0x800... and the only work-around I could find was to do -0x7ff...-1.
    subBuilder.set("int32List", {12345678, -90123456, -0x7fffffff - 1, 0x7fffffff});
    subBuilder.set("int64List", {123456789012345ll, -678901234567890ll, -0x7fffffffffffffffll-1, 0x7fffffffffffffffll});
    subBuilder.set("uInt8List", {12u, 34u, 0u, 0xffu});
    subBuilder.set("uInt16List", {1234u, 5678u, 0u, 0xffffu});
    subBuilder.set("uInt32List", {12345678u, 90123456u, 0u, 0xffffffffu});
    subBuilder.set("uInt64List", {123456789012345ull, 678901234567890ull, 0ull, 0xffffffffffffffffull});
    subBuilder.set("float32List", {0, 1234567, 1e37, -1e37, 1e-37, -1e-37});
    subBuilder.set("float64List", {0, 123456789012345, 1e306, -1e306, 1e-306, -1e-306});
    subBuilder.set("textList", {"quux", "corge", "grault"});
    subBuilder.set("dataList", {data("garply"), data("waldo"), data("fred")});
    {
      auto listBuilder = subBuilder.init("structList", 3).as<DynamicList>();
      listBuilder[0].as<DynamicStruct>().set("textField", "x structlist 1");
      listBuilder[1].as<DynamicStruct>().set("textField", "x structlist 2");
      listBuilder[2].as<DynamicStruct>().set("textField", "x structlist 3");
    }
    subBuilder.set("enumList", {"qux", "bar", "grault"});
  }
  builder.set("enumField", "corge");

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
  builder.set("float32List", {5555.5, kj::inf(), -kj::inf(), kj::nan()});
  builder.set("float64List", {7777.75, kj::inf(), -kj::inf(), kj::nan()});
  builder.set("textList", {"plugh", "xyzzy", "thud"});
  builder.set("dataList", {data("oops"), data("exhausted"), data("rfc3092")});
  {
    auto listBuilder = builder.init("structList", 3).as<DynamicList>();
    listBuilder[0].as<DynamicStruct>().set("textField", "structlist 1");
    listBuilder[1].as<DynamicStruct>().set("textField", "structlist 2");
    listBuilder[2].as<DynamicStruct>().set("textField", "structlist 3");
  }
  builder.set("enumList", {"foo", "garply"});
}

#endif  // !CAPNP_LITE

inline bool isNaN(float f) { return f != f; }
inline bool isNaN(double f) { return f != f; }

template <typename Reader>
void genericCheckTestMessage(Reader reader) {
  EXPECT_EQ(VOID, reader.getVoidField());
  EXPECT_EQ(true, reader.getBoolField());
  EXPECT_EQ(-123, reader.getInt8Field());
  EXPECT_EQ(-12345, reader.getInt16Field());
  EXPECT_EQ(-12345678, reader.getInt32Field());
  EXPECT_EQ(-123456789012345ll, reader.getInt64Field());
  EXPECT_EQ(234u, reader.getUInt8Field());
  EXPECT_EQ(45678u, reader.getUInt16Field());
  EXPECT_EQ(3456789012u, reader.getUInt32Field());
  EXPECT_EQ(12345678901234567890ull, reader.getUInt64Field());
  EXPECT_FLOAT_EQ(1234.5f, reader.getFloat32Field());
  EXPECT_DOUBLE_EQ(-123e45, reader.getFloat64Field());
  EXPECT_EQ("foo", reader.getTextField());
  EXPECT_EQ(data("bar"), reader.getDataField());
  {
    auto subReader = reader.getStructField();
    EXPECT_EQ(VOID, subReader.getVoidField());
    EXPECT_EQ(true, subReader.getBoolField());
    EXPECT_EQ(-12, subReader.getInt8Field());
    EXPECT_EQ(3456, subReader.getInt16Field());
    EXPECT_EQ(-78901234, subReader.getInt32Field());
    EXPECT_EQ(56789012345678ll, subReader.getInt64Field());
    EXPECT_EQ(90u, subReader.getUInt8Field());
    EXPECT_EQ(1234u, subReader.getUInt16Field());
    EXPECT_EQ(56789012u, subReader.getUInt32Field());
    EXPECT_EQ(345678901234567890ull, subReader.getUInt64Field());
    EXPECT_FLOAT_EQ(-1.25e-10f, subReader.getFloat32Field());
    EXPECT_DOUBLE_EQ(345, subReader.getFloat64Field());
    EXPECT_EQ("baz", subReader.getTextField());
    EXPECT_EQ(data("qux"), subReader.getDataField());
    {
      auto subSubReader = subReader.getStructField();
      EXPECT_EQ("nested", subSubReader.getTextField());
      EXPECT_EQ("really nested", subSubReader.getStructField().getTextField());
    }
    EXPECT_EQ(TestEnum::BAZ, subReader.getEnumField());

    checkList(subReader.getVoidList(), {VOID, VOID, VOID});
    checkList(subReader.getBoolList(), {false, true, false, true, true});
    checkList(subReader.getInt8List(), {12, -34, -0x80, 0x7f});
    checkList(subReader.getInt16List(), {1234, -5678, -0x8000, 0x7fff});
    // gcc warns on -0x800... and the only work-around I could find was to do -0x7ff...-1.
    checkList(subReader.getInt32List(), {12345678, -90123456, -0x7fffffff - 1, 0x7fffffff});
    checkList(subReader.getInt64List(), {123456789012345ll, -678901234567890ll, -0x7fffffffffffffffll-1, 0x7fffffffffffffffll});
    checkList(subReader.getUInt8List(), {12u, 34u, 0u, 0xffu});
    checkList(subReader.getUInt16List(), {1234u, 5678u, 0u, 0xffffu});
    checkList(subReader.getUInt32List(), {12345678u, 90123456u, 0u, 0xffffffffu});
    checkList(subReader.getUInt64List(), {123456789012345ull, 678901234567890ull, 0ull, 0xffffffffffffffffull});
    checkList(subReader.getFloat32List(), {0.0f, 1234567.0f, 1e37f, -1e37f, 1e-37f, -1e-37f});
    checkList(subReader.getFloat64List(), {0.0, 123456789012345.0, 1e306, -1e306, 1e-306, -1e-306});
    checkList(subReader.getTextList(), {"quux", "corge", "grault"});
    checkList(subReader.getDataList(), {data("garply"), data("waldo"), data("fred")});
    {
      auto listReader = subReader.getStructList();
      ASSERT_EQ(3u, listReader.size());
      EXPECT_EQ("x structlist 1", listReader[0].getTextField());
      EXPECT_EQ("x structlist 2", listReader[1].getTextField());
      EXPECT_EQ("x structlist 3", listReader[2].getTextField());
    }
    checkList(subReader.getEnumList(), {TestEnum::QUX, TestEnum::BAR, TestEnum::GRAULT});
  }
  EXPECT_EQ(TestEnum::CORGE, reader.getEnumField());

  EXPECT_EQ(6u, reader.getVoidList().size());
  checkList(reader.getBoolList(), {true, false, false, true});
  checkList(reader.getInt8List(), {111, -111});
  checkList(reader.getInt16List(), {11111, -11111});
  checkList(reader.getInt32List(), {111111111, -111111111});
  checkList(reader.getInt64List(), {1111111111111111111ll, -1111111111111111111ll});
  checkList(reader.getUInt8List(), {111u, 222u});
  checkList(reader.getUInt16List(), {33333u, 44444u});
  checkList(reader.getUInt32List(), {3333333333u});
  checkList(reader.getUInt64List(), {11111111111111111111ull});
  {
    auto listReader = reader.getFloat32List();
    ASSERT_EQ(4u, listReader.size());
    EXPECT_EQ(5555.5f, listReader[0]);
    EXPECT_EQ(kj::inf(), listReader[1]);
    EXPECT_EQ(-kj::inf(), listReader[2]);
    EXPECT_TRUE(isNaN(listReader[3]));
  }
  {
    auto listReader = reader.getFloat64List();
    ASSERT_EQ(4u, listReader.size());
    EXPECT_EQ(7777.75, listReader[0]);
    EXPECT_EQ(kj::inf(), listReader[1]);
    EXPECT_EQ(-kj::inf(), listReader[2]);
    EXPECT_TRUE(isNaN(listReader[3]));
  }
  checkList(reader.getTextList(), {"plugh", "xyzzy", "thud"});
  checkList(reader.getDataList(), {data("oops"), data("exhausted"), data("rfc3092")});
  {
    auto listReader = reader.getStructList();
    ASSERT_EQ(3u, listReader.size());
    EXPECT_EQ("structlist 1", listReader[0].getTextField());
    EXPECT_EQ("structlist 2", listReader[1].getTextField());
    EXPECT_EQ("structlist 3", listReader[2].getTextField());
  }
  checkList(reader.getEnumList(), {TestEnum::FOO, TestEnum::GARPLY});
}

#if !CAPNP_LITE

// Hack because as<>() is a template-parameter-dependent lookup everywhere below...
#define as template as

Text::Reader name(DynamicEnum e) {
  KJ_IF_MAYBE(schema, e.getEnumerant()) {
    return schema->getProto().getName();
  } else {
    return "(unknown enumerant)";
  }
}

template <typename T>
void checkEnumList(T reader, std::initializer_list<const char*> expected) {
  auto list = reader.as<DynamicList>();
  ASSERT_EQ(expected.size(), list.size());
  for (uint i = 0; i < expected.size(); i++) {
    EXPECT_EQ(expected.begin()[i], name(list[i].as<DynamicEnum>()));
  }
}

template <typename Reader>
void dynamicCheckTestMessage(Reader reader) {
  EXPECT_EQ(VOID, reader.get("voidField").as<Void>());
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
  EXPECT_EQ(data("bar"), reader.get("dataField").as<Data>());
  {
    auto subReader = reader.get("structField").as<DynamicStruct>();
    EXPECT_EQ(VOID, subReader.get("voidField").as<Void>());
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
    EXPECT_EQ(data("qux"), subReader.get("dataField").as<Data>());
    {
      auto subSubReader = subReader.get("structField").as<DynamicStruct>();
      EXPECT_EQ("nested", subSubReader.get("textField").as<Text>());
      EXPECT_EQ("really nested", subSubReader.get("structField").as<DynamicStruct>()
                                             .get("textField").as<Text>());
    }
    EXPECT_EQ("baz", name(subReader.get("enumField").as<DynamicEnum>()));

    checkList<Void>(subReader.get("voidList"), {VOID, VOID, VOID});
    checkList<bool>(subReader.get("boolList"), {false, true, false, true, true});
    checkList<int8_t>(subReader.get("int8List"), {12, -34, -0x80, 0x7f});
    checkList<int16_t>(subReader.get("int16List"), {1234, -5678, -0x8000, 0x7fff});
    // gcc warns on -0x800... and the only work-around I could find was to do -0x7ff...-1.
    checkList<int32_t>(subReader.get("int32List"), {12345678, -90123456, -0x7fffffff-1, 0x7fffffff});
    checkList<int64_t>(subReader.get("int64List"), {123456789012345ll, -678901234567890ll, -0x7fffffffffffffffll-1, 0x7fffffffffffffffll});
    checkList<uint8_t>(subReader.get("uInt8List"), {12u, 34u, 0u, 0xffu});
    checkList<uint16_t>(subReader.get("uInt16List"), {1234u, 5678u, 0u, 0xffffu});
    checkList<uint32_t>(subReader.get("uInt32List"), {12345678u, 90123456u, 0u, 0xffffffffu});
    checkList<uint64_t>(subReader.get("uInt64List"), {123456789012345ull, 678901234567890ull, 0ull, 0xffffffffffffffffull});
    checkList<float>(subReader.get("float32List"), {0.0f, 1234567.0f, 1e37f, -1e37f, 1e-37f, -1e-37f});
    checkList<double>(subReader.get("float64List"), {0.0, 123456789012345.0, 1e306, -1e306, 1e-306, -1e-306});
    checkList<Text>(subReader.get("textList"), {"quux", "corge", "grault"});
    checkList<Data>(subReader.get("dataList"), {data("garply"), data("waldo"), data("fred")});
    {
      auto listReader = subReader.get("structList").as<DynamicList>();
      ASSERT_EQ(3u, listReader.size());
      EXPECT_EQ("x structlist 1", listReader[0].as<DynamicStruct>().get("textField").as<Text>());
      EXPECT_EQ("x structlist 2", listReader[1].as<DynamicStruct>().get("textField").as<Text>());
      EXPECT_EQ("x structlist 3", listReader[2].as<DynamicStruct>().get("textField").as<Text>());
    }
    checkEnumList(subReader.get("enumList"), {"qux", "bar", "grault"});
  }
  EXPECT_EQ("corge", name(reader.get("enumField").as<DynamicEnum>()));

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
    EXPECT_EQ(kj::inf(), listReader[1].as<float>());
    EXPECT_EQ(-kj::inf(), listReader[2].as<float>());
    EXPECT_TRUE(isNaN(listReader[3].as<float>()));
  }
  {
    auto listReader = reader.get("float64List").as<DynamicList>();
    ASSERT_EQ(4u, listReader.size());
    EXPECT_EQ(7777.75, listReader[0].as<double>());
    EXPECT_EQ(kj::inf(), listReader[1].as<double>());
    EXPECT_EQ(-kj::inf(), listReader[2].as<double>());
    EXPECT_TRUE(isNaN(listReader[3].as<double>()));
  }
  checkList<Text>(reader.get("textList"), {"plugh", "xyzzy", "thud"});
  checkList<Data>(reader.get("dataList"), {data("oops"), data("exhausted"), data("rfc3092")});
  {
    auto listReader = reader.get("structList").as<DynamicList>();
    ASSERT_EQ(3u, listReader.size());
    EXPECT_EQ("structlist 1", listReader[0].as<DynamicStruct>().get("textField").as<Text>());
    EXPECT_EQ("structlist 2", listReader[1].as<DynamicStruct>().get("textField").as<Text>());
    EXPECT_EQ("structlist 3", listReader[2].as<DynamicStruct>().get("textField").as<Text>());
  }
  checkEnumList(reader.get("enumList"), {"foo", "garply"});
}

#undef as

#endif  // !CAPNP_LITE

template <typename Reader>
void genericCheckTestMessageAllZero(Reader reader) {
  EXPECT_EQ(VOID, reader.getVoidField());
  EXPECT_EQ(false, reader.getBoolField());
  EXPECT_EQ(0, reader.getInt8Field());
  EXPECT_EQ(0, reader.getInt16Field());
  EXPECT_EQ(0, reader.getInt32Field());
  EXPECT_EQ(0, reader.getInt64Field());
  EXPECT_EQ(0u, reader.getUInt8Field());
  EXPECT_EQ(0u, reader.getUInt16Field());
  EXPECT_EQ(0u, reader.getUInt32Field());
  EXPECT_EQ(0u, reader.getUInt64Field());
  EXPECT_FLOAT_EQ(0, reader.getFloat32Field());
  EXPECT_DOUBLE_EQ(0, reader.getFloat64Field());
  EXPECT_EQ("", reader.getTextField());
  EXPECT_EQ(data(""), reader.getDataField());
  {
    auto subReader = reader.getStructField();
    EXPECT_EQ(VOID, subReader.getVoidField());
    EXPECT_EQ(false, subReader.getBoolField());
    EXPECT_EQ(0, subReader.getInt8Field());
    EXPECT_EQ(0, subReader.getInt16Field());
    EXPECT_EQ(0, subReader.getInt32Field());
    EXPECT_EQ(0, subReader.getInt64Field());
    EXPECT_EQ(0u, subReader.getUInt8Field());
    EXPECT_EQ(0u, subReader.getUInt16Field());
    EXPECT_EQ(0u, subReader.getUInt32Field());
    EXPECT_EQ(0u, subReader.getUInt64Field());
    EXPECT_FLOAT_EQ(0, subReader.getFloat32Field());
    EXPECT_DOUBLE_EQ(0, subReader.getFloat64Field());
    EXPECT_EQ("", subReader.getTextField());
    EXPECT_EQ(data(""), subReader.getDataField());
    {
      auto subSubReader = subReader.getStructField();
      EXPECT_EQ("", subSubReader.getTextField());
      EXPECT_EQ("", subSubReader.getStructField().getTextField());
    }

    EXPECT_EQ(0u, subReader.getVoidList().size());
    EXPECT_EQ(0u, subReader.getBoolList().size());
    EXPECT_EQ(0u, subReader.getInt8List().size());
    EXPECT_EQ(0u, subReader.getInt16List().size());
    EXPECT_EQ(0u, subReader.getInt32List().size());
    EXPECT_EQ(0u, subReader.getInt64List().size());
    EXPECT_EQ(0u, subReader.getUInt8List().size());
    EXPECT_EQ(0u, subReader.getUInt16List().size());
    EXPECT_EQ(0u, subReader.getUInt32List().size());
    EXPECT_EQ(0u, subReader.getUInt64List().size());
    EXPECT_EQ(0u, subReader.getFloat32List().size());
    EXPECT_EQ(0u, subReader.getFloat64List().size());
    EXPECT_EQ(0u, subReader.getTextList().size());
    EXPECT_EQ(0u, subReader.getDataList().size());
    EXPECT_EQ(0u, subReader.getStructList().size());
  }

  EXPECT_EQ(0u, reader.getVoidList().size());
  EXPECT_EQ(0u, reader.getBoolList().size());
  EXPECT_EQ(0u, reader.getInt8List().size());
  EXPECT_EQ(0u, reader.getInt16List().size());
  EXPECT_EQ(0u, reader.getInt32List().size());
  EXPECT_EQ(0u, reader.getInt64List().size());
  EXPECT_EQ(0u, reader.getUInt8List().size());
  EXPECT_EQ(0u, reader.getUInt16List().size());
  EXPECT_EQ(0u, reader.getUInt32List().size());
  EXPECT_EQ(0u, reader.getUInt64List().size());
  EXPECT_EQ(0u, reader.getFloat32List().size());
  EXPECT_EQ(0u, reader.getFloat64List().size());
  EXPECT_EQ(0u, reader.getTextList().size());
  EXPECT_EQ(0u, reader.getDataList().size());
  EXPECT_EQ(0u, reader.getStructList().size());
}

#if !CAPNP_LITE

// Hack because as<>() is a template-parameter-dependent lookup everywhere below...
#define as template as

template <typename Reader>
void dynamicCheckTestMessageAllZero(Reader reader) {
  EXPECT_EQ(VOID, reader.get("voidField").as<Void>());
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
  EXPECT_EQ(data(""), reader.get("dataField").as<Data>());
  {
    auto subReader = reader.get("structField").as<DynamicStruct>();
    EXPECT_EQ(VOID, subReader.get("voidField").as<Void>());
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
    EXPECT_EQ(data(""), subReader.get("dataField").as<Data>());
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

#endif  // !CAPNP_LITE

template <typename Builder>
void genericInitListDefaults(Builder builder) {
  auto lists = builder.initLists();

  lists.initList0(2);
  lists.initList1(4);
  lists.initList8(2);
  lists.initList16(2);
  lists.initList32(2);
  lists.initList64(2);
  lists.initListP(2);

  lists.getList0()[0].setF(VOID);
  lists.getList0()[1].setF(VOID);
  lists.getList1()[0].setF(true);
  lists.getList1()[1].setF(false);
  lists.getList1()[2].setF(true);
  lists.getList1()[3].setF(true);
  lists.getList8()[0].setF(123u);
  lists.getList8()[1].setF(45u);
  lists.getList16()[0].setF(12345u);
  lists.getList16()[1].setF(6789u);
  lists.getList32()[0].setF(123456789u);
  lists.getList32()[1].setF(234567890u);
  lists.getList64()[0].setF(1234567890123456u);
  lists.getList64()[1].setF(2345678901234567u);
  lists.getListP()[0].setF("foo");
  lists.getListP()[1].setF("bar");

  {
    auto l = lists.initInt32ListList(3);
    l.set(0, {1, 2, 3});
    l.set(1, {4, 5});
    l.set(2, {12341234});
  }

  {
    auto l = lists.initTextListList(3);
    l.set(0, {"foo", "bar"});
    l.set(1, {"baz"});
    l.set(2, {"qux", "corge"});
  }

  {
    auto l = lists.initStructListList(2);
    auto e = l.init(0, 2);
    e[0].setInt32Field(123);
    e[1].setInt32Field(456);
    e = l.init(1, 1);
    e[0].setInt32Field(789);
  }
}

#if !CAPNP_LITE

void dynamicInitListDefaults(DynamicStruct::Builder builder) {
  auto lists = builder.init("lists").as<DynamicStruct>();

  lists.init("list0", 2);
  lists.init("list1", 4);
  lists.init("list8", 2);
  lists.init("list16", 2);
  lists.init("list32", 2);
  lists.init("list64", 2);
  lists.init("listP", 2);

  lists.get("list0").as<DynamicList>()[0].as<DynamicStruct>().set("f", VOID);
  lists.get("list0").as<DynamicList>()[1].as<DynamicStruct>().set("f", VOID);
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

#endif  // !CAPNP_LITE

template <typename Reader>
void genericCheckListDefaults(Reader reader) {
  auto lists = reader.getLists();

  ASSERT_EQ(2u, lists.getList0().size());
  ASSERT_EQ(4u, lists.getList1().size());
  ASSERT_EQ(2u, lists.getList8().size());
  ASSERT_EQ(2u, lists.getList16().size());
  ASSERT_EQ(2u, lists.getList32().size());
  ASSERT_EQ(2u, lists.getList64().size());
  ASSERT_EQ(2u, lists.getListP().size());

  EXPECT_EQ(VOID, lists.getList0()[0].getF());
  EXPECT_EQ(VOID, lists.getList0()[1].getF());
  EXPECT_TRUE(lists.getList1()[0].getF());
  EXPECT_FALSE(lists.getList1()[1].getF());
  EXPECT_TRUE(lists.getList1()[2].getF());
  EXPECT_TRUE(lists.getList1()[3].getF());
  EXPECT_EQ(123u, lists.getList8()[0].getF());
  EXPECT_EQ(45u, lists.getList8()[1].getF());
  EXPECT_EQ(12345u, lists.getList16()[0].getF());
  EXPECT_EQ(6789u, lists.getList16()[1].getF());
  EXPECT_EQ(123456789u, lists.getList32()[0].getF());
  EXPECT_EQ(234567890u, lists.getList32()[1].getF());
  EXPECT_EQ(1234567890123456u, lists.getList64()[0].getF());
  EXPECT_EQ(2345678901234567u, lists.getList64()[1].getF());
  EXPECT_EQ("foo", lists.getListP()[0].getF());
  EXPECT_EQ("bar", lists.getListP()[1].getF());

  {
    auto l = lists.getInt32ListList();
    ASSERT_EQ(3u, l.size());
    checkList(l[0], {1, 2, 3});
    checkList(l[1], {4, 5});
    checkList(l[2], {12341234});
  }

  {
    auto l = lists.getTextListList();
    ASSERT_EQ(3u, l.size());
    checkList(l[0], {"foo", "bar"});
    checkList(l[1], {"baz"});
    checkList(l[2], {"qux", "corge"});
  }

  {
    auto l = lists.getStructListList();
    ASSERT_EQ(2u, l.size());
    auto e = l[0];
    ASSERT_EQ(2u, e.size());
    EXPECT_EQ(123, e[0].getInt32Field());
    EXPECT_EQ(456, e[1].getInt32Field());
    e = l[1];
    ASSERT_EQ(1u, e.size());
    EXPECT_EQ(789, e[0].getInt32Field());
  }
}

#if !CAPNP_LITE

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

  EXPECT_EQ(VOID, lists.get("list0").as<DynamicList>()[0].as<DynamicStruct>().get("f").as<Void>());
  EXPECT_EQ(VOID, lists.get("list0").as<DynamicList>()[1].as<DynamicStruct>().get("f").as<Void>());
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

#endif  // !CAPNP_LITE

}  // namespace

void initTestMessage(TestAllTypes::Builder builder) { genericInitTestMessage(builder); }
void initTestMessage(TestDefaults::Builder builder) { genericInitTestMessage(builder); }
void initTestMessage(TestListDefaults::Builder builder) { genericInitListDefaults(builder); }

void checkTestMessage(TestAllTypes::Builder builder) { genericCheckTestMessage(builder); }
void checkTestMessage(TestDefaults::Builder builder) { genericCheckTestMessage(builder); }
void checkTestMessage(TestListDefaults::Builder builder) { genericCheckListDefaults(builder); }

void checkTestMessage(TestAllTypes::Reader reader) { genericCheckTestMessage(reader); }
void checkTestMessage(TestDefaults::Reader reader) { genericCheckTestMessage(reader); }
void checkTestMessage(TestListDefaults::Reader reader) { genericCheckListDefaults(reader); }

void checkTestMessageAllZero(TestAllTypes::Builder builder) {
  genericCheckTestMessageAllZero(builder);
}
void checkTestMessageAllZero(TestAllTypes::Reader reader) {
  genericCheckTestMessageAllZero(reader);
}

#if !CAPNP_LITE

void initDynamicTestMessage(DynamicStruct::Builder builder) {
  dynamicInitTestMessage(builder);
}
void initDynamicTestLists(DynamicStruct::Builder builder) {
  dynamicInitListDefaults(builder);
}
void checkDynamicTestMessage(DynamicStruct::Builder builder) {
  dynamicCheckTestMessage(builder);
}
void checkDynamicTestLists(DynamicStruct::Builder builder) {
  dynamicCheckListDefaults(builder);
}
void checkDynamicTestMessage(DynamicStruct::Reader reader) {
  dynamicCheckTestMessage(reader);
}
void checkDynamicTestLists(DynamicStruct::Reader reader) {
  dynamicCheckListDefaults(reader);
}
void checkDynamicTestMessageAllZero(DynamicStruct::Builder builder) {
  dynamicCheckTestMessageAllZero(builder);
}
void checkDynamicTestMessageAllZero(DynamicStruct::Reader reader) {
  dynamicCheckTestMessageAllZero(reader);
}

#endif  // !CAPNP_LITE

// =======================================================================================
// Interface implementations.

#if !CAPNP_LITE

TestInterfaceImpl::TestInterfaceImpl(int& callCount): callCount(callCount) {}

kj::Promise<void> TestInterfaceImpl::foo(FooContext context) {
  ++callCount;
  auto params = context.getParams();
  auto result = context.getResults();
  EXPECT_EQ(123, params.getI());
  EXPECT_TRUE(params.getJ());
  result.setX("foo");
  return kj::READY_NOW;
}

kj::Promise<void> TestInterfaceImpl::baz(BazContext context) {
  ++callCount;
  auto params = context.getParams();
  checkTestMessage(params.getS());
  context.releaseParams();
  EXPECT_ANY_THROW(context.getParams());

  return kj::READY_NOW;
}

TestExtendsImpl::TestExtendsImpl(int& callCount): callCount(callCount) {}

kj::Promise<void> TestExtendsImpl::foo(FooContext context) {
  ++callCount;
  auto params = context.getParams();
  auto result = context.getResults();
  EXPECT_EQ(321, params.getI());
  EXPECT_FALSE(params.getJ());
  result.setX("bar");
  return kj::READY_NOW;
}

kj::Promise<void> TestExtendsImpl::grault(GraultContext context) {
  ++callCount;
  context.releaseParams();

  initTestMessage(context.getResults());

  return kj::READY_NOW;
}

TestPipelineImpl::TestPipelineImpl(int& callCount): callCount(callCount) {}

kj::Promise<void> TestPipelineImpl::getCap(GetCapContext context) {
  ++callCount;

  auto params = context.getParams();
  EXPECT_EQ(234, params.getN());

  auto cap = params.getInCap();
  context.releaseParams();

  auto request = cap.fooRequest();
  request.setI(123);
  request.setJ(true);

  return request.send().then(
      [this,KJ_CPCAP(context)](Response<test::TestInterface::FooResults>&& response) mutable {
        EXPECT_EQ("foo", response.getX());

        auto result = context.getResults();
        result.setS("bar");
        result.initOutBox().setCap(kj::heap<TestExtendsImpl>(callCount));
      });
}

kj::Promise<void> TestPipelineImpl::getAnyCap(GetAnyCapContext context) {
  ++callCount;

  auto params = context.getParams();
  EXPECT_EQ(234, params.getN());

  auto cap = params.getInCap();
  context.releaseParams();

  auto request = cap.castAs<test::TestInterface>().fooRequest();
  request.setI(123);
  request.setJ(true);

  return request.send().then(
      [this,KJ_CPCAP(context)](Response<test::TestInterface::FooResults>&& response) mutable {
        EXPECT_EQ("foo", response.getX());

        auto result = context.getResults();
        result.setS("bar");
        result.initOutBox().setCap(kj::heap<TestExtendsImpl>(callCount));
      });
}

kj::Promise<void> TestCallOrderImpl::getCallSequence(GetCallSequenceContext context) {
  auto result = context.getResults();
  result.setN(count++);
  return kj::READY_NOW;
}

TestTailCallerImpl::TestTailCallerImpl(int& callCount): callCount(callCount) {}

kj::Promise<void> TestTailCallerImpl::foo(FooContext context) {
  ++callCount;

  auto params = context.getParams();
  auto tailRequest = params.getCallee().fooRequest();
  tailRequest.setI(params.getI());
  tailRequest.setT("from TestTailCaller");
  return context.tailCall(kj::mv(tailRequest));
}

TestTailCalleeImpl::TestTailCalleeImpl(int& callCount): callCount(callCount) {}

kj::Promise<void> TestTailCalleeImpl::foo(FooContext context) {
  ++callCount;

  auto params = context.getParams();
  auto results = context.getResults();

  results.setI(params.getI());
  results.setT(params.getT());
  results.setC(kj::heap<TestCallOrderImpl>());

  return kj::READY_NOW;
}

TestMoreStuffImpl::TestMoreStuffImpl(int& callCount, int& handleCount)
    : callCount(callCount), handleCount(handleCount) {}

kj::Promise<void> TestMoreStuffImpl::getCallSequence(GetCallSequenceContext context) {
  auto result = context.getResults();
  result.setN(callCount++);
  return kj::READY_NOW;
}

kj::Promise<void> TestMoreStuffImpl::callFoo(CallFooContext context) {
  ++callCount;

  auto params = context.getParams();
  auto cap = params.getCap();

  auto request = cap.fooRequest();
  request.setI(123);
  request.setJ(true);

  return request.send().then(
      [KJ_CPCAP(context)](Response<test::TestInterface::FooResults>&& response) mutable {
        EXPECT_EQ("foo", response.getX());
        context.getResults().setS("bar");
      });
}

kj::Promise<void> TestMoreStuffImpl::callFooWhenResolved(CallFooWhenResolvedContext context) {
  ++callCount;

  auto params = context.getParams();
  auto cap = params.getCap();

  return cap.whenResolved().then([KJ_CPCAP(cap),KJ_CPCAP(context)]() mutable {
    auto request = cap.fooRequest();
    request.setI(123);
    request.setJ(true);

    return request.send().then(
        [KJ_CPCAP(context)](Response<test::TestInterface::FooResults>&& response) mutable {
          EXPECT_EQ("foo", response.getX());
          context.getResults().setS("bar");
        });
  });
}

kj::Promise<void> TestMoreStuffImpl::neverReturn(NeverReturnContext context) {
  ++callCount;

  // Attach `cap` to the promise to make sure it is released.
  auto promise = kj::Promise<void>(kj::NEVER_DONE).attach(context.getParams().getCap());

  // Also attach `cap` to the result struct to make sure that is released.
  context.getResults().setCapCopy(context.getParams().getCap());

  context.allowCancellation();
  return kj::mv(promise);
}

kj::Promise<void> TestMoreStuffImpl::hold(HoldContext context) {
  ++callCount;

  auto params = context.getParams();
  clientToHold = params.getCap();
  return kj::READY_NOW;
}

kj::Promise<void> TestMoreStuffImpl::callHeld(CallHeldContext context) {
  ++callCount;

  auto request = clientToHold.fooRequest();
  request.setI(123);
  request.setJ(true);

  return request.send().then(
      [KJ_CPCAP(context)](Response<test::TestInterface::FooResults>&& response) mutable {
        EXPECT_EQ("foo", response.getX());
        context.getResults().setS("bar");
      });
}

kj::Promise<void> TestMoreStuffImpl::getHeld(GetHeldContext context) {
  ++callCount;
  auto result = context.getResults();
  result.setCap(clientToHold);
  return kj::READY_NOW;
}

kj::Promise<void> TestMoreStuffImpl::echo(EchoContext context) {
  ++callCount;
  auto params = context.getParams();
  auto result = context.getResults();
  result.setCap(params.getCap());
  return kj::READY_NOW;
}

kj::Promise<void> TestMoreStuffImpl::expectCancel(ExpectCancelContext context) {
  auto cap = context.getParams().getCap();
  context.allowCancellation();
  return loop(0, cap, context);
}

kj::Promise<void> TestMoreStuffImpl::loop(uint depth, test::TestInterface::Client cap,
                                          ExpectCancelContext context) {
  if (depth > 100) {
    ADD_FAILURE() << "Looped too long, giving up.";
    return kj::READY_NOW;
  } else {
    return kj::evalLater([this,depth,KJ_CPCAP(cap),KJ_CPCAP(context)]() mutable {
      return loop(depth + 1, cap, context);
    });
  }
}

class HandleImpl final: public test::TestHandle::Server {
public:
  HandleImpl(int& count): count(count) { ++count; }
  ~HandleImpl() { --count; }

private:
  int& count;
};

kj::Promise<void> TestMoreStuffImpl::getHandle(GetHandleContext context) {
  context.getResults().setHandle(kj::heap<HandleImpl>(handleCount));
  return kj::READY_NOW;
}

kj::Promise<void> TestMoreStuffImpl::getNull(GetNullContext context) {
  return kj::READY_NOW;
}

kj::Promise<void> TestMoreStuffImpl::getEnormousString(GetEnormousStringContext context) {
  context.getResults().initStr(100000000);  // 100MB
  return kj::READY_NOW;
}

#endif  // !CAPNP_LITE

}  // namespace _ (private)
}  // namespace capnp
