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
    false
  };
}

Capability::Server::DispatchCallResult Capability::Server::internalUnimplemented(
    const char* interfaceName, uint64_t typeId, uint16_t methodId) {
  return {
    KJ_EXCEPTION(UNIMPLEMENTED, "Method not implemented.", interfaceName, typeId, methodId),
    false
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

class LocalResponse final: public ResponseHook, public kj::Refcounted {
public:
  LocalResponse(kj::Maybe<MessageSize> sizeHint)
      : message(firstSegmentSize(sizeHint)) {}

  MallocMessageBuilder message;
};

class LocalCallContext final: public CallContextHook, public kj::Refcounted {
public:
  LocalCallContext(kj::Own<MallocMessageBuilder>&& request, kj::Own<ClientHook> clientRef,
                   kj::Own<kj::PromiseFulfiller<void>> cancelAllowedFulfiller)
      : request(kj::mv(request)), clientRef(kj::mv(clientRef)),
        cancelAllowedFulfiller(kj::mv(cancelAllowedFulfiller)) {}

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
      auto localResponse = kj::refcounted<LocalResponse>(sizeHint);
      responseBuilder = localResponse->message.getRoot<AnyPointer>();
      response = Response<AnyPointer>(responseBuilder.asReader(), kj::mv(localResponse));
    }
    return responseBuilder;
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

    auto promise = request->send();

    auto voidPromise = promise.then([this](Response<AnyPointer>&& tailResponse) {
      response = kj::mv(tailResponse);
    });

    return { kj::mv(voidPromise), PipelineHook::from(kj::mv(promise)) };
  }
  kj::Promise<AnyPointer::Pipeline> onTailCall() override {
    auto paf = kj::newPromiseAndFulfiller<AnyPointer::Pipeline>();
    tailCallPipelineFulfiller = kj::mv(paf.fulfiller);
    return kj::mv(paf.promise);
  }
  void allowCancellation() override {
    cancelAllowedFulfiller->fulfill();
  }
  kj::Own<CallContextHook> addRef() override {
    return kj::addRef(*this);
  }

  kj::Maybe<kj::Own<MallocMessageBuilder>> request;
  kj::Maybe<Response<AnyPointer>> response;
  AnyPointer::Builder responseBuilder = nullptr;  // only valid if `response` is non-null
  kj::Own<ClientHook> clientRef;
  kj::Maybe<kj::Own<kj::PromiseFulfiller<AnyPointer::Pipeline>>> tailCallPipelineFulfiller;
  kj::Own<kj::PromiseFulfiller<void>> cancelAllowedFulfiller;
};

class LocalRequest final: public RequestHook {
public:
  inline LocalRequest(uint64_t interfaceId, uint16_t methodId,
                      kj::Maybe<MessageSize> sizeHint, kj::Own<ClientHook> client)
      : message(kj::heap<MallocMessageBuilder>(firstSegmentSize(sizeHint))),
        interfaceId(interfaceId), methodId(methodId), client(kj::mv(client)) {}

  RemotePromise<AnyPointer> send() override {
    KJ_REQUIRE(message.get() != nullptr, "Already called send() on this request.");

    auto cancelPaf = kj::newPromiseAndFulfiller<void>();

    auto context = kj::refcounted<LocalCallContext>(
        kj::mv(message), client->addRef(), kj::mv(cancelPaf.fulfiller));
    auto promiseAndPipeline = client->call(interfaceId, methodId, kj::addRef(*context));

    // We have to make sure the call is not canceled unless permitted.  We need to fork the promise
    // so that if the client drops their copy, the promise isn't necessarily canceled.
    auto forked = promiseAndPipeline.promise.fork();

    // We daemonize one branch, but only after joining it with the promise that fires if
    // cancellation is allowed.
    forked.addBranch()
        .attach(kj::addRef(*context))
        .exclusiveJoin(kj::mv(cancelPaf.promise))
        .detach([](kj::Exception&&) {});  // ignore exceptions

    // Now the other branch returns the response from the context.
    auto promise = forked.addBranch().then(kj::mvCapture(context,
        [](kj::Own<LocalCallContext>&& context) {
      context->getResults(MessageSize { 0, 0 });  // force response allocation
      return kj::mv(KJ_ASSERT_NONNULL(context->response));
    }));

    // We return the other branch.
    return RemotePromise<AnyPointer>(
        kj::mv(promise), AnyPointer::Pipeline(kj::mv(promiseAndPipeline.pipeline)));
  }

  kj::Promise<void> sendStreaming() override {
    // We don't do any special handling of streaming in RequestHook for local requests, because
    // there is no latency to compensate for between the client and server in this case.
    return send().ignoreResult();
  }

  const void* getBrand() override {
    return nullptr;
  }

  kj::Own<MallocMessageBuilder> message;

private:
  uint64_t interfaceId;
  uint16_t methodId;
  kj::Own<ClientHook> client;
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
      uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint) override {
    auto hook = kj::heap<LocalRequest>(
        interfaceId, methodId, sizeHint, kj::addRef(*this));
    auto root = hook->message->getRoot<AnyPointer>();
    return Request<AnyPointer, AnyPointer>(root, kj::mv(hook));
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              kj::Own<CallContextHook>&& context) override {
    // This is a bit complicated.  We need to initiate this call later on.  When we initiate the
    // call, we'll get a void promise for its completion and a pipeline object.  Right now, we have
    // to produce a similar void promise and pipeline that will eventually be chained to those.
    // The problem is, these are two independent objects, but they both depend on the result of
    // one future call.
    //
    // So, we need to set up a continuation that will initiate the call later, then we need to
    // fork the promise for that continuation in order to send the completion promise and the
    // pipeline to their respective places.
    //
    // TODO(perf):  Too much reference counting?  Can we do better?  Maybe a way to fork
    //   Promise<Tuple<T, U>> into Tuple<Promise<T>, Promise<U>>?

    struct CallResultHolder: public kj::Refcounted {
      // Essentially acts as a refcounted \VoidPromiseAndPipeline, so that we can create a promise
      // for it and fork that promise.

      VoidPromiseAndPipeline content;
      // One branch of the fork will use content.promise, the other branch will use
      // content.pipeline.  Neither branch will touch the other's piece.

      inline CallResultHolder(VoidPromiseAndPipeline&& content): content(kj::mv(content)) {}

      kj::Own<CallResultHolder> addRef() { return kj::addRef(*this); }
    };

    // Create a promise for the call initiation.
    kj::ForkedPromise<kj::Own<CallResultHolder>> callResultPromise =
        promiseForCallForwarding.addBranch().then(kj::mvCapture(context,
        [=](kj::Own<CallContextHook>&& context, kj::Own<ClientHook>&& client){
          return kj::refcounted<CallResultHolder>(
              client->call(interfaceId, methodId, kj::mv(context)));
        })).fork();

    // Create a promise that extracts the pipeline from the call initiation, and construct our
    // QueuedPipeline to chain to it.
    auto pipelinePromise = callResultPromise.addBranch().then(
        [](kj::Own<CallResultHolder>&& callResult){
          return kj::mv(callResult->content.pipeline);
        });
    auto pipeline = kj::refcounted<QueuedPipeline>(kj::mv(pipelinePromise));

    // Create a promise that simply chains to the void promise produced by the call initiation.
    auto completionPromise = callResultPromise.addBranch().then(
        [](kj::Own<CallResultHolder>&& callResult){
          return kj::mv(callResult->content.promise);
        });

    // OK, now we can actually return our thing.
    return VoidPromiseAndPipeline { kj::mv(completionPromise), kj::mv(pipeline) };
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
    auto clientPromise = promise.addBranch().then(kj::mvCapture(ops,
        [](kj::Array<PipelineOp>&& ops, kj::Own<PipelineHook> pipeline) {
          return pipeline->getPipelinedCap(kj::mv(ops));
        }));

    return kj::refcounted<QueuedClient>(kj::mv(clientPromise));
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
  LocalClient(kj::Own<Capability::Server>&& serverParam)
      : server(kj::mv(serverParam)) {
    server->thisHook = this;

    resolveTask = server->shortenPath().map([this](kj::Promise<Capability::Client> promise) {
      return promise.then([this](Capability::Client&& cap) {
        auto hook = ClientHook::from(kj::mv(cap));
        resolved = hook->addRef();
      }).fork();
    });
  }
  LocalClient(kj::Own<Capability::Server>&& serverParam,
              _::CapabilityServerSetBase& capServerSet, void* ptr)
      : server(kj::mv(serverParam)), capServerSet(&capServerSet), ptr(ptr) {
    server->thisHook = this;
  }

  ~LocalClient() noexcept(false) {
    server->thisHook = nullptr;
  }

  Request<AnyPointer, AnyPointer> newCall(
      uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint) override {
    auto hook = kj::heap<LocalRequest>(
        interfaceId, methodId, sizeHint, kj::addRef(*this));
    auto root = hook->message->getRoot<AnyPointer>();
    return Request<AnyPointer, AnyPointer>(root, kj::mv(hook));
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              kj::Own<CallContextHook>&& context) override {
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

    // We have to fork this promise for the pipeline to receive a copy of the answer.
    auto forked = promise.fork();

    auto pipelinePromise = forked.addBranch().then(kj::mvCapture(context->addRef(),
        [=](kj::Own<CallContextHook>&& context) -> kj::Own<PipelineHook> {
          context->releaseParams();
          return kj::refcounted<LocalPipeline>(kj::mv(context));
        }));

    auto tailPipelinePromise = context->onTailCall().then([](AnyPointer::Pipeline&& pipeline) {
      return kj::mv(pipeline.hook);
    });

    pipelinePromise = pipelinePromise.exclusiveJoin(kj::mv(tailPipelinePromise));

    auto completionPromise = forked.addBranch().attach(kj::mv(context));

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
    return server->getFd();
  }

private:
  kj::Own<Capability::Server> server;
  _::CapabilityServerSetBase* capServerSet = nullptr;
  void* ptr = nullptr;

  kj::Maybe<kj::ForkedPromise<void>> resolveTask;
  kj::Maybe<kj::Own<ClientHook>> resolved;

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

    auto result = server->dispatchCall(interfaceId, methodId,
                                       CallContext<AnyPointer, AnyPointer>(context));
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

kj::Own<ClientHook> newLocalPromiseClient(kj::Promise<kj::Own<ClientHook>>&& promise) {
  return kj::refcounted<QueuedClient>(kj::mv(promise));
}

kj::Own<PipelineHook> newLocalPromisePipeline(kj::Promise<kj::Own<PipelineHook>>&& promise) {
  return kj::refcounted<QueuedPipeline>(kj::mv(promise));
}

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

  const void* getBrand() override {
    return nullptr;
  }

  kj::Exception exception;
  MallocMessageBuilder message;
};

class BrokenClient final: public ClientHook, public kj::Refcounted {
public:
  BrokenClient(const kj::Exception& exception, bool resolved, const void* brand = nullptr)
      : exception(exception), resolved(resolved), brand(brand) {}
  BrokenClient(const kj::StringPtr description, bool resolved, const void* brand = nullptr)
      : exception(kj::Exception::Type::FAILED, "", 0, kj::str(description)),
        resolved(resolved), brand(brand) {}

  Request<AnyPointer, AnyPointer> newCall(
      uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint) override {
    return newBrokenRequest(kj::cp(exception), sizeHint);
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              kj::Own<CallContextHook>&& context) override {
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
  return kj::refcounted<BrokenClient>(exception, false);
}

kj::Own<ClientHook> newNullCap() {
  // A null capability, unlike other broken capabilities, is considered resolved.
  return kj::refcounted<BrokenClient>("Called null capability.", true,
                                      &ClientHook::NULL_CAPABILITY_BRAND);
}

}  // namespace

kj::Own<ClientHook> newBrokenCap(kj::StringPtr reason) {
  return kj::refcounted<BrokenClient>(reason, false);
}

kj::Own<ClientHook> newBrokenCap(kj::Exception&& reason) {
  return kj::refcounted<BrokenClient>(kj::mv(reason), false);
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
