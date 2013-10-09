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

#include "exception.h"
#include "mutex.h"
#include "refcount.h"

namespace kj {

class EventLoop;
class SimpleEventLoop;

template <typename T>
class Promise;
template <typename T>
class PromiseFulfiller;
template <typename T>
struct PromiseFulfillerPair;

// =======================================================================================
// ***************************************************************************************
// This section contains various internal stuff that needs to be declared upfront.
// Scroll down to `class EventLoop` or `class Promise` for the public interfaces.
// ***************************************************************************************
// =======================================================================================

namespace _ {  // private

template <typename T> struct JoinPromises_ { typedef T Type; };
template <typename T> struct JoinPromises_<Promise<T>> { typedef T Type; };

template <typename T>
using JoinPromises = typename JoinPromises_<T>::Type;
// If T is Promise<U>, resolves to U, otherwise resolves to T.

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
  Bottom operator()(const  Exception& e) {
    return Bottom(kj::cp(e));
  }
};

template <typename Func, typename T>
struct ReturnType_ { typedef decltype(instance<Func>()(instance<T>())) Type; };
template <typename Func>
struct ReturnType_<Func, void> { typedef decltype(instance<Func>()()) Type; };

template <typename Func, typename T>
using ReturnType = typename ReturnType_<Func, T>::Type;
// The return type of functor Func given a parameter of type T, with the special exception that if
// T is void, this is the return type of Func called with no arguments.

struct Void {};
// Application code should NOT refer to this!  See `kj::READY_NOW` instead.

template <typename T> struct FixVoid_ { typedef T Type; };
template <> struct FixVoid_<void> { typedef Void Type; };
template <typename T> using FixVoid = typename FixVoid_<T>::Type;
// FixVoid<T> is just T unless T is void in which case it is _::Void (an empty struct).

template <typename T> struct UnfixVoid_ { typedef T Type; };
template <> struct UnfixVoid_<Void> { typedef void Type; };
template <typename T> using UnfixVoid = typename UnfixVoid_<T>::Type;
// UnfixVoid is the opposite of FixVoid.

template <typename T> struct Forked_ { typedef T Type; };
template <typename T> struct Forked_<T&> { typedef const T& Type; };
template <typename T> struct Forked_<Own<T>> { typedef Own<const T> Type; };
template <typename T> using Forked = typename Forked_<T>::Type;
// Forked<T> transforms T as a result of being forked.  If T is an owned pointer or reference,
// it becomes const.

template <typename In, typename Out>
struct MaybeVoidCaller {
  // Calls the function converting a Void input to an empty parameter list and a void return
  // value to a Void output.

  template <typename Func>
  static inline Out apply(Func& func, In&& in) {
    return func(kj::mv(in));
  }
};
template <typename In, typename Out>
struct MaybeVoidCaller<In&, Out> {
  template <typename Func>
  static inline Out apply(Func& func, In& in) {
    return func(in);
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
template <typename In>
struct MaybeVoidCaller<In&, Void> {
  template <typename Func>
  static inline Void apply(Func& func, In& in) {
    func(in);
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

class ExceptionOrValue;
class PromiseNode;
class ChainPromiseNode;
template <typename T>
class ForkHub;

}  // namespace _ (private)

// =======================================================================================
// ***************************************************************************************
// User-relevant interfaces start here.
// ***************************************************************************************
// =======================================================================================

template <typename Func, typename T>
using PromiseForResult = Promise<_::JoinPromises<_::ReturnType<Func, T>>>;
// Evaluates to the type of Promise for the result of calling functor type Func with parameter type
// T.  If T is void, then the promise is for the result of calling Func with no arguments.  If
// Func itself returns a promise, the promises are joined, so you never get Promise<Promise<T>>.

class EventLoop {
  // Represents a queue of events being executed in a loop.  Most code won't interact with
  // EventLoop directly, but instead use `Promise`s to interact with it indirectly.  See the
  // documentation for `Promise`.
  //
  // You will need to construct an `EventLoop` at the top level of your program.  You can then
  // use it to construct some promises and wait on the result.  Example:
  //
  //     int main() {
  //       SimpleEventLoop loop;
  //
  //       // Most code that does I/O needs to be run from within an
  //       // EventLoop, so it can use Promise::then().  So, we need to
  //       // use `evalLater()` to run `getHttp()` inside the event
  //       // loop.
  //       Promise<String> textPromise = loop.evalLater(
  //           []() { return getHttp("http://example.com"); });
  //
  //       // Now we can wait for the promise to complete.
  //       String text = loop.wait(kj::mv(textPromise));
  //       print(text);
  //       return 0;
  //     }

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

  template <typename Func>
  auto evalLater(Func&& func) const -> PromiseForResult<Func, void>;
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
  // If you schedule several evaluations with `evalLater`, they will be executed in order.
  //
  // `evalLater()` is equivalent to `there()` chained on `Promise<void>(READY_NOW)`.

  template <typename T, typename Func, typename ErrorFunc = _::PropagateException>
  PromiseForResult<Func, T> there(Promise<T>&& promise, Func&& func,
                                  ErrorFunc&& errorHandler = _::PropagateException()) const
                                  KJ_WARN_UNUSED_RESULT;
  // Like `Promise::then()`, but schedules the continuation to be executed on *this* EventLoop
  // rather than the thread's current loop.  See Promise::then().

  // -----------------------------------------------------------------
  // Low-level interface.

  class Event {
    // An event waiting to be executed.  Not for direct use by applications -- promises use this
    // internally.

  public:
    Event(const EventLoop& loop): loop(loop), next(nullptr), prev(nullptr) {}
    ~Event() noexcept(false);
    KJ_DISALLOW_COPY(Event);

    enum Schedule {
      PREEMPT,
      // The event gets added to the front of the queue, so that it runs immediately after the
      // event that is currently running, even if other events were armed before that.  If one
      // event's firing arms multiple other events, those events still occur in the order in which
      // they were armed.  PREEMPT should generally only be used when arming an event to run in
      // the current thread.

      YIELD
      // The event gets added to the end of the queue, so that it runs after all other events
      // currently scheduled.
    };

    void arm(Schedule schedule);
    // Enqueue this event so that run() will be called from the event loop soon.  Does nothing
    // if the event is already armed.

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
  // Called just before `sleep()`.  After calling this, the caller checks if any events are
  // scheduled.  If so, it calls `wake()`.  Then, whether or not events were scheduled, it calls
  // `sleep()`.  Thus, `prepareToSleep()` is always followed by exactly one call to `sleep()`.

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

  mutable EventListHead queue;
  // Head of the event list.  queue.mutex protects all next/prev pointers across the list, as well
  // as `insertPoint`.  Each actual event's mutex protects its own `fire()` callback.

  mutable Event* insertPoint;
  // The next event after the one that is currently firing.  New events are inserted just before
  // this event.  When the fire callback completes, the loop continues at the beginning of the
  // queue -- thus, it starts by running any events that were just enqueued by the previous
  // callback.  This keeps related events together.

  template <typename T, typename Func, typename ErrorFunc>
  Own<_::PromiseNode> thereImpl(Promise<T>&& promise, Func&& func, ErrorFunc&& errorHandler,
                                Event::Schedule schedule) const;
  // Shared implementation of there() and Promise::then().

  void waitImpl(Own<_::PromiseNode> node, _::ExceptionOrValue& result);
  // Run the event loop until `node` is fulfilled, and then `get()` its result into `result`.

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
  mutable int preparedToSleep = 0;
#if !KJ_USE_FUTEX
  mutable pthread_mutex_t mutex;
  mutable pthread_cond_t condvar;
#endif
};

// -------------------------------------------------------------------

class PromiseBase {
public:
  PromiseBase(PromiseBase&&) = default;
  PromiseBase& operator=(PromiseBase&&) = default;
  inline ~PromiseBase() { absolve(); }

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
  Own<_::PromiseNode> node;

  PromiseBase() = default;
  PromiseBase(Own<_::PromiseNode>&& node): node(kj::mv(node)) {}

  friend class EventLoop;
  friend class _::ChainPromiseNode;
  template <typename>
  friend class Promise;
};

template <typename T>
class Promise: public PromiseBase {
  // The basic primitive of asynchronous computation in KJ.  Similar to "futures", but more
  // powerful.  Similar to E promises and JavaScript Promises/A.
  //
  // A Promise represents a promise to produce a value of type T some time in the future.  Once
  // that value has been produced, the promise is "fulfilled".  Alternatively, a promise can be
  // "broken", with an Exception describing what went wrong.  You may implicitly convert a value of
  // type T to an already-fulfilled Promise<T>.  You may implicitly convert the constant
  // `kj::READY_NOW` to an already-fulfilled Promise<void>.
  //
  // Promises are linear types -- they are moveable but not copyable.  If a Promise is destroyed
  // or goes out of scope (without being moved elsewhere), any ongoing asynchronous operations
  // meant to fulfill the promise will be canceled if possible.
  //
  // To use the result of a Promise, you must call `then()` and supply a callback function to
  // call with the result.  `then()` returns another promise, for the result of the callback.
  // Any time that this would result in Promise<Promise<T>>, the promises are collapsed into a
  // simple Promise<T> that first waits for the outer promise, then the inner.  Example:
  //
  //     // Open a remote file, read the content, and then count the
  //     // number of lines of text.
  //     // Note that none of the calls here block.  `file`, `content`
  //     // and `lineCount` are all initialized immediately before any
  //     // asynchronous operations occur.  The lambda callbacks are
  //     // called later.
  //     Promise<Own<File>> file = openFtp("ftp://host/foo/bar");
  //     Promise<String> content = file.then(
  //         [](Own<File> file) -> Promise<String> {
  //           return file.readAll();
  //         });
  //     Promise<int> lineCount = content.then(
  //         [](String text) -> int {
  //           uint count = 0;
  //           for (char c: text) count += (c == '\n');
  //           return count;
  //         });
  //
  // For `then()` to work, the current thread must be looping in an `EventLoop`.  Each callback
  // is scheduled to execute in that loop.  Since `then()` schedules callbacks only on the current
  // thread's event loop, you do not need to worry about two callbacks running at the same time.
  // If you explicitly _want_ a callback to run on some other thread, you can schedule it there
  // using the `EventLoop` interface.  You will need to set up at least one `EventLoop` at the top
  // level of your program before you can use promises.
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
  Promise(_::FixVoid<T> value);
  // Construct an already-fulfilled Promise from a value of type T.  For non-void promises, the
  // parameter type is simply T.  So, e.g., in a function that returns `Promise<int>`, you can
  // say `return 123;` to return a promise that is already fulfilled to 123.
  //
  // For void promises, use `kj::READY_NOW` as the value, e.g. `return kj::READY_NOW`.

  Promise(kj::Exception&& e);
  // Construct an already-broken Promise.

  inline Promise(decltype(nullptr)) {}

  template <typename Func, typename ErrorFunc = _::PropagateException>
  PromiseForResult<Func, T> then(Func&& func, ErrorFunc&& errorHandler = _::PropagateException())
      KJ_WARN_UNUSED_RESULT;
  // Register a continuation function to be executed when the promise completes.  The continuation
  // (`func`) takes the promised value (an rvalue of type `T`) as its parameter.  The continuation
  // may return a new value; `then()` itself returns a promise for the continuation's eventual
  // result.  If the continuation itself returns a `Promise<U>`, then `then()` shall also return
  // a `Promise<U>` which first waits for the original promise, then executes the continuation,
  // then waits for the inner promise (i.e. it automatically "unwraps" the promise).
  //
  // In all cases, `then()` returns immediately.  The continuation is executed later.  The
  // continuation is always executed on the same EventLoop (and, therefore, the same thread) which
  // called `then()`, therefore no synchronization is necessary on state shared by the continuation
  // and the surrounding scope.  If no EventLoop is running on the current thread, `then()` throws
  // an exception; in this case you will have to find an explicit EventLoop instance and use
  // its `there()` method to schedule the continuation to occur in that loop.
  // `promise.then(...)` is mostly-equivalent to `EventLoop::current().there(kj::mv(promise), ...)`,
  // except for some scheduling differences described below.
  //
  // You may also specify an error handler continuation as the second parameter.  `errorHandler`
  // must be a functor taking a parameter of type `kj::Exception&&`.  It must return the same
  // type as `func` returns (except when `func` returns `Promise<U>`, in which case `errorHandler`
  // may return either `Promise<U>` or just `U`).  The default error handler simply propagates the
  // exception to the returned promise.
  //
  // Either `func` or `errorHandler` may, of course, throw an exception, in which case the promise
  // is broken.  When compiled with -fno-exceptions, the framework will detect when a non-fatal
  // exception was thrown inside of a continuation and will consider the promise broken even though
  // a (presumably garbage) result was returned.
  //
  // Note that `then()` consumes the promise on which it is called, in the sense of move semantics.
  // After returning, the original promise is no longer valid, but `then()` returns a new promise.
  // If we were targetting GCC 4.8 / Clang 3.3, this method would be rvalue-qualified; we may
  // change it to be so in the future.
  //
  // If the returned promise is destroyed before the callback runs, the callback will be canceled.
  // If the returned promise is destroyed while the callback is running in another thread, the
  // destructor will block until the callback completes.  Additionally, canceling the returned
  // promise will transitively cancel the input promise, if it hasn't already completed.  Or, if
  // `func()` already ran and returned another promise, then canceling the returned promise
  // transitively cancels that promise.  In short, once a Promise's destructor completes, you can
  // assume that any asynchronous operation it was performing has ceased (at least, locally; stuff
  // may still be happening on some remote machine).
  //
  // *Advanced implementation tips:*  Most users will never need to worry about the below, but
  // it is good to be aware of.
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
  // As another optimization, when a callback function registered with `then()` is actually
  // scheduled, it is scheduled to occur immediately, preempting other work in the event queue.
  // This allows a long chain of `then`s to execute all at once, improving cache locality by
  // clustering operations on the same data.  However, this implies that starvation can occur
  // if a chain of `then()`s takes a very long time to execute without ever stopping to wait for
  // actual I/O.  To solve this, use `EventLoop::current()`'s `evalLater()` or `there()` methods
  // to yield control; this way, all other events in the queue will get a chance to run before your
  // callback is executed.

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

  class Fork {
  public:
    virtual Promise<_::Forked<T>> addBranch() = 0;
    // Add a new branch to the fork.  The branch is equivalent to the original promise, except
    // that if T is a reference or owned pointer, the target becomes const.
  };

  Own<Fork> fork();
  // Forks the promise, so that multiple different clients can independently wait on the result.
  // `T` must be copy-constructable for this to work.  Or, in the special case where `T` is
  // `Own<U>`, `U` must have a method `Own<const U> addRef() const` which returns a new reference
  // to the same (or an equivalent) object (probably implemented via reference counting).

private:
  Promise(bool, Own<_::PromiseNode>&& node): PromiseBase(kj::mv(node)) {}
  // Second parameter prevent ambiguity with immediate-value constructor.

  template <typename>
  friend class Promise;
  friend class EventLoop;
  template <typename U, typename Adapter, typename... Params>
  friend Promise<U> newAdaptedPromise(Params&&... adapterConstructorParams);
  template <typename U>
  friend PromiseFulfillerPair<U> newPromiseAndFulfiller();
  template <typename U>
  friend PromiseFulfillerPair<U> newPromiseAndFulfiller(const EventLoop& loop);
  template <typename>
  friend class _::ForkHub;
};

constexpr _::Void READY_NOW = _::Void();
// Use this when you need a Promise<void> that is already fulfilled -- this value can be implicitly
// cast to `Promise<void>`.

// -------------------------------------------------------------------
// Hack for creating a lambda that holds an owned pointer.

template <typename Func, typename MovedParam>
class CaptureByMove {
public:
  inline CaptureByMove(Func&& func, MovedParam&& param)
      : func(kj::mv(func)), param(kj::mv(param)) {}

  template <typename... Params>
  inline auto operator()(Params&&... params)
      -> decltype(kj::instance<Func>()(kj::instance<MovedParam&&>(), kj::fwd<Params>(params)...)) {
    return func(kj::mv(param), kj::fwd<Params>(params)...);
  }

private:
  Func func;
  MovedParam param;
};

template <typename Func, typename MovedParam>
inline CaptureByMove<Func, Decay<MovedParam>> mvCapture(MovedParam&& param, Func&& func) {
  // Hack to create a "lambda" which captures a variable by moving it rather than copying or
  // referencing.  C++14 generalized captures should make this obsolete, but for now in C++11 this
  // is commonly needed for Promise continuations that own their state.  Example usage:
  //
  //    Own<Foo> ptr = makeFoo();
  //    Promise<int> promise = callRpc();
  //    promise.then(mvCapture(ptr, [](Own<Foo>&& ptr, int result) {
  //      return ptr->finish(result);
  //    }));

  return CaptureByMove<Func, Decay<MovedParam>>(kj::fwd<Func>(func), kj::mv(param));
}

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
  //
  // It is safe to call a PromiseFulfiller from any thread, as long as you only call it from one
  // thread at a time.

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
  Promise<_::JoinPromises<T>> promise;
  Own<PromiseFulfiller<T>> fulfiller;
};

template <typename T>
PromiseFulfillerPair<T> newPromiseAndFulfiller();
template <typename T>
PromiseFulfillerPair<T> newPromiseAndFulfiller(const EventLoop& loop);
// Construct a Promise and a separate PromiseFulfiller which can be used to fulfill the promise.
// If the PromiseFulfiller is destroyed before either of its methods are called, the Promise is
// implicitly rejected.
//
// Although this function is easier to use than `newAdaptedPromise()`, it has the serious drawback
// that there is no way to handle cancellation (i.e. detect when the Promise is discarded).
//
// You can arrange to fulfill a promise with another promise by using a promise type for T.  E.g.
// if `T` is `Promise<U>`, then the returned promise will be of type `Promise<U>` but the fulfiller
// will be of type `PromiseFulfiller<Promise<U>>`.  Thus you pass a `Promise<U>` to the `fulfill()`
// callback, and the promises are chained.  In this case, an `EventLoop` is needed in order to wait
// on the chained promise; you can specify one as a parameter, otherwise the current loop in the
// thread that called `newPromiseAndFulfiller` will be used.  If `T` is *not* a promise type, then
// no `EventLoop` is needed and the `loop` parameter, if specified, is ignored.

// =======================================================================================
// internal implementation details follow

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

class PromiseNode {
  // A Promise<T> contains a chain of PromiseNodes tracking the pending transformations.
  //
  // To reduce generated code bloat, PromiseNode is not a template.  Instead, it makes very hacky
  // use of pointers to ExceptionOrValue which actually point to ExceptionOr<T>, but are only
  // so down-cast in the few places that really need to be templated.  Luckily this is all
  // internal implementation details.

public:
  virtual bool onReady(EventLoop::Event& event) noexcept = 0;
  // Returns true if already ready, otherwise arms the given event when ready.

  virtual void get(ExceptionOrValue& output) noexcept = 0;
  // Get the result.  `output` points to an ExceptionOr<T> into which the result will be written.
  // Can only be called once, and only after the node is ready.  Must be called directly from the
  // event loop, with no application code on the stack.

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
  static bool atomicOnReady(EventLoop::Event*& onReadyEvent, EventLoop::Event& newEvent);
  // If onReadyEvent is null, atomically set it to point at newEvent and return false.
  // If onReadyEvent is _kJ_ALREADY_READY, return true.
  // Useful for implementing onReady() thread-safely.

  static void atomicReady(EventLoop::Event*& onReadyEvent, EventLoop::Event::Schedule schedule);
  // If onReadyEvent is null, atomically set it to _kJ_ALREADY_READY.
  // Otherwise, arm whatever it points at.
  // Useful for firing events in conjuction with atomicOnReady().
};

// -------------------------------------------------------------------

class ImmediatePromiseNodeBase: public PromiseNode {
public:
  bool onReady(EventLoop::Event& event) noexcept override;
  Maybe<const EventLoop&> getSafeEventLoop() noexcept override;
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

class TransformPromiseNodeBase: public PromiseNode {
public:
  TransformPromiseNodeBase(const EventLoop& loop, Own<PromiseNode>&& dependency);

  bool onReady(EventLoop::Event& event) noexcept override;
  void get(ExceptionOrValue& output) noexcept override;
  Maybe<const EventLoop&> getSafeEventLoop() noexcept override;

private:
  const EventLoop& loop;
  Own<PromiseNode> dependency;

  virtual void getImpl(ExceptionOrValue& output) = 0;

  template <typename, typename, typename, typename>
  friend class TransformPromiseNode;
};

template <typename T, typename DepT, typename Func, typename ErrorFunc>
class TransformPromiseNode final: public TransformPromiseNodeBase {
  // A PromiseNode that transforms the result of another PromiseNode through an application-provided
  // function (implements `then()`).

public:
  TransformPromiseNode(const EventLoop& loop, Own<PromiseNode>&& dependency,
                       Func&& func, ErrorFunc&& errorHandler)
      : TransformPromiseNodeBase(loop, kj::mv(dependency)),
        func(kj::fwd<Func>(func)), errorHandler(kj::fwd<ErrorFunc>(errorHandler)) {}

private:
  Func func;
  ErrorFunc errorHandler;

  void getImpl(ExceptionOrValue& output) override {
    ExceptionOr<DepT> depResult;
    dependency->get(depResult);
    KJ_IF_MAYBE(depException, depResult.exception) {
      output.as<T>() = handle(MaybeVoidCaller<Exception&&, T>::apply(
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
  ~ForkBranchBase();

  void hubReady() noexcept;
  // Called by the hub to indicate that it is ready.

  // implements PromiseNode ------------------------------------------
  bool onReady(EventLoop::Event& event) noexcept override;
  Maybe<const EventLoop&> getSafeEventLoop() noexcept override;

protected:
  inline const ExceptionOrValue& getHubResultRef() const;

  void releaseHub(ExceptionOrValue& output);
  // Release the hub.  If an exception is thrown, add it to `output`.

private:
  EventLoop::Event* onReadyEvent = nullptr;

  Own<ForkHubBase> hub;
  ForkBranchBase* next = nullptr;
  ForkBranchBase** prevPtr = nullptr;

  friend class ForkHubBase;
};

template <typename T> T copyOrAddRef(const T& t) { return t; }
template <typename T> Own<const T> copyOrAddRef(const Own<T>& t) { return t->addRef(); }
template <typename T> Own<const T> copyOrAddRef(const Own<const T>& t) { return t->addRef(); }

template <typename T>
class ForkBranch final: public ForkBranchBase {
  // A PromiseNode that implements one branch of a fork -- i.e. one of the branches that receives
  // a const reference.

public:
  ForkBranch(Own<ForkHubBase>&& hub): ForkBranchBase(kj::mv(hub)) {}

  void get(ExceptionOrValue& output) noexcept override {
    const ExceptionOr<T>& hubResult = getHubResultRef().template as<T>();
    KJ_IF_MAYBE(value, hubResult.value) {
      output.as<Forked<T>>().value = copyOrAddRef(*value);
    } else {
      output.as<Forked<T>>().value = nullptr;
    }
    output.exception = hubResult.exception;
    releaseHub(output);
  }
};

// -------------------------------------------------------------------

class ForkHubBase: public Refcounted, private EventLoop::Event {
public:
  ForkHubBase(const EventLoop& loop, Own<PromiseNode>&& inner, ExceptionOrValue& resultRef);
  ~ForkHubBase() noexcept(false);

  inline const ExceptionOrValue& getResultRef() const { return resultRef; }

private:
  struct BranchList {
    ForkBranchBase* first = nullptr;
    ForkBranchBase** lastPtr = &first;
  };

  Own<PromiseNode> inner;
  ExceptionOrValue& resultRef;

  bool isWaiting = false;

  MutexGuarded<BranchList> branchList;
  // Becomes null once the inner promise is ready and all branches have been notified.

  void fire() override;

  friend class ForkBranchBase;
};

template <typename T>
class ForkHub final: public ForkHubBase, public Promise<T>::Fork {
  // A PromiseNode that implements the hub of a fork.  The first call to Promise::fork() replaces
  // the promise's outer node with a ForkHub, and subsequent calls add branches to that hub (if
  // possible).

public:
  ForkHub(const EventLoop& loop, Own<PromiseNode>&& inner)
      : ForkHubBase(loop, kj::mv(inner), result) {}

  Promise<_::Forked<T>> addBranch() override {
    return Promise<_::Forked<T>>(false, kj::heap<ForkBranch<T>>(addRef(*this)));
  }

private:
  ExceptionOr<T> result;
};

inline const ExceptionOrValue& ForkBranchBase::getHubResultRef() const {
  return hub->getResultRef();
}

// -------------------------------------------------------------------

class ChainPromiseNode final: public PromiseNode, private EventLoop::Event {
public:
  ChainPromiseNode(const EventLoop& loop, Own<PromiseNode> inner, Schedule schedule);
  ~ChainPromiseNode() noexcept(false);

  bool onReady(EventLoop::Event& event) noexcept override;
  void get(ExceptionOrValue& output) noexcept override;
  Maybe<const EventLoop&> getSafeEventLoop() noexcept override;

private:
  enum State {
    PRE_STEP1,  // Between the constructor and initial call to fire().
    STEP1,
    STEP2
  };

  State state;

  Own<PromiseNode> inner;
  // In PRE_STEP1 / STEP1, a PromiseNode for a Promise<T>.
  // In STEP2, a PromiseNode for a T.

  EventLoop::Event* onReadyEvent = nullptr;

  void fire() override;
};

template <typename T>
Own<PromiseNode> maybeChain(Own<PromiseNode>&& node, const EventLoop& loop,
                            EventLoop::Event::Schedule schedule, Promise<T>*) {
  return heap<ChainPromiseNode>(loop, kj::mv(node), schedule);
}

template <typename T>
Own<PromiseNode>&& maybeChain(Own<PromiseNode>&& node, const EventLoop& loop,
                              EventLoop::Event::Schedule schedule, T*) {
  return kj::mv(node);
}

template <typename T>
Own<PromiseNode> maybeChain(Own<PromiseNode>&& node, Promise<T>*) {
  return heap<ChainPromiseNode>(EventLoop::current(), kj::mv(node), EventLoop::Event::PREEMPT);
}

template <typename T>
Own<PromiseNode>&& maybeChain(Own<PromiseNode>&& node, T*) {
  return kj::mv(node);
}

// -------------------------------------------------------------------

class CrossThreadPromiseNodeBase: public PromiseNode, private EventLoop::Event {
  // A PromiseNode that safely imports a promised value from one EventLoop to another (which
  // implies crossing threads).

public:
  CrossThreadPromiseNodeBase(const EventLoop& loop, Own<PromiseNode>&& dependency,
                             ExceptionOrValue& resultRef);
  ~CrossThreadPromiseNodeBase() noexcept(false);

  bool onReady(EventLoop::Event& event) noexcept override;
  Maybe<const EventLoop&> getSafeEventLoop() noexcept override;

private:
  Own<PromiseNode> dependency;
  EventLoop::Event* onReadyEvent = nullptr;

  ExceptionOrValue& resultRef;

  bool isWaiting = false;

  void fire() override;
};

template <typename T>
class CrossThreadPromiseNode final: public CrossThreadPromiseNodeBase {
public:
  CrossThreadPromiseNode(const EventLoop& loop, Own<PromiseNode>&& dependency)
      : CrossThreadPromiseNodeBase(loop, kj::mv(dependency), result) {}

  void get(ExceptionOrValue& output) noexcept override {
    output.as<T>() = kj::mv(result);
  }

private:
  ExceptionOr<T> result;
};

template <typename T>
Own<PromiseNode> makeSafeForLoop(Own<PromiseNode>&& node, const EventLoop& loop) {
  // If the node cannot safely be used in the given loop (thread), wrap it in one that can.

  KJ_IF_MAYBE(preferred, node->getSafeEventLoop()) {
    if (&loop != preferred) {
      return heap<CrossThreadPromiseNode<T>>(*preferred, kj::mv(node));
    }
  }
  return kj::mv(node);
}

template <typename T>
Own<PromiseNode> spark(Own<PromiseNode>&& node, const EventLoop& loop) {
  // Forces evaluation of the given node to begin as soon as possible, even if no one is waiting
  // on it.
  return heap<CrossThreadPromiseNode<T>>(loop, kj::mv(node));
}

// -------------------------------------------------------------------

class AdapterPromiseNodeBase: public PromiseNode {
public:
  bool onReady(EventLoop::Event& event) noexcept override;
  Maybe<const EventLoop&> getSafeEventLoop() noexcept override;

protected:
  inline void setReady() {
    PromiseNode::atomicReady(onReadyEvent, EventLoop::Event::PREEMPT);
  }

private:
  EventLoop::Event* onReadyEvent = nullptr;
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
    output.as<T>() = kj::mv(result);
  }

private:
  Adapter adapter;
  ExceptionOr<T> result;

  void fulfill(T&& value) override {
    if (isWaiting()) {
      result = ExceptionOr<T>(kj::mv(value));
      setReady();
    }
  }

  void reject(Exception&& exception) override {
    if (isWaiting()) {
      result = ExceptionOr<T>(false, kj::mv(exception));
      setReady();
    }
  }

  bool isWaiting() override {
    return result.value == nullptr && result.exception == nullptr;
  }
};

}  // namespace _ (private)

// =======================================================================================

template <typename T>
T EventLoop::wait(Promise<T>&& promise) {
  _::ExceptionOr<_::FixVoid<T>> result;

  waitImpl(_::makeSafeForLoop<_::FixVoid<T>>(kj::mv(promise.node), *this), result);

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

template <typename Func>
auto EventLoop::evalLater(Func&& func) const -> PromiseForResult<Func, void> {
  return there(Promise<void>(READY_NOW), kj::fwd<Func>(func));
}

template <typename T, typename Func, typename ErrorFunc>
PromiseForResult<Func, T> EventLoop::there(
    Promise<T>&& promise, Func&& func, ErrorFunc&& errorHandler) const {
  return PromiseForResult<Func, T>(false,
      _::spark<_::FixVoid<_::JoinPromises<_::ReturnType<Func, T>>>>(thereImpl(
          kj::mv(promise), kj::fwd<Func>(func), kj::fwd<ErrorFunc>(errorHandler), Event::YIELD),
          *this));
}

template <typename T, typename Func, typename ErrorFunc>
Own<_::PromiseNode> EventLoop::thereImpl(Promise<T>&& promise, Func&& func,
                                         ErrorFunc&& errorHandler,
                                         Event::Schedule schedule) const {
  typedef _::FixVoid<_::ReturnType<Func, T>> ResultT;

  Own<_::PromiseNode> intermediate =
      heap<_::TransformPromiseNode<ResultT, _::FixVoid<T>, Func, ErrorFunc>>(
          *this, _::makeSafeForLoop<_::FixVoid<T>>(kj::mv(promise.node), *this),
          kj::fwd<Func>(func), kj::fwd<ErrorFunc>(errorHandler));
  return _::maybeChain(kj::mv(intermediate), *this, schedule, implicitCast<ResultT*>(nullptr));
}

template <typename T>
Promise<T>::Promise(_::FixVoid<T> value)
    : PromiseBase(heap<_::ImmediatePromiseNode<_::FixVoid<T>>>(kj::mv(value))) {}

template <typename T>
Promise<T>::Promise(kj::Exception&& exception)
    : PromiseBase(heap<_::ImmediateBrokenPromiseNode>(kj::mv(exception))) {}

template <typename T>
template <typename Func, typename ErrorFunc>
PromiseForResult<Func, T> Promise<T>::then(Func&& func, ErrorFunc&& errorHandler) {
  return PromiseForResult<Func, T>(false, EventLoop::current().thereImpl(
      kj::mv(*this), kj::fwd<Func>(func), kj::fwd<ErrorFunc>(errorHandler),
      EventLoop::Event::PREEMPT));
}

template <typename T>
T Promise<T>::wait() {
  return EventLoop::current().wait(kj::mv(*this));
}

template <typename T>
Own<typename Promise<T>::Fork> Promise<T>::fork() {
  auto& loop = EventLoop::current();
  return refcounted<_::ForkHub<T>>(loop, _::makeSafeForLoop<_::FixVoid<T>>(kj::mv(node), loop));
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
  return Promise<T>(Own<_::PromiseNode>(heap<_::AdapterPromiseNode<_::FixVoid<T>, Adapter>>(
      kj::fwd<Params>(adapterConstructorParams)...)));
}

template <typename T>
PromiseFulfillerPair<T> newPromiseAndFulfiller() {
  auto wrapper = heap<_::WeakFulfiller<T>>();

  Own<_::PromiseNode> intermediate(
      heap<_::AdapterPromiseNode<_::FixVoid<T>, _::PromiseAndFulfillerAdapter<T>>>(*wrapper));
  Promise<_::JoinPromises<T>> promise(false,
      _::maybeChain(kj::mv(intermediate), implicitCast<T*>(nullptr)));

  return PromiseFulfillerPair<T> { kj::mv(promise), kj::mv(wrapper) };
}

template <typename T>
PromiseFulfillerPair<T> newPromiseAndFulfiller(const EventLoop& loop) {
  auto wrapper = heap<_::WeakFulfiller<T>>();

  Own<_::PromiseNode> intermediate(
      heap<_::AdapterPromiseNode<_::FixVoid<T>, _::PromiseAndFulfillerAdapter<T>>>(*wrapper));
  Promise<_::JoinPromises<T>> promise(false,
      _::maybeChain(kj::mv(intermediate), loop, EventLoop::Event::YIELD,
                    implicitCast<T*>(nullptr)));

  return PromiseFulfillerPair<T> { kj::mv(promise), kj::mv(wrapper) };
}

}  // namespace kj

#endif  // KJ_ASYNC_H_
