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
#include "tuple.h"

namespace kj {

class EventLoop;
class SimpleEventLoop;

template <typename T>
class Promise;
template <typename T>
class ForkedPromise;
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

class TaskSetImpl;

class Event;

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
  // Each thread can have at most one EventLoop.  When an EventLoop is created, it becomes the
  // default loop for the current thread.  Async APIs require that the thread has a current
  // EventLoop, or they will throw exceptions.
  //
  // Generally, you will want to construct an `EventLoop` at the top level of your program, e.g.
  // in the main() function, or in the start function of a thread.  You can then use it to
  // construct some promises and wait on the result.  Example:
  //
  //     int main() {
  //       // `loop` becomes the official EventLoop for the thread.
  //       SimpleEventLoop loop;
  //
  //       // Now we can call an async function.
  //       Promise<String> textPromise = getHttp("http://example.com");
  //
  //       // And we can wait for the promise to complete.  Note that you can only use `wait()`
  //       // from the top level, not from inside a promise callback.
  //       String text = textPromise.wait();
  //       print(text);
  //       return 0;
  //     }

  class EventJob;

public:
  EventLoop();
  ~EventLoop() noexcept(false);

  static EventLoop& current();
  // Get the event loop for the current thread.  Throws an exception if no event loop is active.

  bool isCurrent() const;
  // Is this EventLoop the current one for this thread?  This can safely be called from any thread.

  void runForever() KJ_NORETURN;
  // Runs the loop forever.  Useful for servers.

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
  bool running = false;
  // True while looping -- wait() is then not allowed.

  _::Event* head = nullptr;
  _::Event** tail = &head;
  _::Event** depthFirstInsertPoint = &head;

  Own<_::TaskSetImpl> daemons;

  template <typename T, typename Func, typename ErrorFunc>
  Own<_::PromiseNode> thenImpl(Promise<T>&& promise, Func&& func, ErrorFunc&& errorHandler);

  void waitImpl(Own<_::PromiseNode>&& node, _::ExceptionOrValue& result);
  // Run the event loop until `node` is fulfilled, and then `get()` its result into `result`.

  Promise<void> yield();
  // Returns a promise that won't resolve until all events currently on the queue are fired.
  // Otherwise, returns an already-resolved promise.  Used to implement evalLater().

  template <typename T>
  T wait(Promise<T>&& promise);

  template <typename Func>
  PromiseForResult<Func, void> evalLater(Func&& func) KJ_WARN_UNUSED_RESULT;

  void daemonize(kj::Promise<void>&& promise);

  template <typename>
  friend class Promise;
  friend Promise<void> yield();
  template <typename ErrorFunc>
  friend void daemonize(kj::Promise<void>&& promise, ErrorFunc&& errorHandler);
  template <typename Func>
  friend PromiseForResult<Func, void> evalLater(Func&& func);
  friend class _::Event;
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

  kj::String trace();
  // Dump debug info about this promise.

private:
  Own<_::PromiseNode> node;

  PromiseBase() = default;
  PromiseBase(Own<_::PromiseNode>&& node): node(kj::mv(node)) {}

  friend class EventLoop;
  friend class _::ChainPromiseNode;
  template <typename>
  friend class Promise;
  friend class _::TaskSetImpl;
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
  // As another optimization, when a callback function registered with `then()` is actually
  // scheduled, it is scheduled to occur immediately, preempting other work in the event queue.
  // This allows a long chain of `then`s to execute all at once, improving cache locality by
  // clustering operations on the same data.  However, this implies that starvation can occur
  // if a chain of `then()`s takes a very long time to execute without ever stopping to wait for
  // actual I/O.  To solve this, use `EventLoop::current()`'s `evalLater()` method to yield
  // control; this way, all other events in the queue will get a chance to run before your callback
  // is executed.
  //
  // `EventLoop::there()` behaves like `then()` when called on the current thread's EventLoop.
  // However, when used to schedule work on some other thread's loop, `there()` does _not_ schedule
  // preemptively as this would make the ordering of events unpredictable.  Also, again only when
  // scheduling cross-thread, `there()` will always evaluate the continuation eagerly even if
  // nothing is waiting on the returned promise, on the assumption that it is being used to
  // schedule a long-running computation in another thread.  In other words, when scheduling
  // cross-thread, both of the "optimizations" described above are avoided.

  T wait();
  // Run the event loop until the promise is fulfilled, then return its result.  If the promise
  // is rejected, throw an exception.
  //
  // wait() cannot be called recursively -- that is, an event callback cannot call wait().
  // Instead, callbacks that need to perform more async operations should return a promise and
  // rely on promise chaining.
  //
  // wait() is primarily useful at the top level of a program -- typically, within the function
  // that allocated the EventLoop.  For example, a program that performs one or two RPCs and then
  // exits would likely use wait() in its main() function to wait on each RPC.  On the other hand,
  // server-side code generally cannot use wait(), because it has to be able to accept multiple
  // requests at once.
  //
  // If the promise is rejected, `wait()` throws an exception.  If the program was compiled without
  // exceptions (-fno-exceptions), this will usually abort.  In this case you really should first
  // use `then()` to set an appropriate handler for the exception case, so that the promise you
  // actually wait on never throws.
  //
  // TODO(someday):  Implement fibers, and let them call wait() even when they are handling an
  //   event.

  ForkedPromise<T> fork();
  // Forks the promise, so that multiple different clients can independently wait on the result.
  // `T` must be copy-constructable for this to work.  Or, in the special case where `T` is
  // `Own<U>`, `U` must have a method `Own<U> addRef()` which returns a new reference to the same
  // (or an equivalent) object (probably implemented via reference counting).

  void exclusiveJoin(Promise<T>&& other);
  // Replace this promise with one that resolves when either the original promise resolves or
  // `other` resolves (whichever comes first).  The promise that didn't resolve first is canceled.

  // TODO(someday): inclusiveJoin(), or perhaps just join(), which waits for both completions
  //   and produces a tuple?

  template <typename... Attachments>
  void attach(Attachments&&... attachments);
  // "Attaches" one or more movable objects (often, Own<T>s) to the promise, such that they will
  // be destroyed when the promise resolves.  This is useful when a promise's callback contains
  // pointers into some object and you want to make sure the object still exists when the callback
  // runs -- after calling then(), use attach() to add necessary objects to the result.

  void eagerlyEvaluate();
  // Force eager evaluation of this promise.  Use this if you are going to hold on to the promise
  // for awhile without consuming the result, but you want to make sure that the system actually
  // processes it.

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
  template <typename>
  friend class _::ForkHub;
};

template <typename T>
class ForkedPromise {
  // The result of `Promise::fork()` and `EventLoop::fork()`.  Allows branches to be created.
  // Like `Promise<T>`, this is a pass-by-move type.

public:
  inline ForkedPromise(decltype(nullptr)) {}

  Promise<T> addBranch();
  // Add a new branch to the fork.  The branch is equivalent to the original promise.

private:
  Own<_::ForkHub<_::FixVoid<T>>> hub;

  inline ForkedPromise(bool, Own<_::ForkHub<_::FixVoid<T>>>&& hub): hub(kj::mv(hub)) {}

  friend class Promise<T>;
  friend class EventLoop;
};

class TaskSet {
  // Holds a collection of Promise<void>s and ensures that each executes to completion.  Memory
  // associated with each promise is automatically freed when the promise completes.  Destroying
  // the TaskSet itself automatically cancels all unfinished promises.
  //
  // This is useful for "daemon" objects that perform background tasks which aren't intended to
  // fulfill any particular external promise.  The daemon object holds a TaskSet to collect these
  // tasks it is working on.  This way, if the daemon itself is destroyed, the TaskSet is detroyed
  // as well, and everything the daemon is doing is canceled.  (The only alternative -- creating
  // a promise that owns itself and deletes itself on completion -- does not allow for clean
  // shutdown.)

public:
  class ErrorHandler {
  public:
    virtual void taskFailed(kj::Exception&& exception) = 0;
  };

  TaskSet(ErrorHandler& errorHandler);
  // `loop` will be used to wait on promises.  `errorHandler` will be executed any time a task
  // throws an exception, and will execute within the given EventLoop.

  ~TaskSet() noexcept(false);

  void add(Promise<void>&& promise);

  kj::String trace();
  // Return debug info about all promises currently in the TaskSet.

private:
  Own<_::TaskSetImpl> impl;
};

constexpr _::Void READY_NOW = _::Void();
// Use this when you need a Promise<void> that is already fulfilled -- this value can be implicitly
// cast to `Promise<void>`.

template <typename Func>
PromiseForResult<Func, void> evalLater(Func&& func);
// Schedule for the given zero-parameter function to be executed in the event loop at some
// point in the near future.  Returns a Promise for its result -- or, if `func()` itself returns
// a promise, `evalLater()` returns a Promise for the result of resolving that promise.
//
// Example usage:
//     Promise<int> x = evalLater([]() { return 123; });
//
// If the returned promise is destroyed before the callback runs, the callback will be canceled
// (never called).
//
// If you schedule several evaluations with `evalLater`, they will be executed in order.

template <typename ErrorFunc>
void daemonize(kj::Promise<void>&& promise, ErrorFunc&& errorHandler);
// Allows the given promise to continue running in the background until it completes or the
// `EventLoop` is destroyed.  Be careful when using this: since you can no longer cancel this
// promise, you need to make sure that the promise owns all the objects it touches or make sure
// those objects outlive the EventLoop.
//
// `errorHandler` is a function that takes `kj::Exception&&`, like the second parameter to `then()`,
// except that it must return void.
//
// This function exists mainly to implement the Cap'n Proto requirement that RPC calls cannot be
// canceled unless the callee explicitly permits it.

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
// Construct a Promise and a separate PromiseFulfiller which can be used to fulfill the promise.
// If the PromiseFulfiller is destroyed before either of its methods are called, the Promise is
// implicitly rejected.
//
// Although this function is easier to use than `newAdaptedPromise()`, it has the serious drawback
// that there is no way to handle cancellation (i.e. detect when the Promise is discarded).
//
// You can arrange to fulfill a promise with another promise by using a promise type for T.  E.g.
// `newPromiseAndFulfiller<Promise<U>>()` will produce a promise of type `Promise<U>` but the
// fulfiller will be of type `PromiseFulfiller<Promise<U>>`.  Thus you pass a `Promise<U>` to the
// `fulfill()` callback, and the promises are chained.

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

class Event {
  // An event waiting to be executed.  Not for direct use by applications -- promises use this
  // internally.

public:
  Event();
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
  // To use breadth-first scheduling instead, use `armLater()`.

  void armBreadthFirst();
  // Like `armDepthFirst()` except that the event is placed at the end of the queue.

  kj::String trace();
  // Dump debug info about this event.

  virtual _::PromiseNode* getInnerForTrace();
  // If this event wraps a PromiseNode, get that node.  Used for debug tracing.
  // Default implementation returns nullptr.

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
  virtual bool onReady(Event& event) noexcept = 0;
  // Returns true if already ready, otherwise arms the given event when ready.

  virtual void get(ExceptionOrValue& output) noexcept = 0;
  // Get the result.  `output` points to an ExceptionOr<T> into which the result will be written.
  // Can only be called once, and only after the node is ready.  Must be called directly from the
  // event loop, with no application code on the stack.

  virtual PromiseNode* getInnerForTrace();
  // If this node wraps some other PromiseNode, get the wrapped node.  Used for debug tracing.
  // Default implementation returns nullptr.

protected:
  class OnReadyEvent {
    // Helper class for implementing onReady().

  public:
    bool init(Event& newEvent);
    // Returns true if arm() was already called.

    void arm();
    // Arms the event if init() has already been called and makes future calls to init() return
    // true.

  private:
    Event* event = nullptr;
  };
};

// -------------------------------------------------------------------

class ImmediatePromiseNodeBase: public PromiseNode {
public:
  bool onReady(Event& event) noexcept override;
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

  bool onReady(Event& event) noexcept override;
  void get(ExceptionOrValue& output) noexcept override;
  PromiseNode* getInnerForTrace() override;

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

class TransformPromiseNodeBase: public PromiseNode {
public:
  TransformPromiseNodeBase(Own<PromiseNode>&& dependency);

  bool onReady(Event& event) noexcept override;
  void get(ExceptionOrValue& output) noexcept override;
  PromiseNode* getInnerForTrace() override;

private:
  Own<PromiseNode> dependency;

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
  TransformPromiseNode(Own<PromiseNode>&& dependency, Func&& func, ErrorFunc&& errorHandler)
      : TransformPromiseNodeBase(kj::mv(dependency)),
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
  bool onReady(Event& event) noexcept override;
  PromiseNode* getInnerForTrace() override;

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
  _::PromiseNode* getInnerForTrace() override;

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
    return Promise<_::UnfixVoid<T>>(false, kj::heap<ForkBranch<T>>(addRef(*this)));
  }

private:
  ExceptionOr<T> result;
};

inline ExceptionOrValue& ForkBranchBase::getHubResultRef() {
  return hub->getResultRef();
}

// -------------------------------------------------------------------

class ChainPromiseNode final: public PromiseNode, private Event {
public:
  explicit ChainPromiseNode(Own<PromiseNode> inner);
  ~ChainPromiseNode() noexcept(false);

  bool onReady(Event& event) noexcept override;
  void get(ExceptionOrValue& output) noexcept override;
  PromiseNode* getInnerForTrace() override;

private:
  enum State {
    STEP1,
    STEP2
  };

  State state;

  Own<PromiseNode> inner;
  // In PRE_STEP1 / STEP1, a PromiseNode for a Promise<T>.
  // In STEP2, a PromiseNode for a T.

  Event* onReadyEvent = nullptr;

  Maybe<Own<Event>> fire() override;
};

template <typename T>
Own<PromiseNode> maybeChain(Own<PromiseNode>&& node, Promise<T>*) {
  return heap<ChainPromiseNode>(kj::mv(node));
}

template <typename T>
Own<PromiseNode>&& maybeChain(Own<PromiseNode>&& node, T*) {
  return kj::mv(node);
}

// -------------------------------------------------------------------

class ExclusiveJoinPromiseNode final: public PromiseNode {
public:
  ExclusiveJoinPromiseNode(Own<PromiseNode> left, Own<PromiseNode> right);
  ~ExclusiveJoinPromiseNode() noexcept(false);

  bool onReady(Event& event) noexcept override;
  void get(ExceptionOrValue& output) noexcept override;
  PromiseNode* getInnerForTrace() override;

private:
  class Branch: public Event {
  public:
    Branch(ExclusiveJoinPromiseNode& joinNode, Own<PromiseNode> dependency);
    ~Branch() noexcept(false);

    bool get(ExceptionOrValue& output);
    // Returns true if this is the side that finished.

    Maybe<Own<Event>> fire() override;
    _::PromiseNode* getInnerForTrace() override;

  private:
    ExclusiveJoinPromiseNode& joinNode;
    Own<PromiseNode> dependency;
  };

  Branch left;
  Branch right;
  OnReadyEvent onReadyEvent;
};

// -------------------------------------------------------------------

class EagerPromiseNodeBase: public PromiseNode, protected Event {
  // A PromiseNode that eagerly evaluates its dependency even if its dependent does not eagerly
  // evaluate it.

public:
  EagerPromiseNodeBase(Own<PromiseNode>&& dependency, ExceptionOrValue& resultRef);

  bool onReady(Event& event) noexcept override;
  PromiseNode* getInnerForTrace() override;

private:
  Own<PromiseNode> dependency;
  OnReadyEvent onReadyEvent;

  ExceptionOrValue& resultRef;

  Maybe<Own<Event>> fire() override;
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
  bool onReady(Event& event) noexcept override;

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

}  // namespace _ (private)

// =======================================================================================

template <typename T>
T EventLoop::wait(Promise<T>&& promise) {
  _::ExceptionOr<_::FixVoid<T>> result;

  waitImpl(kj::mv(promise.node), result);

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
PromiseForResult<Func, void> EventLoop::evalLater(Func&& func) {
  // Invoke thenImpl() on yield().  Always spark the result.
  return PromiseForResult<Func, void>(false,
      _::spark<_::FixVoid<_::JoinPromises<_::ReturnType<Func, void>>>>(
          thenImpl(yield(), kj::fwd<Func>(func), _::PropagateException())));
}

template <typename T, typename Func, typename ErrorFunc>
Own<_::PromiseNode> EventLoop::thenImpl(Promise<T>&& promise, Func&& func,
                                        ErrorFunc&& errorHandler) {
  typedef _::FixVoid<_::ReturnType<Func, T>> ResultT;

  Own<_::PromiseNode> intermediate =
      heap<_::TransformPromiseNode<ResultT, _::FixVoid<T>, Func, ErrorFunc>>(
          kj::mv(promise.node), kj::fwd<Func>(func), kj::fwd<ErrorFunc>(errorHandler));
  return _::maybeChain(kj::mv(intermediate), implicitCast<ResultT*>(nullptr));
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
  return PromiseForResult<Func, T>(false, EventLoop::current().thenImpl(
      kj::mv(*this), kj::fwd<Func>(func), kj::fwd<ErrorFunc>(errorHandler)));
}

template <typename T>
T Promise<T>::wait() {
  return EventLoop::current().wait(kj::mv(*this));
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
void Promise<T>::exclusiveJoin(Promise<T>&& other) {
  node = heap<_::ExclusiveJoinPromiseNode>(kj::mv(node), kj::mv(other.node));
}

template <typename T>
template <typename... Attachments>
void Promise<T>::attach(Attachments&&... attachments) {
  node = kj::heap<_::AttachmentPromiseNode<Tuple<Attachments...>>>(
      kj::mv(node), kj::tuple(kj::fwd<Attachments>(attachments)...));
}

template <typename T>
void Promise<T>::eagerlyEvaluate() {
  node = _::spark<_::FixVoid<T>>(kj::mv(node));
}

template <typename Func>
inline PromiseForResult<Func, void> evalLater(Func&& func) {
  return EventLoop::current().evalLater(kj::fwd<Func>(func));
}

template <typename ErrorFunc>
void daemonize(kj::Promise<void>&& promise, ErrorFunc&& errorHandler) {
  return EventLoop::current().daemonize(promise.then([]() {}, kj::fwd<ErrorFunc>(errorHandler)));
}

// =======================================================================================

namespace _ {  // private

template <typename T>
class WeakFulfiller final: public PromiseFulfiller<T>, private kj::Disposer {
  // A wrapper around PromiseFulfiller which can be detached.
  //
  // There are a couple non-trivialities here:
  // - If the WeakFulfiller is discarded, we want the promise it fulfills to be implicitly
  //   rejected.
  // - We cannot destroy the WeakFulfiller until the application has discarded it *and* it has been
  //   detached from the underlying fulfiller, because otherwise the later detach() call will go
  //   to a dangling pointer.  Essentially, WeakFulfiller is reference counted, although the
  //   refcount never goes over 2 and we manually implement the refcounting because we already need
  //   a mutex anyway.  To this end, WeakFulfiller is its own Disposer -- dispose() is called when
  //   the application discards its owned pointer to the fulfiller and detach() is called when the
  //   promise is destroyed.

public:
  KJ_DISALLOW_COPY(WeakFulfiller);

  static kj::Own<WeakFulfiller> make() {
    WeakFulfiller* ptr = new WeakFulfiller;
    return Own<WeakFulfiller>(ptr, *ptr);
  }

  void fulfill(FixVoid<T>&& value) override {
    if (inner != nullptr) {
      inner->fulfill(kj::mv(value));
    }
  }

  void reject(Exception&& exception) override {
    if (inner != nullptr) {
      inner->reject(kj::mv(exception));
    }
  }

  bool isWaiting() override {
    return inner != nullptr && inner->isWaiting();
  }

  void attach(PromiseFulfiller<T>& newInner) {
    inner = &newInner;
  }

  void detach(PromiseFulfiller<T>& from) {
    if (inner == nullptr) {
      // Already disposed.
      delete this;
    } else {
      KJ_IREQUIRE(inner == &from);
      inner = nullptr;
    }
  }

private:
  mutable PromiseFulfiller<T>* inner;

  WeakFulfiller(): inner(nullptr) {}

  void disposeImpl(void* pointer) const override {
    // TODO(perf): Factor some of this out so it isn't regenerated for every fulfiller type?

    if (inner == nullptr) {
      // Already detached.
      delete this;
    } else {
      if (inner->isWaiting()) {
        inner->reject(kj::Exception(
            kj::Exception::Nature::LOCAL_BUG, kj::Exception::Durability::PERMANENT,
            __FILE__, __LINE__,
            kj::heapString("PromiseFulfiller was destroyed without fulfilling the promise.")));
      }
      inner = nullptr;
    }
  }
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

template <typename T, typename Adapter, typename... Params>
Promise<T> newAdaptedPromise(Params&&... adapterConstructorParams) {
  return Promise<T>(false, heap<_::AdapterPromiseNode<_::FixVoid<T>, Adapter>>(
      kj::fwd<Params>(adapterConstructorParams)...));
}

template <typename T>
PromiseFulfillerPair<T> newPromiseAndFulfiller() {
  auto wrapper = _::WeakFulfiller<T>::make();

  Own<_::PromiseNode> intermediate(
      heap<_::AdapterPromiseNode<_::FixVoid<T>, _::PromiseAndFulfillerAdapter<T>>>(*wrapper));
  Promise<_::JoinPromises<T>> promise(false,
      _::maybeChain(kj::mv(intermediate), implicitCast<T*>(nullptr)));

  return PromiseFulfillerPair<T> { kj::mv(promise), kj::mv(wrapper) };
}

}  // namespace kj

#endif  // KJ_ASYNC_H_
