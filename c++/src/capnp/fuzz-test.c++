// Copyright (c) 2015 Sandstorm Development Group, Inc. and contributors
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <capnp/test-import.capnp.h>
#include <capnp/test-import2.capnp.h>
#include "message.h"
#include "serialize.h"
#include <kj/test.h>
#include <stdlib.h>
#include <kj/miniposix.h>
#include "test-util.h"

namespace capnp {
namespace _ {  // private
namespace {

bool skipFuzzTest() {
  if (getenv("CAPNP_SKIP_FUZZ_TEST") != nullptr) {
    KJ_LOG(WARNING, "Skipping test because CAPNP_SKIP_FUZZ_TEST is set in environment.");
    return true;
  } else {
    return false;
  }
}

class DisableStackTraces: public kj::ExceptionCallback {
  // This test generates a lot of exceptions. Performing a backtrace on each one can be slow,
  // especially on Windows (where it is very, very slow). So, disable them.

public:
  StackTraceMode stackTraceMode() override {
    return StackTraceMode::NONE;
  }
};

uint64_t traverse(AnyPointer::Reader reader);
uint64_t traverse(AnyStruct::Reader reader);
uint64_t traverse(AnyList::Reader reader);

template <typename ListType>
uint64_t traverseList(ListType list) {
  // Traverse in reverse order in order to trigger segfaults before exceptions if we go
  // out-of-bounds.

  uint64_t result = 0;
  for (size_t i = list.size(); i != 0; i--) {
    result += traverse(list[i-1]);
  }
  return result;
}

uint64_t traverse(AnyStruct::Reader reader) {
  uint64_t result = 0;
  for (byte b: reader.getDataSection()) {
    result += b;
  }
  result += traverseList(reader.getPointerSection());
  return result;
}

uint64_t traverse(AnyList::Reader reader) {
  uint64_t result = 0;
  switch (reader.getElementSize()) {
    case ElementSize::VOID: break;
    case ElementSize::BIT:         for (auto e: reader.as<List<bool    >>()) result += e; break;
    case ElementSize::BYTE:        for (auto e: reader.as<List<uint8_t >>()) result += e; break;
    case ElementSize::TWO_BYTES:   for (auto e: reader.as<List<uint16_t>>()) result += e; break;
    case ElementSize::FOUR_BYTES:  for (auto e: reader.as<List<uint32_t>>()) result += e; break;
    case ElementSize::EIGHT_BYTES: for (auto e: reader.as<List<uint64_t>>()) result += e; break;
    case ElementSize::POINTER:
      traverseList(reader.as<List<AnyPointer>>());
      break;
    case ElementSize::INLINE_COMPOSITE:
      traverseList(reader.as<List<AnyStruct>>());
      break;
  }
  return result;
}

uint64_t traverse(AnyPointer::Reader reader) {
  if (reader.isStruct()) {
    return traverse(reader.getAs<AnyStruct>());
  } else if (reader.isList()) {
    return traverse(reader.getAs<AnyList>());
  } else {
    return 0;
  }
}

template <typename Checker>
void traverseCatchingExceptions(kj::ArrayPtr<const word> data) {
  // Try traversing through Checker.
  kj::runCatchingExceptions([&]() {
    FlatArrayMessageReader reader(data);
    KJ_ASSERT(Checker::check(reader) != 0) { break; }
  });

  // Try traversing through AnyPointer.
  kj::runCatchingExceptions([&]() {
    FlatArrayMessageReader reader(data);
    KJ_ASSERT(traverse(reader.getRoot<AnyPointer>()) != 0) { break; }
  });

  // Try counting the size..
  kj::runCatchingExceptions([&]() {
    FlatArrayMessageReader reader(data);
    KJ_ASSERT(reader.getRoot<AnyPointer>().targetSize().wordCount != 0) { break; }
  });

  // Try copying into a builder, and if that works, traversing it with Checker.
  static word buffer[8192];
  kj::runCatchingExceptions([&]() {
    FlatArrayMessageReader reader(data);
    MallocMessageBuilder copyBuilder(buffer);
    copyBuilder.setRoot(reader.getRoot<AnyPointer>());
    KJ_ASSERT(Checker::check(copyBuilder) != 0) { break; }
  });
}

template <typename Checker>
void fuzz(kj::ArrayPtr<word> data, uint flipCount, uint startAt, uint endAt) {
  if (flipCount == 0) {
    traverseCatchingExceptions<Checker>(data);
  } else {
    for (uint i = startAt; i < endAt; i++) {
      byte bit = 1u << (i % 8);
      byte old = data.asBytes()[i / 8];
      data.asBytes()[i / 8] |= bit;
      fuzz<Checker>(data, flipCount - 1, i + 1, endAt);
      data.asBytes()[i / 8] &= ~bit;
      fuzz<Checker>(data, flipCount - 1, i + 1, endAt);
      data.asBytes()[i / 8] = bit;
      fuzz<Checker>(data, flipCount - 1, i + 1, endAt);
      data.asBytes()[i / 8] = old;
    }
  }
}

struct StructChecker {
  template <typename ReaderOrBuilder>
  static uint check(ReaderOrBuilder& message) {
    uint result = 0;
    for (auto c: message.template getRoot<TestAllTypes>().getTextField()) {
      result += c;
    }
    return result;
  }
};

KJ_TEST("fuzz-test struct pointer") {
  if (skipFuzzTest()) return;
  DisableStackTraces disableStackTraces;

  MallocMessageBuilder builder;
  builder.getRoot<TestAllTypes>().setTextField("foo");
  KJ_ASSERT(builder.getSegmentsForOutput().size() == 1);
  fuzz<StructChecker>(messageToFlatArray(builder), 2, 64, 192);
}

struct ListChecker {
  template <typename ReaderOrBuilder>
  static uint check(ReaderOrBuilder& message) {
    uint result = 0;
    for (auto e: message.template getRoot<List<uint32_t>>()) {
      result += e;
    }
    return result;
  }
};

KJ_TEST("fuzz-test list pointer") {
  if (skipFuzzTest()) return;
  DisableStackTraces disableStackTraces;

  MallocMessageBuilder builder;
  auto list = builder.getRoot<AnyPointer>().initAs<List<uint32_t>>(2);
  list.set(0, 12345);
  list.set(1, 67890);
  fuzz<ListChecker>(messageToFlatArray(builder), 2, 64, 192);
}

struct StructListChecker {
  template <typename ReaderOrBuilder>
  static uint check(ReaderOrBuilder& message) {
    uint result = 0;
    auto l = message.template getRoot<List<TestAllTypes>>();
    for (size_t i = l.size(); i > 0; i--) {
      for (auto c: l[i-1].getTextField()) {
        result += c;
      }
    }
    return result;
  }
};

KJ_TEST("fuzz-test struct list pointer") {
  if (skipFuzzTest()) return;
  DisableStackTraces disableStackTraces;

  MallocMessageBuilder builder;
  auto list = builder.getRoot<AnyPointer>().initAs<List<test::TestAllTypes>>(2);
  list[0].setTextField("foo");
  list[1].setTextField("bar");
  KJ_ASSERT(builder.getSegmentsForOutput().size() == 1);

  fuzz<StructListChecker>(messageToFlatArray(builder), 2, 64, 192);
}

struct TextChecker {
  template <typename ReaderOrBuilder>
  static uint check(ReaderOrBuilder& message) {
    uint result = 0;
    for (auto c: message.template getRoot<Text>()) {
      result += c;
    }
    return result;
  }
};

KJ_TEST("fuzz-test text pointer") {
  if (skipFuzzTest()) return;
  DisableStackTraces disableStackTraces;

  MallocMessageBuilder builder;
  builder.template getRoot<AnyPointer>().setAs<Text>("foo");
  fuzz<TextChecker>(messageToFlatArray(builder), 2, 64, 192);
}

KJ_TEST("fuzz-test far pointer") {
  if (skipFuzzTest()) return;
  DisableStackTraces disableStackTraces;

  MallocMessageBuilder builder(1, AllocationStrategy::FIXED_SIZE);
  initTestMessage(builder.getRoot<TestAllTypes>());

  uint segmentCount = builder.getSegmentsForOutput().size();
  uint tableSize = segmentCount / 2 + 1;

  // Fuzz the root far pointer plus its landing pad, which should be in the next word.
  fuzz<StructChecker>(messageToFlatArray(builder), 2, tableSize * 64, tableSize * 64 + 128);
}

KJ_TEST("fuzz-test double-far pointer") {
  if (skipFuzzTest()) return;
  DisableStackTraces disableStackTraces;

  MallocMessageBuilder builder(1, AllocationStrategy::FIXED_SIZE);

  // Carefully arrange for a double-far pointer to be created.
  auto root = builder.getRoot<AnyPointer>();
  root.adopt(builder.getOrphanage().newOrphanCopy(Text::Reader("foo")));

  // Verify that did what we expected.
  KJ_ASSERT(builder.getSegmentsForOutput().size() == 3);
  KJ_ASSERT(builder.getSegmentsForOutput()[0].size() == 1);  // root pointer
  KJ_ASSERT(builder.getSegmentsForOutput()[1].size() == 1);  // "foo"
  KJ_ASSERT(builder.getSegmentsForOutput()[2].size() == 2);  // double-far landing pad

  // Fuzz the root far pointer.
  fuzz<TextChecker>(messageToFlatArray(builder), 2, 64, 128);

  // Fuzz the landing pad.
  fuzz<TextChecker>(messageToFlatArray(builder), 2, 192, 320);
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
