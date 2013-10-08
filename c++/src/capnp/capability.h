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

#ifndef CAPNP_CAPABILITY_H_
#define CAPNP_CAPABILITY_H_

#include <kj/async.h>
#include "object.h"

namespace capnp {

template <typename Results>
class Response;

template <typename T>
class RemotePromise: public kj::Promise<Response<T>>, public T::Pipeline {
  // A Promise which supports pipelined calls.  T is typically a struct type.  T must declare
  // an inner "mix-in" type "Pipeline" which implements pipelining; RemotePromise simply
  // multiply-inherits that type along with Promise<Response<T>>.  T::Pipeline must be movable,
  // but does not need to be copyable (i.e. just like Promise<T>).
  //
  // The promise is for an owned pointer so that the RPC system can allocate the MessageReader
  // itself.

public:
  inline RemotePromise(kj::Promise<Response<T>>&& promise, typename T::Pipeline&& pipeline)
      : kj::Promise<Response<T>>(kj::mv(promise)),
        T::Pipeline(kj::mv(pipeline)) {}
  inline RemotePromise(decltype(nullptr))
      : kj::Promise<Response<T>>(nullptr) {}
  KJ_DISALLOW_COPY(RemotePromise);
  RemotePromise(RemotePromise&& other) = default;
  RemotePromise& operator=(RemotePromise&& other) = default;
};

struct Capability {
  // A capability without type-safe methods.  Typed capability clients wrap `Client` and typed
  // capability servers subclass `Server` to dispatch to the regular, typed methods.

  class Client;
  class Server;
};

// =======================================================================================

class RequestHook;
class ResponseHook;
class PipelineHook;
class ClientHook;

template <typename Params, typename Results>
class Request: public Params::Builder {
  // A call that hasn't been sent yet.  This class extends a Builder for the call's "Params"
  // structure with a method send() that actually sends it.
  //
  // Given a Cap'n Proto method `foo(a :A, b :B): C`, the generated client interface will have
  // a method `Request<FooParams, C> startFoo()` (as well as a convenience method
  // `RemotePromise<C> foo(A::Reader a, B::Reader b)`).

public:
  inline Request(typename Params::Builder builder, kj::Own<RequestHook>&& hook)
      : Params::Builder(builder), hook(kj::mv(hook)) {}

  RemotePromise<Results> send();
  // Send the call and return a promise for the results.

private:
  kj::Own<RequestHook> hook;

  friend class Capability::Client;
};

template <typename Results>
class Response: public Results::Reader {
  // A completed call.  This class extends a Reader for the call's answer structure.  The Response
  // is move-only -- once it goes out-of-scope, the underlying message will be freed.

public:
  inline Response(typename Results::Reader reader, kj::Own<ResponseHook>&& hook)
      : Results::Reader(reader), hook(kj::mv(hook)) {}

private:
  kj::Own<ResponseHook> hook;

  template <typename, typename>
  friend class Request;
};

class Capability::Client {
  // Base type for capability clients.

public:
  explicit Client(kj::Own<const ClientHook>&& hook);

  Client(const Client& other);
  Client& operator=(const Client& other);
  // Copies by reference counting.  Warning:  Refcounting is slow due to atomic ops.  Try to only
  // use move instead.

  Client(Client&&) = default;
  Client& operator=(Client&&) = default;
  // Move is fast.

  kj::Promise<void> whenResolved() const;
  // If the capability is actually only a promise, the returned promise resolves once the
  // capability itself has resolved to its final destination (or propagates the exception if
  // the capability promise is rejected).  This is mainly useful for error-checking in the case
  // where no calls are being made.  There is no reason to wait for this before making calls; if
  // the capability does not resolve, the call results will propagate the error.

  // TODO(soon):  method(s) for Join

private:
  kj::Own<const ClientHook> hook;

protected:
  Client() = default;

  template <typename Params, typename Results>
  Request<Params, Results> newCall(uint64_t interfaceId, uint16_t methodId,
                                   uint firstSegmentWordSize);
};

// =======================================================================================
// Local capabilities

class CallContextHook;

template <typename Params, typename Results>
class CallContext: public kj::DisallowConstCopy {
  // Wrapper around TypelessCallContext with a specific return type.
  //
  // Methods of this class may only be called from within the server's event loop, not from other
  // threads.

public:
  explicit CallContext(CallContextHook& hook);

  typename Params::Reader getParams();
  // Get the params payload.

  void releaseParams();
  // Release the params payload.  getParams() will throw an exception after this is called.
  // Releasing the params may allow the RPC system to free up buffer space to handle other
  // requests.  Long-running asynchronous methods should try to call this as early as is
  // convenient.

  typename Results::Builder getResults(uint firstSegmentWordSize = 0);
  typename Results::Builder initResults(uint firstSegmentWordSize = 0);
  void setResults(typename Results::Reader value);
  void adoptResults(Orphan<Results>&& value);
  Orphanage getResultsOrphanage(uint firstSegmentWordSize = 0);
  // Manipulate the results payload.  The "Return" message (part of the RPC protocol) will
  // typically be allocated the first time one of these is called.  Some RPC systems may
  // allocate these messages in a limited space (such as a shared memory segment), therefore the
  // application should delay calling these as long as is convenient to do so (but don't delay
  // if doing so would require extra copies later).
  //
  // `firstSegmentWordSize` indicates the suggested size of the message's first segment.  This
  // is a hint only.  If not specified, the system will decide on its own.

  void allowAsyncCancellation(bool allow = true);
  // Indicate that it is OK for the RPC system to discard its Promise for this call's result if
  // the caller cancels the call, thereby transitively canceling any asynchronous operations the
  // call implementation was performing.  This is not done by default because it could represent a
  // security risk:  applications must be carefully written to ensure that they do not end up in
  // a bad state if an operation is canceled at an arbitrary point.  However, for long-running
  // method calls that hold significant resources, prompt cancellation is often useful.
  //
  // You can also switch back to disallowing cancellation by passing `false` as the argument.
  //
  // Keep in mind that asynchronous cancellation cannot occur while the method is synchronously
  // executing on a local thread.  The method must perform an asynchronous operation or call
  // `EventLoop::current().runLater()` to yield control.
  //
  // TODO(soon):  This doesn't work for local calls, because there's no one to own the object
  //   in the meantime.  What do we do about that?  Is the security issue here actually a real
  //   threat?  Maybe we can just always enable cancellation.  After all, you need to be fault
  //   tolerant and exception-safe, and those are pretty similar to being cancel-tolerant, though
  //   with less direct control by the attacker...

  bool isCanceled();
  // As an alternative to `allowAsyncCancellation()`, a server can call this to check for
  // cancellation.
  //
  // Keep in mind that if the method is blocking the event loop, the cancel message won't be
  // received, so it is necessary to use `EventLoop::current().runLater()` occasionally.

private:
  CallContextHook* hook;

  friend class Capability::Server;
};

class Capability::Server {
  // Objects implementing a Cap'n Proto interface must subclass this.  Typically, such objects
  // will instead subclass a typed Server interface which will take care of implementing
  // dispatchCall().

public:
  virtual kj::Promise<void> dispatchCall(uint64_t interfaceId, uint16_t methodId,
                                         CallContext<ObjectPointer, ObjectPointer> context) = 0;
  // Call the given method.  `params` is the input struct, and should be released as soon as it
  // is no longer needed.  `context` may be used to allocate the output struct and deal with
  // cancellation.

  // TODO(soon):  Method which can optionally be overridden to implement Join when the object is
  //   a proxy.

protected:
  template <typename Params, typename Results>
  CallContext<Params, Results> internalGetTypedContext(
      CallContext<ObjectPointer, ObjectPointer> typeless);
  kj::Promise<void> internalUnimplemented(const char* actualInterfaceName,
                                          uint64_t requestedTypeId);
  kj::Promise<void> internalUnimplemented(const char* interfaceName,
                                          uint64_t typeId, uint16_t methodId);
  kj::Promise<void> internalUnimplemented(const char* interfaceName, const char* methodName,
                                          uint64_t typeId, uint16_t methodId);
};

kj::Own<const ClientHook> makeLocalClient(kj::Own<Capability::Server>&& server,
                                          kj::EventLoop& eventLoop = kj::EventLoop::current());
// Make a client capability that wraps the given server capability.  The server's methods will
// only be executed in the given EventLoop, regardless of what thread calls the client's methods.

// =======================================================================================

struct PipelineOp {
  enum Type {
    GET_POINTER_FIELD

    // There may be other types in the future...
  };

  Type type;
  union {
    uint16_t pointerIndex;  // for GET_POINTER_FIELD
  };
};

struct TypelessResults {
  // Result of a call, before it has been type-wrapped.  Designed to be used as
  // RemotePromise<CallResult>.

  typedef ObjectPointer::Reader Reader;
  // So RemotePromise<CallResult> resolves to Own<ObjectPointer::Reader>.

  class Pipeline {
  public:
    inline explicit Pipeline(kj::Own<const PipelineHook>&& hook): hook(kj::mv(hook)) {}

    Pipeline getPointerField(uint16_t pointerIndex) const;
    // Return a new Promise representing a sub-object of the result.  `pointerIndex` is the index
    // of the sub-object within the pointer section of the result (the result must be a struct).
    //
    // TODO(kenton):  On GCC 4.8 / Clang 3.3, use rvalue qualifiers to avoid the need for copies.
    //   Also make `ops` into a Vector to optimize this.

    Capability::Client asCap() const;
    // Expect that the result is a capability and construct a pipelined version of it now.

  private:
    kj::Own<const PipelineHook> hook;
    kj::Array<PipelineOp> ops;

    inline Pipeline(kj::Own<const PipelineHook>&& hook, kj::Array<PipelineOp>&& ops)
        : hook(kj::mv(hook)), ops(kj::mv(ops)) {}
  };
};

// =======================================================================================
// Hook interfaces which must be implemented by the RPC system.  Applications never call these
// directly; the RPC system implements them and the types defined earlier in this file wrap them.

class RequestHook {
  // Hook interface implemented by RPC system representing a request being built.

public:
  virtual RemotePromise<TypelessResults> send() = 0;
  // Send the call and return a promise for the result.
};

class ResponseHook {
  // Hook interface implemented by RPC system representing a response.
  //
  // At present this class has no methods.  It exists only for garbage collection -- when the
  // ResponseHook is destroyed, the results can be freed.

public:
  virtual ~ResponseHook() noexcept(false);
  // Just here to make sure the type is dynamic.
};

class PipelineHook {
  // Represents a currently-running call, and implements pipelined requests on its result.

public:
  virtual kj::Own<const PipelineHook> addRef() const = 0;
  // Increment this object's reference count.

  virtual kj::Own<const ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const = 0;
  // Extract a promised Capability from the results.

  virtual kj::Own<const ClientHook> getPipelinedCap(kj::Array<PipelineOp>&& ops) const {
    // Version of getPipelinedCap() passing the array by move.  May avoid a copy in some cases.
    // Default implementation just calls the other version.
    return getPipelinedCap(ops.asPtr());
  }
};

class ClientHook {
public:
  virtual Request<ObjectPointer, TypelessResults> newCall(
      uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) const = 0;
  // Start a new call, allowing the client to allocate request/response objects as it sees fit.
  // This version is used when calls are made from application code in the local process.

  struct VoidPromiseAndPipeline {
    kj::Promise<void> promise;
    kj::Own<const PipelineHook> pipeline;
  };

  virtual VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                                      CallContextHook& context) const = 0;
  // Call the object, but the caller controls allocation of the request/response objects.  If the
  // callee insists on allocating this objects itself, it must make a copy.  This version is used
  // when calls come in over the network via an RPC system.  During the call, the context object
  // may be used from any thread so long as it is only used from one thread at a time.  Once the
  // returned promise resolves or has been canceled, the context can no longer be used.  The caller
  // must not allow the ClientHook to be destroyed until the call completes or is canceled.
  //
  // The call must not begin synchronously, as the caller may hold arbitrary mutexes.

  virtual kj::Maybe<kj::Promise<kj::Own<const ClientHook>>> whenMoreResolved() const = 0;
  // If this client is a settled reference (not a promise), return nullptr.  Otherwise, return a
  // promise that eventually resolves to a new client that is closer to being the final, settled
  // client.  Calling this repeatedly should eventually produce a settled client.

  kj::Promise<void> whenResolved() const;
  // Repeatedly calls whenMoreResolved() until it returns nullptr.

  virtual kj::Own<const ClientHook> addRef() const = 0;
  // Return a new reference to the same capability.

  virtual void* getBrand() const = 0;
  // Returns a void* that identifies who made this client.  This can be used by an RPC adapter to
  // discover when a capability it needs to marshal is one that it created in the first place, and
  // therefore it can transfer the capability without proxying.
};

class CallContextHook {
  // Hook interface implemented by RPC system to manage a call on the server side.  See
  // CallContext<T>.

public:
  virtual ObjectPointer::Reader getParams() = 0;
  virtual void releaseParams() = 0;
  virtual ObjectPointer::Builder getResults(uint firstSegmentWordSize) = 0;
  virtual void allowAsyncCancellation(bool allow) = 0;
  virtual bool isCanceled() = 0;

  virtual Response<ObjectPointer> getResponseForPipeline() = 0;
  // Get a copy or reference to the response which will be used to execute pipelined calls.  This
  // will be called no more than once, just after the server implementation successfully returns
  // from the call.
};

// =======================================================================================
// Inline implementation details

template <typename Params, typename Results>
RemotePromise<Results> Request<Params, Results>::send() {
  auto typelessPromise = hook->send();

  // Convert the Promise to return the correct response type.
  // Explicitly upcast to kj::Promise to make clear that calling .then() doesn't invalidate the
  // Pipeline part of the RemotePromise.
  auto typedPromise = kj::implicitCast<kj::Promise<Response<TypelessResults>>&>(typelessPromise)
      .then([](Response<TypelessResults>&& response) -> Response<Results> {
        return Response<Results>(response.getAs<Results>(), kj::mv(response.hook));
      });

  // Wrap the typeless pipeline in a typed wrapper.
  typename Results::Pipeline typedPipeline(
      kj::mv(kj::implicitCast<TypelessResults::Pipeline&>(typelessPromise)));

  return RemotePromise<Results>(kj::mv(typedPromise), kj::mv(typedPipeline));
}

inline Capability::Client TypelessResults::Pipeline::asCap() const {
  return Capability::Client(hook->getPipelinedCap(ops));
}

inline Capability::Client::Client(kj::Own<const ClientHook>&& hook): hook(kj::mv(hook)) {}
inline Capability::Client::Client(const Client& other): hook(other.hook->addRef()) {}
inline Capability::Client& Capability::Client::operator=(const Client& other) {
  hook = other.hook->addRef();
  return *this;
}
inline kj::Promise<void> Capability::Client::whenResolved() const {
  return hook->whenResolved();
}
template <typename Params, typename Results>
inline Request<Params, Results> Capability::Client::newCall(
    uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) {
  auto typeless = hook->newCall(interfaceId, methodId, firstSegmentWordSize);
  return Request<Params, Results>(typeless.template getAs<Params>(), kj::mv(typeless.hook));
}

template <typename Params, typename Results>
inline CallContext<Params, Results>::CallContext(CallContextHook& hook): hook(&hook) {}
template <typename Params, typename Results>
inline typename Params::Reader CallContext<Params, Results>::getParams() {
  return hook->getParams().template getAs<Params>();
}
template <typename Params, typename Results>
inline void CallContext<Params, Results>::releaseParams() {
  hook->releaseParams();
}
template <typename Params, typename Results>
inline typename Results::Builder CallContext<Params, Results>::getResults(
    uint firstSegmentWordSize) {
  // `template` keyword needed due to: http://llvm.org/bugs/show_bug.cgi?id=17401
  return hook->getResults(firstSegmentWordSize).template getAs<Results>();
}
template <typename Params, typename Results>
inline typename Results::Builder CallContext<Params, Results>::initResults(
    uint firstSegmentWordSize) {
  // `template` keyword needed due to: http://llvm.org/bugs/show_bug.cgi?id=17401
  return hook->getResults(firstSegmentWordSize).template initAs<Results>();
}
template <typename Params, typename Results>
inline void CallContext<Params, Results>::setResults(typename Results::Reader value) {
  hook->getResults(value.totalSizeInWords() + 1).set(value);
}
template <typename Params, typename Results>
inline void CallContext<Params, Results>::adoptResults(Orphan<Results>&& value) {
  hook->getResults(0).adopt(kj::mv(value));
}
template <typename Params, typename Results>
inline Orphanage CallContext<Params, Results>::getResultsOrphanage(uint firstSegmentWordSize) {
  return Orphanage::getForMessageContaining(hook->getResults(firstSegmentWordSize));
}
template <typename Params, typename Results>
inline void CallContext<Params, Results>::allowAsyncCancellation(bool allow) {
  hook->allowAsyncCancellation(allow);
}
template <typename Params, typename Results>
inline bool CallContext<Params, Results>::isCanceled() {
  return hook->isCanceled();
}

template <typename Params, typename Results>
CallContext<Params, Results> Capability::Server::internalGetTypedContext(
    CallContext<ObjectPointer, ObjectPointer> typeless) {
  return CallContext<Params, Results>(*typeless.hook);
}

}  // namespace capnp

#endif  // CAPNP_CAPABILITY_H_
