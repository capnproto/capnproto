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

#include "test-util.h"
#include <gtest/gtest.h>

namespace capnproto {
namespace internal {
namespace {

template <typename Builder>
void genericInitTestMessage(Builder builder) {
  builder.setVoidField(Void::VOID);
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
  builder.setDataField("bar");
  {
    auto subBuilder = builder.initStructField();
    subBuilder.setVoidField(Void::VOID);
    subBuilder.setBoolField(true);
    subBuilder.setInt8Field(-12);
    subBuilder.setInt16Field(3456);
    subBuilder.setInt32Field(-78901234);
    subBuilder.setInt64Field(56789012345678ll);
    subBuilder.setUInt8Field(90u);
    subBuilder.setUInt16Field(1234u);
    subBuilder.setUInt32Field(56789012u);
    subBuilder.setUInt64Field(345678901234567890ull);
    subBuilder.setFloat32Field(-1.25e-10);
    subBuilder.setFloat64Field(345);
    subBuilder.setTextField("baz");
    subBuilder.setDataField("qux");
    {
      auto subSubBuilder = subBuilder.initStructField();
      subSubBuilder.setTextField("nested");
      subSubBuilder.initStructField().setTextField("really nested");
    }
    subBuilder.setEnumField(TestEnum::BAZ);

    subBuilder.setVoidList({Void::VOID, Void::VOID, Void::VOID});
    subBuilder.setBoolList({false, true, false, true, true});
    subBuilder.setInt8List({12, -34, -0x80, 0x7f});
    subBuilder.setInt16List({1234, -5678, -0x8000, 0x7fff});
    subBuilder.setInt32List({12345678, -90123456, -0x8000000, 0x7ffffff});
    // gcc warns on -0x800...ll and the only work-around I could find was to do -0x7ff...ll-1.
    subBuilder.setInt64List({123456789012345ll, -678901234567890ll, -0x7fffffffffffffffll-1, 0x7fffffffffffffffll});
    subBuilder.setUInt8List({12u, 34u, 0u, 0xffu});
    subBuilder.setUInt16List({1234u, 5678u, 0u, 0xffffu});
    subBuilder.setUInt32List({12345678u, 90123456u, 0u, 0xffffffffu});
    subBuilder.setUInt64List({123456789012345ull, 678901234567890ull, 0ull, 0xffffffffffffffffull});
    subBuilder.setFloat32List({0, 1234567, 1e37, -1e37, 1e-37, -1e-37});
    subBuilder.setFloat64List({0, 123456789012345, 1e306, -1e306, 1e-306, -1e-306});
    subBuilder.setTextList({"quux", "corge", "grault"});
    subBuilder.setDataList({"garply", "waldo", "fred"});
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
  builder.setFloat32List({5555.5,
                          std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()});
  builder.setFloat64List({7777.75,
                          std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()});
  builder.setTextList({"plugh", "xyzzy", "thud"});
  builder.setDataList({"oops", "exhausted", "rfc3092"});
  {
    auto listBuilder = builder.initStructList(3);
    listBuilder[0].setTextField("structlist 1");
    listBuilder[1].setTextField("structlist 2");
    listBuilder[2].setTextField("structlist 3");
  }
  builder.setEnumList({TestEnum::FOO, TestEnum::GARPLY});
}

template <typename T, typename U>
void checkList(T reader, std::initializer_list<U> expected) {
  ASSERT_EQ(expected.size(), reader.size());
  for (uint i = 0; i < expected.size(); i++) {
    EXPECT_EQ(expected.begin()[i], reader[i]);
  }
}

template <typename T>
void checkList(T reader, std::initializer_list<float> expected) {
  ASSERT_EQ(expected.size(), reader.size());
  for (uint i = 0; i < expected.size(); i++) {
    EXPECT_FLOAT_EQ(expected.begin()[i], reader[i]);
  }
}

template <typename T>
void checkList(T reader, std::initializer_list<double> expected) {
  ASSERT_EQ(expected.size(), reader.size());
  for (uint i = 0; i < expected.size(); i++) {
    EXPECT_DOUBLE_EQ(expected.begin()[i], reader[i]);
  }
}

inline bool isNaN(float f) { return f != f; }
inline bool isNaN(double f) { return f != f; }

template <typename Reader>
void genericCheckTestMessage(Reader reader) {
  EXPECT_EQ(Void::VOID, reader.getVoidField());
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
  EXPECT_EQ("bar", reader.getDataField());
  {
    auto subReader = reader.getStructField();
    EXPECT_EQ(Void::VOID, subReader.getVoidField());
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
    EXPECT_EQ("qux", subReader.getDataField());
    {
      auto subSubReader = subReader.getStructField();
      EXPECT_EQ("nested", subSubReader.getTextField());
      EXPECT_EQ("really nested", subSubReader.getStructField().getTextField());
    }
    EXPECT_EQ(TestEnum::BAZ, subReader.getEnumField());

    checkList(subReader.getVoidList(), {Void::VOID, Void::VOID, Void::VOID});
    checkList(subReader.getBoolList(), {false, true, false, true, true});
    checkList(subReader.getInt8List(), {12, -34, -0x80, 0x7f});
    checkList(subReader.getInt16List(), {1234, -5678, -0x8000, 0x7fff});
    checkList(subReader.getInt32List(), {12345678, -90123456, -0x8000000, 0x7ffffff});
    // gcc warns on -0x800...ll and the only work-around I could find was to do -0x7ff...ll-1.
    checkList(subReader.getInt64List(), {123456789012345ll, -678901234567890ll, -0x7fffffffffffffffll-1, 0x7fffffffffffffffll});
    checkList(subReader.getUInt8List(), {12u, 34u, 0u, 0xffu});
    checkList(subReader.getUInt16List(), {1234u, 5678u, 0u, 0xffffu});
    checkList(subReader.getUInt32List(), {12345678u, 90123456u, 0u, 0xffffffffu});
    checkList(subReader.getUInt64List(), {123456789012345ull, 678901234567890ull, 0ull, 0xffffffffffffffffull});
    checkList(subReader.getFloat32List(), {0.0f, 1234567.0f, 1e37f, -1e37f, 1e-37f, -1e-37f});
    checkList(subReader.getFloat64List(), {0.0, 123456789012345.0, 1e306, -1e306, 1e-306, -1e-306});
    checkList(subReader.getTextList(), {"quux", "corge", "grault"});
    checkList(subReader.getDataList(), {"garply", "waldo", "fred"});
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
    EXPECT_EQ(std::numeric_limits<float>::infinity(), listReader[1]);
    EXPECT_EQ(-std::numeric_limits<float>::infinity(), listReader[2]);
    EXPECT_TRUE(isNaN(listReader[3]));
  }
  {
    auto listReader = reader.getFloat64List();
    ASSERT_EQ(4u, listReader.size());
    EXPECT_EQ(7777.75, listReader[0]);
    EXPECT_EQ(std::numeric_limits<double>::infinity(), listReader[1]);
    EXPECT_EQ(-std::numeric_limits<double>::infinity(), listReader[2]);
    EXPECT_TRUE(isNaN(listReader[3]));
  }
  checkList(reader.getTextList(), {"plugh", "xyzzy", "thud"});
  checkList(reader.getDataList(), {"oops", "exhausted", "rfc3092"});
  {
    auto listReader = reader.getStructList();
    ASSERT_EQ(3u, listReader.size());
    EXPECT_EQ("structlist 1", listReader[0].getTextField());
    EXPECT_EQ("structlist 2", listReader[1].getTextField());
    EXPECT_EQ("structlist 3", listReader[2].getTextField());
  }
  checkList(reader.getEnumList(), {TestEnum::FOO, TestEnum::GARPLY});
}

template <typename Reader>
void genericCheckTestMessageAllZero(Reader reader) {
  EXPECT_EQ(Void::VOID, reader.getVoidField());
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
  EXPECT_EQ("", reader.getDataField());
  {
    auto subReader = reader.getStructField();
    EXPECT_EQ(Void::VOID, subReader.getVoidField());
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
    EXPECT_EQ("", subReader.getDataField());
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

template <typename Builder>
void genericInitInlineDefaults(Builder builder) {
  {
    auto normal = builder.initNormal();
    normal.getF8().setF0(true);
    normal.getF8().setF1(false);
    normal.getF8().setF2(true);

    normal.getF16().setF0(123u);
    normal.getF16().setF1(45u);
    normal.getF32().setF0(67u);
    normal.getF32().setF1(8901u);
    normal.getF64().setF0(234u);
    normal.getF64().setF1(567890123u);
    normal.getF128().setF0(1234567890123ull);
    normal.getF128().setF1(4567890123456ull);
    normal.getF192().setF0(7890123456789ull);
    normal.getF192().setF1(2345678901234ull);
    normal.getF192().setF2(5678901234567ull);

    normal.getF8p().getF().setF0(true);
    normal.getF8p().getF().setF1(true);
    normal.getF8p().getF().setF2(false);
    normal.getF16p().getF().setF0(98u);
    normal.getF16p().getF().setF1(76u);
    normal.getF32p().getF().setF0(54u);
    normal.getF32p().getF().setF1(32109u);
    normal.getF64p().getF().setF0(87u);
    normal.getF64p().getF().setF1(654321098u);
    normal.getF128p().getF().setF0(7654321098765ull);
    normal.getF128p().getF().setF1(4321098765432ull);
    normal.getF192p().getF().setF0(1098765432109ull);
    normal.getF192p().getF().setF1(8765432109876ull);
    normal.getF192p().getF().setF2(5432109876543ull);

    normal.getF0p().setP0("foo");
    normal.getF8p().setP0("baz");
    normal.getF16p().setP0("qux");
    normal.getF16p().setP1("quux");
    normal.getF32p().setP0("corge");
    normal.getF32p().setP1("grault");
    normal.getF64p().setP0("garply");
    normal.getF64p().setP1("waldo");
    normal.getF128p().setP0("fred");
    normal.getF128p().setP1("plugh");
    normal.getF128p().setP2("xyzzy");
    normal.getF192p().setP0("thud");
    normal.getF192p().setP1("foobar");
    normal.getF192p().setP2("barbaz");
  }

  {
    auto unions = builder.initUnions();

    unions.getUnion0().initF32().setF0(67u);
    unions.getUnion0().getF32().setF1(8901u);

    unions.getUnion1().initF128().setF0(1234567890123ull);
    unions.getUnion1().getF128().setF1(4567890123456ull);

    unions.getUnion3().initF16p().getF().setF0(98u);
    unions.getUnion3().getF16p().getF().setF1(76u);
    unions.getUnion3().getF16p().setP0("qux");
    unions.getUnion3().getF16p().setP1("quux");
  }

  {
    auto lists = builder.initLists();

    ASSERT_EQ(2u, lists.getVoidList().size());
    ASSERT_EQ(3u, lists.getBoolList().size());
    ASSERT_EQ(4u, lists.getUInt8List().size());
    ASSERT_EQ(5u, lists.getUInt16List().size());
    ASSERT_EQ(6u, lists.getUInt32List().size());
    ASSERT_EQ(7u, lists.getUInt64List().size());
    ASSERT_EQ(8u, lists.getTextList().size());

    ASSERT_EQ(2u, lists.getStructList0().size());
    ASSERT_EQ(3u, lists.getStructList1().size());
    ASSERT_EQ(4u, lists.getStructList8().size());
    ASSERT_EQ(2u, lists.getStructList16().size());
    ASSERT_EQ(3u, lists.getStructList32().size());
    ASSERT_EQ(4u, lists.getStructList64().size());
    ASSERT_EQ(2u, lists.getStructList128().size());
    ASSERT_EQ(3u, lists.getStructList192().size());

    ASSERT_EQ(4u, lists.getStructList0p().size());
    ASSERT_EQ(2u, lists.getStructList1p().size());
    ASSERT_EQ(3u, lists.getStructList8p().size());
    ASSERT_EQ(4u, lists.getStructList16p().size());
    ASSERT_EQ(2u, lists.getStructList32p().size());
    ASSERT_EQ(3u, lists.getStructList64p().size());
    ASSERT_EQ(4u, lists.getStructList128p().size());
    ASSERT_EQ(2u, lists.getStructList192p().size());

    lists.getVoidList().set(0, Void::VOID);
    lists.getVoidList().set(1, Void::VOID);
    lists.getBoolList().set(0, false);
    lists.getBoolList().set(1, true);
    lists.getBoolList().set(2, false);
    lists.getUInt8List().set(0, 12u);
    lists.getUInt8List().set(1, 34u);
    lists.getUInt8List().set(2, 56u);
    lists.getUInt8List().set(3, 78u);
    lists.getUInt16List().set(0, 1234u);
    lists.getUInt16List().set(1, 5678u);
    lists.getUInt16List().set(2, 9012u);
    lists.getUInt16List().set(3, 3456u);
    lists.getUInt16List().set(4, 7890u);
    lists.getUInt32List().set(0, 123456789u);
    lists.getUInt32List().set(1, 234567890u);
    lists.getUInt32List().set(2, 345678901u);
    lists.getUInt32List().set(3, 456789012u);
    lists.getUInt32List().set(4, 567890123u);
    lists.getUInt32List().set(5, 678901234u);
    for (uint i = 0; i < 7; i++) {
      lists.getUInt64List().set(i, i + 1);
    }
    lists.getTextList().set(0, "foo");
    lists.getTextList().set(1, "bar");
    lists.getTextList().set(2, "baz");
    lists.getTextList().set(3, "qux");
    lists.getTextList().set(4, "quux");
    lists.getTextList().set(5, "corge");
    lists.getTextList().set(6, "grault");
    lists.getTextList().set(7, "garply");

    lists.getStructList0()[0].setF(Void::VOID);
    lists.getStructList0()[1].setF(Void::VOID);

    lists.getStructList8()[0].setF0(true);
    lists.getStructList8()[0].setF1(false);
    lists.getStructList8()[0].setF2(false);
    lists.getStructList8()[1].setF0(false);
    lists.getStructList8()[1].setF1(true);
    lists.getStructList8()[1].setF2(false);
    lists.getStructList8()[2].setF0(true);
    lists.getStructList8()[2].setF1(true);
    lists.getStructList8()[2].setF2(false);
    lists.getStructList8()[3].setF0(false);
    lists.getStructList8()[3].setF1(false);
    lists.getStructList8()[3].setF2(true);

    lists.getStructList16()[0].setF0(12u);
    lists.getStructList16()[0].setF1(34u);
    lists.getStructList16()[1].setF0(56u);
    lists.getStructList16()[1].setF1(78u);

    lists.getStructList32()[0].setF0(90u);
    lists.getStructList32()[0].setF1(12345u);
    lists.getStructList32()[1].setF0(67u);
    lists.getStructList32()[1].setF1(8901u);
    lists.getStructList32()[2].setF0(23u);
    lists.getStructList32()[2].setF1(45678u);

    lists.getStructList64()[0].setF0(90u);
    lists.getStructList64()[0].setF1(123456789u);
    lists.getStructList64()[1].setF0(12u);
    lists.getStructList64()[1].setF1(345678901u);
    lists.getStructList64()[2].setF0(234u);
    lists.getStructList64()[2].setF1(567890123u);
    lists.getStructList64()[3].setF0(45u);
    lists.getStructList64()[3].setF1(678901234u);

    lists.getStructList128()[0].setF0(56789012345678ull);
    lists.getStructList128()[0].setF1(90123456789012ull);
    lists.getStructList128()[1].setF0(34567890123456ull);
    lists.getStructList128()[1].setF1(78901234567890ull);

    lists.getStructList192()[0].setF0(1234567890123ull);
    lists.getStructList192()[0].setF1(4567890123456ull);
    lists.getStructList192()[0].setF2(7890123456789ull);
    lists.getStructList192()[1].setF0( 123456789012ull);
    lists.getStructList192()[1].setF1(3456789012345ull);
    lists.getStructList192()[1].setF2(6789012345678ull);
    lists.getStructList192()[2].setF0(9012345678901ull);
    lists.getStructList192()[2].setF1(2345678901234ull);
    lists.getStructList192()[2].setF2(5678901234567ull);

    lists.getStructList0p()[0].setP0("foo");
    lists.getStructList0p()[1].setP0("bar");
    lists.getStructList0p()[2].setP0("baz");
    lists.getStructList0p()[3].setP0("qux");

    lists.getStructList8p()[0].getF().setF0(true);
    lists.getStructList8p()[0].setP0("grault");
    lists.getStructList8p()[1].setP0("garply");
    lists.getStructList8p()[2].setP0("waldo");

    lists.getStructList16p()[0].getF().setF0(123u);
    lists.getStructList16p()[0].setP0("fred");
    lists.getStructList16p()[0].setP1("plugh");
    lists.getStructList16p()[1].setP0("xyzzy");
    lists.getStructList16p()[1].setP1("thud");
    lists.getStructList16p()[2].setP0("foobar");
    lists.getStructList16p()[2].setP1("barbaz");
    lists.getStructList16p()[3].setP0("bazqux");
    lists.getStructList16p()[3].setP1("quxquux");

    lists.getStructList32p()[0].getF().setF1(12345u);
    lists.getStructList32p()[0].setP0("quuxcorge");
    lists.getStructList32p()[0].setP1("corgegrault");
    lists.getStructList32p()[1].setP0("graultgarply");
    lists.getStructList32p()[1].setP1("garplywaldo");

    lists.getStructList64p()[0].getF().setF1(123456789u);
    lists.getStructList64p()[0].setP0("waldofred");
    lists.getStructList64p()[0].setP1("fredplugh");
    lists.getStructList64p()[1].setP0("plughxyzzy");
    lists.getStructList64p()[1].setP1("xyzzythud");
    lists.getStructList64p()[2].setP0("thudfoo");
    lists.getStructList64p()[2].setP1("foofoo");

    lists.getStructList128p()[0].getF().setF1(123456789012345ull);
    lists.getStructList128p()[0].setP0("foobaz");
    lists.getStructList128p()[0].setP1("fooqux");
    lists.getStructList128p()[0].setP2("foocorge");
    lists.getStructList128p()[1].setP0("barbaz");
    lists.getStructList128p()[1].setP1("barqux");
    lists.getStructList128p()[1].setP2("barcorge");
    lists.getStructList128p()[2].setP0("bazbaz");
    lists.getStructList128p()[2].setP1("bazqux");
    lists.getStructList128p()[2].setP2("bazcorge");
    lists.getStructList128p()[3].setP0("quxbaz");
    lists.getStructList128p()[3].setP1("quxqux");
    lists.getStructList128p()[3].setP2("quxcorge");

    lists.getStructList192p()[0].getF().setF2(123456789012345ull);
    lists.getStructList192p()[0].setP0("corgebaz");
    lists.getStructList192p()[0].setP1("corgequx");
    lists.getStructList192p()[0].setP2("corgecorge");
    lists.getStructList192p()[1].setP0("graultbaz");
    lists.getStructList192p()[1].setP1("graultqux");
    lists.getStructList192p()[1].setP2("graultcorge");

    lists.setData("12345");
  }

  {
    auto sl = builder.initStructLists();

    sl.initList0(2);
    sl.initList1(2);
    sl.initList8(2);
    sl.initList16(2);
    sl.initList32(2);
    sl.initList64(2);
    sl.initListP(2);

    sl.getList0()[0].setF(Void::VOID);
    sl.getList0()[1].setF(Void::VOID);
    sl.getList1()[0].setF(true);
    sl.getList1()[1].setF(false);
    sl.getList8()[0].setF(123u);
    sl.getList8()[1].setF(45u);
    sl.getList16()[0].setF(12345u);
    sl.getList16()[1].setF(6789u);
    sl.getList32()[0].setF(123456789u);
    sl.getList32()[1].setF(234567890u);
    sl.getList64()[0].setF(1234567890123456u);
    sl.getList64()[1].setF(2345678901234567u);
    sl.getListP()[0].setF("foo");
    sl.getListP()[1].setF("bar");
  }

  {
    auto ll = builder.initListLists();

    {
      auto l = ll.initInt32ListList(3);
      l.init(0, 3).copyFrom({1, 2, 3});
      l.init(1, 2).copyFrom({4, 5});
      l.init(2, 1).copyFrom({12341234});
    }

    {
      auto l = ll.initTextListList(3);
      l.init(0, 2).copyFrom({"foo", "bar"});
      l.init(1, 1).copyFrom({"baz"});
      l.init(2, 2).copyFrom({"qux", "corge"});
    }

    {
      auto l = ll.initStructListList(2);
      auto e = l.init(0, 2);
      e[0].setInt32Field(123);
      e[1].setInt32Field(456);
      e = l.init(1, 1);
      e[0].setInt32Field(789);
    }

    {
      auto l = ll.initInt32InlineListList(2);
      l[0].copyFrom({1, 2, 3, 4, 5, 6, 123456789});
      l[1].copyFrom({987654321, 6, 5, 4, 3, 2, 1});
    }

    {
      auto l = ll.initTextInlineListList(3);
      l[0].copyFrom({"grault1", "grault2", "grault3", "grault4", "grault5"});
      l[1].copyFrom({"garply1", "garply2", "garply3", "garply4", "garply5"});
      l[2].copyFrom({"waldo1", "waldo2", "waldo3", "waldo4", "waldo5"});
    }

    {
      auto l = ll.initStructInlineListList(3);
      ASSERT_EQ(3u, l[0].size());
      ASSERT_EQ(3u, l[1].size());
      ASSERT_EQ(3u, l[2].size());

      l[0][0].getF().setF1(123);
      l[0][1].getF().setF1(456);
      l[0][2].getF().setF1(789);
      l[1][0].getF().setF1(321);
      l[1][1].getF().setF1(654);
      l[1][2].getF().setF1(987);
      l[2][0].getF().setF1(111);
      l[2][1].getF().setF1(222);
      l[2][2].getF().setF1(333);

      l[0][0].setP0("fred1");
      l[0][1].setP0("fred2");
      l[0][2].setP0("fred3");
      l[1][0].setP0("plugh1");
      l[1][1].setP0("plugh2");
      l[1][2].setP0("plugh3");
      l[2][0].setP0("thud1");
      l[2][1].setP0("thud2");
      l[2][2].setP0("thud3");
    }

    ll.setInlineDataList({"123456789", "234567890", "345678901", "456789012", "567890123"});

    {
      auto l = ll.initInt32InlineListListList(3);
      l.init(0, 3);
      l.init(1, 2);
      l.init(2, 1);
      ASSERT_EQ(2u, l[0][0].size());
      ASSERT_EQ(2u, l[0][1].size());
      ASSERT_EQ(2u, l[0][2].size());
      ASSERT_EQ(2u, l[1][0].size());
      ASSERT_EQ(2u, l[1][1].size());
      ASSERT_EQ(2u, l[2][0].size());

      l[0][0].copyFrom({1, 2});
      l[0][1].copyFrom({3, 4});
      l[0][2].copyFrom({5, 6});
      l[1][0].copyFrom({7, 8});
      l[1][1].copyFrom({9, 10});
      l[2][0].copyFrom({1234567, 7654321});
    }

    {
      auto l = ll.initTextInlineListListList(2);
      l.init(0, 2);
      l.init(1, 1);
      ASSERT_EQ(5u, l[0][0].size());
      ASSERT_EQ(5u, l[0][1].size());
      ASSERT_EQ(5u, l[1][0].size());

      l[0][0].copyFrom({"1", "2", "3", "4", "5"});
      l[0][1].copyFrom({"foo", "bar", "baz", "qux", "corge"});
      l[1][0].copyFrom({"z", "y", "x", "w", "v"});
    }

    {
      auto l = ll.initStructInlineListListList(2);
      l.init(0, 2);
      l.init(1, 1);
      ASSERT_EQ(3u, l[0][0].size());
      ASSERT_EQ(3u, l[0][1].size());
      ASSERT_EQ(3u, l[1][0].size());

      l[0][0][0].getF().setF1(123);
      l[0][0][1].getF().setF1(456);
      l[0][0][2].getF().setF1(789);
      l[0][1][0].getF().setF1(321);
      l[0][1][1].getF().setF1(654);
      l[0][1][2].getF().setF1(987);
      l[1][0][0].getF().setF1(111);
      l[1][0][1].getF().setF1(222);
      l[1][0][2].getF().setF1(333);

      l[0][0][0].setP0("fred1");
      l[0][0][1].setP0("fred2");
      l[0][0][2].setP0("fred3");
      l[0][1][0].setP0("plugh1");
      l[0][1][1].setP0("plugh2");
      l[0][1][2].setP0("plugh3");
      l[1][0][0].setP0("thud1");
      l[1][0][1].setP0("thud2");
      l[1][0][2].setP0("thud3");
    }

    {
      auto l = ll.initInlineDataListList(2);
      l.init(0, 3).copyFrom({"foo", "bar", "baz"});
      l.init(1, 2).copyFrom({"123", "234"});
    }
  }
}

template <typename Reader>
void genericCheckInlineDefaults(Reader reader) {
  {
    auto normal = reader.getNormal();
    EXPECT_TRUE(normal.getF8().getF0());
    EXPECT_FALSE(normal.getF8().getF1());
    EXPECT_TRUE(normal.getF8().getF2());
    EXPECT_EQ(123u, normal.getF16().getF0());
    EXPECT_EQ(45u, normal.getF16().getF1());
    EXPECT_EQ(67u, normal.getF32().getF0());
    EXPECT_EQ(8901u, normal.getF32().getF1());
    EXPECT_EQ(234u, normal.getF64().getF0());
    EXPECT_EQ(567890123u, normal.getF64().getF1());
    EXPECT_EQ(1234567890123ull, normal.getF128().getF0());
    EXPECT_EQ(4567890123456ull, normal.getF128().getF1());
    EXPECT_EQ(7890123456789ull, normal.getF192().getF0());
    EXPECT_EQ(2345678901234ull, normal.getF192().getF1());
    EXPECT_EQ(5678901234567ull, normal.getF192().getF2());

    EXPECT_TRUE(normal.getF8p().getF().getF0());
    EXPECT_TRUE(normal.getF8p().getF().getF1());
    EXPECT_FALSE(normal.getF8p().getF().getF2());
    EXPECT_EQ(98u, normal.getF16p().getF().getF0());
    EXPECT_EQ(76u, normal.getF16p().getF().getF1());
    EXPECT_EQ(54u, normal.getF32p().getF().getF0());
    EXPECT_EQ(32109u, normal.getF32p().getF().getF1());
    EXPECT_EQ(87u, normal.getF64p().getF().getF0());
    EXPECT_EQ(654321098u, normal.getF64p().getF().getF1());
    EXPECT_EQ(7654321098765ull, normal.getF128p().getF().getF0());
    EXPECT_EQ(4321098765432ull, normal.getF128p().getF().getF1());
    EXPECT_EQ(1098765432109ull, normal.getF192p().getF().getF0());
    EXPECT_EQ(8765432109876ull, normal.getF192p().getF().getF1());
    EXPECT_EQ(5432109876543ull, normal.getF192p().getF().getF2());

    EXPECT_EQ("foo", normal.getF0p().getP0());
    EXPECT_EQ("baz", normal.getF8p().getP0());
    EXPECT_EQ("qux", normal.getF16p().getP0());
    EXPECT_EQ("quux", normal.getF16p().getP1());
    EXPECT_EQ("corge", normal.getF32p().getP0());
    EXPECT_EQ("grault", normal.getF32p().getP1());
    EXPECT_EQ("garply", normal.getF64p().getP0());
    EXPECT_EQ("waldo", normal.getF64p().getP1());
    EXPECT_EQ("fred", normal.getF128p().getP0());
    EXPECT_EQ("plugh", normal.getF128p().getP1());
    EXPECT_EQ("xyzzy", normal.getF128p().getP2());
    EXPECT_EQ("thud", normal.getF192p().getP0());
    EXPECT_EQ("foobar", normal.getF192p().getP1());
    EXPECT_EQ("barbaz", normal.getF192p().getP2());
  }

  {
    auto unions = reader.getUnions();

    ASSERT_EQ(TestInlineUnions::Union0::F32, unions.getUnion0().which());
    EXPECT_EQ(67u, unions.getUnion0().getF32().getF0());
    EXPECT_EQ(8901u, unions.getUnion0().getF32().getF1());

    ASSERT_EQ(TestInlineUnions::Union1::F128, unions.getUnion1().which());
    EXPECT_EQ(1234567890123ull, unions.getUnion1().getF128().getF0());
    EXPECT_EQ(4567890123456ull, unions.getUnion1().getF128().getF1());

    ASSERT_EQ(TestInlineUnions::Union3::F16P, unions.getUnion3().which());
    EXPECT_EQ(98u, unions.getUnion3().getF16p().getF().getF0());
    EXPECT_EQ(76u, unions.getUnion3().getF16p().getF().getF1());
    EXPECT_EQ("qux", unions.getUnion3().getF16p().getP0());
    EXPECT_EQ("quux", unions.getUnion3().getF16p().getP1());
  }

  {
    auto lists = reader.getLists();

    ASSERT_EQ(2u, lists.getVoidList().size());
    ASSERT_EQ(3u, lists.getBoolList().size());
    ASSERT_EQ(4u, lists.getUInt8List().size());
    ASSERT_EQ(5u, lists.getUInt16List().size());
    ASSERT_EQ(6u, lists.getUInt32List().size());
    ASSERT_EQ(7u, lists.getUInt64List().size());
    ASSERT_EQ(8u, lists.getTextList().size());

    ASSERT_EQ(2u, lists.getStructList0().size());
    ASSERT_EQ(3u, lists.getStructList1().size());
    ASSERT_EQ(4u, lists.getStructList8().size());
    ASSERT_EQ(2u, lists.getStructList16().size());
    ASSERT_EQ(3u, lists.getStructList32().size());
    ASSERT_EQ(4u, lists.getStructList64().size());
    ASSERT_EQ(2u, lists.getStructList128().size());
    ASSERT_EQ(3u, lists.getStructList192().size());

    ASSERT_EQ(4u, lists.getStructList0p().size());
    ASSERT_EQ(2u, lists.getStructList1p().size());
    ASSERT_EQ(3u, lists.getStructList8p().size());
    ASSERT_EQ(4u, lists.getStructList16p().size());
    ASSERT_EQ(2u, lists.getStructList32p().size());
    ASSERT_EQ(3u, lists.getStructList64p().size());
    ASSERT_EQ(4u, lists.getStructList128p().size());
    ASSERT_EQ(2u, lists.getStructList192p().size());

    EXPECT_EQ(Void::VOID, lists.getVoidList()[0]);
    EXPECT_EQ(Void::VOID, lists.getVoidList()[1]);
    EXPECT_FALSE(lists.getBoolList()[0]);
    EXPECT_TRUE(lists.getBoolList()[1]);
    EXPECT_FALSE(lists.getBoolList()[2]);
    EXPECT_EQ(12u, lists.getUInt8List()[0]);
    EXPECT_EQ(34u, lists.getUInt8List()[1]);
    EXPECT_EQ(56u, lists.getUInt8List()[2]);
    EXPECT_EQ(78u, lists.getUInt8List()[3]);
    EXPECT_EQ(1234u, lists.getUInt16List()[0]);
    EXPECT_EQ(5678u, lists.getUInt16List()[1]);
    EXPECT_EQ(9012u, lists.getUInt16List()[2]);
    EXPECT_EQ(3456u, lists.getUInt16List()[3]);
    EXPECT_EQ(7890u, lists.getUInt16List()[4]);
    EXPECT_EQ(123456789u, lists.getUInt32List()[0]);
    EXPECT_EQ(234567890u, lists.getUInt32List()[1]);
    EXPECT_EQ(345678901u, lists.getUInt32List()[2]);
    EXPECT_EQ(456789012u, lists.getUInt32List()[3]);
    EXPECT_EQ(567890123u, lists.getUInt32List()[4]);
    EXPECT_EQ(678901234u, lists.getUInt32List()[5]);
    for (uint i = 0; i < 7; i++) {
      EXPECT_EQ(i + 1, lists.getUInt64List()[i]);
    }
    EXPECT_EQ("foo", lists.getTextList()[0]);
    EXPECT_EQ("bar", lists.getTextList()[1]);
    EXPECT_EQ("baz", lists.getTextList()[2]);
    EXPECT_EQ("qux", lists.getTextList()[3]);
    EXPECT_EQ("quux", lists.getTextList()[4]);
    EXPECT_EQ("corge", lists.getTextList()[5]);
    EXPECT_EQ("grault", lists.getTextList()[6]);
    EXPECT_EQ("garply", lists.getTextList()[7]);

    EXPECT_EQ(Void::VOID, lists.getStructList0()[0].getF());
    EXPECT_EQ(Void::VOID, lists.getStructList0()[1].getF());

    EXPECT_TRUE (lists.getStructList8()[0].getF0());
    EXPECT_FALSE(lists.getStructList8()[0].getF1());
    EXPECT_FALSE(lists.getStructList8()[0].getF2());
    EXPECT_FALSE(lists.getStructList8()[1].getF0());
    EXPECT_TRUE (lists.getStructList8()[1].getF1());
    EXPECT_FALSE(lists.getStructList8()[1].getF2());
    EXPECT_TRUE (lists.getStructList8()[2].getF0());
    EXPECT_TRUE (lists.getStructList8()[2].getF1());
    EXPECT_FALSE(lists.getStructList8()[2].getF2());
    EXPECT_FALSE(lists.getStructList8()[3].getF0());
    EXPECT_FALSE(lists.getStructList8()[3].getF1());
    EXPECT_TRUE (lists.getStructList8()[3].getF2());

    EXPECT_EQ(12u, lists.getStructList16()[0].getF0());
    EXPECT_EQ(34u, lists.getStructList16()[0].getF1());
    EXPECT_EQ(56u, lists.getStructList16()[1].getF0());
    EXPECT_EQ(78u, lists.getStructList16()[1].getF1());

    EXPECT_EQ(90u, lists.getStructList32()[0].getF0());
    EXPECT_EQ(12345u, lists.getStructList32()[0].getF1());
    EXPECT_EQ(67u, lists.getStructList32()[1].getF0());
    EXPECT_EQ(8901u, lists.getStructList32()[1].getF1());
    EXPECT_EQ(23u, lists.getStructList32()[2].getF0());
    EXPECT_EQ(45678u, lists.getStructList32()[2].getF1());

    EXPECT_EQ(90u, lists.getStructList64()[0].getF0());
    EXPECT_EQ(123456789u, lists.getStructList64()[0].getF1());
    EXPECT_EQ(12u, lists.getStructList64()[1].getF0());
    EXPECT_EQ(345678901u, lists.getStructList64()[1].getF1());
    EXPECT_EQ(234u, lists.getStructList64()[2].getF0());
    EXPECT_EQ(567890123u, lists.getStructList64()[2].getF1());
    EXPECT_EQ(45u, lists.getStructList64()[3].getF0());
    EXPECT_EQ(678901234u, lists.getStructList64()[3].getF1());

    EXPECT_EQ(56789012345678ull, lists.getStructList128()[0].getF0());
    EXPECT_EQ(90123456789012ull, lists.getStructList128()[0].getF1());
    EXPECT_EQ(34567890123456ull, lists.getStructList128()[1].getF0());
    EXPECT_EQ(78901234567890ull, lists.getStructList128()[1].getF1());

    EXPECT_EQ(1234567890123ull, lists.getStructList192()[0].getF0());
    EXPECT_EQ(4567890123456ull, lists.getStructList192()[0].getF1());
    EXPECT_EQ(7890123456789ull, lists.getStructList192()[0].getF2());
    EXPECT_EQ( 123456789012ull, lists.getStructList192()[1].getF0());
    EXPECT_EQ(3456789012345ull, lists.getStructList192()[1].getF1());
    EXPECT_EQ(6789012345678ull, lists.getStructList192()[1].getF2());
    EXPECT_EQ(9012345678901ull, lists.getStructList192()[2].getF0());
    EXPECT_EQ(2345678901234ull, lists.getStructList192()[2].getF1());
    EXPECT_EQ(5678901234567ull, lists.getStructList192()[2].getF2());

    EXPECT_EQ("foo", lists.getStructList0p()[0].getP0());
    EXPECT_EQ("bar", lists.getStructList0p()[1].getP0());
    EXPECT_EQ("baz", lists.getStructList0p()[2].getP0());
    EXPECT_EQ("qux", lists.getStructList0p()[3].getP0());

    EXPECT_TRUE(lists.getStructList8p()[0].getF().getF0());
    EXPECT_EQ("grault", lists.getStructList8p()[0].getP0());
    EXPECT_EQ("garply", lists.getStructList8p()[1].getP0());
    EXPECT_EQ("waldo", lists.getStructList8p()[2].getP0());

    EXPECT_EQ(123u, lists.getStructList16p()[0].getF().getF0());
    EXPECT_EQ("fred", lists.getStructList16p()[0].getP0());
    EXPECT_EQ("plugh", lists.getStructList16p()[0].getP1());
    EXPECT_EQ("xyzzy", lists.getStructList16p()[1].getP0());
    EXPECT_EQ("thud", lists.getStructList16p()[1].getP1());
    EXPECT_EQ("foobar", lists.getStructList16p()[2].getP0());
    EXPECT_EQ("barbaz", lists.getStructList16p()[2].getP1());
    EXPECT_EQ("bazqux", lists.getStructList16p()[3].getP0());
    EXPECT_EQ("quxquux", lists.getStructList16p()[3].getP1());

    EXPECT_EQ(12345u, lists.getStructList32p()[0].getF().getF1());
    EXPECT_EQ("quuxcorge", lists.getStructList32p()[0].getP0());
    EXPECT_EQ("corgegrault", lists.getStructList32p()[0].getP1());
    EXPECT_EQ("graultgarply", lists.getStructList32p()[1].getP0());
    EXPECT_EQ("garplywaldo", lists.getStructList32p()[1].getP1());

    EXPECT_EQ(123456789u, lists.getStructList64p()[0].getF().getF1());
    EXPECT_EQ("waldofred", lists.getStructList64p()[0].getP0());
    EXPECT_EQ("fredplugh", lists.getStructList64p()[0].getP1());
    EXPECT_EQ("plughxyzzy", lists.getStructList64p()[1].getP0());
    EXPECT_EQ("xyzzythud", lists.getStructList64p()[1].getP1());
    EXPECT_EQ("thudfoo", lists.getStructList64p()[2].getP0());
    EXPECT_EQ("foofoo", lists.getStructList64p()[2].getP1());

    EXPECT_EQ(123456789012345ull, lists.getStructList128p()[0].getF().getF1());
    EXPECT_EQ("foobaz", lists.getStructList128p()[0].getP0());
    EXPECT_EQ("fooqux", lists.getStructList128p()[0].getP1());
    EXPECT_EQ("foocorge", lists.getStructList128p()[0].getP2());
    EXPECT_EQ("barbaz", lists.getStructList128p()[1].getP0());
    EXPECT_EQ("barqux", lists.getStructList128p()[1].getP1());
    EXPECT_EQ("barcorge", lists.getStructList128p()[1].getP2());
    EXPECT_EQ("bazbaz", lists.getStructList128p()[2].getP0());
    EXPECT_EQ("bazqux", lists.getStructList128p()[2].getP1());
    EXPECT_EQ("bazcorge", lists.getStructList128p()[2].getP2());
    EXPECT_EQ("quxbaz", lists.getStructList128p()[3].getP0());
    EXPECT_EQ("quxqux", lists.getStructList128p()[3].getP1());
    EXPECT_EQ("quxcorge", lists.getStructList128p()[3].getP2());

    EXPECT_EQ(123456789012345ull, lists.getStructList192p()[0].getF().getF2());
    EXPECT_EQ("corgebaz", lists.getStructList192p()[0].getP0());
    EXPECT_EQ("corgequx", lists.getStructList192p()[0].getP1());
    EXPECT_EQ("corgecorge", lists.getStructList192p()[0].getP2());
    EXPECT_EQ("graultbaz", lists.getStructList192p()[1].getP0());
    EXPECT_EQ("graultqux", lists.getStructList192p()[1].getP1());
    EXPECT_EQ("graultcorge", lists.getStructList192p()[1].getP2());

    EXPECT_EQ("12345", lists.getData());
  }

  {
    auto sl = reader.getStructLists();

    ASSERT_EQ(2u, sl.getList0().size());
    ASSERT_EQ(2u, sl.getList1().size());
    ASSERT_EQ(2u, sl.getList8().size());
    ASSERT_EQ(2u, sl.getList16().size());
    ASSERT_EQ(2u, sl.getList32().size());
    ASSERT_EQ(2u, sl.getList64().size());
    ASSERT_EQ(2u, sl.getListP().size());

    EXPECT_EQ(Void::VOID, sl.getList0()[0].getF());
    EXPECT_EQ(Void::VOID, sl.getList0()[1].getF());
    EXPECT_TRUE(sl.getList1()[0].getF());
    EXPECT_FALSE(sl.getList1()[1].getF());
    EXPECT_EQ(123u, sl.getList8()[0].getF());
    EXPECT_EQ(45u, sl.getList8()[1].getF());
    EXPECT_EQ(12345u, sl.getList16()[0].getF());
    EXPECT_EQ(6789u, sl.getList16()[1].getF());
    EXPECT_EQ(123456789u, sl.getList32()[0].getF());
    EXPECT_EQ(234567890u, sl.getList32()[1].getF());
    EXPECT_EQ(1234567890123456u, sl.getList64()[0].getF());
    EXPECT_EQ(2345678901234567u, sl.getList64()[1].getF());
    EXPECT_EQ("foo", sl.getListP()[0].getF());
    EXPECT_EQ("bar", sl.getListP()[1].getF());
  }

  {
    auto ll = reader.getListLists();

    {
      auto l = ll.getInt32ListList();
      ASSERT_EQ(3u, l.size());
      checkList(l[0], {1, 2, 3});
      checkList(l[1], {4, 5});
      checkList(l[2], {12341234});
    }

    {
      auto l = ll.getTextListList();
      ASSERT_EQ(3u, l.size());
      checkList(l[0], {"foo", "bar"});
      checkList(l[1], {"baz"});
      checkList(l[2], {"qux", "corge"});
    }

    {
      auto l = ll.getStructListList();
      ASSERT_EQ(2u, l.size());
      auto e = l[0];
      ASSERT_EQ(2u, e.size());
      EXPECT_EQ(123, e[0].getInt32Field());
      EXPECT_EQ(456, e[1].getInt32Field());
      e = l[1];
      ASSERT_EQ(1u, e.size());
      EXPECT_EQ(789, e[0].getInt32Field());
    }

    {
      auto l = ll.getInt32InlineListList();
      ASSERT_EQ(2u, l.size());
      checkList(l[0], {1, 2, 3, 4, 5, 6, 123456789});
      checkList(l[1], {987654321, 6, 5, 4, 3, 2, 1});
    }

    {
      auto l = ll.getTextInlineListList();
      ASSERT_EQ(3u, l.size());
      checkList(l[0], {"grault1", "grault2", "grault3", "grault4", "grault5"});
      checkList(l[1], {"garply1", "garply2", "garply3", "garply4", "garply5"});
      checkList(l[2], {"waldo1", "waldo2", "waldo3", "waldo4", "waldo5"});
    }

    {
      auto l = ll.getStructInlineListList();
      ASSERT_EQ(3u, l.size());
      ASSERT_EQ(3u, l[0].size());
      ASSERT_EQ(3u, l[1].size());
      ASSERT_EQ(3u, l[2].size());

      EXPECT_EQ(123, l[0][0].getF().getF1());
      EXPECT_EQ(456, l[0][1].getF().getF1());
      EXPECT_EQ(789, l[0][2].getF().getF1());
      EXPECT_EQ(321, l[1][0].getF().getF1());
      EXPECT_EQ(654, l[1][1].getF().getF1());
      EXPECT_EQ(987, l[1][2].getF().getF1());
      EXPECT_EQ(111, l[2][0].getF().getF1());
      EXPECT_EQ(222, l[2][1].getF().getF1());
      EXPECT_EQ(333, l[2][2].getF().getF1());

      EXPECT_EQ("fred1", l[0][0].getP0());
      EXPECT_EQ("fred2", l[0][1].getP0());
      EXPECT_EQ("fred3", l[0][2].getP0());
      EXPECT_EQ("plugh1", l[1][0].getP0());
      EXPECT_EQ("plugh2", l[1][1].getP0());
      EXPECT_EQ("plugh3", l[1][2].getP0());
      EXPECT_EQ("thud1", l[2][0].getP0());
      EXPECT_EQ("thud2", l[2][1].getP0());
      EXPECT_EQ("thud3", l[2][2].getP0());
    }

    checkList(ll.getInlineDataList(),
              {"123456789", "234567890", "345678901", "456789012", "567890123"});

    {
      auto l = ll.getInt32InlineListListList();
      ASSERT_EQ(3u, l.size());

      ASSERT_EQ(3u, l[0].size());
      ASSERT_EQ(2u, l[1].size());
      ASSERT_EQ(1u, l[2].size());

      ASSERT_EQ(2u, l[0][0].size());
      ASSERT_EQ(2u, l[0][1].size());
      ASSERT_EQ(2u, l[0][2].size());
      ASSERT_EQ(2u, l[1][0].size());
      ASSERT_EQ(2u, l[1][1].size());
      ASSERT_EQ(2u, l[2][0].size());

      checkList(l[0][0], {1, 2});
      checkList(l[0][1], {3, 4});
      checkList(l[0][2], {5, 6});
      checkList(l[1][0], {7, 8});
      checkList(l[1][1], {9, 10});
      checkList(l[2][0], {1234567, 7654321});
    }

    {
      auto l = ll.getTextInlineListListList();
      ASSERT_EQ(2u, l.size());

      ASSERT_EQ(2u, l[0].size());
      ASSERT_EQ(1u, l[1].size());

      ASSERT_EQ(5u, l[0][0].size());
      ASSERT_EQ(5u, l[0][1].size());
      ASSERT_EQ(5u, l[1][0].size());

      checkList(l[0][0], {"1", "2", "3", "4", "5"});
      checkList(l[0][1], {"foo", "bar", "baz", "qux", "corge"});
      checkList(l[1][0], {"z", "y", "x", "w", "v"});
    }

    {
      auto l = ll.getStructInlineListListList();
      ASSERT_EQ(2u, l.size());

      ASSERT_EQ(2u, l[0].size());
      ASSERT_EQ(1u, l[1].size());

      ASSERT_EQ(3u, l[0][0].size());
      ASSERT_EQ(3u, l[0][1].size());
      ASSERT_EQ(3u, l[1][0].size());

      EXPECT_EQ(123, l[0][0][0].getF().getF1());
      EXPECT_EQ(456, l[0][0][1].getF().getF1());
      EXPECT_EQ(789, l[0][0][2].getF().getF1());
      EXPECT_EQ(321, l[0][1][0].getF().getF1());
      EXPECT_EQ(654, l[0][1][1].getF().getF1());
      EXPECT_EQ(987, l[0][1][2].getF().getF1());
      EXPECT_EQ(111, l[1][0][0].getF().getF1());
      EXPECT_EQ(222, l[1][0][1].getF().getF1());
      EXPECT_EQ(333, l[1][0][2].getF().getF1());

      EXPECT_EQ("fred1", l[0][0][0].getP0());
      EXPECT_EQ("fred2", l[0][0][1].getP0());
      EXPECT_EQ("fred3", l[0][0][2].getP0());
      EXPECT_EQ("plugh1", l[0][1][0].getP0());
      EXPECT_EQ("plugh2", l[0][1][1].getP0());
      EXPECT_EQ("plugh3", l[0][1][2].getP0());
      EXPECT_EQ("thud1", l[1][0][0].getP0());
      EXPECT_EQ("thud2", l[1][0][1].getP0());
      EXPECT_EQ("thud3", l[1][0][2].getP0());
    }

    {
      auto l = ll.getInlineDataListList();
      ASSERT_EQ(2u, l.size());

      checkList(l[0], {"foo", "bar", "baz"});
      checkList(l[1], {"123", "234"});
    }
  }
}

}  // namespace

void initTestMessage(TestAllTypes::Builder builder) { genericInitTestMessage(builder); }
void initTestMessage(TestDefaults::Builder builder) { genericInitTestMessage(builder); }
void initTestMessage(TestInlineDefaults::Builder builder) { genericInitInlineDefaults(builder); }

void checkTestMessage(TestAllTypes::Builder builder) { genericCheckTestMessage(builder); }
void checkTestMessage(TestDefaults::Builder builder) { genericCheckTestMessage(builder); }
void checkTestMessage(TestInlineDefaults::Builder builder) { genericCheckInlineDefaults(builder); }

void checkTestMessage(TestAllTypes::Reader reader) { genericCheckTestMessage(reader); }
void checkTestMessage(TestDefaults::Reader reader) { genericCheckTestMessage(reader); }
void checkTestMessage(TestInlineDefaults::Reader reader) { genericCheckInlineDefaults(reader); }

void checkTestMessageAllZero(TestAllTypes::Builder builder) {
  genericCheckTestMessageAllZero(builder);
}
void checkTestMessageAllZero(TestAllTypes::Reader reader) {
  genericCheckTestMessageAllZero(reader);
}

}  // namespace internal
}  // namespace capnproto
