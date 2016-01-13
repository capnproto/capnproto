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
#include <kj/debug.h>
#include <kj/compat/gtest.h>
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

TEST(Orphans, EmptyStruct) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();
  auto anyPointer = root.getAnyPointerField();
  EXPECT_TRUE(anyPointer.isNull());
  auto orphan = builder.getOrphanage().newOrphan<test::TestEmptyStruct>();
  anyPointer.adopt(kj::mv(orphan));
  EXPECT_EQ(0, anyPointer.targetSize().wordCount);
  EXPECT_FALSE(anyPointer.isNull());
}

TEST(Orphans, EmptyStructOverwrite) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();
  auto anyPointer = root.getAnyPointerField();
  EXPECT_TRUE(anyPointer.isNull());
  anyPointer.initAs<TestAllTypes>();
  auto orphan = builder.getOrphanage().newOrphan<test::TestEmptyStruct>();
  anyPointer.adopt(kj::mv(orphan));
  EXPECT_EQ(0, anyPointer.targetSize().wordCount);
  EXPECT_FALSE(anyPointer.isNull());
}

TEST(Orphans, AdoptNullStruct) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();
  auto anyPointer = root.getAnyPointerField();
  EXPECT_TRUE(anyPointer.isNull());
  anyPointer.initAs<TestAllTypes>();
  anyPointer.adopt(Orphan<test::TestEmptyStruct>());
  EXPECT_EQ(0, anyPointer.targetSize().wordCount);
  EXPECT_TRUE(anyPointer.isNull());
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

TEST(Orphans, StructAnyPointer) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  initTestMessage(root.getAnyPointerField().initAs<TestAllTypes>());
  EXPECT_TRUE(root.hasAnyPointerField());

  Orphan<TestAllTypes> orphan = root.getAnyPointerField().disownAs<TestAllTypes>();
  EXPECT_FALSE(orphan == nullptr);

  checkTestMessage(orphan.getReader());
  EXPECT_FALSE(root.hasAnyPointerField());

  root.getAnyPointerField().adopt(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasAnyPointerField());
  checkTestMessage(root.asReader().getAnyPointerField().getAs<TestAllTypes>());
}

TEST(Orphans, ListAnyPointer) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  root.getAnyPointerField().setAs<List<uint32_t>>({12, 34, 56});
  EXPECT_TRUE(root.hasAnyPointerField());

  Orphan<List<uint32_t>> orphan = root.getAnyPointerField().disownAs<List<uint32_t>>();
  EXPECT_FALSE(orphan == nullptr);

  checkList(orphan.getReader(), {12u, 34u, 56u});
  EXPECT_FALSE(root.hasAnyPointerField());

  root.getAnyPointerField().adopt(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasAnyPointerField());
  checkList(root.asReader().getAnyPointerField().getAs<List<uint32_t>>(), {12u, 34u, 56u});
}

#if !CAPNP_LITE
TEST(Orphans, DynamicStruct) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  initTestMessage(root.getAnyPointerField().initAs<TestAllTypes>());
  EXPECT_TRUE(root.hasAnyPointerField());

  Orphan<DynamicStruct> orphan =
      root.getAnyPointerField().disownAs<DynamicStruct>(Schema::from<TestAllTypes>());
  EXPECT_FALSE(orphan == nullptr);

  EXPECT_TRUE(orphan.get().getSchema() == Schema::from<TestAllTypes>());
  checkDynamicTestMessage(orphan.getReader());
  EXPECT_FALSE(root.hasAnyPointerField());

  root.getAnyPointerField().adopt(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasAnyPointerField());
  checkTestMessage(root.asReader().getAnyPointerField().getAs<TestAllTypes>());

  Orphan<DynamicStruct> orphan2 = root.getAnyPointerField().disownAs<TestAllTypes>();
  EXPECT_FALSE(orphan2 == nullptr);
  EXPECT_TRUE(orphan2.get().getSchema() == Schema::from<TestAllTypes>());
  checkDynamicTestMessage(orphan2.getReader());
}

TEST(Orphans, DynamicList) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  root.getAnyPointerField().setAs<List<uint32_t>>({12, 34, 56});
  EXPECT_TRUE(root.hasAnyPointerField());

  Orphan<DynamicList> orphan =
      root.getAnyPointerField().disownAs<DynamicList>(Schema::from<List<uint32_t>>());
  EXPECT_FALSE(orphan == nullptr);

  checkList<uint32_t>(orphan.getReader(), {12, 34, 56});
  EXPECT_FALSE(root.hasAnyPointerField());

  root.getAnyPointerField().adopt(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasAnyPointerField());
  checkList(root.asReader().getAnyPointerField().getAs<List<uint32_t>>(), {12u, 34u, 56u});

  Orphan<DynamicList> orphan2 = root.getAnyPointerField().disownAs<List<uint32_t>>();
  EXPECT_FALSE(orphan2 == nullptr);
  checkList<uint32_t>(orphan2.getReader(), {12, 34, 56});
}

TEST(Orphans, DynamicStructList) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  auto list = root.getAnyPointerField().initAs<List<TestAllTypes>>(2);
  list[0].setTextField("foo");
  list[1].setTextField("bar");
  EXPECT_TRUE(root.hasAnyPointerField());

  Orphan<DynamicList> orphan =
      root.getAnyPointerField().disownAs<DynamicList>(Schema::from<List<TestAllTypes>>());
  EXPECT_FALSE(orphan == nullptr);

  ASSERT_EQ(2u, orphan.get().size());
  EXPECT_EQ("foo", orphan.get()[0].as<TestAllTypes>().getTextField());
  EXPECT_EQ("bar", orphan.get()[1].as<TestAllTypes>().getTextField());
  EXPECT_FALSE(root.hasAnyPointerField());

  root.getAnyPointerField().adopt(kj::mv(orphan));
  EXPECT_TRUE(orphan == nullptr);
  EXPECT_TRUE(root.hasAnyPointerField());
  ASSERT_EQ(2u, root.asReader().getAnyPointerField().getAs<List<TestAllTypes>>().size());
  EXPECT_EQ("foo", root.asReader().getAnyPointerField()
                       .getAs<List<TestAllTypes>>()[0].getTextField());
  EXPECT_EQ("bar", root.asReader().getAnyPointerField()
                       .getAs<List<TestAllTypes>>()[1].getTextField());
}

TEST(Orphans, OrphanageDynamicStruct) {
  MallocMessageBuilder builder;

  Orphan<DynamicStruct> orphan = builder.getOrphanage().newOrphan(Schema::from<TestAllTypes>());
  initDynamicTestMessage(orphan.get());
  checkDynamicTestMessage(orphan.getReader());

  auto root = builder.initRoot<test::TestAnyPointer>();
  root.getAnyPointerField().adopt(kj::mv(orphan));
  checkTestMessage(root.asReader().getAnyPointerField().getAs<TestAllTypes>());
}

TEST(Orphans, OrphanageDynamicList) {
  MallocMessageBuilder builder;

  Orphan<DynamicList> orphan = builder.getOrphanage().newOrphan(Schema::from<List<uint32_t>>(), 2);
  orphan.get().set(0, 123);
  orphan.get().set(1, 456);

  checkList<uint32_t>(orphan.getReader(), {123, 456});

  auto root = builder.initRoot<test::TestAnyPointer>();
  root.getAnyPointerField().adopt(kj::mv(orphan));
  checkList(root.getAnyPointerField().getAs<List<uint32_t>>(), {123u, 456u});
}

TEST(Orphans, OrphanageDynamicStructCopy) {
  MallocMessageBuilder builder1;
  MallocMessageBuilder builder2;

  auto root1 = builder1.initRoot<test::TestAnyPointer>();
  initTestMessage(root1.getAnyPointerField().initAs<TestAllTypes>());

  Orphan<DynamicStruct> orphan = builder2.getOrphanage().newOrphanCopy(
      root1.asReader().getAnyPointerField().getAs<DynamicStruct>(Schema::from<TestAllTypes>()));
  checkDynamicTestMessage(orphan.getReader());

  auto root2 = builder2.initRoot<test::TestAnyPointer>();
  root2.getAnyPointerField().adopt(kj::mv(orphan));
  checkTestMessage(root2.asReader().getAnyPointerField().getAs<TestAllTypes>());
}

TEST(Orphans, OrphanageDynamicListCopy) {
  MallocMessageBuilder builder1;
  MallocMessageBuilder builder2;

  auto root1 = builder1.initRoot<test::TestAnyPointer>();
  root1.getAnyPointerField().setAs<List<uint32_t>>({12, 34, 56});

  Orphan<DynamicList> orphan = builder2.getOrphanage().newOrphanCopy(
      root1.asReader().getAnyPointerField().getAs<DynamicList>(Schema::from<List<uint32_t>>()));
  checkList<uint32_t>(orphan.getReader(), {12, 34, 56});

  auto root2 = builder2.initRoot<test::TestAnyPointer>();
  root2.getAnyPointerField().adopt(kj::mv(orphan));
  checkList(root2.getAnyPointerField().getAs<List<uint32_t>>(), {12u, 34u, 56u});
}

TEST(Orphans, DynamicStructAs) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  initTestMessage(root.getAnyPointerField().initAs<TestAllTypes>());
  EXPECT_TRUE(root.hasAnyPointerField());

  Orphan<DynamicValue> orphan =
      root.getAnyPointerField().disownAs<DynamicStruct>(Schema::from<TestAllTypes>());
  EXPECT_EQ(DynamicValue::STRUCT, orphan.getType());

  checkTestMessage(orphan.getReader().as<TestAllTypes>());
  checkTestMessage(orphan.get().as<TestAllTypes>());

  {
    Orphan<DynamicStruct> structOrphan = orphan.releaseAs<DynamicStruct>();
    EXPECT_EQ(DynamicValue::UNKNOWN, orphan.getType());
    EXPECT_FALSE(structOrphan == nullptr);
    checkDynamicTestMessage(structOrphan.getReader());
    checkDynamicTestMessage(structOrphan.get());
    checkTestMessage(structOrphan.getReader().as<TestAllTypes>());
    checkTestMessage(structOrphan.get().as<TestAllTypes>());

    {
      Orphan<TestAllTypes> typedOrphan = structOrphan.releaseAs<TestAllTypes>();
      EXPECT_TRUE(structOrphan == nullptr);
      EXPECT_FALSE(typedOrphan == nullptr);
      checkTestMessage(typedOrphan.getReader());
      checkTestMessage(typedOrphan.get());
      orphan = kj::mv(typedOrphan);
      EXPECT_EQ(DynamicValue::STRUCT, orphan.getType());
      EXPECT_TRUE(typedOrphan == nullptr);
    }
  }

  {
    Orphan<TestAllTypes> typedOrphan = orphan.releaseAs<TestAllTypes>();
    checkTestMessage(typedOrphan.getReader());
    checkTestMessage(typedOrphan.get());
  }
}

TEST(Orphans, DynamicListAs) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  root.getAnyPointerField().setAs<List<uint32_t>>({12, 34, 56});
  EXPECT_TRUE(root.hasAnyPointerField());

  Orphan<DynamicValue> orphan =
      root.getAnyPointerField().disownAs<DynamicList>(Schema::from<List<uint32_t>>());
  EXPECT_EQ(DynamicValue::LIST, orphan.getType());

  checkList(orphan.getReader().as<List<uint32_t>>(), {12, 34, 56});
  checkList(orphan.get().as<List<uint32_t>>(), {12, 34, 56});

  {
    Orphan<DynamicList> listOrphan = orphan.releaseAs<DynamicList>();
    EXPECT_EQ(DynamicValue::UNKNOWN, orphan.getType());
    EXPECT_FALSE(listOrphan == nullptr);
    checkList<uint32_t>(listOrphan.getReader(), {12, 34, 56});
    checkList<uint32_t>(listOrphan.get(), {12, 34, 56});
    checkList(listOrphan.getReader().as<List<uint32_t>>(), {12, 34, 56});
    checkList(listOrphan.get().as<List<uint32_t>>(), {12, 34, 56});

    {
      Orphan<List<uint32_t>> typedOrphan = listOrphan.releaseAs<List<uint32_t>>();
      EXPECT_TRUE(listOrphan == nullptr);
      EXPECT_FALSE(typedOrphan == nullptr);
      checkList(typedOrphan.getReader(), {12, 34, 56});
      checkList(typedOrphan.get(), {12, 34, 56});
      orphan = kj::mv(typedOrphan);
      EXPECT_EQ(DynamicValue::LIST, orphan.getType());
      EXPECT_TRUE(typedOrphan == nullptr);
    }
  }

  {
    Orphan<List<uint32_t>> typedOrphan = orphan.releaseAs<List<uint32_t>>();
    checkList(typedOrphan.getReader(), {12, 34, 56});
    checkList(typedOrphan.get(), {12, 34, 56});
  }
}

TEST(Orphans, DynamicAnyPointer) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  initTestMessage(root.getAnyPointerField().initAs<TestAllTypes>());
  EXPECT_TRUE(root.hasAnyPointerField());

  Orphan<DynamicValue> orphan = root.getAnyPointerField().disown();
  EXPECT_EQ(DynamicValue::ANY_POINTER, orphan.getType());

  Orphan<AnyPointer> objectOrphan = orphan.releaseAs<AnyPointer>();
  checkTestMessage(objectOrphan.getAs<TestAllTypes>());
  checkDynamicTestMessage(objectOrphan.getAs<DynamicStruct>(Schema::from<TestAllTypes>()));
}

TEST(Orphans, DynamicDisown) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();
  initTestMessage(root);

  Orphan<TestAllTypes> dstOrphan =
      Orphanage::getForMessageContaining(root).newOrphan<TestAllTypes>();
  auto dst = dstOrphan.get();

  DynamicStruct::Builder dynamic = root;
  DynamicStruct::Builder dynamicDst = dst;

  for (auto field: dynamic.getSchema().getFields()) {
    dynamicDst.adopt(field, dynamic.disown(field));
  }

  checkTestMessageAllZero(root.asReader());
  checkTestMessage(dst.asReader());

  for (auto field: dynamic.getSchema().getFields()) {
    dynamicDst.adopt(field, dynamic.disown(field));
  }

  checkTestMessageAllZero(root.asReader());
  checkTestMessageAllZero(dst.asReader());
}

TEST(Orphans, DynamicDisownGroup) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestGroups>();

  auto bar = root.initGroups().initBar();
  bar.setCorge(123);
  bar.setGrault("foo");
  bar.setGarply(9876543210987ll);

  Orphan<test::TestGroups> dstOrphan =
      Orphanage::getForMessageContaining(root).newOrphan<test::TestGroups>();
  auto dst = dstOrphan.get();

  toDynamic(dst).adopt("groups", toDynamic(root).disown("groups"));

  EXPECT_EQ(test::TestGroups::Groups::FOO, root.getGroups().which());

  EXPECT_EQ(test::TestGroups::Groups::BAR, dst.getGroups().which());
  auto newBar = dst.getGroups().getBar();
  EXPECT_EQ(123, newBar.getCorge());
  EXPECT_EQ("foo", newBar.getGrault());
  EXPECT_EQ(9876543210987ll, newBar.getGarply());
}
#endif  // !CAPNP_LITE

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

#if !CAPNP_LITE
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
#endif  // !CAPNP_LITE
}

static bool allZero(const word* begin, const word* end) {
  for (const byte* pos = reinterpret_cast<const byte*>(begin);
       pos < reinterpret_cast<const byte*>(end); ++pos) {
    if (*pos != 0) return false;
  }
  return true;
}

TEST(Orphans, StructsZerodAfterUse) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  const word* zerosStart = builder.getSegmentsForOutput()[0].end();
  initTestMessage(root.initStructField());
  const word* zerosEnd = builder.getSegmentsForOutput()[0].end();

  root.setTextField("foo");  // guard against overruns

  EXPECT_EQ(1u, builder.getSegmentsForOutput().size());  // otherwise test is invalid

  root.disownStructField();

  EXPECT_TRUE(allZero(zerosStart, zerosEnd));

  EXPECT_EQ("foo", root.getTextField());
}

TEST(Orphans, ListsZerodAfterUse) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  const word* zerosStart = builder.getSegmentsForOutput()[0].end();
  root.setUInt32List({12, 34, 56});
  const word* zerosEnd = builder.getSegmentsForOutput()[0].end();

  root.setTextField("foo");  // guard against overruns

  EXPECT_EQ(1u, builder.getSegmentsForOutput().size());  // otherwise test is invalid

  root.disownUInt32List();

  EXPECT_TRUE(allZero(zerosStart, zerosEnd));

  EXPECT_EQ("foo", root.getTextField());
}

TEST(Orphans, EmptyListsZerodAfterUse) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  const word* zerosStart = builder.getSegmentsForOutput()[0].end();
  root.initUInt32List(0);
  const word* zerosEnd = builder.getSegmentsForOutput()[0].end();

  root.setTextField("foo");  // guard against overruns

  EXPECT_EQ(1u, builder.getSegmentsForOutput().size());  // otherwise test is invalid

  root.disownUInt32List();

  EXPECT_TRUE(allZero(zerosStart, zerosEnd));

  EXPECT_EQ("foo", root.getTextField());
}

TEST(Orphans, StructListsZerodAfterUse) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  const word* zerosStart = builder.getSegmentsForOutput()[0].end();
  {
    auto list = root.initStructList(2);
    initTestMessage(list[0]);
    initTestMessage(list[1]);
  }
  const word* zerosEnd = builder.getSegmentsForOutput()[0].end();

  root.setTextField("foo");  // guard against overruns

  EXPECT_EQ(1u, builder.getSegmentsForOutput().size());  // otherwise test is invalid

  root.disownStructList();

  EXPECT_TRUE(allZero(zerosStart, zerosEnd));

  EXPECT_EQ("foo", root.getTextField());
}

TEST(Orphans, EmptyStructListsZerodAfterUse) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  const word* zerosStart = builder.getSegmentsForOutput()[0].end();
  root.initStructList(0);
  const word* zerosEnd = builder.getSegmentsForOutput()[0].end();

  root.setTextField("foo");  // guard against overruns

  EXPECT_EQ(1u, builder.getSegmentsForOutput().size());  // otherwise test is invalid

  root.disownStructList();

  EXPECT_TRUE(allZero(zerosStart, zerosEnd));

  EXPECT_EQ("foo", root.getTextField());
}

TEST(Orphans, TextZerodAfterUse) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  const word* zerosStart = builder.getSegmentsForOutput()[0].end();
  root.setTextField("abcd123");
  const word* zerosEnd = builder.getSegmentsForOutput()[0].end();

  root.setDataField(data("foo"));  // guard against overruns

  EXPECT_EQ(1u, builder.getSegmentsForOutput().size());  // otherwise test is invalid

  root.disownTextField();

  EXPECT_TRUE(allZero(zerosStart, zerosEnd));

  EXPECT_EQ(data("foo"), root.getDataField());
}

TEST(Orphans, DataZerodAfterUse) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  const word* zerosStart = builder.getSegmentsForOutput()[0].end();
  root.setDataField(data("abcd123"));
  const word* zerosEnd = builder.getSegmentsForOutput()[0].end();

  root.setTextField("foo");  // guard against overruns

  EXPECT_EQ(1u, builder.getSegmentsForOutput().size());  // otherwise test is invalid

  root.disownDataField();

  EXPECT_TRUE(allZero(zerosStart, zerosEnd));

  EXPECT_EQ("foo", root.getTextField());
}

TEST(Orphans, FarPointer) {
  MallocMessageBuilder builder(0, AllocationStrategy::FIXED_SIZE);
  auto root = builder.initRoot<TestAllTypes>();
  auto child = root.initStructField();
  initTestMessage(child);

  auto orphan = root.disownStructField();
  EXPECT_FALSE(root.hasStructField());
  EXPECT_TRUE(orphan != nullptr);
  EXPECT_FALSE(orphan == nullptr);

  checkTestMessage(orphan.getReader());
  checkTestMessage(orphan.get());
}

TEST(Orphans, UpgradeStruct) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  auto old = root.getAnyPointerField().initAs<test::TestOldVersion>();
  old.setOld1(1234);
  old.setOld2("foo");

  auto orphan = root.getAnyPointerField().disownAs<test::TestNewVersion>();

  // Relocation has not occurred yet.
  old.setOld1(12345);
  EXPECT_EQ(12345, orphan.getReader().getOld1());
  EXPECT_EQ("foo", old.getOld2());

  // This will relocate the struct.
  auto newVersion = orphan.get();

  EXPECT_EQ(0, old.getOld1());
  EXPECT_EQ("", old.getOld2());

  EXPECT_EQ(12345, newVersion.getOld1());
  EXPECT_EQ("foo", newVersion.getOld2());
}

TEST(Orphans, UpgradeStructList) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<test::TestAnyPointer>();

  auto old = root.getAnyPointerField().initAs<List<test::TestOldVersion>>(2);
  old[0].setOld1(1234);
  old[0].setOld2("foo");
  old[1].setOld1(4321);
  old[1].setOld2("bar");

  auto orphan = root.getAnyPointerField().disownAs<List<test::TestNewVersion>>();

  // Relocation has not occurred yet.
  old[0].setOld1(12345);
  EXPECT_EQ(12345, orphan.getReader()[0].getOld1());
  EXPECT_EQ("foo", old[0].getOld2());

  // This will relocate the struct.
  auto newVersion = orphan.get();

  EXPECT_EQ(0, old[0].getOld1());
  EXPECT_EQ("", old[0].getOld2());

  EXPECT_EQ(12345, newVersion[0].getOld1());
  EXPECT_EQ("foo", newVersion[0].getOld2());
  EXPECT_EQ(4321, newVersion[1].getOld1());
  EXPECT_EQ("bar", newVersion[1].getOld2());
}

TEST(Orphans, DisownNull) {
  MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();

  {
    Orphan<TestAllTypes> orphan = root.disownStructField();
    EXPECT_TRUE(orphan == nullptr);

    checkTestMessageAllZero(orphan.getReader());
    EXPECT_TRUE(orphan == nullptr);

    // get()ing the orphan allocates an object, for security reasons.
    checkTestMessageAllZero(orphan.get());
    EXPECT_FALSE(orphan == nullptr);
  }

  {
    Orphan<List<int32_t>> orphan = root.disownInt32List();
    EXPECT_TRUE(orphan == nullptr);

    EXPECT_EQ(0, orphan.getReader().size());
    EXPECT_TRUE(orphan == nullptr);

    EXPECT_EQ(0, orphan.get().size());
    EXPECT_TRUE(orphan == nullptr);
  }

  {
    Orphan<List<TestAllTypes>> orphan = root.disownStructList();
    EXPECT_TRUE(orphan == nullptr);

    EXPECT_EQ(0, orphan.getReader().size());
    EXPECT_TRUE(orphan == nullptr);

    EXPECT_EQ(0, orphan.get().size());
    EXPECT_TRUE(orphan == nullptr);
  }
}

TEST(Orphans, ReferenceExternalData) {
  MallocMessageBuilder builder;

  union {
    word align;
    byte data[50];
  };

  memset(data, 0x55, sizeof(data));

  auto orphan = builder.getOrphanage().referenceExternalData(Data::Builder(data, sizeof(data)));

  // Data was added as a new segment.
  {
    auto segments = builder.getSegmentsForOutput();
    ASSERT_EQ(2, segments.size());
    EXPECT_EQ(data, segments[1].asBytes().begin());
    EXPECT_EQ((sizeof(data) + 7) / 8, segments[1].size());
  }

  // Can't get builder because it's read-only.
  EXPECT_ANY_THROW(orphan.get());

  // Can get reader.
  {
    auto reader = orphan.getReader();
    EXPECT_EQ(data, reader.begin());
    EXPECT_EQ(sizeof(data), reader.size());
  }

  // Adopt into message tree.
  auto root = builder.getRoot<TestAllTypes>();
  root.adoptDataField(kj::mv(orphan));

  // Can't get child builder.
  EXPECT_ANY_THROW(root.getDataField());

  // Can get child reader.
  {
    auto reader = root.asReader().getDataField();
    EXPECT_EQ(data, reader.begin());
    EXPECT_EQ(sizeof(data), reader.size());
  }

  // Back to orphan.
  orphan = root.disownDataField();

  // Now the orphan may be pointing to a far pointer landing pad, so check that it still does the
  // right things.

  // Can't get builder because it's read-only.
  EXPECT_ANY_THROW(orphan.get());

  // Can get reader.
  {
    auto reader = orphan.getReader();
    EXPECT_EQ(data, reader.begin());
    EXPECT_EQ(sizeof(data), reader.size());
  }

  // Finally, let's abandon the orphan and check that this doesn't zero out the data.
  orphan = Orphan<Data>();

  for (byte b: data) {
    EXPECT_EQ(0x55, b);
  }
}

TEST(Orphans, ReferenceExternalData_NoZeroOnSet) {
  // Verify that an external blob is not zeroed by setFoo().

  union {
    word align;
    byte data[50];
  };

  memset(data, 0x55, sizeof(data));

  MallocMessageBuilder builder;
  auto root = builder.getRoot<TestAllTypes>();
  root.adoptDataField(builder.getOrphanage().referenceExternalData(
      Data::Builder(data, sizeof(data))));

  root.setDataField(Data::Builder());

  for (byte b: data) {
    EXPECT_EQ(0x55, b);
  }
}

TEST(Orphans, ReferenceExternalData_NoZeroImmediateAbandon) {
  // Verify that an external blob is not zeroed when abandoned immediately, without ever being
  // adopted.

  union {
    word align;
    byte data[50];
  };

  memset(data, 0x55, sizeof(data));

  MallocMessageBuilder builder;
  builder.getOrphanage().referenceExternalData(Data::Builder(data, sizeof(data)));

  for (byte b: data) {
    EXPECT_EQ(0x55, b);
  }
}

TEST(Orphans, TruncateData) {
  MallocMessageBuilder message;
  auto orphan = message.getOrphanage().newOrphan<Data>(17);
  auto builder = orphan.get();
  memset(builder.begin(), 123, builder.size());

  EXPECT_EQ(4, message.getSegmentsForOutput()[0].size());
  orphan.truncate(2);
  EXPECT_EQ(2, message.getSegmentsForOutput()[0].size());

  auto reader = orphan.getReader();
  EXPECT_EQ(2, reader.size());
  EXPECT_EQ(builder.begin(), reader.begin());

  EXPECT_EQ(123, builder[0]);
  EXPECT_EQ(123, builder[1]);
  EXPECT_EQ(0, builder[2]);
  EXPECT_EQ(0, builder[3]);
  EXPECT_EQ(0, builder[16]);
}

TEST(Orphans, ExtendData) {
  MallocMessageBuilder message;
  auto orphan = message.getOrphanage().newOrphan<Data>(17);
  auto builder = orphan.get();
  memset(builder.begin(), 123, builder.size());

  EXPECT_EQ(4, message.getSegmentsForOutput()[0].size());
  orphan.truncate(27);
  EXPECT_EQ(5, message.getSegmentsForOutput()[0].size());

  auto reader = orphan.getReader();
  EXPECT_EQ(27, reader.size());
  EXPECT_EQ(builder.begin(), reader.begin());

  for (uint i = 0; i < 17; i++) {
    EXPECT_EQ(123, reader[i]);
  }
  for (uint i = 17; i < 27; i++) {
    EXPECT_EQ(0, reader[i]);
  }
}

TEST(Orphans, ExtendDataCopy) {
  MallocMessageBuilder message;
  auto orphan = message.getOrphanage().newOrphan<Data>(17);
  auto builder = orphan.get();
  memset(builder.begin(), 123, builder.size());

  auto orphan2 = message.getOrphanage().newOrphan<Data>(1);
  orphan2.get()[0] = 32;

  orphan.truncate(27);

  auto reader = orphan.getReader();
  EXPECT_EQ(27, reader.size());
  EXPECT_NE(builder.begin(), reader.begin());

  for (uint i = 0; i < 17; i++) {
    EXPECT_EQ(123, reader[i]);
    EXPECT_EQ(0, builder[i]);
  }
  for (uint i = 17; i < 27; i++) {
    EXPECT_EQ(0, reader[i]);
  }

  EXPECT_EQ(32, orphan2.getReader()[0]);
}

TEST(Orphans, ExtendDataFromEmpty) {
  MallocMessageBuilder message;

  auto orphan = message.initRoot<TestAllTypes>().disownDataField();
  orphan.truncate(3);

  auto reader = orphan.getReader();
  EXPECT_EQ(3, reader.size());

  for (uint i = 0; i < 3; i++) {
    EXPECT_EQ(0, reader[i]);
  }
}

TEST(Orphans, TruncateText) {
  MallocMessageBuilder message;
  auto orphan = message.getOrphanage().newOrphan<Text>(17);
  auto builder = orphan.get();
  memset(builder.begin(), 'a', builder.size());

  EXPECT_EQ(4, message.getSegmentsForOutput()[0].size());
  orphan.truncate(2);
  EXPECT_EQ(2, message.getSegmentsForOutput()[0].size());

  auto reader = orphan.getReader();
  EXPECT_EQ(2, reader.size());
  EXPECT_EQ(builder.begin(), reader.begin());

  EXPECT_EQ('a', builder[0]);
  EXPECT_EQ('a', builder[1]);
  EXPECT_EQ('\0', builder[2]);
  EXPECT_EQ('\0', builder[3]);
  EXPECT_EQ('\0', builder[16]);
}

TEST(Orphans, ExtendText) {
  MallocMessageBuilder message;
  auto orphan = message.getOrphanage().newOrphan<Text>(17);
  auto builder = orphan.get();
  memset(builder.begin(), 'a', builder.size());

  EXPECT_EQ(4, message.getSegmentsForOutput()[0].size());
  orphan.truncate(27);
  EXPECT_EQ(5, message.getSegmentsForOutput()[0].size());

  auto reader = orphan.getReader();
  EXPECT_EQ(27, reader.size());
  EXPECT_EQ(builder.begin(), reader.begin());

  for (uint i = 0; i < 17; i++) {
    EXPECT_EQ('a', reader[i]);
  }
  for (uint i = 17; i < 27; i++) {
    EXPECT_EQ('\0', reader[i]);
  }
}

TEST(Orphans, ExtendTextCopy) {
  MallocMessageBuilder message;
  auto orphan = message.getOrphanage().newOrphan<Text>(17);
  auto builder = orphan.get();
  memset(builder.begin(), 'a', builder.size());

  auto orphan2 = message.getOrphanage().newOrphan<Data>(1);
  orphan2.get()[0] = 32;

  orphan.truncate(27);

  auto reader = orphan.getReader();
  EXPECT_EQ(27, reader.size());
  EXPECT_NE(builder.begin(), reader.begin());

  for (uint i = 0; i < 17; i++) {
    EXPECT_EQ('a', reader[i]);
    EXPECT_EQ('\0', builder[i]);
  }
  for (uint i = 17; i < 27; i++) {
    EXPECT_EQ('\0', reader[i]);
  }

  EXPECT_EQ(32, orphan2.getReader()[0]);
}

TEST(Orphans, ExtendTextFromEmpty) {
  MallocMessageBuilder message;

  auto orphan = message.initRoot<TestAllTypes>().disownTextField();
  orphan.truncate(3);

  auto reader = orphan.getReader();
  EXPECT_EQ(3, reader.size());

  for (uint i = 0; i < 3; i++) {
    EXPECT_EQ('\0', reader[i]);
  }
}

TEST(Orphans, TruncatePrimitiveList) {
  MallocMessageBuilder message;
  auto orphan = message.getOrphanage().newOrphan<List<uint32_t>>(7);
  auto builder = orphan.get();
  for (uint i = 0; i < 7; i++) {
    builder.set(i, 123456789 + i);
  }

  EXPECT_EQ(5, message.getSegmentsForOutput()[0].size());
  orphan.truncate(3);
  EXPECT_EQ(3, message.getSegmentsForOutput()[0].size());

  auto reader = orphan.getReader();
  EXPECT_EQ(3, reader.size());

  for (uint i = 0; i < 3; i++) {
    EXPECT_EQ(123456789 + i, builder[i]);
    EXPECT_EQ(123456789 + i, reader[i]);
  }
  for (uint i = 3; i < 7; i++) {
    EXPECT_EQ(0, builder[i]);
  }

  // Can't compare pointers directly, but we can check if builder modifications are visible using
  // the reader.
  builder.set(0, 321);
  EXPECT_EQ(321, reader[0]);
}

TEST(Orphans, ExtendPrimitiveList) {
  MallocMessageBuilder message;
  auto orphan = message.getOrphanage().newOrphan<List<uint32_t>>(7);
  auto builder = orphan.get();
  for (uint i = 0; i < 7; i++) {
    builder.set(i, 123456789 + i);
  }

  EXPECT_EQ(5, message.getSegmentsForOutput()[0].size());
  orphan.truncate(11);
  EXPECT_EQ(7, message.getSegmentsForOutput()[0].size());

  auto reader = orphan.getReader();
  EXPECT_EQ(11, reader.size());

  for (uint i = 0; i < 7; i++) {
    EXPECT_EQ(123456789 + i, reader[i]);
    EXPECT_EQ(123456789 + i, builder[i]);
  }
  for (uint i = 7; i < 11; i++) {
    EXPECT_EQ(0, reader[i]);
  }

  // Can't compare pointers directly, but we can check if builder modifications are visible using
  // the reader.
  builder.set(0, 321);
  EXPECT_EQ(321, reader[0]);
}

TEST(Orphans, ExtendPrimitiveListCopy) {
  MallocMessageBuilder message;
  auto orphan = message.getOrphanage().newOrphan<List<uint32_t>>(7);
  auto builder = orphan.get();
  for (uint i = 0; i < 7; i++) {
    builder.set(i, 123456789 + i);
  }

  auto orphan2 = message.getOrphanage().newOrphan<Data>(1);
  orphan2.get()[0] = 32;

  orphan.truncate(11);

  auto reader = orphan.getReader();
  EXPECT_EQ(11, reader.size());

  for (uint i = 0; i < 7; i++) {
    EXPECT_EQ(123456789 + i, reader[i]);
    EXPECT_EQ(0, builder[i]);
  }
  for (uint i = 7; i < 11; i++) {
    EXPECT_EQ(0, reader[i]);
  }

  // Can't compare pointers directly, but we can check if builder modifications are visible using
  // the reader.
  builder.set(0, 321);
  EXPECT_EQ(123456789, reader[0]);

  EXPECT_EQ(32, orphan2.getReader()[0]);
}

TEST(Orphans, ExtendPointerListFromEmpty) {
  MallocMessageBuilder message;

  auto orphan = message.initRoot<TestAllTypes>().disownUInt32List();
  orphan.truncate(3);

  auto reader = orphan.getReader();
  EXPECT_EQ(3, reader.size());

  for (uint i = 0; i < 3; i++) {
    EXPECT_EQ(0, reader[i]);
  }
}

TEST(Orphans, TruncatePointerList) {
  MallocMessageBuilder message;

  // Allocate data in advance so that the list itself is at the end of the segment.
  kj::Vector<Orphan<Text>> pointers(7);
  for (uint i = 0; i < 7; i++) {
    pointers.add(message.getOrphanage().newOrphanCopy(Text::Reader(kj::str("foo", i))));
  }
  size_t sizeBeforeList = message.getSegmentsForOutput()[0].size();

  auto orphan = message.getOrphanage().newOrphan<List<Text>>(7);
  auto builder = orphan.get();
  for (uint i = 0; i < 7; i++) {
    builder.adopt(i, kj::mv(pointers[i]));
  }

  EXPECT_EQ(sizeBeforeList + 7, message.getSegmentsForOutput()[0].size());
  orphan.truncate(3);
  EXPECT_EQ(sizeBeforeList + 3, message.getSegmentsForOutput()[0].size());

  auto reader = orphan.getReader();
  EXPECT_EQ(3, reader.size());

  for (uint i = 0; i < 3; i++) {
    EXPECT_EQ(kj::str("foo", i), builder[i]);
    EXPECT_EQ(kj::str("foo", i), reader[i]);
  }
  for (uint i = 3; i < 7; i++) {
    EXPECT_TRUE(builder[i] == nullptr);
  }

  // Can't compare pointers directly, but we can check if builder modifications are visible using
  // the reader.
  builder.set(0, "bar");
  EXPECT_EQ("bar", reader[0]);
}

TEST(Orphans, ExtendPointerList) {
  MallocMessageBuilder message;

  // Allocate data in advance so that the list itself is at the end of the segment.
  kj::Vector<Orphan<Text>> pointers(7);
  for (uint i = 0; i < 7; i++) {
    pointers.add(message.getOrphanage().newOrphanCopy(Text::Reader(kj::str("foo", i))));
  }
  size_t sizeBeforeList = message.getSegmentsForOutput()[0].size();

  auto orphan = message.getOrphanage().newOrphan<List<Text>>(7);
  auto builder = orphan.get();
  for (uint i = 0; i < 7; i++) {
    builder.adopt(i, kj::mv(pointers[i]));
  }

  EXPECT_EQ(sizeBeforeList + 7, message.getSegmentsForOutput()[0].size());
  orphan.truncate(11);
  EXPECT_EQ(sizeBeforeList + 11, message.getSegmentsForOutput()[0].size());

  auto reader = orphan.getReader();
  EXPECT_EQ(11, reader.size());

  for (uint i = 0; i < 7; i++) {
    EXPECT_EQ(kj::str("foo", i), reader[i]);
    EXPECT_EQ(kj::str("foo", i), builder[i]);
  }
  for (uint i = 7; i < 11; i++) {
    EXPECT_TRUE(reader[i] == nullptr);
  }

  // Can't compare pointers directly, but we can check if builder modifications are visible using
  // the reader.
  builder.set(0, "bar");
  EXPECT_EQ("bar", reader[0]);
}

TEST(Orphans, ExtendPointerListCopy) {
  MallocMessageBuilder message;

  // Allocate data in advance so that the list itself is at the end of the segment.
  kj::Vector<Orphan<Text>> pointers(7);
  for (uint i = 0; i < 7; i++) {
    pointers.add(message.getOrphanage().newOrphanCopy(Text::Reader(kj::str("foo", i))));
  }

  auto orphan = message.getOrphanage().newOrphan<List<Text>>(7);
  auto builder = orphan.get();
  for (uint i = 0; i < 7; i++) {
    builder.adopt(i, kj::mv(pointers[i]));
  }

  auto orphan2 = message.getOrphanage().newOrphan<Data>(1);
  orphan2.get()[0] = 32;

  orphan.truncate(11);

  auto reader = orphan.getReader();
  EXPECT_EQ(11, reader.size());

  for (uint i = 0; i < 7; i++) {
    EXPECT_EQ(kj::str("foo", i), reader[i]);
    EXPECT_TRUE(builder[i] == nullptr);
  }
  for (uint i = 7; i < 11; i++) {
    EXPECT_TRUE(reader[i] == nullptr);
  }

  // Can't compare pointers directly, but we can check if builder modifications are visible using
  // the reader.
  builder.set(0, "bar");
  EXPECT_EQ("foo0", reader[0]);

  EXPECT_EQ(32, orphan2.getReader()[0]);
}

TEST(Orphans, ExtendPointerListFromEmpty) {
  MallocMessageBuilder message;

  auto orphan = message.initRoot<TestAllTypes>().disownTextList();
  orphan.truncate(3);

  auto reader = orphan.getReader();
  EXPECT_EQ(3, reader.size());

  for (uint i = 0; i < 3; i++) {
    EXPECT_EQ("", reader[i]);
  }
}

TEST(Orphans, TruncateStructList) {
  MallocMessageBuilder message;

  // Allocate data in advance so that the list itself is at the end of the segment.
  kj::Vector<Orphan<TestAllTypes>> pointers(7);
  for (uint i = 0; i < 7; i++) {
    auto o = message.getOrphanage().newOrphan<TestAllTypes>();
    auto b = o.get();
    b.setUInt32Field(123456789 + i);
    b.setTextField(kj::str("foo", i));
    b.setUInt8List({123, 45});
    pointers.add(kj::mv(o));
  }

  auto orphan = message.getOrphanage().newOrphan<List<TestAllTypes>>(7);
  auto builder = orphan.get();
  for (uint i = 0; i < 7; i++) {
    builder.adoptWithCaveats(i, kj::mv(pointers[i]));
  }

  size_t sizeBeforeTruncate = message.getSegmentsForOutput()[0].size();
  orphan.truncate(3);
  EXPECT_LT(message.getSegmentsForOutput()[0].size(), sizeBeforeTruncate);

  auto reader = orphan.getReader();
  EXPECT_EQ(3, reader.size());

  for (uint i = 0; i < 3; i++) {
    EXPECT_EQ(123456789 + i, reader[i].getUInt32Field());
    EXPECT_EQ(kj::str("foo", i), reader[i].getTextField());
    checkList(reader[i].getUInt8List(), {123, 45});

    EXPECT_EQ(123456789 + i, builder[i].getUInt32Field());
    EXPECT_EQ(kj::str("foo", i), builder[i].getTextField());
    checkList(builder[i].getUInt8List(), {123, 45});
  }
  for (uint i = 3; i < 7; i++) {
    checkTestMessageAllZero(builder[i]);
  }

  // Can't compare pointers directly, but we can check if builder modifications are visible using
  // the reader.
  builder[0].setUInt32Field(321);
  EXPECT_EQ(321, reader[0].getUInt32Field());
}

TEST(Orphans, ExtendStructList) {
  MallocMessageBuilder message;

  // Allocate data in advance so that the list itself is at the end of the segment.
  kj::Vector<Orphan<TestAllTypes>> pointers(7);
  for (uint i = 0; i < 7; i++) {
    auto o = message.getOrphanage().newOrphan<TestAllTypes>();
    auto b = o.get();
    b.setUInt32Field(123456789 + i);
    b.setTextField(kj::str("foo", i));
    b.setUInt8List({123, 45});
    pointers.add(kj::mv(o));
  }

  auto orphan = message.getOrphanage().newOrphan<List<TestAllTypes>>(7);
  auto builder = orphan.get();
  for (uint i = 0; i < 7; i++) {
    builder.adoptWithCaveats(i, kj::mv(pointers[i]));
  }

  orphan.truncate(11);

  auto reader = orphan.getReader();
  EXPECT_EQ(11, reader.size());

  for (uint i = 0; i < 7; i++) {
    EXPECT_EQ(123456789 + i, reader[i].getUInt32Field());
    EXPECT_EQ(kj::str("foo", i), reader[i].getTextField());
    checkList(reader[i].getUInt8List(), {123, 45});

    EXPECT_EQ(123456789 + i, builder[i].getUInt32Field());
    EXPECT_EQ(kj::str("foo", i), builder[i].getTextField());
    checkList(builder[i].getUInt8List(), {123, 45});
  }
  for (uint i = 7; i < 11; i++) {
    checkTestMessageAllZero(reader[i]);
  }

  // Can't compare pointers directly, but we can check if builder modifications are visible using
  // the reader.
  builder[0].setUInt32Field(321);
  EXPECT_EQ(321, reader[0].getUInt32Field());
}

TEST(Orphans, ExtendStructListCopy) {
  MallocMessageBuilder message;

  // Allocate data in advance so that the list itself is at the end of the segment.
  kj::Vector<Orphan<TestAllTypes>> pointers(7);
  for (uint i = 0; i < 7; i++) {
    auto o = message.getOrphanage().newOrphan<TestAllTypes>();
    auto b = o.get();
    b.setUInt32Field(123456789 + i);
    b.setTextField(kj::str("foo", i));
    b.setUInt8List({123, 45});
    pointers.add(kj::mv(o));
  }

  auto orphan = message.getOrphanage().newOrphan<List<TestAllTypes>>(7);
  auto builder = orphan.get();
  for (uint i = 0; i < 7; i++) {
    builder.adoptWithCaveats(i, kj::mv(pointers[i]));
  }

  auto orphan2 = message.getOrphanage().newOrphan<Data>(1);
  orphan2.get()[0] = 32;

  orphan.truncate(11);

  auto reader = orphan.getReader();
  EXPECT_EQ(11, reader.size());

  for (uint i = 0; i < 7; i++) {
    EXPECT_EQ(123456789 + i, reader[i].getUInt32Field());
    EXPECT_EQ(kj::str("foo", i), reader[i].getTextField());
    checkList(reader[i].getUInt8List(), {123, 45});

    checkTestMessageAllZero(builder[i]);
  }
  for (uint i = 7; i < 11; i++) {
    checkTestMessageAllZero(reader[i]);
  }

  // Can't compare pointers directly, but we can check if builder modifications are visible using
  // the reader.
  builder[0].setUInt32Field(321);
  EXPECT_EQ(123456789, reader[0].getUInt32Field());

  EXPECT_EQ(32, orphan2.getReader()[0]);
}

TEST(Orphans, ExtendStructListFromEmpty) {
  MallocMessageBuilder message;

  auto orphan = message.initRoot<TestAllTypes>().disownStructList();
  orphan.truncate(3);

  auto reader = orphan.getReader();
  EXPECT_EQ(3, reader.size());

  for (uint i = 0; i < 3; i++) {
    checkTestMessageAllZero(reader[i]);
  }
}

template <typename ListBuilder>
void initList(ListBuilder builder,
    std::initializer_list<ReaderFor<ListElementType<FromBuilder<ListBuilder>>>> values) {
  KJ_ASSERT(builder.size() == values.size());
  size_t i = 0;
  for (auto& value: values) {
    builder.set(i++, value);
  }
}

TEST(Orphans, ConcatenatePrimitiveLists) {
  MallocMessageBuilder message;
  auto orphanage = message.getOrphanage();

  auto list1 = orphanage.newOrphan<List<uint32_t>>(3);
  initList(list1.get(), {12, 34, 56});

  auto list2 = orphanage.newOrphan<List<uint32_t>>(2);
  initList(list2.get(), {78, 90});

  auto list3 = orphanage.newOrphan<List<uint32_t>>(4);
  initList(list3.get(), {23, 45, 67, 89});

  List<uint32_t>::Reader lists[] = { list1.getReader(), list2.getReader(), list3.getReader() };
  kj::ArrayPtr<List<uint32_t>::Reader> array = lists;

  auto cat = message.getOrphanage().newOrphanConcat(array);

  checkList(cat.getReader(), {12, 34, 56, 78, 90, 23, 45, 67, 89});
}

TEST(Orphans, ConcatenateBitLists) {
  MallocMessageBuilder message;
  auto orphanage = message.getOrphanage();

  auto list1 = orphanage.newOrphan<List<bool>>(3);
  initList(list1.get(), {true, true, false});

  auto list2 = orphanage.newOrphan<List<bool>>(2);
  initList(list2.get(), {false, true});

  auto list3 = orphanage.newOrphan<List<bool>>(4);
  initList(list3.get(), {false, false, true, false});

  List<bool>::Reader lists[] = { list1.getReader(), list2.getReader(), list3.getReader() };
  kj::ArrayPtr<List<bool>::Reader> array = lists;

  auto cat = message.getOrphanage().newOrphanConcat(array);

  checkList(cat.getReader(), {true, true, false, false, true, false, false, true, false});
}

TEST(Orphans, ConcatenatePointerLists) {
  MallocMessageBuilder message;
  auto orphanage = message.getOrphanage();

  auto list1 = orphanage.newOrphan<List<Text>>(3);
  initList(list1.get(), {"foo", "bar", "baz"});

  auto list2 = orphanage.newOrphan<List<Text>>(2);
  initList(list2.get(), {"qux", "corge"});

  auto list3 = orphanage.newOrphan<List<Text>>(4);
  initList(list3.get(), {"grault", "garply", "waldo", "fred"});

  List<Text>::Reader lists[] = { list1.getReader(), list2.getReader(), list3.getReader() };
  kj::ArrayPtr<List<Text>::Reader> array = lists;

  auto cat = message.getOrphanage().newOrphanConcat(array);

  checkList(cat.getReader(), {
      "foo", "bar", "baz", "qux", "corge", "grault", "garply", "waldo", "fred"});
}

TEST(Orphans, ConcatenateStructLists) {
  // In this test, we not only concatenate two struct lists, but we concatenate in a list that
  // contains a newer-than-expected version of the struct with extra fields, in order to verify
  // that the new fields aren't lost.

  MallocMessageBuilder message;
  auto orphanage = message.getOrphanage();

  auto orphan1 = orphanage.newOrphan<List<test::TestOldVersion>>(2);
  auto list1 = orphan1.get();
  list1[0].setOld1(12);
  list1[0].setOld2("foo");
  list1[1].setOld1(34);
  list1[1].setOld2("bar");

  auto orphan2 = orphanage.newOrphan<test::TestAnyPointer>();
  auto list2 = orphan2.get().getAnyPointerField().initAs<List<test::TestNewVersion>>(2);
  list2[0].setOld1(56);
  list2[0].setOld2("baz");
  list2[0].setNew1(123);
  list2[0].setNew2("corge");
  list2[1].setOld1(78);
  list2[1].setOld2("qux");
  list2[1].setNew1(456);
  list2[1].setNew2("grault");

  List<test::TestOldVersion>::Reader lists[] = {
    orphan1.getReader(),
    orphan2.getReader().getAnyPointerField().getAs<List<test::TestOldVersion>>()
  };
  kj::ArrayPtr<List<test::TestOldVersion>::Reader> array = lists;

  auto orphan3 = orphanage.newOrphan<test::TestAnyPointer>();
  orphan3.get().getAnyPointerField().adopt(message.getOrphanage().newOrphanConcat(array));

  auto cat = orphan3.getReader().getAnyPointerField().getAs<List<test::TestNewVersion>>();
  ASSERT_EQ(4, cat.size());

  EXPECT_EQ(12, cat[0].getOld1());
  EXPECT_EQ("foo", cat[0].getOld2());
  EXPECT_EQ(987, cat[0].getNew1());
  EXPECT_FALSE(cat[0].hasNew2());

  EXPECT_EQ(34, cat[1].getOld1());
  EXPECT_EQ("bar", cat[1].getOld2());
  EXPECT_EQ(987, cat[1].getNew1());
  EXPECT_FALSE(cat[1].hasNew2());

  EXPECT_EQ(56, cat[2].getOld1());
  EXPECT_EQ("baz", cat[2].getOld2());
  EXPECT_EQ(123, cat[2].getNew1());
  EXPECT_EQ("corge", cat[2].getNew2());

  EXPECT_EQ(78, cat[3].getOld1());
  EXPECT_EQ("qux", cat[3].getOld2());
  EXPECT_EQ(456, cat[3].getNew1());
  EXPECT_EQ("grault", cat[3].getNew2());
}

TEST(Orphans, ConcatenateStructListsUpgradeFromPrimitive) {
  // Like above, but we're "upgrading" a primitive list to a struct list.

  MallocMessageBuilder message;
  auto orphanage = message.getOrphanage();

  auto orphan1 = orphanage.newOrphan<List<test::TestOldVersion>>(2);
  auto list1 = orphan1.get();
  list1[0].setOld1(12);
  list1[0].setOld2("foo");
  list1[1].setOld1(34);
  list1[1].setOld2("bar");

  auto orphan2 = orphanage.newOrphan<test::TestAnyPointer>();
  auto list2 = orphan2.get().getAnyPointerField().initAs<List<uint64_t>>(2);
  initList(list2, {12345, 67890});

  List<test::TestOldVersion>::Reader lists[] = {
    orphan1.getReader(),
    orphan2.getReader().getAnyPointerField().getAs<List<test::TestOldVersion>>()
  };
  kj::ArrayPtr<List<test::TestOldVersion>::Reader> array = lists;

  auto orphan3 = message.getOrphanage().newOrphanConcat(array);
  auto cat = orphan3.getReader();
  ASSERT_EQ(4, cat.size());

  EXPECT_EQ(12, cat[0].getOld1());
  EXPECT_EQ("foo", cat[0].getOld2());

  EXPECT_EQ(34, cat[1].getOld1());
  EXPECT_EQ("bar", cat[1].getOld2());

  EXPECT_EQ(12345, cat[2].getOld1());
  EXPECT_FALSE(cat[2].hasOld2());

  EXPECT_EQ(67890, cat[3].getOld1());
  EXPECT_FALSE(cat[3].hasOld2());
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
