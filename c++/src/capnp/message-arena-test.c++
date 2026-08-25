// Copyright (c) 2026 Cap'n Proto contributors
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
#include <kj/test.h>
#include <string.h>

namespace capnp {
namespace {

class ArenaTestMessageReader final: public MessageReader {
public:
  ArenaTestMessageReader(): MessageReader(ReaderOptions()) {}

  kj::ArrayPtr<const word> getSegment(uint id) override {
    return id == 0 ? kj::arrayPtr(segment) : nullptr;
  }

private:
  word segment[1] = {};
};

class ArenaTestMessageBuilder final: public MessageBuilder {
public:
  kj::ArrayPtr<word> allocateSegment(uint minimumSize) override {
    KJ_REQUIRE(minimumSize <= kj::size(segment));
    return kj::arrayPtr(segment);
  }

private:
  word segment[8] = {};
};

template <typename T, typename Func>
void withPoisonedStorage(Func&& func) {
  alignas(T) byte storage[sizeof(T)];
  memset(storage, 0xff, sizeof(storage));

  T* object = reinterpret_cast<T*>(storage);
  kj::ctor(*object);
  func(*object);
  kj::dtor(*object);
}

KJ_TEST("message arenas initialize objects constructed in arenaSpace") {
  // MessageReader and MessageBuilder intentionally leave arenaSpace uninitialized until an arena
  // is needed. Poison the storage first so that this test catches arena members which placement
  // construction fails to initialize, including ArrayPtr's counter when it is enabled.
  withPoisonedStorage<ArenaTestMessageReader>([](auto& reader) {
    KJ_EXPECT(reader.isCanonical());
  });
  withPoisonedStorage<ArenaTestMessageBuilder>([](auto& builder) {
    KJ_EXPECT(builder.isCanonical());
    KJ_EXPECT(builder.getSegmentsForOutput().size() == 1);
  });
}

}  // namespace
}  // namespace capnp
