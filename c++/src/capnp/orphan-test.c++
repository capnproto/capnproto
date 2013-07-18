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
#include <kj/debug.h>
#include <gtest/gtest.h>
#include "test-util.h"

namespace capnp {
namespace _ {  // private
namespace {

TEST(Orphans, Structs) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  initTestMessage(root.initStructField());
  EXPECT_TRUE(root.hasStructField());

  Orphan<TestAllTypes> orphan = root.disownStructField();
  EXPECT_FALSE(orphan == nullptr);

  checkTestMessage(orphan.getReader());
  checkTestMessage(orphan.get());
  EXPECT_FALSE(root.hasStructField());

  root.adoptStructField(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasStructField());
  checkTestMessage(root.asReader().getStructField());
}

TEST(Orphans, Lists) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  root.setUInt32List({12, 34, 56});
  EXPECT_TRUE(root.hasUInt32List());

  Orphan<List<uint32_t>> orphan = root.disownUInt32List();
  EXPECT_FALSE(orphan == nullptr);

  checkList(orphan.getReader(), {12u, 34u, 56u});
  checkList(orphan.get(), {12u, 34u, 56u});
  EXPECT_FALSE(root.hasUInt32List());

  root.adoptUInt32List(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasUInt32List());
  checkList(root.asReader().getUInt32List(), {12u, 34u, 56u});
}

TEST(Orphans, StructLists) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  auto list = root.initStructList(2);
  list[0].setTextField("foo");
  list[1].setTextField("bar");
  EXPECT_TRUE(root.hasStructList());

  Orphan<List<TestAllTypes>> orphan = root.disownStructList();
  EXPECT_FALSE(orphan == nullptr);

  ASSERT_EQ(2u, orphan.getReader().size());
  EXPECT_EQ("foo", orphan.getReader()[0].getTextField());
  EXPECT_EQ("bar", orphan.getReader()[1].getTextField());
  ASSERT_EQ(2u, orphan.get().size());
  EXPECT_EQ("foo", orphan.get()[0].getTextField());
  EXPECT_EQ("bar", orphan.get()[1].getTextField());
  EXPECT_FALSE(root.hasStructList());

  root.adoptStructList(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasStructList());
  ASSERT_EQ(2u, root.asReader().getStructList().size());
  EXPECT_EQ("foo", root.asReader().getStructList()[0].getTextField());
  EXPECT_EQ("bar", root.asReader().getStructList()[1].getTextField());
}

TEST(Orphans, Text) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  root.setTextField("foo");
  EXPECT_TRUE(root.hasTextField());

  Orphan<Text> orphan = root.disownTextField();
  EXPECT_FALSE(orphan == nullptr);

  EXPECT_EQ("foo", orphan.getReader());
  EXPECT_EQ("foo", orphan.get());
  EXPECT_FALSE(root.hasTextField());

  root.adoptTextField(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasTextField());
  EXPECT_EQ("foo", root.getTextField());
}

TEST(Orphans, Data) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  root.setDataField(data("foo"));
  EXPECT_TRUE(root.hasDataField());

  Orphan<Data> orphan = root.disownDataField();
  EXPECT_FALSE(orphan == nullptr);

  EXPECT_EQ(data("foo"), orphan.getReader());
  EXPECT_EQ(data("foo"), orphan.get());
  EXPECT_FALSE(root.hasDataField());

  root.adoptDataField(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasDataField());
  EXPECT_EQ(data("foo"), root.getDataField());
}

#if KJ_NO_EXCEPTIONS
#undef EXPECT_ANY_THROW
#define EXPECT_ANY_THROW(code) EXPECT_DEATH(code, ".")
#define EXPECT_NONFATAL_FAILURE(code) code
#else
#define EXPECT_NONFATAL_FAILURE EXPECT_ANY_THROW
#endif

TEST(Orphans, NoCrossMessageTransfers) {
  MallocMessageBuilder builder1;
  MallocMessageBuilder builder2;
  auto root1 = builder1.initRoot<TestAllTypes>();
  auto root2 = builder2.initRoot<TestAllTypes>();

  initTestMessage(root1.initStructField());

  EXPECT_ANY_THROW(root2.adoptStructField(root1.disownStructField()));
}

TEST(Orphans, OrphanageStruct) {
  MallocMessageBuilder builder;

  Orphan<TestAllTypes> orphan = builder.getOrphanage().newOrphan<TestAllTypes>();
  initTestMessage(orphan.get());
  checkTestMessage(orphan.getReader());

  auto root = builder.initRoot<TestAllTypes>();
  root.adoptStructField(kj::mv(orphan));
}

TEST(Orphans, OrphanageList) {
  MallocMessageBuilder builder;

  Orphan<List<uint32_t>> orphan = builder.getOrphanage().newOrphan<List<uint32_t>>(2);
  orphan.get().set(0, 123);
  orphan.get().set(1, 456);

  List<uint32_t>::Reader reader = orphan.getReader();
  ASSERT_EQ(2u, reader.size());
  EXPECT_EQ(123u, reader[0]);
  EXPECT_EQ(456u, reader[1]);

  auto root = builder.initRoot<TestAllTypes>();
  root.adoptUInt32List(kj::mv(orphan));
}

TEST(Orphans, OrphanageText) {
  MallocMessageBuilder builder;

  Orphan<Text> orphan = builder.getOrphanage().newOrphan<Text>(8);
  ASSERT_EQ(8u, orphan.get().size());
  memcpy(orphan.get().begin(), "12345678", 8);

  auto root = builder.initRoot<TestAllTypes>();
  root.adoptTextField(kj::mv(orphan));
  EXPECT_EQ("12345678", root.getTextField());
}

TEST(Orphans, OrphanageData) {
  MallocMessageBuilder builder;

  Orphan<Data> orphan = builder.getOrphanage().newOrphan<Data>(2);
  ASSERT_EQ(2u, orphan.get().size());
  orphan.get()[0] = 123;
  orphan.get()[1] = 45;

  auto root = builder.initRoot<TestAllTypes>();
  root.adoptDataField(kj::mv(orphan));
  ASSERT_EQ(2u, root.getDataField().size());
  EXPECT_EQ(123u, root.getDataField()[0]);
  EXPECT_EQ(45u, root.getDataField()[1]);
}

TEST(Orphans, OrphanageStructCopy) {
  MallocMessageBuilder builder1;
  MallocMessageBuilder builder2;

  auto root1 = builder1.initRoot<TestAllTypes>();
  initTestMessage(root1);

  Orphan<TestAllTypes> orphan = builder2.getOrphanage().newOrphanCopy(root1.asReader());
  checkTestMessage(orphan.getReader());

  auto root2 = builder2.initRoot<TestAllTypes>();
  root2.adoptStructField(kj::mv(orphan));
}

TEST(Orphans, OrphanageListCopy) {
  MallocMessageBuilder builder1;
  MallocMessageBuilder builder2;

  auto root1 = builder1.initRoot<TestAllTypes>();
  root1.setUInt32List({12, 34, 56});

  Orphan<List<uint32_t>> orphan = builder2.getOrphanage().newOrphanCopy(
      root1.asReader().getUInt32List());
  checkList(orphan.getReader(), {12u, 34u, 56u});

  auto root2 = builder2.initRoot<TestAllTypes>();
  root2.adoptUInt32List(kj::mv(orphan));
}

TEST(Orphans, OrphanageTextCopy) {
  MallocMessageBuilder builder;

  Orphan<Text> orphan = builder.getOrphanage().newOrphanCopy(Text::Reader("foobarba"));
  EXPECT_EQ("foobarba", orphan.getReader());

  auto root = builder.initRoot<TestAllTypes>();
  root.adoptTextField(kj::mv(orphan));
}

TEST(Orphans, OrphanageDataCopy) {
  MallocMessageBuilder builder;

  Orphan<Data> orphan = builder.getOrphanage().newOrphanCopy(data("foo"));
  EXPECT_EQ(data("foo"), orphan.getReader());

  auto root = builder.initRoot<TestAllTypes>();
  root.adoptDataField(kj::mv(orphan));
}

TEST(Orphans, ZeroOut) {
  MallocMessageBuilder builder;
  TestAllTypes::Reader orphanReader;

  {
    Orphan<TestAllTypes> orphan = builder.getOrphanage().newOrphan<TestAllTypes>();
    orphanReader = orphan.getReader();
    initTestMessage(orphan.get());
    checkTestMessage(orphan.getReader());
  }

  // Once the Orphan destructor is called, the message should be zero'd out.
  checkTestMessageAllZero(orphanReader);
}

TEST(Orphans, StructObject) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestObject>();

  initTestMessage(root.initObjectField<TestAllTypes>());
  EXPECT_TRUE(root.hasObjectField());

  Orphan<TestAllTypes> orphan = root.disownObjectField<TestAllTypes>();
  EXPECT_FALSE(orphan == nullptr);

  checkTestMessage(orphan.getReader());
  EXPECT_FALSE(root.hasObjectField());

  root.adoptObjectField(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasObjectField());
  checkTestMessage(root.asReader().getObjectField<TestAllTypes>());
}

TEST(Orphans, ListObject) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestObject>();

  root.setObjectField<List<uint32_t>>({12, 34, 56});
  EXPECT_TRUE(root.hasObjectField());

  Orphan<List<uint32_t>> orphan = root.disownObjectField<List<uint32_t>>();
  EXPECT_FALSE(orphan == nullptr);

  checkList(orphan.getReader(), {12u, 34u, 56u});
  EXPECT_FALSE(root.hasObjectField());

  root.adoptObjectField(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasObjectField());
  checkList(root.asReader().getObjectField<List<uint32_t>>(), {12u, 34u, 56u});
}

TEST(Orphans, DynamicStruct) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestObject>();

  initTestMessage(root.initObjectField<TestAllTypes>());
  EXPECT_TRUE(root.hasObjectField());

  Orphan<DynamicStruct> orphan =
      root.disownObjectField<DynamicStruct>(Schema::from<TestAllTypes>());
  EXPECT_FALSE(orphan == nullptr);

  EXPECT_TRUE(orphan.get().getSchema() == Schema::from<TestAllTypes>());
  checkDynamicTestMessage(orphan.getReader());
  EXPECT_FALSE(root.hasObjectField());

  root.adoptObjectField(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasObjectField());
  checkTestMessage(root.asReader().getObjectField<TestAllTypes>());
}

TEST(Orphans, DynamicList) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestObject>();

  root.setObjectField<List<uint32_t>>({12, 34, 56});
  EXPECT_TRUE(root.hasObjectField());

  Orphan<DynamicList> orphan = root.disownObjectField<DynamicList>(Schema::from<List<uint32_t>>());
  EXPECT_FALSE(orphan == nullptr);

  checkList<uint32_t>(orphan.getReader(), {12, 34, 56});
  EXPECT_FALSE(root.hasObjectField());

  root.adoptObjectField(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasObjectField());
  checkList(root.asReader().getObjectField<List<uint32_t>>(), {12u, 34u, 56u});
}

TEST(Orphans, DynamicStructList) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestObject>();

  auto list = root.initObjectField<List<TestAllTypes>>(2);
  list[0].setTextField("foo");
  list[1].setTextField("bar");
  EXPECT_TRUE(root.hasObjectField());

  Orphan<DynamicList> orphan =
      root.disownObjectField<DynamicList>(Schema::from<List<TestAllTypes>>());
  EXPECT_FALSE(orphan == nullptr);

  ASSERT_EQ(2u, orphan.get().size());
  EXPECT_EQ("foo", orphan.get()[0].as<TestAllTypes>().getTextField());
  EXPECT_EQ("bar", orphan.get()[1].as<TestAllTypes>().getTextField());
  EXPECT_FALSE(root.hasObjectField());

  root.adoptObjectField(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasObjectField());
  ASSERT_EQ(2u, root.asReader().getObjectField<List<TestAllTypes>>().size());
  EXPECT_EQ("foo", root.asReader().getObjectField<List<TestAllTypes>>()[0].getTextField());
  EXPECT_EQ("bar", root.asReader().getObjectField<List<TestAllTypes>>()[1].getTextField());
}

TEST(Orphans, OrphanageDynamicStruct) {
  MallocMessageBuilder builder;

  Orphan<DynamicStruct> orphan = builder.getOrphanage().newOrphan(Schema::from<TestAllTypes>());
  initDynamicTestMessage(orphan.get());
  checkDynamicTestMessage(orphan.getReader());

  auto root = builder.initRoot<test::TestObject>();
  root.adoptObjectField(kj::mv(orphan));
  checkTestMessage(root.asReader().getObjectField<TestAllTypes>());
}

TEST(Orphans, OrphanageDynamicList) {
  MallocMessageBuilder builder;

  Orphan<DynamicList> orphan = builder.getOrphanage().newOrphan(Schema::from<List<uint32_t>>(), 2);
  orphan.get().set(0, 123);
  orphan.get().set(1, 456);

  checkList<uint32_t>(orphan.getReader(), {123, 456});

  auto root = builder.initRoot<test::TestObject>();
  root.adoptObjectField(kj::mv(orphan));
  checkList(root.getObjectField<List<uint32_t>>(), {123u, 456u});
}

TEST(Orphans, OrphanageDynamicStructCopy) {
  MallocMessageBuilder builder1;
  MallocMessageBuilder builder2;

  auto root1 = builder1.initRoot<test::TestObject>();
  initTestMessage(root1.initObjectField<TestAllTypes>());

  Orphan<DynamicStruct> orphan = builder2.getOrphanage().newOrphanCopy(
      root1.asReader().getObjectField<DynamicStruct>(Schema::from<TestAllTypes>()));
  checkDynamicTestMessage(orphan.getReader());

  auto root2 = builder2.initRoot<test::TestObject>();
  root2.adoptObjectField(kj::mv(orphan));
  checkTestMessage(root2.asReader().getObjectField<TestAllTypes>());
}

TEST(Orphans, OrphanageDynamicListCopy) {
  MallocMessageBuilder builder1;
  MallocMessageBuilder builder2;

  auto root1 = builder1.initRoot<test::TestObject>();
  root1.setObjectField<List<uint32_t>>({12, 34, 56});

  Orphan<DynamicList> orphan = builder2.getOrphanage().newOrphanCopy(
      root1.asReader().getObjectField<DynamicList>(Schema::from<List<uint32_t>>()));
  checkList<uint32_t>(orphan.getReader(), {12, 34, 56});

  auto root2 = builder2.initRoot<test::TestObject>();
  root2.adoptObjectField(kj::mv(orphan));
  checkList(root2.getObjectField<List<uint32_t>>(), {12u, 34u, 56u});
}

TEST(Orphans, OrphanageFromBuilder) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  {
    Orphanage orphanage = Orphanage::getForMessageContaining(root);
    Orphan<TestAllTypes> orphan = orphanage.newOrphan<TestAllTypes>();
    initTestMessage(orphan.get());
    root.adoptStructField(kj::mv(orphan));
    checkTestMessage(root.asReader().getStructField());
  }

  {
    Orphanage orphanage = Orphanage::getForMessageContaining(root.initBoolList(3));
    Orphan<TestAllTypes> orphan = orphanage.newOrphan<TestAllTypes>();
    initTestMessage(orphan.get());
    root.adoptStructField(kj::mv(orphan));
    checkTestMessage(root.asReader().getStructField());
  }

  {
    Orphanage orphanage = Orphanage::getForMessageContaining(toDynamic(root));
    Orphan<TestAllTypes> orphan = orphanage.newOrphan<TestAllTypes>();
    initTestMessage(orphan.get());
    root.adoptStructField(kj::mv(orphan));
    checkTestMessage(root.asReader().getStructField());
  }

  {
    Orphanage orphanage = Orphanage::getForMessageContaining(toDynamic(root.initBoolList(3)));
    Orphan<TestAllTypes> orphan = orphanage.newOrphan<TestAllTypes>();
    initTestMessage(orphan.get());
    root.adoptStructField(kj::mv(orphan));
    checkTestMessage(root.asReader().getStructField());
  }
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
