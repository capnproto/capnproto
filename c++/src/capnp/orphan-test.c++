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

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
