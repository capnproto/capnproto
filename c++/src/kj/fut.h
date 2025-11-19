// Copyright (c) 2025 Cloudflare, Inc. and contributors
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

#include "kj/async-prelude.h"
#include "kj/common.h"
#include "kj/debug.h"
#include "kj/exception.h"
#include "kj/source-location.h"
#include "kj/vector.h"
#include <coroutine>
#include <kj/async.h>

namespace kj {

#if !defined(FUT_DEBUG)
#ifdef KJ_DEBUG
#define FUT_DEBUG 1
#else
#define FUT_DEBUG 0
#endif
#endif

#if FUT_DEBUG
#define FUT_DBG(...) KJ_DBG(__VA_ARGS__)
#else
#define FUT_DBG(...)
#endif

template <typename T, bool Lazy> class FutPromiseNode;
template <typename T, bool Lazy> class Fut;

namespace fut {

template <typename T> class ReadyNow;
class Stack;

namespace _ {
template <typename T, bool Lazy> class CoroBase;
template <typename T, bool Lazy> class Coro;

} // namespace _
} // namespace fut

template <typename T> class FutExclusiveJoin;

template <typename T> using LazyFut = Fut<T, true>;

template <typename T, bool Lazy>
using FutHandle = std::coroutine_handle<fut::_::CoroBase<T, Lazy>>;

template <typename T> constexpr bool isVoid = kj::isSameType<T, void>();

/////////////////////
// Allocator

namespace fut {

namespace _ {
struct Allocator {
  struct Stack;

  struct Frame {
    size_t dataSize;
    Stack *stack;
    kj::byte data[];

    inline kj::byte *dataBegin() { return data; }
    inline kj::byte *dataEnd() { return data + dataSize; }

    inline constexpr size_t allocSize() { return sizeof(Frame) + dataSize; }
    inline constexpr static size_t allocSize(size_t dataSize) {
      return sizeof(Frame) + dataSize;
    }

    inline void free() {
      if (stack) {
        stack->free(this);
      } else {
        delete[] reinterpret_cast<kj::byte *>(this);
      }
    }
  };

  static_assert(alignof(Frame) == alignof(size_t),
                "size_t alignment is expected");

  struct Chunk {
    size_t offset = 0;
    kj::byte bytes[16 * 1024];

    kj::byte *begin() { return bytes; }
    kj::byte *end() { return bytes + size(); }

    size_t size() { return sizeof(bytes); };

    kj::byte *alloc(size_t n) {
      KJ_ASSERT(offset + n < size(), "not implemented");
      auto ptr = bytes + offset;
      offset += n;
      // ASAN_UNPOISON_MEMORY_REGION(ptr, size);
      return ptr;
    }

    void free(kj::byte *ptr, size_t n) {
      KJ_ASSERT(ptr + n == bytes + offset);
      offset -= n;
    }
  };

  struct Stack {
    Frame *alloc(size_t frameSize) {
      auto allocSize = Frame::allocSize(frameSize);
      auto ptr = chunk.alloc(allocSize);
      auto frame = reinterpret_cast<Frame *>(ptr);
      frame->stack = this;
      frame->dataSize = frameSize;
      return frame;
    }

    void free(Frame *frame) {
      chunk.free(reinterpret_cast<kj::byte *>(frame), frame->allocSize());
    }

    Chunk chunk;
  };

  inline Frame *alloc(size_t frameSize) {
    if (stack) {
      return stack->alloc(frameSize);
    }
    auto allocSize = Frame::allocSize(frameSize);
    auto ptr = new kj::byte[allocSize];
    auto frame = reinterpret_cast<Frame *>(ptr);
    frame->stack = nullptr;
    frame->dataSize = frameSize;
    return frame;
  }

  Stack *stack = nullptr;
};

static inline thread_local Allocator allocator;

} // namespace _

class Stack {
public:
  inline Stack() noexcept : prevStack(_::allocator.stack) {
    _::allocator.stack = &stack;
  }

  inline ~Stack() noexcept { _::allocator.stack = prevStack; }

  inline _::Allocator::Frame *alloc(size_t frameSize) {
    return stack.alloc(frameSize);
  }

private:
  _::Allocator::Stack *prevStack = _::allocator.stack;
  _::Allocator::Stack stack;
};

// main allocator entry points
namespace _ {

template <typename... Args>
static constexpr bool hasStack = (kj::isSameType<Args, Stack &>() || ...);

template <typename X> constexpr Stack *tryGetStack(X &&) { return nullptr; }

constexpr Stack *tryGetStack(Stack &stack) { return &stack; }

template <typename... Args> static constexpr Stack &getStack(Args &&...args) {
  Stack *stack = nullptr;
  ((stack = tryGetStack(args)) || ...);
  return KJ_REQUIRE_NONNULL(stack);
}

template <typename... Args>
inline kj::byte *alloc(size_t frameSize, Args &&...args) {
  if constexpr (hasStack<Args...>) {
    auto frame = getStack(args...).alloc(frameSize);
    FUT_DBG("new stack", frameSize, frame);
    return frame->dataBegin();
  } else {
    auto frame = allocator.alloc(frameSize);
    FUT_DBG("new allocator", frameSize, frame);
    return frame->dataBegin();
  }
}

inline void free(kj::byte *ptr) {
  FUT_DBG("delete", ptr);
  auto frame =
      reinterpret_cast<Allocator::Frame *>(ptr - sizeof(Allocator::Frame));
  frame->free();
}

} // namespace _
} // namespace fut

// promise-like object
template <typename T, bool Lazy> class Fut {
public:
  using promise_type = fut::_::Coro<T, Lazy>;

  inline Fut(FutHandle<T, Lazy> h) : handle(h) {
    FUT_DBG("Fut::Fut", this, handle.address());
    handle.promise().fut = this;
  }

  inline Fut(Fut<T, Lazy> &&other) : handle(kj::mv(other.handle)) {
    FUT_DBG("Fut::Fut&&", this, &other);

    other.handle = {};
    // event = other.event;
    // other.event = nullptr;

    if (handle) {
      KJ_ASSERT(!handle.done());
      handle.promise().fut = this;
    } else {
      result = kj::mv(other.result);
    }
  }

  Fut(const Fut<T, Lazy> &other) = delete;

  inline ~Fut() {
    FUT_DBG("Fut::~Fut", this);
    if (handle) {
      // cancel the coro
      handle.promise().fut = nullptr;
      handle.destroy();
    }
  }

  inline bool done() const {
    // we always clear the handle on resolve
    return !handle;
  }

  T wait(WaitScope &waitScope, SourceLocation location = {});

  // Awaiter implementation

  inline bool await_ready() {
    FUT_DBG("Fut::await_ready", this, done());
    // no need to suspend if we're already done
    return done();
  }

  // called by co_await if await_ready() returned false.
  inline void await_suspend(std::coroutine_handle<> awaiter) {
    FUT_DBG("Fut::await_suspend", this, awaiter.address(), &promise());
    promise().awaiter = awaiter;
  }

  inline T await_resume() {
    FUT_DBG("Fut::await_resume", this);
    return _::convertToReturn(kj::mv(result));
  }

  //

  static Fut<T, Lazy> never() { return Fut<T, Lazy>(); }

private:
  Fut() {}

  FutHandle<T, Lazy> handle = {};
  _::ExceptionOr<_::FixVoid<T>> result;

  // todo: get rid of this?
  _::OnReadyEvent *event = nullptr;

  inline fut::_::CoroBase<T, Lazy> &promise() {
    KJ_ASSERT(handle != nullptr);
    return handle.promise();
  }

  inline void onReady(_::OnReadyEvent *event) noexcept {
    FUT_DBG("Fut::onReady", this, done());
    if (done()) {
      event->arm();
    } else {
      this->event = event;
    }
  }

  inline void resolveValue(_::FixVoid<T> &&t) {
    FUT_DBG("Fut::resolveValue", this, event);
    result.value = kj::mv(t);
    handle = {};
    if (event != nullptr) {
      event->arm();
      event = nullptr;
    }
  }

  inline void resolveException(kj::Exception &&e) {
    FUT_DBG("Fut:resolveException", this, handle.address());
    result.exception = kj::mv(e);
    handle = {};
    if (event != nullptr) {
      event->arm();
      event = nullptr;
    }
  }

  inline void get(_::ExceptionOr<_::FixVoid<T>> &output) noexcept {
    KJ_ASSERT(done());
    output = kj::mv(result);
  }

  friend class fut::_::CoroBase<T, Lazy>;
  friend class fut::_::Coro<T, Lazy>;
  friend class FutPromiseNode<T, Lazy>;
};

// Awaits for kj::Promise from within a Fut-coroutine
template <typename T> class PromiseFutAwaiter : public _::Event {
public:
  PromiseFutAwaiter(_::OwnPromiseNode &&promise, SourceLocation location)
      : _::Event(location), node(kj::mv(promise)) {
    FUT_DBG("PromiseFutAwaiter::PromiseFutAwaiter", this, node);
  }

  virtual ~PromiseFutAwaiter() {
    FUT_DBG("PromiseFutAwaiter::~PromiseFutAwaiter", this, node);
  }

  bool await_ready() {
    FUT_DBG("PromiseFutAwaiter::await_ready => false", this, node);
    // promises always suspend initially
    return false;
  }

  bool await_suspend(std::coroutine_handle<> h) {
    FUT_DBG("PromiseFutAwaiter::await_suspend", this, node, h.done(),
            h.address());
    this->handle = h;
    node->setSelfPointer(&node);
    node->onReady(this);

    if (isNext()) {
      disarm();
      FUT_DBG("PromiseFutAwaiter::await_suspend => false", this, node);
      return false;
    }

    FUT_DBG("PromiseFutAwaiter::await_suspend => true", this, node);
    return true;
  }

  T await_resume() {
    FUT_DBG("PromiseFutAwaiter::await_resume", this, node);
    _::ExceptionOr<_::FixVoid<T>> result;
    node->get(result);
    return _::convertToReturn(kj::mv(result));
  }

  void traceEvent(_::TraceBuilder &builder) override {
    FUT_DBG("PromiseFutAwaiter::traceEvent", this);
    KJ_UNIMPLEMENTED("TODO");
  }

  Maybe<Own<Event>> fire() override {
    FUT_DBG("PromiseFutAwaiter::fire", this, node);
    auto h = handle;
    handle = {};
    h.resume();
    return kj::none;
  }

  _::OwnPromiseNode node;
  std::coroutine_handle<> handle = {};
};

// make this an event and arm an event.
struct LazyInitialSuspend : public kj::_::Event {
  LazyInitialSuspend(SourceLocation location) : Event(location) {
    FUT_DBG("LazyInitialSuspend::LazyInitialSuspend", this);
  }
  ~LazyInitialSuspend() {
    FUT_DBG("LazyInitialSuspend::~LazyInitialSuspend", this);
  }
  bool await_ready() const noexcept {
    FUT_DBG("LazyInitialSuspend::await_ready", this);
    return false;
  }

  auto await_suspend(std::coroutine_handle<> h) {
    FUT_DBG("LazyInitialSuspend::await_suspend", this, h.address());
    handle = h;
    armDepthFirst();
  }

  void await_resume() const noexcept {
    FUT_DBG("LazyInitialSuspend::await_resume", this);
  }

  void traceEvent(kj::_::TraceBuilder &builder) override { 
    FUT_DBG("LazyInitialSuspend::traceEvent", this);
    KJ_UNIMPLEMENTED(); 
  }

  Maybe<Own<Event>> fire() override {
    FUT_DBG("LazyInitialSuspend::fire", this, handle.address());
    handle.resume();
    return kj::none;
  }

  std::coroutine_handle<> handle;
};

namespace fut {

namespace _ {

// Coroutine promise_type. We call it Coro not to be confused with promises
// and also because its lifetime directly corresponds to a coro frame.
template <typename T, bool Lazy> class CoroBase {
public:
  inline CoroBase() { FUT_DBG("Coro::Coro", this, Lazy); }

  inline ~CoroBase() {
    FUT_DBG("Coro::~Coro", this, fut);
    KJ_ASSERT(fut == nullptr);
  }

  auto initial_suspend() noexcept {
    FUT_DBG("Coro::initial_suspend", this, Lazy);
    if constexpr (Lazy) {
      return LazyInitialSuspend({});
    } else {
      return std::suspend_never{};
    }
  }

  auto final_suspend() noexcept {
    FUT_DBG("Coro::final_suspend", this, Lazy, awaiter == nullptr);
    if (awaiter) {
      awaiter.resume();
    }
    return std::suspend_never{};
  }

  inline Fut<T, Lazy> get_return_object() {
    FUT_DBG("Coro::get_return_object", this);
    return Fut<T, Lazy>(FutHandle<T, Lazy>::from_promise(*this));
  }

  inline void unhandled_exception() {
    FUT_DBG("Coro::unhandled_exception", this, fut);
    fut->resolveException(kj::getCaughtExceptionAsKj());
    fut = nullptr;
  }

  // await_transform

  template <typename U>
  inline PromiseFutAwaiter<U> await_transform(kj::Promise<U> &&promise) {
    // child coro creation goes through this
    FUT_DBG("Coro::await_transform Promise", this, &promise);
    return PromiseFutAwaiter<U>(kj::_::PromiseNode::from(kj::mv(promise)), {});
  }

  template <typename U> inline U &&await_transform(U &&awaitable) {
    FUT_DBG("Coro::await_transform generic", this, &awaitable);
    return kj::fwd<U>(awaitable);
  }

  // coroutine frame allocation

  template <typename... Args>
  inline void *operator new(size_t size, Args &&...args) {
    return alloc(size, args...);
  }

  inline void operator delete(void *ptr) { free(static_cast<kj::byte *>(ptr)); }

protected:
  // Will update fut result on completion.
  // Set and maintained by Fut constructors.
  // Becomes nullptr after completion again.
  Fut<T, Lazy> *fut = nullptr;

private:
  std::coroutine_handle<> awaiter = {};

  friend class Fut<T, Lazy>;
};

template <typename T, bool Lazy> class Coro : public CoroBase<T, Lazy> {
public:
  inline void return_value(T &&value) {
    FUT_DBG("Coro::return_value", this);
    this->fut->resolveValue(kj::mv(value));
    this->fut = nullptr;
  }
};

template <bool Lazy> class Coro<void, Lazy> : public CoroBase<void, Lazy> {
public:
  inline void return_void() {
    FUT_DBG("Coro::return_void", this);
    this->fut->resolveValue({});
    this->fut = nullptr;
  }
};

} // namespace _

} // namespace fut

///////////////////////
// operators

// todo: destroy
template <typename T> class FutExclusiveJoin {
public:
  FutExclusiveJoin(Fut<T> &&fut1, Fut<T> &&fut2)
      : fut1(kj::mv(fut1)), fut2(kj::mv(fut2)) {}

  bool await_ready() const noexcept {
    FUT_DBG("FutExclusiveJoin::await_ready", this);
    return fut1.done() || fut2.done();
  }

  void await_suspend(std::coroutine_handle<> h) {
    FUT_DBG("FutExclusiveJoin::await_suspend", this);
    fut1.await_suspend(h);
    fut2.await_suspend(h);
  }

  T await_resume() noexcept {
    FUT_DBG("FutExclusiveJoin::await_resume", this);
    if (fut1.done()) {
      return fut1.await_resume();
    } else {
      return fut2.await_resume();
    }
  }

private:
  Fut<T> fut1;
  Fut<T> fut2;
};

template <typename T>
FutExclusiveJoin<T> exclusiveJoin(Fut<T> &&fut1, Fut<T> &&fut2) {
  return FutExclusiveJoin<T>(kj::mv(fut1), kj::mv(fut2));
}

namespace fut {

template <typename T> class ReadyNow {
public:
  inline ReadyNow(kj::_::FixVoid<T> &&t) : t(kj::mv(t)) {}

  T wait(WaitScope &waitScope, SourceLocation location = {}) {
    return await_resume();
  }

  inline bool await_ready() { return true; }

  inline void await_suspend(std::coroutine_handle<> awaiter) { KJ_UNREACHABLE; }

  inline T await_resume() {
    if constexpr (!isVoid<T>) {
      return kj::mv(t);
    }
  }

private:
  kj::_::FixVoid<T> t;
};

template <typename T> inline ReadyNow<T> readyNow(T &&t) {
  return ReadyNow(kj::mv(t));
}

inline ReadyNow<void> readyNow() { return ReadyNow<void>({}); }

} // namespace fut

//---------------------
// KJ event loop integration

template <typename T, bool Lazy> class FutPromiseNode : public _::PromiseNode {
public:
  FutPromiseNode(Fut<T, Lazy> &fut) : fut(fut) {
    FUT_DBG("FutPromiseNode::FutPromiseNode", this, &fut);
  }

  virtual ~FutPromiseNode() {
    FUT_DBG("FutPromiseNode::~FutPromiseNode", this, &fut);
  }

  void destroy() override { FUT_DBG("FutPromiseNode::destroy", this, &fut); }

  void onReady(_::Event *event) noexcept override {
    FUT_DBG("FutPromiseNode::onReady", this, &fut);
    onReadyEvent.init(event);
    fut.onReady(&onReadyEvent);
  }

  void get(_::ExceptionOrValue &output) noexcept override {
    FUT_DBG("FutPromiseNode::get", this, &fut, &output);
    auto &result = output.as<_::FixVoid<T>>();
    fut.get(result);
  }

  void tracePromise(_::TraceBuilder &builder, bool stopAtNextEvent) override {
    KJ_UNIMPLEMENTED("todo");
  }

private:
  Fut<T, Lazy> &fut;
  _::OnReadyEvent onReadyEvent;
};

namespace _ {

template <typename T, bool Lazy> class FutEvent : public _::Event {
public:
  FutEvent(Fut<T, Lazy> *fut, SourceLocation location)
      : Event(location), fut(fut) {}

  bool fired = false;

  Maybe<Own<_::Event>> fire() override {
    fired = true;
    return kj::none;
  }

  void traceEvent(_::TraceBuilder &builder) override {
    KJ_UNIMPLEMENTED("todo");
  }

private:
  Fut<T, Lazy> *fut;
};

} // namespace _

template <typename T, bool Lazy>
inline T Fut<T, Lazy>::wait(WaitScope &waitScope, SourceLocation location) {
  FUT_DBG("Fut::wait", this);
  _::ExceptionOr<_::FixVoid<T>> result;

  EventLoop &loop = waitScope.loop;
  // KJ_REQUIRE(&loop == threadLocalEventLoop, "WaitScope not valid for this
  // thread.");

  KJ_REQUIRE(waitScope.fiber == kj::none, "fut is not supported with fiber");
  KJ_REQUIRE(!loop.running,
             "wait() is not allowed from within event callbacks.");

  _::FutEvent doneEvent(this, location);
  _::OnReadyEvent onReady;
  onReady.init(&doneEvent);
  this->onReady(&onReady);

  loop.running = true;
  KJ_DEFER(loop.running = false);

  for (;;) {
    waitScope.runOnStackPool([&]() {
      uint counter = 0;
      while (!doneEvent.fired) {
        if (!loop.turn()) {
          // No events in the queue.  Wait for callback.
          return;
        } else if (++counter > waitScope.busyPollInterval) {
          // Note: It's intentional that if busyPollInterval is kj::maxValue, we
          // never poll.
          counter = 0;
          loop.poll();
        }
      }
    });

    if (doneEvent.fired) {
      break;
    } else {
      loop.wait();
    }
  }

  loop.setRunnable(loop.isRunnable());

  waitScope.runOnStackPool([&]() {
    this->get(result);

    // todo
    // KJ_IF_SOME(exception,
    //            kj::runCatchingExceptions([this]() { delete this; })) {
    //   result.addException(kj::mv(exception));
    // }
  });

  return _::convertToReturn(kj::mv(result));
}

/////////////////
// Queue

namespace fut {

class Queue {

public:
  struct Spot {
    KJ_DISALLOW_COPY_AND_MOVE(Spot);

    inline Spot(Queue &q) : queue(q) {}

    ~Spot() { 
      queue.numOutstandingJoins--;
      if (!queue.awaiters.empty()) {
        queue.awaiters.pop().resume();
      }
     }

    Queue &queue;
  };

  struct Join {
    Queue &queue;

    Join(Queue &q) : queue(q) { q.numOutstandingJoins++; }

    inline bool await_ready() const noexcept {
      // we are the only outstanding join, no need to suspend
      return queue.numOutstandingJoins == 1;
    }

    void await_suspend(std::coroutine_handle<> h) { 
      queue.awaiters.add(h);
     }

    Spot await_resume() noexcept { return Spot(queue); }

    KJ_DISALLOW_COPY_AND_MOVE(Join);
  };

  Join join() { return Join{*this}; }

  size_t numOutstandingJoins;
  kj::Vector<std::coroutine_handle<>> awaiters;
};

} // namespace fut

} // namespace kj
