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

#pragma once

#include "async-prelude.h"
#include "exception.h"
#include "refcount.h"

KJ_BEGIN_HEADER

namespace kj {

class EventLoop;
class WaitScope;

template <typename T>
class Promise;
template <typename T>
class ForkedPromise;
template <typename T>
class PromiseFulfiller;
template <typename T>
struct PromiseFulfillerPair;

template <typename Func, typename T>
using PromiseForResult = _::ReducePromises<_::ReturnType<Func, T>>;
// Evaluates to the type of Promise for the result of calling functor type Func with parameter type
// T.  If T is void, then the promise is for the result of calling Func with no arguments.  If
// Func itself returns a promise, the promises are joined, so you never get Promise<Promise<T>>.

// =======================================================================================
// Promises

template <typename T>
class Promise: protected _::PromiseBase {
  // The basic primitive of asynchronous computation in KJ.  Similar to "futures", but designed
  // specifically for event loop concurrency.  Similar to E promises and JavaScript Promises/A.
  //
  // A Promise represents a promise to produce a value of type T some time in the future.  Once
  // that value has been produced, the promise is "fulfilled".  Alternatively, a promise can be
  // "broken", with an Exception describing what went wrong.  You may implicitly convert a value of
  // type T to an already-fulfilled Promise<T>.  You may implicitly convert the constant
  // `kj::READY_NOW` to an already-fulfilled Promise<void>.  You may also implicitly convert a
  // `kj::Exception` to an already-broken promise of any type.
  //
  // Promises are linear types -- they are moveable but not copyable.  If a Promise is destroyed
  // or goes out of scope (without being moved elsewhere), any ongoing asynchronous operations
  // meant to fulfill the promise will be canceled if possible.  All methods of `Promise` (unless
  // otherwise noted) actually consume the promise in the sense of move semantics.  (Arguably they
  // should be rvalue-qualified, but at the time this interface was created compilers didn't widely
  // support that yet and anyway it would be pretty ugly typing kj::mv(promise).whatever().)  If
  // you want to use one Promise in two different places, you must fork it with `fork()`.
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
  // For `then()` to work, the current thread must have an active `EventLoop`.  Each callback
  // is scheduled to execute in that loop.  Since `then()` schedules callbacks only on the current
  // thread's event loop, you do not need to worry about two callbacks running at the same time.
  // You will need to set up at least one `EventLoop` at the top level of your program before you
  // can use promises.
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
  // should in turn return promises for the eventual results of said invocations.  Cap'n Proto,
  // for example, implements the type `RemotePromise` which supports pipelining RPC requests -- see
  // `capnp/capability.h`.
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
  // an exception.
  //
  // You may also specify an error handler continuation as the second parameter.  `errorHandler`
  // must be a functor taking a parameter of type `kj::Exception&&`.  It must return the same
  // type as `func` returns (except when `func` returns `Promise<U>`, in which case `errorHandler`
  // may return either `Promise<U>` or just `U`).  The default error handler simply propagates the
  // exception to the returned promise.
  //
  // Either `func` or `errorHandler` may, of course, throw an exception, in which case the promise
  // is broken.  When compiled with -fno-exceptions, the framework will still detect when a
  // recoverable exception was thrown inside of a continuation and will consider the promise
  // broken even though a (presumably garbage) result was returned.
  //
  // If the returned promise is destroyed before the callback runs, the callback will be canceled
  // (it will never run).
  //
  // Note that `then()` -- like all other Promise methods -- consumes the promise on which it is
  // called, in the sense of move semantics.  After returning, the original promise is no longer
  // valid, but `then()` returns a new promise.
  //
  // *Advanced implementation tips:*  Most users will never need to worry about the below, but
  // it is good to be aware of.
  //
  // As an optimization, if the callback function `func` does _not_ return another promise, then
  // execution of `func` itself may be delayed until its result is known to be needed.  The
  // expectation here is that `func` is just doing some transformation on the results, not
  // scheduling any other actions, therefore the system doesn't need to be proactive about
  // evaluating it.  This way, a chain of trivial then() transformations can be executed all at
  // once without repeatedly re-scheduling through the event loop.  Use the `eagerlyEvaluate()`
  // method to suppress this behavior.
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
  // actual I/O.  To solve this, use `kj::evalLater()` to yield control; this way, all other events
  // in the queue will get a chance to run before your callback is executed.

  Promise<void> ignoreResult() KJ_WARN_UNUSED_RESULT { return then([](T&&) {}); }
  // Convenience method to convert the promise to a void promise by ignoring the return value.
  //
  // You must still wait on the returned promise if you want the task to execute.

  template <typename ErrorFunc>
  Promise<T> catch_(ErrorFunc&& errorHandler) KJ_WARN_UNUSED_RESULT;
  // Equivalent to `.then(identityFunc, errorHandler)`, where `identifyFunc` is a function that
  // just returns its input.

  T wait(WaitScope& waitScope);
  // Run the event loop until the promise is fulfilled, then return its result.  If the promise
  // is rejected, throw an exception.
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
  // `waitScope` is an object proving that the caller is in a scope where wait() is allowed.  By
  // convention, any function which might call wait(), or which might call another function which
  // might call wait(), must take `WaitScope&` as one of its parameters.  This is needed for two
  // reasons:
  // * `wait()` is not allowed during an event callback, because event callbacks are themselves
  //   called during some other `wait()`, and such recursive `wait()`s would only be able to
  //   complete in LIFO order, which might mean that the outer `wait()` ends up waiting longer
  //   than it is supposed to.  To prevent this, a `WaitScope` cannot be constructed or used during
  //   an event callback.
  // * Since `wait()` runs the event loop, unrelated event callbacks may execute before `wait()`
  //   returns.  This means that anyone calling `wait()` must be reentrant -- state may change
  //   around them in arbitrary ways.  Therefore, callers really need to know if a function they
  //   are calling might wait(), and the `WaitScope&` parameter makes this clear.
  //
  // Usually, there is only one `WaitScope` for each `EventLoop`, and it can only be used at the
  // top level of the thread owning the loop. Calling `wait()` with this `WaitScope` is what
  // actually causes the event loop to run at all. This top-level `WaitScope` cannot be used
  // recursively, so cannot be used within an event callback.
  //
  // However, it is possible to obtain a `WaitScope` in lower-level code by using fibers. Use
  // kj::startFiber() to start some code executing on an alternate call stack. That code will get
  // its own `WaitScope` allowing it to operate in a synchronous style. In this case, `wait()`
  // switches back to the main stack in order to run the event loop, returning to the fiber's stack
  // once the awaited promise resolves.

  bool poll(WaitScope& waitScope);
  // Returns true if a call to wait() would complete without blocking, false if it would block.
  //
  // If the promise is not yet resolved, poll() will pump the event loop and poll for I/O in an
  // attempt to resolve it. Only when there is nothing left to do will it return false.
  //
  // Generally, poll() is most useful in tests. Often, you may want to verify that a promise does
  // not resolve until some specific event occurs. To do so, poll() the promise before the event to
  // verify it isn't resolved, then trigger the event, then poll() again to verify that it resolves.
  // The first poll() verifies that the promise doesn't resolve early, which would otherwise be
  // hard to do deterministically. The second poll() allows you to check that the promise has
  // resolved and avoid a wait() that might deadlock in the case that it hasn't.
  //
  // poll() is not supported in fibers; it will throw an exception.

  ForkedPromise<T> fork() KJ_WARN_UNUSED_RESULT;
  // Forks the promise, so that multiple different clients can independently wait on the result.
  // `T` must be copy-constructable for this to work.  Or, in the special case where `T` is
  // `Own<U>`, `U` must have a method `Own<U> addRef()` which returns a new reference to the same
  // (or an equivalent) object (probably implemented via reference counting).

  _::SplitTuplePromise<T> split();
  // Split a promise for a tuple into a tuple of promises.
  //
  // E.g. if you have `Promise<kj::Tuple<T, U>>`, `split()` returns
  // `kj::Tuple<Promise<T>, Promise<U>>`.

  Promise<T> exclusiveJoin(Promise<T>&& other) KJ_WARN_UNUSED_RESULT;
  // Return a new promise that resolves when either the original promise resolves or `other`
  // resolves (whichever comes first).  The promise that didn't resolve first is canceled.

  // TODO(someday): inclusiveJoin(), or perhaps just join(), which waits for both completions
  //   and produces a tuple?

  template <typename... Attachments>
  Promise<T> attach(Attachments&&... attachments) KJ_WARN_UNUSED_RESULT;
  // "Attaches" one or more movable objects (often, Own<T>s) to the promise, such that they will
  // be destroyed when the promise resolves.  This is useful when a promise's callback contains
  // pointers into some object and you want to make sure the object still exists when the callback
  // runs -- after calling then(), use attach() to add necessary objects to the result.

  template <typename ErrorFunc>
  Promise<T> eagerlyEvaluate(ErrorFunc&& errorHandler) KJ_WARN_UNUSED_RESULT;
  Promise<T> eagerlyEvaluate(decltype(nullptr)) KJ_WARN_UNUSED_RESULT;
  // Force eager evaluation of this promise.  Use this if you are going to hold on to the promise
  // for awhile without consuming the result, but you want to make sure that the system actually
  // processes it.
  //
  // `errorHandler` is a function that takes `kj::Exception&&`, like the second parameter to
  // `then()`, or the parameter to `catch_()`.  We make you specify this because otherwise it's
  // easy to forget to handle errors in a promise that you never use.  You may specify nullptr for
  // the error handler if you are sure that ignoring errors is fine, or if you know that you'll
  // eventually wait on the promise somewhere.

  template <typename ErrorFunc>
  void detach(ErrorFunc&& errorHandler);
  // Allows the promise to continue running in the background until it completes or the
  // `EventLoop` is destroyed.  Be careful when using this: since you can no longer cancel this
  // promise, you need to make sure that the promise owns all the objects it touches or make sure
  // those objects outlive the EventLoop.
  //
  // `errorHandler` is a function that takes `kj::Exception&&`, like the second parameter to
  // `then()`, except that it must return void.
  //
  // This function exists mainly to implement the Cap'n Proto requirement that RPC calls cannot be
  // canceled unless the callee explicitly permits it.

  kj::String trace();
  // Returns a dump of debug info about this promise.  Not for production use.  Requires RTTI.
  // This method does NOT consume the promise as other methods do.

private:
  Promise(bool, Own<_::PromiseNode>&& node): PromiseBase(kj::mv(node)) {}
  // Second parameter prevent ambiguity with immediate-value constructor.

  friend class _::PromiseNode;
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

constexpr _::Void READY_NOW = _::Void();
// Use this when you need a Promise<void> that is already fulfilled -- this value can be implicitly
// cast to `Promise<void>`.

constexpr _::NeverDone NEVER_DONE = _::NeverDone();
// The opposite of `READY_NOW`, return this when the promise should never resolve.  This can be
// implicitly converted to any promise type.  You may also call `NEVER_DONE.wait()` to wait
// forever (useful for servers).

template <typename Func>
PromiseForResult<Func, void> evalLater(Func&& func) KJ_WARN_UNUSED_RESULT;
// Schedule for the given zero-parameter function to be executed in the event loop at some
// point in the near future.  Returns a Promise for its result -- or, if `func()` itself returns
// a promise, `evalLater()` returns a Promise for the result of resolving that promise.
//
// Example usage:
//     Promise<int> x = evalLater([]() { return 123; });
//
// The above is exactly equivalent to:
//     Promise<int> x = Promise<void>(READY_NOW).then([]() { return 123; });
//
// If the returned promise is destroyed before the callback runs, the callback will be canceled
// (never called).
//
// If you schedule several evaluations with `evalLater` during the same callback, they are
// guaranteed to be executed in order.

template <typename Func>
PromiseForResult<Func, void> evalNow(Func&& func) KJ_WARN_UNUSED_RESULT;
// Run `func()` and return a promise for its result. `func()` executes before `evalNow()` returns.
// If `func()` throws an exception, the exception is caught and wrapped in a promise -- this is the
// main reason why `evalNow()` is useful.

template <typename Func>
PromiseForResult<Func, void> evalLast(Func&& func) KJ_WARN_UNUSED_RESULT;
// Like `evalLater()`, except that the function doesn't run until the event queue is otherwise
// completely empty and the thread is about to suspend waiting for I/O.
//
// This is useful when you need to perform some disruptive action and you want to make sure that
// you don't interrupt some other task between two .then() continuations. For example, say you want
// to cancel a read() operation on a socket and know for sure that if any bytes were read, you saw
// them. It could be that a read() has completed and bytes have been transferred to the target
// buffer, but the .then() callback that handles the read result hasn't executed yet. If you
// cancel the promise at this inopportune moment, the bytes in the buffer are lost. If you do
// evalLast(), then you can be sure that any pending .then() callbacks had a chance to finish out
// and if you didn't receive the read result yet, then you know nothing has been read, and you can
// simply drop the promise.
//
// If evalLast() is called multiple times, functions are executed in LIFO order. If the first
// callback enqueues new events, then latter callbacks will not execute until those events are
// drained.

template <typename Func>
PromiseForResult<Func, WaitScope&> startFiber(size_t stackSize, Func&& func) KJ_WARN_UNUSED_RESULT;
// Executes `func()` in a fiber, returning a promise for the eventual reseult. `func()` will be
// passed a `WaitScope&` as its parameter, allowing it to call `.wait()` on promises. Thus, `func()`
// can be written in a synchronous, blocking style, instead of using `.then()`. This is often much
// easier to write and read, and may even be significantly faster if it allows the use of stack
// allocation rather than heap allocation.
//
// However, fibers have a major disadvantage: memory must be allocated for the fiber's call stack.
// The entire stack must be allocated at once, making it necessary to choose a stack size upfront
// that is big enough for whatever the fiber needs to do. Estimating this is often difficult. That
// said, over-estimating is not too terrible since pages of the stack will actually be allocated
// lazily when first accessed; actual memory usage will correspond to the "high watermark" of the
// actual stack usage. That said, this lazy allocation forces page faults, which can be quite slow.
// Worse, freeing a stack forces a TLB flush and shootdown -- all currently-executing threads will
// have to be interrupted to flush their CPU cores' TLB caches.
//
// In short, when performance matters, you should try to avoid creating fibers very frequently.
//
// TODO(perf): We should add a mechanism for freelisting stacks. However, this improves CPU usage
//   at the expense of memory usage: stacks on the freelist will consume however many pages they
//   used at their high watermark, forever.

template <typename T>
Promise<Array<T>> joinPromises(Array<Promise<T>>&& promises);
// Join an array of promises into a promise for an array.

// =======================================================================================
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

// =======================================================================================
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
_::ReducePromises<T> newAdaptedPromise(Params&&... adapterConstructorParams);
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
// called.
//
// An adapter implementation should be carefully written to ensure that it cannot accidentally
// be left unfulfilled permanently because of an exception.  Consider making liberal use of
// `PromiseFulfiller<T>::rejectIfThrows()`.

template <typename T>
struct PromiseFulfillerPair {
  _::ReducePromises<T> promise;
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
// Canceler

class Canceler {
  // A Canceler can wrap some set of Promises and then forcefully cancel them on-demand, or
  // implicitly when the Canceler is destroyed.
  //
  // The cancellation is done in such a way that once cancel() (or the Canceler's destructor)
  // returns, it's guaranteed that the promise has already been canceled and destroyed. This
  // guarantee is important for enforcing ownership constraints. For example, imagine that Alice
  // calls a method on Bob that returns a Promise. That Promise encapsulates a task that uses Bob's
  // internal state. But, imagine that Alice does not own Bob, and indeed Bob might be destroyed
  // at random without Alice having canceled the promise. In this case, it is necessary for Bob to
  // ensure that the promise will be forcefully canceled. Bob can do this by constructing a
  // Canceler and using it to wrap promises before returning them to callers. When Bob is
  // destroyed, the Canceler is destroyed too, and all promises Bob wrapped with it throw errors.
  //
  // Note that another common strategy for cancelation is to use exclusiveJoin() to join a promise
  // with some "cancellation promise" which only resolves if the operation should be canceled. The
  // cancellation promise could itself be created by newPromiseAndFulfiller<void>(), and thus
  // calling the PromiseFulfiller cancels the operation. There is a major problem with this
  // approach: upon invoking the fulfiller, an arbitrary amount of time may pass before the
  // exclusive-joined promise actually resolves and cancels its other fork. During that time, the
  // task might continue to execute. If it holds pointers to objects that have been destroyed, this
  // might cause segfaults. Thus, it is safer to use a Canceler.

public:
  inline Canceler() {}
  ~Canceler() noexcept(false);
  KJ_DISALLOW_COPY(Canceler);

  template <typename T>
  Promise<T> wrap(Promise<T> promise) {
    return newAdaptedPromise<T, AdapterImpl<T>>(*this, kj::mv(promise));
  }

  void cancel(StringPtr cancelReason);
  void cancel(const Exception& exception);
  // Cancel all previously-wrapped promises that have not already completed, causing them to throw
  // the given exception. If you provide just a description message instead of an exception, then
  // an exception object will be constructed from it -- but only if there are requests to cancel.

  void release();
  // Releases previously-wrapped promises, so that they will not be canceled regardless of what
  // happens to this Canceler.

  bool isEmpty() const { return list == nullptr; }
  // Indicates if any previously-wrapped promises are still executing. (If this returns false, then
  // cancel() would be a no-op.)

private:
  class AdapterBase {
  public:
    AdapterBase(Canceler& canceler);
    ~AdapterBase() noexcept(false);

    virtual void cancel(Exception&& e) = 0;

  private:
    Maybe<Maybe<AdapterBase&>&> prev;
    Maybe<AdapterBase&> next;
    friend class Canceler;
  };

  template <typename T>
  class AdapterImpl: public AdapterBase {
  public:
    AdapterImpl(PromiseFulfiller<T>& fulfiller,
                Canceler& canceler, Promise<T> inner)
        : AdapterBase(canceler),
          fulfiller(fulfiller),
          inner(inner.then(
              [&fulfiller](T&& value) { fulfiller.fulfill(kj::mv(value)); },
              [&fulfiller](Exception&& e) { fulfiller.reject(kj::mv(e)); })
              .eagerlyEvaluate(nullptr)) {}

    void cancel(Exception&& e) override {
      fulfiller.reject(kj::mv(e));
      inner = nullptr;
    }

  private:
    PromiseFulfiller<T>& fulfiller;
    Promise<void> inner;
  };

  Maybe<AdapterBase&> list;
};

template <>
class Canceler::AdapterImpl<void>: public AdapterBase {
public:
  AdapterImpl(kj::PromiseFulfiller<void>& fulfiller,
              Canceler& canceler, kj::Promise<void> inner);
  void cancel(kj::Exception&& e) override;
  // These must be defined in async.c++ to prevent translation units compiled by MSVC from trying to
  // link with symbols defined in async.c++ merely because they included async.h.

private:
  kj::PromiseFulfiller<void>& fulfiller;
  kj::Promise<void> inner;
};

// =======================================================================================
// TaskSet

class TaskSet {
  // Holds a collection of Promise<void>s and ensures that each executes to completion.  Memory
  // associated with each promise is automatically freed when the promise completes.  Destroying
  // the TaskSet itself automatically cancels all unfinished promises.
  //
  // This is useful for "daemon" objects that perform background tasks which aren't intended to
  // fulfill any particular external promise, but which may need to be canceled (and thus can't
  // use `Promise::detach()`).  The daemon object holds a TaskSet to collect these tasks it is
  // working on.  This way, if the daemon itself is destroyed, the TaskSet is detroyed as well,
  // and everything the daemon is doing is canceled.

public:
  class ErrorHandler {
  public:
    virtual void taskFailed(kj::Exception&& exception) = 0;
  };

  TaskSet(ErrorHandler& errorHandler);
  // `errorHandler` will be executed any time a task throws an exception, and will execute within
  // the given EventLoop.

  ~TaskSet() noexcept(false);

  void add(Promise<void>&& promise);

  kj::String trace();
  // Return debug info about all promises currently in the TaskSet.

  bool isEmpty() { return tasks == nullptr; }
  // Check if any tasks are running.

  Promise<void> onEmpty();
  // Returns a promise that fulfills the next time the TaskSet is empty. Only one such promise can
  // exist at a time.

private:
  class Task;

  TaskSet::ErrorHandler& errorHandler;
  Maybe<Own<Task>> tasks;
  Maybe<Own<PromiseFulfiller<void>>> emptyFulfiller;
};

// =======================================================================================
// Cross-thread execution.

class Executor {
  // Executes code on another thread's event loop.
  //
  // Use `kj::getCurrentThreadExecutor()` to get an executor that schedules calls on the current
  // thread's event loop. You may then pass the reference to other threads to enable them to call
  // back to this one.

public:
  Executor(EventLoop& loop, Badge<EventLoop>);
  ~Executor() noexcept(false);

  template <typename Func>
  PromiseForResult<Func, void> executeAsync(Func&& func) const;
  // Call from any thread to request that the given function be executed on the executor's thread,
  // returning a promise for the result.
  //
  // The Promise returned by executeAsync() belongs to the requesting thread, not the executor
  // thread. Hence, for example, continuations added to this promise with .then() will exceute in
  // the requesting thread.
  //
  // If func() itself returns a Promise, that Promise is *not* returned verbatim to the requesting
  // thread -- after all, Promise objects cannot be used cross-thread. Instead, the executor thread
  // awaits the promise. Once it resolves to a final result, that result is transferred to the
  // requesting thread, resolving the promise that executeAsync() returned earlier.
  //
  // `func` will be destroyed in the requesting thread, after the final result has been returned
  // from the executor thread. This means that it is safe for `func` to capture objects that cannot
  // safely be destroyed from another thread. It is also safe for `func` to be an lvalue reference,
  // so long as the functor remains live until the promise completes or is canceled, and the
  // function is thread-safe.
  //
  // Of course, the body of `func` must be careful that any access it makes on these objects is
  // safe cross-thread. For example, it must not attempt to access Promise-related objects
  // cross-thread; you cannot create a `PromiseFulfiller` in one thread and then `fulfill()` it
  // from another. Unfortunately, the usual convention of using const-correctness to enforce
  // thread-safety does not work here, because applications can often ensure that `func` has
  // exclusive access to captured objects, and thus can safely mutate them even in non-thread-safe
  // ways; the const qualifier is not sufficient to express this.
  //
  // The final return value of `func` is transferred between threads, and hence is constructed and
  // destroyed in separate threads. It is the app's responsibility to make sure this is OK.
  // Alternatively, the app can perhaps arrange to send the return value back to the original
  // thread for destruction, if needed.
  //
  // If the requesting thread destroys the returned Promise, the destructor will block waiting for
  // the executor thread to acknowledge cancellation. This ensures that `func` can be destroyed
  // before the Promise's destructor returns.
  //
  // Multiple calls to executeAsync() from the same requesting thread to the same target thread
  // will be delivered in the same order in which they were requested. (However, if func() returns
  // a promise, delivery of subsequent calls is not blocked on that promise. In other words, this
  // call provides E-Order in the same way as Cap'n Proto.)

  template <typename Func>
  _::UnwrapPromise<PromiseForResult<Func, void>> executeSync(Func&& func) const;
  // Schedules `func()` to execute on the executor thread, and then blocks the requesting thread
  // until `func()` completes. If `func()` returns a Promise, then the wait will continue until
  // that promise resolves, and the final result will be returned to the requesting thread.
  //
  // The requesting thread does not need to have an EventLoop. If it does have an EventLoop, that
  // loop will *not* execute while the thread is blocked. This method is particularly useful to
  // allow non-event-loop threads to perform I/O via a separate event-loop thread.
  //
  // As with `executeAsync()`, `func` is always destroyed on the requesting thread, after the
  // executor thread has signaled completion. The return value is transferred between threads.

private:
  EventLoop& loop;

  struct Impl;
  Own<Impl> impl;
  // To avoid including mutex.h...

  friend class EventLoop;
  friend class _::XThreadEvent;

  void send(_::XThreadEvent& event, bool sync) const;
  void wait();
  bool poll();
};

const Executor& getCurrentThreadExecutor();
// Get the executor for the current thread's event loop. This reference can then be passed to other
// threads.

// =======================================================================================
// The EventLoop class

class EventPort {
  // Interfaces between an `EventLoop` and events originating from outside of the loop's thread.
  // All such events come in through the `EventPort` implementation.
  //
  // An `EventPort` implementation may interface with low-level operating system APIs and/or other
  // threads.  You can also write an `EventPort` which wraps some other (non-KJ) event loop
  // framework, allowing the two to coexist in a single thread.

public:
  virtual bool wait() = 0;
  // Wait for an external event to arrive, sleeping if necessary.  Once at least one event has
  // arrived, queue it to the event loop (e.g. by fulfilling a promise) and return.
  //
  // This is called during `Promise::wait()` whenever the event queue becomes empty, in order to
  // wait for new events to populate the queue.
  //
  // It is safe to return even if nothing has actually been queued, so long as calling `wait()` in
  // a loop will eventually sleep.  (That is to say, false positives are fine.)
  //
  // Returns true if wake() has been called from another thread. (Precisely, returns true if
  // no previous call to wait `wait()` nor `poll()` has returned true since `wake()` was last
  // called.)

  virtual bool poll() = 0;
  // Check if any external events have arrived, but do not sleep.  If any events have arrived,
  // add them to the event queue (e.g. by fulfilling promises) before returning.
  //
  // This may be called during `Promise::wait()` when the EventLoop has been executing for a while
  // without a break but is still non-empty.
  //
  // Returns true if wake() has been called from another thread. (Precisely, returns true if
  // no previous call to wait `wait()` nor `poll()` has returned true since `wake()` was last
  // called.)

  virtual void setRunnable(bool runnable);
  // Called to notify the `EventPort` when the `EventLoop` has work to do; specifically when it
  // transitions from empty -> runnable or runnable -> empty.  This is typically useful when
  // integrating with an external event loop; if the loop is currently runnable then you should
  // arrange to call run() on it soon.  The default implementation does nothing.

  virtual void wake() const;
  // Wake up the EventPort's thread from another thread.
  //
  // Unlike all other methods on this interface, `wake()` may be called from another thread, hence
  // it is `const`.
  //
  // Technically speaking, `wake()` causes the target thread to cease sleeping and not to sleep
  // again until `wait()` or `poll()` has returned true at least once.
  //
  // The default implementation throws an UNIMPLEMENTED exception.
};

class EventLoop {
  // Represents a queue of events being executed in a loop.  Most code won't interact with
  // EventLoop directly, but instead use `Promise`s to interact with it indirectly.  See the
  // documentation for `Promise`.
  //
  // Each thread can have at most one current EventLoop.  To make an `EventLoop` current for
  // the thread, create a `WaitScope`.  Async APIs require that the thread has a current EventLoop,
  // or they will throw exceptions.  APIs that use `Promise::wait()` additionally must explicitly
  // be passed a reference to the `WaitScope` to make the caller aware that they might block.
  //
  // Generally, you will want to construct an `EventLoop` at the top level of your program, e.g.
  // in the main() function, or in the start function of a thread.  You can then use it to
  // construct some promises and wait on the result.  Example:
  //
  //     int main() {
  //       // `loop` becomes the official EventLoop for the thread.
  //       MyEventPort eventPort;
  //       EventLoop loop(eventPort);
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
  //
  // Most applications that do I/O will prefer to use `setupAsyncIo()` from `async-io.h` rather
  // than allocate an `EventLoop` directly.

public:
  EventLoop();
  // Construct an `EventLoop` which does not receive external events at all.

  explicit EventLoop(EventPort& port);
  // Construct an `EventLoop` which receives external events through the given `EventPort`.

  ~EventLoop() noexcept(false);

  void run(uint maxTurnCount = maxValue);
  // Run the event loop for `maxTurnCount` turns or until there is nothing left to be done,
  // whichever comes first.  This never calls the `EventPort`'s `sleep()` or `poll()`.  It will
  // call the `EventPort`'s `setRunnable(false)` if the queue becomes empty.

  bool isRunnable();
  // Returns true if run() would currently do anything, or false if the queue is empty.

  const Executor& getExecutor();
  // Returns an Executor that can be used to schedule events on this EventLoop from another thread.
  //
  // Use the global function kj::getCurrentThreadExecutor() to get the current thread's EventLoop's
  // Executor.
  //
  // Note that this is only needed for cross-thread scheduling. To schedule code to run later in
  // the current thread, use `kj::evalLater()`, which will be more efficient.

private:
  kj::Maybe<EventPort&> port;
  // If null, this thread doesn't receive I/O events from the OS. It can potentially receive
  // events from other threads via the Executor.

  bool running = false;
  // True while looping -- wait() is then not allowed.

  bool lastRunnableState = false;
  // What did we last pass to port.setRunnable()?

  _::Event* head = nullptr;
  _::Event** tail = &head;
  _::Event** depthFirstInsertPoint = &head;
  _::Event** breadthFirstInsertPoint = &head;

  kj::Maybe<Executor> executor;
  // Allocated the first time getExecutor() is requested, making cross-thread request possible.

  Own<TaskSet> daemons;

#if _WIN32 || __CYGWIN__
  void* mainFiber = nullptr;
#endif

  bool turn();
  void setRunnable(bool runnable);
  void enterScope();
  void leaveScope();

  void wait();
  void poll();

  friend void _::detach(kj::Promise<void>&& promise);
  friend void _::waitImpl(Own<_::PromiseNode>&& node, _::ExceptionOrValue& result,
                          WaitScope& waitScope);
  friend bool _::pollImpl(_::PromiseNode& node, WaitScope& waitScope);
  friend class _::Event;
  friend class WaitScope;
  friend class Executor;
  friend class _::XThreadEvent;
  friend class _::FiberBase;
};

class WaitScope {
  // Represents a scope in which asynchronous programming can occur.  A `WaitScope` should usually
  // be allocated on the stack and serves two purposes:
  // * While the `WaitScope` exists, its `EventLoop` is registered as the current loop for the
  //   thread.  Most operations dealing with `Promise` (including all of its methods) do not work
  //   unless the thread has a current `EventLoop`.
  // * `WaitScope` may be passed to `Promise::wait()` to synchronously wait for a particular
  //   promise to complete.  See `Promise::wait()` for an extended discussion.

public:
  inline explicit WaitScope(EventLoop& loop): loop(loop) { loop.enterScope(); }
  inline ~WaitScope() { if (fiber == nullptr) loop.leaveScope(); }
  KJ_DISALLOW_COPY(WaitScope);

  void poll();
  // Pumps the event queue and polls for I/O until there's nothing left to do (without blocking).
  //
  // Not supported in fibers.

  void setBusyPollInterval(uint count) { busyPollInterval = count; }
  // Set the maximum number of events to run in a row before calling poll() on the EventPort to
  // check for new I/O.
  //
  // This has no effect when used in a fiber.

private:
  EventLoop& loop;
  uint busyPollInterval = kj::maxValue;

  kj::Maybe<_::FiberBase&> fiber;

  explicit WaitScope(EventLoop& loop, _::FiberBase& fiber)
      : loop(loop), fiber(fiber) {}

  friend class EventLoop;
  friend class _::FiberBase;
  friend void _::waitImpl(Own<_::PromiseNode>&& node, _::ExceptionOrValue& result,
                          WaitScope& waitScope);
  friend bool _::pollImpl(_::PromiseNode& node, WaitScope& waitScope);
};

}  // namespace kj

#define KJ_ASYNC_H_INCLUDED
#include "async-inl.h"

KJ_END_HEADER
