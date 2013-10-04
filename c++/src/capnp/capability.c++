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

// =======================================================================================

namespace {

class LocalResponse final: public ResponseHook {
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
      response = kj::heap<LocalResponse>(firstSegmentWordSize);
    }
    return response->message.getRoot<ObjectPointer>();
  }
  void allowAsyncCancellation(bool allow) override {
    // ignored for local calls
  }
  bool isCanceled() override {
    return false;
  }

  kj::Own<MallocMessageBuilder> request;
  kj::Own<LocalResponse> response;
  kj::Own<const ClientHook> clientRef;
};

class LocalPipelinedClient final: public ClientHook, public kj::Refcounted {
public:
  LocalPipelinedClient(kj::Promise<kj::Own<const ClientHook>> promise)
      : innerPromise(promise.then([this](kj::Own<const ClientHook>&& resolution) {
          auto lock = state.lockExclusive();

          auto oldState = kj::mv(*lock);
          for (auto& call: oldState.pending) {
            call.fulfiller->fulfill(resolution->call(
                call.interfaceId, call.methodId, call.context).promise);
          }
          for (auto& notify: oldState.notifyOnResolution) {
            notify->fulfill(resolution->addRef());
          }

          lock->resolution = kj::mv(resolution);
        }, [this](kj::Exception&& exception) {
          auto lock = state.lockExclusive();

          auto oldState = kj::mv(*lock);
          for (auto& call: oldState.pending) {
            call.fulfiller->reject(kj::Exception(exception));
          }
          for (auto& notify: oldState.notifyOnResolution) {
            notify->reject(kj::Exception(exception));
          }

          lock->exception = kj::mv(exception);
        })) {}

  Request<ObjectPointer, TypelessResults> newCall(
      uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) const override {
    auto lock = state.lockExclusive();
    KJ_IF_MAYBE(r, lock->resolution) {
      return r->newCall(interfaceId, methodId, firstSegmentWordSize);
    } else {
      // TODO(now)
    }
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              CallContext<ObjectPointer, ObjectPointer> context) const override {
    auto lock = state.lockExclusive();
    KJ_IF_MAYBE(r, lock->resolution) {
      return r->call(interfaceId, methodId, context);
    } else {
      lock->pending.add(PendingCall { interfaceId, methodId, context });
    }
  }

  kj::Maybe<kj::Promise<kj::Own<const ClientHook>>> whenMoreResolved() const override {
    auto lock = state.lockExclusive();
    KJ_IF_MAYBE(r, lock->resolution) {
      // Already resolved.
      return kj::Promise<kj::Own<const ClientHook>>(r->addRef());
    } else {
      auto pair = kj::newPromiseAndFulfiller<kj::Own<const ClientHook>>();
      lock->notifyOnResolution.add(kj::mv(pair.fulfiller));
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
    CallContext<ObjectPointer, ObjectPointer> context;
    kj::Own<kj::PromiseFulfiller<void>> fulfiller;
  };

  struct State {
    kj::Maybe<kj::Own<const ClientHook>> resolution;
    kj::Vector<PendingCall> pending;
    kj::Vector<kj::Own<kj::PromiseFulfiller<kj::Own<const ClientHook>>>> notifyOnResolution;
  };
  kj::MutexGuarded<State> state;

  kj::Promise<void> innerPromise;
};

class LocalPipeline final: public PipelineHook, public kj::Refcounted {
public:
  kj::Own<const PipelineHook> addRef() const override {
    return kj::addRef(*this);
  }

  kj::Own<const ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const override {

  }

private:
  struct Waiter {

  };

  struct State {
    kj::Vector<kj::Own<kj::PromiseFulfiller<kj::Own<const ClientHook>>>> notifyOnResolution;
  };
  kj::MutexGuarded<State> state;
};

class LocalRequest final: public RequestHook {
public:
  inline LocalRequest(kj::EventLoop& eventLoop, const Capability::Server* server,
                      uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize,
                      kj::Own<const ClientHook> clientRef)
      : message(kj::heap<MallocMessageBuilder>(
            firstSegmentWordSize == 0 ? SUGGESTED_FIRST_SEGMENT_WORDS : firstSegmentWordSize)),
        eventLoop(eventLoop), server(server), interfaceId(interfaceId), methodId(methodId),
        clientRef(kj::mv(clientRef)) {}

  RemotePromise<TypelessResults> send() override {
    // For the lambda capture.
    // We can const-cast the server pointer because we are synchronizing to its event loop here.
    Capability::Server* server = const_cast<Capability::Server*>(this->server);
    uint64_t interfaceId = this->interfaceId;
    uint16_t methodId = this->methodId;

    auto context = kj::heap<LocalCallContext>(kj::mv(message), kj::mv(clientRef));
    auto promise = eventLoop.evalLater(
      kj::mvCapture(context, [=](kj::Own<LocalCallContext> context) {
        return server->dispatchCall(interfaceId, methodId,
                                    CallContext<ObjectPointer, ObjectPointer>(*context))
            .then(kj::mvCapture(context, [=](kj::Own<LocalCallContext> context) {
              return Response<TypelessResults>(context->getResults(1).asReader(),
                                               kj::mv(context->response));
            }));
      }));
    return RemotePromise<TypelessResults>(
        kj::mv(promise),
        TypelessResults::Pipeline(kj::heap<LocalPipeline>()));
  }

  kj::Own<MallocMessageBuilder> message;

private:
  kj::EventLoop& eventLoop;
  const Capability::Server* server;
  uint64_t interfaceId;
  uint16_t methodId;
  kj::Own<const ClientHook> clientRef;
};

class LocalClient final: public ClientHook, public kj::Refcounted {
public:
  LocalClient(kj::EventLoop& eventLoop, kj::Own<Capability::Server>&& server)
      : eventLoop(eventLoop), server(kj::mv(server)) {}

  Request<ObjectPointer, TypelessResults> newCall(
      uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) const override {
    auto hook = kj::heap<LocalRequest>(
        eventLoop, server, interfaceId, methodId, firstSegmentWordSize, kj::addRef(*this));
    return Request<ObjectPointer, TypelessResults>(
        hook->message->getRoot<ObjectPointer>(), kj::mv(hook));
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              CallContext<ObjectPointer, ObjectPointer> context) const override {
    // We can const-cast the server because we're synchronizing on the event loop.
    auto server = const_cast<Capability::Server*>(this->server.get());
    auto promise = eventLoop.evalLater([=]() mutable {
      return server->dispatchCall(interfaceId, methodId, context);
    });

    return VoidPromiseAndPipeline { kj::mv(promise),
        TypelessResults::Pipeline(kj::heap<LocalPipeline>()) };
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
