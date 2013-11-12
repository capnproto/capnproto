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

#include "async-unix.h"
#include "debug.h"
#include <setjmp.h>
#include <errno.h>

namespace kj {

class NewWorkCallback {
public:
  virtual void receivedNewWork() const = 0;
};

template <typename Item>
class WorkQueue {
  // Item has two methods: fire() and cancel().  Exactly one of these methods will be called,
  // exactly once.  cancel() is called if the application discards the Own<Item> returned by add()
  // before fire() gets a chance to be called.
  //
  // TODO(cleanup):  Make this reusable, use it in EventLoop and TaskSet.  The terminology, e.g.
  //   "item", could be improved.

public:
  class ItemWrapper final: private Disposer {
  public:
    const Item& get() const {
      return item;
    }

    Maybe<const ItemWrapper&> getNext() const {
      // Get the next item in the queue.  Returns nullptr if there are no more items.

      ItemWrapper* nextCopy = __atomic_load_n(&next, __ATOMIC_ACQUIRE);
      if (nextCopy == nullptr) {
        return nullptr;
      } else {
        return nextCopy->skipDead();
      }
    }

    template <typename Param>
    void fire(Param&& param) const {
      // If the listener has not yet dropped its pointer to the Item, invokes the item's `fire()`
      // method with the given parameters.
      //
      // TODO(someday):  This ought to be a variadic template, letting you pass any number of
      //   params.  However, GCC 4.7 and 4.8 appear to get confused by param packs used inside
      //   lambdas.  Clang handles it fine.

      doOnce([&]() {
        const_cast<Item&>(item).fire(kj::fwd<Param>(param));
      });
    }

  private:
    Item item;
    mutable _::Once once;
    mutable uint refcount = 2;

    ItemWrapper* next = nullptr;
    // The ItemWrapper cannot be destroyed until this pointer becomes non-null.

    friend class WorkQueue;

    template <typename... Params>
    ItemWrapper(Params&&... params): item(kj::fwd<Params>(params)...) {}

    void disposeImpl(void* pointer) const override {
      // Called when the listener discards its Own<Item>.
      //
      // If a fire is in-progress in another thread, blocks until it completes.
      //
      // Once both fire() and done() have been called, the item wrapper is deleted.
      //
      // `drop()` is also responsible for calling the item's destructor.

      if (!once.isInitialized()) {
        doOnce([this]() { const_cast<Item&>(item).cancel(); });
      }
      removeRef();
    }

    void removeRef() const {
      if (__atomic_sub_fetch(&refcount, 1, __ATOMIC_ACQ_REL) == 0) {
        delete this;
      }
    }

    template <typename Func>
    void doOnce(Func&& func) const {
      // Execute the given function once.
      //
      // TODO(cleanup):  Perhaps there should be an easier way to do this, without the extra
      //   overhead of Lazy<T>.

      class InitializerImpl: public _::Once::Initializer {
      public:
        InitializerImpl(Func& func): func(func) {}

        void run() override {
          func();
        }

      private:
        Func& func;
      };
      InitializerImpl init(func);
      once.runOnce(init);
    }

    Maybe<const ItemWrapper&> skipDead() const {
      // Find the first ItemWrapper in the list (starting from this one) which isn't already
      // fired.

      const ItemWrapper* current = this;

      while (current->once.isInitialized()) {
        current = __atomic_load_n(&current->next, __ATOMIC_ACQUIRE);
        if (current == nullptr) {
          return nullptr;
        }
      }

      return *current;
    }
  };

  Maybe<const ItemWrapper&> peek(kj::Maybe<NewWorkCallback&> callback) {
    // Get the first item in the list, or null if the list is empty.
    //
    // `callback` will be invoked in the future, if and when new work is added, to let the caller
    // know that it's necessary to peek again.  The callback will be called at most once.  False
    // alarms are possible; you must actually peek() again to find out if there is new work.  If
    // you are sure that you don't need to be notified, pass null for the callback.
    KJ_IF_MAYBE(c, callback) {
      __atomic_store_n(&newWorkCallback, c, __ATOMIC_RELEASE);
    } else {
      __atomic_store_n(&newWorkCallback, nullptr, __ATOMIC_RELEASE);
    }

    ItemWrapper* headCopy = __atomic_load_n(&head, __ATOMIC_ACQUIRE);
    if (headCopy == nullptr) {
      return nullptr;
    } else {
      return headCopy->skipDead();
    }
  }

  void cleanup(uint count = maxValue) {
    // Goes through the list (up to a max of `count` items) and removes any that are no longer
    // relevant.

    ItemWrapper** ptr = &head;

    while (count-- > 0) {
      ItemWrapper* item = __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
      if (item == nullptr) {
        return;
      } else if (item->once.isInitialized()) {
        // This item is no longer useful to us.

        ItemWrapper* next = __atomic_load_n(&item->next, __ATOMIC_ACQUIRE);

        if (next == nullptr) {
          // We can't delete this item yet because another thread may be modifying its `next`
          // pointer.  We're at the end of the list, so we might as well return.
          return;
        } else {
          // Unlink this item from the list.

          // Note that since *ptr was already determined to be non-null, it now belongs to this
          // thread, therefore we don't need to modify it atomically.
          *ptr = next;

          // If the tail pointer points to this item's next pointer (because a race in add() left
          // it behind), be sure to get it updated.  (This is unusual.)
          if (__atomic_load_n(&tail, __ATOMIC_RELAXED) == &item->next) {
            __atomic_store_n(&tail, &next->next, __ATOMIC_RELAXED);
          }

          // Now we can remove our ref to the item.  This may delete it.
          item->removeRef();
        }
      } else {
        // Move on to the next item.
        ptr = &item->next;
      }
    }
  }

  template <typename... Params>
  Own<const Item> add(Params&&... params) const {
    // Adds an item to the list, passing the given parameters to its constructor.

    ItemWrapper* item = new ItemWrapper(kj::fwd<Params>(params)...);
    Own<const Item> result(&item->item, *item);

    ItemWrapper** tailCopy = __atomic_load_n(&tail, __ATOMIC_ACQUIRE);

    ItemWrapper* expected = nullptr;
    while (!__atomic_compare_exchange_n(tailCopy, &expected, item, false,
                                        __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
      // Oops, the tail pointer was out-of-date.  Follow it looking for the real tail.
      tailCopy = &expected->next;
      expected = nullptr;
    }

    // Update tail to point at the end.  Note that this is sloppy:  it's possible that another
    // thread has added another item concurrently and we're now moving the tail pointer backwards,
    // but that's OK because we'll correct for it being behind the next time we use it.
    __atomic_store_n(&tail, &item->next, __ATOMIC_RELEASE);

    if (NewWorkCallback* callback = __atomic_load_n(&newWorkCallback, __ATOMIC_ACQUIRE)) {
      __atomic_store_n(&newWorkCallback, nullptr, __ATOMIC_RELAXED);
      callback->receivedNewWork();
    }

    return kj::mv(result);
  }

private:
  ItemWrapper* head = nullptr;
  // Pointer to the first item.

  mutable ItemWrapper** tail = &head;
  // Usually points to the last `next` pointer in the chain (which should be null, since it's the
  // last one).  However, because `tail` cannot be atomically updated at the same time that `*tail`
  // becomes non-null, `tail` may be behind by a step or two.  In fact, we are sloppy about
  // updating this pointer, so there's a chance it will remain behind indefinitely (if two threads
  // adding items at the same time race to update `tail`), but it should not be too far behind.

  mutable NewWorkCallback* newWorkCallback = nullptr;
};

// =======================================================================================

namespace {

struct SignalCapture {
  sigjmp_buf jumpTo;
  siginfo_t siginfo;
};

__thread SignalCapture* threadCapture = nullptr;

void signalHandler(int, siginfo_t* siginfo, void*) {
  SignalCapture* capture = threadCapture;
  if (capture != nullptr) {
    capture->siginfo = *siginfo;
    siglongjmp(capture->jumpTo, 1);
  }
}

void registerSignalHandler(int signum) {
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, signum);
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_sigaction = &signalHandler;
  sigfillset(&action.sa_mask);
  action.sa_flags = SA_SIGINFO;
  sigaction(signum, &action, nullptr);
}

void registerSigusr1() {
  registerSignalHandler(SIGUSR1);
}

pthread_once_t registerSigusr1Once = PTHREAD_ONCE_INIT;

}  // namespace

// =======================================================================================

struct UnixEventLoop::Impl final: public NewWorkCallback {
  Impl(UnixEventLoop& loop): loop(loop) {}

  UnixEventLoop& loop;

  WorkQueue<PollItem> pollQueue;
  WorkQueue<SignalItem> signalQueue;

  void receivedNewWork() const override {
    loop.wake();
  }
};

class UnixEventLoop::SignalItem {
public:
  inline SignalItem(PromiseFulfiller<siginfo_t>& fulfiller, int signum)
      : fulfiller(fulfiller), signum(signum) {}

  inline void cancel() {}

  void fire(const siginfo_t& siginfo) {
    fulfiller.fulfill(kj::cp(siginfo));
  }

  inline int getSignum() const { return signum; }

private:
  PromiseFulfiller<siginfo_t>& fulfiller;
  int signum;
};

class UnixEventLoop::SignalPromiseAdapter {
public:
  inline SignalPromiseAdapter(PromiseFulfiller<siginfo_t>& fulfiller,
                              const WorkQueue<SignalItem>& signalQueue,
                              int signum)
      : item(signalQueue.add(fulfiller, signum)) {}

private:
  Own<const SignalItem> item;
};

class UnixEventLoop::PollItem {
public:
  inline PollItem(PromiseFulfiller<short>& fulfiller, int fd, short eventMask)
      : fulfiller(fulfiller), fd(fd), eventMask(eventMask) {}

  inline void cancel() {}

  void fire(const struct pollfd& pollfd) {
    fulfiller.fulfill(kj::cp(pollfd.revents));
  }

  inline void prepare(struct pollfd& pollfd) const {
    pollfd.fd = fd;
    pollfd.events = eventMask;
    pollfd.revents = 0;
  }

private:
  PromiseFulfiller<short>& fulfiller;
  int fd;
  short eventMask;
};

class UnixEventLoop::PollPromiseAdapter {
public:
  inline PollPromiseAdapter(PromiseFulfiller<short>& fulfiller,
                            const WorkQueue<PollItem>& pollQueue,
                            int fd, short eventMask)
      : item(pollQueue.add(fulfiller, fd, eventMask)) {}

private:
  Own<const PollItem> item;
};

UnixEventLoop::UnixEventLoop(): impl(heap<Impl>(*this)) {
  pthread_once(&registerSigusr1Once, &registerSigusr1);
}

UnixEventLoop::~UnixEventLoop() {
}

Promise<short> UnixEventLoop::onFdEvent(int fd, short eventMask) const {
  return newAdaptedPromise<short, PollPromiseAdapter>(impl->pollQueue, fd, eventMask);
}

Promise<siginfo_t> UnixEventLoop::onSignal(int signum) const {
  return newAdaptedPromise<siginfo_t, SignalPromiseAdapter>(impl->signalQueue, signum);
}

void UnixEventLoop::captureSignal(int signum) {
  KJ_REQUIRE(signum != SIGUSR1, "Sorry, SIGUSR1 is reserved by the UnixEventLoop implementation.");
  registerSignalHandler(signum);
}

void UnixEventLoop::prepareToSleep() noexcept {
  waitThread = pthread_self();
  __atomic_store_n(&isSleeping, true, __ATOMIC_RELEASE);
}

void UnixEventLoop::sleep() {
  SignalCapture capture;
  threadCapture = &capture;

  if (sigsetjmp(capture.jumpTo, true)) {
    // We received a signal and longjmp'd back out of the signal handler.
    threadCapture = nullptr;
    __atomic_store_n(&isSleeping, false, __ATOMIC_RELAXED);

    if (capture.siginfo.si_signo != SIGUSR1) {
      // Fire any events waiting on this signal.
      auto item = impl->signalQueue.peek(nullptr);
      for (;;) {
        KJ_IF_MAYBE(i, item) {
          if (i->get().getSignum() == capture.siginfo.si_signo) {
            i->fire(capture.siginfo);
          }
          item = i->getNext();
        } else {
          break;
        }
      }
    }

    return;
  }

  // Make sure we don't wait for any events that are no longer relevant, either because we fired
  // them last time around or because the application has discarded the corresponding promises.
  impl->signalQueue.cleanup();
  impl->pollQueue.cleanup();

  sigset_t newMask;
  sigemptyset(&newMask);
  sigaddset(&newMask, SIGUSR1);

  {
    auto item = impl->signalQueue.peek(*impl);
    for (;;) {
      KJ_IF_MAYBE(i, item) {
        sigaddset(&newMask, i->get().getSignum());
        item = i->getNext();
      } else {
        break;
      }
    }
  }

  kj::Vector<struct pollfd> pollfds;
  kj::Vector<const WorkQueue<PollItem>::ItemWrapper*> pollItems;

  {
    auto item = impl->pollQueue.peek(*impl);
    for (;;) {
      KJ_IF_MAYBE(i, item) {
        struct pollfd pollfd;
        memset(&pollfd, 0, sizeof(pollfd));
        i->get().prepare(pollfd);
        pollfds.add(pollfd);
        pollItems.add(i);
        item = i->getNext();
      } else {
        break;
      }
    }
  }

  sigset_t origMask;
  sigprocmask(SIG_UNBLOCK, &newMask, &origMask);

  int pollResult;
  int pollError;
  do {
    pollResult = poll(pollfds.begin(), pollfds.size(), -1);
    pollError = pollResult < 0 ? errno : 0;

    // EINTR should only happen if we received a signal *other than* the ones registered via
    // the UnixEventLoop, so we don't care about that case.
  } while (pollError == EINTR);

  sigprocmask(SIG_SETMASK, &origMask, nullptr);
  threadCapture = nullptr;
  __atomic_store_n(&isSleeping, false, __ATOMIC_RELAXED);

  if (pollResult < 0) {
    KJ_FAIL_SYSCALL("poll()", pollError);
  }

  for (auto i: indices(pollfds)) {
    if (pollfds[i].revents != 0) {
      pollItems[i]->fire(pollfds[i]);
      if (--pollResult <= 0) {
        break;
      }
    }
  }
}

void UnixEventLoop::wake() const {
  // The first load is a fast-path check -- if false, we can avoid a barrier.  If true, then we
  // follow up with an exchange to set it false.  If it turns out we were in fact the one thread
  // to transition the value from true to false, then we go ahead and raise SIGUSR1 on the target
  // thread to wake it up.
  if (__atomic_load_n(&isSleeping, __ATOMIC_RELAXED) &&
      __atomic_exchange_n(&isSleeping, false, __ATOMIC_ACQUIRE)) {
    pthread_kill(waitThread, SIGUSR1);
  }
}

}  // namespace kj
