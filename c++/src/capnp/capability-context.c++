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

#define CAPNP_PRIVATE

#include "capability-context.h"
#include "arena.h"
#include <kj/debug.h>

namespace capnp {

CapReaderContext::CapReaderContext(CapExtractorBase& extractor): extractor(&extractor) {}
CapReaderContext::~CapReaderContext() noexcept(false) {
  if (extractor == nullptr) {
    kj::dtor(arena());
  }
}

ObjectPointer::Reader CapReaderContext::imbue(ObjectPointer::Reader base) {
  KJ_REQUIRE(extractor != nullptr, "imbue() can only be called once.");
  KJ_IF_MAYBE(oldArena, base.reader.getArena()) {
    kj::ctor(arena(), oldArena, extractor);
  } else {
    KJ_FAIL_REQUIRE("Cannot imbue unchecked message.");
  }
  extractor = nullptr;
  return ObjectPointer::Reader(base.reader.imbue(arena()));
}

CapBuilderContext::CapBuilderContext(CapInjectorBase& injector): injector(&injector) {}
CapBuilderContext::~CapBuilderContext() noexcept(false) {
  if (injector == nullptr) {
    kj::dtor(arena());
  }
}

ObjectPointer::Builder CapBuilderContext::imbue(ObjectPointer::Builder base) {
  KJ_REQUIRE(injector != nullptr, "imbue() can only be called once.");
  kj::ctor(arena(), &base.builder.getArena(), injector);
  injector = nullptr;
  return ObjectPointer::Builder(base.builder.imbue(arena()));
}

}  // namespace capnp
