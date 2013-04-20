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

}  // namespace

void initTestMessage(TestAllTypes::Builder builder) { genericInitTestMessage(builder); }
void initTestMessage(TestDefaults::Builder builder) { genericInitTestMessage(builder); }

void checkTestMessage(TestAllTypes::Builder builder) { genericCheckTestMessage(builder); }
void checkTestMessage(TestDefaults::Builder builder) { genericCheckTestMessage(builder); }

void checkTestMessage(TestAllTypes::Reader reader) { genericCheckTestMessage(reader); }
void checkTestMessage(TestDefaults::Reader reader) { genericCheckTestMessage(reader); }

void checkTestMessageAllZero(TestAllTypes::Builder builder) {
  genericCheckTestMessageAllZero(builder);
}
void checkTestMessageAllZero(TestAllTypes::Reader reader) {
  genericCheckTestMessageAllZero(reader);
}

}  // namespace internal
}  // namespace capnproto
