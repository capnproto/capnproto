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

#include "test.capnp.h"
#include "message.h"
#include <gtest/gtest.h>

namespace capnproto {
namespace internal {
namespace {

TEST(Encoding, Simple) {
  std::unique_ptr<MessageBuilder> message = newMallocMessage(512 * WORDS);
  SegmentBuilder* segment = message->getSegmentWithAvailable(1 * WORDS);
  word* rootLocation = segment->allocate(1 * WORDS);

  Foo::Builder builder(StructBuilder::initRoot(segment, rootLocation, Foo::DEFAULT.words));

  EXPECT_EQ(1234, builder.getA());
  EXPECT_EQ(-1, builder.getB());
  EXPECT_EQ(200, builder.getC());
  ASSERT_EQ(0u, builder.getNums().size());

  builder.setA(321);
  builder.setB(45);
  builder.setC(67);
  builder.initD().setX(55.25);

  {
    List<int32_t>::Builder listBuilder = builder.initNums(5);
    ASSERT_EQ(5u, listBuilder.size());
    listBuilder.set(0, 12);
    listBuilder.set(1, 34);
    listBuilder.set(2, 56);
    listBuilder.set(3, 78);
    listBuilder.set(4, 90);

    EXPECT_EQ(12, listBuilder[0]);
    EXPECT_EQ(34, listBuilder[1]);
    EXPECT_EQ(56, listBuilder[2]);
    EXPECT_EQ(78, listBuilder[3]);
    EXPECT_EQ(90, listBuilder[4]);

    {
      int sum = 0;
      for (int32_t i: listBuilder) {
        sum += i;
      }
      EXPECT_EQ(12 + 34 + 56 + 78 + 90, sum);
    }
  }

  {
    List<Bar>::Builder structListBuilder = builder.initBars(3);
    ASSERT_EQ(3u, structListBuilder.size());

    structListBuilder[0].setX(123);
    structListBuilder[1].setX(456);
    structListBuilder[2].setX(789);

    EXPECT_EQ(123, structListBuilder[0].getX());
    EXPECT_EQ(456, structListBuilder[1].getX());
    EXPECT_EQ(789, structListBuilder[2].getX());

    {
      double sum = 0;
      for (auto bar: structListBuilder) {
        sum += bar.getX();
      }
      EXPECT_EQ(123 + 456 + 789, sum);
    }
  }

  {
    List<Bar>::Builder structListBuilder = builder.getBars();
    ASSERT_EQ(3u, structListBuilder.size());

    EXPECT_EQ(123, structListBuilder[0].getX());
    EXPECT_EQ(456, structListBuilder[1].getX());
    EXPECT_EQ(789, structListBuilder[2].getX());

    {
      double sum = 0;
      for (auto bar: structListBuilder) {
        sum += bar.getX();
      }
      EXPECT_EQ(123 + 456 + 789, sum);
    }
  }

  {
    List<List<int32_t>>::Builder listListBuilder = builder.initPrimListList(2);
    ASSERT_EQ(2u, listListBuilder.size());

    List<int32_t>::Builder sublist = listListBuilder.init(0, 2);
    ASSERT_EQ(2u, sublist.size());
    sublist.set(0, 1234);
    sublist.set(1, 5678);

    sublist = listListBuilder.init(1, 4);
    ASSERT_EQ(4u, sublist.size());
    sublist.set(0, 21);
    sublist.set(1, 43);
    sublist.set(2, 65);
    sublist.set(3, 87);
  }

  {
    List<List<Bar>>::Builder listListBuilder = builder.initStructListList(2);
    ASSERT_EQ(2u, listListBuilder.size());

    List<Bar>::Builder sublist = listListBuilder.init(0, 2);
    ASSERT_EQ(2u, sublist.size());
    sublist[0].setX(1234);
    sublist[1].setX(5678);

    sublist = listListBuilder.init(1, 4);
    ASSERT_EQ(4u, sublist.size());
    sublist[0].setX(21);
    sublist[1].setX(43);
    sublist[2].setX(65);
    sublist[3].setX(87);
  }

  EXPECT_EQ(321, builder.getA());
  EXPECT_EQ(45, builder.getB());
  EXPECT_EQ(67, builder.getC());
  EXPECT_EQ(55.25, builder.getD().getX());

  Foo::Reader reader(StructReader::readRoot(
      segment->getStartPtr(), Foo::DEFAULT.words, segment, 4));

  EXPECT_EQ(321, reader.getA());
  EXPECT_EQ(45, reader.getB());
  EXPECT_EQ(67, reader.getC());
  EXPECT_EQ(55.25, reader.getD().getX());

  {
    List<int32_t>::Reader listReader = reader.getNums();
    ASSERT_EQ(5u, listReader.size());
    EXPECT_EQ(12, listReader[0]);
    EXPECT_EQ(34, listReader[1]);
    EXPECT_EQ(56, listReader[2]);
    EXPECT_EQ(78, listReader[3]);
    EXPECT_EQ(90, listReader[4]);

    {
      int sum = 0;
      for (int32_t i: listReader) {
        sum += i;
      }
      EXPECT_EQ(12 + 34 + 56 + 78 + 90, sum);
    }
  }

  {
    List<Bar>::Reader structListReader = reader.getBars();
    ASSERT_EQ(3u, structListReader.size());

    EXPECT_EQ(123, structListReader[0].getX());
    EXPECT_EQ(456, structListReader[1].getX());
    EXPECT_EQ(789, structListReader[2].getX());

    {
      double sum = 0;
      for (auto bar: structListReader) {
        sum += bar.getX();
      }
      EXPECT_EQ(123 + 456 + 789, sum);
    }
  }

  {
    List<List<int32_t>>::Reader listListReader = reader.getPrimListList();
    ASSERT_EQ(2u, listListReader.size());

    List<int32_t>::Reader sublist = listListReader[0];
    ASSERT_EQ(2u, sublist.size());
    EXPECT_EQ(1234, sublist[0]);
    EXPECT_EQ(5678, sublist[1]);

    sublist = listListReader[1];
    ASSERT_EQ(4u, sublist.size());
    EXPECT_EQ(21, sublist[0]);
    EXPECT_EQ(43, sublist[1]);
    EXPECT_EQ(65, sublist[2]);
    EXPECT_EQ(87, sublist[3]);
  }

  {
    List<List<Bar>>::Reader listListReader = reader.getStructListList();
    ASSERT_EQ(2u, listListReader.size());

    List<Bar>::Reader sublist = listListReader[0];
    ASSERT_EQ(2u, sublist.size());
    EXPECT_EQ(1234, sublist[0].getX());
    EXPECT_EQ(5678, sublist[1].getX());

    sublist = listListReader[1];
    ASSERT_EQ(4u, sublist.size());
    EXPECT_EQ(21, sublist[0].getX());
    EXPECT_EQ(43, sublist[1].getX());
    EXPECT_EQ(65, sublist[2].getX());
    EXPECT_EQ(87, sublist[3].getX());
  }
}

}  // namespace
}  // namespace internal
}  // namespace capnproto
