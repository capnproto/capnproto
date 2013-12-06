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
#include "capability.h"
#include "arena.h"
#include <kj/debug.h>

namespace capnp {

namespace {

class BrokenCapFactoryImpl: public _::BrokenCapFactory {
public:
  kj::Own<ClientHook> newBrokenCap(kj::StringPtr description) override {
    return capnp::newBrokenCap(description);
  }
};

static BrokenCapFactoryImpl brokenCapFactory;

}  // namespace

CapReaderContext::CapReaderContext(kj::Array<kj::Own<ClientHook>>&& capTable)
    : capTable(kj::mv(capTable)) {}
CapReaderContext::~CapReaderContext() noexcept(false) {
  if (capTable == nullptr) {
    kj::dtor(arena());
  }
}

AnyPointer::Reader CapReaderContext::imbue(AnyPointer::Reader base) {
  KJ_IF_MAYBE(oldArena, base.reader.getArena()) {
    static_assert(sizeof(arena()) <= sizeof(arenaSpace),
                  "arenaSpace is too small.  Please increase it.");
    kj::ctor(arena(), oldArena, brokenCapFactory,
             kj::mv(KJ_REQUIRE_NONNULL(capTable, "imbue() can only be called once.")));
  } else {
    KJ_FAIL_REQUIRE("Cannot imbue unchecked message.");
  }
  capTable = nullptr;
  return AnyPointer::Reader(base.reader.imbue(arena()));
}

CapBuilderContext::CapBuilderContext() {}
CapBuilderContext::~CapBuilderContext() noexcept(false) {
  if (arenaAllocated) {
    kj::dtor(arena());
  }
}

AnyPointer::Builder CapBuilderContext::imbue(AnyPointer::Builder base) {
  KJ_REQUIRE(!arenaAllocated, "imbue() can only be called once.");
  static_assert(sizeof(arena()) <= sizeof(arenaSpace),
                "arenaSpace is too small.  Please increase it.");
  kj::ctor(arena(), base.builder.getArena(), brokenCapFactory);
  arenaAllocated = true;
  return AnyPointer::Builder(base.builder.imbue(arena()));
}

kj::ArrayPtr<kj::Own<ClientHook>> CapBuilderContext::getCapTable() {
  if (arenaAllocated) {
    return arena().getCapTable();
  } else {
    return nullptr;
  }
}

// =======================================================================================

namespace {

uint firstSegmentSize(kj::Maybe<MessageSize> sizeHint) {
  KJ_IF_MAYBE(s, sizeHint) {
    // 1 for the root pointer.  We don't store caps in the message so we don't count those here.
    return s->wordCount + 1;
  } else {
    return SUGGESTED_FIRST_SEGMENT_WORDS;
  }
}

}  // namespace

LocalMessage::LocalMessage(kj::Maybe<MessageSize> sizeHint)
    : message(firstSegmentSize(sizeHint)),
      root(capContext.imbue(message.getRoot<AnyPointer>())) {}

// =======================================================================================

namespace {

class BrokenPipeline final: public PipelineHook, public kj::Refcounted {
public:
  BrokenPipeline(const kj::Exception& exception): exception(exception) {}

  kj::Own<PipelineHook> addRef() override {
    return kj::addRef(*this);
  }

  kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) override;

private:
  kj::Exception exception;
};

class BrokenRequest final: public RequestHook {
public:
  BrokenRequest(const kj::Exception& exception, kj::Maybe<MessageSize> sizeHint)
      : exception(exception), message(sizeHint) {}

  RemotePromise<AnyPointer> send() override {
    return RemotePromise<AnyPointer>(kj::cp(exception),
        AnyPointer::Pipeline(kj::refcounted<BrokenPipeline>(exception)));
  }

  const void* getBrand() {
    return nullptr;
  }

  kj::Exception exception;
  LocalMessage message;
};

class BrokenClient final: public ClientHook, public kj::Refcounted {
public:
  BrokenClient(const kj::Exception& exception): exception(exception) {}
  BrokenClient(const kj::StringPtr description)
      : exception(kj::Exception::Nature::PRECONDITION, kj::Exception::Durability::PERMANENT,
                  "", 0, kj::str(description)) {}

  Request<AnyPointer, AnyPointer> newCall(
      uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint) override {
    auto hook = kj::heap<BrokenRequest>(exception, sizeHint);
    auto root = hook->message.getRoot();
    return Request<AnyPointer, AnyPointer>(root, kj::mv(hook));
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              kj::Own<CallContextHook>&& context) override {
    return VoidPromiseAndPipeline { kj::cp(exception), kj::heap<BrokenPipeline>(exception) };
  }

  kj::Maybe<ClientHook&> getResolved() {
    return nullptr;
  }

  kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
    return kj::Promise<kj::Own<ClientHook>>(kj::cp(exception));
  }

  kj::Own<ClientHook> addRef() override {
    return kj::addRef(*this);
  }

  const void* getBrand() override {
    return nullptr;
  }

private:
  kj::Exception exception;
};

kj::Own<ClientHook> BrokenPipeline::getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) {
  return kj::refcounted<BrokenClient>(exception);
}

}  // namespace

kj::Own<ClientHook> newBrokenCap(kj::StringPtr reason) {
  return kj::refcounted<BrokenClient>(reason);
}

kj::Own<ClientHook> newBrokenCap(kj::Exception&& reason) {
  return kj::refcounted<BrokenClient>(kj::mv(reason));
}

kj::Own<PipelineHook> newBrokenPipeline(kj::Exception&& reason) {
  return kj::refcounted<BrokenPipeline>(kj::mv(reason));
}

}  // namespace capnp
