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

#include "capability.h"
#include "capability-context.h"
#include "message.h"
#include "arena.h"
#include <kj/refcount.h>
#include <kj/debug.h>
#include <kj/vector.h>
#include <map>

namespace capnp {

Capability::Client::Client(decltype(nullptr))
    : hook(newBrokenCap("Called null capability.")) {}

kj::Promise<void> Capability::Server::internalUnimplemented(
    const char* actualInterfaceName, uint64_t requestedTypeId) {
  KJ_FAIL_REQUIRE("Requested interface not implemented.", actualInterfaceName, requestedTypeId) {
    // Recoverable exception will be caught by promise framework.
    return kj::READY_NOW;
  }
}

kj::Promise<void> Capability::Server::internalUnimplemented(
    const char* interfaceName, uint64_t typeId, uint16_t methodId) {
  KJ_FAIL_REQUIRE("Method not implemented.", interfaceName, typeId, methodId) {
    // Recoverable exception will be caught by promise framework.
    return kj::READY_NOW;
  }
}

kj::Promise<void> Capability::Server::internalUnimplemented(
    const char* interfaceName, const char* methodName, uint64_t typeId, uint16_t methodId) {
  KJ_FAIL_REQUIRE("Method not implemented.", interfaceName, typeId, methodName, methodId) {
    // Recoverable exception will be caught by promise framework.
    return kj::READY_NOW;
  }
}

ResponseHook::~ResponseHook() noexcept(false) {}

kj::Promise<void> ClientHook::whenResolved() const {
  KJ_IF_MAYBE(promise, whenMoreResolved()) {
    return promise->then([](kj::Own<const ClientHook>&& resolution) {
      return resolution->whenResolved();
    });
  } else {
    return kj::READY_NOW;
  }
}

kj::Own<const ClientHook> newBrokenCap(const char* reason) {
  return _::newBrokenCap(reason);
}

// =======================================================================================

namespace {

class LocalResponse final: public ResponseHook, public kj::Refcounted {
public:
  LocalResponse(uint sizeHint)
      : message(sizeHint == 0 ? SUGGESTED_FIRST_SEGMENT_WORDS : sizeHint) {}

  LocalMessage message;
};

class LocalCallContext final: public CallContextHook, public kj::Refcounted {
public:
  LocalCallContext(kj::Own<LocalMessage>&& request, kj::Own<const ClientHook> clientRef)
      : request(kj::mv(request)), clientRef(kj::mv(clientRef)) {}

  ObjectPointer::Reader getParams() override {
    KJ_IF_MAYBE(r, request) {
      return r->get()->getRoot();
    } else {
      KJ_FAIL_REQUIRE("Can't call getParams() after releaseParams().");
    }
  }
  void releaseParams() override {
    request = nullptr;
  }
  ObjectPointer::Builder getResults(uint firstSegmentWordSize) override {
    if (!response) {
      response = kj::refcounted<LocalResponse>(firstSegmentWordSize);
    }
    return response->message.getRoot();
  }
  void allowAsyncCancellation() override {
    // ignored for local calls
  }
  bool isCanceled() override {
    return false;
  }
  kj::Own<CallContextHook> addRef() override {
    return kj::addRef(*this);
  }

  kj::Maybe<kj::Own<LocalMessage>> request;
  kj::Own<LocalResponse> response;
  kj::Own<const ClientHook> clientRef;
};

class LocalRequest final: public RequestHook {
public:
  inline LocalRequest(const kj::EventLoop& loop, uint64_t interfaceId, uint16_t methodId,
                      uint firstSegmentWordSize, kj::Own<const ClientHook> client)
      : message(kj::heap<LocalMessage>(
            firstSegmentWordSize == 0 ? SUGGESTED_FIRST_SEGMENT_WORDS : firstSegmentWordSize)),
        loop(loop),
        interfaceId(interfaceId), methodId(methodId), client(kj::mv(client)) {}

  RemotePromise<ObjectPointer> send() override {
    // For the lambda capture.
    uint64_t interfaceId = this->interfaceId;
    uint16_t methodId = this->methodId;

    auto context = kj::refcounted<LocalCallContext>(kj::mv(message), client->addRef());
    auto promiseAndPipeline = client->call(interfaceId, methodId, kj::addRef(*context));

    auto promise = loop.there(kj::mv(promiseAndPipeline.promise),
        kj::mvCapture(context, [=](kj::Own<LocalCallContext> context) {
          // Do not inline `reader` -- kj::mv on next line may occur first.
          auto reader = context->getResults(1).asReader();
          return Response<ObjectPointer>(reader, kj::mv(context->response));
        }));

    return RemotePromise<ObjectPointer>(
        kj::mv(promise), ObjectPointer::Pipeline(kj::mv(promiseAndPipeline.pipeline)));
  }

  kj::Own<LocalMessage> message;

private:
  const kj::EventLoop& loop;
  uint64_t interfaceId;
  uint16_t methodId;
  kj::Own<const ClientHook> client;
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
  QueuedPipeline(const kj::EventLoop& loop, kj::Promise<kj::Own<const PipelineHook>>&& promise)
      : loop(loop),
        promise(loop.fork(kj::mv(promise))) {}

  kj::Own<const PipelineHook> addRef() const override {
    return kj::addRef(*this);
  }

  kj::Own<const ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const override {
    auto copy = kj::heapArrayBuilder<PipelineOp>(ops.size());
    for (auto& op: ops) {
      copy.add(op);
    }
    return getPipelinedCap(copy.finish());
  }

  kj::Own<const ClientHook> getPipelinedCap(kj::Array<PipelineOp>&& ops) const override;

private:
  const kj::EventLoop& loop;
  kj::ForkedPromise<kj::Own<const PipelineHook>> promise;
};

class QueuedClient final: public ClientHook, public kj::Refcounted {
  // A ClientHook which simply queues calls while waiting for a ClientHook to which to forward
  // them.

public:
  QueuedClient(const kj::EventLoop& loop, kj::Promise<kj::Own<const ClientHook>>&& promise)
      : loop(loop),
        promise(loop.fork(kj::mv(promise))) {}

  Request<ObjectPointer, ObjectPointer> newCall(
      uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) const override {
    auto hook = kj::heap<LocalRequest>(
        loop, interfaceId, methodId, firstSegmentWordSize, kj::addRef(*this));
    auto root = hook->message->getRoot();  // Do not inline `root` -- kj::mv may happen first.
    return Request<ObjectPointer, ObjectPointer>(root, kj::mv(hook));
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              kj::Own<CallContextHook>&& context) const override {
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

      mutable VoidPromiseAndPipeline content;
      // One branch of the fork will use content.promise, the other branch will use
      // content.pipeline.  Neither branch will touch the other's piece, but each needs to clobber
      // its own piece, so we declare this mutable.

      inline CallResultHolder(VoidPromiseAndPipeline&& content): content(kj::mv(content)) {}

      kj::Own<const CallResultHolder> addRef() const { return kj::addRef(*this); }
    };

    // Create a promise for the call initiation.
    kj::ForkedPromise<kj::Own<CallResultHolder>> callResultPromise = loop.fork(loop.there(
        getPromiseForCallForwarding().addBranch(), kj::mvCapture(context,
        [=](kj::Own<CallContextHook>&& context, kj::Own<const ClientHook>&& client){
          return kj::refcounted<CallResultHolder>(
              client->call(interfaceId, methodId, kj::mv(context)));
        })));

    // Create a promise that extracts the pipeline from the call initiation, and construct our
    // QueuedPipeline to chain to it.
    auto pipelinePromise = loop.there(callResultPromise.addBranch(),
        [](kj::Own<const CallResultHolder>&& callResult){
          return kj::mv(callResult->content.pipeline);
        });
    auto pipeline = kj::refcounted<QueuedPipeline>(loop, kj::mv(pipelinePromise));

    // Create a promise that simply chains to the void promise produced by the call initiation.
    auto completionPromise = loop.there(callResultPromise.addBranch(),
        [](kj::Own<const CallResultHolder>&& callResult){
          return kj::mv(callResult->content.promise);
        });

    // OK, now we can actually return our thing.
    return VoidPromiseAndPipeline { kj::mv(completionPromise), kj::mv(pipeline) };
  }

  kj::Maybe<kj::Promise<kj::Own<const ClientHook>>> whenMoreResolved() const override {
    return getPromiseForClientResolution().addBranch();
  }

  kj::Own<const ClientHook> addRef() const override {
    return kj::addRef(*this);
  }

  const void* getBrand() const override {
    return nullptr;
  }

private:
  const kj::EventLoop& loop;

  typedef kj::ForkedPromise<kj::Own<const ClientHook>> ClientHookPromiseFork;

  ClientHookPromiseFork promise;
  // Promise that resolves when we have a new ClientHook to forward to.
  //
  // This fork shall only have two branches:  `promiseForCallForwarding` and
  // `promiseForClientResolution`, in that order.

  kj::Lazy<ClientHookPromiseFork> promiseForCallForwarding;
  // When this promise resolves, each queued call will be forwarded to the real client.  This needs
  // to occur *before* any 'whenMoreResolved()' promises resolve, because we want to make sure
  // previously-queued calls are delivered before any new calls made in response to the resolution.

  kj::Lazy<ClientHookPromiseFork> promiseForClientResolution;
  // whenMoreResolved() returns forks of this promise.  These must resolve *after* queued calls
  // have been initiated (so that any calls made in the whenMoreResolved() handler are correctly
  // delivered after calls made earlier), but *before* any queued calls return (because it might
  // confuse the application if a queued call returns before the capability on which it was made
  // resolves).  Luckily, we know that queued calls will involve, at the very least, an
  // eventLoop.evalLater.

  const ClientHookPromiseFork& getPromiseForCallForwarding() const {
    return promiseForCallForwarding.get([this](kj::SpaceFor<ClientHookPromiseFork>& space) {
      return space.construct(loop.fork(promise.addBranch()));
    });
  }

  const kj::ForkedPromise<kj::Own<const ClientHook>>& getPromiseForClientResolution() const {
    return promiseForClientResolution.get([this](kj::SpaceFor<ClientHookPromiseFork>& space) {
      getPromiseForCallForwarding();  // must be initialized first.
      return space.construct(loop.fork(promise.addBranch()));
    });
  }
};

kj::Own<const ClientHook> QueuedPipeline::getPipelinedCap(kj::Array<PipelineOp>&& ops) const {
  auto clientPromise = loop.there(promise.addBranch(), kj::mvCapture(ops,
      [](kj::Array<PipelineOp>&& ops, kj::Own<const PipelineHook> pipeline) {
        return pipeline->getPipelinedCap(kj::mv(ops));
      }));

  return kj::refcounted<QueuedClient>(loop, kj::mv(clientPromise));
}

// =======================================================================================

class LocalPipeline final: public PipelineHook, public kj::Refcounted {
public:
  inline LocalPipeline(kj::Own<CallContextHook>&& contextParam)
      : context(kj::mv(contextParam)),
        results(context->getResults(1)) {}

  kj::Own<const PipelineHook> addRef() const {
    return kj::addRef(*this);
  }

  kj::Own<const ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const {
    return results.getPipelinedCap(ops);
  }

private:
  kj::Own<CallContextHook> context;
  ObjectPointer::Reader results;
};

class LocalClient final: public ClientHook, public kj::Refcounted {
public:
  LocalClient(const kj::EventLoop& eventLoop, kj::Own<Capability::Server>&& server)
      : eventLoop(eventLoop), server(kj::mv(server)) {}

  Request<ObjectPointer, ObjectPointer> newCall(
      uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) const override {
    auto hook = kj::heap<LocalRequest>(
        eventLoop, interfaceId, methodId, firstSegmentWordSize, kj::addRef(*this));
    auto root = hook->message->getRoot();  // Do not inline `root` -- kj::mv may happen first.
    return Request<ObjectPointer, ObjectPointer>(root, kj::mv(hook));
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              kj::Own<CallContextHook>&& context) const override {
    // We can const-cast the server because we're synchronizing on the event loop.
    auto server = const_cast<Capability::Server*>(this->server.get());

    auto contextPtr = context.get();

    // We don't want to actually dispatch the call synchronously, because:
    // 1) The server may prefer a different EventLoop.
    // 2) If the server is in the same EventLoop, calling it synchronously could be dangerous due
    //    to risk of deadlocks if it happens to take a mutex that the client already holds.  One
    //    of the main goals of message-passing architectures is to avoid this!
    //
    // So, we do an evalLater() here.
    //
    // Note also that QueuedClient depends on this evalLater() to ensure that pipelined calls don't
    // complete before 'whenMoreResolved()' promises resolve.
    auto promise = eventLoop.evalLater(
        [=]() {
          return server->dispatchCall(interfaceId, methodId,
                                      CallContext<ObjectPointer, ObjectPointer>(*contextPtr));
        });

    // We have to fork this promise for the pipeline to receive a copy of the answer.
    auto forked = eventLoop.fork(kj::mv(promise));

    auto pipelinePromise = eventLoop.there(forked.addBranch(), kj::mvCapture(context->addRef(),
        [=](kj::Own<CallContextHook>&& context) -> kj::Own<const PipelineHook> {
          context->releaseParams();
          return kj::refcounted<LocalPipeline>(kj::mv(context));
        }));

    auto completionPromise = eventLoop.there(forked.addBranch(), kj::mvCapture(context->addRef(),
        [=](kj::Own<CallContextHook>&& context) {
          // Nothing to do here.  We just wanted to make sure to hold on to a reference to the
          // context even if the pipeline was discarded.
          //
          // TODO(someday):  We could probably make this less ugly if we had the ability to
          //   convert Promise<Tuple<T, U>> -> Tuple<Promise<T>, Promise<U>>...
        }));

    return VoidPromiseAndPipeline { kj::mv(completionPromise),
        kj::refcounted<QueuedPipeline>(eventLoop, kj::mv(pipelinePromise)) };
  }

  kj::Maybe<kj::Promise<kj::Own<const ClientHook>>> whenMoreResolved() const override {
    return nullptr;
  }

  kj::Own<const ClientHook> addRef() const override {
    return kj::addRef(*this);
  }

  const void* getBrand() const override {
    // We have no need to detect local objects.
    return nullptr;
  }

private:
  const kj::EventLoop& eventLoop;
  kj::Own<Capability::Server> server;
};

}  // namespace

kj::Own<const ClientHook> Capability::Client::makeLocalClient(
    kj::Own<Capability::Server>&& server, const kj::EventLoop& eventLoop) {
  return kj::refcounted<LocalClient>(eventLoop, kj::mv(server));
}

}  // namespace capnp
