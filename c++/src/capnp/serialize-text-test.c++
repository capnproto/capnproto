// Copyright (c) 2015 Philip Quinn.
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

#include "serialize-text.h"
#include <kj/compat/gtest.h>
#include <kj/string.h>
#include <capnp/pretty-print.h>
#include <capnp/message.h>
#include "test-util.h"

#include <capnp/test.capnp.h>

namespace capnp {
namespace _ {  // private
namespace {

KJ_TEST("TestAllTypes") {
  MallocMessageBuilder builder;
  initTestMessage(builder.initRoot<TestAllTypes>());

  {
    // Plain output
    TextCodec codec;
    codec.setPrettyPrint(false);
    auto text = codec.encode(builder.getRoot<TestAllTypes>());

    auto stringify = kj::str(builder.getRoot<TestAllTypes>());
    KJ_EXPECT(text == stringify);

    MallocMessageBuilder reader;
    auto orphan = codec.decode<TestAllTypes>(text, reader.getOrphanage());
    auto structReader = orphan.getReader();
    checkTestMessage(structReader);
  }
  {
    // Pretty output
    TextCodec codec;
    codec.setPrettyPrint(true);
    auto text = codec.encode(builder.getRoot<TestAllTypes>());

    auto stringify = prettyPrint(builder.getRoot<TestAllTypes>()).flatten();
    KJ_EXPECT(text == stringify);

    MallocMessageBuilder reader;
    auto orphan = codec.decode<TestAllTypes>(text, reader.getOrphanage());
    auto structReader = orphan.getReader();
    checkTestMessage(structReader);
  }
}

KJ_TEST("TestDefaults") {
  MallocMessageBuilder builder;
  initTestMessage(builder.initRoot<TestDefaults>());

  TextCodec codec;
  auto text = codec.encode(builder.getRoot<TestDefaults>());

  MallocMessageBuilder reader;
  auto orphan = codec.decode<TestDefaults>(text, reader.getOrphanage());
  auto structReader = orphan.getReader();
  checkTestMessage(structReader);
}

KJ_TEST("TestListDefaults") {
  MallocMessageBuilder builder;
  initTestMessage(builder.initRoot<TestListDefaults>());

  TextCodec codec;
  auto text = codec.encode(builder.getRoot<TestListDefaults>());

  MallocMessageBuilder reader;
  auto orphan = codec.decode<TestListDefaults>(text, reader.getOrphanage());
  auto structReader = orphan.getReader();
  checkTestMessage(structReader);
}

KJ_TEST("raw text") {
  using TestType = capnproto_test::capnp::test::TestLateUnion;

  kj::String message =
      kj::str(R"((
        foo = -123, bar = "bar", baz = 456,
        # Test Comment
        theUnion = ( qux = "qux" ),
        anotherUnion = ( corge = [ 7, 8, 9 ] ),
      ))");

  MallocMessageBuilder builder;
  auto testType = builder.initRoot<TestType>();

  TextCodec codec;
  codec.decode(message, testType);

  auto reader = testType.asReader();
  KJ_EXPECT(reader.getFoo() == -123);
  KJ_EXPECT(reader.getBar() == "bar");
  KJ_EXPECT(reader.getBaz() == 456);

  KJ_EXPECT(reader.getTheUnion().isQux());
  KJ_EXPECT(reader.getTheUnion().hasQux());
  KJ_EXPECT(reader.getTheUnion().getQux() == "qux");

  KJ_EXPECT(reader.getAnotherUnion().isCorge());
  KJ_EXPECT(reader.getAnotherUnion().hasCorge());
  KJ_EXPECT(reader.getAnotherUnion().getCorge().size() == 3);
  KJ_EXPECT(reader.getAnotherUnion().getCorge()[0] == 7);
  KJ_EXPECT(reader.getAnotherUnion().getCorge()[1] == 8);
  KJ_EXPECT(reader.getAnotherUnion().getCorge()[2] == 9);
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
