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

#include "capability.h"
#include "message.h"
#include <kj/refcount.h>
#include <kj/debug.h>
#include <kj/vector.h>
#include <kj/one-of.h>

namespace capnp {

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

TypelessResults::Pipeline TypelessResults::Pipeline::getPointerField(
    uint16_t pointerIndex) const {
  auto newOps = kj::heapArray<PipelineOp>(ops.size() + 1);
  for (auto i: kj::indices(ops)) {
    newOps[i] = ops[i];
  }
  auto& newOp = newOps[ops.size()];
  newOp.type = PipelineOp::GET_POINTER_FIELD;
  newOp.pointerIndex = pointerIndex;

  return Pipeline(hook->addRef(), kj::mv(newOps));
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

// =======================================================================================

class LocalResponse final: public ResponseHook, public kj::Refcounted {
public:
  LocalResponse(uint sizeHint)
      : message(sizeHint == 0 ? SUGGESTED_FIRST_SEGMENT_WORDS : sizeHint) {}

  MallocMessageBuilder message;
};

class LocalCallContext final: public CallContextHook {
public:
  LocalCallContext(kj::Own<MallocMessageBuilder>&& request, kj::Own<const ClientHook> clientRef)
      : request(kj::mv(request)), clientRef(kj::mv(clientRef)) {}

  ObjectPointer::Reader getParams() override {
    return request->getRoot<ObjectPointer>();
  }
  void releaseParams() override {
    request = nullptr;
  }
  ObjectPointer::Builder getResults(uint firstSegmentWordSize) override {
    if (!response) {
      response = kj::refcounted<LocalResponse>(firstSegmentWordSize);
    }
    return response->message.getRoot<ObjectPointer>();
  }
  void allowAsyncCancellation(bool allow) override {
    // ignored for local calls
  }
  bool isCanceled() override {
    return false;
  }
  Response<ObjectPointer> getResponseForPipeline() override {
    auto reader = getResults(1);  // Needs to be a separate line since it may allocate the response.
    return Response<ObjectPointer>(reader, kj::addRef(*response));
  }

  kj::Own<MallocMessageBuilder> request;
  kj::Own<LocalResponse> response;
  kj::Own<const ClientHook> clientRef;
};

class LocalRequest final: public RequestHook {
public:
  inline LocalRequest(uint64_t interfaceId, uint16_t methodId,
                      uint firstSegmentWordSize, kj::Own<const ClientHook> client)
      : message(kj::heap<MallocMessageBuilder>(
            firstSegmentWordSize == 0 ? SUGGESTED_FIRST_SEGMENT_WORDS : firstSegmentWordSize)),
        interfaceId(interfaceId), methodId(methodId), client(kj::mv(client)) {}

  RemotePromise<TypelessResults> send() override {
    // For the lambda capture.
    uint64_t interfaceId = this->interfaceId;
    uint16_t methodId = this->methodId;

    auto context = kj::heap<LocalCallContext>(kj::mv(message), kj::mv(client));
    auto promiseAndPipeline = client->call(interfaceId, methodId, *context);

    auto promise = promiseAndPipeline.promise.then(
        kj::mvCapture(context, [=](kj::Own<LocalCallContext> context) {
          return Response<TypelessResults>(context->getResults(1).asReader(),
                                           kj::mv(context->response));
        }));

    return RemotePromise<TypelessResults>(
        kj::mv(promise), TypelessResults::Pipeline(kj::mv(promiseAndPipeline.pipeline)));
  }

  kj::Own<MallocMessageBuilder> message;

private:
  uint64_t interfaceId;
  uint16_t methodId;
  kj::Own<const ClientHook> client;
};

// =======================================================================================

namespace {

class BrokenPipeline final: public PipelineHook, public kj::Refcounted {
public:
  BrokenPipeline(const kj::Exception& exception): exception(exception) {}

  kj::Own<const PipelineHook> addRef() const override {
    return kj::addRef(*this);
  }

  kj::Own<const ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const override;

private:
  kj::Exception exception;
};

class BrokenClient final: public ClientHook, public kj::Refcounted {
public:
  BrokenClient(const kj::Exception& exception): exception(exception) {}

  Request<ObjectPointer, TypelessResults> newCall(
      uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) const override {
    auto hook = kj::heap<LocalRequest>(
        interfaceId, methodId, firstSegmentWordSize, kj::addRef(*this));
    return Request<ObjectPointer, TypelessResults>(
        hook->message->getRoot<ObjectPointer>(), kj::mv(hook));
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              CallContextHook& context) const override {
    return VoidPromiseAndPipeline { kj::cp(exception), kj::heap<BrokenPipeline>(exception) };
  }

  kj::Maybe<kj::Promise<kj::Own<const ClientHook>>> whenMoreResolved() const override {
    return kj::Promise<kj::Own<const ClientHook>>(kj::cp(exception));
  }

  kj::Own<const ClientHook> addRef() const override {
    return kj::addRef(*this);
  }

  void* getBrand() const override {
    return nullptr;
  }

private:
  kj::Exception exception;
};

kj::Own<const ClientHook> BrokenPipeline::getPipelinedCap(
    kj::ArrayPtr<const PipelineOp> ops) const {
  return kj::heap<BrokenClient>(exception);
}

// =======================================================================================
// Call queues
//
// These classes handle pipelining in the case where calls need to be queued in-memory until some
// local operation completes.

class QueuedPipeline final: public PipelineHook, public kj::Refcounted {
  // A PipelineHook which simply queues calls while waiting for a PipelineHook to which to forward
  // them.

public:
  QueuedPipeline(kj::EventLoop& loop, kj::Promise<kj::Own<const PipelineHook>>&& promise)
      : loop(loop),
        innerPromise(loop.there(kj::mv(promise), [this](kj::Own<const PipelineHook>&& resolution) {
          auto lock = state.lockExclusive();
          auto oldState = kj::mv(lock->get<Waiting>());

          for (auto& waiter: oldState) {
            waiter.fulfiller->fulfill(resolution->getPipelinedCap(kj::mv(waiter.ops)));
          }

          lock->init<Resolved>(kj::mv(resolution));
        }, [this](kj::Exception&& exception) {
          auto lock = state.lockExclusive();
          auto oldState = kj::mv(lock->get<Waiting>());

          for (auto& waiter: oldState) {
            waiter.fulfiller->reject(kj::cp(exception));
          }

          lock->init<kj::Exception>(kj::mv(exception));
        })) {
    state.getWithoutLock().init<Waiting>();
  }

  kj::Own<const PipelineHook> addRef() const override {
    return kj::addRef(*this);
  }

  kj::Own<const ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const override {
    auto copy = kj::heapArrayBuilder<PipelineOp>(ops.size());
    for (auto& op: ops) {
      copy.add(op);
    }
    return getPipelinedCap(kj::mv(copy));
  }

  kj::Own<const ClientHook> getPipelinedCap(kj::Array<PipelineOp>&& ops) const override;

private:
  struct Waiter {
    kj::Array<PipelineOp> ops;
    kj::Own<kj::PromiseFulfiller<kj::Own<const ClientHook>>> fulfiller;
  };

  typedef kj::Vector<Waiter> Waiting;
  typedef kj::Own<const PipelineHook> Resolved;

  kj::EventLoop& loop;
  kj::Promise<void> innerPromise;
  kj::MutexGuarded<kj::OneOf<Waiting, Resolved, kj::Exception>> state;
};

class QueuedClient final: public ClientHook, public kj::Refcounted {
  // A ClientHook which simply queues calls while waiting for a ClientHook to which to forward
  // them.

public:
  QueuedClient(kj::EventLoop& loop, kj::Promise<kj::Own<const ClientHook>>&& promise)
      : loop(loop),
        innerPromise(loop.there(kj::mv(promise), [this](kj::Own<const ClientHook>&& resolution) {
          // The promised capability has resolved.  Forward all queued calls to it.
          auto lock = state.lockExclusive();
          auto oldState = kj::mv(lock->get<Waiting>());

          // First we want to initiate all the queued calls, and notify the QueuedPipelines to
          // transfer their queues to the new call's own pipeline.  It's important that this all
          // happen before the application receives any notification that the promise resolved,
          // so that any new calls it makes in response to the resolution don't end up being
          // delivered before the previously-queued calls.
          auto realCallPromises = kj::heapArrayBuilder<kj::Promise<void>>(oldState.pending.size());
          for (auto& pendingCall: oldState.pending) {
            auto realCall = resolution->call(
                pendingCall.interfaceId, pendingCall.methodId, *pendingCall.context);
            pendingCall.pipelineFulfiller->fulfill(kj::mv(realCall.pipeline));
            realCallPromises.add(kj::mv(realCall.promise));
          }

          // Fire the "whenMoreResolved" callbacks.
          for (auto& notify: oldState.notifyOnResolution) {
            notify->fulfill(resolution->addRef());
          }

          // For each queued call, chain the pipelined promise to the real promise.  It's important
          // that this happens after the "whenMoreResolved" callbacks because applications may get
          // confused if a pipelined call completes before the promise on which it was made
          // resolves.
          for (uint i: kj::indices(realCallPromises)) {
            oldState.pending[i].fulfiller->fulfill(kj::mv(realCallPromises[i]));
          }

          lock->init<Resolved>(kj::mv(resolution));
        }, [this](kj::Exception&& exception) {
          auto lock = state.lockExclusive();
          auto oldState = kj::mv(lock->get<Waiting>());

          // Reject outer promises before dependent promises.
          for (auto& notify: oldState.notifyOnResolution) {
            notify->reject(kj::cp(exception));
          }
          for (auto& call: oldState.pending) {
            call.fulfiller->reject(kj::cp(exception));
            call.pipelineFulfiller->reject(kj::cp(exception));
          }

          lock->init<kj::Exception>(kj::mv(exception));
        })) {
    state.getWithoutLock().init<Waiting>();
  }

  Request<ObjectPointer, TypelessResults> newCall(
      uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) const override {
    auto lock = state.lockExclusive();
    if (lock->is<Resolved>()) {
      return lock->get<Resolved>()->newCall(interfaceId, methodId, firstSegmentWordSize);
    } else {
      auto hook = kj::heap<LocalRequest>(
          interfaceId, methodId, firstSegmentWordSize, kj::addRef(*this));
      return Request<ObjectPointer, TypelessResults>(
          hook->message->getRoot<ObjectPointer>(), kj::mv(hook));
    }
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              CallContextHook& context) const override {
    auto lock = state.lockExclusive();
    if (lock->is<Resolved>()) {
      return lock->get<Resolved>()->call(interfaceId, methodId, context);
    } else if (lock->is<kj::Exception>()) {
      return VoidPromiseAndPipeline { kj::cp(lock->get<kj::Exception>()),
                                      kj::heap<BrokenPipeline>(lock->get<kj::Exception>()) };
    } else {
      auto pair = kj::newPromiseAndFulfiller<kj::Promise<void>>(loop);
      auto pipelinePromise = kj::newPromiseAndFulfiller<kj::Own<const PipelineHook>>(loop);
      auto pipeline = kj::heap<QueuedPipeline>(loop, kj::mv(pipelinePromise.promise));

      lock->get<Waiting>().pending.add(PendingCall {
          interfaceId, methodId, &context, kj::mv(pair.fulfiller),
          kj::mv(pipelinePromise.fulfiller) });

      // TODO(now):  returned promise must hold a reference to this.
      return VoidPromiseAndPipeline { kj::mv(pair.promise), kj::mv(pipeline) };
    }
  }

  kj::Maybe<kj::Promise<kj::Own<const ClientHook>>> whenMoreResolved() const override {
    auto lock = state.lockExclusive();
    if (lock->is<Resolved>()) {
      // Already resolved.
      return kj::Promise<kj::Own<const ClientHook>>(lock->get<Resolved>()->addRef());
    } else if (lock->is<kj::Exception>()) {
      // Already broken.
      return kj::Promise<kj::Own<const ClientHook>>(kj::Own<const ClientHook>(
          kj::heap<BrokenClient>(lock->get<kj::Exception>())));
    } else {
      // Waiting.
      auto pair = kj::newPromiseAndFulfiller<kj::Own<const ClientHook>>();
      lock->get<Waiting>().notifyOnResolution.add(kj::mv(pair.fulfiller));
      // TODO(now):  returned promise must hold a reference to this.
      return kj::mv(pair.promise);
    }
  }

  kj::Own<const ClientHook> addRef() const override {
    return kj::addRef(*this);
  }

  void* getBrand() const override {
    return nullptr;
  }

private:
  struct PendingCall {
    uint64_t interfaceId;
    uint16_t methodId;
    CallContextHook* context;
    kj::Own<kj::PromiseFulfiller<kj::Promise<void>>> fulfiller;
    kj::Own<kj::PromiseFulfiller<kj::Own<const PipelineHook>>> pipelineFulfiller;
  };

  struct Waiting {
    kj::Vector<PendingCall> pending;
    kj::Vector<kj::Own<kj::PromiseFulfiller<kj::Own<const ClientHook>>>> notifyOnResolution;
  };

  typedef kj::Own<const ClientHook> Resolved;

  kj::EventLoop& loop;
  kj::Promise<void> innerPromise;
  kj::MutexGuarded<kj::OneOf<Waiting, Resolved, kj::Exception>> state;
};

kj::Own<const ClientHook> QueuedPipeline::getPipelinedCap(kj::Array<PipelineOp>&& ops) const {
  auto lock = state.lockExclusive();
  if (lock->is<Resolved>()) {
    return lock->get<Resolved>()->getPipelinedCap(ops);
  } else if (lock->is<kj::Exception>()) {
    return kj::heap<BrokenClient>(lock->get<kj::Exception>());
  } else {
    auto pair = kj::newPromiseAndFulfiller<kj::Own<const ClientHook>>();
    lock->get<Waiting>().add(Waiter { kj::mv(ops), kj::mv(pair.fulfiller) });
    return kj::heap<QueuedClient>(loop, kj::mv(pair.promise));
  }
}

// =======================================================================================

class LocalPipeline final: public PipelineHook, public kj::Refcounted {
public:
  inline LocalPipeline(Response<ObjectPointer> response): response(kj::mv(response)) {}

  kj::Own<const PipelineHook> addRef() const {
    return kj::addRef(*this);
  }

  kj::Own<const ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const {
    return response.getPipelinedCap(ops);
  }

private:
  Response<ObjectPointer> response;
};

class LocalClient final: public ClientHook, public kj::Refcounted {
public:
  LocalClient(kj::EventLoop& eventLoop, kj::Own<Capability::Server>&& server)
      : eventLoop(eventLoop), server(kj::mv(server)) {}

  Request<ObjectPointer, TypelessResults> newCall(
      uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) const override {
    auto hook = kj::heap<LocalRequest>(
        interfaceId, methodId, firstSegmentWordSize, kj::addRef(*this));
    return Request<ObjectPointer, TypelessResults>(
        hook->message->getRoot<ObjectPointer>(), kj::mv(hook));
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              CallContextHook& context) const override {
    // We can const-cast the server because we're synchronizing on the event loop.
    auto server = const_cast<Capability::Server*>(this->server.get());

    auto pipelineFulfiller = kj::newPromiseAndFulfiller<kj::Own<const PipelineHook>>();

    auto promise = eventLoop.evalLater(kj::mvCapture(pipelineFulfiller.fulfiller,
      [=,&context](kj::Own<kj::PromiseFulfiller<kj::Own<const PipelineHook>>>&& fulfiller) mutable {
        return server->dispatchCall(interfaceId, methodId,
                                    CallContext<ObjectPointer, ObjectPointer>(context))
            .then(kj::mvCapture(fulfiller,
                [=,&context](kj::Own<kj::PromiseFulfiller<kj::Own<const PipelineHook>>>&& fulfiller) {
              fulfiller->fulfill(kj::heap<LocalPipeline>(context.getResponseForPipeline()));
            }));
      }));

    return VoidPromiseAndPipeline { kj::mv(promise),
        kj::heap<QueuedPipeline>(eventLoop, kj::mv(pipelineFulfiller.promise)) };
  }

  kj::Maybe<kj::Promise<kj::Own<const ClientHook>>> whenMoreResolved() const override {
    return nullptr;
  }

  kj::Own<const ClientHook> addRef() const override {
    return kj::addRef(*this);
  }

  void* getBrand() const override {
    // We have no need to detect local objects.
    return nullptr;
  }

private:
  kj::EventLoop& eventLoop;
  kj::Own<Capability::Server> server;
};

}  // namespace

kj::Own<const ClientHook> makeLocalClient(kj::Own<Capability::Server>&& server,
                                          kj::EventLoop& eventLoop) {
  return kj::refcounted<LocalClient>(eventLoop, kj::mv(server));
}

}  // namespace capnp
