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

#if _WIN32 || __CYGWIN__
#define WIN32_LEAN_AND_MEAN 1  // lolz
#elif __APPLE__
// getcontext() and friends are marked deprecated on MacOS but seemingly no replacement is
// provided. It appears as if they deprecated it solely because the standards bodies deprecated it,
// which they seemingly did mainly because the proper sematics are too difficult for them to
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

#if _WIN32 || __CYGWIN__
#include <windows.h>  // for Sleep(0) and fibers
#include "windows-sanity.h"
#else
#include <ucontext.h>
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
#include <stdlib.h>
#endif
#endif

namespace kj {

namespace {

KJ_THREADLOCAL_PTR(EventLoop) threadLocalEventLoop = nullptr;

#define _kJ_ALREADY_READY reinterpret_cast< ::kj::_::Event*>(1)

EventLoop& currentEventLoop() {
  EventLoop* loop = threadLocalEventLoop;
  KJ_REQUIRE(loop != nullptr, "No event loop is running on this thread.");
  return *loop;
}

class BoolEvent: public _::Event {
public:
  bool fired = false;

  Maybe<Own<_::Event>> fire() override {
    fired = true;
    return nullptr;
  }
};

class YieldPromiseNode final: public _::PromiseNode {
public:
  void onReady(_::Event* event) noexcept override {
    if (event) event->armBreadthFirst();
  }
  void get(_::ExceptionOrValue& output) noexcept override {
    output.as<_::Void>() = _::Void();
  }
};

class YieldHarderPromiseNode final: public _::PromiseNode {
public:
  void onReady(_::Event* event) noexcept override {
    if (event) event->armLast();
  }
  void get(_::ExceptionOrValue& output) noexcept override {
    output.as<_::Void>() = _::Void();
  }
};

class NeverDonePromiseNode final: public _::PromiseNode {
public:
  void onReady(_::Event* event) noexcept override {
    // ignore
  }
  void get(_::ExceptionOrValue& output) noexcept override {
    KJ_FAIL_REQUIRE("Not ready.");
  }
};

}  // namespace

// =======================================================================================

Canceler::~Canceler() noexcept(false) {
  cancel("operation canceled");
}

void Canceler::cancel(StringPtr cancelReason) {
  if (isEmpty()) return;
  cancel(Exception(Exception::Type::FAILED, __FILE__, __LINE__, kj::str(cancelReason)));
}

void Canceler::cancel(const Exception& exception) {
  for (;;) {
    KJ_IF_MAYBE(a, list) {
      list = a->next;
      a->prev = nullptr;
      a->next = nullptr;
      a->cancel(kj::cp(exception));
    } else {
      break;
    }
  }
}

void Canceler::release() {
  for (;;) {
    KJ_IF_MAYBE(a, list) {
      list = a->next;
      a->prev = nullptr;
      a->next = nullptr;
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
  KJ_IF_MAYBE(p, prev) {
    *p = next;
  }
  KJ_IF_MAYBE(n, next) {
    n->prev = prev;
  }
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

TaskSet::TaskSet(TaskSet::ErrorHandler& errorHandler)
  : errorHandler(errorHandler) {}

TaskSet::~TaskSet() noexcept(false) {}

class TaskSet::Task final: public _::Event {
public:
  Task(TaskSet& taskSet, Own<_::PromiseNode>&& nodeParam)
      : taskSet(taskSet), node(kj::mv(nodeParam)) {
    node->setSelfPointer(&node);
    node->onReady(this);
  }

  Maybe<Own<Task>> next;
  Maybe<Own<Task>>* prev = nullptr;

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

    // Call the error handler if there was an exception.
    KJ_IF_MAYBE(e, result.exception) {
      taskSet.errorHandler.taskFailed(kj::mv(*e));
    }

    // Remove from the task list.
    KJ_IF_MAYBE(n, next) {
      n->get()->prev = prev;
    }
    Own<Event> self = kj::mv(KJ_ASSERT_NONNULL(*prev));
    KJ_ASSERT(self.get() == this);
    *prev = kj::mv(next);
    next = nullptr;
    prev = nullptr;

    KJ_IF_MAYBE(f, taskSet.emptyFulfiller) {
      if (taskSet.tasks == nullptr) {
        f->get()->fulfill();
        taskSet.emptyFulfiller = nullptr;
      }
    }

    return mv(self);
  }

  _::PromiseNode* getInnerForTrace() override {
    return node;
  }

private:
  TaskSet& taskSet;
  Own<_::PromiseNode> node;
};

void TaskSet::add(Promise<void>&& promise) {
  auto task = heap<Task>(*this, _::PromiseNode::from(kj::mv(promise)));
  KJ_IF_MAYBE(head, tasks) {
    head->get()->prev = &task->next;
    task->next = kj::mv(tasks);
  }
  task->prev = &tasks;
  tasks = kj::mv(task);
}

kj::String TaskSet::trace() {
  kj::Vector<kj::String> traces;

  Maybe<Own<Task>>* ptr = &tasks;
  for (;;) {
    KJ_IF_MAYBE(task, *ptr) {
      traces.add(task->get()->trace());
      ptr = &task->get()->next;
    } else {
      break;
    }
  }

  return kj::strArray(traces, "\n============================================\n");
}

Promise<void> TaskSet::onEmpty() {
  KJ_REQUIRE(emptyFulfiller == nullptr, "onEmpty() can only be called once at a time");

  if (tasks == nullptr) {
    return READY_NOW;
  } else {
    auto paf = newPromiseAndFulfiller<void>();
    emptyFulfiller = kj::mv(paf.fulfiller);
    return kj::mv(paf.promise);
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
  typedef Maybe<_::XThreadEvent&> _::XThreadEvent::*NextMember;
  typedef Maybe<_::XThreadEvent&>* _::XThreadEvent::*PrevMember;

  template <NextMember next, PrevMember prev>
  struct List {
    kj::Maybe<_::XThreadEvent&> head;
    kj::Maybe<_::XThreadEvent&>* tail = &head;

    bool empty() const {
      return head == nullptr;
    }

    void insert(_::XThreadEvent& event) {
      KJ_REQUIRE(event.*prev == nullptr);
      *tail = event;
      event.*prev = tail;
      tail = &(event.*next);
    }

    void erase(_::XThreadEvent& event) {
      KJ_REQUIRE(event.*prev != nullptr);
      *(event.*prev) = event.*next;
      KJ_IF_MAYBE(n, event.*next) {
        n->*prev = event.*prev;
      } else {
        KJ_DASSERT(tail == &(event.*next));
        tail = event.*prev;
      }
      event.*next = nullptr;
      event.*prev = nullptr;
    }

    template <typename Func>
    void forEach(Func&& func) {
      kj::Maybe<_::XThreadEvent&> current = head;
      for (;;) {
        KJ_IF_MAYBE(c, current) {
          auto nextItem = c->*next;
          func(*c);
          current = nextItem;
        } else {
          break;
        }
      }
    }
  };

  struct State {
    // Queues of notifications from other threads that need this thread's attention.

    List<&_::XThreadEvent::targetNext, &_::XThreadEvent::targetPrev> run;
    List<&_::XThreadEvent::targetNext, &_::XThreadEvent::targetPrev> cancel;
    List<&_::XThreadEvent::replyNext, &_::XThreadEvent::replyPrev> replies;

    bool waitingForCancel = false;
    // True if this thread is currently blocked waiting for some other thread to pump its
    // cancellation queue. If that other thread tries to block on *this* thread, then it could
    // deadlock -- it must take precautions against this.

    bool empty() const {
      return run.empty() && cancel.empty() && replies.empty();
    }

    void dispatchAll(Vector<_::XThreadEvent*>& eventsToCancelOutsideLock) {
      run.forEach([&](_::XThreadEvent& event) {
        run.erase(event);
        event.state = _::XThreadEvent::EXECUTING;
        event.armBreadthFirst();
      });

      dispatchCancels(eventsToCancelOutsideLock);

      replies.forEach([&](_::XThreadEvent& event) {
        replies.erase(event);
        event.onReadyEvent.armBreadthFirst();
      });
    }

    void dispatchCancels(Vector<_::XThreadEvent*>& eventsToCancelOutsideLock) {
      cancel.forEach([&](_::XThreadEvent& event) {
        cancel.erase(event);

        if (event.promiseNode == nullptr) {
          event.state = _::XThreadEvent::DONE;
        } else {
          // We can't destroy the promiseNode while the mutex is locked, because we don't know
          // what the destructor might do. But, we *must* destroy it before acknowledging
          // cancellation. So we have to add it to a list to destroy later.
          eventsToCancelOutsideLock.add(&event);
        }
      });
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
      event->state = _::XThreadEvent::DONE;
    }
  }
};

namespace _ {  // (private)

void XThreadEvent::ensureDoneOrCanceled() {
#if _MSC_VER
  {  // TODO(perf): TODO(msvc): Implement the double-checked lock optimization on MSVC.
#else
  if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) != DONE) {
#endif
    auto lock = targetExecutor.impl->state.lockExclusive();
    switch (state) {
      case UNUSED:
        // Nothing to do.
        break;
      case QUEUED:
        lock->run.erase(*this);
        // No wake needed since we removed work rather than adding it.
        state = DONE;
        break;
      case EXECUTING: {
        lock->cancel.insert(*this);
        KJ_IF_MAYBE(p, targetExecutor.loop.port) {
          p->wake();
        }

        Maybe<Executor&> maybeSelfExecutor = nullptr;
        if (threadLocalEventLoop != nullptr) {
          KJ_IF_MAYBE(e, threadLocalEventLoop->executor) {
            maybeSelfExecutor = *e;
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
            lock = targetExecutor.impl->state.lockExclusive();

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
        KJ_DASSERT(targetPrev == nullptr);
        break;
      }
      case DONE:
        // Became done while we waited for lock. Nothing to do.
        break;
    }
  }

  KJ_IF_MAYBE(e, replyExecutor) {
    // Since we know we reached the DONE state (or never left UNUSED), we know that the remote
    // thread is all done playing with our `replyPrev` pointer. Only the current thread could
    // possibly modify it after this point. So we can skip the lock if it's already null.
    if (replyPrev != nullptr) {
      auto lock = e->impl->state.lockExclusive();
      lock->replies.erase(*this);
    }
  }
}

void XThreadEvent::done() {
  KJ_IF_MAYBE(e, replyExecutor) {
    // Queue the reply.
    {
      auto lock = e->impl->state.lockExclusive();
      lock->replies.insert(*this);
    }

    KJ_IF_MAYBE(p, e->loop.port) {
      p->wake();
    }
  }

  {
    auto lock = targetExecutor.impl->state.lockExclusive();
    KJ_DASSERT(state == EXECUTING);

    if (targetPrev != nullptr) {
      // We must be in the cancel list, because we can't be in the run list during EXECUTING state.
      // We can remove ourselves from the cancel list because at this point we're done anyway, so
      // whatever.
      lock->cancel.erase(*this);
    }

#if _MSC_VER
    // TODO(perf): TODO(msvc): Implement the double-checked lock optimization on MSVC.
    state = DONE;
#else
    __atomic_store_n(&state, DONE, __ATOMIC_RELEASE);
#endif
  }
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

void XThreadEvent::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

}  // namespace _

Executor::Executor(EventLoop& loop, Badge<EventLoop>): loop(loop), impl(kj::heap<Impl>()) {}
Executor::~Executor() noexcept(false) {}

void Executor::send(_::XThreadEvent& event, bool sync) const {
  KJ_ASSERT(event.state == _::XThreadEvent::UNUSED);

  if (sync) {
    if (threadLocalEventLoop == &loop) {
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
  event.state = _::XThreadEvent::QUEUED;
  lock->run.insert(event);

  KJ_IF_MAYBE(p, loop.port) {
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
    return !state.empty();
  });

  lock->dispatchAll(eventsToCancelOutsideLock);
}

bool Executor::poll() {
  Vector<_::XThreadEvent*> eventsToCancelOutsideLock;
  KJ_DEFER(impl->processAsyncCancellations(eventsToCancelOutsideLock));

  auto lock = impl->state.lockExclusive();
  if (lock->empty()) {
    return false;
  } else {
    lock->dispatchAll(eventsToCancelOutsideLock);
    return true;
  }
}

const Executor& getCurrentThreadExecutor() {
  return currentEventLoop().getExecutor();
}

// =======================================================================================
// Fiber implementation.

namespace _ {  // private

#if !(_WIN32 || __CYGWIN__)
struct FiberBase::Impl {
  // This struct serves two purposes:
  // - It contains OS-specific state that we don't want to declare in the header.
  // - It is allocated at the top of the fiber's stack area, so the Impl pointer also serves to
  //   track where the stack was allocated.

  ucontext_t fiberContext;
  ucontext_t originalContext;

  static Impl& alloc(size_t stackSize) {
    // TODO(perf): Freelist stacks to avoid TLB flushes.

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifndef MAP_STACK
#define MAP_STACK 0
#endif

    size_t pageSize = getPageSize();
    size_t allocSize = stackSize + pageSize;  // size plus guard page

    // Allocate virtual address space for the stack but make it inaccessible initially.
    // TODO(someday): Does it make sense to use MAP_GROWSDOWN on Linux? It's a kind of bizarre flag
    //   that causes the mapping to automatically allocate extra pages (beyond the range specified)
    //   until it hits something...
    void* stack = mmap(nullptr, allocSize, PROT_NONE,
        MAP_ANONYMOUS | MAP_PRIVATE | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
      KJ_FAIL_SYSCALL("mmap(new stack)", errno);
    }
    KJ_ON_SCOPE_FAILURE({
      KJ_SYSCALL(munmap(stack, allocSize)) { break; }
    });

    // Now mark everything except the guard page as read-write. We assume the stack grows down, so
    // the guard page is at the beginning. No modern architecture uses stacks that grow up.
    KJ_SYSCALL(mprotect(reinterpret_cast<byte*>(stack) + pageSize,
                        stackSize, PROT_READ | PROT_WRITE));

    // Stick `Impl` at the top of the stack.
    Impl& impl = *(reinterpret_cast<Impl*>(reinterpret_cast<byte*>(stack) + allocSize) - 1);

    // Note: mmap() allocates zero'd pages so we don't have to memset() anything here.

    KJ_SYSCALL(getcontext(&impl.fiberContext));
    impl.fiberContext.uc_stack.ss_size = allocSize - sizeof(Impl);
    impl.fiberContext.uc_stack.ss_sp = reinterpret_cast<char*>(stack);
    impl.fiberContext.uc_stack.ss_flags = 0;
    impl.fiberContext.uc_link = &impl.originalContext;

    return impl;
  }

  static void free(Impl& impl, size_t stackSize) {
    size_t allocSize = stackSize + getPageSize();
    void* stack = reinterpret_cast<byte*>(&impl + 1) - allocSize;
    KJ_SYSCALL(munmap(stack, allocSize)) { break; }
  }

  static size_t getPageSize() {
#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE _SC_PAGE_SIZE
#endif
    static size_t result = sysconf(_SC_PAGE_SIZE);
    return result;
  }
};
#endif

struct FiberBase::StartRoutine {
#if _WIN32 || __CYGWIN__
  static void WINAPI run(LPVOID ptr) {
    // This is the static C-style function we pass to CreateFiber().
    auto& fiber = *reinterpret_cast<FiberBase*>(ptr);
    fiber.run();

    // On Windows, if the fiber main function returns, the thread exits. We need to explicitly switch
    // back to the main stack.
    fiber.switchToMain();
  }
#else
  static void run(int arg1, int arg2) {
    // This is the static C-style function we pass to makeContext().

    // POSIX says the arguments are ints, not pointers. So we split our pointer in half in order to
    // work correctly on 64-bit machines. Gross.
    uintptr_t ptr = static_cast<uint>(arg1);
    ptr |= static_cast<uintptr_t>(static_cast<uint>(arg2)) << (sizeof(ptr) * 4);
    reinterpret_cast<FiberBase*>(ptr)->run();
  }
#endif
};

FiberBase::FiberBase(size_t stackSizeParam, _::ExceptionOrValue& result)
    : state(WAITING),
      // Force stackSize to a reasonable minimum.
      stackSize(kj::max(stackSizeParam, 65536)),
#if !(_WIN32 || __CYGWIN__)
      impl(Impl::alloc(stackSize)),
#endif
      result(result) {
#if _WIN32 || __CYGWIN__
  auto& eventLoop = currentEventLoop();
  if (eventLoop.mainFiber == nullptr) {
    // First time we've created a fiber. We need to convert the main stack into a fiber as well
    // before we can start switching.
    eventLoop.mainFiber = ConvertThreadToFiber(nullptr);
  }

  KJ_WIN32(osFiber = CreateFiber(stackSize, &StartRoutine::run, this));

#else
  // Note: Nothing below here can throw. If that changes then we need to call Impl::free(impl)
  //   on exceptions...

  // POSIX says the arguments are ints, not pointers. So we split our pointer in half in order to
  // work correctly on 64-bit machines. Gross.
  uintptr_t ptr = reinterpret_cast<uintptr_t>(this);
  int arg1 = ptr & ((uintptr_t(1) << (sizeof(ptr) * 4)) - 1);
  int arg2 = ptr >> (sizeof(ptr) * 4);

  makecontext(&impl.fiberContext, reinterpret_cast<void(*)()>(&StartRoutine::run), 2, arg1, arg2);
#endif
}

FiberBase::~FiberBase() noexcept(false) {
#if _WIN32 || __CYGWIN__
  KJ_DEFER(DeleteFiber(osFiber));
#else
  KJ_DEFER(Impl::free(impl, stackSize));
#endif

  switch (state) {
    case WAITING:
      // We can't just free the stack while the fiber is running. We need to force it to execute
      // until finished, so we cause it to throw an exception.
      state = CANCELED;
      switchToFiber();

      // The fiber should only switch back to the main stack on completion, because any further
      // calls to wait() would throw before trying to switch.
      KJ_ASSERT(state == FINISHED);
      break;

    case RUNNING:
    case CANCELED:
      // Bad news.
      KJ_LOG(FATAL, "fiber tried to destroy itself");
      ::abort();
      break;

    case FINISHED:
      // Normal completion, yay.
      break;
  }
}

Maybe<Own<Event>> FiberBase::fire() {
  KJ_ASSERT(state == WAITING);
  state = RUNNING;
  switchToFiber();
  return nullptr;
}

void FiberBase::switchToFiber() {
  // Switch from the main stack to the fiber. Returns once the fiber either calls switchToMain()
  // or returns from its main function.
#if _WIN32 || __CYGWIN__
  SwitchToFiber(osFiber);
#else
  KJ_SYSCALL(swapcontext(&impl.originalContext, &impl.fiberContext));
#endif
}
void FiberBase::switchToMain() {
  // Switch from the fiber to the main stack. Returns the next time the main stack calls
  // switchToFiber().
#if _WIN32 || __CYGWIN__
  SwitchToFiber(currentEventLoop().mainFiber);
#else
  KJ_SYSCALL(swapcontext(&impl.fiberContext, &impl.originalContext));
#endif
}

void FiberBase::run() {
  state = RUNNING;
  KJ_DEFER(state = FINISHED);

  WaitScope waitScope(currentEventLoop(), *this);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    runImpl(waitScope);
  })) {
    result.addException(kj::mv(*exception));
  }

  onReadyEvent.arm();
}

void FiberBase::onReady(_::Event* event) noexcept {
  onReadyEvent.init(event);
}

PromiseNode* FiberBase::getInnerForTrace() {
  return currentInner;
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
#if _WIN32 || __CYGWIN__
  KJ_DEFER({
    if (mainFiber != nullptr) {
      // We converted the thread to a fiber, need to convert it back.
      KJ_WIN32(ConvertFiberToThread());
    }
  });
#endif

  // Destroy all "daemon" tasks, noting that their destructors might try to access the EventLoop
  // some more.
  daemons = nullptr;

  // The application _should_ destroy everything using the EventLoop before destroying the
  // EventLoop itself, so if there are events on the loop, this indicates a memory leak.
  KJ_REQUIRE(head == nullptr, "EventLoop destroyed with events still in the queue.  Memory leak?",
             head->trace()) {
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
    return *e;
  } else {
    return executor.emplace(*this, Badge<EventLoop>());
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
        e->poll();
      }
    }
  } else KJ_IF_MAYBE(e, executor) {
    e->wait();
  } else {
    KJ_FAIL_REQUIRE("Nothing to wait for; this thread would hang forever.");
  }
}

void EventLoop::poll() {
  KJ_IF_MAYBE(p, port) {
    if (p->poll()) {
      // Another thread called wake(). Check for cross-thread events.
      KJ_IF_MAYBE(e, executor) {
        e->poll();
      }
    }
  } else KJ_IF_MAYBE(e, executor) {
    e->poll();
  }
}

void WaitScope::poll() {
  KJ_REQUIRE(&loop == threadLocalEventLoop, "WaitScope not valid for this thread.");
  KJ_REQUIRE(!loop.running, "poll() is not allowed from within event callbacks.");

  loop.running = true;
  KJ_DEFER(loop.running = false);

  for (;;) {
    if (!loop.turn()) {
      // No events in the queue.  Poll for I/O.
      loop.poll();

      if (!loop.isRunnable()) {
        // Still no events in the queue. We're done.
        return;
      }
    }
  }
}

namespace _ {  // private

static kj::Exception fiberCanceledException() {
  // Construct the exception to throw from wait() when the fiber has been canceled (because the
  // promise returned by startFiber() was dropped before completion).
  //
  // TODO(someday): Should we have a dedicated exception type for cancellation? Do we even want
  //   to build stack traces and such for these?
  return KJ_EXCEPTION(FAILED, "This fiber is being canceled.");
};

void waitImpl(Own<_::PromiseNode>&& node, _::ExceptionOrValue& result, WaitScope& waitScope) {
  EventLoop& loop = waitScope.loop;
  KJ_REQUIRE(&loop == threadLocalEventLoop, "WaitScope not valid for this thread.");

  KJ_IF_MAYBE(fiber, waitScope.fiber) {
    if (fiber->state == FiberBase::CANCELED) {
      result.addException(fiberCanceledException());
      return;
    }
    KJ_REQUIRE(fiber->state == FiberBase::RUNNING,
        "This WaitScope can only be used within the fiber that created it.");

    node->setSelfPointer(&node);
    node->onReady(fiber);

    fiber->currentInner = node;
    KJ_DEFER(fiber->currentInner = nullptr);

    // Switch to the main stack to run the event loop.
    fiber->state = FiberBase::WAITING;
    fiber->switchToMain();

    // The main stack switched back to us, meaning either the event we registered with
    // node->onReady() fired, or we are being canceled by FiberBase's destructor.

    if (fiber->state == FiberBase::CANCELED) {
      result.addException(fiberCanceledException());
      return;
    }

    KJ_ASSERT(fiber->state == FiberBase::RUNNING);
  } else {
    KJ_REQUIRE(!loop.running, "wait() is not allowed from within event callbacks.");

    BoolEvent doneEvent;
    node->setSelfPointer(&node);
    node->onReady(&doneEvent);

    loop.running = true;
    KJ_DEFER(loop.running = false);

    uint counter = 0;
    while (!doneEvent.fired) {
      if (!loop.turn()) {
        // No events in the queue.  Wait for callback.
        counter = 0;
        loop.wait();
      } else if (++counter > waitScope.busyPollInterval) {
        // Note: It's intentional that if busyPollInterval is kj::maxValue, we never poll.
        counter = 0;
        loop.poll();
      }
    }

    loop.setRunnable(loop.isRunnable());
  }

  node->get(result);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    node = nullptr;
  })) {
    result.addException(kj::mv(*exception));
  }
}

bool pollImpl(_::PromiseNode& node, WaitScope& waitScope) {
  EventLoop& loop = waitScope.loop;
  KJ_REQUIRE(&loop == threadLocalEventLoop, "WaitScope not valid for this thread.");
  KJ_REQUIRE(waitScope.fiber == nullptr, "poll() is not supported in fibers.");
  KJ_REQUIRE(!loop.running, "poll() is not allowed from within event callbacks.");

  BoolEvent doneEvent;
  node.onReady(&doneEvent);

  loop.running = true;
  KJ_DEFER(loop.running = false);

  while (!doneEvent.fired) {
    if (!loop.turn()) {
      // No events in the queue.  Poll for I/O.
      loop.poll();

      if (!doneEvent.fired && !loop.isRunnable()) {
        // No progress. Give up.
        node.onReady(nullptr);
        loop.setRunnable(false);
        return false;
      }
    }
  }

  loop.setRunnable(loop.isRunnable());
  return true;
}

Promise<void> yield() {
  return _::PromiseNode::to<Promise<void>>(kj::heap<YieldPromiseNode>());
}

Promise<void> yieldHarder() {
  return _::PromiseNode::to<Promise<void>>(kj::heap<YieldHarderPromiseNode>());
}

Own<PromiseNode> neverDone() {
  return kj::heap<NeverDonePromiseNode>();
}

void NeverDone::wait(WaitScope& waitScope) const {
  ExceptionOr<Void> dummy;
  waitImpl(neverDone(), dummy, waitScope);
  KJ_UNREACHABLE;
}

void detach(kj::Promise<void>&& promise) {
  EventLoop& loop = currentEventLoop();
  KJ_REQUIRE(loop.daemons.get() != nullptr, "EventLoop is shutting down.") { return; }
  loop.daemons->add(kj::mv(promise));
}

Event::Event()
    : loop(currentEventLoop()), next(nullptr), prev(nullptr) {}

Event::Event(kj::EventLoop& loop)
    : loop(loop), next(nullptr), prev(nullptr) {}

Event::~Event() noexcept(false) {
  disarm();

  KJ_REQUIRE(!firing, "Promise callback destroyed itself.");
}

void Event::armDepthFirst() {
  KJ_REQUIRE(threadLocalEventLoop == &loop || threadLocalEventLoop == nullptr,
             "Event armed from different thread than it was created in.  You must use "
             "Executor to queue events cross-thread.");

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

_::PromiseNode* Event::getInnerForTrace() {
  return nullptr;
}

#if !KJ_NO_RTTI
#if __GNUC__
static kj::String demangleTypeName(const char* name) {
  int status;
  char* buf = abi::__cxa_demangle(name, nullptr, nullptr, &status);
  kj::String result = kj::heapString(buf == nullptr ? name : buf);
  free(buf);
  return kj::mv(result);
}
#else
static kj::String demangleTypeName(const char* name) {
  return kj::heapString(name);
}
#endif
#endif

static kj::String traceImpl(Event* event, _::PromiseNode* node) {
#if KJ_NO_RTTI
  return heapString("Trace not available because RTTI is disabled.");
#else
  kj::Vector<kj::String> trace;

  if (event != nullptr) {
    trace.add(demangleTypeName(typeid(*event).name()));
  }

  while (node != nullptr) {
    trace.add(demangleTypeName(typeid(*node).name()));
    node = node->getInnerForTrace();
  }

  return strArray(trace, "\n");
#endif
}

kj::String Event::trace() {
  return traceImpl(this, getInnerForTrace());
}

}  // namespace _ (private)

// =======================================================================================

namespace _ {  // private

kj::String PromiseBase::trace() {
  return traceImpl(nullptr, node);
}

void PromiseNode::setSelfPointer(Own<PromiseNode>* selfPtr) noexcept {}

PromiseNode* PromiseNode::getInnerForTrace() { return nullptr; }

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

ImmediateBrokenPromiseNode::ImmediateBrokenPromiseNode(Exception&& exception)
    : exception(kj::mv(exception)) {}

void ImmediateBrokenPromiseNode::get(ExceptionOrValue& output) noexcept {
  output.exception = kj::mv(exception);
}

// -------------------------------------------------------------------

AttachmentPromiseNodeBase::AttachmentPromiseNodeBase(Own<PromiseNode>&& dependencyParam)
    : dependency(kj::mv(dependencyParam)) {
  dependency->setSelfPointer(&dependency);
}

void AttachmentPromiseNodeBase::onReady(Event* event) noexcept {
  dependency->onReady(event);
}

void AttachmentPromiseNodeBase::get(ExceptionOrValue& output) noexcept {
  dependency->get(output);
}

PromiseNode* AttachmentPromiseNodeBase::getInnerForTrace() {
  return dependency;
}

void AttachmentPromiseNodeBase::dropDependency() {
  dependency = nullptr;
}

// -------------------------------------------------------------------

TransformPromiseNodeBase::TransformPromiseNodeBase(
    Own<PromiseNode>&& dependencyParam, void* continuationTracePtr)
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

PromiseNode* TransformPromiseNodeBase::getInnerForTrace() {
  return dependency;
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

ForkBranchBase::ForkBranchBase(Own<ForkHubBase>&& hubParam): hub(kj::mv(hubParam)) {
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

PromiseNode* ForkBranchBase::getInnerForTrace() {
  return hub->getInnerForTrace();
}

// -------------------------------------------------------------------

ForkHubBase::ForkHubBase(Own<PromiseNode>&& innerParam, ExceptionOrValue& resultRef)
    : inner(kj::mv(innerParam)), resultRef(resultRef) {
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

_::PromiseNode* ForkHubBase::getInnerForTrace() {
  return inner;
}

// -------------------------------------------------------------------

ChainPromiseNode::ChainPromiseNode(Own<PromiseNode> innerParam)
    : state(STEP1), inner(kj::mv(innerParam)) {
  inner->setSelfPointer(&inner);
  inner->onReady(this);
}

ChainPromiseNode::~ChainPromiseNode() noexcept(false) {}

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

void ChainPromiseNode::setSelfPointer(Own<PromiseNode>* selfPtr) noexcept {
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

PromiseNode* ChainPromiseNode::getInnerForTrace() {
  return inner;
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
    inner = heap<ImmediateBrokenPromiseNode>(kj::mv(*exception));
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
    return Own<Event>(kj::mv(chain));
  } else {
    inner->setSelfPointer(&inner);
    if (onReadyEvent != nullptr) {
      inner->onReady(onReadyEvent);
    }

    return nullptr;
  }
}

// -------------------------------------------------------------------

ExclusiveJoinPromiseNode::ExclusiveJoinPromiseNode(Own<PromiseNode> left, Own<PromiseNode> right)
    : left(*this, kj::mv(left)), right(*this, kj::mv(right)) {}

ExclusiveJoinPromiseNode::~ExclusiveJoinPromiseNode() noexcept(false) {}

void ExclusiveJoinPromiseNode::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

void ExclusiveJoinPromiseNode::get(ExceptionOrValue& output) noexcept {
  KJ_REQUIRE(left.get(output) || right.get(output), "get() called before ready.");
}

PromiseNode* ExclusiveJoinPromiseNode::getInnerForTrace() {
  auto result = left.getInnerForTrace();
  if (result == nullptr) {
    result = right.getInnerForTrace();
  }
  return result;
}

ExclusiveJoinPromiseNode::Branch::Branch(
    ExclusiveJoinPromiseNode& joinNode, Own<PromiseNode> dependencyParam)
    : joinNode(joinNode), dependency(kj::mv(dependencyParam)) {
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

PromiseNode* ExclusiveJoinPromiseNode::Branch::getInnerForTrace() {
  return dependency;
}

// -------------------------------------------------------------------

ArrayJoinPromiseNodeBase::ArrayJoinPromiseNodeBase(
    Array<Own<PromiseNode>> promises, ExceptionOrValue* resultParts, size_t partSize)
    : countLeft(promises.size()) {
  // Make the branches.
  auto builder = heapArrayBuilder<Branch>(promises.size());
  for (uint i: indices(promises)) {
    ExceptionOrValue& output = *reinterpret_cast<ExceptionOrValue*>(
        reinterpret_cast<byte*>(resultParts) + i * partSize);
    builder.add(*this, kj::mv(promises[i]), output);
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
  // If any of the elements threw exceptions, propagate them.
  for (auto& branch: branches) {
    KJ_IF_MAYBE(exception, branch.getPart()) {
      output.addException(kj::mv(*exception));
    }
  }

  if (output.exception == nullptr) {
    // No errors.  The template subclass will need to fill in the result.
    getNoError(output);
  }
}

PromiseNode* ArrayJoinPromiseNodeBase::getInnerForTrace() {
  return branches.size() == 0 ? nullptr : branches[0].getInnerForTrace();
}

ArrayJoinPromiseNodeBase::Branch::Branch(
    ArrayJoinPromiseNodeBase& joinNode, Own<PromiseNode> dependencyParam, ExceptionOrValue& output)
    : joinNode(joinNode), dependency(kj::mv(dependencyParam)), output(output) {
  dependency->setSelfPointer(&dependency);
  dependency->onReady(this);
}

ArrayJoinPromiseNodeBase::Branch::~Branch() noexcept(false) {}

Maybe<Own<Event>> ArrayJoinPromiseNodeBase::Branch::fire() {
  if (--joinNode.countLeft == 0) {
    joinNode.onReadyEvent.arm();
  }
  return nullptr;
}

_::PromiseNode* ArrayJoinPromiseNodeBase::Branch::getInnerForTrace() {
  return dependency->getInnerForTrace();
}

Maybe<Exception> ArrayJoinPromiseNodeBase::Branch::getPart() {
  dependency->get(output);
  return kj::mv(output.exception);
}

ArrayJoinPromiseNode<void>::ArrayJoinPromiseNode(
    Array<Own<PromiseNode>> promises, Array<ExceptionOr<_::Void>> resultParts)
    : ArrayJoinPromiseNodeBase(kj::mv(promises), resultParts.begin(), sizeof(ExceptionOr<_::Void>)),
      resultParts(kj::mv(resultParts)) {}

ArrayJoinPromiseNode<void>::~ArrayJoinPromiseNode() {}

void ArrayJoinPromiseNode<void>::getNoError(ExceptionOrValue& output) noexcept {
  output.as<_::Void>() = _::Void();
}

}  // namespace _ (private)

Promise<void> joinPromises(Array<Promise<void>>&& promises) {
  return _::PromiseNode::to<Promise<void>>(kj::heap<_::ArrayJoinPromiseNode<void>>(
      KJ_MAP(p, promises) { return _::PromiseNode::from(kj::mv(p)); },
      heapArray<_::ExceptionOr<_::Void>>(promises.size())));
}

namespace _ {  // (private)

// -------------------------------------------------------------------

EagerPromiseNodeBase::EagerPromiseNodeBase(
    Own<PromiseNode>&& dependencyParam, ExceptionOrValue& resultRef)
    : dependency(kj::mv(dependencyParam)), resultRef(resultRef) {
  dependency->setSelfPointer(&dependency);
  dependency->onReady(this);
}

void EagerPromiseNodeBase::onReady(Event* event) noexcept {
  onReadyEvent.init(event);
}

PromiseNode* EagerPromiseNodeBase::getInnerForTrace() {
  return dependency;
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

// -------------------------------------------------------------------

Promise<void> IdentityFunc<Promise<void>>::operator()() const { return READY_NOW; }

}  // namespace _ (private)
}  // namespace kj
