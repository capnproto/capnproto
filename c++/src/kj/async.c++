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

#undef _FORTIFY_SOURCE
// If _FORTIFY_SOURCE is defined, longjmp will complain when it detects the stack
// pointer moving in the "wrong direction", thinking you're jumping to a non-existent
// stack frame. But we use longjmp to jump between different stacks to implement fibers,
// so this check isn't appropriate for us.

#if _WIN32 || __CYGWIN__
#include <kj/win32-api-version.h>
#elif __APPLE__
// getcontext() and friends are marked deprecated on MacOS but seemingly no replacement is
// provided. It appears as if they deprecated it solely because the standards bodies deprecated it,
// which they seemingly did mainly because the proper semantics are too difficult for them to
// define. I doubt MacOS would actually remove these functions as they are widely used. But if they
// do, then I guess we'll need to fall back to using setjmp()/longjmp(), and some sort of hack
// involving sigaltstack() (and generating a fake signal I guess) in order to initialize the fiber
// in the first place. Or we could use assembly, I suppose. Either way, ick.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#define _XOPEN_SOURCE  // Must be defined to see getcontext() on MacOS.
#endif

#include "async.h"
#include "debug.h"
#include "vector.h"
#include "threadlocal.h"
#include "mutex.h"
#include "one-of.h"
#include "function.h"
#include "list.h"
#include <deque>
#include <atomic>

#if _WIN32 || __CYGWIN__
#include <windows.h>  // for Sleep(0) and fibers
#include <kj/windows-sanity.h>
#else

#if KJ_USE_FIBERS
#include <ucontext.h>
#include <setjmp.h>    // for fibers
#endif

#include <sys/mman.h>  // mmap(), for allocating new stacks
#include <unistd.h>    // sysconf()
#include <errno.h>
#endif

#if !_WIN32
#include <sched.h>    // just for sched_yield()
#endif

#if !KJ_NO_RTTI
#include <typeinfo>
#if __GNUC__
#include <cxxabi.h>
#endif
#endif

#include <stdlib.h>

#if KJ_HAS_COMPILER_FEATURE(address_sanitizer)
// Clang's address sanitizer requires special hints when switching fibers, especially in order for
// stack-use-after-return handling to work right.
//
// TODO(someday): Does GCC's sanitizer, flagged by __SANITIZE_ADDRESS__, have these hints too? I
//   don't know and am not in a position to test, so I'm assuming not for now.
#include <sanitizer/common_interface_defs.h>
#else
// Nop the hints so that we don't have to put #ifdefs around every use.
#define __sanitizer_start_switch_fiber(...)
#define __sanitizer_finish_switch_fiber(...)
#endif

#if _MSC_VER && !__clang__
// MSVC's atomic intrinsics are weird and different, whereas the C++ standard atomics match the GCC
// builtins -- except for requiring the obnoxious std::atomic<T> wrapper. So, on MSVC let's just
// #define the builtins based on the C++ library, reinterpret-casting native types to
// std::atomic... this is cheating but ugh, whatever.
template <typename T>
static std::atomic<T>* reinterpretAtomic(T* ptr) { return reinterpret_cast<std::atomic<T>*>(ptr); }
#define __atomic_store_n(ptr, val, order) \
    std::atomic_store_explicit(reinterpretAtomic(ptr), val, order)
#define __atomic_load_n(ptr, order) \
    std::atomic_load_explicit(reinterpretAtomic(ptr), order)
#define __atomic_compare_exchange_n(ptr, expected, desired, weak, succ, fail) \
    std::atomic_compare_exchange_strong_explicit( \
        reinterpretAtomic(ptr), expected, desired, succ, fail)
#define __atomic_exchange_n(ptr, val, order) \
    std::atomic_exchange_explicit(reinterpretAtomic(ptr), val, order)
#define __ATOMIC_RELAXED std::memory_order_relaxed
#define __ATOMIC_ACQUIRE std::memory_order_acquire
#define __ATOMIC_RELEASE std::memory_order_release
#endif

namespace kj {

namespace {

KJ_THREADLOCAL_PTR(DisallowAsyncDestructorsScope) disallowAsyncDestructorsScope = nullptr;

}  // namespace

AsyncObject::~AsyncObject() {
  if (disallowAsyncDestructorsScope != nullptr) {
    // If we try to do the KJ_FAIL_REQUIRE here (declaring `~AsyncObject()` itself to be noexcept),
    // it seems to have a non-negligible performance impact in the HTTP benchmark. My guess is that
    // it's because it breaks inlining of `~AsyncObject()` into various subclass destructors that
    // are defined inside this file, which are some of the biggest ones. By forcing the actual
    // failure code out into a separate function we get a little performance boost.
    failed();
  }
}

void AsyncObject::failed() noexcept {
  // Since the method is noexcept, this will abort the process.
  KJ_FAIL_REQUIRE(
      kj::str("KJ async object being destroyed when not allowed: ",
              disallowAsyncDestructorsScope->reason));
}

DisallowAsyncDestructorsScope::DisallowAsyncDestructorsScope(kj::StringPtr reason)
    : reason(reason), previousValue(disallowAsyncDestructorsScope) {
  requireOnStack(this, "DisallowAsyncDestructorsScope must be allocated on the stack.");
  disallowAsyncDestructorsScope = this;
}

DisallowAsyncDestructorsScope::~DisallowAsyncDestructorsScope() {
  disallowAsyncDestructorsScope = previousValue;
}

AllowAsyncDestructorsScope::AllowAsyncDestructorsScope()
    : previousValue(disallowAsyncDestructorsScope) {
  requireOnStack(this, "AllowAsyncDestructorsScope must be allocated on the stack.");
  disallowAsyncDestructorsScope = nullptr;
}
AllowAsyncDestructorsScope::~AllowAsyncDestructorsScope() {
  disallowAsyncDestructorsScope = previousValue;
}

// =======================================================================================

namespace {

KJ_THREADLOCAL_PTR(EventLoop) threadLocalEventLoop = nullptr;

#define _kJ_ALREADY_READY reinterpret_cast< ::kj::_::Event*>(1)

EventLoop& currentEventLoop() {
  EventLoop* loop = threadLocalEventLoop;
  KJ_REQUIRE(loop != nullptr, "No event loop is running on this thread.");
  return *loop;
}

class RootEvent: public _::Event {
public:
  RootEvent(_::PromiseNode* node, void* traceAddr, SourceLocation location)
      : Event(location), node(node), traceAddr(traceAddr) {}

  bool fired = false;

  Maybe<Own<_::Event>> fire() override {
    fired = true;
    return nullptr;
  }

  void traceEvent(_::TraceBuilder& builder) override {
    node->tracePromise(builder, true);
    builder.add(traceAddr);
  }

private:
  _::PromiseNode* node;
  void* traceAddr;
};

struct DummyFunctor {
  void operator()() {};
};

}  // namespace

// =======================================================================================

void END_CANCELER_STACK_START_CANCELEE_STACK() {}
// Dummy symbol used when reporting how a Canceler was canceled. We end up combining two stack
// traces into one and we use this as a separator.

Canceler::~Canceler() noexcept(false) {
  if (isEmpty()) return;
  cancel(getDestructionReason(
      reinterpret_cast<void*>(&END_CANCELER_STACK_START_CANCELEE_STACK),
      Exception::Type::DISCONNECTED, __FILE__, __LINE__, "operation canceled"_kj));
}

void Canceler::cancel(StringPtr cancelReason) {
  if (isEmpty()) return;
  // We can't use getDestructionReason() here because if an exception is in-flight, it would use
  // that exception, totally discarding the reason given by the caller. This would probably be
  // unexpected. The caller can always use getDestructionReason() themselves if desired.
  cancel(Exception(Exception::Type::DISCONNECTED, __FILE__, __LINE__, kj::str(cancelReason)));
}

void Canceler::cancel(const Exception& exception) {
  for (;;) {
    KJ_IF_MAYBE(a, list) {
      a->unlink();
      a->cancel(kj::cp(exception));
    } else {
      break;
    }
  }
}

void Canceler::release() {
  for (;;) {
    KJ_IF_MAYBE(a, list) {
      a->unlink();
    } else {
      break;
    }
  }
}

Canceler::AdapterBase::AdapterBase(Canceler& canceler)
    : prev(canceler.list),
      next(canceler.list) {
  canceler.list = *this;
  KJ_IF_MAYBE(n, next) {
    n->prev = next;
  }
}

Canceler::AdapterBase::~AdapterBase() noexcept(false) {
  unlink();
}

void Canceler::AdapterBase::unlink() {
  KJ_IF_MAYBE(p, prev) {
    *p = next;
  }
  KJ_IF_MAYBE(n, next) {
    n->prev = prev;
  }
  next = nullptr;
  prev = nullptr;
}

Canceler::AdapterImpl<void>::AdapterImpl(kj::PromiseFulfiller<void>& fulfiller,
            Canceler& canceler, kj::Promise<void> inner)
    : AdapterBase(canceler),
      fulfiller(fulfiller),
      inner(inner.then(
          [&fulfiller]() { fulfiller.fulfill(); },
          [&fulfiller](kj::Exception&& e) { fulfiller.reject(kj::mv(e)); })
          .eagerlyEvaluate(nullptr)) {}

void Canceler::AdapterImpl<void>::cancel(kj::Exception&& e) {
  fulfiller.reject(kj::mv(e));
  inner = nullptr;
}

// =======================================================================================

TaskSet::TaskSet(TaskSet::ErrorHandler& errorHandler, SourceLocation location)
  : errorHandler(errorHandler), location(location) {}

class TaskSet::Task final: public _::PromiseArenaMember, public _::Event {
public:
  Task(_::OwnPromiseNode&& nodeParam, TaskSet& taskSet)
      : Event(taskSet.location), taskSet(taskSet), node(kj::mv(nodeParam)) {
    node->setSelfPointer(&node);
    node->onReady(this);
  }

  void destroy() override { freePromise(this); }

  OwnTask pop() {
    KJ_IF_MAYBE(n, next) { n->get()->prev = prev; }
    OwnTask self = kj::mv(KJ_ASSERT_NONNULL(*prev));
    KJ_ASSERT(self.get() == this);
    *prev = kj::mv(next);
    next = nullptr;
    prev = nullptr;
    return self;
  }

  Maybe<OwnTask> next;
  Maybe<OwnTask>* prev = nullptr;

  kj::String trace() {
    void* space[32];
    _::TraceBuilder builder(space);
    node->tracePromise(builder, false);
    return kj::str("task: ", builder);
  }

protected:
  Maybe<Own<Event>> fire() override {
    // Get the result.
    _::ExceptionOr<_::Void> result;
    node->get(result);

    // Delete the node, catching any exceptions.
    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
      node = nullptr;
    })) {
      result.addException(kj::mv(*exception));
    }

    // Remove from the task list. Do this before calling taskFailed(), so that taskFailed() can
    // safely call clear().
    auto self = pop();

    // We'll also process onEmpty() now, just in case `taskFailed()` actually destroys the whole
    // `TaskSet`.
    KJ_IF_MAYBE(f, taskSet.emptyFulfiller) {
      if (taskSet.tasks == nullptr) {
        f->get()->fulfill();
        taskSet.emptyFulfiller = nullptr;
      }
    }

    // Call the error handler if there was an exception.
    KJ_IF_MAYBE(e, result.exception) {
      taskSet.errorHandler.taskFailed(kj::mv(*e));
    }

    return Own<Event>(mv(self));
  }

  void traceEvent(_::TraceBuilder& builder) override {
    // Pointing out the ErrorHandler's taskFailed() implementation will usually identify the
    // particular TaskSet that contains this event.
    builder.add(_::getMethodStartAddress(taskSet.errorHandler, &ErrorHandler::taskFailed));
  }

private:
  TaskSet& taskSet;
  _::OwnPromiseNode node;
};

TaskSet::~TaskSet() noexcept(false) {
  // You could argue it is dubious, but some applications would like for the destructor of a
  // task to be able to schedule new tasks. So when we cancel our tasks... we might find new
  // tasks added! We'll have to repeatedly cancel. Additionally, we need to make sure that we destroy
  // the items in a loop to prevent any issues with stack overflow.
  while (tasks != nullptr) {
    auto removed = KJ_REQUIRE_NONNULL(tasks)->pop();
  }
}

void TaskSet::add(Promise<void>&& promise) {
  auto task = _::appendPromise<Task>(_::PromiseNode::from(kj::mv(promise)), *this);
  KJ_IF_MAYBE(head, tasks) {
    head->get()->prev = &task->next;
    task->next = kj::mv(tasks);
  }
  task->prev = &tasks;
  tasks = kj::mv(task);
}

kj::String TaskSet::trace() {
  kj::Vector<kj::String> traces;

  Maybe<OwnTask>* ptr = &tasks;
  for (;;) {
    KJ_IF_MAYBE(task, *ptr) {
      traces.add(task->get()->trace());
      ptr = &task->get()->next;
    } else {
      break;
    }
  }

  return kj::strArray(traces, "\n");
}

Promise<void> TaskSet::onEmpty() {
  KJ_IF_MAYBE(fulfiller, emptyFulfiller) {
    if (fulfiller->get()->isWaiting()) {
      KJ_FAIL_REQUIRE("onEmpty() can only be called once at a time");
    }
  }

  if (tasks == nullptr) {
    return READY_NOW;
  } else {
    auto paf = newPromiseAndFulfiller<void>();
    emptyFulfiller = kj::mv(paf.fulfiller);
    return kj::mv(paf.promise);
  }
}

void TaskSet::clear() {
  tasks = nullptr;

  KJ_IF_MAYBE(fulfiller, emptyFulfiller) {
    fulfiller->get()->fulfill();
  }
}

// =======================================================================================

namespace {

#if _WIN32 || __CYGWIN__
thread_local void* threadMainFiber = nullptr;

void* getMainWin32Fiber() {
  return threadMainFiber;
}
#endif

inline void ensureThreadCanRunFibers() {
#if _WIN32 || __CYGWIN__
  // Make sure the current thread has been converted to a fiber.
  void* fiber = threadMainFiber;
  if (fiber == nullptr) {
    // Thread not initialized. Convert it to a fiber now.
    // Note: Unfortunately, if the application has already converted the thread to a fiber, I
    //   guess this will fail. But trying to call GetCurrentFiber() when the thread isn't a fiber
    //   doesn't work (it returns null on WINE but not on real windows, ugh). So I guess we're
    //   just incompatible with the application doing anything with fibers, which is sad.
    threadMainFiber = fiber = ConvertThreadToFiber(nullptr);
  }
#endif
}

}  // namespace

namespace _ {

class FiberStack final {
  // A class containing a fiber stack impl. This is separate from fiber
  // promises since it lets us move the stack itself around and reuse it.

public:
  FiberStack(size_t stackSize);
  ~FiberStack() noexcept(false);

  struct SynchronousFunc {
    kj::FunctionParam<void()>& func;
    kj::Maybe<kj::Exception> exception;
  };

  void initialize(FiberBase& fiber);
  void initialize(SynchronousFunc& syncFunc);

  void reset() {
    main = {};
  }

  void switchToFiber();
  void switchToMain();

  void trace(TraceBuilder& builder) {
    // TODO(someday): Trace through fiber stack? Can it be done???
    builder.add(getMethodStartAddress(*this, &FiberStack::trace));
  }

private:
  size_t stackSize;
  OneOf<FiberBase*, SynchronousFunc*> main;

  friend class FiberBase;
  friend class FiberPool::Impl;

  struct StartRoutine;

#if KJ_USE_FIBERS
#if _WIN32 || __CYGWIN__
  void* osFiber;
#else
  struct Impl;
  Impl* impl;
#endif
#endif

  [[noreturn]] void run();

  bool isReset() { return main == nullptr; }
};

}  // namespace _

#if __linux__
// TODO(someday): Support core-local freelists on OSs other than Linux. The only tricky part is
//   finding what to use instead of sched_getcpu() to get the current CPU ID.
#define USE_CORE_LOCAL_FREELISTS 1
#endif

#if USE_CORE_LOCAL_FREELISTS
static const size_t CACHE_LINE_SIZE = 64;
// Most modern architectures have 64-byte cache lines.
#endif

class FiberPool::Impl final: private Disposer {
public:
  Impl(size_t stackSize): stackSize(stackSize) {}
  ~Impl() noexcept(false) {
#if USE_CORE_LOCAL_FREELISTS
    if (coreLocalFreelists != nullptr) {
      KJ_DEFER(free(coreLocalFreelists));

      for (uint i: kj::zeroTo(nproc)) {
        for (auto stack: coreLocalFreelists[i].stacks) {
          if (stack != nullptr) {
            delete stack;
          }
        }
      }
    }
#endif

    // Make sure we're not leaking anything from the global freelist either.
    auto lock = freelist.lockExclusive();
    auto dangling = kj::mv(*lock);
    for (auto& stack: dangling) {
      delete stack;
    }
  }

  void setMaxFreelist(size_t count) {
    maxFreelist = count;
  }

  size_t getFreelistSize() const {
    return freelist.lockShared()->size();
  }

  void useCoreLocalFreelists() {
#if USE_CORE_LOCAL_FREELISTS
    if (coreLocalFreelists != nullptr) {
      // Ignore repeat call.
      return;
    }

    int nproc_;
    KJ_SYSCALL(nproc_ = sysconf(_SC_NPROCESSORS_CONF));
    nproc = nproc_;

    void* allocPtr;
    size_t totalSize = nproc * sizeof(CoreLocalFreelist);
    int error = posix_memalign(&allocPtr, CACHE_LINE_SIZE, totalSize);
    if (error != 0) {
      KJ_FAIL_SYSCALL("posix_memalign", error);
    }
    memset(allocPtr, 0, totalSize);
    coreLocalFreelists = reinterpret_cast<CoreLocalFreelist*>(allocPtr);
#endif
  }

  Own<_::FiberStack> takeStack() const {
    // Get a stack from the pool. The disposer on the returned Own pointer will return the stack
    // to the pool, provided that reset() has been called to indicate that the stack is not in
    // a weird state.

#if USE_CORE_LOCAL_FREELISTS
    KJ_IF_MAYBE(core, lookupCoreLocalFreelist()) {
      for (auto& stackPtr: core->stacks) {
        _::FiberStack* result = __atomic_exchange_n(&stackPtr, nullptr, __ATOMIC_ACQUIRE);
        if (result != nullptr) {
          // Found a stack in this slot!
          return { result, *this };
        }
      }
      // No stacks found, fall back to global freelist.
    }
#endif

    {
      auto lock = freelist.lockExclusive();
      if (!lock->empty()) {
        _::FiberStack* result = lock->back();
        lock->pop_back();
        return { result, *this };
      }
    }

    _::FiberStack* result = new _::FiberStack(stackSize);
    return { result, *this };
  }

private:
  size_t stackSize;
  size_t maxFreelist = kj::maxValue;
  MutexGuarded<std::deque<_::FiberStack*>> freelist;

#if USE_CORE_LOCAL_FREELISTS
  struct CoreLocalFreelist {
    union {
      _::FiberStack* stacks[2];
      // For now, we don't try to freelist more than 2 stacks per core. If you have three or more
      // threads interleaved on a core, chances are you have bigger problems...

      byte padToCacheLine[CACHE_LINE_SIZE];
      // We don't want two core-local freelists to live in the same cache line, otherwise the
      // cores will fight over ownership of that line.
    };
  };

  uint nproc;
  CoreLocalFreelist* coreLocalFreelists = nullptr;

  kj::Maybe<CoreLocalFreelist&> lookupCoreLocalFreelist() const {
    if (coreLocalFreelists == nullptr) {
      return nullptr;
    } else {
      int cpu = sched_getcpu();
      if (cpu >= 0) {
        // TODO(perf): Perhaps two hyperthreads on the same physical core should share a freelist?
        //   But I don't know how to find out if the system uses hyperthreading.
        return coreLocalFreelists[cpu];
      } else {
        static bool logged = false;
        if (!logged) {
          KJ_LOG(ERROR, "invalid cpu number from sched_getcpu()?", cpu, nproc);
          logged = true;
        }
        return nullptr;
      }
    }
  }
#endif

  void disposeImpl(void* pointer) const {
    _::FiberStack* stack = reinterpret_cast<_::FiberStack*>(pointer);
    KJ_DEFER(delete stack);

    // Verify that the stack was reset before returning, otherwise it might be in a weird state
    // where we don't want to reuse it.
    if (stack->isReset()) {
#if USE_CORE_LOCAL_FREELISTS
      KJ_IF_MAYBE(core, lookupCoreLocalFreelist()) {
        for (auto& stackPtr: core->stacks) {
          stack = __atomic_exchange_n(&stackPtr, stack, __ATOMIC_RELEASE);
          if (stack == nullptr) {
            // Cool, we inserted the stack into an unused slot. We're done.
            return;
          }
        }
        // All slots were occupied, so we inserted the new stack in the front, pushed the rest back,
        // and now `stack` refers to the stack that fell off the end of the core-local list. That
        // needs to go into the global freelist.
      }
#endif

      auto lock = freelist.lockExclusive();
      lock->push_back(stack);
      if (lock->size() > maxFreelist) {
        stack = lock->front();
        lock->pop_front();
      } else {
        stack = nullptr;
      }
    }
  }
};

FiberPool::FiberPool(size_t stackSize) : impl(kj::heap<FiberPool::Impl>(stackSize)) {}
FiberPool::~FiberPool() noexcept(false) {}

void FiberPool::setMaxFreelist(size_t count) {
  impl->setMaxFreelist(count);
}

size_t FiberPool::getFreelistSize() const {
  return impl->getFreelistSize();
}

void FiberPool::useCoreLocalFreelists() {
  impl->useCoreLocalFreelists();
}

void FiberPool::runSynchronously(kj::FunctionParam<void()> func) const {
  ensureThreadCanRunFibers();

  _::FiberStack::SynchronousFunc syncFunc { func, nullptr };

  {
    auto stack = impl->takeStack();
    stack->initialize(syncFunc);
    stack->switchToFiber();
    stack->reset();  // safe to reuse
  }

  KJ_IF_MAYBE(e, syncFunc.exception) {
    kj::throwRecoverableException(kj::mv(*e));
  }
}

namespace _ {  // private

class LoggingErrorHandler: public TaskSet::ErrorHandler {
public:
  static LoggingErrorHandler instance;

  void taskFailed(kj::Exception&& exception) override {
    KJ_LOG(ERROR, "Uncaught exception in daemonized task.", exception);
  }
};

LoggingErrorHandler LoggingErrorHandler::instance = LoggingErrorHandler();

}  // namespace _ (private)

// =======================================================================================

struct Executor::Impl {
  Impl(EventLoop& loop): state(loop) {}

  struct State {
    // Queues of notifications from other threads that need this thread's attention.

    State(EventLoop& loop): loop(loop) {}

    kj::Maybe<EventLoop&> loop;
    // Becomes null when the loop is destroyed.

    List<_::XThreadEvent, &_::XThreadEvent::targetLink> start;
    List<_::XThreadEvent, &_::XThreadEvent::targetLink> cancel;
    List<_::XThreadEvent, &_::XThreadEvent::replyLink> replies;
    // Lists of events that need actioning by this thread.

    List<_::XThreadEvent, &_::XThreadEvent::targetLink> executing;
    // Events that have already been dispatched and are happily executing. This list is maintained
    // so that they can be canceled if the event loop exits.

    List<_::XThreadPaf, &_::XThreadPaf::link> fulfilled;
    // Set of XThreadPafs that have been fulfilled by another thread.

    bool waitingForCancel = false;
    // True if this thread is currently blocked waiting for some other thread to pump its
    // cancellation queue. If that other thread tries to block on *this* thread, then it could
    // deadlock -- it must take precautions against this.

    bool isDispatchNeeded() const {
      return !start.empty() || !cancel.empty() || !replies.empty() || !fulfilled.empty();
    }

    void dispatchAll(Vector<_::XThreadEvent*>& eventsToCancelOutsideLock) {
      for (auto& event: start) {
        start.remove(event);
        executing.add(event);
        event.state = _::XThreadEvent::EXECUTING;
        event.armBreadthFirst();
      }

      dispatchCancels(eventsToCancelOutsideLock);

      for (auto& event: replies) {
        replies.remove(event);
        event.onReadyEvent.armBreadthFirst();
      }

      for (auto& event: fulfilled) {
        fulfilled.remove(event);
        event.state = _::XThreadPaf::DISPATCHED;
        event.onReadyEvent.armBreadthFirst();
      }
    }

    void dispatchCancels(Vector<_::XThreadEvent*>& eventsToCancelOutsideLock) {
      for (auto& event: cancel) {
        cancel.remove(event);

        if (event.promiseNode == nullptr) {
          event.setDoneState();
        } else {
          // We can't destroy the promiseNode while the mutex is locked, because we don't know
          // what the destructor might do. But, we *must* destroy it before acknowledging
          // cancellation. So we have to add it to a list to destroy later.
          eventsToCancelOutsideLock.add(&event);
        }
      }
    }
  };

  kj::MutexGuarded<State> state;
  // After modifying state from another thread, the loop's port.wake() must be called.

  void processAsyncCancellations(Vector<_::XThreadEvent*>& eventsToCancelOutsideLock) {
    // After calling dispatchAll() or dispatchCancels() with the lock held, it may be that some
    // cancellations require dropping the lock before destroying the promiseNode. In that case
    // those cancellations will be added to the eventsToCancelOutsideLock Vector passed to the
    // method. That vector must then be passed to processAsyncCancellations() as soon as the lock
    // is released.

    for (auto& event: eventsToCancelOutsideLock) {
      event->promiseNode = nullptr;
      event->disarm();
    }

    // Now we need to mark all the events "done" under lock.
    auto lock = state.lockExclusive();
    for (auto& event: eventsToCancelOutsideLock) {
      event->setDoneState();
    }
  }

  void disconnect() {
    state.lockExclusive()->loop = nullptr;

    // Now that `loop` is set null in `state`, other threads will no longer try to manipulate our
    // lists, so we can access them without a lock. That's convenient because a bunch of the things
    // we want to do with them would require dropping the lock to avoid deadlocks. We'd end up
    // copying all the lists over into separate vectors first, dropping the lock, operating on
    // them, and then locking again.
    auto& s = state.getWithoutLock();

    // We do, however, take and release the lock on the way out, to make sure anyone performing
    // a conditional wait for state changes gets a chance to have their wait condition re-checked.
    KJ_DEFER(state.lockExclusive());

    for (auto& event: s.start) {
      KJ_ASSERT(event.state == _::XThreadEvent::QUEUED, event.state) { break; }
      s.start.remove(event);
      event.setDisconnected();
      event.sendReply();
      event.setDoneState();
    }

    for (auto& event: s.executing) {
      KJ_ASSERT(event.state == _::XThreadEvent::EXECUTING, event.state) { break; }
      s.executing.remove(event);
      event.promiseNode = nullptr;
      event.setDisconnected();
      event.sendReply();
      event.setDoneState();
    }

    for (auto& event: s.cancel) {
      KJ_ASSERT(event.state == _::XThreadEvent::CANCELING, event.state) { break; }
      s.cancel.remove(event);
      event.promiseNode = nullptr;
      event.setDoneState();
    }

    // The replies list "should" be empty, because any locally-initiated tasks should have been
    // canceled before destroying the EventLoop.
    if (!s.replies.empty()) {
      KJ_LOG(ERROR, "EventLoop destroyed with cross-thread event replies outstanding");
      for (auto& event: s.replies) {
        s.replies.remove(event);
      }
    }

    // Similarly for cross-thread fulfillers. The waiting tasks should have been canceled.
    if (!s.fulfilled.empty()) {
      KJ_LOG(ERROR, "EventLoop destroyed with cross-thread fulfiller replies outstanding");
      for (auto& event: s.fulfilled) {
        s.fulfilled.remove(event);
        event.state = _::XThreadPaf::DISPATCHED;
      }
    }
  }};

namespace _ {  // (private)

XThreadEvent::XThreadEvent(
    ExceptionOrValue& result, const Executor& targetExecutor, EventLoop& loop,
    void* funcTracePtr, SourceLocation location)
    : Event(loop, location), result(result), funcTracePtr(funcTracePtr),
      targetExecutor(targetExecutor.addRef()) {}

void XThreadEvent::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  // We can't safely trace into another thread, so we'll stop here.
  builder.add(funcTracePtr);
}

void XThreadEvent::ensureDoneOrCanceled() {
  if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) != DONE) {
    auto lock = targetExecutor->impl->state.lockExclusive();

    const EventLoop* loop;
    KJ_IF_MAYBE(l, lock->loop) {
      loop = l;
    } else {
      // Target event loop is already dead, so we know it's already working on transitioning all
      // events to the DONE state. We can just wait.
      lock.wait([&](auto&) { return state == DONE; });
      return;
    }

    switch (state) {
      case UNUSED:
        // Nothing to do.
        break;
      case QUEUED:
        lock->start.remove(*this);
        // No wake needed since we removed work rather than adding it.
        state = DONE;
        break;
      case EXECUTING: {
        lock->executing.remove(*this);
        lock->cancel.add(*this);
        state = CANCELING;
        KJ_IF_MAYBE(p, loop->port) {
          p->wake();
        }

        Maybe<Executor&> maybeSelfExecutor = nullptr;
        if (threadLocalEventLoop != nullptr) {
          KJ_IF_MAYBE(e, threadLocalEventLoop->executor) {
            maybeSelfExecutor = **e;
          }
        }

        KJ_IF_MAYBE(selfExecutor, maybeSelfExecutor) {
          // If, while waiting for other threads to process our cancellation request, we have
          // cancellation requests queued back to this thread, we must process them. Otherwise,
          // we could deadlock with two threads waiting on each other to process cancellations.
          //
          // We don't have a terribly good way to detect this, except to check if the remote
          // thread is itself waiting for cancellations and, if so, wake ourselves up to check for
          // cancellations to process. This will busy-loop but at least it should eventually
          // resolve assuming fair scheduling.
          //
          // To make things extra-annoying, in order to update our waitingForCancel flag, we have
          // to lock our own executor state, but we can't take both locks at once, so we have to
          // release the other lock in the meantime.

          // Make sure we unset waitingForCancel on the way out.
          KJ_DEFER({
            lock = {};

            Vector<_::XThreadEvent*> eventsToCancelOutsideLock;
            KJ_DEFER(selfExecutor->impl->processAsyncCancellations(eventsToCancelOutsideLock));

            auto selfLock = selfExecutor->impl->state.lockExclusive();
            selfLock->waitingForCancel = false;
            selfLock->dispatchCancels(eventsToCancelOutsideLock);

            // We don't need to re-take the lock on the other executor here; it's not used again
            // after this scope.
          });

          while (state != DONE) {
            bool otherThreadIsWaiting = lock->waitingForCancel;

            // Make sure our waitingForCancel is on and dispatch any pending cancellations on this
            // thread.
            lock = {};
            {
              Vector<_::XThreadEvent*> eventsToCancelOutsideLock;
              KJ_DEFER(selfExecutor->impl->processAsyncCancellations(eventsToCancelOutsideLock));

              auto selfLock = selfExecutor->impl->state.lockExclusive();
              selfLock->waitingForCancel = true;

              // Note that we don't have to proactively delete the PromiseNodes extracted from
              // the canceled events because those nodes belong to this thread and can't possibly
              // continue executing while we're blocked here.
              selfLock->dispatchCancels(eventsToCancelOutsideLock);
            }

            if (otherThreadIsWaiting) {
              // We know the other thread was waiting for cancellations to complete a moment ago.
              // We may have just processed the necessary cancellations in this thread, in which
              // case the other thread needs a chance to receive control and notice this. Or, it
              // may be that the other thread is waiting for some third thread to take action.
              // Either way, we should yield control here to give things a chance to settle.
              // Otherwise we could end up in a tight busy loop.
#if _WIN32
              Sleep(0);
#else
              sched_yield();
#endif
            }

            // OK now we can take the original lock again.
            lock = targetExecutor->impl->state.lockExclusive();

            // OK, now we can wait for the other thread to either process our cancellation or
            // indicate that it is waiting for remote cancellation.
            lock.wait([&](const Executor::Impl::State& executorState) {
              return state == DONE || executorState.waitingForCancel;
            });
          }
        } else {
          // We have no executor of our own so we don't have to worry about cancellation cycles
          // causing deadlock.
          //
          // NOTE: I don't think we can actually get here, because it implies that this is a
          //   synchronous execution, which means there's no way to cancel it.
          lock.wait([&](auto&) { return state == DONE; });
        }
        KJ_DASSERT(!targetLink.isLinked());
        break;
      }
      case CANCELING:
        KJ_FAIL_ASSERT("impossible state: CANCELING should only be set within the above case");
      case DONE:
        // Became done while we waited for lock. Nothing to do.
        break;
    }
  }

  KJ_IF_MAYBE(e, replyExecutor) {
    // Since we know we reached the DONE state (or never left UNUSED), we know that the remote
    // thread is all done playing with our `replyPrev` pointer. Only the current thread could
    // possibly modify it after this point. So we can skip the lock if it's already null.
    if (replyLink.isLinked()) {
      auto lock = e->impl->state.lockExclusive();
      lock->replies.remove(*this);
    }
  }
}

void XThreadEvent::sendReply() {
  KJ_IF_MAYBE(e, replyExecutor) {
    // Queue the reply.
    const EventLoop* replyLoop;
    {
      auto lock = e->impl->state.lockExclusive();
      KJ_IF_MAYBE(l, lock->loop) {
        lock->replies.add(*this);
        replyLoop = l;
      } else {
        // Calling thread exited without cancelling the promise. This is UB. In fact,
        // `replyExecutor` is probably already destroyed and we are in use-after-free territory
        // already. Better abort.
        KJ_LOG(FATAL,
            "the thread which called kj::Executor::executeAsync() apparently exited its own "
            "event loop without canceling the cross-thread promise first; this is undefined "
            "behavior so I will crash now");
        abort();
      }
    }

    // Note that it's safe to assume `replyLoop` still exists even though we dropped the lock
    // because that thread would have had to cancel any promises before destroying its own
    // EventLoop, and when it tries to destroy this promise, it will wait for `state` to become
    // `DONE`, which we don't set until later on. That's nice because wake() probably makes a
    // syscall and we'd rather not hold the lock through syscalls.
    KJ_IF_MAYBE(p, replyLoop->port) {
      p->wake();
    }
  }
}

void XThreadEvent::done() {
  KJ_ASSERT(targetExecutor.get() == &currentEventLoop().getExecutor(),
      "calling done() from wrong thread?");

  sendReply();

  {
    auto lock = targetExecutor->impl->state.lockExclusive();

    switch (state) {
      case EXECUTING:
        lock->executing.remove(*this);
        break;
      case CANCELING:
        // Sending thread requested cancellation, but we're done anyway, so it doesn't matter at this
        // point.
        lock->cancel.remove(*this);
        break;
      default:
        KJ_FAIL_ASSERT("can't call done() from this state", (uint)state);
    }

    setDoneState();
  }
}

inline void XThreadEvent::setDoneState() {
  __atomic_store_n(&state, DONE, __ATOMIC_RELEASE);
}

void XThreadEvent::setDisconnected() {
  result.addException(KJ_EXCEPTION(DISCONNECTED,
      "Executor's event loop exited before cross-thread event could complete"));
}

class XThreadEvent::DelayedDoneHack: public Disposer {
  // Crazy hack: In fire(), we want to call done() if the event is finished. But done() signals
  // the requesting thread to wake up and possibly delete the XThreadEvent. But the caller (the
  // EventLoop) still has to set `event->firing = false` after `fire()` returns, so this would be
  // a race condition use-after-free.
  //
  // It just so happens, though, that fire() is allowed to return an optional `Own<Event>` to drop,
  // and the caller drops that pointer immediately after setting event->firing = false. So we
  // return a pointer whose disposer calls done().
  //
  // It's not quite as much of a hack as it seems: The whole reason fire() returns an Own<Event> is
  // so that the event can delete itself, but do so after the caller sets event->firing = false.
  // It just happens to be that in this case, the event isn't deleting itself, but rather releasing
  // itself back to the other thread.

protected:
  void disposeImpl(void* pointer) const override {
    reinterpret_cast<XThreadEvent*>(pointer)->done();
  }
};

Maybe<Own<Event>> XThreadEvent::fire() {
  static constexpr DelayedDoneHack DISPOSER {};

  KJ_IF_MAYBE(n, promiseNode) {
    n->get()->get(result);
    promiseNode = nullptr;  // make sure to destroy in the thread that created it
    return Own<Event>(this, DISPOSER);
  } else {
    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
      promiseNode = execute();
    })) {
      result.addException(kj::mv(*exception));
    };
    KJ_IF_MAYBE(n, promiseNode) {
      n->get()->onReady(this);
    } else {
      return Own<Event>(this, DISPOSER);
    }
  }

  return nullptr;
}

void XThreadEvent::traceEvent(TraceBuilder& builder) {
  KJ_IF_MAYBE(n, promiseNode) {
    n->get()->tracePromise(builder, true);
  }

  // We can't safely trace into another thread, so we'll stop here.
  builder.add(funcTracePtr);
}

void XThreadEvent::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

XThreadPaf::XThreadPaf()
    : state(WAITING), executor(getCurrentThreadExecutor()) {}
XThreadPaf::~XThreadPaf() noexcept(false) {}

void XThreadPaf::destroy() {
  auto oldState = WAITING;

  if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) == DISPATCHED) {
    // Common case: Promise was fully fulfilled and dispatched, no need for locking.
    delete this;
  } else if (__atomic_compare_exchange_n(&state, &oldState, CANCELED, false,
                                         __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
    // State transitioned from WAITING to CANCELED, so now it's the fulfiller's job to destroy the
    // object.
  } else {
    // Whoops, another thread is already in the process of fulfilling this promise. We'll have to
    // wait for it to finish and transition the state to FULFILLED.
    executor.impl->state.when([&](auto&) {
      return state == FULFILLED || state == DISPATCHED;
    }, [&](Executor::Impl::State& exState) {
      if (state == FULFILLED) {
        // The object is on the queue but was not yet dispatched. Remove it.
        exState.fulfilled.remove(*this);
      }
    });

    // It's ours now, delete it.
    delete this;
  }
}

void XThreadPaf::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

void XThreadPaf::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  // We can't safely trace into another thread, so we'll stop here.
  // Maybe returning the address of get() will give us a function name with meaningful type
  // information.
  builder.add(getMethodStartAddress(implicitCast<PromiseNode&>(*this), &PromiseNode::get));
}

XThreadPaf::FulfillScope::FulfillScope(XThreadPaf** pointer) {
  obj = __atomic_exchange_n(pointer, static_cast<XThreadPaf*>(nullptr), __ATOMIC_ACQUIRE);
  auto oldState = WAITING;
  if (obj == nullptr) {
    // Already fulfilled (possibly by another thread).
  } else if (__atomic_compare_exchange_n(&obj->state, &oldState, FULFILLING, false,
                                         __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
    // Transitioned to FULFILLING, good.
  } else {
    // The waiting thread must have canceled.
    KJ_ASSERT(oldState == CANCELED);

    // It's our responsibility to clean up, then.
    delete obj;

    // Set `obj` null so that we don't try to fill it in or delete it later.
    obj = nullptr;
  }
}
XThreadPaf::FulfillScope::~FulfillScope() noexcept(false) {
  if (obj != nullptr) {
    auto lock = obj->executor.impl->state.lockExclusive();
    KJ_IF_MAYBE(l, lock->loop) {
      lock->fulfilled.add(*obj);
      __atomic_store_n(&obj->state, FULFILLED, __ATOMIC_RELEASE);
      KJ_IF_MAYBE(p, l->port) {
        // TODO(perf): It's annoying we have to call wake() with the lock held, but we have to
        //   prevent the destination EventLoop from being destroyed first.
        p->wake();
      }
    } else {
      KJ_LOG(FATAL,
          "the thread which called kj::newPromiseAndCrossThreadFulfiller<T>() apparently exited "
          "its own event loop without canceling the cross-thread promise first; this is "
          "undefined behavior so I will crash now");
      abort();
    }
  }
}

kj::Exception XThreadPaf::unfulfilledException() {
  // TODO(cleanup): Share code with regular PromiseAndFulfiller for stack tracing here.
  return kj::Exception(kj::Exception::Type::FAILED, __FILE__, __LINE__, kj::heapString(
      "cross-thread PromiseFulfiller was destroyed without fulfilling the promise."));
}

class ExecutorImpl: public Executor, public AtomicRefcounted {
public:
  using Executor::Executor;

  kj::Own<const Executor> addRef() const override {
    return kj::atomicAddRef(*this);
  }
};

}  // namespace _

Executor::Executor(EventLoop& loop, Badge<EventLoop>): impl(kj::heap<Impl>(loop)) {}
Executor::~Executor() noexcept(false) {}

bool Executor::isLive() const {
  return impl->state.lockShared()->loop != nullptr;
}

void Executor::send(_::XThreadEvent& event, bool sync) const {
  KJ_ASSERT(event.state == _::XThreadEvent::UNUSED);

  if (sync) {
    EventLoop* thisThread = threadLocalEventLoop;
    if (thisThread != nullptr &&
        thisThread->executor.map([this](auto& e) { return e == this; }).orDefault(false)) {
      // Invoking a sync request on our own thread. Just execute it directly; if we try to queue
      // it to the loop, we'll deadlock.
      auto promiseNode = event.execute();

      // If the function returns a promise, we have no way to pump the event loop to wait for it,
      // because the event loop may already be pumping somewhere up the stack.
      KJ_ASSERT(promiseNode == nullptr,
          "can't call executeSync() on own thread's executor with a promise-returning function");

      return;
    }
  } else {
    event.replyExecutor = getCurrentThreadExecutor();

    // Note that async requests will "just work" even if the target executor is our own thread's
    // executor. In theory we could detect this case to avoid some locking and signals but that
    // would be extra code complexity for probably little benefit.
  }

  auto lock = impl->state.lockExclusive();
  const EventLoop* loop;
  KJ_IF_MAYBE(l, lock->loop) {
    loop = l;
  } else {
    event.setDisconnected();
    return;
  }

  event.state = _::XThreadEvent::QUEUED;
  lock->start.add(event);

  KJ_IF_MAYBE(p, loop->port) {
    p->wake();
  } else {
    // Event loop will be waiting on executor.wait(), which will be woken when we unlock the mutex.
  }

  if (sync) {
    lock.wait([&](auto&) { return event.state == _::XThreadEvent::DONE; });
  }
}

void Executor::wait() {
  Vector<_::XThreadEvent*> eventsToCancelOutsideLock;
  KJ_DEFER(impl->processAsyncCancellations(eventsToCancelOutsideLock));

  auto lock = impl->state.lockExclusive();

  lock.wait([](const Impl::State& state) {
    return state.isDispatchNeeded();
  });

  lock->dispatchAll(eventsToCancelOutsideLock);
}

bool Executor::poll() {
  Vector<_::XThreadEvent*> eventsToCancelOutsideLock;
  KJ_DEFER(impl->processAsyncCancellations(eventsToCancelOutsideLock));

  auto lock = impl->state.lockExclusive();
  if (lock->isDispatchNeeded()) {
    lock->dispatchAll(eventsToCancelOutsideLock);
    return true;
  } else {
    return false;
  }
}

EventLoop& Executor::getLoop() const {
  KJ_IF_MAYBE(l, impl->state.lockShared()->loop) {
    return *l;
  } else {
    kj::throwFatalException(KJ_EXCEPTION(DISCONNECTED, "Executor's event loop has exited"));
  }
}

const Executor& getCurrentThreadExecutor() {
  return currentEventLoop().getExecutor();
}

// =======================================================================================
// Fiber implementation.

namespace _ {  // private

#if KJ_USE_FIBERS
#if !(_WIN32 || __CYGWIN__)
struct FiberStack::Impl {
  // This struct serves two purposes:
  // - It contains OS-specific state that we don't want to declare in the header.
  // - It is allocated at the top of the fiber's stack area, so the Impl pointer also serves to
  //   track where the stack was allocated.

  jmp_buf fiberJmpBuf;
  jmp_buf originalJmpBuf;

#if KJ_HAS_COMPILER_FEATURE(address_sanitizer)
  // Stuff that we need to pass to __sanitizer_start_switch_fiber() /
  // __sanitizer_finish_switch_fiber() when using ASAN.

  void* originalFakeStack = nullptr;
  void* fiberFakeStack = nullptr;
  // Pointer to ASAN "fake stack" associated with the fiber and its calling stack. Filled in by
  // __sanitizer_start_switch_fiber() before switching away, consumed by
  // __sanitizer_finish_switch_fiber() upon switching back.

  void const* originalBottom;
  size_t originalSize;
  // Size and location of the original stack before switching fibers. These are filled in by
  // __sanitizer_finish_switch_fiber() after the switch, and must be passed to
  // __sanitizer_start_switch_fiber() when switching back later.
#endif

  static Impl* alloc(size_t stackSize, ucontext_t* context) {
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
    size_t pageSize = getPageSize();
    size_t allocSize = stackSize + pageSize;  // size plus guard page and impl

    // Allocate virtual address space for the stack but make it inaccessible initially.
    // TODO(someday): Does it make sense to use MAP_GROWSDOWN on Linux? It's a kind of bizarre flag
    //   that causes the mapping to automatically allocate extra pages (beyond the range specified)
    //   until it hits something... Note that on FreeBSD, MAP_STACK has the effect that
    //   MAP_GROWSDOWN has on Linux. (MAP_STACK, meanwhile, has no effect on Linux.)
    void* stackMapping = mmap(nullptr, allocSize, PROT_NONE,
        MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (stackMapping == MAP_FAILED) {
      KJ_FAIL_SYSCALL("mmap(new stack)", errno);
    }
    KJ_ON_SCOPE_FAILURE({
      KJ_SYSCALL(munmap(stackMapping, allocSize)) { break; }
    });

    void* stack = reinterpret_cast<byte*>(stackMapping) + pageSize;
    // Now mark everything except the guard page as read-write. We assume the stack grows down, so
    // the guard page is at the beginning. No modern architecture uses stacks that grow up.
    KJ_SYSCALL(mprotect(stack, stackSize, PROT_READ | PROT_WRITE));

    // Stick `Impl` at the top of the stack.
    Impl* impl = (reinterpret_cast<Impl*>(reinterpret_cast<byte*>(stack) + stackSize) - 1);

    // Note: mmap() allocates zero'd pages so we don't have to memset() anything here.

    KJ_SYSCALL(getcontext(context));
#if __APPLE__ && __aarch64__
    // Per issue #1386, apple on arm64 zeros the entire configured stack.
    // But this is redundant, since we just allocated the stack with mmap() which
    // returns zero'd pages. Re-zeroing is both slow and results in prematurely
    // allocating pages we may not need -- it's normal for stacks to rely heavily
    // on lazy page allocation to avoid wasting memory. Instead, we lie:
    // we allocate the full size, but tell the ucontext the stack is the last
    // page only. This appears to work as no particular bounds checks or
    // anything are set up based on what we say here.
    context->uc_stack.ss_size = min(pageSize, stackSize) - sizeof(Impl);
    context->uc_stack.ss_sp = reinterpret_cast<char*>(stack) + stackSize - min(pageSize, stackSize);
#else
    context->uc_stack.ss_size = stackSize - sizeof(Impl);
    context->uc_stack.ss_sp = reinterpret_cast<char*>(stack);
#endif
    context->uc_stack.ss_flags = 0;
    // We don't use uc_link since our fiber start routine runs forever in a loop to allow for
    // reuse. When we're done with the fiber, we just destroy it, without switching to it's
    // stack. This is safe since the start routine doesn't allocate any memory or RAII objects
    // before looping.
    context->uc_link = 0;

    return impl;
  }

  static void free(Impl* impl, size_t stackSize) {
    size_t allocSize = stackSize + getPageSize();
    void* stack = reinterpret_cast<byte*>(impl + 1) - allocSize;
    KJ_SYSCALL(munmap(stack, allocSize)) { break; }
  }

  static size_t getPageSize() {
#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE _SC_PAGE_SIZE
#endif
    static size_t result = sysconf(_SC_PAGESIZE);
    return result;
  }
};
#endif
#endif

struct FiberStack::StartRoutine {
#if _WIN32 || __CYGWIN__
  static void WINAPI run(LPVOID ptr) {
    // This is the static C-style function we pass to CreateFiber().
    reinterpret_cast<FiberStack*>(ptr)->run();
  }
#else
  [[noreturn]] static void run(int arg1, int arg2) {
    // This is the static C-style function we pass to makeContext().

    // POSIX says the arguments are ints, not pointers. So we split our pointer in half in order to
    // work correctly on 64-bit machines. Gross.
    uintptr_t ptr = static_cast<uint>(arg1);
    ptr |= static_cast<uintptr_t>(static_cast<uint>(arg2)) << (sizeof(ptr) * 4);

    auto& stack = *reinterpret_cast<FiberStack*>(ptr);

    __sanitizer_finish_switch_fiber(nullptr,
        &stack.impl->originalBottom, &stack.impl->originalSize);

    // We first switch to the fiber inside of the FiberStack constructor. This is just for
    // initialization purposes, and we're expected to switch back immediately.
    stack.switchToMain();

    // OK now have a real job.
    stack.run();
  }
#endif
};

void FiberStack::run() {
  // Loop forever so that the fiber can be reused.
  for (;;) {
    KJ_SWITCH_ONEOF(main) {
      KJ_CASE_ONEOF(event, FiberBase*) {
        event->run();
      }
      KJ_CASE_ONEOF(func, SynchronousFunc*) {
        KJ_IF_MAYBE(exception, kj::runCatchingExceptions(func->func)) {
          func->exception.emplace(kj::mv(*exception));
        }
      }
    }

    // Wait for the fiber to be used again. Note the fiber might simply be destroyed without this
    // ever returning. That's OK because we don't have any nontrivial destructors on the stack
    // at this point.
    switchToMain();
  }
}

FiberStack::FiberStack(size_t stackSizeParam)
    // Force stackSize to a reasonable minimum.
    : stackSize(kj::max(stackSizeParam, 65536))
{

#if KJ_USE_FIBERS
#if _WIN32 || __CYGWIN__
  // We can create fibers before we convert the main thread into a fiber in FiberBase
  KJ_WIN32(osFiber = CreateFiber(stackSize, &StartRoutine::run, this));

#else
  // Note: Nothing below here can throw. If that changes then we need to call Impl::free(impl)
  //   on exceptions...
  ucontext_t context;
  impl = Impl::alloc(stackSize, &context);

  // POSIX says the arguments are ints, not pointers. So we split our pointer in half in order to
  // work correctly on 64-bit machines. Gross.
  uintptr_t ptr = reinterpret_cast<uintptr_t>(this);
  int arg1 = ptr & ((uintptr_t(1) << (sizeof(ptr) * 4)) - 1);
  int arg2 = ptr >> (sizeof(ptr) * 4);

  makecontext(&context, reinterpret_cast<void(*)()>(&StartRoutine::run), 2, arg1, arg2);

  __sanitizer_start_switch_fiber(&impl->originalFakeStack, impl, stackSize - sizeof(Impl));
  if (_setjmp(impl->originalJmpBuf) == 0) {
    setcontext(&context);
  }
  __sanitizer_finish_switch_fiber(impl->originalFakeStack, nullptr, nullptr);
#endif
#else
#if KJ_NO_EXCEPTIONS
  KJ_UNIMPLEMENTED("Fibers are not implemented because exceptions are disabled");
#else
  KJ_UNIMPLEMENTED(
      "Fibers are not implemented on this platform because its C library lacks setcontext() "
      "and friends. If you'd like to see fiber support added, file a bug to let us know. "
      "We can likely make it happen using assembly, but didn't want to try unless it was "
      "actually needed.");
#endif
#endif
}

FiberStack::~FiberStack() noexcept(false) {
#if KJ_USE_FIBERS
#if _WIN32 || __CYGWIN__
  DeleteFiber(osFiber);
#else
  Impl::free(impl, stackSize);
#endif
#endif
}

void FiberStack::initialize(FiberBase& fiber) {
  KJ_REQUIRE(this->main == nullptr);
  this->main = &fiber;
}

void FiberStack::initialize(SynchronousFunc& func) {
  KJ_REQUIRE(this->main == nullptr);
  this->main = &func;
}

FiberBase::FiberBase(size_t stackSize, _::ExceptionOrValue& result, SourceLocation location)
    : Event(location),  state(WAITING), stack(kj::heap<FiberStack>(stackSize)), result(result) {
  stack->initialize(*this);
  ensureThreadCanRunFibers();
}

FiberBase::FiberBase(const FiberPool& pool, _::ExceptionOrValue& result, SourceLocation location)
    : Event(location), state(WAITING), result(result) {
  stack = pool.impl->takeStack();
  stack->initialize(*this);
  ensureThreadCanRunFibers();
}

FiberBase::~FiberBase() noexcept(false) {}

void FiberBase::cancel() {
  // Called by `~Fiber()` to begin teardown. We can't do this work in `~FiberBase()` because the
  // `Fiber` subclass contains members that may still be in-use until the fiber stops.

  switch (state) {
    case WAITING:
      // We can't just free the stack while the fiber is running. We need to force it to execute
      // until finished, so we cause it to throw an exception.
      state = CANCELED;
      stack->switchToFiber();

      // The fiber should only switch back to the main stack on completion, because any further
      // calls to wait() would throw before trying to switch.
      KJ_ASSERT(state == FINISHED);

      // The fiber shut down properly so the stack is safe to reuse.
      stack->reset();
      break;

    case RUNNING:
    case CANCELED:
      // Bad news.
      KJ_LOG(FATAL, "fiber tried to cancel itself");
      ::abort();
      break;

    case FINISHED:
      // Normal completion, yay.
      stack->reset();
      break;
  }
}

Maybe<Own<Event>> FiberBase::fire() {
  KJ_ASSERT(state == WAITING);
  state = RUNNING;
  stack->switchToFiber();
  return nullptr;
}

void FiberStack::switchToFiber() {
  // Switch from the main stack to the fiber. Returns once the fiber either calls switchToMain()
  // or returns from its main function.
#if KJ_USE_FIBERS
#if _WIN32 || __CYGWIN__
  SwitchToFiber(osFiber);
#else
  __sanitizer_start_switch_fiber(&impl->originalFakeStack, impl, stackSize - sizeof(Impl));
  if (_setjmp(impl->originalJmpBuf) == 0) {
    _longjmp(impl->fiberJmpBuf, 1);
  }
  __sanitizer_finish_switch_fiber(impl->originalFakeStack, nullptr, nullptr);
#endif
#endif
}
void FiberStack::switchToMain() {
  // Switch from the fiber to the main stack. Returns the next time the main stack calls
  // switchToFiber().
#if KJ_USE_FIBERS
#if _WIN32 || __CYGWIN__
  SwitchToFiber(getMainWin32Fiber());
#else
  // TODO(someady): In theory, the last time we switch away from the fiber, we should pass `nullptr`
  //   for the first argument here, so that ASAN destroys the fake stack. However, as currently
  //   designed, we don't actually know if we're switching away for the last time. It's understood
  //   that when we call switchToMain() in FiberStack::run(), then the main stack is allowed to
  //   destroy the fiber, or reuse it. I don't want to develop a mechanism to switch back to the
  //   fiber on final destruction just to get the hints right, so instead we leak the fake stack.
  //   This doesn't seem to cause any problems -- it's not even detected by ASAN as a memory leak.
  //   But if we wanted to run ASAN builds in production or something, it might be an issue.
  __sanitizer_start_switch_fiber(&impl->fiberFakeStack,
                                 impl->originalBottom, impl->originalSize);
  if (_setjmp(impl->fiberJmpBuf) == 0) {
    _longjmp(impl->originalJmpBuf, 1);
  }
  __sanitizer_finish_switch_fiber(impl->fiberFakeStack,
                                  &impl->originalBottom, &impl->originalSize);
#endif
#endif
}

void FiberBase::run() {
#if !KJ_NO_EXCEPTIONS
  bool caughtCanceled = false;
  state = RUNNING;
  KJ_DEFER(state = FINISHED);

  WaitScope waitScope(currentEventLoop(), *this);

  try {
    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
      runImpl(waitScope);
    })) {
      result.addException(kj::mv(*exception));
    }
  } catch (CanceledException) {
    if (state != CANCELED) {
      // no idea who would throw this but it's not really our problem
      result.addException(KJ_EXCEPTION(FAILED, "Caught CanceledException, but fiber wasn't canceled"));
    }
    caughtCanceled = true;
  }

  if (state == CANCELED && !caughtCanceled) {
    KJ_LOG(ERROR, "Canceled fiber apparently caught CanceledException and didn't rethrow it. "
      "Generally, applications should not catch CanceledException, but if they do, they must always rethrow.");
  }

  onReadyEvent.arm();
#endif
}

void FiberBase::onReady(_::Event* event) noexcept {
  onReadyEvent.init(event);
}

void FiberBase::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  if (stopAtNextEvent) return;
  currentInner->tracePromise(builder, false);
  stack->trace(builder);
}

void FiberBase::traceEvent(TraceBuilder& builder) {
  currentInner->tracePromise(builder, true);
  stack->trace(builder);
  onReadyEvent.traceEvent(builder);
}

}  // namespace _ (private)

// =======================================================================================

void EventPort::setRunnable(bool runnable) {}

void EventPort::wake() const {
  kj::throwRecoverableException(KJ_EXCEPTION(UNIMPLEMENTED,
      "cross-thread wake() not implemented by this EventPort implementation"));
}

EventLoop::EventLoop()
    : daemons(kj::heap<TaskSet>(_::LoggingErrorHandler::instance)) {}

EventLoop::EventLoop(EventPort& port)
    : port(port),
      daemons(kj::heap<TaskSet>(_::LoggingErrorHandler::instance)) {}

EventLoop::~EventLoop() noexcept(false) {
  // Destroy all "daemon" tasks, noting that their destructors might register more daemon tasks.
  while (!daemons->isEmpty()) {
    auto oldDaemons = kj::mv(daemons);
    daemons = kj::heap<TaskSet>(_::LoggingErrorHandler::instance);
  }
  daemons = nullptr;

  KJ_IF_MAYBE(e, executor) {
    // Cancel all outstanding cross-thread events.
    e->get()->impl->disconnect();
  }

  // The application _should_ destroy everything using the EventLoop before destroying the
  // EventLoop itself, so if there are events on the loop, this indicates a memory leak.
  KJ_REQUIRE(head == nullptr, "EventLoop destroyed with events still in the queue.  Memory leak?",
             head->traceEvent()) {
    // Unlink all the events and hope that no one ever fires them...
    _::Event* event = head;
    while (event != nullptr) {
      _::Event* next = event->next;
      event->next = nullptr;
      event->prev = nullptr;
      event = next;
    }
    break;
  }

  KJ_REQUIRE(threadLocalEventLoop != this,
             "EventLoop destroyed while still current for the thread.") {
    threadLocalEventLoop = nullptr;
    break;
  }
}

void EventLoop::run(uint maxTurnCount) {
  running = true;
  KJ_DEFER(running = false);

  for (uint i = 0; i < maxTurnCount; i++) {
    if (!turn()) {
      break;
    }
  }

  setRunnable(isRunnable());
}

bool EventLoop::turn() {
  _::Event* event = head;

  if (event == nullptr) {
    // No events in the queue.
    return false;
  } else {
    head = event->next;
    if (head != nullptr) {
      head->prev = &head;
    }

    depthFirstInsertPoint = &head;
    if (breadthFirstInsertPoint == &event->next) {
      breadthFirstInsertPoint = &head;
    }
    if (tail == &event->next) {
      tail = &head;
    }

    event->next = nullptr;
    event->prev = nullptr;

    Maybe<Own<_::Event>> eventToDestroy;
    {
      event->firing = true;
      KJ_DEFER(event->firing = false);
      currentlyFiring = event;
      KJ_DEFER(currentlyFiring = nullptr);
      eventToDestroy = event->fire();
    }

    depthFirstInsertPoint = &head;
    return true;
  }
}

bool EventLoop::isRunnable() {
  return head != nullptr;
}

const Executor& EventLoop::getExecutor() {
  KJ_IF_MAYBE(e, executor) {
    return **e;
  } else {
    return *executor.emplace(kj::atomicRefcounted<_::ExecutorImpl>(*this, Badge<EventLoop>()));
  }
}

void EventLoop::setRunnable(bool runnable) {
  if (runnable != lastRunnableState) {
    KJ_IF_MAYBE(p, port) {
      p->setRunnable(runnable);
    }
    lastRunnableState = runnable;
  }
}

void EventLoop::enterScope() {
  KJ_REQUIRE(threadLocalEventLoop == nullptr, "This thread already has an EventLoop.");
  threadLocalEventLoop = this;
}

void EventLoop::leaveScope() {
  KJ_REQUIRE(threadLocalEventLoop == this,
             "WaitScope destroyed in a different thread than it was created in.") {
    break;
  }
  threadLocalEventLoop = nullptr;
}

void EventLoop::wait() {
  KJ_IF_MAYBE(p, port) {
    if (p->wait()) {
      // Another thread called wake(). Check for cross-thread events.
      KJ_IF_MAYBE(e, executor) {
        e->get()->poll();
      }
    }
  } else KJ_IF_MAYBE(e, executor) {
    e->get()->wait();
  } else {
    KJ_FAIL_REQUIRE("Nothing to wait for; this thread would hang forever.");
  }
}

void EventLoop::poll() {
  KJ_IF_MAYBE(p, port) {
    if (p->poll()) {
      // Another thread called wake(). Check for cross-thread events.
      KJ_IF_MAYBE(e, executor) {
        e->get()->poll();
      }
    }
  } else KJ_IF_MAYBE(e, executor) {
    e->get()->poll();
  }
}

uint WaitScope::poll(uint maxTurnCount) {
  KJ_REQUIRE(&loop == threadLocalEventLoop, "WaitScope not valid for this thread.");
  KJ_REQUIRE(!loop.running, "poll() is not allowed from within event callbacks.");

  loop.running = true;
  KJ_DEFER(loop.running = false);

  uint turnCount = 0;
  runOnStackPool([&]() {
    while (turnCount < maxTurnCount) {
      if (loop.turn()) {
        ++turnCount;
      } else {
        // No events in the queue.  Poll for I/O.
        loop.poll();

        if (!loop.isRunnable()) {
          // Still no events in the queue. We're done.
          return;
        }
      }
    }
  });
  return turnCount;
}

void WaitScope::cancelAllDetached() {
  KJ_REQUIRE(fiber == nullptr,
      "can't call cancelAllDetached() on a fiber WaitScope, only top-level");

  while (!loop.daemons->isEmpty()) {
    auto oldDaemons = kj::mv(loop.daemons);
    loop.daemons = kj::heap<TaskSet>(_::LoggingErrorHandler::instance);
    // Destroying `oldDaemons` could theoretically add new ones.
  }
}

namespace _ {  // private

#if !KJ_NO_EXCEPTIONS
static kj::CanceledException fiberCanceledException() {
  // Construct the exception to throw from wait() when the fiber has been canceled (because the
  // promise returned by startFiber() was dropped before completion).
  return kj::CanceledException { };
};
#endif

void waitImpl(_::OwnPromiseNode&& node, _::ExceptionOrValue& result, WaitScope& waitScope,
              SourceLocation location) {
  EventLoop& loop = waitScope.loop;
  KJ_REQUIRE(&loop == threadLocalEventLoop, "WaitScope not valid for this thread.");

#if !KJ_NO_EXCEPTIONS
  // we don't support fibers when running without exceptions, so just remove the whole block
  KJ_IF_MAYBE(fiber, waitScope.fiber) {
    if (fiber->state == FiberBase::CANCELED) {
      throw fiberCanceledException();
    }
    KJ_REQUIRE(fiber->state == FiberBase::RUNNING,
        "This WaitScope can only be used within the fiber that created it.");

    node->setSelfPointer(&node);
    node->onReady(fiber);

    fiber->currentInner = node;
    KJ_DEFER(fiber->currentInner = nullptr);

    // Switch to the main stack to run the event loop.
    fiber->state = FiberBase::WAITING;
    fiber->stack->switchToMain();

    // The main stack switched back to us, meaning either the event we registered with
    // node->onReady() fired, or we are being canceled by FiberBase's destructor.

    if (fiber->state == FiberBase::CANCELED) {
      throw fiberCanceledException();
    }

    KJ_ASSERT(fiber->state == FiberBase::RUNNING);
  } else {
#endif
    KJ_REQUIRE(!loop.running, "wait() is not allowed from within event callbacks.");

    RootEvent doneEvent(node, reinterpret_cast<void*>(&waitImpl), location);
    node->setSelfPointer(&node);
    node->onReady(&doneEvent);

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
            // Note: It's intentional that if busyPollInterval is kj::maxValue, we never poll.
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
#if !KJ_NO_EXCEPTIONS
  }
#endif

  waitScope.runOnStackPool([&]() {
    node->get(result);
    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
      node = nullptr;
    })) {
      result.addException(kj::mv(*exception));
    }
  });
}

bool pollImpl(_::PromiseNode& node, WaitScope& waitScope, SourceLocation location) {
  EventLoop& loop = waitScope.loop;
  KJ_REQUIRE(&loop == threadLocalEventLoop, "WaitScope not valid for this thread.");
  KJ_REQUIRE(waitScope.fiber == nullptr, "poll() is not supported in fibers.");
  KJ_REQUIRE(!loop.running, "poll() is not allowed from within event callbacks.");

  RootEvent doneEvent(&node, reinterpret_cast<void*>(&pollImpl), location);
  node.onReady(&doneEvent);

  loop.running = true;
  KJ_DEFER(loop.running = false);

  waitScope.runOnStackPool([&]() {
    while (!doneEvent.fired) {
      if (!loop.turn()) {
        // No events in the queue.  Poll for I/O.
        loop.poll();

        if (!doneEvent.fired && !loop.isRunnable()) {
          // No progress. Give up.
          node.onReady(nullptr);
          loop.setRunnable(false);
          break;
        }
      }
    }
  });

  if (!doneEvent.fired) {
    return false;
  }

  loop.setRunnable(loop.isRunnable());
  return true;
}

Promise<void> yield() {
  class YieldPromiseNode final: public _::PromiseNode {
  public:
    void destroy() override {}

    void onReady(_::Event* event) noexcept override {
      if (event) event->armBreadthFirst();
    }
    void get(_::ExceptionOrValue& output) noexcept override {
      output.as<_::Void>() = _::Void();
    }
    void tracePromise(_::TraceBuilder& builder, bool stopAtNextEvent) override {
      builder.add(reinterpret_cast<void*>(&kj::evalLater<DummyFunctor>));
    }
  };

  static YieldPromiseNode NODE;
  return _::PromiseNode::to<Promise<void>>(OwnPromiseNode(&NODE));
}

Promise<void> yieldHarder() {
  class YieldHarderPromiseNode final: public _::PromiseNode {
  public:
    void destroy() override {}

    void onReady(_::Event* event) noexcept override {
      if (event) event->armLast();
    }
    void get(_::ExceptionOrValue& output) noexcept override {
      output.as<_::Void>() = _::Void();
    }
    void tracePromise(_::TraceBuilder& builder, bool stopAtNextEvent) override {
      builder.add(reinterpret_cast<void*>(&kj::evalLast<DummyFunctor>));
    }
  };

  static YieldHarderPromiseNode NODE;
  return _::PromiseNode::to<Promise<void>>(OwnPromiseNode(&NODE));
}

OwnPromiseNode readyNow() {
  class ReadyNowPromiseNode: public ImmediatePromiseNodeBase {
    // This is like `ConstPromiseNode<Void, Void{}>`, but the compiler won't let me pass a literal
    // value of type `Void` as a template parameter. (Might require C++20?)

  public:
    void destroy() override {}
    void get(ExceptionOrValue& output) noexcept override {
      output.as<Void>() = Void();
    }
  };

  static ReadyNowPromiseNode NODE;
  return OwnPromiseNode(&NODE);
}

OwnPromiseNode neverDone() {
  class NeverDonePromiseNode final: public _::PromiseNode {
  public:
    void destroy() override {}

    void onReady(_::Event* event) noexcept override {
      // ignore
    }
    void get(_::ExceptionOrValue& output) noexcept override {
      KJ_FAIL_REQUIRE("Not ready.");
    }
    void tracePromise(_::TraceBuilder& builder, bool stopAtNextEvent) override {
      builder.add(_::getMethodStartAddress(kj::NEVER_DONE, &_::NeverDone::wait));
    }
  };

  static NeverDonePromiseNode NODE;
  return OwnPromiseNode(&NODE);
}

void NeverDone::wait(WaitScope& waitScope, SourceLocation location) const {
  ExceptionOr<Void> dummy;
  waitImpl(neverDone(), dummy, waitScope, location);
  KJ_UNREACHABLE;
}

void detach(kj::Promise<void>&& promise) {
  EventLoop& loop = currentEventLoop();
  KJ_REQUIRE(loop.daemons.get() != nullptr, "EventLoop is shutting down.") { return; }
  loop.daemons->add(kj::mv(promise));
}

Event::Event(SourceLocation location)
    : loop(currentEventLoop()), next(nullptr), prev(nullptr), location(location) {}

Event::Event(kj::EventLoop& loop, SourceLocation location)
    : loop(loop), next(nullptr), prev(nullptr), location(location) {}

Event::~Event() noexcept(false) {
  live = 0;

  // Prevent compiler from eliding this store above. This line probably isn't needed because there
  // are complex calls later in this destructor, and the compiler probably can't prove that they
  // won't come back and examine `live`, so it won't elide the write anyway. However, an
  // atomic_signal_fence is also sufficient to tell the compiler that a signal handler might access
  // `live`, so it won't optimize away the write. Note that a signal fence does not produce
  // any instructions, it just blocks compiler optimizations.
  std::atomic_signal_fence(std::memory_order_acq_rel);

  disarm();

  KJ_REQUIRE(!firing, "Promise callback destroyed itself.");
}

void Event::armDepthFirst() {
  KJ_REQUIRE(threadLocalEventLoop == &loop || threadLocalEventLoop == nullptr,
             "Event armed from different thread than it was created in.  You must use "
             "Executor to queue events cross-thread.");
  if (live != MAGIC_LIVE_VALUE) {
    ([this]() noexcept {
      KJ_FAIL_ASSERT("tried to arm Event after it was destroyed", location);
    })();
  }

  if (prev == nullptr) {
    next = *loop.depthFirstInsertPoint;
    prev = loop.depthFirstInsertPoint;
    *prev = this;
    if (next != nullptr) {
      next->prev = &next;
    }

    loop.depthFirstInsertPoint = &next;

    if (loop.breadthFirstInsertPoint == prev) {
      loop.breadthFirstInsertPoint = &next;
    }
    if (loop.tail == prev) {
      loop.tail = &next;
    }

    loop.setRunnable(true);
  }
}

void Event::armBreadthFirst() {
  KJ_REQUIRE(threadLocalEventLoop == &loop || threadLocalEventLoop == nullptr,
             "Event armed from different thread than it was created in.  You must use "
             "Executor to queue events cross-thread.");
  if (live != MAGIC_LIVE_VALUE) {
    ([this]() noexcept {
      KJ_FAIL_ASSERT("tried to arm Event after it was destroyed", location);
    })();
  }

  if (prev == nullptr) {
    next = *loop.breadthFirstInsertPoint;
    prev = loop.breadthFirstInsertPoint;
    *prev = this;
    if (next != nullptr) {
      next->prev = &next;
    }

    loop.breadthFirstInsertPoint = &next;

    if (loop.tail == prev) {
      loop.tail = &next;
    }

    loop.setRunnable(true);
  }
}

void Event::armLast() {
  KJ_REQUIRE(threadLocalEventLoop == &loop || threadLocalEventLoop == nullptr,
             "Event armed from different thread than it was created in.  You must use "
             "Executor to queue events cross-thread.");
  if (live != MAGIC_LIVE_VALUE) {
    ([this]() noexcept {
      KJ_FAIL_ASSERT("tried to arm Event after it was destroyed", location);
    })();
  }

  if (prev == nullptr) {
    next = *loop.breadthFirstInsertPoint;
    prev = loop.breadthFirstInsertPoint;
    *prev = this;
    if (next != nullptr) {
      next->prev = &next;
    }

    // We don't update loop.breadthFirstInsertPoint because we want further inserts to go *before*
    // this event.

    if (loop.tail == prev) {
      loop.tail = &next;
    }

    loop.setRunnable(true);
  }
}

bool Event::isNext() {
  return loop.running && loop.head == this;
}

void Event::disarm() {
  if (prev != nullptr) {
    if (threadLocalEventLoop != &loop && threadLocalEventLoop != nullptr) {
      KJ_LOG(FATAL, "Promise destroyed from a different thread than it was created in.");
      // There's no way out of this place without UB, so abort now.
      abort();
    }

    if (loop.tail == &next) {
      loop.tail = prev;
    }
    if (loop.depthFirstInsertPoint == &next) {
      loop.depthFirstInsertPoint = prev;
    }
    if (loop.breadthFirstInsertPoint == &next) {
      loop.breadthFirstInsertPoint = prev;
    }

    *prev = next;
    if (next != nullptr) {
      next->prev = prev;
    }

    prev = nullptr;
    next = nullptr;
  }
}

String Event::traceEvent() {
  void* space[32];
  TraceBuilder builder(space);
  traceEvent(builder);
  return kj::str(builder);
}

String TraceBuilder::toString() {
  auto result = finish();
  return kj::str(stringifyStackTraceAddresses(result),
                 stringifyStackTrace(result));
}

}  // namespace _ (private)

ArrayPtr<void* const> getAsyncTrace(ArrayPtr<void*> space) {
  EventLoop* loop = threadLocalEventLoop;
  if (loop == nullptr) return nullptr;
  if (loop->currentlyFiring == nullptr) return nullptr;

  _::TraceBuilder builder(space);
  loop->currentlyFiring->traceEvent(builder);
  return builder.finish();
}

kj::String getAsyncTrace() {
  void* space[32];
  auto trace = getAsyncTrace(space);
  return kj::str(stringifyStackTraceAddresses(trace), stringifyStackTrace(trace));
}

// =======================================================================================

namespace _ {  // private

kj::String PromiseBase::trace() {
  void* space[32];
  TraceBuilder builder(space);
  node->tracePromise(builder, false);
  return kj::str(builder);
}

void PromiseNode::setSelfPointer(OwnPromiseNode* selfPtr) noexcept {}

void PromiseNode::OnReadyEvent::init(Event* newEvent) {
  if (event == _kJ_ALREADY_READY) {
    // A new continuation was added to a promise that was already ready.  In this case, we schedule
    // breadth-first, to make it difficult for applications to accidentally starve the event loop
    // by repeatedly waiting on immediate promises.
    if (newEvent) newEvent->armBreadthFirst();
  } else {
    event = newEvent;
  }
}

void PromiseNode::OnReadyEvent::arm() {
  KJ_ASSERT(event != _kJ_ALREADY_READY, "arm() should only be called once");

  if (event != nullptr) {
    // A promise resolved and an event is already waiting on it.  In this case, arm in depth-first
    // order so that the event runs immediately after the current one.  This way, chained promises
    // execute together for better cache locality and lower latency.
    event->armDepthFirst();
  }

  event = _kJ_ALREADY_READY;
}

void PromiseNode::OnReadyEvent::armBreadthFirst() {
  KJ_ASSERT(event != _kJ_ALREADY_READY, "armBreadthFirst() should only be called once");

  if (event != nullptr) {
    // A promise resolved and an event is already waiting on it.
    event->armBreadthFirst();
  }

  event = _kJ_ALREADY_READY;
}

// -------------------------------------------------------------------

ImmediatePromiseNodeBase::ImmediatePromiseNodeBase() {}
ImmediatePromiseNodeBase::~ImmediatePromiseNodeBase() noexcept(false) {}

void ImmediatePromiseNodeBase::onReady(Event* event) noexcept {
  if (event) event->armBreadthFirst();
}

void ImmediatePromiseNodeBase::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  // Maybe returning the address of get() will give us a function name with meaningful type
  // information.
  builder.add(getMethodStartAddress(implicitCast<PromiseNode&>(*this), &PromiseNode::get));
}

ImmediateBrokenPromiseNode::ImmediateBrokenPromiseNode(Exception&& exception)
    : exception(kj::mv(exception)) {}

void ImmediateBrokenPromiseNode::destroy() { freePromise(this); }

void ImmediateBrokenPromiseNode::get(ExceptionOrValue& output) noexcept {
  output.exception = kj::mv(exception);
}

// -------------------------------------------------------------------

AttachmentPromiseNodeBase::AttachmentPromiseNodeBase(OwnPromiseNode&& dependencyParam)
    : dependency(kj::mv(dependencyParam)) {
  dependency->setSelfPointer(&dependency);
}

void AttachmentPromiseNodeBase::onReady(Event* event) noexcept {
  dependency->onReady(event);
}

void AttachmentPromiseNodeBase::get(ExceptionOrValue& output) noexcept {
  dependency->get(output);
}

void AttachmentPromiseNodeBase::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  dependency->tracePromise(builder, stopAtNextEvent);

  // TODO(debug): Maybe use __builtin_return_address to get the locations that called fork() and
  //   addBranch()?
}

void AttachmentPromiseNodeBase::dropDependency() {
  dependency = nullptr;
}

// -------------------------------------------------------------------

TransformPromiseNodeBase::TransformPromiseNodeBase(
    OwnPromiseNode&& dependencyParam, void* continuationTracePtr)
    : dependency(kj::mv(dependencyParam)), continuationTracePtr(continuationTracePtr) {
  dependency->setSelfPointer(&dependency);
}

void TransformPromiseNodeBase::onReady(Event* event) noexcept {
  dependency->onReady(event);
}

void TransformPromiseNodeBase::get(ExceptionOrValue& output) noexcept {
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    getImpl(output);
    dropDependency();
  })) {
    output.addException(kj::mv(*exception));
  }
}

void TransformPromiseNodeBase::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  // Note that we null out the dependency just before calling our own continuation, which
  // conveniently means that if we're currently executing the continuation when the trace is
  // requested, it won't trace into the obsolete dependency. Nice.
  if (dependency.get() != nullptr) {
    dependency->tracePromise(builder, stopAtNextEvent);
  }

  builder.add(continuationTracePtr);
}

void TransformPromiseNodeBase::dropDependency() {
  dependency = nullptr;
}

void TransformPromiseNodeBase::getDepResult(ExceptionOrValue& output) {
  dependency->get(output);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    dependency = nullptr;
  })) {
    output.addException(kj::mv(*exception));
  }

  KJ_IF_MAYBE(e, output.exception) {
    e->addTrace(continuationTracePtr);
  }
}

// -------------------------------------------------------------------

ForkBranchBase::ForkBranchBase(OwnForkHubBase&& hubParam): hub(kj::mv(hubParam)) {
  if (hub->tailBranch == nullptr) {
    onReadyEvent.arm();
  } else {
    // Insert into hub's linked list of branches.
    prevPtr = hub->tailBranch;
    *prevPtr = this;
    next = nullptr;
    hub->tailBranch = &next;
  }
}

ForkBranchBase::~ForkBranchBase() noexcept(false) {
  if (prevPtr != nullptr) {
    // Remove from hub's linked list of branches.
    *prevPtr = next;
    (next == nullptr ? hub->tailBranch : next->prevPtr) = prevPtr;
  }
}

void ForkBranchBase::hubReady() noexcept {
  onReadyEvent.arm();
}

void ForkBranchBase::releaseHub(ExceptionOrValue& output) {
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
    hub = nullptr;
  })) {
    output.addException(kj::mv(*exception));
  }
}

void ForkBranchBase::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

void ForkBranchBase::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  if (stopAtNextEvent) return;

  if (hub.get() != nullptr) {
    hub->inner->tracePromise(builder, false);
  }

  // TODO(debug): Maybe use __builtin_return_address to get the locations that called fork() and
  //   addBranch()?
}

// -------------------------------------------------------------------

ForkHubBase::ForkHubBase(OwnPromiseNode&& innerParam, ExceptionOrValue& resultRef,
                         SourceLocation location)
    : Event(location), inner(kj::mv(innerParam)), resultRef(resultRef) {
  inner->setSelfPointer(&inner);
  inner->onReady(this);
}

Maybe<Own<Event>> ForkHubBase::fire() {
  // Dependency is ready.  Fetch its result and then delete the node.
  inner->get(resultRef);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
    inner = nullptr;
  })) {
    resultRef.addException(kj::mv(*exception));
  }

  for (auto branch = headBranch; branch != nullptr; branch = branch->next) {
    branch->hubReady();
    *branch->prevPtr = nullptr;
    branch->prevPtr = nullptr;
  }
  *tailBranch = nullptr;

  // Indicate that the list is no longer active.
  tailBranch = nullptr;

  return nullptr;
}

void ForkHubBase::traceEvent(TraceBuilder& builder) {
  if (inner.get() != nullptr) {
    inner->tracePromise(builder, true);
  }

  if (headBranch != nullptr) {
    // We'll trace down the first branch, I guess.
    headBranch->onReadyEvent.traceEvent(builder);
  }
}

// -------------------------------------------------------------------

ChainPromiseNode::ChainPromiseNode(OwnPromiseNode innerParam, SourceLocation location)
    : Event(location), state(STEP1), inner(kj::mv(innerParam)) {
  inner->setSelfPointer(&inner);
  inner->onReady(this);
}

ChainPromiseNode::~ChainPromiseNode() noexcept(false) {}

void ChainPromiseNode::destroy() { freePromise(this); }

void ChainPromiseNode::onReady(Event* event) noexcept {
  switch (state) {
    case STEP1:
      onReadyEvent = event;
      return;
    case STEP2:
      inner->onReady(event);
      return;
  }
  KJ_UNREACHABLE;
}

void ChainPromiseNode::setSelfPointer(OwnPromiseNode* selfPtr) noexcept {
  if (state == STEP2) {
    *selfPtr = kj::mv(inner);  // deletes this!
    selfPtr->get()->setSelfPointer(selfPtr);
  } else {
    this->selfPtr = selfPtr;
  }
}

void ChainPromiseNode::get(ExceptionOrValue& output) noexcept {
  KJ_REQUIRE(state == STEP2);
  return inner->get(output);
}

void ChainPromiseNode::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  if (stopAtNextEvent && state == STEP1) {
    // In STEP1, we are an Event -- when the inner node resolves, it will arm *this* object.
    // In STEP2, we are not an Event -- when the inner node resolves, it directly arms our parent
    // event.
    return;
  }

  inner->tracePromise(builder, stopAtNextEvent);
}

Maybe<Own<Event>> ChainPromiseNode::fire() {
  KJ_REQUIRE(state != STEP2);

  static_assert(sizeof(Promise<int>) == sizeof(PromiseBase),
      "This code assumes Promise<T> does not add any new members to PromiseBase.");

  ExceptionOr<PromiseBase> intermediate;
  inner->get(intermediate);

  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
    inner = nullptr;
  })) {
    intermediate.addException(kj::mv(*exception));
  }

  KJ_IF_MAYBE(exception, intermediate.exception) {
    // There is an exception.  If there is also a value, delete it.
    kj::runCatchingExceptions([&]() { intermediate.value = nullptr; });
    // Now set step2 to a rejected promise.
    inner = allocPromise<ImmediateBrokenPromiseNode>(kj::mv(*exception));
  } else KJ_IF_MAYBE(value, intermediate.value) {
    // There is a value and no exception.  The value is itself a promise.  Adopt it as our
    // step2.
    inner = _::PromiseNode::from(kj::mv(*value));
  } else {
    // We can only get here if inner->get() returned neither an exception nor a
    // value, which never actually happens.
    KJ_FAIL_ASSERT("Inner node returned empty value.");
  }
  state = STEP2;

  if (selfPtr != nullptr) {
    // Hey, we can shorten the chain here.
    auto chain = selfPtr->downcast<ChainPromiseNode>();
    *selfPtr = kj::mv(inner);
    selfPtr->get()->setSelfPointer(selfPtr);
    if (onReadyEvent != nullptr) {
      selfPtr->get()->onReady(onReadyEvent);
    }

    // Return our self-pointer so that the caller takes care of deleting it.
    return Own<Event>(kj::Own<ChainPromiseNode>(kj::mv(chain)));
  } else {
    inner->setSelfPointer(&inner);
    if (onReadyEvent != nullptr) {
      inner->onReady(onReadyEvent);
    }

    return nullptr;
  }
}

void ChainPromiseNode::traceEvent(TraceBuilder& builder) {
  switch (state) {
    case STEP1:
      if (inner.get() != nullptr) {
        inner->tracePromise(builder, true);
      }
      if (!builder.full() && onReadyEvent != nullptr) {
        onReadyEvent->traceEvent(builder);
      }
      break;
    case STEP2:
      // This probably never happens -- a trace being generated after the meat of fire() already
      // executed. If it does, though, we probably can't do anything here. We don't know if
      // `onReadyEvent` is still valid because we passed it on to the phase-2 promise, and tracing
      // just `inner` would probably be confusing. Let's just do nothing.
      break;
  }
}

// -------------------------------------------------------------------

ExclusiveJoinPromiseNode::ExclusiveJoinPromiseNode(
    OwnPromiseNode left, OwnPromiseNode right, SourceLocation location)
    : left(*this, kj::mv(left), location), right(*this, kj::mv(right), location) {}

ExclusiveJoinPromiseNode::~ExclusiveJoinPromiseNode() noexcept(false) {}

void ExclusiveJoinPromiseNode::destroy() { freePromise(this); }

void ExclusiveJoinPromiseNode::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

void ExclusiveJoinPromiseNode::get(ExceptionOrValue& output) noexcept {
  KJ_REQUIRE(left.get(output) || right.get(output), "get() called before ready.");
}

void ExclusiveJoinPromiseNode::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  // TODO(debug): Maybe use __builtin_return_address to get the locations that called
  //   exclusiveJoin()?

  if (stopAtNextEvent) return;

  // Trace the left branch I guess.
  if (left.dependency.get() != nullptr) {
    left.dependency->tracePromise(builder, false);
  } else if (right.dependency.get() != nullptr) {
    right.dependency->tracePromise(builder, false);
  }
}

ExclusiveJoinPromiseNode::Branch::Branch(
    ExclusiveJoinPromiseNode& joinNode, OwnPromiseNode dependencyParam, SourceLocation location)
    : Event(location), joinNode(joinNode), dependency(kj::mv(dependencyParam)) {
  dependency->setSelfPointer(&dependency);
  dependency->onReady(this);
}

ExclusiveJoinPromiseNode::Branch::~Branch() noexcept(false) {}

bool ExclusiveJoinPromiseNode::Branch::get(ExceptionOrValue& output) {
  if (dependency) {
    dependency->get(output);
    return true;
  } else {
    return false;
  }
}

Maybe<Own<Event>> ExclusiveJoinPromiseNode::Branch::fire() {
  if (dependency) {
    // Cancel the branch that didn't return first.  Ignore exceptions caused by cancellation.
    if (this == &joinNode.left) {
      kj::runCatchingExceptions([&]() { joinNode.right.dependency = nullptr; });
    } else {
      kj::runCatchingExceptions([&]() { joinNode.left.dependency = nullptr; });
    }

    joinNode.onReadyEvent.arm();
  } else {
    // The other branch already fired, and this branch was canceled. It's possible for both
    // branches to fire if both were armed simultaneously.
  }
  return nullptr;
}

void ExclusiveJoinPromiseNode::Branch::traceEvent(TraceBuilder& builder) {
  if (dependency.get() != nullptr) {
    dependency->tracePromise(builder, true);
  }
  joinNode.onReadyEvent.traceEvent(builder);
}

// -------------------------------------------------------------------

ArrayJoinPromiseNodeBase::ArrayJoinPromiseNodeBase(
    Array<OwnPromiseNode> promises, ExceptionOrValue* resultParts, size_t partSize,
    SourceLocation location, ArrayJoinBehavior joinBehavior)
    : joinBehavior(joinBehavior), countLeft(promises.size()) {
  // Make the branches.
  auto builder = heapArrayBuilder<Branch>(promises.size());
  for (uint i: indices(promises)) {
    ExceptionOrValue& output = *reinterpret_cast<ExceptionOrValue*>(
        reinterpret_cast<byte*>(resultParts) + i * partSize);
    builder.add(*this, kj::mv(promises[i]), output, location);
  }
  branches = builder.finish();

  if (branches.size() == 0) {
    onReadyEvent.arm();
  }
}
ArrayJoinPromiseNodeBase::~ArrayJoinPromiseNodeBase() noexcept(false) {}

void ArrayJoinPromiseNodeBase::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

void ArrayJoinPromiseNodeBase::get(ExceptionOrValue& output) noexcept {
  for (auto& branch: branches) {
    if (joinBehavior == ArrayJoinBehavior::LAZY) {
      // This implements `joinPromises()`'s lazy evaluation semantics.
      branch.dependency->get(branch.output);
    }

    // If any of the elements threw exceptions, propagate them.
    KJ_IF_MAYBE(exception, branch.output.exception) {
      output.addException(kj::mv(*exception));
    }
  }

  // We either failed fast, or waited for all promises.
  KJ_DASSERT(countLeft == 0 || output.exception != nullptr);

  if (output.exception == nullptr) {
    // No errors.  The template subclass will need to fill in the result.
    getNoError(output);
  }
}

void ArrayJoinPromiseNodeBase::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  // TODO(debug): Maybe use __builtin_return_address to get the locations that called
  //   joinPromises()?

  if (stopAtNextEvent) return;

  // Trace the first branch I guess.
  if (branches != nullptr) {
    branches[0].dependency->tracePromise(builder, false);
  }
}

ArrayJoinPromiseNodeBase::Branch::Branch(
    ArrayJoinPromiseNodeBase& joinNode, OwnPromiseNode dependencyParam, ExceptionOrValue& output,
    SourceLocation location)
    : Event(location), joinNode(joinNode), dependency(kj::mv(dependencyParam)), output(output) {
  dependency->setSelfPointer(&dependency);
  dependency->onReady(this);
}

ArrayJoinPromiseNodeBase::Branch::~Branch() noexcept(false) {}

Maybe<Own<Event>> ArrayJoinPromiseNodeBase::Branch::fire() {
  if (--joinNode.countLeft == 0 && !joinNode.armed) {
    joinNode.onReadyEvent.arm();
    joinNode.armed = true;
  }

  if (joinNode.joinBehavior == ArrayJoinBehavior::EAGER) {
    // This implements `joinPromisesFailFast()`'s eager-evaluation semantics.
    dependency->get(output);
    if (output.exception != nullptr && !joinNode.armed) {
      joinNode.onReadyEvent.arm();
      joinNode.armed = true;
    }
  }

  return nullptr;
}

void ArrayJoinPromiseNodeBase::Branch::traceEvent(TraceBuilder& builder) {
  dependency->tracePromise(builder, true);
  joinNode.onReadyEvent.traceEvent(builder);
}

ArrayJoinPromiseNode<void>::ArrayJoinPromiseNode(
    Array<OwnPromiseNode> promises, Array<ExceptionOr<_::Void>> resultParts,
    SourceLocation location, ArrayJoinBehavior joinBehavior)
    : ArrayJoinPromiseNodeBase(kj::mv(promises), resultParts.begin(), sizeof(ExceptionOr<_::Void>),
                               location, joinBehavior),
      resultParts(kj::mv(resultParts)) {}

ArrayJoinPromiseNode<void>::~ArrayJoinPromiseNode() {}

void ArrayJoinPromiseNode<void>::destroy() { freePromise(this); }

void ArrayJoinPromiseNode<void>::getNoError(ExceptionOrValue& output) noexcept {
  output.as<_::Void>() = _::Void();
}

}  // namespace _ (private)

Promise<void> joinPromises(Array<Promise<void>>&& promises, SourceLocation location) {
  return _::PromiseNode::to<Promise<void>>(_::allocPromise<_::ArrayJoinPromiseNode<void>>(
      KJ_MAP(p, promises) { return _::PromiseNode::from(kj::mv(p)); },
      heapArray<_::ExceptionOr<_::Void>>(promises.size()), location,
      _::ArrayJoinBehavior::LAZY));
}

Promise<void> joinPromisesFailFast(Array<Promise<void>>&& promises, SourceLocation location) {
  return _::PromiseNode::to<Promise<void>>(_::allocPromise<_::ArrayJoinPromiseNode<void>>(
      KJ_MAP(p, promises) { return _::PromiseNode::from(kj::mv(p)); },
      heapArray<_::ExceptionOr<_::Void>>(promises.size()), location,
      _::ArrayJoinBehavior::EAGER));
}

namespace _ {  // (private)

// -------------------------------------------------------------------

EagerPromiseNodeBase::EagerPromiseNodeBase(
    OwnPromiseNode&& dependencyParam, ExceptionOrValue& resultRef, SourceLocation location)
    : Event(location), dependency(kj::mv(dependencyParam)), resultRef(resultRef) {
  dependency->setSelfPointer(&dependency);
  dependency->onReady(this);
}

void EagerPromiseNodeBase::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

void EagerPromiseNodeBase::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  // TODO(debug): Maybe use __builtin_return_address to get the locations that called
  //   eagerlyEvaluate()? But note that if a non-null exception handler was passed to it, that
  //   creates a TransformPromiseNode which will report the location anyhow.

  if (stopAtNextEvent) return;
  if (dependency.get() != nullptr) {
    dependency->tracePromise(builder, stopAtNextEvent);
  }
}

void EagerPromiseNodeBase::traceEvent(TraceBuilder& builder) {
  if (dependency.get() != nullptr) {
    dependency->tracePromise(builder, true);
  }
  onReadyEvent.traceEvent(builder);
}

Maybe<Own<Event>> EagerPromiseNodeBase::fire() {
  dependency->get(resultRef);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([this]() {
    dependency = nullptr;
  })) {
    resultRef.addException(kj::mv(*exception));
  }

  onReadyEvent.arm();
  return nullptr;
}

// -------------------------------------------------------------------

void AdapterPromiseNodeBase::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

void AdapterPromiseNodeBase::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  // Maybe returning the address of get() will give us a function name with meaningful type
  // information.
  builder.add(getMethodStartAddress(implicitCast<PromiseNode&>(*this), &PromiseNode::get));
}

void END_FULFILLER_STACK_START_LISTENER_STACK() {}
// Dummy symbol used when reporting how a PromiseFulfiller was destroyed without fulfilling the
// promise. We end up combining two stack traces into one and we use this as a separator.

void WeakFulfillerBase::disposeImpl(void* pointer) const {
  if (inner == nullptr) {
    // Already detached.
    delete this;
  } else {
    if (inner->isWaiting()) {
      // Let's find out if there's an exception being thrown. If so, we'll use it to reject the
      // promise.
      inner->reject(getDestructionReason(
          reinterpret_cast<void*>(&END_FULFILLER_STACK_START_LISTENER_STACK),
          kj::Exception::Type::FAILED, __FILE__, __LINE__,
          "PromiseFulfiller was destroyed without fulfilling the promise."_kj));
    }
    inner = nullptr;
  }
}

}  // namespace _ (private)

// -------------------------------------------------------------------

namespace _ {  // (private)

Promise<void> IdentityFunc<Promise<void>>::operator()() const { return READY_NOW; }

}  // namespace _ (private)

// -------------------------------------------------------------------

#if KJ_HAS_COROUTINE

namespace _ {  // (private)

CoroutineBase::CoroutineBase(stdcoro::coroutine_handle<> coroutine, ExceptionOrValue& resultRef,
                             SourceLocation location)
    : Event(location),
      coroutine(coroutine),
      resultRef(resultRef) {}
CoroutineBase::~CoroutineBase() noexcept(false) {
  readMaybe(maybeDisposalResults)->destructorRan = true;
}

void CoroutineBase::unhandled_exception() {
  // Pretty self-explanatory, we propagate the exception to the promise which owns us, unless
  // we're being destroyed, in which case we propagate it back to our disposer. Note that all
  // unhandled exceptions end up here, not just ones after the first co_await.

  auto exception = getCaughtExceptionAsKj();

  KJ_IF_MAYBE(disposalResults, maybeDisposalResults) {
    // Exception during coroutine destruction. Only record the first one.
    if (disposalResults->exception == nullptr) {
      disposalResults->exception = kj::mv(exception);
    }
  } else if (isWaiting()) {
    // Exception during coroutine execution.
    resultRef.addException(kj::mv(exception));
    scheduleResumption();
  } else {
    // Okay, what could this mean? We've already been fulfilled or rejected, but we aren't being
    // destroyed yet. The only possibility is that we are unwinding the coroutine frame due to a
    // successful completion, and something in the frame threw. We can't already be rejected,
    // because rejecting a coroutine involves throwing, which would have unwound the frame prior
    // to setting `waiting = false`.
    //
    // Since we know we're unwinding due to a successful completion, we also know that whatever
    // Event we may have armed has not yet fired, because we haven't had a chance to return to
    // the event loop.

    // final_suspend() has not been called.
    KJ_IASSERT(!coroutine.done());

    // Since final_suspend() hasn't been called, whatever Event is waiting on us has not fired,
    // and will see this exception.
    resultRef.addException(kj::mv(exception));
  }
}

void CoroutineBase::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

void CoroutineBase::tracePromise(TraceBuilder& builder, bool stopAtNextEvent) {
  if (stopAtNextEvent) return;

  KJ_IF_MAYBE(promise, promiseNodeForTrace) {
    promise->tracePromise(builder, stopAtNextEvent);
  }

  // Maybe returning the address of coroutine() will give us a function name with meaningful type
  // information. (Narrator: It doesn't.)
  builder.add(GetFunctorStartAddress<>::apply(coroutine));
};

Maybe<Own<Event>> CoroutineBase::fire() {
  // Call Awaiter::await_resume() and proceed with the coroutine. Note that this will not destroy
  // the coroutine if control flows off the end of it, because we return suspend_always() from
  // final_suspend().
  //
  // It's tempting to arrange to check for exceptions right now and reject the promise that owns
  // us without resuming the coroutine, which would save us from throwing an exception when we
  // already know where it's going. But, we don't really know: unlike in the KJ_NO_EXCEPTIONS
  // case, the `co_await` might be in a try-catch block, so we have no choice but to resume and
  // throw later.
  //
  // TODO(someday): If we ever support coroutines with -fno-exceptions, we'll need to reject the
  //   enclosing coroutine promise here, if the Awaiter's result is exceptional.

  promiseNodeForTrace = nullptr;

  coroutine.resume();

  return nullptr;
}

void CoroutineBase::traceEvent(TraceBuilder& builder) {
  KJ_IF_MAYBE(promise, promiseNodeForTrace) {
    promise->tracePromise(builder, true);
  }

  // Maybe returning the address of coroutine() will give us a function name with meaningful type
  // information. (Narrator: It doesn't.)
  builder.add(GetFunctorStartAddress<>::apply(coroutine));

  onReadyEvent.traceEvent(builder);
}

void CoroutineBase::destroy() {
  // Called by PromiseDisposer to delete the object. Basically a wrapper around coroutine.destroy()
  // with some stuff to propagate exceptions appropriately.

  // Objects in the coroutine frame might throw from their destructors, so unhandled_exception()
  // will need some way to communicate those exceptions back to us. Separately, we also want
  // confirmation that our own ~Coroutine() destructor ran. To solve this, we put a
  // DisposalResults object on the stack and set a pointer to it in the Coroutine object. This
  // indicates to unhandled_exception() and ~Coroutine() where to store the results of the
  // destruction operation.
  DisposalResults disposalResults;
  maybeDisposalResults = &disposalResults;

  // Need to save this while `unwindDetector` is still valid.
  bool shouldRethrow = !unwindDetector.isUnwinding();

  do {
    // Clang's implementation of the Coroutines TS does not destroy the Coroutine object or
    // deallocate the coroutine frame if a destructor of an object on the frame threw an
    // exception. This is despite the fact that it delivered the exception to _us_ via
    // unhandled_exception(). Anyway, it appears we can work around this by running
    // coroutine.destroy() a second time.
    //
    // On Clang, `disposalResults.exception != nullptr` implies `!disposalResults.destructorRan`.
    // We could optimize out the separate `destructorRan` flag if we verify that other compilers
    // behave the same way.
    coroutine.destroy();
  } while (!disposalResults.destructorRan);

  // WARNING: `this` is now a dangling pointer.

  KJ_IF_MAYBE(exception, disposalResults.exception) {
    if (shouldRethrow) {
      kj::throwFatalException(kj::mv(*exception));
    } else {
      // An exception is already unwinding the stack, so throwing this secondary exception would
      // call std::terminate().
    }
  }
}

CoroutineBase::AwaiterBase::AwaiterBase(OwnPromiseNode node): node(kj::mv(node)) {}
CoroutineBase::AwaiterBase::AwaiterBase(AwaiterBase&&) = default;
CoroutineBase::AwaiterBase::~AwaiterBase() noexcept(false) {
  // Make sure it's safe to generate an async stack trace between now and when the Coroutine is
  // destroyed.
  KJ_IF_MAYBE(coroutineEvent, maybeCoroutineEvent) {
    coroutineEvent->promiseNodeForTrace = nullptr;
  }

  unwindDetector.catchExceptionsIfUnwinding([this]() {
    // No need to check for a moved-from state, node will just ignore the nullification.
    node = nullptr;
  });
}

void CoroutineBase::AwaiterBase::getImpl(ExceptionOrValue& result, void* awaitedAt) {
  node->get(result);

  KJ_IF_MAYBE(exception, result.exception) {
    // Manually extend the stack trace with the instruction address where the co_await occurred.
    exception->addTrace(awaitedAt);

    // Pass kj::maxValue for ignoreCount here so that `throwFatalException()` dosen't try to
    // extend the stack trace. There's no point in extending the trace beyond the single frame we
    // added above, as the rest of the trace will always be async framework stuff that no one wants
    // to see.
    kj::throwFatalException(kj::mv(*exception), kj::maxValue);
  }
}

bool CoroutineBase::AwaiterBase::awaitSuspendImpl(CoroutineBase& coroutineEvent) {
  node->setSelfPointer(&node);
  node->onReady(&coroutineEvent);

  if (coroutineEvent.hasSuspendedAtLeastOnce && coroutineEvent.isNext()) {
    // The result is immediately ready and this coroutine is running on the event loop's stack, not
    // a user code stack. Let's cancel our event and immediately resume. It's important that we
    // don't perform this optimization if this is the first suspension, because our caller may
    // depend on running code before this promise's continuations fire.
    coroutineEvent.disarm();

    // We can resume ourselves by returning false. This accomplishes the same thing as if we had
    // returned true from await_ready().
    return false;
  } else {
    // Otherwise, we must suspend. Store a reference to the promise we're waiting on for tracing
    // purposes; coroutineEvent.fire() and/or ~Adapter() will null this out.
    coroutineEvent.promiseNodeForTrace = *node;
    maybeCoroutineEvent = coroutineEvent;

    coroutineEvent.hasSuspendedAtLeastOnce = true;

    return true;
  }
}

// ---------------------------------------------------------
// Helpers for coCapture()

void throwMultipleCoCaptureInvocations() {
  KJ_FAIL_REQUIRE("Attempted to invoke CaptureForCoroutine functor multiple times");
}

}  // namespace _ (private)

#endif  // KJ_HAS_COROUTINE

}  // namespace kj
