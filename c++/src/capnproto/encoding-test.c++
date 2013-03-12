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
  ASSERT_EQ(0, builder.getNums().size());

  builder.setA(321);
  builder.setB(45);
  builder.setC(67);
  builder.initD().setX(55.25);

  List<int32_t>::Builder listBuilder = builder.initNums(5);
  ASSERT_EQ(5, listBuilder.size());
  listBuilder[0] = 12;
  listBuilder[1] = 34;
  listBuilder[2] = 56;
  listBuilder[3] = 78;
  listBuilder[4] = 90;

  {
    int sum = 0;
    for (int32_t i: listBuilder) {
      sum += i;
    }
    EXPECT_EQ(12 + 34 + 56 + 78 + 90, sum);
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

  List<int32_t>::Reader listReader = reader.getNums();
  ASSERT_EQ(5, listReader.size());
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

}  // namespace
}  // namespace internal
}  // namespace capnproto
