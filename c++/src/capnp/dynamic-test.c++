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

#include "dynamic.h"
#include "message.h"
#include <kj/debug.h>
#include <gtest/gtest.h>
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

TEST(DynamicApi, GenericObjects) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<test::TestObject>();

  initDynamicTestMessage(root.initObjectField<DynamicStruct>(Schema::from<TestAllTypes>()));
  checkTestMessage(root.asReader().getObjectField<TestAllTypes>());

  checkDynamicTestMessage(
      root.asReader().getObjectField<DynamicStruct>(Schema::from<TestAllTypes>()));
  checkDynamicTestMessage(root.getObjectField<DynamicStruct>(Schema::from<TestAllTypes>()));

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

  initDynamicTestMessage(root.initObject("objectField", Schema::from<TestAllTypes>()));
  checkTestMessage(root.asReader().as<test::TestObject>().getObjectField<TestAllTypes>());

  checkDynamicTestMessage(
      root.asReader().get("objectField").as<DynamicObject>().as(Schema::from<TestAllTypes>()));
  checkDynamicTestMessage(
      root.get("objectField").as<DynamicObject>().as(Schema::from<TestAllTypes>()));
  checkDynamicTestMessage(
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

#define EXPECT_MAYBE_EQ(name, exp, expected, actual) \
  KJ_IF_MAYBE(name, exp) { \
    EXPECT_EQ(expected, actual); \
  } else { \
    FAIL() << "Maybe was empty."; \
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
      EXPECT_MAYBE_EQ(w, u.which(), "u0f1s32", w->getProto().getName());
      EXPECT_EQ(1234567, u.get().as<int32_t>());
    }
    {
      auto u = dynamic.get("union1").as<DynamicUnion>();
      EXPECT_MAYBE_EQ(w, u.which(), "u1f1sp", w->getProto().getName());
      EXPECT_EQ("foo", u.get().as<Text>());
    }
    {
      auto u = dynamic.get("union2").as<DynamicUnion>();
      EXPECT_MAYBE_EQ(w, u.which(), "u2f0s1", w->getProto().getName());
      EXPECT_TRUE(u.get().as<bool>());
    }
    {
      auto u = dynamic.get("union3").as<DynamicUnion>();
      EXPECT_MAYBE_EQ(w, u.which(), "u3f0s64", w->getProto().getName());
      EXPECT_EQ(1234567890123456789ll, u.get().as<int64_t>());
    }
  }

  {
    // Again as a builder.
    auto dynamic = toDynamic(root);
    {
      auto u = dynamic.get("union0").as<DynamicUnion>();
      EXPECT_MAYBE_EQ(w, u.which(), "u0f1s32", w->getProto().getName());
      EXPECT_EQ(1234567, u.get().as<int32_t>());
    }
    {
      auto u = dynamic.get("union1").as<DynamicUnion>();
      EXPECT_MAYBE_EQ(w, u.which(), "u1f1sp", w->getProto().getName());
      EXPECT_EQ("foo", u.get().as<Text>());
    }
    {
      auto u = dynamic.get("union2").as<DynamicUnion>();
      EXPECT_MAYBE_EQ(w, u.which(), "u2f0s1", w->getProto().getName());
      EXPECT_TRUE(u.get().as<bool>());
    }
    {
      auto u = dynamic.get("union3").as<DynamicUnion>();
      EXPECT_MAYBE_EQ(w, u.which(), "u3f0s64", w->getProto().getName());
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

#if KJ_NO_EXCEPTIONS
#undef EXPECT_ANY_THROW
// All exceptions should be non-fatal, so when exceptions are disabled the code should return.
#define EXPECT_ANY_THROW(code) code
#endif

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

TEST(DynamicApi, LateUnion) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<test::TestLateUnion>());

  root.get("theUnion").as<DynamicUnion>().set("qux", "hello");
  EXPECT_EQ("hello", root.as<test::TestLateUnion>().getTheUnion().getQux());
}

TEST(DynamicApi, Has) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<DynamicStruct>(Schema::from<TestDefaults>());

  EXPECT_FALSE(root.has("int32Field"));
  root.set("int32Field", 123);
  EXPECT_TRUE(root.has("int32Field"));
  root.set("int32Field", -12345678);
  EXPECT_FALSE(root.has("int32Field"));

  EXPECT_FALSE(root.has("structField"));
  root.init("structField");
  EXPECT_TRUE(root.has("structField"));
}

TEST(DynamicApi, HasWhenEmpty) {
  AlignedData<1> nullRoot = {{0, 0, 0, 0, 0, 0, 0, 0}};
  kj::ArrayPtr<const word> segments[1] = {kj::arrayPtr(nullRoot.words, 1)};
  SegmentArrayMessageReader reader(kj::arrayPtr(segments, 1));
  auto root = reader.getRoot<DynamicStruct>(Schema::from<TestDefaults>());

  EXPECT_FALSE(root.has("voidField"));
  EXPECT_FALSE(root.has("int32Field"));
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

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
