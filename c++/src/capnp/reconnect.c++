// Copyright (c) 2020 Cloudflare, Inc. and contributors
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

#include "reconnect.h"

namespace capnp {

namespace {

class ReconnectHook final: public ClientHook, public kj::Refcounted {
public:
  ReconnectHook(kj::Function<Capability::Client()> connectParam, bool lazy = false)
      : connect(kj::mv(connectParam)),
        current(lazy ? kj::Maybe<kj::Own<ClientHook>>() : ClientHook::from(connect())) {}

  Request<AnyPointer, AnyPointer> newCall(
      uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
      CallHints hints) override {
    auto result = getCurrent().newCall(interfaceId, methodId, sizeHint, hints);
    AnyPointer::Builder builder = result;
    auto hook = kj::heap<RequestImpl>(kj::addRef(*this), RequestHook::from(kj::mv(result)));
    return { builder, kj::mv(hook) };
  }

  VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                              kj::Own<CallContextHook>&& context, CallHints hints) override {
    auto result = getCurrent().call(interfaceId, methodId, kj::mv(context), hints);
    if (hints.onlyPromisePipeline) {
      // Just in case the callee didn't implement the hint, replace its promise.
      result.promise = kj::NEVER_DONE;

      // TODO(bug): In this case we won't detect cancellation. This is essentially the same
      //   bug as described in `RequestImpl::send()` below, and will need the same solution.
    } else {
      wrap(result.promise);
    }
    return result;
  }

  kj::Maybe<ClientHook&> getResolved() override {
    // We can't let people resolve to the underlying capability because then we wouldn't be able
    // to redirect them later.
    return nullptr;
  }

  kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
    return nullptr;
  }

  kj::Own<ClientHook> addRef() override {
    return kj::addRef(*this);
  }

  const void* getBrand() override {
    return nullptr;
  }

  kj::Maybe<int> getFd() override {
    // It's not safe to return current->getFd() because normally callers wouldn't expect the FD to
    // change or go away over time, but this one could whenever we reconnect. If there's a use
    // case for being able to access the FD here, we'll need a different interface to do it.
    return nullptr;
  }

private:
  kj::Function<Capability::Client()> connect;
  kj::Maybe<kj::Own<ClientHook>> current;
  uint generation = 0;

  template <typename T>
  void wrap(kj::Promise<T>& promise) {
    promise = promise.catch_(
        [self = kj::addRef(*this), startGeneration = generation]
        (kj::Exception&& exception) mutable -> kj::Promise<T> {
      if (exception.getType() == kj::Exception::Type::DISCONNECTED &&
          self->generation == startGeneration) {
        self->generation++;
        KJ_IF_MAYBE(e2, kj::runCatchingExceptions([&]() {
          self->current = ClientHook::from(self->connect());
        })) {
          self->current = newBrokenCap(kj::mv(*e2));
        }
      }
      return kj::mv(exception);
    });
  }

  ClientHook& getCurrent() {
    KJ_IF_MAYBE(c, current) {
      return **c;
    } else {
      return *current.emplace(ClientHook::from(connect()));
    }
  }

  class RequestImpl final: public RequestHook {
  public:
    RequestImpl(kj::Own<ReconnectHook> parent, kj::Own<RequestHook> inner)
        : parent(kj::mv(parent)), inner(kj::mv(inner)) {}

    RemotePromise<AnyPointer> send() override {
      auto result = inner->send();
      // TODO(bug): If the returned promise is dropped, e.g. because the caller only cares about
      //   pipelining, then the DISCONNECTED exception will not be noticed. I suppose we have to
      //   split the promise and hold one branch, but we don't want to prevent cancellation, so
      //   we only want to hold that branch as long as the PipelineHook or some pipelined
      //   capability obtained through it lives. So we need a bunch of custom wrappers for that.
      //   Ugh.
      parent->wrap(result);
      return result;
    }

    kj::Promise<void> sendStreaming() override {
      auto result = inner->sendStreaming();
      parent->wrap(result);
      return result;
    }

    AnyPointer::Pipeline sendForPipeline() override {
      // TODO(bug): This definitely fails to detect disconnects; see comment in send().
      return inner->sendForPipeline();
    }

    const void* getBrand() override {
      return nullptr;
    }

  private:
    kj::Own<ReconnectHook> parent;
    kj::Own<RequestHook> inner;
  };
};

}  // namespace

Capability::Client autoReconnect(kj::Function<Capability::Client()> connect) {
  return Capability::Client(kj::refcounted<ReconnectHook>(kj::mv(connect)));
}

Capability::Client lazyAutoReconnect(kj::Function<Capability::Client()> connect) {
  return Capability::Client(kj::refcounted<ReconnectHook>(kj::mv(connect), true));
}
}  // namespace capnp
