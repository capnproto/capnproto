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

// This file contains extended inline implementation details that are required along with async.h.
// We move this all into a separate file to make async.h more readable.
//
// Non-inline declarations here are defined in async.c++.

#pragma once

#ifndef KJ_ASYNC_H_INCLUDED
#error "Do not include this directly; include kj/async.h."
#include "async.h"  // help IDE parse this file
#endif

KJ_BEGIN_HEADER

namespace kj {
namespace _ {  // private

template <typename T>
class ExceptionOr;

class ExceptionOrValue {
public:
  ExceptionOrValue(bool, Exception&& exception): exception(kj::mv(exception)) {}
  KJ_DISALLOW_COPY(ExceptionOrValue);

  void addException(Exception&& exception) {
    if (this->exception == nullptr) {
      this->exception = kj::mv(exception);
    }
  }

  template <typename T>
  ExceptionOr<T>& as() { return *static_cast<ExceptionOr<T>*>(this); }
  template <typename T>
  const ExceptionOr<T>& as() const { return *static_cast<const ExceptionOr<T>*>(this); }

  Maybe<Exception> exception;

protected:
  // Allow subclasses to have move constructor / assignment.
  ExceptionOrValue() = default;
  ExceptionOrValue(ExceptionOrValue&& other) = default;
  ExceptionOrValue& operator=(ExceptionOrValue&& other) = default;
};

template <typename T>
class ExceptionOr: public ExceptionOrValue {
public:
  ExceptionOr() = default;
  ExceptionOr(T&& value): value(kj::mv(value)) {}
  ExceptionOr(bool, Exception&& exception): ExceptionOrValue(false, kj::mv(exception)) {}
  ExceptionOr(ExceptionOr&&) = default;
  ExceptionOr& operator=(ExceptionOr&&) = default;

  Maybe<T> value;
};

template <typename T>
inline T convertToReturn(ExceptionOr<T>&& result) {
  KJ_IF_MAYBE(value, result.value) {
    KJ_IF_MAYBE(exception, result.exception) {
      throwRecoverableException(kj::mv(*exception));
    }
    return _::returnMaybeVoid(kj::mv(*value));
  } else KJ_IF_MAYBE(exception, result.exception) {
    throwFatalException(kj::mv(*exception));
  } else {
    // Result contained neither a value nor an exception?
    KJ_UNREACHABLE;
  }
}

inline void convertToReturn(ExceptionOr<Void>&& result) {
  // Override <void> case to use throwRecoverableException().

  if (result.value != nullptr) {
    KJ_IF_MAYBE(exception, result.exception) {
      throwRecoverableException(kj::mv(*exception));
    }
  } else KJ_IF_MAYBE(exception, result.exception) {
    throwRecoverableException(kj::mv(*exception));
  } else {
    // Result contained neither a value nor an exception?
    KJ_UNREACHABLE;
  }
}

class TraceBuilder {
  // Helper for methods that build a call trace.
public:
  TraceBuilder(ArrayPtr<void*> space)
      : start(space.begin()), current(space.begin()), limit(space.end()) {}

  inline void add(void* addr) {
    if (current < limit) {
      *current++ = addr;
    }
  }

  inline bool full() const { return current == limit; }

  ArrayPtr<void*> finish() {
    return arrayPtr(start, current);
  }

  String toString();

private:
  void** start;
  void** current;
  void** limit;
};

class Event {
  // An event waiting to be executed.  Not for direct use by applications -- promises use this
  // internally.

public:
  Event();
  Event(kj::EventLoop& loop);
  ~Event() noexcept(false);
  KJ_DISALLOW_COPY(Event);

  void armDepthFirst();
  // Enqueue this event so that `fire()` will be called from the event loop soon.
  //
  // Events scheduled in this way are executed in depth-first order:  if an event callback arms
  // more events, those events are placed at the front of the queue (in the order in which they
  // were armed), so that they run immediately after the first event's callback returns.
  //
  // Depth-first event scheduling is appropriate for events that represent simple continuations
  // of a previous event that should be globbed together for performance.  Depth-first scheduling
  // can lead to starvation, so any long-running task must occasionally yield with
  // `armBreadthFirst()`.  (Promise::then() uses depth-first whereas evalLater() uses
  // breadth-first.)
  //
  // To use breadth-first scheduling instead, use `armBreadthFirst()`.

  void armBreadthFirst();
  // Like `armDepthFirst()` except that the event is placed at the end of the queue.

  void armLast();
  // Enqueues this event to happen after all other events have run to completion and there is
  // really nothing left to do except wait for I/O.

  void disarm();
  // If the event is armed but hasn't fired, cancel it. (Destroying the event does this
  // implicitly.)

  virtual void traceEvent(TraceBuilder& builder) = 0;
  // Build a trace of the callers leading up to this event. `builder` will be populated with
  // "return addresses" of the promise chain waiting on this event. The return addresses may
  // actually the addresses of lambdas passed to .then(), but in any case, feeding them into
  // addr2line should produce useful source code locations.
  //
  // `traceEvent()` may be called from an async signal handler while `fire()` is executing. It
  // must not allocate nor take locks.

  String traceEvent();
  // Helper that builds a trace and stringifies it.

protected:
  virtual Maybe<Own<Event>> fire() = 0;
  // Fire the event.  Possibly returns a pointer to itself, which will be discarded by the
  // caller.  This is the only way that an event can delete itself as a result of firing, as
  // doing so from within fire() will throw an exception.

private:
  friend class kj::EventLoop;
  EventLoop& loop;
  Event* next;
  Event** prev;
  bool firing = false;
};

class PromiseNode {
  // A Promise<T> contains a chain of PromiseNodes tracking the pending transformations.
  //
  // To reduce generated code bloat, PromiseNode is not a template.  Instead, it makes very hacky
  // use of pointers to ExceptionOrValue which actually point to ExceptionOr<T>, but are only
  // so down-cast in the few places that really need to be templated.  Luckily this is all
  // internal implementation details.

public:
  virtual void onReady(Event* event) noexcept = 0;
  // Arms the given event when ready.
  //
  // May be called multiple times. If called again before the event was armed, the old event will
  // never be armed, only the new one. If called again after the event was armed, the new event
  // will be armed immediately. Can be called with nullptr to un-register the existing event.

  virtual void setSelfPointer(Own<PromiseNode>* selfPtr) noexcept;
  // Tells the node that `selfPtr` is the pointer that owns this node, and will continue to own
  // this node until it is destroyed or setSelfPointer() is called again.  ChainPromiseNode uses
  // this to shorten redundant chains.  The default implementation does nothing; only
  // ChainPromiseNode should implement this.

  virtual void get(ExceptionOrValue& output) noexcept = 0;
  // Get the result.  `output` points to an ExceptionOr<T> into which the result will be written.
  // Can only be called once, and only after the node is ready.  Must be called directly from the
  // event loop, with no application code on the stack.

  virtual void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) = 0;
  // Build a trace of this promise chain, showing what it is currently waiting on.
  //
  // Since traces are ordered callee-before-caller, PromiseNode::tracePromise() should typically
  // recurse to its child first, then after the child returns, add itself to the trace.
  //
  // If `stopAtNextEvent` is true, then the trace should stop as soon as it hits a PromiseNode that
  // also implements Event, and should not trace that node or its children. This is used in
  // conjuction with Event::traceEvent(). The chain of Events is often more sparse than the chain
  // of PromiseNodes, because a TransformPromiseNode (which implements .then()) is not itself an
  // Event. TransformPromiseNode instead tells its child node to directly notify its *parent* node
  // when it is ready, and then TransformPromiseNode applies the .then() transformation during the
  // call to .get().
  //
  // So, when we trace the chain of Events backwards, we end up hoping over segments of
  // TransformPromiseNodes (and other similar types). In order to get those added to the trace,
  // each Event must call back down the PromiseNode chain in the opposite direction, using this
  // method.
  //
  // `tracePromise()` may be called from an async signal handler while `get()` is executing. It
  // must not allocate nor take locks.

  template <typename T>
  static Own<PromiseNode> from(T&& promise) {
    // Given a Promise, extract the PromiseNode.
    return kj::mv(promise.node);
  }
  template <typename T>
  static PromiseNode& from(T& promise) {
    // Given a Promise, extract the PromiseNode.
    return *promise.node;
  }
  template <typename T>
  static T to(Own<PromiseNode>&& node) {
    // Construct a Promise from a PromiseNode. (T should be a Promise type.)
    return T(false, kj::mv(node));
  }

protected:
  class OnReadyEvent {
    // Helper class for implementing onReady().

  public:
    void init(Event* newEvent);

    void arm();
    void armBreadthFirst();
    // Arms the event if init() has already been called and makes future calls to init()
    // automatically arm the event.

    inline void traceEvent(TraceBuilder& builder) {
      if (event != nullptr && !builder.full()) event->traceEvent(builder);
    }

  private:
    Event* event = nullptr;
  };
};

// -------------------------------------------------------------------

template <typename T>
inline NeverDone::operator Promise<T>() const {
  return PromiseNode::to<Promise<T>>(neverDone());
}

// -------------------------------------------------------------------

class ImmediatePromiseNodeBase: public PromiseNode {
public:
  ImmediatePromiseNodeBase();
  ~ImmediatePromiseNodeBase() noexcept(false);

  void onReady(Event* event) noexcept override;
  void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) override;
};

template <typename T>
class ImmediatePromiseNode final: public ImmediatePromiseNodeBase {
  // A promise that has already been resolved to an immediate value or exception.

public:
  ImmediatePromiseNode(ExceptionOr<T>&& result): result(kj::mv(result)) {}

  void get(ExceptionOrValue& output) noexcept override {
    output.as<T>() = kj::mv(result);
  }

private:
  ExceptionOr<T> result;
};

class ImmediateBrokenPromiseNode final: public ImmediatePromiseNodeBase {
public:
  ImmediateBrokenPromiseNode(Exception&& exception);

  void get(ExceptionOrValue& output) noexcept override;

private:
  Exception exception;
};

// -------------------------------------------------------------------

class AttachmentPromiseNodeBase: public PromiseNode {
public:
  AttachmentPromiseNodeBase(Own<PromiseNode>&& dependency);

  void onReady(Event* event) noexcept override;
  void get(ExceptionOrValue& output) noexcept override;
  void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) override;

private:
  Own<PromiseNode> dependency;

  void dropDependency();

  template <typename>
  friend class AttachmentPromiseNode;
};

template <typename Attachment>
class AttachmentPromiseNode final: public AttachmentPromiseNodeBase {
  // A PromiseNode that holds on to some object (usually, an Own<T>, but could be any movable
  // object) until the promise resolves.

public:
  AttachmentPromiseNode(Own<PromiseNode>&& dependency, Attachment&& attachment)
      : AttachmentPromiseNodeBase(kj::mv(dependency)),
        attachment(kj::mv<Attachment>(attachment)) {}

  ~AttachmentPromiseNode() noexcept(false) {
    // We need to make sure the dependency is deleted before we delete the attachment because the
    // dependency may be using the attachment.
    dropDependency();
  }

private:
  Attachment attachment;
};

// -------------------------------------------------------------------

#if __GNUC__ >= 8 && !__clang__
// GCC 8's class-memaccess warning rightly does not like the memcpy()'s below, but there's no
// "legal" way for us to extract the contetn of a PTMF so too bad.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

template <typename T, typename ReturnType, typename... ParamTypes>
void* getMethodStartAddress(T& obj, ReturnType (T::*method)(ParamTypes...));
template <typename T, typename ReturnType, typename... ParamTypes>
void* getMethodStartAddress(const T& obj, ReturnType (T::*method)(ParamTypes...) const);
// Given an object and a pointer-to-method, return the start address of the method's code. The
// intent is that this address can be used in a trace; addr2line should map it to the start of
// the function's definition. For virtual methods, this does a vtable lookup on `obj` to determine
// the address of the specific implementation (otherwise, `obj` wouldn't be needed).
//
// Note that if the method is overloaded or is a template, you will need to explicitly specify
// the param and return types, otherwise the compiler won't know which overload / template
// specialization you are requesting.

class PtmfHelper {
  // This class is a private helper for GetFunctorStartAddress and getMethodStartAddress(). The
  // class represents the internal representation of a pointer-to-member-function.

  template <typename... ParamTypes>
  friend struct GetFunctorStartAddress;
  template <typename T, typename ReturnType, typename... ParamTypes>
  friend void* getMethodStartAddress(T& obj, ReturnType (T::*method)(ParamTypes...));
  template <typename T, typename ReturnType, typename... ParamTypes>
  friend void* getMethodStartAddress(const T& obj, ReturnType (T::*method)(ParamTypes...) const);

#if __GNUG__

  void* ptr;
  ptrdiff_t adj;
  // Layout of a pointer-to-member-function used by GCC and compatible compilers.

  void* apply(const void* obj) {
#if defined(__arm__) || defined(__mips__) || defined(__aarch64__)
    if (adj & 1) {
      ptrdiff_t voff = (ptrdiff_t)ptr;
#else
    ptrdiff_t voff = (ptrdiff_t)ptr;
    if (voff & 1) {
      voff &= ~1;
#endif
      return *(void**)(*(char**)obj + voff);
    } else {
      return ptr;
    }
  }

#define BODY \
    PtmfHelper result; \
    static_assert(sizeof(p) == sizeof(result), "unknown ptmf layout"); \
    memcpy(&result, &p, sizeof(result)); \
    return result

#else  // __GNUG__

  void* apply(const void* obj) { return nullptr; }
  // TODO(port):  PTMF instruction address extraction

#define BODY return PtmfHelper{}

#endif  // __GNUG__, else

  template <typename R, typename C, typename... P, typename F>
  static PtmfHelper from(F p) { BODY; }
  // Create a PtmfHelper from some arbitrary pointer-to-member-function which is not
  // overloaded nor a template. In this case the compiler is able to deduce the full function
  // signature directly given the name since there is only one function with that name.

  template <typename R, typename C, typename... P>
  static PtmfHelper from(R (C::*p)(NoInfer<P>...)) { BODY; }
  template <typename R, typename C, typename... P>
  static PtmfHelper from(R (C::*p)(NoInfer<P>...) const) { BODY; }
  // Create a PtmfHelper from some poniter-to-member-function which is a template. In this case
  // the function must match exactly the containing type C, return type R, and parameter types P...
  // GetFunctorStartAddress normally specifies exactly the correct C and R, but can only make a
  // guess at P. Luckily, if the function parameters are template parameters then it's not
  // necessary to be precise about P.
#undef BODY
};

#if __GNUC__ >= 8 && !__clang__
#pragma GCC diagnostic pop
#endif

template <typename T, typename ReturnType, typename... ParamTypes>
void* getMethodStartAddress(T& obj, ReturnType (T::*method)(ParamTypes...)) {
  return PtmfHelper::from<ReturnType, T, ParamTypes...>(method).apply(&obj);
}
template <typename T, typename ReturnType, typename... ParamTypes>
void* getMethodStartAddress(const T& obj, ReturnType (T::*method)(ParamTypes...) const) {
  return PtmfHelper::from<ReturnType, T, ParamTypes...>(method).apply(&obj);
}

template <typename... ParamTypes>
struct GetFunctorStartAddress {
  // Given a functor (any object defining operator()), return the start address of the function,
  // suitable for passing to addr2line to obtain a source file/line for debugging purposes.
  //
  // This turns out to be incredibly hard to implement in the presence of overloaded or templated
  // functors. Therefore, we impose these specific restrictions, specific to our use case:
  // - Overloading is not allowed, but templating is. (Generally we only intend to support lambdas
  //   anyway.)
  // - The template parameters to GetFunctorStartAddress specify a hint as to the expected
  //   parameter types. If the functor is templated, its parameters must match exactly these types.
  //   (If it's not templated, ParamTypes are ignored.)

  template <typename Func>
  static void* apply(Func&& func) {
    typedef decltype(func(instance<ParamTypes>()...)) ReturnType;
    return PtmfHelper::from<ReturnType, Decay<Func>, ParamTypes...>(
        &Decay<Func>::operator()).apply(&func);
  }
};

template <>
struct GetFunctorStartAddress<Void&&>: public GetFunctorStartAddress<> {};
// Hack for TransformPromiseNode use case: an input type of `Void` indicates that the function
// actually has no parameters.

class TransformPromiseNodeBase: public PromiseNode {
public:
  TransformPromiseNodeBase(Own<PromiseNode>&& dependency, void* continuationTracePtr);

  void onReady(Event* event) noexcept override;
  void get(ExceptionOrValue& output) noexcept override;
  void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) override;

private:
  Own<PromiseNode> dependency;
  void* continuationTracePtr;

  void dropDependency();
  void getDepResult(ExceptionOrValue& output);

  virtual void getImpl(ExceptionOrValue& output) = 0;

  template <typename, typename, typename, typename>
  friend class TransformPromiseNode;
};

template <typename T, typename DepT, typename Func, typename ErrorFunc>
class TransformPromiseNode final: public TransformPromiseNodeBase {
  // A PromiseNode that transforms the result of another PromiseNode through an application-provided
  // function (implements `then()`).

public:
  TransformPromiseNode(Own<PromiseNode>&& dependency, Func&& func, ErrorFunc&& errorHandler,
                       void* continuationTracePtr)
      : TransformPromiseNodeBase(kj::mv(dependency), continuationTracePtr),
        func(kj::fwd<Func>(func)), errorHandler(kj::fwd<ErrorFunc>(errorHandler)) {}

  ~TransformPromiseNode() noexcept(false) {
    // We need to make sure the dependency is deleted before we delete the continuations because it
    // is a common pattern for the continuations to hold ownership of objects that might be in-use
    // by the dependency.
    dropDependency();
  }

private:
  Func func;
  ErrorFunc errorHandler;

  void getImpl(ExceptionOrValue& output) override {
    ExceptionOr<DepT> depResult;
    getDepResult(depResult);
    KJ_IF_MAYBE(depException, depResult.exception) {
      output.as<T>() = handle(
          MaybeVoidCaller<Exception, FixVoid<ReturnType<ErrorFunc, Exception>>>::apply(
              errorHandler, kj::mv(*depException)));
    } else KJ_IF_MAYBE(depValue, depResult.value) {
      output.as<T>() = handle(MaybeVoidCaller<DepT, T>::apply(func, kj::mv(*depValue)));
    }
  }

  ExceptionOr<T> handle(T&& value) {
    return kj::mv(value);
  }
  ExceptionOr<T> handle(PropagateException::Bottom&& value) {
    return ExceptionOr<T>(false, value.asException());
  }
};

// -------------------------------------------------------------------

class ForkHubBase;

class ForkBranchBase: public PromiseNode {
public:
  ForkBranchBase(Own<ForkHubBase>&& hub);
  ~ForkBranchBase() noexcept(false);

  void hubReady() noexcept;
  // Called by the hub to indicate that it is ready.

  // implements PromiseNode ------------------------------------------
  void onReady(Event* event) noexcept override;
  void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) override;

protected:
  inline ExceptionOrValue& getHubResultRef();

  void releaseHub(ExceptionOrValue& output);
  // Release the hub.  If an exception is thrown, add it to `output`.

private:
  OnReadyEvent onReadyEvent;

  Own<ForkHubBase> hub;
  ForkBranchBase* next = nullptr;
  ForkBranchBase** prevPtr = nullptr;

  friend class ForkHubBase;
};

template <typename T> T copyOrAddRef(T& t) { return t; }
template <typename T> Own<T> copyOrAddRef(Own<T>& t) { return t->addRef(); }

template <typename T>
class ForkBranch final: public ForkBranchBase {
  // A PromiseNode that implements one branch of a fork -- i.e. one of the branches that receives
  // a const reference.

public:
  ForkBranch(Own<ForkHubBase>&& hub): ForkBranchBase(kj::mv(hub)) {}

  void get(ExceptionOrValue& output) noexcept override {
    ExceptionOr<T>& hubResult = getHubResultRef().template as<T>();
    KJ_IF_MAYBE(value, hubResult.value) {
      output.as<T>().value = copyOrAddRef(*value);
    } else {
      output.as<T>().value = nullptr;
    }
    output.exception = hubResult.exception;
    releaseHub(output);
  }
};

template <typename T, size_t index>
class SplitBranch final: public ForkBranchBase {
  // A PromiseNode that implements one branch of a fork -- i.e. one of the branches that receives
  // a const reference.

public:
  SplitBranch(Own<ForkHubBase>&& hub): ForkBranchBase(kj::mv(hub)) {}

  typedef kj::Decay<decltype(kj::get<index>(kj::instance<T>()))> Element;

  void get(ExceptionOrValue& output) noexcept override {
    ExceptionOr<T>& hubResult = getHubResultRef().template as<T>();
    KJ_IF_MAYBE(value, hubResult.value) {
      output.as<Element>().value = kj::mv(kj::get<index>(*value));
    } else {
      output.as<Element>().value = nullptr;
    }
    output.exception = hubResult.exception;
    releaseHub(output);
  }
};

// -------------------------------------------------------------------

class ForkHubBase: public Refcounted, protected Event {
public:
  ForkHubBase(Own<PromiseNode>&& inner, ExceptionOrValue& resultRef);

  inline ExceptionOrValue& getResultRef() { return resultRef; }

private:
  Own<PromiseNode> inner;
  ExceptionOrValue& resultRef;

  ForkBranchBase* headBranch = nullptr;
  ForkBranchBase** tailBranch = &headBranch;
  // Tail becomes null once the inner promise is ready and all branches have been notified.

  Maybe<Own<Event>> fire() override;
  void traceEvent(TraceBuilder& builder) override;

  friend class ForkBranchBase;
};

template <typename T>
class ForkHub final: public ForkHubBase {
  // A PromiseNode that implements the hub of a fork.  The first call to Promise::fork() replaces
  // the promise's outer node with a ForkHub, and subsequent calls add branches to that hub (if
  // possible).

public:
  ForkHub(Own<PromiseNode>&& inner): ForkHubBase(kj::mv(inner), result) {}

  Promise<_::UnfixVoid<T>> addBranch() {
    return _::PromiseNode::to<Promise<_::UnfixVoid<T>>>(kj::heap<ForkBranch<T>>(addRef(*this)));
  }

  _::SplitTuplePromise<T> split() {
    return splitImpl(MakeIndexes<tupleSize<T>()>());
  }

private:
  ExceptionOr<T> result;

  template <size_t... indexes>
  _::SplitTuplePromise<T> splitImpl(Indexes<indexes...>) {
    return kj::tuple(addSplit<indexes>()...);
  }

  template <size_t index>
  ReducePromises<typename SplitBranch<T, index>::Element> addSplit() {
    return _::PromiseNode::to<ReducePromises<typename SplitBranch<T, index>::Element>>(
        maybeChain(kj::heap<SplitBranch<T, index>>(addRef(*this)),
                   implicitCast<typename SplitBranch<T, index>::Element*>(nullptr)));
  }
};

inline ExceptionOrValue& ForkBranchBase::getHubResultRef() {
  return hub->getResultRef();
}

// -------------------------------------------------------------------

class ChainPromiseNode final: public PromiseNode, public Event {
  // Promise node which reduces Promise<Promise<T>> to Promise<T>.
  //
  // `Event` is only a public base class because otherwise we can't cast Own<ChainPromiseNode> to
  // Own<Event>.  Ugh, templates and private...

public:
  explicit ChainPromiseNode(Own<PromiseNode> inner);
  ~ChainPromiseNode() noexcept(false);

  void onReady(Event* event) noexcept override;
  void setSelfPointer(Own<PromiseNode>* selfPtr) noexcept override;
  void get(ExceptionOrValue& output) noexcept override;
  void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) override;

private:
  enum State {
    STEP1,
    STEP2
  };

  State state;

  Own<PromiseNode> inner;
  // In STEP1, a PromiseNode for a Promise<T>.
  // In STEP2, a PromiseNode for a T.

  Event* onReadyEvent = nullptr;
  Own<PromiseNode>* selfPtr = nullptr;

  Maybe<Own<Event>> fire() override;
  void traceEvent(TraceBuilder& builder) override;
};

template <typename T>
Own<PromiseNode> maybeChain(Own<PromiseNode>&& node, Promise<T>*) {
  return heap<ChainPromiseNode>(kj::mv(node));
}

template <typename T>
Own<PromiseNode>&& maybeChain(Own<PromiseNode>&& node, T*) {
  return kj::mv(node);
}

template <typename T, typename Result = decltype(T::reducePromise(instance<Promise<T>>()))>
inline Result maybeReduce(Promise<T>&& promise, bool) {
  return T::reducePromise(kj::mv(promise));
}

template <typename T>
inline Promise<T> maybeReduce(Promise<T>&& promise, ...) {
  return kj::mv(promise);
}

// -------------------------------------------------------------------

class ExclusiveJoinPromiseNode final: public PromiseNode {
public:
  ExclusiveJoinPromiseNode(Own<PromiseNode> left, Own<PromiseNode> right);
  ~ExclusiveJoinPromiseNode() noexcept(false);

  void onReady(Event* event) noexcept override;
  void get(ExceptionOrValue& output) noexcept override;
  void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) override;

private:
  class Branch: public Event {
  public:
    Branch(ExclusiveJoinPromiseNode& joinNode, Own<PromiseNode> dependency);
    ~Branch() noexcept(false);

    bool get(ExceptionOrValue& output);
    // Returns true if this is the side that finished.

    Maybe<Own<Event>> fire() override;
    void traceEvent(TraceBuilder& builder) override;

  private:
    ExclusiveJoinPromiseNode& joinNode;
    Own<PromiseNode> dependency;

    friend class ExclusiveJoinPromiseNode;
  };

  Branch left;
  Branch right;
  OnReadyEvent onReadyEvent;
};

// -------------------------------------------------------------------

class ArrayJoinPromiseNodeBase: public PromiseNode {
public:
  ArrayJoinPromiseNodeBase(Array<Own<PromiseNode>> promises,
                           ExceptionOrValue* resultParts, size_t partSize);
  ~ArrayJoinPromiseNodeBase() noexcept(false);

  void onReady(Event* event) noexcept override final;
  void get(ExceptionOrValue& output) noexcept override final;
  void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) override final;

protected:
  virtual void getNoError(ExceptionOrValue& output) noexcept = 0;
  // Called to compile the result only in the case where there were no errors.

private:
  uint countLeft;
  OnReadyEvent onReadyEvent;

  class Branch final: public Event {
  public:
    Branch(ArrayJoinPromiseNodeBase& joinNode, Own<PromiseNode> dependency,
           ExceptionOrValue& output);
    ~Branch() noexcept(false);

    Maybe<Own<Event>> fire() override;
    void traceEvent(TraceBuilder& builder) override;

    Maybe<Exception> getPart();
    // Calls dependency->get(output).  If there was an exception, return it.

  private:
    ArrayJoinPromiseNodeBase& joinNode;
    Own<PromiseNode> dependency;
    ExceptionOrValue& output;

    friend class ArrayJoinPromiseNodeBase;
  };

  Array<Branch> branches;
};

template <typename T>
class ArrayJoinPromiseNode final: public ArrayJoinPromiseNodeBase {
public:
  ArrayJoinPromiseNode(Array<Own<PromiseNode>> promises,
                       Array<ExceptionOr<T>> resultParts)
      : ArrayJoinPromiseNodeBase(kj::mv(promises), resultParts.begin(), sizeof(ExceptionOr<T>)),
        resultParts(kj::mv(resultParts)) {}

protected:
  void getNoError(ExceptionOrValue& output) noexcept override {
    auto builder = heapArrayBuilder<T>(resultParts.size());
    for (auto& part: resultParts) {
      KJ_IASSERT(part.value != nullptr,
                 "Bug in KJ promise framework:  Promise result had neither value no exception.");
      builder.add(kj::mv(*_::readMaybe(part.value)));
    }
    output.as<Array<T>>() = builder.finish();
  }

private:
  Array<ExceptionOr<T>> resultParts;
};

template <>
class ArrayJoinPromiseNode<void> final: public ArrayJoinPromiseNodeBase {
public:
  ArrayJoinPromiseNode(Array<Own<PromiseNode>> promises,
                       Array<ExceptionOr<_::Void>> resultParts);
  ~ArrayJoinPromiseNode();

protected:
  void getNoError(ExceptionOrValue& output) noexcept override;

private:
  Array<ExceptionOr<_::Void>> resultParts;
};

// -------------------------------------------------------------------

class EagerPromiseNodeBase: public PromiseNode, protected Event {
  // A PromiseNode that eagerly evaluates its dependency even if its dependent does not eagerly
  // evaluate it.

public:
  EagerPromiseNodeBase(Own<PromiseNode>&& dependency, ExceptionOrValue& resultRef);

  void onReady(Event* event) noexcept override;
  void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) override;

private:
  Own<PromiseNode> dependency;
  OnReadyEvent onReadyEvent;

  ExceptionOrValue& resultRef;

  Maybe<Own<Event>> fire() override;
  void traceEvent(TraceBuilder& builder) override;
};

template <typename T>
class EagerPromiseNode final: public EagerPromiseNodeBase {
public:
  EagerPromiseNode(Own<PromiseNode>&& dependency)
      : EagerPromiseNodeBase(kj::mv(dependency), result) {}

  void get(ExceptionOrValue& output) noexcept override {
    output.as<T>() = kj::mv(result);
  }

private:
  ExceptionOr<T> result;
};

template <typename T>
Own<PromiseNode> spark(Own<PromiseNode>&& node) {
  // Forces evaluation of the given node to begin as soon as possible, even if no one is waiting
  // on it.
  return heap<EagerPromiseNode<T>>(kj::mv(node));
}

// -------------------------------------------------------------------

class AdapterPromiseNodeBase: public PromiseNode {
public:
  void onReady(Event* event) noexcept override;
  void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) override;

protected:
  inline void setReady() {
    onReadyEvent.arm();
  }

private:
  OnReadyEvent onReadyEvent;
};

template <typename T, typename Adapter>
class AdapterPromiseNode final: public AdapterPromiseNodeBase,
                                private PromiseFulfiller<UnfixVoid<T>> {
  // A PromiseNode that wraps a PromiseAdapter.

public:
  template <typename... Params>
  AdapterPromiseNode(Params&&... params)
      : adapter(static_cast<PromiseFulfiller<UnfixVoid<T>>&>(*this), kj::fwd<Params>(params)...) {}

  void get(ExceptionOrValue& output) noexcept override {
    KJ_IREQUIRE(!isWaiting());
    output.as<T>() = kj::mv(result);
  }

private:
  ExceptionOr<T> result;
  bool waiting = true;
  Adapter adapter;

  void fulfill(T&& value) override {
    if (waiting) {
      waiting = false;
      result = ExceptionOr<T>(kj::mv(value));
      setReady();
    }
  }

  void reject(Exception&& exception) override {
    if (waiting) {
      waiting = false;
      result = ExceptionOr<T>(false, kj::mv(exception));
      setReady();
    }
  }

  bool isWaiting() override {
    return waiting;
  }
};

// -------------------------------------------------------------------

class FiberBase: public PromiseNode, private Event {
  // Base class for the outer PromiseNode representing a fiber.

public:
  explicit FiberBase(size_t stackSize, _::ExceptionOrValue& result);
  explicit FiberBase(const FiberPool& pool, _::ExceptionOrValue& result);
  ~FiberBase() noexcept(false);

  void start() { armDepthFirst(); }
  // Call immediately after construction to begin executing the fiber.

  class WaitDoneEvent;

  void onReady(_::Event* event) noexcept override;
  void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) override;

protected:
  bool isFinished() { return state == FINISHED; }
  void destroy();

private:
  enum { WAITING, RUNNING, CANCELED, FINISHED } state;

  _::PromiseNode* currentInner = nullptr;
  OnReadyEvent onReadyEvent;
  Own<FiberStack> stack;
  _::ExceptionOrValue& result;

  void run();
  virtual void runImpl(WaitScope& waitScope) = 0;

  Maybe<Own<Event>> fire() override;
  void traceEvent(TraceBuilder& builder) override;
  // Implements Event. Each time the event is fired, switchToFiber() is called.

  friend class FiberStack;
  friend void _::waitImpl(Own<_::PromiseNode>&& node, _::ExceptionOrValue& result,
                          WaitScope& waitScope);
  friend bool _::pollImpl(_::PromiseNode& node, WaitScope& waitScope);
};

template <typename Func>
class Fiber final: public FiberBase {
public:
  explicit Fiber(size_t stackSize, Func&& func): FiberBase(stackSize, result), func(kj::fwd<Func>(func)) {}
  explicit Fiber(const FiberPool& pool, Func&& func): FiberBase(pool, result), func(kj::fwd<Func>(func)) {}
  ~Fiber() noexcept(false) { destroy(); }

  typedef FixVoid<decltype(kj::instance<Func&>()(kj::instance<WaitScope&>()))> ResultType;

  void get(ExceptionOrValue& output) noexcept override {
    KJ_IREQUIRE(isFinished());
    output.as<ResultType>() = kj::mv(result);
  }

private:
  Func func;
  ExceptionOr<ResultType> result;

  void runImpl(WaitScope& waitScope) override {
    result.template as<ResultType>() =
        MaybeVoidCaller<WaitScope&, ResultType>::apply(func, waitScope);
  }
};

}  // namespace _ (private)

// =======================================================================================

template <typename T>
Promise<T>::Promise(_::FixVoid<T> value)
    : PromiseBase(heap<_::ImmediatePromiseNode<_::FixVoid<T>>>(kj::mv(value))) {}

template <typename T>
Promise<T>::Promise(kj::Exception&& exception)
    : PromiseBase(heap<_::ImmediateBrokenPromiseNode>(kj::mv(exception))) {}

template <typename T>
template <typename Func, typename ErrorFunc>
PromiseForResult<Func, T> Promise<T>::then(Func&& func, ErrorFunc&& errorHandler) {
  typedef _::FixVoid<_::ReturnType<Func, T>> ResultT;

  void* continuationTracePtr = _::GetFunctorStartAddress<_::FixVoid<T>&&>::apply(func);
  Own<_::PromiseNode> intermediate =
      heap<_::TransformPromiseNode<ResultT, _::FixVoid<T>, Func, ErrorFunc>>(
          kj::mv(node), kj::fwd<Func>(func), kj::fwd<ErrorFunc>(errorHandler),
          continuationTracePtr);
  auto result = _::PromiseNode::to<_::ChainPromises<_::ReturnType<Func, T>>>(
      _::maybeChain(kj::mv(intermediate), implicitCast<ResultT*>(nullptr)));
  return _::maybeReduce(kj::mv(result), false);
}

namespace _ {  // private

template <typename T>
struct IdentityFunc {
  inline T operator()(T&& value) const {
    return kj::mv(value);
  }
};
template <typename T>
struct IdentityFunc<Promise<T>> {
  inline Promise<T> operator()(T&& value) const {
    return kj::mv(value);
  }
};
template <>
struct IdentityFunc<void> {
  inline void operator()() const {}
};
template <>
struct IdentityFunc<Promise<void>> {
  Promise<void> operator()() const;
  // This can't be inline because it will make the translation unit depend on kj-async. Awkwardly,
  // Cap'n Proto relies on being able to include this header without creating such a link-time
  // dependency.
};

}  // namespace _ (private)

template <typename T>
template <typename ErrorFunc>
Promise<T> Promise<T>::catch_(ErrorFunc&& errorHandler) {
  // then()'s ErrorFunc can only return a Promise if Func also returns a Promise. In this case,
  // Func is being filled in automatically. We want to make sure ErrorFunc can return a Promise,
  // but we don't want the extra overhead of promise chaining if ErrorFunc doesn't actually
  // return a promise. So we make our Func return match ErrorFunc.
  typedef _::IdentityFunc<decltype(errorHandler(instance<Exception&&>()))> Func;
  typedef _::FixVoid<_::ReturnType<Func, T>> ResultT;

  // The reason catch_() isn't simply implemented in terms of then() is because we want the trace
  // pointer to be based on ErrorFunc rather than Func.
  void* continuationTracePtr = _::GetFunctorStartAddress<kj::Exception&&>::apply(errorHandler);
  Own<_::PromiseNode> intermediate =
      heap<_::TransformPromiseNode<ResultT, _::FixVoid<T>, Func, ErrorFunc>>(
          kj::mv(node), Func(), kj::fwd<ErrorFunc>(errorHandler), continuationTracePtr);
  auto result = _::PromiseNode::to<_::ChainPromises<_::ReturnType<Func, T>>>(
      _::maybeChain(kj::mv(intermediate), implicitCast<ResultT*>(nullptr)));
  return _::maybeReduce(kj::mv(result), false);
}

template <typename T>
T Promise<T>::wait(WaitScope& waitScope) {
  _::ExceptionOr<_::FixVoid<T>> result;
  _::waitImpl(kj::mv(node), result, waitScope);
  return convertToReturn(kj::mv(result));
}

template <typename T>
bool Promise<T>::poll(WaitScope& waitScope) {
  return _::pollImpl(*node, waitScope);
}

template <typename T>
ForkedPromise<T> Promise<T>::fork() {
  return ForkedPromise<T>(false, refcounted<_::ForkHub<_::FixVoid<T>>>(kj::mv(node)));
}

template <typename T>
Promise<T> ForkedPromise<T>::addBranch() {
  return hub->addBranch();
}

template <typename T>
_::SplitTuplePromise<T> Promise<T>::split() {
  return refcounted<_::ForkHub<_::FixVoid<T>>>(kj::mv(node))->split();
}

template <typename T>
Promise<T> Promise<T>::exclusiveJoin(Promise<T>&& other) {
  return Promise(false, heap<_::ExclusiveJoinPromiseNode>(kj::mv(node), kj::mv(other.node)));
}

template <typename T>
template <typename... Attachments>
Promise<T> Promise<T>::attach(Attachments&&... attachments) {
  return Promise(false, kj::heap<_::AttachmentPromiseNode<Tuple<Attachments...>>>(
      kj::mv(node), kj::tuple(kj::fwd<Attachments>(attachments)...)));
}

template <typename T>
template <typename ErrorFunc>
Promise<T> Promise<T>::eagerlyEvaluate(ErrorFunc&& errorHandler) {
  // See catch_() for commentary.
  return Promise(false, _::spark<_::FixVoid<T>>(then(
      _::IdentityFunc<decltype(errorHandler(instance<Exception&&>()))>(),
      kj::fwd<ErrorFunc>(errorHandler)).node));
}

template <typename T>
Promise<T> Promise<T>::eagerlyEvaluate(decltype(nullptr)) {
  return Promise(false, _::spark<_::FixVoid<T>>(kj::mv(node)));
}

template <typename T>
kj::String Promise<T>::trace() {
  return PromiseBase::trace();
}

template <typename Func>
inline PromiseForResult<Func, void> evalLater(Func&& func) {
  return _::yield().then(kj::fwd<Func>(func), _::PropagateException());
}

template <typename Func>
inline PromiseForResult<Func, void> evalLast(Func&& func) {
  return _::yieldHarder().then(kj::fwd<Func>(func), _::PropagateException());
}

template <typename Func>
inline PromiseForResult<Func, void> evalNow(Func&& func) {
  PromiseForResult<Func, void> result = nullptr;
  KJ_IF_MAYBE(e, kj::runCatchingExceptions([&]() {
    result = func();
  })) {
    result = kj::mv(*e);
  }
  return result;
}

template <typename Func>
struct RetryOnDisconnect_ {
  static inline PromiseForResult<Func, void> apply(Func&& func) {
    return evalLater([func = kj::mv(func)]() mutable -> PromiseForResult<Func, void> {
      auto promise = evalNow(func);
      return promise.catch_([func = kj::mv(func)](kj::Exception&& e) mutable -> PromiseForResult<Func, void> {
        if (e.getType() == kj::Exception::Type::DISCONNECTED) {
          return func();
        } else {
          return kj::mv(e);
        }
      });
    });
  }
};
template <typename Func>
struct RetryOnDisconnect_<Func&> {
  // Specialization for references. Needed because the syntax for capturing references in a
  // lambda is different. :(
  static inline PromiseForResult<Func, void> apply(Func& func) {
    auto promise = evalLater(func);
    return promise.catch_([&func](kj::Exception&& e) -> PromiseForResult<Func, void> {
      if (e.getType() == kj::Exception::Type::DISCONNECTED) {
        return func();
      } else {
        return kj::mv(e);
      }
    });
  }
};

template <typename Func>
inline PromiseForResult<Func, void> retryOnDisconnect(Func&& func) {
  return RetryOnDisconnect_<Func>::apply(kj::fwd<Func>(func));
}

template <typename Func>
inline PromiseForResult<Func, WaitScope&> startFiber(size_t stackSize, Func&& func) {
  typedef _::FixVoid<_::ReturnType<Func, WaitScope&>> ResultT;

  Own<_::FiberBase> intermediate = kj::heap<_::Fiber<Func>>(stackSize, kj::fwd<Func>(func));
  intermediate->start();
  auto result = _::PromiseNode::to<_::ChainPromises<_::ReturnType<Func, WaitScope&>>>(
      _::maybeChain(kj::mv(intermediate), implicitCast<ResultT*>(nullptr)));
  return _::maybeReduce(kj::mv(result), false);
}

template <typename Func>
inline PromiseForResult<Func, WaitScope&> FiberPool::startFiber(Func&& func) const {
  typedef _::FixVoid<_::ReturnType<Func, WaitScope&>> ResultT;

  Own<_::FiberBase> intermediate = kj::heap<_::Fiber<Func>>(*this, kj::fwd<Func>(func));
  intermediate->start();
  auto result = _::PromiseNode::to<_::ChainPromises<_::ReturnType<Func, WaitScope&>>>(
      _::maybeChain(kj::mv(intermediate), implicitCast<ResultT*>(nullptr)));
  return _::maybeReduce(kj::mv(result), false);
}

template <typename T>
template <typename ErrorFunc>
void Promise<T>::detach(ErrorFunc&& errorHandler) {
  return _::detach(then([](T&&) {}, kj::fwd<ErrorFunc>(errorHandler)));
}

template <>
template <typename ErrorFunc>
void Promise<void>::detach(ErrorFunc&& errorHandler) {
  return _::detach(then([]() {}, kj::fwd<ErrorFunc>(errorHandler)));
}

template <typename T>
Promise<Array<T>> joinPromises(Array<Promise<T>>&& promises) {
  return _::PromiseNode::to<Promise<Array<T>>>(kj::heap<_::ArrayJoinPromiseNode<T>>(
      KJ_MAP(p, promises) { return _::PromiseNode::from(kj::mv(p)); },
      heapArray<_::ExceptionOr<T>>(promises.size())));
}

// =======================================================================================

namespace _ {  // private

class WeakFulfillerBase: protected kj::Disposer {
protected:
  WeakFulfillerBase(): inner(nullptr) {}
  virtual ~WeakFulfillerBase() noexcept(false) {}

  template <typename T>
  inline PromiseFulfiller<T>* getInner() {
    return static_cast<PromiseFulfiller<T>*>(inner);
  };
  template <typename T>
  inline void setInner(PromiseFulfiller<T>* ptr) {
    inner = ptr;
  };

private:
  mutable PromiseRejector* inner;

  void disposeImpl(void* pointer) const override;
};

template <typename T>
class WeakFulfiller final: public PromiseFulfiller<T>, public WeakFulfillerBase {
  // A wrapper around PromiseFulfiller which can be detached.
  //
  // There are a couple non-trivialities here:
  // - If the WeakFulfiller is discarded, we want the promise it fulfills to be implicitly
  //   rejected.
  // - We cannot destroy the WeakFulfiller until the application has discarded it *and* it has been
  //   detached from the underlying fulfiller, because otherwise the later detach() call will go
  //   to a dangling pointer.  Essentially, WeakFulfiller is reference counted, although the
  //   refcount never goes over 2 and we manually implement the refcounting because we need to do
  //   other special things when each side detaches anyway.  To this end, WeakFulfiller is its own
  //   Disposer -- dispose() is called when the application discards its owned pointer to the
  //   fulfiller and detach() is called when the promise is destroyed.

public:
  KJ_DISALLOW_COPY(WeakFulfiller);

  static kj::Own<WeakFulfiller> make() {
    WeakFulfiller* ptr = new WeakFulfiller;
    return Own<WeakFulfiller>(ptr, *ptr);
  }

  void fulfill(FixVoid<T>&& value) override {
    if (getInner<T>() != nullptr) {
      getInner<T>()->fulfill(kj::mv(value));
    }
  }

  void reject(Exception&& exception) override {
    if (getInner<T>() != nullptr) {
      getInner<T>()->reject(kj::mv(exception));
    }
  }

  bool isWaiting() override {
    return getInner<T>() != nullptr && getInner<T>()->isWaiting();
  }

  void attach(PromiseFulfiller<T>& newInner) {
    setInner<T>(&newInner);
  }

  void detach(PromiseFulfiller<T>& from) {
    if (getInner<T>() == nullptr) {
      // Already disposed.
      delete this;
    } else {
      KJ_IREQUIRE(getInner<T>() == &from);
      setInner<T>(nullptr);
    }
  }

private:
  WeakFulfiller() {}
};

template <typename T>
class PromiseAndFulfillerAdapter {
public:
  PromiseAndFulfillerAdapter(PromiseFulfiller<T>& fulfiller,
                             WeakFulfiller<T>& wrapper)
      : fulfiller(fulfiller), wrapper(wrapper) {
    wrapper.attach(fulfiller);
  }

  ~PromiseAndFulfillerAdapter() noexcept(false) {
    wrapper.detach(fulfiller);
  }

private:
  PromiseFulfiller<T>& fulfiller;
  WeakFulfiller<T>& wrapper;
};

}  // namespace _ (private)

template <typename T>
template <typename Func>
bool PromiseFulfiller<T>::rejectIfThrows(Func&& func) {
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions(kj::mv(func))) {
    reject(kj::mv(*exception));
    return false;
  } else {
    return true;
  }
}

template <typename Func>
bool PromiseFulfiller<void>::rejectIfThrows(Func&& func) {
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions(kj::mv(func))) {
    reject(kj::mv(*exception));
    return false;
  } else {
    return true;
  }
}

template <typename T, typename Adapter, typename... Params>
_::ReducePromises<T> newAdaptedPromise(Params&&... adapterConstructorParams) {
  Own<_::PromiseNode> intermediate(
      heap<_::AdapterPromiseNode<_::FixVoid<T>, Adapter>>(
          kj::fwd<Params>(adapterConstructorParams)...));
  return _::PromiseNode::to<_::ReducePromises<T>>(
      _::maybeChain(kj::mv(intermediate), implicitCast<T*>(nullptr)));
}

template <typename T>
PromiseFulfillerPair<T> newPromiseAndFulfiller() {
  auto wrapper = _::WeakFulfiller<T>::make();

  Own<_::PromiseNode> intermediate(
      heap<_::AdapterPromiseNode<_::FixVoid<T>, _::PromiseAndFulfillerAdapter<T>>>(*wrapper));
  auto promise = _::PromiseNode::to<_::ReducePromises<T>>(
      _::maybeChain(kj::mv(intermediate), implicitCast<T*>(nullptr)));

  return PromiseFulfillerPair<T> { kj::mv(promise), kj::mv(wrapper) };
}

// =======================================================================================
// cross-thread stuff

namespace _ {  // (private)

class XThreadEvent: private Event,         // it's an event in the target thread
                    public PromiseNode {   // it's a PromiseNode in the requesting thread
public:
  XThreadEvent(ExceptionOrValue& result, const Executor& targetExecutor, void* funcTracePtr);

  void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) override;

protected:
  void ensureDoneOrCanceled();
  // MUST be called in destructor of subclasses to make sure the object is not destroyed while
  // still being accessed by the other thread. (This can't be placed in ~XThreadEvent() because
  // that destructor doesn't run until the subclass has already been destroyed.)

  virtual kj::Maybe<Own<PromiseNode>> execute() = 0;
  // Run the function. If the function returns a promise, returns the inner PromiseNode, otherwise
  // returns null.

  // implements PromiseNode ----------------------------------------------------
  void onReady(Event* event) noexcept override;

private:
  ExceptionOrValue& result;
  void* funcTracePtr;

  kj::Own<const Executor> targetExecutor;
  Maybe<const Executor&> replyExecutor;  // If executeAsync() was used.

  kj::Maybe<Own<PromiseNode>> promiseNode;
  // Accessed only in target thread.

  Maybe<XThreadEvent&> targetNext;
  Maybe<XThreadEvent&>* targetPrev = nullptr;
  // Membership in one of the linked lists in the target Executor's work list or cancel list. These
  // fields are protected by the target Executor's mutex.

  enum {
    UNUSED,
    // Object was never queued on another thread.

    QUEUED,
    // Target thread has not yet dequeued the event from the state.start list. The requesting
    // thread can cancel execution by removing the event from the list.

    EXECUTING,
    // Target thread has dequeued the event from state.start and moved it to state.executing. To
    // cancel, the requesting thread must add the event to the state.cancel list and change the
    // state to CANCELING.

    CANCELING,
    // Requesting thread is trying to cancel this event. The target thread will change the state to
    // `DONE` once canceled.

    DONE
    // Target thread has completed handling this event and will not touch it again. The requesting
    // thread can safely delete the object. The `state` is updated to `DONE` using an atomic
    // release operation after ensuring that the event will not be touched again, so that the
    // requesting can safely skip locking if it observes the state is already DONE.
  } state = UNUSED;
  // State, which is also protected by `targetExecutor`'s mutex.

  Maybe<XThreadEvent&> replyNext;
  Maybe<XThreadEvent&>* replyPrev = nullptr;
  // Membership in `replyExecutor`'s reply list. Protected by `replyExecutor`'s mutex. The
  // executing thread places the event in the reply list near the end of the `EXECUTING` state.
  // Because the thread cannot lock two mutexes at once, it's possible that the reply executor
  // will receive the reply while the event is still listed in the EXECUTING state, but it can
  // ignore the state and proceed with the result.

  OnReadyEvent onReadyEvent;
  // Accessed only in requesting thread.

  friend class kj::Executor;

  void done();
  // Sets the state to `DONE` and notifies the originating thread that this event is done. Do NOT
  // call under lock.

  void sendReply();
  // Notifies the originating thread that this event is done, but doesn't set the state to DONE
  // yet. Do NOT call under lock.

  void setDoneState();
  // Assigns `state` to `DONE`, being careful to use an atomic-release-store if needed. This must
  // only be called in the destination thread, and must either be called under lock, or the thread
  // must take the lock and release it again shortly after setting the state (because some threads
  // may be waiting on the DONE state using a conditional wait on the mutex). After calling
  // setDoneState(), the destination thread MUST NOT touch this object ever again; it now belongs
  // solely to the requesting thread.

  void setDisconnected();
  // Sets the result to a DISCONNECTED exception indicating that the target event loop exited.

  class DelayedDoneHack;

  // implements Event ----------------------------------------------------------
  Maybe<Own<Event>> fire() override;
  // If called with promiseNode == nullptr, it's time to call execute(). If promiseNode != nullptr,
  // then it just indicated readiness and we need to get its result.

  void traceEvent(TraceBuilder& builder) override;
};

template <typename Func, typename = _::FixVoid<_::ReturnType<Func, void>>>
class XThreadEventImpl final: public XThreadEvent {
  // Implementation for a function that does not return a Promise.
public:
  XThreadEventImpl(Func&& func, const Executor& target)
      : XThreadEvent(result, target, GetFunctorStartAddress<>::apply(func)),
        func(kj::fwd<Func>(func)) {}
  ~XThreadEventImpl() noexcept(false) { ensureDoneOrCanceled(); }

  typedef _::FixVoid<_::ReturnType<Func, void>> ResultT;

  kj::Maybe<Own<_::PromiseNode>> execute() override {
    result.value = MaybeVoidCaller<Void, FixVoid<decltype(func())>>::apply(func, Void());
    return nullptr;
  }

  // implements PromiseNode ----------------------------------------------------
  void get(ExceptionOrValue& output) noexcept override {
    output.as<ResultT>() = kj::mv(result);
  }

private:
  Func func;
  ExceptionOr<ResultT> result;
  friend Executor;
};

template <typename Func, typename T>
class XThreadEventImpl<Func, Promise<T>> final: public XThreadEvent {
  // Implementation for a function that DOES return a Promise.
public:
  XThreadEventImpl(Func&& func, const Executor& target)
      : XThreadEvent(result, target, GetFunctorStartAddress<>::apply(func)),
        func(kj::fwd<Func>(func)) {}
  ~XThreadEventImpl() noexcept(false) { ensureDoneOrCanceled(); }

  typedef _::FixVoid<_::UnwrapPromise<PromiseForResult<Func, void>>> ResultT;

  kj::Maybe<Own<_::PromiseNode>> execute() override {
    auto result = _::PromiseNode::from(func());
    KJ_IREQUIRE(result.get() != nullptr);
    return kj::mv(result);
  }

  // implements PromiseNode ----------------------------------------------------
  void get(ExceptionOrValue& output) noexcept override {
    output.as<ResultT>() = kj::mv(result);
  }

private:
  Func func;
  ExceptionOr<ResultT> result;
  friend Executor;
};

}  // namespace _ (private)

template <typename Func>
_::UnwrapPromise<PromiseForResult<Func, void>> Executor::executeSync(Func&& func) const {
  _::XThreadEventImpl<Func> event(kj::fwd<Func>(func), *this);
  send(event, true);
  return convertToReturn(kj::mv(event.result));
}

template <typename Func>
PromiseForResult<Func, void> Executor::executeAsync(Func&& func) const {
  auto event = kj::heap<_::XThreadEventImpl<Func>>(kj::fwd<Func>(func), *this);
  send(*event, false);
  return _::PromiseNode::to<PromiseForResult<Func, void>>(kj::mv(event));
}

// -----------------------------------------------------------------------------

namespace _ {  // (private)

template <typename T>
class XThreadFulfiller;

class XThreadPaf: public PromiseNode {
public:
  XThreadPaf();
  virtual ~XThreadPaf() noexcept(false);

  class Disposer: public kj::Disposer {
  public:
    void disposeImpl(void* pointer) const override;
  };
  static const Disposer DISPOSER;

  // implements PromiseNode ----------------------------------------------------
  void onReady(Event* event) noexcept override;
  void tracePromise(TraceBuilder& builder, bool stopAtNextEvent) override;

private:
  enum {
    WAITING,
    // Not yet fulfilled, and the waiter is still waiting.
    //
    // Starting from this state, the state may transition to either FULFILLING or CANCELED
    // using an atomic compare-and-swap.

    FULFILLING,
    // The fulfiller thread atomically transitions the state from WAITING to FULFILLING when it
    // wishes to fulfill the promise. By doing so, it guarantees that the `executor` will not
    // disappear out from under it. It then fills in the result value, locks the executor mutex,
    // adds the object to the executor's list of fulfilled XThreadPafs, changes the state to
    // FULFILLED, and finally unlocks the mutex.
    //
    // If the waiting thread tries to cancel but discovers the object in this state, then it
    // must perform a conditional wait on the executor mutex to await the state becoming FULFILLED.
    // It can then delete the object.

    FULFILLED,
    // The fulfilling thread has completed filling in the result value and inserting the object
    // into the waiting thread's executor event queue. Moreover, the fulfilling thread no longer
    // holds any pointers to this object. The waiting thread is responsible for deleting it.

    DISPATCHED,
    // The object reached FULFILLED state, and then was dispatched from the waiting thread's
    // executor's event queue. Therefore, the object is completely owned by the waiting thread with
    // no need to lock anything.

    CANCELED
    // The waiting thread atomically transitions the state from WAITING to CANCELED if it is no
    // longer listening. In this state, it is the fulfiller thread's responsibility to destroy the
    // object.
  } state;

  const Executor& executor;
  // Executor of the waiting thread. Only guaranteed to be valid when state is `WAITING` or
  // `FULFILLING`. After any other state has been reached, this reference may be invalidated.

  Maybe<XThreadPaf&> next;
  Maybe<XThreadPaf&>* prev = nullptr;
  // In the FULFILLING/FULFILLED states, the object is placed in a linked list within the waiting
  // thread's executor. In those states, these pointers are guarded by said executor's mutex.

  OnReadyEvent onReadyEvent;

  class FulfillScope;

  static kj::Exception unfulfilledException();
  // Construct appropriate exception to use to reject an unfulfilled XThreadPaf.

  template <typename T>
  friend class XThreadFulfiller;
  friend Executor;
};

template <typename T>
class XThreadPafImpl final: public XThreadPaf {
public:
  // implements PromiseNode ----------------------------------------------------
  void get(ExceptionOrValue& output) noexcept override {
    output.as<FixVoid<T>>() = kj::mv(result);
  }

private:
  ExceptionOr<FixVoid<T>> result;

  friend class XThreadFulfiller<T>;
};

class XThreadPaf::FulfillScope {
  // Create on stack while setting `XThreadPafImpl<T>::result`.
  //
  // This ensures that:
  // - Only one call is carried out, even if multiple threads try to fulfill concurrently.
  // - The waiting thread is correctly signaled.
public:
  FulfillScope(XThreadPaf** pointer);
  // Atomically nulls out *pointer and takes ownership of the pointer.

  ~FulfillScope() noexcept(false);

  KJ_DISALLOW_COPY(FulfillScope);

  bool shouldFulfill() { return obj != nullptr; }

  template <typename T>
  XThreadPafImpl<T>* getTarget() { return static_cast<XThreadPafImpl<T>*>(obj); }

private:
  XThreadPaf* obj;
};

template <typename T>
class XThreadFulfiller final: public PromiseFulfiller<T> {
public:
  XThreadFulfiller(XThreadPafImpl<T>* target): target(target) {}

  ~XThreadFulfiller() noexcept(false) {
    if (target != nullptr) {
      reject(XThreadPaf::unfulfilledException());
    }
  }
  void fulfill(FixVoid<T>&& value) override {
    XThreadPaf::FulfillScope scope(&target);
    if (scope.shouldFulfill()) {
      scope.getTarget<T>()->result = kj::mv(value);
    }
  }
  void reject(Exception&& exception) override {
    XThreadPaf::FulfillScope scope(&target);
    if (scope.shouldFulfill()) {
      scope.getTarget<T>()->result.addException(kj::mv(exception));
    }
  }
  bool isWaiting() override {
    KJ_IF_MAYBE(t, target) {
#if _MSC_VER && !__clang__
      // Just assume 1-byte loads are atomic... on what kind of absurd platform would they not be?
      return t->state == XThreadPaf::WAITING;
#else
      return __atomic_load_n(&t->state, __ATOMIC_RELAXED) == XThreadPaf::WAITING;
#endif
    } else {
      return false;
    }
  }

private:
  XThreadPaf* target;
};

template <typename T>
class XThreadFulfiller<kj::Promise<T>> {
public:
  static_assert(sizeof(T) < 0,
      "newCrosssThreadPromiseAndFulfiller<Promise<T>>() is not currently supported");
  // TODO(someday): Is this worth supporting? Presumably, when someone calls `fulfill(somePromise)`,
  //   then `somePromise` should be assumed to be a promise owned by the fulfilling thread, not
  //   the waiting thread.
};

}  // namespace _ (private)

template <typename T>
PromiseFulfillerPair<T> newCrossThreadPromiseAndFulfiller() {
  kj::Own<_::XThreadPafImpl<T>> node(new _::XThreadPafImpl<T>, _::XThreadPaf::DISPOSER);
  auto fulfiller = kj::heap<_::XThreadFulfiller<T>>(node);
  return { _::PromiseNode::to<_::ReducePromises<T>>(kj::mv(node)), kj::mv(fulfiller) };
}

}  // namespace kj

KJ_END_HEADER
