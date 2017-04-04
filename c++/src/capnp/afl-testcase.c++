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
    context.exit();
  }

  kj::MainBuilder::Validity runLists() {
    capnp::StreamFdMessageReader reader(STDIN_FILENO);
    KJ_IF_MAYBE(e, kj::runCatchingExceptions([&]() {
      kj::str(reader.getRoot<test::TestLists>());
    })) {
      KJ_LOG(ERROR, "threw");
    }
    context.exit();
  }

private:
  kj::ProcessContext& context;
};

}  // namespace
}  // namespace _
}  // namespace capnp

KJ_MAIN(capnp::_::AflTestMain);
