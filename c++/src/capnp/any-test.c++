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

#if !_MSC_VER || defined(__clang__) // TODO(msvc): ICE on the necessary constructor; see any.h.
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

#if !_MSC_VER || defined(__clang__)  // TODO(msvc): ICE on the necessary constructor; see any.h.
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

#if !_MSC_VER || defined(__clang__) // TODO(msvc): ICE on the necessary constructor; see any.h.
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

#if !_MSC_VER || defined(__clang__) // TODO(msvc): ICE on the necessary constructor; see any.h.
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

    EXPECT_TRUE(root.hasAnyStructField());
    auto orphan = root.disownAnyStructField();
    checkTestMessage(orphan.getReader().as<TestAllTypes>());
    EXPECT_FALSE(root.hasAnyStructField());

    root.adoptAnyStructField(kj::mv(orphan));
    EXPECT_TRUE(root.hasAnyStructField());
    checkTestMessage(root.getAnyStructField().as<TestAllTypes>());
  }

  {
    List<int>::Builder list = root.initAnyListFieldAs<List<int>>(3);
    list.set(0, 123);
    list.set(1, 456);
    list.set(2, 789);

    AnyList::Builder anyList = root.getAnyListField();
    checkList(anyList.as<List<int>>(), {123, 456, 789});

    EXPECT_TRUE(root.hasAnyListField());
    auto orphan = root.disownAnyListField();
    checkList(orphan.getReader().as<List<int>>(), {123, 456, 789});
    EXPECT_FALSE(root.hasAnyListField());

    root.adoptAnyListField(kj::mv(orphan));
    EXPECT_TRUE(root.hasAnyListField());
    checkList(root.getAnyListField().as<List<int>>(), {123, 456, 789});
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

KJ_TEST("Builder::isStruct() does not corrupt segment pointer") {
  MallocMessageBuilder builder(1); // small first segment
  auto root = builder.getRoot<AnyPointer>();

  // Do a lot of allocations so that there is likely a segment with a decent
  // amount of free space.
  initTestMessage(root.initAs<test::TestAllTypes>());

  // This will probably get allocated in a segment that still has room for the
  // Data allocation below.
  root.initAs<test::TestAllTypes>();

  // At one point, this caused root.builder.segment to point to the segment
  // where the struct is allocated, rather than segment where the root pointer
  // lives, i.e. segment zero.
  EXPECT_TRUE(root.isStruct());

  // If root.builder.segment points to the wrong segment and that segment has free
  // space, then this triggers a DREQUIRE failure in WirePointer::setKindAndTarget().
  root.initAs<Data>(1);
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

KJ_TEST("Bit list with nonzero pad bits") {
  AlignedData<2> segment1 = {{
      0x01, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, // eleven bit-sized elements
      0xee, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // twelfth bit is set!
  }};
  kj::ArrayPtr<const word> segments1[1] = {
    kj::arrayPtr(segment1.words, 2)
  };
  SegmentArrayMessageReader message1(kj::arrayPtr(segments1, 1));

  AlignedData<2> segment2 = {{
      0x01, 0x00, 0x00, 0x00, 0x59, 0x00, 0x00, 0x00, // eleven bit-sized elements
      0xee, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // twelfth bit is not set
  }};
  kj::ArrayPtr<const word> segments2[1] = {
    kj::arrayPtr(segment2.words, 2)
  };
  SegmentArrayMessageReader message2(kj::arrayPtr(segments2, 1));

  // Should be equal, despite nonzero padding.
  KJ_ASSERT(message1.getRoot<AnyList>() == message2.getRoot<AnyList>());
}

KJ_TEST("Pointer list unequal to struct list") {
  AlignedData<1> segment1 = {{
      // list with zero pointer-sized elements
      0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
  }};
  kj::ArrayPtr<const word> segments1[1] = {
    kj::arrayPtr(segment1.words, 1)
  };
  SegmentArrayMessageReader message1(kj::arrayPtr(segments1, 1));

  AlignedData<2> segment2 = {{
      // struct list of length zero
      0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,

      // struct list tag, zero elements
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  }};
  kj::ArrayPtr<const word> segments2[1] = {
    kj::arrayPtr(segment2.words, 2)
  };
  SegmentArrayMessageReader message2(kj::arrayPtr(segments2, 1));

  EXPECT_EQ(Equality::NOT_EQUAL, message1.getRoot<AnyList>().equals(message2.getRoot<AnyList>()));
}

KJ_TEST("Truncating non-null pointer fields does not preserve equality") {
  AlignedData<3> segment1 = {{
      // list with one data word and one pointer field
      0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,

      // data word
      0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,

      // non-null pointer to zero-sized struct
      0xfc, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
  }};
  kj::ArrayPtr<const word> segments1[1] = {
    kj::arrayPtr(segment1.words, 3)
  };
  SegmentArrayMessageReader message1(kj::arrayPtr(segments1, 1));

  AlignedData<2> segment2 = {{
      // list with one data word and zero pointers
      0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,

      // data word
      0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab, 0xab,
  }};
  kj::ArrayPtr<const word> segments2[1] = {
    kj::arrayPtr(segment2.words, 2)
  };
  SegmentArrayMessageReader message2(kj::arrayPtr(segments2, 1));

  EXPECT_EQ(Equality::NOT_EQUAL,
            message1.getRoot<AnyPointer>().equals(message2.getRoot<AnyPointer>()));
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
