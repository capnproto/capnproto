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

#define CAPNP_PRIVATE

#include "capability.h"
#include "message.h"
#include "arena.h"
#include <kj/refcount.h>
#include <kj/debug.h>
#include <kj/vector.h>
#include <map>
#include "generated-header-support.h"

namespace capnp {

namespace _ {

void setGlobalBrokenCapFactoryForLayoutCpp(BrokenCapFactory& factory);
// Defined in layout.c++.

}  // namespace _

namespace {

static kj::Own<ClientHook> newNullCap();

class BrokenCapFactoryImpl: public _::BrokenCapFactory {
public:
  kj::Own<ClientHook> newBrokenCap(kj::StringPtr description) override {
    return capnp::newBrokenCap(description);
  }
  kj::Own<ClientHook> newNullCap() override {
    return capnp::newNullCap();
  }
};

static BrokenCapFactoryImpl brokenCapFactory;

}  // namespace

ClientHook::ClientHook() {
  setGlobalBrokenCapFactoryForLayoutCpp(brokenCapFactory);
}

// =======================================================================================

Capability::Client::Client(decltype(nullptr))
    : hook(newNullCap()) {}

Capability::Client::Client(kj::Exception&& exception)
    : hook(newBrokenCap(kj::mv(exception))) {}

kj::Promise<kj::Maybe<int>> Capability::Client::getFd() {
  auto fd = hook->getFd();
  if (fd != nullptr) {
    return fd;
  } else KJ_IF_MAYBE(promise, hook->whenMoreResolved()) {
    return promise->attach(hook->addRef()).then([](kj::Own<ClientHook> newHook) {
      return Client(kj::mv(newHook)).getFd();
    });
  } else {
    return kj::Maybe<int>(nullptr);
  }
}

kj::Maybe<kj::Promise<Capability::Client>> Capability::Server::shortenPath() {
  return nullptr;
}

Capability::Server::DispatchCallResult Capability::Server::internalUnimplemented(
    const char* actualInterfaceName, uint64_t requestedTypeId) {
  return {
    KJ_EXCEPTION(UNIMPLEMENTED, "Requested interface not implemented.",
                 actualInterfaceName, requestedTypeId),
    false, true
  };
}

Capability::Server::DispatchCallResult Capability::Server::internalUnimplemented(
    const char* interfaceName, uint64_t typeId, uint16_t methodId) {
  return {
    KJ_EXCEPTION(UNIMPLEMENTED, "Method not implemented.", interfaceName, typeId, methodId),
    false, true
  };
}

kj::Promise<void> Capability::Server::internalUnimplemented(
    const char* interfaceName, const char* methodName, uint64_t typeId, uint16_t methodId) {
  return KJ_EXCEPTION(UNIMPLEMENTED, "Method not implemented.", interfaceName,
                      typeId, methodName, methodId);
}

ResponseHook::~ResponseHook() noexcept(false) {}

kj::Promise<void> ClientHook::whenResolved() {
  KJ_IF_MAYBE(promise, whenMoreResolved()) {
    return promise->then([](kj::Own<ClientHook>&& resolution) {
      return resolution->whenResolved();
    });
  } else {
    return kj::READY_NOW;
  }
}

kj::Promise<void> Capability::Client::whenResolved() {
  return hook->whenResolved().attach(hook->addRef());
}

// =======================================================================================

static inline uint firstSegmentSize(kj::Maybe<MessageSize> sizeHint) {
  KJ_IF_MAYBE(s, sizeHint) {
    return s->wordCount;
  } else {
    return SUGGESTED_FIRST_SEGMENT_WORDS;
  }
}

class LocalResponse final: public ResponseHook {
public:
  LocalResponse(kj::Maybe<MessageSize> sizeHint)
      : message(firstSegmentSize(sizeHint)) {}

  MallocMessageBuilder message;
};

class LocalCallContext final: public CallContextHook, public ResponseHook, public kj::Refcounted {
public:
  LocalCallContext(kj::Own<MallocMessageBuilder>&& request, kj::Own<ClientHook> clientRef,
                   ClientHook::CallHints hints, bool isStreaming)
      : request(kj::mv(request)), clientRef(kj::mv(clientRef)), hints(hints),
        isStreaming(isStreaming) {}

  AnyPointer::Reader getParams() override {
    KJ_IF_MAYBE(r, request) {
      return r->get()->getRoot<AnyPointer>();
    } else {
      KJ_FAIL_REQUIRE("Can't call getParams() after releaseParams().");
    }
  }
  void releaseParams() override {
    request = nullptr;
  }
  AnyPointer::Builder getResults(kj::Maybe<MessageSize> sizeHint) override {
    if (response == nullptr) {
      auto localResponse = kj::heap<LocalResponse>(sizeHint);
      responseBuilder = localResponse->message.getRoot<AnyPointer>();
      response = Response<AnyPointer>(responseBuilder.asReader(), kj::mv(localResponse));
    }
    return responseBuilder;
  }
  void setPipeline(kj::Own<PipelineHook>&& pipeline) override {
    KJ_IF_MAYBE(f, tailCallPipelineFulfiller) {
      f->get()->fulfill(AnyPointer::Pipeline(kj::mv(pipeline)));
    }
  }
  kj::Promise<void> tailCall(kj::Own<RequestHook>&& request) override {
    auto result = directTailCall(kj::mv(request));
    KJ_IF_MAYBE(f, tailCallPipelineFulfiller) {
      f->get()->fulfill(AnyPointer::Pipeline(kj::mv(result.pipeline)));
    }
    return kj::mv(result.promise);
  }
  ClientHook::VoidPromiseAndPipeline directTailCall(kj::Own<RequestHook>&& request) override {
    KJ_REQUIRE(response == nullptr, "Can't call tailCall() after initializing the results struct.");

    if (hints.onlyPromisePipeline) {
      return {
        kj::NEVER_DONE,
        PipelineHook::from(request->sendForPipeline())
      };
    }

    if (isStreaming) {
      auto promise = request->sendStreaming();
      return { kj::mv(promise), getDisabledPipeline() };
    } else {
      auto promise = request->send();

      auto voidPromise = promise.then([this](Response<AnyPointer>&& tailResponse) {
        response = kj::mv(tailResponse);
      });

      return { kj::mv(voidPromise), PipelineHook::from(kj::mv(promise)) };
    }
  }
  kj::Promise<AnyPointer::Pipeline> onTailCall() override {
    auto paf = kj::newPromiseAndFulfiller<AnyPointer::Pipeline>();
    tailCallPipelineFulfiller = kj::mv(paf.fulfiller);
    return kj::mv(paf.promise);
  }
  kj::Own<CallContextHook> addRef() override {
    return kj::addRef(*this);
  }

  kj::Maybe<kj::Own<MallocMessageBuilder>> request;
  kj::Maybe<Response<AnyPointer>> response;
  AnyPointer::Builder responseBuilder = nullptr;  // only valid if `response` is non-null
  kj::Own<ClientHook> clientRef;
  kj::Maybe<kj::Own<kj::PromiseFulfiller<AnyPointer::Pipeline>>> tailCallPipelineFulfiller;
  ClientHook::CallHints hints;
  bool isStreaming;
};

class LocalRequest final: public RequestHook {
public:
  inline LocalRequest(uint64_t interfaceId, uint16_t methodId,
                      kj::Maybe<MessageSize> sizeHint, ClientHook::CallHints hints,
                      kj::Own<ClientHook> client)
      : message(kj::heap<MallocMessageBuilder>(firstSegmentSize(sizeHint))),
        interfaceId(interfaceId), methodId(methodId), hints(hints), client(kj::mv(client)) {}

  RemotePromise<AnyPointer> send() override {
    bool isStreaming = false;
    return sendImpl(isStreaming);
  }

  kj::Promise<void> sendStreaming() override {
    // We don't do any special handling of streaming in RequestHook for local requests, because
    // there is no latency to compensate for between the client and server in this case.  However,
    // we record whether the call was streaming, so that it can be preserved as a streaming call
    // if the local capability later resolves to a remote capability.
    bool isStreaming = true;
    return sendImpl(isStreaming).ignoreResult();
  }

  AnyPointer::Pipeline sendForPipeline() override {
    KJ_REQUIRE(message.get() != nullptr, "Already called send() on this request.");

    hints.onlyPromisePipeline = true;
    bool isStreaming = false;
    auto context = kj::refcounted<LocalCallContext>(
        kj::mv(message), client->addRef(), hints, isStreaming);
    auto vpap = client->call(interfaceId, methodId, kj::addRef(*context), hints);
    return AnyPointer::Pipeline(kj::mv(vpap.pipeline));
  }

  const void* getBrand() override {
    return nullptr;
  }

  kj::Own<MallocMessageBuilder> message;

private:
  uint64_t interfaceId;
  uint16_t methodId;
  ClientHook::CallHints hints;
  kj::Own<ClientHook> client;

  RemotePromise<AnyPointer> sendImpl(bool isStreaming) {
    KJ_REQUIRE(message.get() != nullptr, "Already called send() on this request.");

    auto context = kj::refcounted<LocalCallContext>(kj::mv(message), client->addRef(), hints, isStreaming);
    auto promiseAndPipeline = client->call(interfaceId, methodId, kj::addRef(*context), hints);

    // Now the other branch returns the response from the context.
    auto promise = promiseAndPipeline.promise.then([context=kj::mv(context)]() mutable {
      // force response allocation
      auto reader = context->getResults(MessageSize { 0, 0 }).asReader();

      if (context->isShared()) {
        // We can't just move away context->response as `context` itself is still referenced by
        // something -- probably a Pipeline object. As a bit of a hack, LocalCallContext itself
        // implements ResponseHook so that we can just return a ref on it.
        //
        // TODO(cleanup): Maybe ResponseHook should be refcounted? Note that context->response
        //   might not necessarily contain a LocalResponse if it was resolved by a tail call, so
        //   we'd have to add refcounting to all ResponseHook implementations.
        context->releaseParams();      // The call is done so params can definitely be dropped.
        context->clientRef = nullptr;  // Definitely not using the client cap anymore either.
        return Response<AnyPointer>(reader, kj::mv(context));
      } else {
        return kj::mv(KJ_ASSERT_NONNULL(context->response));
      }
    });

    // We return the other branch.
    return RemotePromise<AnyPointer>(
        kj::mv(promise), AnyPointer::Pipeline(kj::mv(promiseAndPipeline.pipeline)));
  }
};

// =======================================================================================
// Call queues
//
// These classes handle pipelining in the case where calls need to be queued in-memory until some
// local operation completes.

class QueuedPipeline final: public PipelineHook, public kj::Refcounted {
  // A PipelineHook which simply queues calls while waiting for a PipelineHook to which to forward
  // them.

public:
  QueuedPipeline(kj::Promise<kj::Own<PipelineHook>>&& promiseParam)
      : promise(promiseParam.fork()),
        selfResolutionOp(promise.addBranch().then([this](kj::Own<PipelineHook>&& inner) {
          redirect = kj::mv(inner);
        }, [this](kj::Exception&& exception) {
          redirect = newBrokenPipeline(kj::mv(exception));
        }).eagerlyEvaluate(nullptr)) {}

  kj::Own<PipelineHook> addRef() override {
    return kj::addRef(*this);
  }

  kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) override {
    auto copy = kj::heapArrayBuilder<PipelineOp>(ops.size());
    for (auto& op: ops) {
      copy.add(op);
    }
    return getPipelinedCap(copy.finish());
  }

  kj::Own<ClientHook> getPipelinedCap(kj::Array<PipelineOp>&& ops) override;

private:
  kj::ForkedPromise<kj::Own<PipelineHook>> promise;

  kj::Maybe<kj::Own<PipelineHook>> redirect;
  // Once the promise resolves, this will become non-null and point to the underlying object.

  kj::Promise<void> selfResolutionOp;
  // Represents the operation which will set `redirect` when possible.

  kj::HashMap<kj::Array<PipelineOp>, kj::Own<ClientHook>> clientMap;
  // If the same pipelined cap is requested twice, we have to return the same object. This is
  // necessary because each ClientHook we create is a QueuedClient which queues up calls. If we
  // return a new one each time, there will be several queues, and ordering of calls will be lost
  // between the queues.
  //
  // One case where this is particularly problematic is with promises resolved over RPC. Consider
  // this case:
  //
  // * Alice holds a promise capability P pointing towards Bob.
  // * Bob makes a call Q on an object hosted by Alice.
  // * Without waiting for Q to complete, Bob obtains a pipelined-promise capability for Q's
  //   eventual result, P2.
  // * Alice invokes a method M on P. The call is sent to Bob.
  // * Bob resolves Alice's original promise P to P2.
  // * Alice receives a Resolve message from Bob resolving P to Q's eventual result.
  //   * As a result, Alice calls getPipelinedCap() on the QueuedPipeline for Q's result, which
  //     returns a QueuedClient for that result, which we'll call QR1.
  //   * Alice also sends a Disembargo to Bob.
  // * Alice calls a method M2 on P. This call is blocked locally waiting for the disembargo to
  //   complete.
  // * Bob receives Alice's first method call, M. Since it's addressed to P, which later resolved
  //   to Q's result, Bob reflects the call back to Alice.
  // * Alice receives the reflected call, which is addressed to Q's result.
  //   * Alice calls getPipelinedCap() on the QueuedPipeline for Q's result, which returns a
  //     QueuedClient for that result, which we'll call QR2.
  //   * Alice enqueues the call M on QR2.
  // * Bob receives Alice's Disembargo message, and reflects it back.
  // * Alices receives the Disembrago.
  //   * Alice unblocks the method cgall M2, which had been blocked on the embargo.
  //   * The call M2 is then equeued onto QR1.
  // * Finally, the call Q completes.
  //   * This causes QR1 and QR2 to resolve to their final destinations. But if QR1 and QR2 are
  //     separate objects, then one of them must resolve first. QR1 was created first, so naturally
  //     it resolves first, followed by QR2.
  //   * Because QR1 resolves first, method call M2 is delivered first.
  //   * QR2 resolves second, so method call M1 is delivered next.
  //   * THIS IS THE WRONG ORDER!
  //
  // In order to avoid this problem, it's necessary for QR1 and QR2 to be the same object, so that
  // they share the same call queue. In this case, M2 is correctly enqueued onto QR2 *after* M1 was
  // enqueued on QR1, and so the method calls are delivered in the correct order.
};

class QueuedClient final: public ClientHook, public kj::Refcounted {
  // A ClientHook which simply queues calls while waiting for a ClientHook to which to forward
  // them.

public:
  QueuedClient(kj::Promise<kj::Own<ClientHook>>&& promiseParam)
      : promise(promiseParam.fork()),
        selfResolutionOp(promise.addBranch().then([this](kj::Own<ClientHook>&& inner) {
          redirect = kj::mv(inner);
        }, [this](kj::Exception&& exception) {
          redirect = newBrokenCap(kj::mv(exception));
        }).eagerlyEvaluate(nullptr)),
        promiseForCallForwarding(promise.addBranch().fork()),
        promiseForClientResolution(promise.addBranch().fork()) {}

  Request<AnyPointer, AnyPointer> newCall(
      uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
      CallHints hints) override {
    auto hook = kj::heap<LocalRequest>(
        interfaceId, methodId, sizeHint, hints, kj::addRef(*this));
    auto root = hook->message->getRoot<AnyPointer>();
    return Request<AnyPointer, AnyPointer>(root, kj::mv(hook));
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              kj::Own<CallContextHook>&& context, CallHints hints) override {
    if (hints.noPromisePipelining) {
      // Optimize for no pipelining.
      auto promise = promiseForCallForwarding.addBranch()
          .then([=,context=kj::mv(context)](kj::Own<ClientHook>&& client) mutable {
        return client->call(interfaceId, methodId, kj::mv(context), hints).promise;
      });
      return VoidPromiseAndPipeline { kj::mv(promise), getDisabledPipeline() };
    } else if (hints.onlyPromisePipeline) {
      auto pipelinePromise = promiseForCallForwarding.addBranch()
          .then([=,context=kj::mv(context)](kj::Own<ClientHook>&& client) mutable {
        return client->call(interfaceId, methodId, kj::mv(context), hints).pipeline;
      });
      return VoidPromiseAndPipeline {
        kj::NEVER_DONE,
        kj::refcounted<QueuedPipeline>(kj::mv(pipelinePromise))
      };
    } else {
      auto split = promiseForCallForwarding.addBranch()
          .then([=,context=kj::mv(context)](kj::Own<ClientHook>&& client) mutable {
        auto vpap = client->call(interfaceId, methodId, kj::mv(context), hints);
        return kj::tuple(kj::mv(vpap.promise), kj::mv(vpap.pipeline));
      }).split();

      kj::Promise<void> completionPromise = kj::mv(kj::get<0>(split));
      kj::Promise<kj::Own<PipelineHook>> pipelinePromise = kj::mv(kj::get<1>(split));

      auto pipeline = kj::refcounted<QueuedPipeline>(kj::mv(pipelinePromise));

      // OK, now we can actually return our thing.
      return VoidPromiseAndPipeline { kj::mv(completionPromise), kj::mv(pipeline) };
    }
  }

  kj::Maybe<ClientHook&> getResolved() override {
    KJ_IF_MAYBE(inner, redirect) {
      return **inner;
    } else {
      return nullptr;
    }
  }

  kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
    return promiseForClientResolution.addBranch();
  }

  kj::Own<ClientHook> addRef() override {
    return kj::addRef(*this);
  }

  const void* getBrand() override {
    return nullptr;
  }

  kj::Maybe<int> getFd() override {
    KJ_IF_MAYBE(r, redirect) {
      return r->get()->getFd();
    } else {
      return nullptr;
    }
  }

private:
  typedef kj::ForkedPromise<kj::Own<ClientHook>> ClientHookPromiseFork;

  kj::Maybe<kj::Own<ClientHook>> redirect;
  // Once the promise resolves, this will become non-null and point to the underlying object.

  ClientHookPromiseFork promise;
  // Promise that resolves when we have a new ClientHook to forward to.
  //
  // This fork shall only have three branches:  `selfResolutionOp`, `promiseForCallForwarding`, and
  // `promiseForClientResolution`, in that order.

  kj::Promise<void> selfResolutionOp;
  // Represents the operation which will set `redirect` when possible.

  ClientHookPromiseFork promiseForCallForwarding;
  // When this promise resolves, each queued call will be forwarded to the real client.  This needs
  // to occur *before* any 'whenMoreResolved()' promises resolve, because we want to make sure
  // previously-queued calls are delivered before any new calls made in response to the resolution.

  ClientHookPromiseFork promiseForClientResolution;
  // whenMoreResolved() returns forks of this promise.  These must resolve *after* queued calls
  // have been initiated (so that any calls made in the whenMoreResolved() handler are correctly
  // delivered after calls made earlier), but *before* any queued calls return (because it might
  // confuse the application if a queued call returns before the capability on which it was made
  // resolves).  Luckily, we know that queued calls will involve, at the very least, an
  // eventLoop.evalLater.
};

kj::Own<ClientHook> QueuedPipeline::getPipelinedCap(kj::Array<PipelineOp>&& ops) {
  KJ_IF_MAYBE(r, redirect) {
    return r->get()->getPipelinedCap(kj::mv(ops));
  } else {
    return clientMap.findOrCreate(ops.asPtr(), [&]() {
      auto clientPromise = promise.addBranch()
          .then([ops = KJ_MAP(op, ops) { return op; }](kj::Own<PipelineHook> pipeline) {
        return pipeline->getPipelinedCap(kj::mv(ops));
      });
      return kj::HashMap<kj::Array<PipelineOp>, kj::Own<ClientHook>>::Entry {
        kj::mv(ops), kj::refcounted<QueuedClient>(kj::mv(clientPromise))
      };
    })->addRef();
  }
}

// =======================================================================================

class LocalPipeline final: public PipelineHook, public kj::Refcounted {
public:
  inline LocalPipeline(kj::Own<CallContextHook>&& contextParam)
      : context(kj::mv(contextParam)),
        results(context->getResults(MessageSize { 0, 0 })) {}

  kj::Own<PipelineHook> addRef() {
    return kj::addRef(*this);
  }

  kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) {
    return results.getPipelinedCap(ops);
  }

private:
  kj::Own<CallContextHook> context;
  AnyPointer::Reader results;
};

class LocalClient final: public ClientHook, public kj::Refcounted {
public:
  LocalClient(kj::Own<Capability::Server>&& serverParam, bool revocable = false) {
    auto& serverRef = *server.emplace(kj::mv(serverParam));
    serverRef.thisHook = this;
    if (revocable) revoker.emplace();
    startResolveTask(serverRef);
  }
  LocalClient(kj::Own<Capability::Server>&& serverParam,
              _::CapabilityServerSetBase& capServerSet, void* ptr,
              bool revocable = false)
      : capServerSet(&capServerSet), ptr(ptr) {
    auto& serverRef = *server.emplace(kj::mv(serverParam));
    serverRef.thisHook = this;
    if (revocable) revoker.emplace();
    startResolveTask(serverRef);
  }

  ~LocalClient() noexcept(false) {
    KJ_IF_MAYBE(s, server) {
      s->get()->thisHook = nullptr;
    }
  }

  void revoke(kj::Exception&& e) {
    KJ_IF_MAYBE(s, server) {
      KJ_ASSERT_NONNULL(revoker).cancel(e);
      brokenException = kj::mv(e);
      s->get()->thisHook = nullptr;
      server = nullptr;
    }
  }

  Request<AnyPointer, AnyPointer> newCall(
      uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
      CallHints hints) override {
    KJ_IF_MAYBE(r, resolved) {
      // We resolved to a shortened path. New calls MUST go directly to the replacement capability
      // so that their ordering is consistent with callers who call getResolved() to get direct
      // access to the new capability. In particular it's important that we don't place these calls
      // in our streaming queue.
      return r->get()->newCall(interfaceId, methodId, sizeHint, hints);
    }

    auto hook = kj::heap<LocalRequest>(
        interfaceId, methodId, sizeHint, hints, kj::addRef(*this));
    auto root = hook->message->getRoot<AnyPointer>();
    return Request<AnyPointer, AnyPointer>(root, kj::mv(hook));
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              kj::Own<CallContextHook>&& context, CallHints hints) override {
    KJ_IF_MAYBE(r, resolved) {
      // We resolved to a shortened path. New calls MUST go directly to the replacement capability
      // so that their ordering is consistent with callers who call getResolved() to get direct
      // access to the new capability. In particular it's important that we don't place these calls
      // in our streaming queue.
      return r->get()->call(interfaceId, methodId, kj::mv(context), hints);
    }

    auto contextPtr = context.get();

    // We don't want to actually dispatch the call synchronously, because we don't want the callee
    // to have any side effects before the promise is returned to the caller.  This helps avoid
    // race conditions.
    //
    // So, we do an evalLater() here.
    //
    // Note also that QueuedClient depends on this evalLater() to ensure that pipelined calls don't
    // complete before 'whenMoreResolved()' promises resolve.
    auto promise = kj::evalLater([this,interfaceId,methodId,contextPtr]() {
      if (blocked) {
        return kj::newAdaptedPromise<kj::Promise<void>, BlockedCall>(
            *this, interfaceId, methodId, *contextPtr);
      } else {
        return callInternal(interfaceId, methodId, *contextPtr);
      }
    }).attach(kj::addRef(*this));

    if (hints.noPromisePipelining) {
      // No need to set up pipelining..

      // Make sure we release the params on return, since we would on the normal pipelining path.
      // TODO(perf): Experiment with whether this is actually useful. It seems likely the params
      //   will be released soon anyway, so maybe this is a waste?
      promise = promise.then([context=kj::mv(context)]() mutable {
        context->releaseParams();
      });

      // When we do apply pipelining, the use of `.fork()` has the side effect of eagerly
      // evaluating the promise. To match the behavior here, we use `.eagerlyEvaluate()`.
      // TODO(perf): Maybe we don't need to match behavior? It did break some tests but arguably
      //   those tests are weird and not what a real program would do...
      promise = promise.eagerlyEvaluate(nullptr);
      return VoidPromiseAndPipeline { kj::mv(promise), getDisabledPipeline() };
    }

    kj::Promise<void> completionPromise = nullptr;
    kj::Promise<void> pipelineBranch = nullptr;

    if (hints.onlyPromisePipeline) {
      pipelineBranch = kj::mv(promise);
      completionPromise = kj::NEVER_DONE;
    } else {
      // We have to fork this promise for the pipeline to receive a copy of the answer.
      auto forked = promise.fork();
      pipelineBranch = forked.addBranch();
      completionPromise = forked.addBranch().attach(context->addRef());
    }

    auto pipelinePromise = pipelineBranch
        .then([=,context=context->addRef()]() mutable -> kj::Own<PipelineHook> {
          context->releaseParams();
          return kj::refcounted<LocalPipeline>(kj::mv(context));
        });

    auto tailPipelinePromise = context->onTailCall()
        .then([context = context->addRef()](AnyPointer::Pipeline&& pipeline) {
      return kj::mv(pipeline.hook);
    });

    pipelinePromise = pipelinePromise.exclusiveJoin(kj::mv(tailPipelinePromise));

    return VoidPromiseAndPipeline { kj::mv(completionPromise),
        kj::refcounted<QueuedPipeline>(kj::mv(pipelinePromise)) };
  }

  kj::Maybe<ClientHook&> getResolved() override {
    return resolved.map([](kj::Own<ClientHook>& hook) -> ClientHook& { return *hook; });
  }

  kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
    KJ_IF_MAYBE(r, resolved) {
      return kj::Promise<kj::Own<ClientHook>>(r->get()->addRef());
    } else KJ_IF_MAYBE(t, resolveTask) {
      return t->addBranch().then([this]() {
        return KJ_ASSERT_NONNULL(resolved)->addRef();
      });
    } else {
      return nullptr;
    }
  }

  kj::Own<ClientHook> addRef() override {
    return kj::addRef(*this);
  }

  static const uint BRAND;
  // Value is irrelevant; used for pointer.

  const void* getBrand() override {
    return &BRAND;
  }

  kj::Maybe<kj::Promise<void*>> getLocalServer(_::CapabilityServerSetBase& capServerSet) {
    // If this is a local capability created through `capServerSet`, return the underlying Server.
    // Otherwise, return nullptr. Default implementation (which everyone except LocalClient should
    // use) always returns nullptr.

    if (this->capServerSet == &capServerSet) {
      if (blocked) {
        // If streaming calls are in-flight, it could be the case that they were originally sent
        // over RPC and reflected back, before the capability had resolved to a local object. In
        // that case, the client may already perceive these calls as "done" because the RPC
        // implementation caused the client promise to resolve early. However, the capability is
        // now local, and the app is trying to break through the LocalClient wrapper and access
        // the server directly, bypassing the stream queue. Since the app thinks that all
        // previous calls already completed, it may then try to queue a new call directly on the
        // server, jumping the queue.
        //
        // We can solve this by delaying getLocalServer() until all current streaming calls have
        // finished. Note that if a new streaming call is started *after* this point, we need not
        // worry about that, because in this case it is presumably a local call and the caller
        // won't be informed of completion until the call actually does complete. Thus the caller
        // is well-aware that this call is still in-flight.
        //
        // However, the app still cannot assume that there aren't multiple clients, perhaps even
        // a malicious client that tries to send stream requests that overlap with the app's
        // direct use of the server... so it's up to the app to check for and guard against
        // concurrent calls after using getLocalServer().
        return kj::newAdaptedPromise<kj::Promise<void>, BlockedCall>(*this)
            .then([this]() { return ptr; });
      } else {
        return kj::Promise<void*>(ptr);
      }
    } else {
      return nullptr;
    }
  }

  kj::Maybe<int> getFd() override {
    KJ_IF_MAYBE(s, server) {
      return s->get()->getFd();
    } else {
      return nullptr;
    }
  }

private:
  kj::Maybe<kj::Own<Capability::Server>> server;
  _::CapabilityServerSetBase* capServerSet = nullptr;
  void* ptr = nullptr;

  kj::Maybe<kj::ForkedPromise<void>> resolveTask;
  kj::Maybe<kj::Own<ClientHook>> resolved;

  kj::Maybe<kj::Canceler> revoker;
  // If non-null, all promises must be wrapped in this revoker.

  void startResolveTask(Capability::Server& serverRef) {
    resolveTask = serverRef.shortenPath().map([this](kj::Promise<Capability::Client> promise) {
      KJ_IF_MAYBE(r, revoker) {
        promise = r->wrap(kj::mv(promise));
      }

      return promise.then([this](Capability::Client&& cap) {
        auto hook = ClientHook::from(kj::mv(cap));

        if (blocked) {
          // This is a streaming interface and we have some calls queued up as a result. We cannot
          // resolve directly to the new shorter path because this may allow new calls to hop
          // the queue -- we need to embargo new calls until the queue clears out.
          auto promise = kj::newAdaptedPromise<kj::Promise<void>, BlockedCall>(*this)
              .then([hook = kj::mv(hook)]() mutable { return kj::mv(hook); });
          hook = newLocalPromiseClient(kj::mv(promise));
        }

        resolved = kj::mv(hook);
      }).fork();
    });
  }

  class BlockedCall {
  public:
    BlockedCall(kj::PromiseFulfiller<kj::Promise<void>>& fulfiller, LocalClient& client,
                uint64_t interfaceId, uint16_t methodId, CallContextHook& context)
        : fulfiller(fulfiller), client(client),
          interfaceId(interfaceId), methodId(methodId), context(context),
          prev(client.blockedCallsEnd) {
      *prev = *this;
      client.blockedCallsEnd = &next;
    }

    BlockedCall(kj::PromiseFulfiller<kj::Promise<void>>& fulfiller, LocalClient& client)
        : fulfiller(fulfiller), client(client), prev(client.blockedCallsEnd) {
      *prev = *this;
      client.blockedCallsEnd = &next;
    }

    ~BlockedCall() noexcept(false) {
      unlink();
    }

    void unblock() {
      unlink();
      KJ_IF_MAYBE(c, context) {
        fulfiller.fulfill(kj::evalNow([&]() {
          return client.callInternal(interfaceId, methodId, *c);
        }));
      } else {
        // This is just a barrier.
        fulfiller.fulfill(kj::READY_NOW);
      }
    }

  private:
    kj::PromiseFulfiller<kj::Promise<void>>& fulfiller;
    LocalClient& client;
    uint64_t interfaceId;
    uint16_t methodId;
    kj::Maybe<CallContextHook&> context;

    kj::Maybe<BlockedCall&> next;
    kj::Maybe<BlockedCall&>* prev;

    void unlink() {
      if (prev != nullptr) {
        *prev = next;
        KJ_IF_MAYBE(n, next) {
          n->prev = prev;
        } else {
          client.blockedCallsEnd = prev;
        }
        prev = nullptr;
      }
    }
  };

  class BlockingScope {
  public:
    BlockingScope(LocalClient& client): client(client) { client.blocked = true; }
    BlockingScope(): client(nullptr) {}
    BlockingScope(BlockingScope&& other): client(other.client) { other.client = nullptr; }
    KJ_DISALLOW_COPY(BlockingScope);

    ~BlockingScope() noexcept(false) {
      KJ_IF_MAYBE(c, client) {
        c->unblock();
      }
    }

  private:
    kj::Maybe<LocalClient&> client;
  };

  bool blocked = false;
  kj::Maybe<kj::Exception> brokenException;
  kj::Maybe<BlockedCall&> blockedCalls;
  kj::Maybe<BlockedCall&>* blockedCallsEnd = &blockedCalls;

  void unblock() {
    blocked = false;
    while (!blocked) {
      KJ_IF_MAYBE(t, blockedCalls) {
        t->unblock();
      } else {
        break;
      }
    }
  }

  kj::Promise<void> callInternal(uint64_t interfaceId, uint16_t methodId,
                                 CallContextHook& context) {
    KJ_ASSERT(!blocked);

    KJ_IF_MAYBE(e, brokenException) {
      // Previous streaming call threw, so everything fails from now on.
      return kj::cp(*e);
    }

    // `server` can't be null here since `brokenException` is null.
    auto result = KJ_ASSERT_NONNULL(server)->dispatchCall(interfaceId, methodId,
                                       CallContext<AnyPointer, AnyPointer>(context));

    KJ_IF_MAYBE(r, revoker) {
      result.promise = r->wrap(kj::mv(result.promise));
    }

    if (!result.allowCancellation) {
      // Make sure this call cannot be canceled by forking the promise and detaching one branch.
      auto fork = result.promise.attach(kj::addRef(*this), context.addRef()).fork();
      result.promise = fork.addBranch();
      fork.addBranch().detach([](kj::Exception&&) {
        // Exception from canceled call is silently discarded. The caller should have waited for
        // it if they cared.
      });
    }

    if (result.isStreaming) {
      return result.promise
          .catch_([this](kj::Exception&& e) {
        brokenException = kj::cp(e);
        kj::throwRecoverableException(kj::mv(e));
      }).attach(BlockingScope(*this));
    } else {
      return kj::mv(result.promise);
    }
  }
};

const uint LocalClient::BRAND = 0;

kj::Own<ClientHook> Capability::Client::makeLocalClient(kj::Own<Capability::Server>&& server) {
  return kj::refcounted<LocalClient>(kj::mv(server));
}

kj::Own<ClientHook> Capability::Client::makeRevocableLocalClient(Capability::Server& server) {
  auto result = kj::refcounted<LocalClient>(
      kj::Own<Capability::Server>(&server, kj::NullDisposer::instance), true /* revocable */);
  return result;
}
void Capability::Client::revokeLocalClient(ClientHook& hook) {
  revokeLocalClient(hook, KJ_EXCEPTION(FAILED,
      "capability was revoked (RevocableServer was destroyed)"));
}
void Capability::Client::revokeLocalClient(ClientHook& hook, kj::Exception&& e) {
  kj::downcast<LocalClient>(hook).revoke(kj::mv(e));
}

kj::Own<ClientHook> newLocalPromiseClient(kj::Promise<kj::Own<ClientHook>>&& promise) {
  return kj::refcounted<QueuedClient>(kj::mv(promise));
}

kj::Own<PipelineHook> newLocalPromisePipeline(kj::Promise<kj::Own<PipelineHook>>&& promise) {
  return kj::refcounted<QueuedPipeline>(kj::mv(promise));
}

// =======================================================================================

namespace _ {  // private

class PipelineBuilderHook final: public PipelineHook, public kj::Refcounted {
public:
  PipelineBuilderHook(uint firstSegmentWords)
      : message(firstSegmentWords),
        root(message.getRoot<AnyPointer>()) {}

  kj::Own<PipelineHook> addRef() override {
    return kj::addRef(*this);
  }

  kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) override {
    return root.asReader().getPipelinedCap(ops);
  }

  MallocMessageBuilder message;
  AnyPointer::Builder root;
};

PipelineBuilderPair newPipelineBuilder(uint firstSegmentWords) {
  auto hook = kj::refcounted<PipelineBuilderHook>(firstSegmentWords);
  auto root = hook->root;
  return { root, kj::mv(hook) };
}

}  // namespace _ (private)

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
      : exception(exception), message(firstSegmentSize(sizeHint)) {}

  RemotePromise<AnyPointer> send() override {
    return RemotePromise<AnyPointer>(kj::cp(exception),
        AnyPointer::Pipeline(kj::refcounted<BrokenPipeline>(exception)));
  }

  kj::Promise<void> sendStreaming() override {
    return kj::cp(exception);
  }

  AnyPointer::Pipeline sendForPipeline() override {
    return AnyPointer::Pipeline(kj::refcounted<BrokenPipeline>(exception));
  }

  const void* getBrand() override {
    return nullptr;
  }

  kj::Exception exception;
  MallocMessageBuilder message;
};

class BrokenClient final: public ClientHook, public kj::Refcounted {
public:
  BrokenClient(const kj::Exception& exception, bool resolved, const void* brand)
      : exception(exception), resolved(resolved), brand(brand) {}
  BrokenClient(const kj::StringPtr description, bool resolved, const void* brand)
      : exception(kj::Exception::Type::FAILED, "", 0, kj::str(description)),
        resolved(resolved), brand(brand) {}

  Request<AnyPointer, AnyPointer> newCall(
      uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
      CallHints hints) override {
    return newBrokenRequest(kj::cp(exception), sizeHint);
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              kj::Own<CallContextHook>&& context, CallHints hints) override {
    return VoidPromiseAndPipeline { kj::cp(exception), kj::refcounted<BrokenPipeline>(exception) };
  }

  kj::Maybe<ClientHook&> getResolved() override {
    return nullptr;
  }

  kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
    if (resolved) {
      return nullptr;
    } else {
      return kj::Promise<kj::Own<ClientHook>>(kj::cp(exception));
    }
  }

  kj::Own<ClientHook> addRef() override {
    return kj::addRef(*this);
  }

  const void* getBrand() override {
    return brand;
  }

  kj::Maybe<int> getFd() override {
    return nullptr;
  }

private:
  kj::Exception exception;
  bool resolved;
  const void* brand;
};

kj::Own<ClientHook> BrokenPipeline::getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) {
  return kj::refcounted<BrokenClient>(exception, false, &ClientHook::BROKEN_CAPABILITY_BRAND);
}

kj::Own<ClientHook> newNullCap() {
  // A null capability, unlike other broken capabilities, is considered resolved.
  return kj::refcounted<BrokenClient>("Called null capability.", true,
                                      &ClientHook::NULL_CAPABILITY_BRAND);
}

}  // namespace

kj::Own<ClientHook> newBrokenCap(kj::StringPtr reason) {
  return kj::refcounted<BrokenClient>(reason, false, &ClientHook::BROKEN_CAPABILITY_BRAND);
}

kj::Own<ClientHook> newBrokenCap(kj::Exception&& reason) {
  return kj::refcounted<BrokenClient>(kj::mv(reason), false, &ClientHook::BROKEN_CAPABILITY_BRAND);
}

kj::Own<PipelineHook> newBrokenPipeline(kj::Exception&& reason) {
  return kj::refcounted<BrokenPipeline>(kj::mv(reason));
}

Request<AnyPointer, AnyPointer> newBrokenRequest(
    kj::Exception&& reason, kj::Maybe<MessageSize> sizeHint) {
  auto hook = kj::heap<BrokenRequest>(kj::mv(reason), sizeHint);
  auto root = hook->message.getRoot<AnyPointer>();
  return Request<AnyPointer, AnyPointer>(root, kj::mv(hook));
}

kj::Own<PipelineHook> getDisabledPipeline() {
  class DisabledPipelineHook final: public PipelineHook {
  public:
    kj::Own<PipelineHook> addRef() override {
      return kj::Own<PipelineHook>(this, kj::NullDisposer::instance);
    }

    kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) override {
      return newBrokenCap(KJ_EXCEPTION(FAILED,
          "caller specified noPromisePipelining hint, but then tried to pipeline"));
    }

    kj::Own<ClientHook> getPipelinedCap(kj::Array<PipelineOp>&& ops) override {
      return newBrokenCap(KJ_EXCEPTION(FAILED,
          "caller specified noPromisePipelining hint, but then tried to pipeline"));
    }
  };
  static DisabledPipelineHook instance;
  return instance.addRef();
}

// =======================================================================================

ReaderCapabilityTable::ReaderCapabilityTable(
    kj::Array<kj::Maybe<kj::Own<ClientHook>>> table)
    : table(kj::mv(table)) {
  setGlobalBrokenCapFactoryForLayoutCpp(brokenCapFactory);
}

kj::Maybe<kj::Own<ClientHook>> ReaderCapabilityTable::extractCap(uint index) {
  if (index < table.size()) {
    return table[index].map([](kj::Own<ClientHook>& cap) { return cap->addRef(); });
  } else {
    return nullptr;
  }
}

BuilderCapabilityTable::BuilderCapabilityTable() {
  setGlobalBrokenCapFactoryForLayoutCpp(brokenCapFactory);
}

kj::Maybe<kj::Own<ClientHook>> BuilderCapabilityTable::extractCap(uint index) {
  if (index < table.size()) {
    return table[index].map([](kj::Own<ClientHook>& cap) { return cap->addRef(); });
  } else {
    return nullptr;
  }
}

uint BuilderCapabilityTable::injectCap(kj::Own<ClientHook>&& cap) {
  uint result = table.size();
  table.add(kj::mv(cap));
  return result;
}

void BuilderCapabilityTable::dropCap(uint index) {
  KJ_ASSERT(index < table.size(), "Invalid capability descriptor in message.") {
    return;
  }
  table[index] = nullptr;
}

// =======================================================================================
// CapabilityServerSet

namespace _ {  // private

Capability::Client CapabilityServerSetBase::addInternal(
    kj::Own<Capability::Server>&& server, void* ptr) {
  return Capability::Client(kj::refcounted<LocalClient>(kj::mv(server), *this, ptr));
}

kj::Promise<void*> CapabilityServerSetBase::getLocalServerInternal(Capability::Client& client) {
  ClientHook* hook = client.hook.get();

  // Get the most-resolved-so-far version of the hook.
  for (;;) {
    KJ_IF_MAYBE(h, hook->getResolved()) {
      hook = h;
    } else {
      break;
    }
  }

  // Try to unwrap that.
  if (hook->getBrand() == &LocalClient::BRAND) {
    KJ_IF_MAYBE(promise, kj::downcast<LocalClient>(*hook).getLocalServer(*this)) {
      // This is definitely a member of our set and will resolve to non-null. We just have to wait
      // for any existing streaming calls to complete.
      return kj::mv(*promise);
    }
  }

  // OK, the capability isn't part of this set.
  KJ_IF_MAYBE(p, hook->whenMoreResolved()) {
    // This hook is an unresolved promise. It might resolve eventually to a local server, so wait
    // for it.
    return p->attach(hook->addRef())
        .then([this](kj::Own<ClientHook>&& resolved) {
      Capability::Client client(kj::mv(resolved));
      return getLocalServerInternal(client);
    });
  } else {
    // Cap is settled, so it definitely will never resolve to a member of this set.
    return kj::implicitCast<void*>(nullptr);
  }
}

}  // namespace _ (private)

}  // namespace capnp
