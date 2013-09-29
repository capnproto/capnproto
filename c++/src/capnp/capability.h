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

template <typename Answer>
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

template <typename Params, typename Answer>
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

  RemotePromise<Answer> send();
  // Send the call and return a promise for the answer.

private:
  kj::Own<RequestHook> hook;
};

template <typename Answer>
class Response: public Answer::Reader {
  // A completed call.  This class extends a Reader for the call's answer structure.  The Response
  // is move-only -- once it goes out-of-scope, the underlying message will be freed.

public:
  inline Response(typename Answer::Reader reader, kj::Own<ResponseHook>&& hook)
      : Answer::Reader(reader), hook(kj::mv(hook)) {}

private:
  kj::Own<ResponseHook> hook;

  template <typename, typename>
  friend class Request;
};

class Capability::Client {
  // Base type for capability clients.

public:
  explicit Client(kj::Own<ClientHook>&& hook);

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
  kj::Own<ClientHook> hook;
};

// =======================================================================================
// Local capabilities

class CallContextHook;

template <typename T>
class CallContext: public kj::DisallowConstCopy {
  // Wrapper around TypelessCallContext with a specific return type.

public:
  explicit CallContext(kj::Own<CallContextHook> hook);

  typename T::Builder getAnswer();
  typename T::Builder initAnswer();
  typename T::Builder initAnswer(uint size);
  void setAnswer(typename T::Reader value);
  void adoptAnswer(Orphan<T>&& value);
  Orphanage getAnswerOrphanage();
  // Manipulate the answer (return value) payload.  The "Return" message (part of the RPC protocol)
  // will typically be allocated the first time one of these is called.  Some RPC systems may
  // allocate these messages in a limited space (such as a shared memory segment), therefore the
  // application should delay calling these as long as is convenient to do so (but don't delay
  // if doing so would require extra copies later).

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

  bool isCanceled();
  // As an alternative to `allowAsyncCancellation()`, a server can call this to check for
  // cancellation.
  //
  // Keep in mind that if the method is blocking the event loop, the cancel message won't be
  // received, so it is necessary to use `EventLoop::current().runLater()` occasionally.

private:
  kj::Own<CallContextHook> hook;
};

class Capability::Server {
  // Objects implementing a Cap'n Proto interface must subclass this.  Typically, such objects
  // will instead subclass a typed Server interface which will take care of implementing
  // dispatchCall().

public:
  virtual kj::Promise<void> dispatchCall(uint64_t interfaceId, uint16_t methodId,
                                         kj::Own<ObjectPointer::Reader> params,
                                         CallContext<ObjectPointer> context) = 0;
  // Call the given method.  `params` is the input struct, and should be released as soon as it
  // is no longer needed.  `context` may be used to allocate the output struct and deal with
  // cancellation.

  // TODO(soon):  Method which can optionally be overridden to implement Join when the object is
  //   a proxy.
};

Capability::Client makeLocalClient(kj::Own<Capability::Server>&& server);
// Make a client capability that wraps the given server capability.

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

struct TypelessAnswer {
  // Result of a call, before it has been type-wrapped.  Designed to be used as
  // RemotePromise<CallResult>.

  typedef ObjectPointer::Reader Reader;
  // So RemotePromise<CallResult> resolves to Own<ObjectPointer::Reader>.

  class Pipeline {
  public:
    inline explicit Pipeline(kj::Own<PipelineHook>&& hook): hook(kj::mv(hook)) {}

    Pipeline getPointerField(uint16_t pointerIndex) const;
    // Return a new Promise representing a sub-object of the result.  `pointerIndex` is the index
    // of the sub-object within the pointer section of the result (the result must be a struct).
    //
    // TODO(kenton):  On GCC 4.8 / Clang 3.3, use rvalue qualifiers to avoid the need for copies.
    //   Also make `ops` into a Vector to optimize this.

    Capability::Client asCap() const;
    // Expect that the result is a capability and construct a pipelined version of it now.

  private:
    kj::Own<PipelineHook> hook;
    kj::Array<PipelineOp> ops;

    inline Pipeline(kj::Own<PipelineHook>&& hook, kj::Array<PipelineOp>&& ops)
        : hook(kj::mv(hook)), ops(kj::mv(ops)) {}
  };
};

// =======================================================================================
// Hook interfaces which must be implemented by the RPC system.  Applications never call these
// directly; the RPC system implements them and the types defined earlier in this file wrap them.

class RequestHook {
  // Hook interface implemented by RPC system representing a request being built.

public:
  virtual ObjectPointer::Builder getRequest() = 0;
  // Get the request object for this call, to be filled in before sending.

  virtual RemotePromise<TypelessAnswer> send() = 0;
  // Send the call and return a promise for the result.
};

class ResponseHook {
  // Hook interface implemented by RPC system representing a response.
  //
  // At present this class has no methods.  It exists only for garbage collection -- when the
  // ResponseHook is destroyed, the answer can be freed.

public:
};

class PipelineHook {
  // Represents a currently-running call, and implements pipelined requests on its result.

public:
  virtual kj::Own<PipelineHook> addRef() const = 0;
  // Increment this object's reference count.

  virtual kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const = 0;
  // Extract a promised Capability from the answer.
};

class ClientHook {
public:
  virtual Request<ObjectPointer, TypelessAnswer> newCall(
      uint64_t interfaceId, uint16_t methodId) const = 0;
  virtual kj::Promise<void> whenResolved() const = 0;

  virtual kj::Own<ClientHook> addRef() const = 0;
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
  virtual ObjectPointer::Builder getAnswer() = 0;
  virtual void allowAsyncCancellation(bool allow) = 0;
  virtual bool isCanceled() = 0;
};

// =======================================================================================
// Inline implementation details

template <typename Params, typename Answer>
RemotePromise<Answer> Request<Params, Answer>::send() {
  auto typelessPromise = hook->send();

  // Convert the Promise to return the correct response type.
  // Explicitly upcast to kj::Promise to make clear that calling .then() doesn't invalidate the
  // Pipeline part of the RemotePromise.
  auto typedPromise = kj::implicitCast<kj::Promise<Response<TypelessAnswer>>&>(typelessPromise)
      .then([](Response<TypelessAnswer>&& response) -> Response<Answer> {
        return Response<Answer>(response.getAs<Answer>(), kj::mv(response.hook));
      });

  // Wrap the typeless pipeline in a typed wrapper.
  typename Answer::Pipeline typedPipeline(
      kj::mv(kj::implicitCast<TypelessAnswer::Pipeline&>(typelessPromise)));

  return RemotePromise<Answer>(kj::mv(typedPromise), kj::mv(typedPipeline));
}

inline Capability::Client TypelessAnswer::Pipeline::asCap() const {
  return Capability::Client(hook->getPipelinedCap(ops));
}

inline Capability::Client::Client(kj::Own<ClientHook>&& hook): hook(kj::mv(hook)) {}
inline Capability::Client::Client(const Client& other): hook(other.hook->addRef()) {}
inline Capability::Client& Capability::Client::operator=(const Client& other) {
  hook = other.hook->addRef();
  return *this;
}
inline kj::Promise<void> Capability::Client::whenResolved() const {
  return hook->whenResolved();
}

template <typename T>
inline CallContext<T>::CallContext(kj::Own<CallContextHook> hook): hook(kj::mv(hook)) {}
template <typename T>
inline typename T::Builder CallContext<T>::getAnswer() {
  // `template` keyword needed due to: http://llvm.org/bugs/show_bug.cgi?id=17401
  return hook->getAnswer().template getAs<T>();
}
template <typename T>
inline typename T::Builder CallContext<T>::initAnswer() {
  // `template` keyword needed due to: http://llvm.org/bugs/show_bug.cgi?id=17401
  return hook->getAnswer().template initAs<T>();
}
template <typename T>
inline typename T::Builder CallContext<T>::initAnswer(uint size) {
  // `template` keyword needed due to: http://llvm.org/bugs/show_bug.cgi?id=17401
  return hook->getAnswer().template initAs<T>(size);
}
template <typename T>
inline void CallContext<T>::setAnswer(typename T::Reader value) {
  hook->getAnswer().set(value);
}
template <typename T>
inline void CallContext<T>::adoptAnswer(Orphan<T>&& value) {
  hook->getAnswer().adopt(kj::mv(value));
}
template <typename T>
inline Orphanage CallContext<T>::getAnswerOrphanage() {
  return Orphanage::getForMessageContaining(hook->getAnswer());
}
template <typename T>
inline void CallContext<T>::allowAsyncCancellation(bool allow) {
  hook->allowAsyncCancellation(allow);
}
template <typename T>
inline bool CallContext<T>::isCanceled() {
  return hook->isCanceled();
}

}  // namespace capnp

#endif  // CAPNP_CAPABILITY_H_
