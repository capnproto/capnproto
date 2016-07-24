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

#include "dynamic.h"
#include "message.h"
#include <kj/debug.h>
#include <kj/compat/gtest.h>
#include "test-util.h"

namespace capnp {
namespace _ {  // private
namespace {

template <typename Element, typename T>
void checkList(T reader, std::initializer_list<ReaderFor<Element>> expected) {
  auto list = reader.template as<DynamicList>();
  ASSERT_EQ(expected.size(), list.size());
  for (uint i = 0; i < expected.size(); i++) {
    EXPECT_EQ(expected.begin()[i], list[i].template as<Element>());
  }

  auto typed = reader.template as<List<Element>>();
  ASSERT_EQ(expected.size(), typed.size());
  for (uint i = 0; i < expected.size(); i++) {
    EXPECT_EQ(expected.begin()[i], typed[i]);
  }
}

TEST(DynamicApi, Build) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestAllTypes>());

  initDynamicTestMessage(root);
  checkTestMessage(root.asReader().as<TestAllTypes>());

  checkDynamicTestMessage(root.asReader());
  checkDynamicTestMessage(root);
}

TEST(DynamicApi, Read) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  initTestMessage(root);

  checkDynamicTestMessage(toDynamic(root.asReader()));
  checkDynamicTestMessage(toDynamic(root).asReader());
  checkDynamicTestMessage(toDynamic(root));
}

TEST(DynamicApi, Defaults) {
  AlignedData<1> nullRoot = {{0, 0, 0, 0, 0, 0, 0, 0}};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(nullRoot.words, 1)};
  SegmentArrayMessageReader reader(kj::arrayPtr(segments, 1));
  auto root = reader.getRoot<DynamicStruct>(Schema::from<TestDefaults>());
  checkDynamicTestMessage(root);
}

TEST(DynamicApi, DefaultsBuilder) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestDefaults>());

  checkTestMessage(root.asReader().as<TestDefaults>());
  checkDynamicTestMessage(root.asReader());

  // This will initialize the whole message, replacing null pointers with copies of defaults.
  checkDynamicTestMessage(root);

  // Check again now that the message is initialized.
  checkTestMessage(root.asReader().as<TestDefaults>());
  checkDynamicTestMessage(root.asReader());
  checkDynamicTestMessage(root);
}

TEST(DynamicApi, Zero) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestAllTypes>());

  checkDynamicTestMessageAllZero(root.asReader());
  checkTestMessageAllZero(root.asReader().as<TestAllTypes>());
  checkDynamicTestMessageAllZero(root);
  checkTestMessageAllZero(root.asReader().as<TestAllTypes>());
}

TEST(DynamicApi, ListListsBuild) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestListDefaults>());

  initDynamicTestLists(root);
  checkTestMessage(root.asReader().as<TestListDefaults>());

  checkDynamicTestLists(root.asReader());
  checkDynamicTestLists(root);
}

TEST(DynamicApi, ListListsRead) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestListDefaults>();

  initTestMessage(root);

  checkDynamicTestLists(toDynamic(root.asReader()));
  checkDynamicTestLists(toDynamic(root).asReader());
  checkDynamicTestLists(toDynamic(root));
}

TEST(DynamicApi, AnyPointers) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<test::TestAnyPointer>();

  initDynamicTestMessage(
      root.getAnyPointerField().initAs<DynamicStruct>(Schema::from<TestAllTypes>()));
  checkTestMessage(root.asReader().getAnyPointerField().getAs<TestAllTypes>());

  checkDynamicTestMessage(
      root.asReader().getAnyPointerField().getAs<DynamicStruct>(Schema::from<TestAllTypes>()));
  checkDynamicTestMessage(
      root.getAnyPointerField().getAs<DynamicStruct>(Schema::from<TestAllTypes>()));

  {
    {
      auto list = root.getAnyPointerField().initAs<DynamicList>(Schema::from<List<uint32_t>>(), 4);
      list.set(0, 123);
      list.set(1, 456);
      list.set(2, 789);
      list.set(3, 123456789);
    }

    {
      auto list = root.asReader().getAnyPointerField().getAs<List<uint32_t>>();
      ASSERT_EQ(4u, list.size());
      EXPECT_EQ(123u, list[0]);
      EXPECT_EQ(456u, list[1]);
      EXPECT_EQ(789u, list[2]);
      EXPECT_EQ(123456789u, list[3]);
    }

    checkList<uint32_t>(root.asReader().getAnyPointerField().getAs<DynamicList>(
        Schema::from<List<uint32_t>>()), {123u, 456u, 789u, 123456789u});
    checkList<uint32_t>(root.getAnyPointerField().getAs<DynamicList>(
        Schema::from<List<uint32_t>>()), {123u, 456u, 789u, 123456789u});
  }

  // Setting an AnyPointer to various types should work.
  toDynamic(root).set("anyPointerField", capnp::Text::Reader("foo"));
  EXPECT_EQ("foo", root.getAnyPointerField().getAs<Text>());

  {
    auto orphan = builder.getOrphanage().newOrphan<TestAllTypes>();
    initTestMessage(orphan.get());
    toDynamic(root).set("anyPointerField", orphan.getReader());
    checkTestMessage(root.getAnyPointerField().getAs<TestAllTypes>());

    toDynamic(root).adopt("anyPointerField", kj::mv(orphan));
    checkTestMessage(root.getAnyPointerField().getAs<TestAllTypes>());
  }

  {
    auto lorphan = builder.getOrphanage().newOrphan<List<uint32_t>>(3);
    lorphan.get().set(0, 12);
    lorphan.get().set(1, 34);
    lorphan.get().set(2, 56);
    toDynamic(root).set("anyPointerField", lorphan.getReader());
    auto l = root.getAnyPointerField().getAs<List<uint32_t>>();
    ASSERT_EQ(3, l.size());
    EXPECT_EQ(12, l[0]);
    EXPECT_EQ(34, l[1]);
    EXPECT_EQ(56, l[2]);
  }

  // Just compile this one.
  toDynamic(root).set("anyPointerField", Capability::Client(nullptr));
  root.getAnyPointerField().getAs<Capability>();
}

TEST(DynamicApi, DynamicAnyPointers) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<DynamicStruct>(Schema::from<test::TestAnyPointer>());

  initDynamicTestMessage(
      root.get("anyPointerField").as<AnyPointer>()
          .initAs<DynamicStruct>(Schema::from<TestAllTypes>()));
  checkTestMessage(
      root.asReader().as<test::TestAnyPointer>().getAnyPointerField().getAs<TestAllTypes>());

  checkDynamicTestMessage(
      root.asReader().get("anyPointerField").as<AnyPointer>()
          .getAs<DynamicStruct>(Schema::from<TestAllTypes>()));
  checkDynamicTestMessage(
      root.asReader().get("anyPointerField").as<AnyPointer>()
          .getAs<DynamicStruct>(Schema::from<TestAllTypes>()));
  checkDynamicTestMessage(
      root.get("anyPointerField").as<AnyPointer>().asReader()
          .getAs<DynamicStruct>(Schema::from<TestAllTypes>()));
  checkDynamicTestMessage(
      root.get("anyPointerField").as<AnyPointer>()
          .getAs<DynamicStruct>(Schema::from<TestAllTypes>()));

  {
    {
      auto list = root.init("anyPointerField").as<AnyPointer>()
                      .initAs<DynamicList>(Schema::from<List<uint32_t>>(), 4);
      list.set(0, 123);
      list.set(1, 456);
      list.set(2, 789);
      list.set(3, 123456789);
    }

    {
      auto list = root.asReader().as<test::TestAnyPointer>()
          .getAnyPointerField().getAs<List<uint32_t>>();
      ASSERT_EQ(4u, list.size());
      EXPECT_EQ(123u, list[0]);
      EXPECT_EQ(456u, list[1]);
      EXPECT_EQ(789u, list[2]);
      EXPECT_EQ(123456789u, list[3]);
    }

    checkList<uint32_t>(
        root.asReader().get("anyPointerField").as<AnyPointer>()
            .getAs<DynamicList>(Schema::from<List<uint32_t>>()),
        {123u, 456u, 789u, 123456789u});
    checkList<uint32_t>(
        root.asReader().get("anyPointerField").as<AnyPointer>()
            .getAs<DynamicList>(Schema::from<List<uint32_t>>()),
        {123u, 456u, 789u, 123456789u});
    checkList<uint32_t>(
        root.get("anyPointerField").as<AnyPointer>().asReader()
            .getAs<DynamicList>(Schema::from<List<uint32_t>>()),
        {123u, 456u, 789u, 123456789u});
    checkList<uint32_t>(
        root.get("anyPointerField").as<AnyPointer>()
            .getAs<DynamicList>(Schema::from<List<uint32_t>>()),
        {123u, 456u, 789u, 123456789u});
  }
}

TEST(DynamicApi, DynamicAnyStructs) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestAllTypes>());

  root.as<AnyStruct>().as<TestAllTypes>().setInt8Field(123);
  EXPECT_EQ(root.get("int8Field").as<int8_t>(), 123);
  EXPECT_EQ(root.asReader().as<AnyStruct>().as<TestAllTypes>().getInt8Field(), 123);
}

#define EXPECT_MAYBE_EQ(name, exp, expected, actual) \
  KJ_IF_MAYBE(name, exp) { \
    EXPECT_EQ(expected, actual); \
  } else { \
    KJ_FAIL_EXPECT("Maybe was empty."); \
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
      auto u = dynamic.get("union0").as<DynamicStruct>();
      EXPECT_MAYBE_EQ(w, u.which(), "u0f1s32", w->getProto().getName());
      EXPECT_EQ(1234567, u.get("u0f1s32").as<int32_t>());
    }
    {
      auto u = dynamic.get("union1").as<DynamicStruct>();
      EXPECT_MAYBE_EQ(w, u.which(), "u1f1sp", w->getProto().getName());
      EXPECT_EQ("foo", u.get("u1f1sp").as<Text>());
    }
    {
      auto u = dynamic.get("union2").as<DynamicStruct>();
      EXPECT_MAYBE_EQ(w, u.which(), "u2f0s1", w->getProto().getName());
      EXPECT_TRUE(u.get("u2f0s1").as<bool>());
    }
    {
      auto u = dynamic.get("union3").as<DynamicStruct>();
      EXPECT_MAYBE_EQ(w, u.which(), "u3f0s64", w->getProto().getName());
      EXPECT_EQ(1234567890123456789ll, u.get("u3f0s64").as<int64_t>());
    }
  }

  {
    // Again as a builder.
    auto dynamic = toDynamic(root);
    {
      auto u = dynamic.get("union0").as<DynamicStruct>();
      EXPECT_MAYBE_EQ(w, u.which(), "u0f1s32", w->getProto().getName());
      EXPECT_EQ(1234567, u.get("u0f1s32").as<int32_t>());
    }
    {
      auto u = dynamic.get("union1").as<DynamicStruct>();
      EXPECT_MAYBE_EQ(w, u.which(), "u1f1sp", w->getProto().getName());
      EXPECT_EQ("foo", u.get("u1f1sp").as<Text>());
    }
    {
      auto u = dynamic.get("union2").as<DynamicStruct>();
      EXPECT_MAYBE_EQ(w, u.which(), "u2f0s1", w->getProto().getName());
      EXPECT_TRUE(u.get("u2f0s1").as<bool>());
    }
    {
      auto u = dynamic.get("union3").as<DynamicStruct>();
      EXPECT_MAYBE_EQ(w, u.which(), "u3f0s64", w->getProto().getName());
      EXPECT_EQ(1234567890123456789ll, u.get("u3f0s64").as<int64_t>());
    }
  }
}

TEST(DynamicApi, UnionsWrite) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestUnion>());

  root.get("union0").as<DynamicStruct>().set("u0f1s32", 1234567);
  root.get("union1").as<DynamicStruct>().set("u1f1sp", "foo");
  root.get("union2").as<DynamicStruct>().set("u2f0s1", true);
  root.get("union3").as<DynamicStruct>().set("u3f0s64", 1234567890123456789ll);

  auto reader = root.asReader().as<TestUnion>();
  ASSERT_EQ(TestUnion::Union0::U0F1S32, reader.getUnion0().which());
  EXPECT_EQ(1234567, reader.getUnion0().getU0f1s32());

  ASSERT_EQ(TestUnion::Union1::U1F1SP, reader.getUnion1().which());
  EXPECT_EQ("foo", reader.getUnion1().getU1f1sp());

  ASSERT_EQ(TestUnion::Union2::U2F0S1, reader.getUnion2().which());
  EXPECT_TRUE(reader.getUnion2().getU2f0s1());

  ASSERT_EQ(TestUnion::Union3::U3F0S64, reader.getUnion3().which());
  EXPECT_EQ(1234567890123456789ll, reader.getUnion3().getU3f0s64());

  // Can't access union members by name from the root.
  EXPECT_ANY_THROW(root.get("u0f1s32"));
  EXPECT_ANY_THROW(root.set("u0f1s32", 1234567));
}

TEST(DynamicApi, UnnamedUnion) {
  MallocMessageBuilder builder;
  StructSchema schema = Schema::from<test::TestUnnamedUnion>();
  auto root = builder.initRoot<DynamicStruct>(schema);

  EXPECT_EQ(schema.getFieldByName("foo"), KJ_ASSERT_NONNULL(root.which()));

  root.set("bar", 321);
  EXPECT_EQ(schema.getFieldByName("bar"), KJ_ASSERT_NONNULL(root.which()));
  EXPECT_EQ(321u, root.get("bar").as<uint>());
  EXPECT_EQ(321u, root.asReader().get("bar").as<uint>());
  EXPECT_ANY_THROW(root.get("foo"));
  EXPECT_ANY_THROW(root.asReader().get("foo"));

  root.set("foo", 123);
  EXPECT_EQ(schema.getFieldByName("foo"), KJ_ASSERT_NONNULL(root.which()));
  EXPECT_EQ(123u, root.get("foo").as<uint>());
  EXPECT_EQ(123u, root.asReader().get("foo").as<uint>());
  EXPECT_ANY_THROW(root.get("bar"));
  EXPECT_ANY_THROW(root.asReader().get("bar"));

  root.set("bar", 321);
  EXPECT_EQ(schema.getFieldByName("bar"), KJ_ASSERT_NONNULL(root.which()));
  EXPECT_EQ(321u, root.get("bar").as<uint>());
  EXPECT_EQ(321u, root.asReader().get("bar").as<uint>());
  EXPECT_ANY_THROW(root.get("foo"));
  EXPECT_ANY_THROW(root.asReader().get("foo"));

  root.set("foo", 123);
  EXPECT_EQ(schema.getFieldByName("foo"), KJ_ASSERT_NONNULL(root.which()));
  EXPECT_EQ(123u, root.get("foo").as<uint>());
  EXPECT_EQ(123u, root.asReader().get("foo").as<uint>());
  EXPECT_ANY_THROW(root.get("bar"));
  EXPECT_ANY_THROW(root.asReader().get("bar"));
}

TEST(DynamicApi, ConversionFailures) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestAllTypes>());

  root.set("int8Field", 123);
  EXPECT_NONFATAL_FAILURE(root.set("int8Field", 1234));

  root.set("uInt32Field", 1);
  EXPECT_NONFATAL_FAILURE(root.set("uInt32Field", -1));

  root.set("int16Field", 5);
  EXPECT_NONFATAL_FAILURE(root.set("int16Field", 0.5));

  root.set("boolField", true);
  EXPECT_NONFATAL_FAILURE(root.set("boolField", 1));
}

TEST(DynamicApi, LateUnion) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<test::TestLateUnion>());

  root.get("theUnion").as<DynamicStruct>().set("qux", "hello");
  EXPECT_EQ("hello", root.as<test::TestLateUnion>().getTheUnion().getQux());
}

TEST(DynamicApi, Has) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestDefaults>());

  // Primitive fields are always present even if set to default.
  EXPECT_TRUE(root.has("int32Field"));
  root.set("int32Field", 123);
  EXPECT_TRUE(root.has("int32Field"));
  root.set("int32Field", -12345678);
  EXPECT_TRUE(root.has("int32Field"));

  // Pointers are absent until initialized.
  EXPECT_FALSE(root.has("structField"));
  root.init("structField");
  EXPECT_TRUE(root.has("structField"));
}

TEST(DynamicApi, HasWhenEmpty) {
  AlignedData<1> nullRoot = {{0, 0, 0, 0, 0, 0, 0, 0}};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(nullRoot.words, 1)};
  SegmentArrayMessageReader reader(kj::arrayPtr(segments, 1));
  auto root = reader.getRoot<DynamicStruct>(Schema::from<TestDefaults>());

  EXPECT_TRUE(root.has("voidField"));
  EXPECT_TRUE(root.has("int32Field"));
  EXPECT_FALSE(root.has("structField"));
  EXPECT_FALSE(root.has("int32List"));
}

TEST(DynamicApi, SetEnumFromNative) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestAllTypes>());

  root.set("enumField", TestEnum::BAZ);
  root.set("enumList", {TestEnum::BAR, TestEnum::FOO});
  EXPECT_EQ(TestEnum::BAZ, root.get("enumField").as<TestEnum>());
  checkList<TestEnum>(root.get("enumList"), {TestEnum::BAR, TestEnum::FOO});
}

TEST(DynamicApi, SetDataFromText) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestAllTypes>());

  root.set("dataField", "foo");
  EXPECT_EQ(data("foo"), root.get("dataField").as<Data>());
}

TEST(DynamicApi, BuilderAssign) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestAllTypes>());

  // Declare upfront, assign later.
  // Note that the Python implementation requires defaulted constructors.  Do not delete them!
  DynamicValue::Builder value;
  DynamicStruct::Builder structValue;
  DynamicList::Builder listValue;

  value = root.get("structField");
  structValue = value.as<DynamicStruct>();
  structValue.set("int32Field", 123);

  value = root.init("int32List", 1);
  listValue = value.as<DynamicList>();
  listValue.set(0, 123);
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
