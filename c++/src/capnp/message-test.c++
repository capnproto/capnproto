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
#include "serialize.h"
#include "test-util.h"
#include <kj/refcount.h>
#include <kj/convert.h>
#include <kj/array.h>
#include <kj/vector.h>
#include <kj/debug.h>
#include <kj/compat/gtest.h>

namespace capnp {
namespace _ {  // private
namespace {

struct OwnedTestMessage {
  kj::Array<word> words;
  FlatArrayMessageReader message;
  TestAllTypes::Reader root;
  bool& destroyed;

  OwnedTestMessage(kj::Array<word> words, bool& destroyed)
      : words(kj::mv(words)), message(this->words.asPtr()),
        root(message.getRoot<TestAllTypes>()), destroyed(destroyed) {}
  ~OwnedTestMessage() { destroyed = true; }
};

kj::Array<word> makeProjectedMessage() {
  MallocMessageBuilder message(1, AllocationStrategy::FIXED_SIZE);
  auto root = message.initRoot<TestAllTypes>();
  auto child = root.initStructField();
  child.initStructList(2)[1].setTextField("projected text");
  child.setDataField(kj::arrayPtr(reinterpret_cast<const byte*>("data"), 4));
  KJ_REQUIRE(message.getSegmentsForOutput().size() > 1);
  return messageToFlatArray(message);
}

KJ_TEST("Rc readers retain multi-segment message bytes and arena through field projections") {
  bool destroyed = false;
  auto owner = kj::rc<OwnedTestMessage>(makeProjectedMessage(), destroyed);
  auto root = kj::mv(owner).project([](auto& message) { return message.root; });
  static_assert(kj::isSameType<decltype(root), kj::Rc<TestAllTypes::Reader>>());
  auto child = kj::mv(root).project([](auto reader) { return reader.getStructField(); });
  auto data = child.addRef().project([](auto reader) { return reader.getDataField(); });
  auto list = kj::mv(child).project([](auto reader) { return reader.getStructList(); });
  auto element = kj::mv(list).project([](auto reader) { return reader[1]; });
  auto text = kj::mv(element).project([](auto reader) { return reader.getTextField(); });
  KJ_EXPECT(!destroyed);
  KJ_EXPECT(*text == "projected text");
  KJ_EXPECT(data->size() == 4);
  data = nullptr;
  KJ_EXPECT(!destroyed);
  text = nullptr;
  KJ_EXPECT(destroyed);
}

KJ_TEST("Arc reader navigation retains the immutable message context") {
  bool destroyed = false;
  auto owner = kj::arc<OwnedTestMessage>(makeProjectedMessage(), destroyed);
  auto root = kj::mv(owner).project([](auto& message) { return message.root; });
  auto clone = root.addRef();
  auto text = kj::mv(root).project([](auto reader) {
    return reader.getStructField().getStructList()[1].getTextField();
  });
  auto any = kj::mv(clone).project([](auto reader) { return AnyStruct::Reader(reader); });
  auto pointers = kj::mv(any).project([](auto reader) { return reader.getPointerSection(); });
  KJ_EXPECT(pointers->size() > 0);
  pointers = nullptr;
  KJ_EXPECT(!destroyed);
  KJ_EXPECT(*text == "projected text");
  text = nullptr;
  KJ_EXPECT(destroyed);
}

KJ_TEST("Rc builders project mutable fields and can project to readers") {
  auto owner = kj::rc<MallocMessageBuilder>();
  auto root = kj::mv(owner).project([](auto& message) {
    return message.template initRoot<TestAllTypes>();
  });
  auto child = kj::mv(root).project([](auto builder) { return builder.initStructField(); });
  auto text = child.addRef().project([](auto builder) { return builder.initTextField(3); });
  (*text)[0] = 'f';
  (*text)[1] = 'o';
  (*text)[2] = 'o';
  kj::Rc<const Text::Builder> frozen(kj::mv(text));
  auto copy = frozen.addRef();
  auto clone = frozen.clone();
  static_assert(kj::isSameType<decltype(copy), kj::Rc<const Text::Builder>>());
  frozen = nullptr;
  KJ_EXPECT(copy->asReader() == "foo");
  copy = nullptr;
  KJ_EXPECT(clone->asReader() == "foo");
  clone = nullptr;
  auto reader = kj::mv(child).project([](auto builder) { return builder.asReader(); });
  KJ_EXPECT(reader->getTextField() == "foo");
}

KJ_TEST("Rc readers support as<View> and as<Copy>") {
  bool destroyed = false;
  auto owner = kj::rc<OwnedTestMessage>(makeProjectedMessage(), destroyed);
  auto root = kj::mv(owner).project([](auto& message) { return message.root; });
  kj::Rc<TestAllTypes::Reader> rootView = root.as<kj::View>();
  KJ_EXPECT(rootView->getStructField().getDataField().size() == 4);

  auto text = root.addRef().project([](auto reader) {
    return reader.getStructField().getStructList()[1].getTextField();
  });
  kj::Rc<Text::Reader> textView = text.as<kj::View>();
  kj::Rc<kj::String> textCopy = text.as<kj::Copy>();
  KJ_EXPECT(*textView == "projected text");
  KJ_EXPECT(*textCopy == "projected text");
  KJ_EXPECT(textCopy->begin() != text->begin());

  auto data = kj::mv(root).project([](auto reader) {
    return reader.getStructField().getDataField();
  });
  kj::Rc<Data::Reader> dataView = data.as<kj::View>();
  kj::Rc<kj::Array<byte>> dataCopy = kj::mv(data).as<kj::Copy>();
  KJ_EXPECT(data == nullptr);
  KJ_EXPECT(*dataCopy == *dataView);

  rootView = nullptr;
  text = nullptr;
  textView = nullptr;
  KJ_EXPECT(!destroyed);
  dataView = nullptr;
  KJ_EXPECT(destroyed);
  KJ_EXPECT(*textCopy == "projected text");
  KJ_EXPECT(dataCopy->size() == 4);
}

KJ_TEST("Rc builders support as<View>; const builders view as readers") {
  auto owner = kj::rc<MallocMessageBuilder>();
  auto root = kj::mv(owner).project([](auto& message) {
    return message.template initRoot<TestAllTypes>();
  });
  kj::Rc<TestAllTypes::Builder> rootView = root.as<kj::View>();
  rootView->setInt32Field(123);

  auto text = root.addRef().project([](auto builder) { return builder.initTextField(3); });
  kj::Rc<Text::Builder> textView = text.as<kj::View>();
  (*textView)[0] = 'f';
  (*textView)[1] = 'o';
  (*textView)[2] = 'o';
  kj::Rc<const Text::Builder> constText(kj::mv(text));
  kj::Rc<Text::Reader> textReader = constText.as<kj::View>();
  KJ_EXPECT(*textReader == "foo");

  auto data = root.addRef().project([](auto builder) { return builder.initDataField(2); });
  kj::Rc<Data::Builder> dataView = data.as<kj::View>();
  (*dataView)[0] = 'a';
  kj::Rc<const Data::Builder> constData(kj::mv(data));
  kj::Rc<Data::Reader> dataReader = constData.as<kj::View>();
  KJ_EXPECT((*dataReader)[0] == 'a');

  kj::Rc<const TestAllTypes::Builder> constRoot(kj::mv(root));
  kj::Rc<TestAllTypes::Reader> reader = constRoot.as<kj::View>();
  KJ_EXPECT(reader->getInt32Field() == 123);
  KJ_EXPECT(reader->getTextField() == "foo");
}

TEST(Message, MallocBuilderWithFirstSegment) {
  word scratch[16];
  memset(scratch, 0, sizeof(scratch));
  MallocMessageBuilder builder(kj::arrayPtr(scratch, 16), AllocationStrategy::FIXED_SIZE);

  kj::ArrayPtr<word> segment = builder.allocateSegment(1);
  EXPECT_EQ(scratch, segment.begin());
  EXPECT_EQ(16u, segment.size());

  segment = builder.allocateSegment(1);
  EXPECT_NE(scratch, segment.begin());
  EXPECT_EQ(16u, segment.size());

  segment = builder.allocateSegment(1);
  EXPECT_NE(scratch, segment.begin());
  EXPECT_EQ(16u, segment.size());
}

class TestInitMessageBuilder: public MessageBuilder {
public:
  TestInitMessageBuilder(kj::ArrayPtr<SegmentInit> segments): MessageBuilder(segments) {}

  kj::ArrayPtr<word> allocateSegment(uint minimumSize) override {
    auto array = kj::heapArray<word>(minimumSize);
    memset(array.begin(), 0, array.asBytes().size());
    allocations.add(kj::mv(array));
    return allocations.back();
  }

  kj::Vector<kj::Array<word>> allocations;
};

TEST(Message, MessageBuilderInit) {
  MallocMessageBuilder builder(2048);
  initTestMessage(builder.getRoot<TestAllTypes>());

  // Pull the segments out and make a segment init table out of them.
  //
  // We const_cast for simplicity of implementing the test, but you shouldn't do that at home. :)
  auto segs = builder.getSegmentsForOutput();
  ASSERT_EQ(1, segs.size());

  auto segInits = KJ_MAP(seg, segs) -> MessageBuilder::SegmentInit {
    return { kj::arrayPtr(const_cast<word*>(seg.begin()), seg.size()), seg.size() };
  };

  // Init a new builder from the old segments.
  TestInitMessageBuilder builder2(segInits);
  checkTestMessage(builder2.getRoot<TestAllTypes>());

  // Verify that they're really using the same underlying memory.
  builder2.getRoot<TestAllTypes>().setInt64Field(123321);
  EXPECT_EQ(123321, builder.getRoot<TestAllTypes>().getInt64Field());

  // Force builder2 to allocate new space.
  EXPECT_EQ(0, builder2.allocations.size());
  builder2.getRoot<TestAllTypes>().setTextField("foobarbaz");
  EXPECT_EQ(1, builder2.allocations.size());
}

TEST(Message, MessageBuilderInitMultiSegment) {
  // Same as previous test, but with a message containing many segments.

  MallocMessageBuilder builder(1, AllocationStrategy::FIXED_SIZE);
  initTestMessage(builder.getRoot<TestAllTypes>());

  // Pull the segments out and make a segment init table out of them.
  //
  // We const_cast for simplicity of implementing the test, but you shouldn't do that at home. :)
  auto segs = builder.getSegmentsForOutput();
  ASSERT_NE(1, segs.size());

  auto segInits = KJ_MAP(seg, segs) -> MessageBuilder::SegmentInit {
    return { kj::arrayPtr(const_cast<word*>(seg.begin()), seg.size()), seg.size() };
  };

  // Init a new builder from the old segments.
  TestInitMessageBuilder builder2(segInits);
  checkTestMessage(builder2.getRoot<TestAllTypes>());

  // Verify that they're really using the same underlying memory.
  builder2.getRoot<TestAllTypes>().setInt64Field(123321);
  EXPECT_EQ(123321, builder.getRoot<TestAllTypes>().getInt64Field());

  // Force builder2 to allocate new space.
  EXPECT_EQ(0, builder2.allocations.size());
  builder2.getRoot<TestAllTypes>().setTextField("foobarbaz");
  EXPECT_EQ(1, builder2.allocations.size());
}

TEST(Message, MessageBuilderInitSpaceAvailable) {
  word buffer[2048];
  memset(buffer, 0, sizeof(buffer));
  MallocMessageBuilder builder(buffer);
  initTestMessage(builder.getRoot<TestAllTypes>());

  // Find out how much space in `buffer` was used in order to use in initializing the new message.
  auto segs = builder.getSegmentsForOutput();
  ASSERT_EQ(1, segs.size());
  KJ_ASSERT(segs[0].begin() == buffer);

  MessageBuilder::SegmentInit init = { kj::ArrayPtr<word>(buffer), segs[0].size() };

  // Init a new builder from the old segments.
  TestInitMessageBuilder builder2(kj::arrayPtr(init));
  checkTestMessage(builder2.getRoot<TestAllTypes>());

  // Verify that they're really using the same underlying memory.
  builder2.getRoot<TestAllTypes>().setInt64Field(123321);
  EXPECT_EQ(123321, builder.getRoot<TestAllTypes>().getInt64Field());

  // Ask builder2 to allocate new space. It should go into the free space at the end of the
  // segment.
  EXPECT_EQ(0, builder2.allocations.size());
  builder2.getRoot<TestAllTypes>().setTextField("foobarbaz");
  EXPECT_EQ(0, builder2.allocations.size());

  EXPECT_EQ(kj::implicitCast<void*>(buffer + segs[0].size()),
            kj::implicitCast<void*>(builder2.getRoot<TestAllTypes>().getTextField().begin()));
}

TEST(Message, ReadWriteDataStruct) {
  MallocMessageBuilder builder;
  auto root = builder.getRoot<TestAllTypes>();

  root.setUInt32Field(123);
  root.setFloat64Field(1.5);
  root.setTextField("foo");

  auto copy = readDataStruct<TestAllTypes>(writeDataStruct(root));
  EXPECT_EQ(123, copy.getUInt32Field());
  EXPECT_EQ(1.5, copy.getFloat64Field());
  EXPECT_FALSE(copy.hasTextField());

  checkTestMessageAllZero(readDataStruct<TestAllTypes>(nullptr));
  checkTestMessageAllZero(defaultValue<TestAllTypes>());
}

KJ_TEST("clone()") {
  MallocMessageBuilder builder(2048);
  initTestMessage(builder.getRoot<TestAllTypes>());

  auto copy = clone(builder.getRoot<TestAllTypes>().asReader());
  checkTestMessage(*copy);
}

#if !CAPNP_ALLOW_UNALIGNED
KJ_TEST("disallow unaligned") {
  union {
    char buffer[16];
    word align;
  };
  memset(buffer, 0, sizeof(buffer));

  auto unaligned = kj::arrayPtr(reinterpret_cast<word*>(buffer + 1), 1);

  kj::ArrayPtr<const word> segments[1] = {unaligned};
  SegmentArrayMessageReader message(segments);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("unaligned", message.getRoot<TestAllTypes>());
}
#endif

KJ_TEST("MessageBuilder::sizeInWords()") {
  capnp::MallocMessageBuilder builder;
  auto root = builder.initRoot<TestAllTypes>();
  initTestMessage(root);

  size_t expected = root.totalSize().wordCount + 1;

  KJ_EXPECT(builder.sizeInWords() == expected);

  auto segments = builder.getSegmentsForOutput();
  size_t total = 0;
  for (auto& segment: segments) {
    total += segment.size();
  }
  KJ_EXPECT(total == expected);

  capnp::SegmentArrayMessageReader reader(segments);
  checkTestMessage(reader.getRoot<TestAllTypes>());
  KJ_EXPECT(reader.sizeInWords() == expected);
}

// TODO(test):  More tests.

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
