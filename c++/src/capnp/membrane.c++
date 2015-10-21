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

#include "membrane.h"
#include <kj/debug.h>

namespace capnp {

namespace {

static const char DUMMY = 0;
static constexpr const void* MEMBRANE_BRAND = &DUMMY;

kj::Own<ClientHook> membrane(kj::Own<ClientHook> inner, MembranePolicy& policy, bool reverse);

class MembraneCapTableReader final: public _::CapTableReader {
public:
  MembraneCapTableReader(MembranePolicy& policy, bool reverse)
      : policy(policy), reverse(reverse) {}

  AnyPointer::Reader imbue(AnyPointer::Reader reader) {
    return AnyPointer::Reader(imbue(
        _::PointerHelpers<AnyPointer>::getInternalReader(kj::mv(reader))));
  }

  _::PointerReader imbue(_::PointerReader reader) {
    KJ_REQUIRE(inner == nullptr, "can only call this once");
    inner = reader.getCapTable();
    return reader.imbue(this);
  }

  _::StructReader imbue(_::StructReader reader) {
    KJ_REQUIRE(inner == nullptr, "can only call this once");
    inner = reader.getCapTable();
    return reader.imbue(this);
  }

  _::ListReader imbue(_::ListReader reader) {
    KJ_REQUIRE(inner == nullptr, "can only call this once");
    inner = reader.getCapTable();
    return reader.imbue(this);
  }

  kj::Maybe<kj::Own<ClientHook>> extractCap(uint index) override {
    // The underlying message is inside the membrane, and we're pulling a cap out of it. Therefore,
    // we want to wrap the extracted capability in the membrane.
    return inner->extractCap(index).map([this](kj::Own<ClientHook>&& cap) {
      return membrane(kj::mv(cap), policy, reverse);
    });
  }

private:
  _::CapTableReader* inner = nullptr;
  MembranePolicy& policy;
  bool reverse;
};

class MembraneCapTableBuilder final: public _::CapTableBuilder {
public:
  MembraneCapTableBuilder(MembranePolicy& policy, bool reverse)
      : policy(policy), reverse(reverse) {}

  AnyPointer::Builder imbue(AnyPointer::Builder builder) {
    KJ_REQUIRE(inner == nullptr, "can only call this once");
    auto pointerBuilder = _::PointerHelpers<AnyPointer>::getInternalBuilder(kj::mv(builder));
    inner = pointerBuilder.getCapTable();
    return AnyPointer::Builder(pointerBuilder.imbue(this));
  }

  AnyPointer::Builder unimbue(AnyPointer::Builder builder) {
    auto pointerBuilder = _::PointerHelpers<AnyPointer>::getInternalBuilder(kj::mv(builder));
    KJ_REQUIRE(pointerBuilder.getCapTable() == this);
    return AnyPointer::Builder(pointerBuilder.imbue(inner));
  }

  kj::Maybe<kj::Own<ClientHook>> extractCap(uint index) override {
    // The underlying message is inside the membrane, and we're pulling a cap out of it. Therefore,
    // we want to wrap the extracted capability in the membrane.
    return inner->extractCap(index).map([this](kj::Own<ClientHook>&& cap) {
      return membrane(kj::mv(cap), policy, reverse);
    });
  }

  uint injectCap(kj::Own<ClientHook>&& cap) override {
    // The underlying message is inside the membrane, and we're inserting a cap from outside into
    // it. Therefore we want to add a reverse membrane.
    return inner->injectCap(membrane(kj::mv(cap), policy, !reverse));
  }

  void dropCap(uint index) override {
    inner->dropCap(index);
  }

private:
  _::CapTableBuilder* inner = nullptr;
  MembranePolicy& policy;
  bool reverse;
};

class MembranePipelineHook final: public PipelineHook, public kj::Refcounted {
public:
  MembranePipelineHook(
      kj::Own<PipelineHook>&& inner, kj::Own<MembranePolicy>&& policy, bool reverse)
      : inner(kj::mv(inner)), policy(kj::mv(policy)), reverse(reverse) {}

  kj::Own<PipelineHook> addRef() override {
    return kj::addRef(*this);
  }

  kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) override {
    return membrane(inner->getPipelinedCap(ops), *policy, reverse);
  }

  kj::Own<ClientHook> getPipelinedCap(kj::Array<PipelineOp>&& ops) override {
    return membrane(inner->getPipelinedCap(kj::mv(ops)), *policy, reverse);
  }

private:
  kj::Own<PipelineHook> inner;
  kj::Own<MembranePolicy> policy;
  bool reverse;
};

class MembraneResponseHook final: public ResponseHook {
public:
  MembraneResponseHook(
      kj::Own<ResponseHook>&& inner, kj::Own<MembranePolicy>&& policy, bool reverse)
      : inner(kj::mv(inner)), policy(kj::mv(policy)), capTable(*this->policy, reverse) {}

  AnyPointer::Reader imbue(AnyPointer::Reader reader) { return capTable.imbue(reader); }

private:
  kj::Own<ResponseHook> inner;
  kj::Own<MembranePolicy> policy;
  MembraneCapTableReader capTable;
};

class MembraneRequestHook final: public RequestHook {
public:
  MembraneRequestHook(kj::Own<RequestHook>&& inner, kj::Own<MembranePolicy>&& policy, bool reverse)
      : inner(kj::mv(inner)), policy(kj::mv(policy)),
        reverse(reverse), capTable(*this->policy, reverse) {}

  static Request<AnyPointer, AnyPointer> wrap(
      Request<AnyPointer, AnyPointer>&& inner, MembranePolicy& policy, bool reverse) {
    AnyPointer::Builder builder = inner;
    auto innerHook = RequestHook::from(kj::mv(inner));
    if (innerHook->getBrand() == MEMBRANE_BRAND) {
      auto& otherMembrane = kj::downcast<MembraneRequestHook>(*innerHook);
      if (otherMembrane.policy.get() == &policy && otherMembrane.reverse == !reverse) {
        // Request that passed across the membrane one way is now passing back the other way.
        // Unwrap it rather than double-wrap it.
        builder = otherMembrane.capTable.unimbue(builder);
        return { builder, kj::mv(otherMembrane.inner) };
      }
    }

    auto newHook = kj::heap<MembraneRequestHook>(kj::mv(innerHook), policy.addRef(), reverse);
    builder = newHook->capTable.imbue(builder);
    return { builder, kj::mv(newHook) };
  }

  static kj::Own<RequestHook> wrap(
      kj::Own<RequestHook>&& inner, MembranePolicy& policy, bool reverse) {
    if (inner->getBrand() == MEMBRANE_BRAND) {
      auto& otherMembrane = kj::downcast<MembraneRequestHook>(*inner);
      if (otherMembrane.policy.get() == &policy && otherMembrane.reverse == !reverse) {
        // Request that passed across the membrane one way is now passing back the other way.
        // Unwrap it rather than double-wrap it.
        return kj::mv(otherMembrane.inner);
      }
    }

    return kj::heap<MembraneRequestHook>(kj::mv(inner), policy.addRef(), reverse);
  }

  RemotePromise<AnyPointer> send() override {
    auto promise = inner->send();

    auto newPipeline = AnyPointer::Pipeline(kj::refcounted<MembranePipelineHook>(
        PipelineHook::from(kj::mv(promise)), policy->addRef(), reverse));

    bool reverse = this->reverse;  // for capture
    auto newPromise = promise.then(kj::mvCapture(policy,
        [reverse](kj::Own<MembranePolicy>&& policy, Response<AnyPointer>&& response) {
      AnyPointer::Reader reader = response;
      auto newRespHook = kj::heap<MembraneResponseHook>(
          ResponseHook::from(kj::mv(response)), policy->addRef(), reverse);
      reader = newRespHook->imbue(reader);
      return Response<AnyPointer>(reader, kj::mv(newRespHook));
    }));

    return RemotePromise<AnyPointer>(kj::mv(newPromise), kj::mv(newPipeline));
  }

  const void* getBrand() override {
    return MEMBRANE_BRAND;
  }

private:
  kj::Own<RequestHook> inner;
  kj::Own<MembranePolicy> policy;
  bool reverse;
  MembraneCapTableBuilder capTable;
};

class MembraneCallContextHook final: public CallContextHook, public kj::Refcounted {
public:
  MembraneCallContextHook(kj::Own<CallContextHook>&& inner,
                          kj::Own<MembranePolicy>&& policy, bool reverse)
      : inner(kj::mv(inner)), policy(kj::mv(policy)), reverse(reverse),
        paramsCapTable(*this->policy, reverse),
        resultsCapTable(*this->policy, reverse) {}

  AnyPointer::Reader getParams() override {
    KJ_REQUIRE(!releasedParams);
    KJ_IF_MAYBE(p, params) {
      return *p;
    } else {
      auto result = paramsCapTable.imbue(inner->getParams());
      params = result;
      return result;
    }
  }

  void releaseParams() override {
    KJ_REQUIRE(!releasedParams);
    releasedParams = true;
    inner->releaseParams();
  }

  AnyPointer::Builder getResults(kj::Maybe<MessageSize> sizeHint) override {
    KJ_IF_MAYBE(r, results) {
      return *r;
    } else {
      auto result = resultsCapTable.imbue(inner->getResults(sizeHint));
      results = result;
      return result;
    }
  }

  kj::Promise<void> tailCall(kj::Own<RequestHook>&& request) override {
    return inner->tailCall(MembraneRequestHook::wrap(kj::mv(request), *policy, !reverse));
  }

  void allowCancellation() override {
    inner->allowCancellation();
  }

  kj::Promise<AnyPointer::Pipeline> onTailCall() override {
    return inner->onTailCall().then([this](AnyPointer::Pipeline&& innerPipeline) {
      return AnyPointer::Pipeline(kj::refcounted<MembranePipelineHook>(
          PipelineHook::from(kj::mv(innerPipeline)), policy->addRef(), reverse));
    });
  }

  ClientHook::VoidPromiseAndPipeline directTailCall(kj::Own<RequestHook>&& request) override {
    auto pair = inner->directTailCall(
        MembraneRequestHook::wrap(kj::mv(request), *policy, !reverse));

    return {
      kj::mv(pair.promise),
      kj::refcounted<MembranePipelineHook>(kj::mv(pair.pipeline), policy->addRef(), reverse)
    };
  }

  kj::Own<CallContextHook> addRef() override {
    return kj::addRef(*this);
  }

private:
  kj::Own<CallContextHook> inner;
  kj::Own<MembranePolicy> policy;
  bool reverse;

  MembraneCapTableReader paramsCapTable;
  kj::Maybe<AnyPointer::Reader> params;
  bool releasedParams = false;

  MembraneCapTableBuilder resultsCapTable;
  kj::Maybe<AnyPointer::Builder> results;
};

class MembraneHook final: public ClientHook, public kj::Refcounted {
public:
  MembraneHook(kj::Own<ClientHook>&& inner, kj::Own<MembranePolicy>&& policy, bool reverse)
      : inner(kj::mv(inner)), policy(kj::mv(policy)), reverse(reverse) {}

  static kj::Own<ClientHook> wrap(ClientHook& cap, MembranePolicy& policy, bool reverse) {
    if (cap.getBrand() == MEMBRANE_BRAND) {
      auto& otherMembrane = kj::downcast<MembraneHook>(cap);
      if (otherMembrane.policy.get() == &policy && otherMembrane.reverse == !reverse) {
        // Capability that passed across the membrane one way is now passing back the other way.
        // Unwrap it rather than double-wrap it.
        return otherMembrane.inner->addRef();
      }
    }

    return kj::refcounted<MembraneHook>(cap.addRef(), policy.addRef(), reverse);
  }

  static kj::Own<ClientHook> wrap(kj::Own<ClientHook> cap, MembranePolicy& policy, bool reverse) {
    if (cap->getBrand() == MEMBRANE_BRAND) {
      auto& otherMembrane = kj::downcast<MembraneHook>(*cap);
      if (otherMembrane.policy.get() == &policy && otherMembrane.reverse == !reverse) {
        // Capability that passed across the membrane one way is now passing back the other way.
        // Unwrap it rather than double-wrap it.
        return otherMembrane.inner->addRef();
      }
    }

    return kj::refcounted<MembraneHook>(kj::mv(cap), policy.addRef(), reverse);
  }

  Request<AnyPointer, AnyPointer> newCall(
      uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint) override {
    KJ_IF_MAYBE(r, resolved) {
      return r->get()->newCall(interfaceId, methodId, sizeHint);
    }

    auto redirect = reverse
        ? policy->outboundCall(interfaceId, methodId, Capability::Client(inner->addRef()))
        : policy->inboundCall(interfaceId, methodId, Capability::Client(inner->addRef()));
    KJ_IF_MAYBE(r, redirect) {
      // The policy says that *if* this capability points into the membrane, then we want to
      // redirect the call. However, if this capability is a promise, then it could resolve to
      // something outside the membrane later. We have to wait before we actually redirect,
      // otherwise behavior will differ depending on whether the promise is resolved.
      KJ_IF_MAYBE(p, whenMoreResolved()) {
        return newLocalPromiseClient(kj::mv(*p))->newCall(interfaceId, methodId, sizeHint);
      }

      return ClientHook::from(kj::mv(*r))->newCall(interfaceId, methodId, sizeHint);
    } else {
      // For pass-through calls, we don't worry about promises, because if the capability resolves
      // to something outside the membrane, then the call will pass back out of the membrane too.
      return MembraneRequestHook::wrap(
          inner->newCall(interfaceId, methodId, sizeHint), *policy, reverse);
    }
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              kj::Own<CallContextHook>&& context) override {
    KJ_IF_MAYBE(r, resolved) {
      return r->get()->call(interfaceId, methodId, kj::mv(context));
    }

    auto redirect = reverse
        ? policy->outboundCall(interfaceId, methodId, Capability::Client(inner->addRef()))
        : policy->inboundCall(interfaceId, methodId, Capability::Client(inner->addRef()));
    KJ_IF_MAYBE(r, redirect) {
      // The policy says that *if* this capability points into the membrane, then we want to
      // redirect the call. However, if this capability is a promise, then it could resolve to
      // something outside the membrane later. We have to wait before we actually redirect,
      // otherwise behavior will differ depending on whether the promise is resolved.
      KJ_IF_MAYBE(p, whenMoreResolved()) {
        return newLocalPromiseClient(kj::mv(*p))->call(interfaceId, methodId, kj::mv(context));
      }

      return ClientHook::from(kj::mv(*r))->call(interfaceId, methodId, kj::mv(context));
    } else {
      // !reverse because calls to the CallContext go in the opposite direction.
      auto result = inner->call(interfaceId, methodId,
          kj::refcounted<MembraneCallContextHook>(kj::mv(context), policy->addRef(), !reverse));

      return {
        kj::mv(result.promise),
        kj::refcounted<MembranePipelineHook>(kj::mv(result.pipeline), policy->addRef(), reverse)
      };
    }
  }

  kj::Maybe<ClientHook&> getResolved() override {
    KJ_IF_MAYBE(r, resolved) {
      return **r;
    }

    KJ_IF_MAYBE(newInner, inner->getResolved()) {
      kj::Own<ClientHook> newResolved = wrap(*newInner, *policy, reverse);
      ClientHook& result = *newResolved;
      resolved = kj::mv(newResolved);
      return result;
    } else {
      return nullptr;
    }
  }

  kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
    KJ_IF_MAYBE(r, resolved) {
      return kj::Promise<kj::Own<ClientHook>>(r->get()->addRef());
    }

    KJ_IF_MAYBE(promise, inner->whenMoreResolved()) {
      return promise->then([this](kj::Own<ClientHook>&& newInner) {
        kj::Own<ClientHook> newResolved = wrap(*newInner, *policy, reverse);
        if (resolved == nullptr) {
          resolved = newResolved->addRef();
        }
        return newResolved;
      });
    } else {
      return nullptr;
    }
  }

  kj::Own<ClientHook> addRef() override {
    return kj::addRef(*this);
  }

  const void* getBrand() override {
    return MEMBRANE_BRAND;
  }

private:
  kj::Own<ClientHook> inner;
  kj::Own<MembranePolicy> policy;
  bool reverse;
  kj::Maybe<kj::Own<ClientHook>> resolved;
};

kj::Own<ClientHook> membrane(kj::Own<ClientHook> inner, MembranePolicy& policy, bool reverse) {
  return MembraneHook::wrap(kj::mv(inner), policy, reverse);
}

}  // namespace

Capability::Client membrane(Capability::Client inner, kj::Own<MembranePolicy> policy) {
  return Capability::Client(membrane(
      ClientHook::from(kj::mv(inner)), *policy, false));
}

Capability::Client reverseMembrane(Capability::Client inner, kj::Own<MembranePolicy> policy) {
  return Capability::Client(membrane(
      ClientHook::from(kj::mv(inner)), *policy, true));
}

namespace _ {  // private

_::OrphanBuilder copyOutOfMembrane(PointerReader from, Orphanage to,
                                   kj::Own<MembranePolicy> policy, bool reverse) {
  MembraneCapTableReader capTable(*policy, reverse);
  return _::OrphanBuilder::copy(
      OrphanageInternal::getArena(to),
      OrphanageInternal::getCapTable(to),
      capTable.imbue(from));
}

_::OrphanBuilder copyOutOfMembrane(StructReader from, Orphanage to,
                                   kj::Own<MembranePolicy> policy, bool reverse) {
  MembraneCapTableReader capTable(*policy, reverse);
  return _::OrphanBuilder::copy(
      OrphanageInternal::getArena(to),
      OrphanageInternal::getCapTable(to),
      capTable.imbue(from));
}

_::OrphanBuilder copyOutOfMembrane(ListReader from, Orphanage to,
                                   kj::Own<MembranePolicy> policy, bool reverse) {
  MembraneCapTableReader capTable(*policy, reverse);
  return _::OrphanBuilder::copy(
      OrphanageInternal::getArena(to),
      OrphanageInternal::getCapTable(to),
      capTable.imbue(from));
}

}  // namespace _ (private)

}  // namespace capnp

