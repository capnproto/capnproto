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

    auto onRevoked = policy->onRevoked();

    bool reverse = this->reverse;  // for capture
    auto newPromise = promise.then(
        [reverse,policy=kj::mv(policy)](Response<AnyPointer>&& response) mutable {
      AnyPointer::Reader reader = response;
      auto newRespHook = kj::heap<MembraneResponseHook>(
          ResponseHook::from(kj::mv(response)), policy->addRef(), reverse);
      reader = newRespHook->imbue(reader);
      return Response<AnyPointer>(reader, kj::mv(newRespHook));
    });

    KJ_IF_MAYBE(r, kj::mv(onRevoked)) {
      newPromise = newPromise.exclusiveJoin(r->then([]() -> Response<AnyPointer> {
        KJ_FAIL_REQUIRE("onRevoked() promise resolved; it should only reject");
      }));
    }

    return RemotePromise<AnyPointer>(kj::mv(newPromise), kj::mv(newPipeline));
  }

  kj::Promise<void> sendStreaming() override {
    auto promise = inner->sendStreaming();

    KJ_IF_MAYBE(r, policy->onRevoked()) {
      promise = promise.exclusiveJoin(r->then([]() {
        KJ_FAIL_REQUIRE("onRevoked() promise resolved; it should only reject");
      }));
    }

    return promise;
  }

  AnyPointer::Pipeline sendForPipeline() override {
    return AnyPointer::Pipeline(kj::refcounted<MembranePipelineHook>(
        PipelineHook::from(inner->sendForPipeline()), policy->addRef(), reverse));
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
    // Note that releaseParams() is idempotent -- it can be called multiple times.
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

  void setPipeline(kj::Own<PipelineHook>&& pipeline) override {
    inner->setPipeline(kj::refcounted<MembranePipelineHook>(
        kj::mv(pipeline), policy->addRef(), !reverse));
  }

  kj::Promise<void> tailCall(kj::Own<RequestHook>&& request) override {
    return inner->tailCall(MembraneRequestHook::wrap(kj::mv(request), *policy, !reverse));
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

}  // namespace

class MembraneHook final: public ClientHook, public kj::Refcounted {
public:
  MembraneHook(kj::Own<ClientHook>&& inner, kj::Own<MembranePolicy>&& policyParam, bool reverse)
      : inner(kj::mv(inner)), policy(kj::mv(policyParam)), reverse(reverse) {
    KJ_IF_MAYBE(r, policy->onRevoked()) {
      revocationTask = r->eagerlyEvaluate([this](kj::Exception&& exception) {
        this->inner = newBrokenCap(kj::mv(exception));
      });
    }
  }

  ~MembraneHook() noexcept(false) {
    auto& map = reverse ? policy->reverseWrappers : policy->wrappers;
    map.erase(inner.get());
  }

  static kj::Own<ClientHook> wrap(ClientHook& cap, MembranePolicy& policy, bool reverse) {
    if (cap.getBrand() == MEMBRANE_BRAND) {
      auto& otherMembrane = kj::downcast<MembraneHook>(cap);
      auto& rootPolicy = policy.rootPolicy();
      if (&otherMembrane.policy->rootPolicy() == &rootPolicy &&
          otherMembrane.reverse == !reverse) {
        // Capability that passed across the membrane one way is now passing back the other way.
        // Unwrap it rather than double-wrap it.
        Capability::Client unwrapped(otherMembrane.inner->addRef());
        return ClientHook::from(
            reverse ? rootPolicy.importInternal(kj::mv(unwrapped), *otherMembrane.policy, policy)
                    : rootPolicy.exportExternal(kj::mv(unwrapped), *otherMembrane.policy, policy));
      }
    }

    auto& map = reverse ? policy.reverseWrappers : policy.wrappers;
    ClientHook*& slot = map.findOrCreate(&cap, [&]() -> kj::Decay<decltype(map)>::Entry {
      return { &cap, nullptr };
    });
    if (slot == nullptr) {
      auto result = ClientHook::from(
          reverse ? policy.importExternal(Capability::Client(cap.addRef()))
                  : policy.exportInternal(Capability::Client(cap.addRef())));
      slot = result;
      return result;
    } else {
      return slot->addRef();
    }
  }

  static kj::Own<ClientHook> wrap(kj::Own<ClientHook> cap, MembranePolicy& policy, bool reverse) {
    if (cap->getBrand() == MEMBRANE_BRAND) {
      auto& otherMembrane = kj::downcast<MembraneHook>(*cap);
      auto& rootPolicy = policy.rootPolicy();
      if (&otherMembrane.policy->rootPolicy() == &rootPolicy &&
          otherMembrane.reverse == !reverse) {
        // Capability that passed across the membrane one way is now passing back the other way.
        // Unwrap it rather than double-wrap it.
        Capability::Client unwrapped(otherMembrane.inner->addRef());
        return ClientHook::from(
            reverse ? rootPolicy.importInternal(kj::mv(unwrapped), *otherMembrane.policy, policy)
                    : rootPolicy.exportExternal(kj::mv(unwrapped), *otherMembrane.policy, policy));
      }
    }

    auto& map = reverse ? policy.reverseWrappers : policy.wrappers;
    ClientHook*& slot = map.findOrCreate(cap.get(), [&]() -> kj::Decay<decltype(map)>::Entry {
      return { cap.get(), nullptr };
    });
    if (slot == nullptr) {
      auto result = ClientHook::from(
          reverse ? policy.importExternal(Capability::Client(kj::mv(cap)))
                  : policy.exportInternal(Capability::Client(kj::mv(cap))));
      slot = result;
      return result;
    } else {
      return slot->addRef();
    }
  }

  Request<AnyPointer, AnyPointer> newCall(
      uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
      CallHints hints) override {
    KJ_IF_MAYBE(r, resolved) {
      return r->get()->newCall(interfaceId, methodId, sizeHint, hints);
    }

    auto redirect = reverse
        ? policy->outboundCall(interfaceId, methodId, Capability::Client(inner->addRef()))
        : policy->inboundCall(interfaceId, methodId, Capability::Client(inner->addRef()));
    KJ_IF_MAYBE(r, redirect) {
      if (policy->shouldResolveBeforeRedirecting()) {
        // The policy says that *if* this capability points into the membrane, then we want to
        // redirect the call. However, if this capability is a promise, then it could resolve to
        // something outside the membrane later. We have to wait before we actually redirect,
        // otherwise behavior will differ depending on whether the promise is resolved.
        KJ_IF_MAYBE(p, whenMoreResolved()) {
          return newLocalPromiseClient(p->attach(addRef()))
              ->newCall(interfaceId, methodId, sizeHint, hints);
        }
      }

      return ClientHook::from(kj::mv(*r))->newCall(interfaceId, methodId, sizeHint, hints);
    } else {
      // For pass-through calls, we don't worry about promises, because if the capability resolves
      // to something outside the membrane, then the call will pass back out of the membrane too.
      return MembraneRequestHook::wrap(
          inner->newCall(interfaceId, methodId, sizeHint, hints), *policy, reverse);
    }
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              kj::Own<CallContextHook>&& context,
                              CallHints hints) override {
    KJ_IF_MAYBE(r, resolved) {
      return r->get()->call(interfaceId, methodId, kj::mv(context), hints);
    }

    auto redirect = reverse
        ? policy->outboundCall(interfaceId, methodId, Capability::Client(inner->addRef()))
        : policy->inboundCall(interfaceId, methodId, Capability::Client(inner->addRef()));
    KJ_IF_MAYBE(r, redirect) {
      if (policy->shouldResolveBeforeRedirecting()) {
        // The policy says that *if* this capability points into the membrane, then we want to
        // redirect the call. However, if this capability is a promise, then it could resolve to
        // something outside the membrane later. We have to wait before we actually redirect,
        // otherwise behavior will differ depending on whether the promise is resolved.
        KJ_IF_MAYBE(p, whenMoreResolved()) {
          return newLocalPromiseClient(p->attach(addRef()))
              ->call(interfaceId, methodId, kj::mv(context), hints);
        }
      }

      return ClientHook::from(kj::mv(*r))->call(interfaceId, methodId, kj::mv(context), hints);
    } else {
      // !reverse because calls to the CallContext go in the opposite direction.
      auto result = inner->call(interfaceId, methodId,
          kj::refcounted<MembraneCallContextHook>(kj::mv(context), policy->addRef(), !reverse),
          hints);

      if (hints.onlyPromisePipeline) {
        // Just in case the called capability returned a valid promise, replace it here.
        result.promise = kj::NEVER_DONE;
      } else KJ_IF_MAYBE(r, policy->onRevoked()) {
        result.promise = result.promise.exclusiveJoin(kj::mv(*r));
      }

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
      KJ_IF_MAYBE(r, policy->onRevoked()) {
        *promise = promise->exclusiveJoin(r->then([]() -> kj::Own<ClientHook> {
          KJ_FAIL_REQUIRE("onRevoked() promise resolved; it should only reject");
        }));
      }

      return promise->then([this](kj::Own<ClientHook>&& newInner) {
        // There's a chance resolved was set by getResolved() or a concurrent whenMoreResolved()
        // while we yielded the event loop. If the inner ClientHook is maintaining the contract,
        // then resolved would already be set to newInner after wrapping in a MembraneHook.
        KJ_IF_MAYBE(r, resolved) {
          return (*r)->addRef();
        } else {
          return resolved.emplace(wrap(*newInner, *policy, reverse))->addRef();
        }
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

  kj::Maybe<int> getFd() override {
    KJ_IF_MAYBE(f, inner->getFd()) {
      if (policy->allowFdPassthrough()) {
        return *f;
      }
    }
    return nullptr;
  }

private:
  kj::Own<ClientHook> inner;
  kj::Own<MembranePolicy> policy;
  bool reverse;
  kj::Maybe<kj::Own<ClientHook>> resolved;
  kj::Promise<void> revocationTask = nullptr;
};

namespace {

kj::Own<ClientHook> membrane(kj::Own<ClientHook> inner, MembranePolicy& policy, bool reverse) {
  return MembraneHook::wrap(kj::mv(inner), policy, reverse);
}

}  // namespace

Capability::Client MembranePolicy::importExternal(Capability::Client external) {
  return Capability::Client(kj::refcounted<MembraneHook>(
      ClientHook::from(kj::mv(external)), addRef(), true));
}

Capability::Client MembranePolicy::exportInternal(Capability::Client internal) {
  return Capability::Client(kj::refcounted<MembraneHook>(
      ClientHook::from(kj::mv(internal)), addRef(), false));
}

Capability::Client MembranePolicy::importInternal(
    Capability::Client internal, MembranePolicy& exportPolicy, MembranePolicy& importPolicy) {
  return kj::mv(internal);
}

Capability::Client MembranePolicy::exportExternal(
    Capability::Client external, MembranePolicy& importPolicy, MembranePolicy& exportPolicy) {
  return kj::mv(external);
}

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

