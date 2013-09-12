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

#ifndef KJ_ASYNC_H_
#define KJ_ASYNC_H_

#include "function.h"
#include "exception.h"
#include "mutex.h"

namespace kj {

// =======================================================================================
// Various internal stuff that needs to be declared upfront.  Users should ignore this.

class PropagateException {
  // A functor which accepts a kj::Exception as a parameter and returns a broken promise of
  // arbitrary type which simply propagates the exception.
public:
  class Bottom {
  public:
    Bottom(Exception&& exception): exception(kj::mv(exception)) {}

    template <typename T>
    operator T() {
      throwFatalException(kj::mv(exception));
    }

    Exception asException() { return kj::mv(exception); }

  private:
    Exception exception;
  };

  Bottom operator()(Exception&& e) {
    return Bottom(kj::mv(e));
  }
};

template <typename T>
class Promise;

template <typename T> struct PromiseType_ { typedef T Type; };
template <typename T> struct PromiseType_<Promise<T>> { typedef T Type; };

template <typename T>
using PromiseType = typename PromiseType_<T>::Type;
// If T is Promise<U>, resolves to U, otherwise resolves to T.

namespace _ {  // private

template <typename Func, typename T>
struct ReturnType_ { typedef decltype(instance<Func>()(instance<T>())) Type; };
template <typename Func>
struct ReturnType_<Func, void> { typedef decltype(instance<Func>()()) Type; };

template <typename Func, typename T>
using ReturnType = typename ReturnType_<Func, T>::Type;
// The return type of functor Func given a parameter of type T, with the special exception that if
// T is void, this is the return type of Func called with no arguments.

struct Void {};

template <typename T> struct FixVoid_ { typedef T Type; };
template <> struct FixVoid_<void> { typedef Void Type; };
template <typename T> using FixVoid = typename FixVoid_<T>::Type;
// FixVoid<T> is just T unless T is void in which case it is _::Void (an empty struct).

template <typename T> struct UnfixVoid_ { typedef T Type; };
template <> struct UnfixVoid_<Void> { typedef void Type; };
template <typename T> using UnfixVoid = typename UnfixVoid_<T>::Type;
// UnfixVoid is the opposite of FixVoid.

template <typename In, typename Out>
struct MaybeVoidCaller {
  // Calls the function converting a Void input to an empty parameter list and a void return
  // value to a Void output.

  template <typename Func>
  static inline Out apply(Func& func, In&& in) {
    return func(kj::mv(in));
  }
};
template <typename Out>
struct MaybeVoidCaller<Void, Out> {
  template <typename Func>
  static inline Out apply(Func& func, Void&& in) {
    return func();
  }
};
template <typename In>
struct MaybeVoidCaller<In, Void> {
  template <typename Func>
  static inline Void apply(Func& func, In&& in) {
    func(kj::mv(in));
    return Void();
  }
};
template <>
struct MaybeVoidCaller<Void, Void> {
  template <typename Func>
  static inline Void apply(Func& func, Void&& in) {
    func();
    return Void();
  }
};

template <typename T>
inline T&& returnMaybeVoid(T&& t) {
  return kj::fwd<T>(t);
}
inline void returnMaybeVoid(Void&& v) {}

template <typename T>
class PromiseNode;

template <typename T>
class ChainPromiseNode;

}  // namespace _ (private)

// =======================================================================================
// User-relevant interfaces start here.

class EventLoop {
  // Represents a queue of events being executed in a loop.  Most code won't interact with
  // EventLoop directly, but instead use `Promise`s to interact with it indirectly.

public:
  EventLoop();

  static EventLoop& current();
  // Get the event loop for the current thread.  Throws an exception if no event loop is active.

  template <typename T>
  T wait(Promise<T>&& promise);
  // Run the event loop until the promise is fulfilled, then return its result.  If the promise
  // is rejected, throw an exception.
  //
  // It is possible to call wait() multiple times on the same event loop simultaneously, but you
  // must be very careful about this.  Here's the deal:
  // - If wait() is called from thread A when it is already being executed in thread B, then
  //   thread A will block at least until thread B's call to wait() completes, _even if_ the
  //   promise is fulfilled before that.
  // - If wait() is called recursively from a thread in which wait() is already running, then
  //   the inner wait() will proceed, but the outer wait() obviously cannot return until the inner
  //   wait() completes.
  // - Keep in mind that while wait() is running the event loop, it may be firing events that have
  //   nothing to do with the thing you're actually waiting for.  Avoid holding any mutex locks
  //   when you call wait() as if some other event handler happens to try to take that lock, you
  //   will deadlock.
  //
  // In general, it is only a good idea to use `wait()` in high-level code that has a simple
  // goal, e.g. in the main() function of a program that does one or two specific things and then
  // exits.  On the other hand, `wait()` should be avoided in library code, unless you spawn a
  // private thread and event loop to use it on.  Put another way, `wait()` is useful for quick
  // prototyping but generally bad for "real code".
  //
  // If the promise is rejected, `wait()` throws an exception.  This exception is usually fatal,
  // so if compiled with -fno-exceptions, the process will abort.  You may work around this by
  // using `there()` with an error handler to handle this case.  If your error handler throws a
  // non-fatal exception and then recovers by returning a dummy value, wait() will also throw a
  // non-fatal exception and return the same dummy value.

  Promise<void> yield();
  // Returns a promise which is fulfilled when all work currently in the queue has completed.
  // Note that this doesn't necessarily mean the queue is empty at that point -- if you call
  // `yield()` twice, the promise from the first call will be fulfilled before the one returned
  // by the second call.
  //
  // Note that `yield()` is the only way to add events to the _end_ of the queue.  When a promise
  // is fulfilled and some other promise is waiting on it, the `then` callback for that promise
  // actually goes onto the _beginning_ of the queue, so that related callbacks occur together and
  // splitting a task into finer-grained callbacks does not cause the task to "lose priority"
  // compared to other tasks occurring concurrently.

  template <typename Func>
  auto evalLater(Func&& func) const -> Promise<PromiseType<_::ReturnType<Func, void>>>;
  // Schedule for the given zero-parameter function to be executed in the event loop at some
  // point in the near future.  Returns a Promise for its result -- or, if `func()` itself returns
  // a promise, `evalLater()` returns a Promise for the result of resolving that promise.
  //
  // Example usage:
  //     Promise<int> x = loop.evalLater([]() { return 123; });
  //
  // If the returned promise is destroyed before the callback runs, the callback will be canceled.
  // If the returned promise is destroyed while the callback is running in another thread, the
  // destructor will block until the callback completes.
  //
  // `evalLater()` is largely equivalent to `there()` called on an already-fulfilled
  // `Promise<Void>`.

  template <typename T, typename Func, typename ErrorFunc = PropagateException>
  auto there(Promise<T>&& promise, Func&& func,
             ErrorFunc&& errorHandler = PropagateException()) const
      -> Promise<PromiseType<_::ReturnType<Func, T>>>;
  // When the given promise is fulfilled, execute `func` on its result inside this `EventLoop`.
  // Returns a promise for the result of `func()` -- or, if `func()` itself returns a promise,
  // `there()` returns a Promise for the result of resolving that promise.
  //
  // If `promise` is broken/rejected (i.e. with an exception), then `errorHandler` is called rather
  // than `func`.  The default error handler just propagates the exception.
  //
  // If the returned promise is destroyed before the callback runs, the callback will be canceled.
  // If the returned promise is destroyed while the callback is running in another thread, the
  // destructor will block until the callback completes.  Additionally, canceling the returned
  // promise will transitively cancel the input `promise`.  Or, if `func()` already ran and
  // returned another promise, then canceling the returned promise transitively cancels that
  // promise.

  // -----------------------------------------------------------------
  // Low-level interface.

  class Event {
    // An event waiting to be executed.  Not for direct use by applications -- promises use this
    // internally.

  public:
    Event(const EventLoop& loop): loop(loop), next(nullptr), prev(nullptr) {}
    ~Event() noexcept(false);
    KJ_DISALLOW_COPY(Event);

    void arm();
    // Enqueue this event so that run() will be called from the event loop soon.

    void disarm();
    // Cancel this event if it is armed.  If it is already running, block until it finishes
    // before returning.  MUST be called in the subclass's destructor if it is possible that
    // the event is still armed, because once Event's destructor is reached, fire() is a
    // pure-virtual function.

    inline const EventLoop& getEventLoop() { return loop; }
    // Get the event loop on which this event will run.

  protected:
    virtual void fire() = 0;
    // Fire the event.

  private:
    friend class EventLoop;
    const EventLoop& loop;
    Event* next;
    Event* prev;

    mutable kj::_::Mutex mutex;
    // Hack:  The mutex on the list head is treated as protecting the next/prev links across the
    //   whole list.  The mutex on each Event other than the head is treated as protecting that
    //   event's armed/disarmed state.
  };

protected:
  // -----------------------------------------------------------------
  // Subclasses should implement these.

  virtual void prepareToSleep() noexcept = 0;
  // Called just before `sleep()`.  `sleep()` may or may not actually be called after this -- it's
  // possible that some other work will be done and then `prepareToSleep()` will be called again.

  virtual void sleep() = 0;
  // Do not return until `wake()` is called.  Always preceded by a call to `prepareToSleep()`.

  virtual void wake() const = 0;
  // Cancel any calls to sleep() that occurred *after* the last call to `prepareToSleep()`.
  // May be called from a different thread.  The interaction with `prepareToSleep()` is important:
  // a `wake()` may occur between a call to `prepareToSleep()` and `sleep()`, in which case
  // the subsequent `sleep()` must return immediately.  `wake()` may be called any time an event
  // is armed; it should return quickly if the loop isn't prepared to sleep.

private:
  class EventListHead: public Event {
  public:
    inline EventListHead(EventLoop& loop): Event(loop) {}
    void fire() override;  // throws
  };

  EventListHead queue;

  template <typename T, typename Func, typename ErrorFunc>
  auto thereImpl(Promise<T>&& promise, Func&& func, ErrorFunc&& errorHandler) const
      -> Own<_::PromiseNode<_::FixVoid<PromiseType<_::ReturnType<Func, T>>>>>;
  // Shared implementation of there() and Promise::then().

  void loopWhile(bool& keepGoing);
  // Run the event loop until keepGoing becomes false.

  template <typename>
  friend class Promise;
};

// -------------------------------------------------------------------

class SimpleEventLoop final: public EventLoop {
  // A simple EventLoop implementation that does not know how to wait for any external I/O.

public:
  SimpleEventLoop();
  ~SimpleEventLoop() noexcept(false);

protected:
  void prepareToSleep() noexcept override;
  void sleep() override;
  void wake() const override;

private:
  int preparedToSleep = 0;
};

// -------------------------------------------------------------------

template <typename T>
class Promise {
  // The basic primitive of asynchronous computation in KJ.  Similar to "futures", but more
  // powerful.  Similar to E promises and JavaScript Promises/A.
  //
  // A Promise represents a promise to produce a value of type T some time in the future.  Once
  // that value has been produced, the promise is "fulfilled".  Alternatively, a promise can be
  // "broken", with an Exception describing what went wrong.
  //
  // Promises are linear types -- they are moveable but not copyable.  If a Promise is destroyed
  // or goes out of scope (without being moved elsewhere), any ongoing asynchronous operations
  // meant to fulfill the promise will be canceled if possible.
  //
  // To use the result of a Promise, you must call `then()` and supply a callback function to
  // call with the result.  `then()` returns another promise, for the result of the callback.
  // Any time that this would result in Promise<Promise<T>>, the promises are collapsed into a
  // simple Promise<T> that first waits for the outer promise, then the inner.
  //
  // You may implicitly convert a value of type T to an already-fulfilled Promise<T>.
  //
  // To adapt a non-Promise-based asynchronous API to promises, use `newAdaptedPromise()`.
  //
  // Systems using promises should consider supporting the concept of "pipelining".  Pipelining
  // means allowing a caller to start issuing method calls against a promised object before the
  // promise has actually been fulfilled.  This is particularly useful if the promise is for a
  // remote object living across a network, as this can avoid round trips when chaining a series
  // of calls.  It is suggested that any class T which supports pipelining implement a subclass of
  // Promise<T> which adds "eventual send" methods -- methods which, when called, say "please
  // invoke the corresponding method on the promised value once it is available".  These methods
  // should in turn return promises for the eventual results of said invocations.
  //
  // KJ Promises are based on E promises:
  //   http://wiki.erights.org/wiki/Walnut/Distributed_Computing#Promises
  //
  // KJ Promises are also inspired in part by the evolving standards for JavaScript/ECMAScript
  // promises, which are themselves influenced by E promises:
  //   http://promisesaplus.com/
  //   https://github.com/domenic/promises-unwrapping

public:
  Promise(_::FixVoid<T>&& value);
  inline Promise(decltype(nullptr)) {}
  inline ~Promise() { absolve(); }
  Promise(Promise&&) = default;
  Promise& operator=(Promise&&) = default;

  template <typename Func, typename ErrorFunc = PropagateException>
  auto then(Func&& func, ErrorFunc&& errorHandler = PropagateException())
      -> Promise<PromiseType<_::ReturnType<Func, T>>>;
  // Mostly equivalent to `EventLoop::current().there(kj::mv(*this), func, errorHandler)`.
  //
  // Note that `then()` consumes the promise on which it is called, in the sense of move semantics.
  // After returning, the original promise is no longer valid, but `then()` returns a new promise.
  //
  // As an optimization, if the callback function `func` does _not_ return another promise, then
  // execution of `func` itself may be delayed until its result is known to be needed.  The
  // here expectation is that `func` is just doing some transformation on the results, not
  // scheduling any other actions, therefore the system doesn't need to be proactive about
  // evaluating it.  This way, a chain of trivial then() transformations can be executed all at
  // once without repeatedly re-scheduling through the event loop.
  //
  // On the other hand, if `func` _does_ return another promise, then the system evaluates `func`
  // as soon as possible, because the promise it returns might be for a newly-scheduled
  // long-running asynchronous task.
  //
  // On the gripping hand, `EventLoop::there()` is _always_ proactive about evaluating `func`.  This
  // is because `there()` is commonly used to schedule a long-running computation on another thread.
  // It is important that such a computation begin as soon as possible, even if no one is yet
  // waiting for the result.
  //
  // In most cases, none of the above makes a difference and you need not worry about it.

  T wait();
  // Equivalent to `EventLoop::current().wait(kj::mv(*this))`.  WARNING:  Although `wait()`
  // advances the event loop, calls to `wait()` obviously can only return in the reverse of the
  // order in which they were made.  `wait()` should therefore be considered a hack that should be
  // avoided.  Consider using it only in high-level and one-off code.  In deep library code, use
  // `then()` instead.
  //
  // Note that `wait()` consumes the promise on which it is called, in the sense of move semantics.
  // After returning, the promise is no longer valid, and cannot be `wait()`ed on or `then()`ed
  // again.

  void absolve();
  // Explicitly cancel the async operation and free all related local data structures.  This is
  // called automatically by Promise's destructor, but is sometimes useful to call explicitly.
  //
  // Any exceptions thrown by destructors of objects that were handling the async operation will
  // be caught and discarded, on the assumption that such exceptions are a side-effect of deleting
  // these structures while they were in the middle of doing something.  Presumably, you do not
  // care.  In contrast, if you were to call `then()` or `wait()`, such exceptions would be caught
  // and propagated.

private:
  Own<_::PromiseNode<_::FixVoid<T>>> node;

  Promise(Own<_::PromiseNode<_::FixVoid<T>>>&& node): node(kj::mv(node)) {}

  friend class EventLoop;
  template <typename>
  friend class _::ChainPromiseNode;
  template <typename U, typename Adapter, typename... Params>
  friend Promise<U> newAdaptedPromise(Params&&... adapterConstructorParams);
};

// -------------------------------------------------------------------
// Advanced promise construction

template <typename T>
class PromiseFulfiller {
  // A callback which can be used to fulfill a promise.  Only the first call to fulfill() or
  // reject() matters; subsequent calls are ignored.

public:
  virtual void fulfill(T&& value) = 0;
  // Fulfill the promise with the given value.

  virtual void reject(Exception&& exception) = 0;
  // Reject the promise with an error.

  virtual bool isWaiting() = 0;
  // Returns true if the promise is still unfulfilled and someone is potentially waiting for it.
  // Returns false if fulfill()/reject() has already been called *or* if the promise to be
  // fulfilled has been discarded and therefore the result will never be used anyway.

  template <typename Func>
  bool rejectIfThrows(Func&& func);
  // Call the function (with no arguments) and return true.  If an exception is thrown, call
  // `fulfiller.reject()` and then return false.  When compiled with exceptions disabled,
  // non-fatal exceptions are still detected and handled correctly.
};

template <>
class PromiseFulfiller<void> {
  // Specialization of PromiseFulfiller for void promises.  See PromiseFulfiller<T>.

public:
  virtual void fulfill(_::Void&& value = _::Void()) = 0;
  // Call with zero parameters.  The parameter is a dummy that only exists so that subclasses don't
  // have to specialize for <void>.

  virtual void reject(Exception&& exception) = 0;
  virtual bool isWaiting() = 0;

  template <typename Func>
  bool rejectIfThrows(Func&& func);
};

template <typename T, typename Adapter, typename... Params>
Promise<T> newAdaptedPromise(Params&&... adapterConstructorParams);
// Creates a new promise which owns an instance of `Adapter` which encapsulates the operation
// that will eventually fulfill the promise.  This is primarily useful for adapting non-KJ
// asynchronous APIs to use promises.
//
// An instance of `Adapter` will be allocated and owned by the returned `Promise`.  A
// `PromiseFulfiller<T>&` will be passed as the first parameter to the adapter's constructor,
// and `adapterConstructorParams` will be forwarded as the subsequent parameters.  The adapter
// is expected to perform some asynchronous operation and call the `PromiseFulfiller<T>` once
// it is finished.
//
// The adapter is destroyed when its owning Promise is destroyed.  This may occur before the
// Promise has been fulfilled.  In this case, the adapter's destructor should cancel the
// asynchronous operation.  Once the adapter is destroyed, the fulfillment callback cannot be
// called.  If the callback may already be in progress in another thread, then the destructor
// must block until the callback returns.
//
// An adapter implementation should be carefully written to ensure that it cannot accidentally
// be left unfulfilled permanently because of an exception.  Consider making liberal use of
// `PromiseFulfiller<T>::rejectIfThrows()`.

template <typename T>
struct PromiseFulfillerPair {
  Promise<T> promise;
  Own<PromiseFulfiller<T>> fulfiller;
};

template <typename T>
PromiseFulfillerPair<T> newPromiseAndFulfiller();
// Construct a Promise and a separate PromiseFulfiller which can be used to fulfill the promise.
// If the PromiseFulfiller is destroyed before either of its methods are called, the Promise is
// implicitly rejected.
//
// Although this function is easier to use than `newAdaptedPromise()`, it has the serious drawback
// that there is no way to handle cancellation (i.e. detect when the Promise is discarded).

// =======================================================================================
// internal implementation details follow

namespace _ {  // private

#define _kJ_ALREADY_READY reinterpret_cast< ::kj::EventLoop::Event*>(1)

template <typename T>
struct ExceptionOr {
  ExceptionOr() = default;
  ExceptionOr(T&& value): value(kj::mv(value)) {}
  ExceptionOr(bool, Exception&& exception): exception(kj::mv(exception)) {}

  void addException(Exception&& exception) {
    if (this->exception == nullptr) {
      this->exception = kj::mv(exception);
    }
  }

  Maybe<T> value;
  Maybe<Exception> exception;
};

template <typename T>
class PromiseNode {
  // A Promise<T> contains a chain of PromiseNodes tracking the pending transformations.
  //
  // TODO(perf):  Maybe PromiseNode should not be a template?  ExceptionOr<T> could subclass some
  //   generic type, and then only certain key pieces of code that really need to know what T is
  //   would need to be templated.  Several of the node types might not need any templating at
  //   all.  This would save a lot of code generation.

public:
  virtual bool onReady(EventLoop::Event& event) noexcept = 0;
  // Returns true if already ready, otherwise arms the given event when ready.

  virtual ExceptionOr<T> get() noexcept = 0;
  // Get the result.  Can only be called once, and only after the node is ready.  Must be
  // called directly from the event loop, with no application code on the stack.

  virtual Maybe<const EventLoop&> getSafeEventLoop() noexcept = 0;
  // Returns an EventLoop from which get() and onReady() may safely be called.  If the node has
  // no preference, it should return null.

  inline bool isSafeEventLoop(const EventLoop& loop) {
    KJ_IF_MAYBE(preferred, getSafeEventLoop()) {
      return preferred == &loop;
    } else {
      return true;
    }
  }

protected:
  static bool atomicOnReady(EventLoop::Event*& onReadyEvent, EventLoop::Event& newEvent) {
    // If onReadyEvent is null, atomically set it to point at newEvent and return false.
    // If onReadyEvent is _kJ_ALREADY_READY, return true.
    // Useful for implementing onReady() thread-safely.

    EventLoop::Event* oldEvent = nullptr;
    if (__atomic_compare_exchange_n(&onReadyEvent, &oldEvent, &newEvent, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
      // Event was swapped in and will be called later.
      return false;
    } else {
      // `onReadyEvent` is not null.  If it is _kJ_ALREADY_READY then this promise was fulfilled
      // before any dependent existed, otherwise there is already a different dependent.
      KJ_IREQUIRE(oldEvent == _kJ_ALREADY_READY, "onReady() can only be called once.");
      return true;
    }
  }

  static void atomicReady(EventLoop::Event*& onReadyEvent) {
    // If onReadyEvent is null, atomically set it to _kJ_ALREADY_READY.
    // Otherwise, arm whatever it points at.
    // Useful for firing events in conjuction with atomicOnReady().

    EventLoop::Event* oldEvent = nullptr;
    if (!__atomic_compare_exchange_n(&onReadyEvent, &oldEvent, _kJ_ALREADY_READY, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
      oldEvent->arm();
    }
  }
};

template <typename T>
class ImmediatePromiseNode final: public PromiseNode<T> {
  // A promise that has already been resolved to an immediate value or exception.

public:
  ImmediatePromiseNode(ExceptionOr<T>&& result): result(kj::mv(result)) {}

  bool onReady(EventLoop::Event& event) noexcept override { return true; }
  ExceptionOr<T> get() noexcept override { return kj::mv(result); }
  Maybe<const EventLoop&> getSafeEventLoop() noexcept override { return nullptr; }

private:
  ExceptionOr<T> result;
};

template <typename T, typename DepT, typename Func, typename ErrorFunc>
class TransformPromiseNode final: public PromiseNode<T> {
  // A PromiseNode that transforms the result of another PromiseNode through an application-provided
  // function (implements `then()`).

public:
  TransformPromiseNode(const EventLoop& loop, Own<PromiseNode<DepT>>&& dependency,
                       Func&& func, ErrorFunc&& errorHandler)
      : loop(loop), dependency(kj::mv(dependency)), func(kj::fwd<Func>(func)),
        errorHandler(kj::fwd<ErrorFunc>(errorHandler)) {}

  bool onReady(EventLoop::Event& event) noexcept override {
    return dependency->onReady(event);
  }

  ExceptionOr<T> get() noexcept override {
    ExceptionOr<T> result;

    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
      ExceptionOr<DepT> depResult = dependency->get();
      KJ_IF_MAYBE(depException, depResult.exception) {
        result = handle(errorHandler(kj::mv(*depException)));
      } else KJ_IF_MAYBE(depValue, depResult.value) {
        result = handle(MaybeVoidCaller<DepT, T>::apply(func, kj::mv(*depValue)));
      }
    })) {
      result.addException(kj::mv(*exception));
    }

    return kj::mv(result);
  }

  Maybe<const EventLoop&> getSafeEventLoop() noexcept override {
    return loop;
  }

private:
  const EventLoop& loop;
  Own<PromiseNode<DepT>> dependency;
  Func func;
  ErrorFunc errorHandler;

  ExceptionOr<T> handle(T&& value) {
    return kj::mv(value);
  }
  ExceptionOr<T> handle(PropagateException::Bottom&& value) {
    return ExceptionOr<T>(false, value.asException());
  }
};

template <typename T>
class ChainPromiseNode final: public PromiseNode<T>, private EventLoop::Event {
  // Adapts a PromiseNode<Promise<T>> to PromiseNode<T>, by first waiting for the outer promise,
  // then waiting for the inner promise.

public:
  inline ChainPromiseNode(const EventLoop& loop, Own<PromiseNode<Promise<UnfixVoid<T>>>> step1)
      : Event(loop), state(PRE_STEP1), step1(kj::mv(step1)) {
    KJ_IREQUIRE(this->step1->isSafeEventLoop(loop));
    arm();
  }

  ~ChainPromiseNode() {
    disarm();
    switch (state) {
      case PRE_STEP1:
      case STEP1:
        dtor(step1);
        break;
      case STEP2:
        dtor(step2);
        break;
    }
  }

  bool onReady(EventLoop::Event& event) noexcept override {
    switch (state) {
      case PRE_STEP1:
      case STEP1:
        KJ_IREQUIRE(onReadyEvent == nullptr, "onReady() can only be called once.");
        onReadyEvent = &event;
        return false;
      case STEP2:
        return step2->onReady(event);
    }
    KJ_UNREACHABLE;
  }

  ExceptionOr<T> get() noexcept override {
    KJ_IREQUIRE(state == STEP2);
    return step2->get();
  }

  Maybe<const EventLoop&> getSafeEventLoop() noexcept override {
    return getEventLoop();
  }

private:
  enum State {
    PRE_STEP1,  // Between the constructor and initial call to fire().
    STEP1,
    STEP2
  };

  State state;

  union {
    Own<PromiseNode<Promise<UnfixVoid<T>>>> step1;
    Own<PromiseNode<T>> step2;
  };

  EventLoop::Event* onReadyEvent = nullptr;

  void fire() override {
    if (state == PRE_STEP1 && !step1->onReady(*this)) {
      state = STEP1;
      return;
    }

    KJ_IREQUIRE(state != STEP2);

    ExceptionOr<Promise<UnfixVoid<T>>> intermediate = step1->get();

    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
      dtor(step1);
    })) {
      intermediate.addException(kj::mv(*exception));
    }

    // We're in a dangerous state here where neither step1 nor step2 is initialized, but we know
    // that none of the below can throw until we set state = STEP2.

    KJ_IF_MAYBE(exception, intermediate.exception) {
      // There is an exception.  If there is also a value, delete it.
      kj::runCatchingExceptions([&,this]() { intermediate.value = nullptr; });
      // Now set step2 to a rejected promise.
      ctor(step2, heap<ImmediatePromiseNode<T>>(ExceptionOr<T>(false, kj::mv(*exception))));
    } else KJ_IF_MAYBE(value, intermediate.value) {
      // There is a value and no exception.  The value is itself a promise.  Adopt it as our
      // step2.
      ctor(step2, kj::mv(value->node));
    } else {
      // We can only get here if step1->get() returned neither an exception nor a
      // value, which never actually happens.
      ctor(step2, heap<ImmediatePromiseNode<T>>(ExceptionOr<T>()));
    }
    state = STEP2;

    if (onReadyEvent != nullptr) {
      if (step2->onReady(*onReadyEvent)) {
        onReadyEvent->arm();
      }
    }
  }
};

template <typename T>
Own<PromiseNode<FixVoid<T>>> maybeChain(
    Own<PromiseNode<Promise<T>>>&& node, const EventLoop& loop) {
  return heap<ChainPromiseNode<FixVoid<T>>>(loop, kj::mv(node));
}

template <typename T>
Own<PromiseNode<T>>&& maybeChain(Own<PromiseNode<T>>&& node, const EventLoop& loop) {
  return kj::mv(node);
}

template <typename T>
class CrossThreadPromiseNode final: public PromiseNode<T>, private EventLoop::Event {
  // A PromiseNode that safely imports a promised value from one EventLoop to another (which
  // implies crossing threads).

public:
  CrossThreadPromiseNode(const EventLoop& loop, Own<PromiseNode<T>>&& dependent)
      : Event(loop), dependent(kj::mv(dependent)) {
    KJ_IREQUIRE(this->dependent->isSafeEventLoop(loop));

    // The constructor may be called from any thread, so before we can even call onReady() we need
    // to switch threads.
    arm();
  }

  ~CrossThreadPromiseNode() {
    disarm();
  }

  bool onReady(EventLoop::Event& event) noexcept override {
    return PromiseNode<T>::atomicOnReady(onReadyEvent, event);
  }

  ExceptionOr<T> get() noexcept override {
    KJ_IF_MAYBE(r, result) {
      return kj::mv(*r);
    } else {
      KJ_IREQUIRE(false, "Called get() before ready.");
      KJ_UNREACHABLE;
    }
  }

  Maybe<const EventLoop&> getSafeEventLoop() noexcept override {
    return nullptr;
  }

private:
  Own<PromiseNode<T>> dependent;
  EventLoop::Event* onReadyEvent = nullptr;

  Maybe<ExceptionOr<T>> result;

  bool isWaiting = false;

  void fire() override {
    if (!isWaiting && !this->dependent->onReady(*this)) {
      isWaiting = true;
    } else {
      ExceptionOr<T> result = dependent->get();
      KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
        auto deleteMe = kj::mv(dependent);
      })) {
        result.addException(kj::mv(*exception));
      }
      this->result = kj::mv(result);

      // If onReadyEvent is null, set it to _kJ_ALREADY_READY.  Otherwise, arm it.
      PromiseNode<T>::atomicReady(onReadyEvent);
    }
  }
};

template <typename T>
Own<PromiseNode<T>> makeSafeForLoop(Own<PromiseNode<T>>&& node, const EventLoop* loop) {
  // If the node cannot safely be used in the given loop (thread), wrap it in one that can.

  KJ_IF_MAYBE(preferred, node->getSafeEventLoop()) {
    if (loop != preferred) {
      return heap<CrossThreadPromiseNode<T>>(*preferred, kj::mv(node));
    }
  }
  return kj::mv(node);
}

template <typename T>
Own<PromiseNode<T>> spark(Own<PromiseNode<T>>&& node, const EventLoop& loop) {
  // Forces evaluation of the given node to begin as soon as possible, even if no one is waiting
  // on it.
  return heap<CrossThreadPromiseNode<T>>(loop, kj::mv(node));
}

template <typename T, typename Adapter>
class AdapterPromiseNode final: public PromiseNode<T>, private PromiseFulfiller<UnfixVoid<T>> {
  // A PromiseNode that wraps a PromiseAdapter.

public:
  template <typename... Params>
  AdapterPromiseNode(Params&&... params)
      : adapter(static_cast<PromiseFulfiller<UnfixVoid<T>>&>(*this), kj::fwd<Params>(params)...) {}

  bool onReady(EventLoop::Event& event) noexcept override {
    return PromiseNode<T>::atomicOnReady(onReadyEvent, event);
  }

  ExceptionOr<T> get() noexcept override {
    return kj::mv(result);
  }

  Maybe<const EventLoop&> getSafeEventLoop() noexcept override {
    // We're careful to be thread-safe so any thread is OK.
    return nullptr;
  }

private:
  Adapter adapter;
  EventLoop::Event* onReadyEvent = nullptr;
  ExceptionOr<T> result;

  void fulfill(T&& value) override {
    if (isWaiting()) {
      result = ExceptionOr<T>(kj::mv(value));
      PromiseNode<T>::atomicReady(onReadyEvent);
    }
  }

  void reject(Exception&& exception) override {
    if (isWaiting()) {
      result = ExceptionOr<T>(false, kj::mv(exception));
      PromiseNode<T>::atomicReady(onReadyEvent);
    }
  }

  bool isWaiting() override {
    return result.value == nullptr && result.exception == nullptr;
  }
};

// =======================================================================================

class WaitEvent: public EventLoop::Event {
public:
  WaitEvent(const EventLoop& loop): Event(loop) {}
  ~WaitEvent() { disarm(); }

  bool keepGoing = true;

  // TODO(now):  Move to .c++ file
  void fire() override {
    keepGoing = false;
  }
};

}  // namespace _ (private)

template <typename T>
T EventLoop::wait(Promise<T>&& promise) {
  // Make sure we can safely call node->get() outside of the event loop.
  Own<_::PromiseNode<_::FixVoid<T>>> node = _::makeSafeForLoop(kj::mv(promise.node), nullptr);

  _::WaitEvent event(*this);
  if (!node->onReady(event)) {
    loopWhile(event.keepGoing);
  }

  _::ExceptionOr<_::FixVoid<T>> result = node->get();

  KJ_IF_MAYBE(exception, result.exception) {
    KJ_IF_MAYBE(value, result.value) {
      throwRecoverableException(kj::mv(*exception));
      return _::returnMaybeVoid(kj::mv(*value));
    } else {
      throwFatalException(kj::mv(*exception));
    }
  } else KJ_IF_MAYBE(value, result.value) {
    return _::returnMaybeVoid(kj::mv(*value));
  } else {
    // Result contained neither a value nor an exception?
    KJ_UNREACHABLE;
  }
}

template <typename Func>
auto EventLoop::evalLater(Func&& func) const -> Promise<PromiseType<_::ReturnType<Func, void>>> {
  return there(Promise<void>(_::Void()), kj::fwd<Func>(func));
}

template <typename T, typename Func, typename ErrorFunc>
auto EventLoop::there(Promise<T>&& promise, Func&& func, ErrorFunc&& errorHandler) const
    -> Promise<PromiseType<_::ReturnType<Func, T>>> {
  return _::spark(thereImpl(
      kj::mv(promise), kj::fwd<Func>(func), kj::fwd<ErrorFunc>(errorHandler)), *this);
}

template <typename T, typename Func, typename ErrorFunc>
auto EventLoop::thereImpl(Promise<T>&& promise, Func&& func, ErrorFunc&& errorHandler) const
    -> Own<_::PromiseNode<_::FixVoid<PromiseType<_::ReturnType<Func, T>>>>> {
  typedef _::FixVoid<_::ReturnType<Func, T>> ResultT;

  Own<_::PromiseNode<ResultT>> intermediate =
      heap<_::TransformPromiseNode<ResultT, _::FixVoid<T>, Func, ErrorFunc>>(
          *this, _::makeSafeForLoop(kj::mv(promise.node), this),
          kj::fwd<Func>(func), kj::fwd<ErrorFunc>(errorHandler));
  return _::maybeChain(kj::mv(intermediate), *this);
}

template <typename T>
Promise<T>::Promise(_::FixVoid<T>&& value)
    : node(heap<_::ImmediatePromiseNode<_::FixVoid<T>>>(kj::mv(value))) {}

template <typename T>
template <typename Func, typename ErrorFunc>
auto Promise<T>::then(Func&& func, ErrorFunc&& errorHandler)
    -> Promise<PromiseType<_::ReturnType<Func, T>>> {
  return EventLoop::current().thereImpl(
      kj::mv(*this), kj::fwd<Func>(func), kj::fwd<ErrorFunc>(errorHandler));
}

template <typename T>
T Promise<T>::wait() {
  return EventLoop::current().wait(kj::mv(*this));
}

template <typename T>
void Promise<T>::absolve() {
  runCatchingExceptions([this]() { auto deleteMe = kj::mv(node); });
}

// =======================================================================================

namespace _ {  // private

template <typename T>
class WeakFulfiller final: public PromiseFulfiller<T> {
  // A wrapper around PromiseFulfiller which can be detached.
public:
  WeakFulfiller(): inner(nullptr) {}

  void fulfill(FixVoid<T>&& value) override {
    auto lock = inner.lockExclusive();
    if (*lock != nullptr) {
      (*lock)->fulfill(kj::mv(value));
    }
  }

  void reject(Exception&& exception) override {
    auto lock = inner.lockExclusive();
    if (*lock != nullptr) {
      (*lock)->reject(kj::mv(exception));
    }
  }

  bool isWaiting() override {
    auto lock = inner.lockExclusive();
    return *lock != nullptr && (*lock)->isWaiting();
  }

  void attach(PromiseFulfiller<T>& newInner) {
    inner.getWithoutLock() = &newInner;
  }

  void detach() {
    *inner.lockExclusive() = nullptr;
  }

private:
  MutexGuarded<PromiseFulfiller<T>*> inner;
};

template <typename T>
class PromiseAndFulfillerAdapter {
public:
  PromiseAndFulfillerAdapter(PromiseFulfiller<T>& fulfiller,
                             WeakFulfiller<T>& wrapper)
      : wrapper(wrapper) {
    wrapper.attach(fulfiller);
  }

  ~PromiseAndFulfillerAdapter() {
    wrapper.detach();
  }

private:
  WeakFulfiller<T>& wrapper;
};

}  // namespace _ (private)

template <typename T, typename Adapter, typename... Params>
Promise<T> newAdaptedPromise(Params&&... adapterConstructorParams) {
  return Promise<T>(heap<_::AdapterPromiseNode<_::FixVoid<T>, Adapter>>(
      kj::fwd<Params>(adapterConstructorParams)...));
}

template <typename T>
PromiseFulfillerPair<T> newPromiseAndFulfiller() {
  auto wrapper = heap<_::WeakFulfiller<T>>();
  Promise<T> promise = newAdaptedPromise<T, _::PromiseAndFulfillerAdapter<T>>(*wrapper);
  return PromiseFulfillerPair<T> { kj::mv(promise), kj::mv(wrapper) };
}

}  // namespace kj

#endif  // KJ_ASYNC_H_
