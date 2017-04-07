// Copyright (c) 2017 Cloudflare, Inc. and contributors
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

#include "test-util.h"
#include <kj/main.h>
#include "serialize.h"
#include <capnp/test.capnp.h>
#include <unistd.h>

namespace capnp {
namespace _ {
namespace {

class AflTestMain {
public:
  explicit AflTestMain(kj::ProcessContext& context)
      : context(context) {}

  kj::MainFunc getMain() {
    return kj::MainBuilder(context, "(unknown version)",
        "American Fuzzy Lop test case. Pass input on stdin. Expects a binary "
        "message of type TestAllTypes.")
        .addOption({"lists"}, KJ_BIND_METHOD(*this, runLists),
            "Expect a message of type TestLists instead of TestAllTypes.")
        .addOption({"canonicalize"}, KJ_BIND_METHOD(*this, canonicalize),
            "Test canonicalization code.")
        .callAfterParsing(KJ_BIND_METHOD(*this, run))
        .build();
  }

  kj::MainBuilder::Validity run() {
    capnp::StreamFdMessageReader reader(STDIN_FILENO);
    KJ_IF_MAYBE(e, kj::runCatchingExceptions([&]() {
      checkTestMessage(reader.getRoot<TestAllTypes>());
    })) {
      KJ_LOG(ERROR, "threw");
    }
    KJ_IF_MAYBE(e, kj::runCatchingExceptions([&]() {
      checkDynamicTestMessage(reader.getRoot<DynamicStruct>(Schema::from<TestAllTypes>()));
    })) {
      KJ_LOG(ERROR, "dynamic threw");
    }
    KJ_IF_MAYBE(e, kj::runCatchingExceptions([&]() {
      kj::str(reader.getRoot<TestAllTypes>());
    })) {
      KJ_LOG(ERROR, "str threw");
    }
    return true;
  }

  kj::MainBuilder::Validity runLists() {
    capnp::StreamFdMessageReader reader(STDIN_FILENO);
    KJ_IF_MAYBE(e, kj::runCatchingExceptions([&]() {
      kj::str(reader.getRoot<test::TestLists>());
    })) {
      KJ_LOG(ERROR, "threw");
    }
    return true;
  }

  kj::MainBuilder::Validity canonicalize() {
    // (Test case contributed by David Renshaw.)

    kj::Array<capnp::word> canonical;
    bool equal = false;
    KJ_IF_MAYBE(e, kj::runCatchingExceptions([&]() {
      capnp::ReaderOptions options;

      // The default traversal limit of 8 * 1024 * 1024 causes
      // AFL to think that it has found "hang" bugs.
      options.traversalLimitInWords = 8 * 1024;

      capnp::StreamFdMessageReader message(0, options); // read from stdin
      TestAllTypes::Reader myStruct = message.getRoot<TestAllTypes>();
      canonical = capnp::canonicalize(myStruct);

      kj::ArrayPtr<const capnp::word> segments[1] = {canonical.asPtr()};
      capnp::SegmentArrayMessageReader reader(kj::arrayPtr(segments, 1));

      auto originalAny = message.getRoot<capnp::AnyPointer>();

      // Discard cases where the original message is null.
      KJ_ASSERT(!originalAny.isNull());

      equal = originalAny == reader.getRoot<capnp::AnyPointer>();
    })) {
      // Probably some kind of decoding exception.
      KJ_LOG(ERROR, "threw");
      context.exit();
    }

    KJ_ASSERT(equal);

    kj::ArrayPtr<const capnp::word> segments[1] = {canonical.asPtr()};
    capnp::SegmentArrayMessageReader reader(kj::arrayPtr(segments, 1));
    KJ_ASSERT(reader.isCanonical());

    kj::Array<capnp::word> canonical2;
    {
      capnp::ReaderOptions options;
      options.traversalLimitInWords = 8 * 1024;

      TestAllTypes::Reader myStruct = reader.getRoot<TestAllTypes>();
      canonical2 = capnp::canonicalize(myStruct);
    }

    KJ_ASSERT(canonical.size() == canonical2.size());
    auto b1 = canonical.asBytes();
    auto b2 = canonical2.asBytes();
    for (int idx = 0; idx < b1.size(); ++idx) {
      KJ_ASSERT(b1[idx] == b2[idx], idx, b1.size());
    }

    return true;
  }

private:
  kj::ProcessContext& context;
};

}  // namespace
}  // namespace _
}  // namespace capnp

KJ_MAIN(capnp::_::AflTestMain);
