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

#include "any.h"
#include "message.h"
#include <kj/compat/gtest.h>
#include "test-util.h"

namespace capnp {
namespace _ {  // private
namespace {

TEST(Any, AnyPointer) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<test::TestAnyPointer>();

  initTestMessage(root.getAnyPointerField().initAs<TestAllTypes>());
  checkTestMessage(root.getAnyPointerField().getAs<TestAllTypes>());
  checkTestMessage(root.asReader().getAnyPointerField().getAs<TestAllTypes>());

  root.getAnyPointerField().setAs<Text>("foo");
  EXPECT_EQ("foo", root.getAnyPointerField().getAs<Text>());
  EXPECT_EQ("foo", root.asReader().getAnyPointerField().getAs<Text>());

  root.getAnyPointerField().setAs<Data>(data("foo"));
  EXPECT_EQ(data("foo"), root.getAnyPointerField().getAs<Data>());
  EXPECT_EQ(data("foo"), root.asReader().getAnyPointerField().getAs<Data>());

  {
    root.getAnyPointerField().setAs<List<uint32_t>>({123, 456, 789});

    {
      List<uint32_t>::Builder list = root.getAnyPointerField().getAs<List<uint32_t>>();
      ASSERT_EQ(3u, list.size());
      EXPECT_EQ(123u, list[0]);
      EXPECT_EQ(456u, list[1]);
      EXPECT_EQ(789u, list[2]);
    }

    {
      List<uint32_t>::Reader list = root.asReader().getAnyPointerField().getAs<List<uint32_t>>();
      ASSERT_EQ(3u, list.size());
      EXPECT_EQ(123u, list[0]);
      EXPECT_EQ(456u, list[1]);
      EXPECT_EQ(789u, list[2]);
    }
  }

  {
    root.getAnyPointerField().setAs<List<Text>>({"foo", "bar"});

    {
      List<Text>::Builder list = root.getAnyPointerField().getAs<List<Text>>();
      ASSERT_EQ(2u, list.size());
      EXPECT_EQ("foo", list[0]);
      EXPECT_EQ("bar", list[1]);
    }

    {
      List<Text>::Reader list = root.asReader().getAnyPointerField().getAs<List<Text>>();
      ASSERT_EQ(2u, list.size());
      EXPECT_EQ("foo", list[0]);
      EXPECT_EQ("bar", list[1]);
    }
  }

  {
    {
      List<TestAllTypes>::Builder list = root.getAnyPointerField().initAs<List<TestAllTypes>>(2);
      ASSERT_EQ(2u, list.size());
      initTestMessage(list[0]);
    }

    {
      List<TestAllTypes>::Builder list = root.getAnyPointerField().getAs<List<TestAllTypes>>();
      ASSERT_EQ(2u, list.size());
      checkTestMessage(list[0]);
      checkTestMessageAllZero(list[1]);
    }

    {
      List<TestAllTypes>::Reader list =
          root.asReader().getAnyPointerField().getAs<List<TestAllTypes>>();
      ASSERT_EQ(2u, list.size());
      checkTestMessage(list[0]);
      checkTestMessageAllZero(list[1]);
    }
  }
}

TEST(Any, AnyStruct) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<test::TestAnyPointer>();

  initTestMessage(root.getAnyPointerField().initAs<TestAllTypes>());
  checkTestMessage(root.getAnyPointerField().getAs<TestAllTypes>());
  checkTestMessage(root.asReader().getAnyPointerField().getAs<TestAllTypes>());

  auto allTypes = root.getAnyPointerField().getAs<AnyStruct>().as<TestAllTypes>();
  auto allTypesReader = root.getAnyPointerField().getAs<AnyStruct>().asReader().as<TestAllTypes>();
  allTypes.setInt32Field(100);
  EXPECT_EQ(100, allTypes.getInt32Field());
  EXPECT_EQ(100, allTypesReader.getInt32Field());

  EXPECT_EQ(48, root.getAnyPointerField().getAs<AnyStruct>().getDataSection().size());
  EXPECT_EQ(20, root.getAnyPointerField().getAs<AnyStruct>().getPointerSection().size());

  EXPECT_EQ(48, root.getAnyPointerField().asReader().getAs<AnyStruct>().getDataSection().size());
  EXPECT_EQ(20, root.getAnyPointerField().asReader().getAs<AnyStruct>().getPointerSection().size());

  auto b = toAny(root.getAnyPointerField().getAs<TestAllTypes>());
  EXPECT_EQ(48, b.getDataSection().size());
  EXPECT_EQ(20, b.getPointerSection().size());

#if !_MSC_VER  // TODO(msvc): ICE on the necessary constructor; see any.h.
  b = root.getAnyPointerField().getAs<TestAllTypes>();
  EXPECT_EQ(48, b.getDataSection().size());
  EXPECT_EQ(20, b.getPointerSection().size());
#endif

  auto r = toAny(root.getAnyPointerField().getAs<TestAllTypes>().asReader());
  EXPECT_EQ(48, r.getDataSection().size());
  EXPECT_EQ(20, r.getPointerSection().size());

  r = toAny(root.getAnyPointerField().getAs<TestAllTypes>()).asReader();
  EXPECT_EQ(48, r.getDataSection().size());
  EXPECT_EQ(20, r.getPointerSection().size());

#if !_MSC_VER  // TODO(msvc): ICE on the necessary constructor; see any.h.
  r = root.getAnyPointerField().getAs<TestAllTypes>().asReader();
  EXPECT_EQ(48, r.getDataSection().size());
  EXPECT_EQ(20, r.getPointerSection().size());
#endif

  {
    MallocMessageBuilder b2;
    auto root2 = b2.getRoot<test::TestAnyPointer>();
    auto sb = root2.getAnyPointerField().initAsAnyStruct(
        r.getDataSection().size() / 8, r.getPointerSection().size());

    EXPECT_EQ(48, sb.getDataSection().size());
    EXPECT_EQ(20, sb.getPointerSection().size());

    // TODO: is there a higher-level API for this?
    memcpy(sb.getDataSection().begin(), r.getDataSection().begin(), r.getDataSection().size());
  }

  {
    auto ptrs = r.getPointerSection();
    EXPECT_EQ("foo", ptrs[0].getAs<Text>());
    EXPECT_EQ("bar", kj::heapString(ptrs[1].getAs<Data>().asChars()));
    EXPECT_EQ("xyzzy", ptrs[15].getAs<List<Text>>()[1]);
  }

  {
    auto ptrs = b.getPointerSection();
    EXPECT_EQ("foo", ptrs[0].getAs<Text>());
    EXPECT_EQ("bar", kj::heapString(ptrs[1].getAs<Data>().asChars()));
    EXPECT_EQ("xyzzy", ptrs[15].getAs<List<Text>>()[1]);
  }
}

TEST(Any, AnyList) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<test::TestAnyPointer>();
  List<TestAllTypes>::Builder b = root.getAnyPointerField().initAs<List<TestAllTypes>>(2);
  initTestMessage(b[0]);

  auto ptr = root.getAnyPointerField().getAs<AnyList>();

  EXPECT_EQ(2, ptr.size());
  EXPECT_EQ(48, ptr.as<List<AnyStruct>>()[0].getDataSection().size());
  EXPECT_EQ(20, ptr.as<List<AnyStruct>>()[0].getPointerSection().size());

  auto readPtr = root.getAnyPointerField().asReader().getAs<AnyList>();

  EXPECT_EQ(2, readPtr.size());
  EXPECT_EQ(48, readPtr.as<List<AnyStruct>>()[0].getDataSection().size());
  EXPECT_EQ(20, readPtr.as<List<AnyStruct>>()[0].getPointerSection().size());

  auto alb = toAny(root.getAnyPointerField().getAs<List<TestAllTypes>>());
  EXPECT_EQ(2, alb.size());
  EXPECT_EQ(48, alb.as<List<AnyStruct>>()[0].getDataSection().size());
  EXPECT_EQ(20, alb.as<List<AnyStruct>>()[0].getPointerSection().size());

#if !_MSC_VER  // TODO(msvc): ICE on the necessary constructor; see any.h.
  alb = root.getAnyPointerField().getAs<List<TestAllTypes>>();
  EXPECT_EQ(2, alb.size());
  EXPECT_EQ(48, alb.as<List<AnyStruct>>()[0].getDataSection().size());
  EXPECT_EQ(20, alb.as<List<AnyStruct>>()[0].getPointerSection().size());
#endif

  auto alr = toAny(root.getAnyPointerField().getAs<List<TestAllTypes>>().asReader());
  EXPECT_EQ(2, alr.size());
  EXPECT_EQ(48, alr.as<List<AnyStruct>>()[0].getDataSection().size());
  EXPECT_EQ(20, alr.as<List<AnyStruct>>()[0].getPointerSection().size());

  alr = toAny(root.getAnyPointerField().getAs<List<TestAllTypes>>()).asReader();
  EXPECT_EQ(2, alr.size());
  EXPECT_EQ(48, alr.as<List<AnyStruct>>()[0].getDataSection().size());
  EXPECT_EQ(20, alr.as<List<AnyStruct>>()[0].getPointerSection().size());

#if !_MSC_VER  // TODO(msvc): ICE on the necessary constructor; see any.h.
  alr = root.getAnyPointerField().getAs<List<TestAllTypes>>().asReader();
  EXPECT_EQ(2, alr.size());
  EXPECT_EQ(48, alr.as<List<AnyStruct>>()[0].getDataSection().size());
  EXPECT_EQ(20, alr.as<List<AnyStruct>>()[0].getPointerSection().size());
#endif
}

TEST(Any, AnyStructListCapInSchema) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<test::TestAnyOthers>();

  {
    initTestMessage(root.initAnyStructFieldAs<TestAllTypes>());
    AnyStruct::Builder anyStruct = root.getAnyStructField();
    checkTestMessage(anyStruct.as<TestAllTypes>());
    checkTestMessage(anyStruct.asReader().as<TestAllTypes>());
  }

  {
    List<int>::Builder list = root.initAnyListFieldAs<List<int>>(3);
    list.set(0, 123);
    list.set(1, 456);
    list.set(2, 789);

    AnyList::Builder anyList = root.getAnyListField();
    checkList(anyList.as<List<int>>(), {123, 456, 789});
  }

  #if !CAPNP_LITE
  // This portion of the test relies on a Client, not present in lite-mode.
  {
    kj::EventLoop loop;
    kj::WaitScope waitScope(loop);
    int callCount = 0;
    root.setCapabilityField(kj::heap<TestInterfaceImpl>(callCount));
    Capability::Client client = root.getCapabilityField();
    auto req = client.castAs<test::TestInterface>().fooRequest();
    req.setI(123);
    req.setJ(true);
    req.send().wait(waitScope);
    EXPECT_EQ(1, callCount);
  }
  #endif
}



TEST(Any, Equals) {
  MallocMessageBuilder builderA;
  auto rootA = builderA.getRoot<test::TestAllTypes>();
  auto anyA = builderA.getRoot<AnyPointer>();
  initTestMessage(rootA);

  MallocMessageBuilder builderB;
  auto rootB = builderB.getRoot<test::TestAllTypes>();
  auto anyB = builderB.getRoot<AnyPointer>();
  initTestMessage(rootB);

  EXPECT_EQ(Equality::EQUAL, anyA.equals(anyB));

  rootA.setBoolField(false);
  EXPECT_EQ(Equality::NOT_EQUAL, anyA.equals(anyB));

  rootB.setBoolField(false);
  EXPECT_EQ(Equality::EQUAL, anyA.equals(anyB));

  rootB.setEnumField(test::TestEnum::GARPLY);
  EXPECT_EQ(Equality::NOT_EQUAL, anyA.equals(anyB));

  rootA.setEnumField(test::TestEnum::GARPLY);
  EXPECT_EQ(Equality::EQUAL, anyA.equals(anyB));

  rootA.getStructField().setTextField("buzz");
  EXPECT_EQ(Equality::NOT_EQUAL, anyA.equals(anyB));

  rootB.getStructField().setTextField("buzz");
  EXPECT_EQ(Equality::EQUAL, anyA.equals(anyB));

  rootA.initVoidList(3);
  EXPECT_EQ(Equality::NOT_EQUAL, anyA.equals(anyB));

  rootB.initVoidList(3);
  EXPECT_EQ(Equality::EQUAL, anyA.equals(anyB));

  rootA.getBoolList().set(2, true);
  EXPECT_EQ(Equality::NOT_EQUAL, anyA.equals(anyB));

  rootB.getBoolList().set(2, true);
  EXPECT_EQ(Equality::EQUAL, anyA.equals(anyB));

  rootB.getStructList()[1].setTextField("my NEW structlist 2");
  EXPECT_EQ(Equality::NOT_EQUAL, anyA.equals(anyB));

  rootA.getStructList()[1].setTextField("my NEW structlist 2");
  EXPECT_EQ(Equality::EQUAL, anyA.equals(anyB));
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
