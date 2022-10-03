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

#if !_WIN32

#include "async-unix.h"
#include "debug.h"
#include "threadlocal.h"
#include <setjmp.h>
#include <errno.h>
#include <inttypes.h>
#include <limits>
#include <pthread.h>
#include <map>
#include <sys/wait.h>
#include <unistd.h>

#if KJ_USE_EPOLL
#include <sys/epoll.h>
#include <sys/eventfd.h>
#elif KJ_USE_KQUEUE
#include <sys/event.h>
#include <fcntl.h>
#include <time.h>
#if !__APPLE__ && !__OpenBSD__
// MacOS and OpenBSD are missing this, which means we have to do ugly hacks instead on those.
#define KJ_HAS_SIGTIMEDWAIT 1
#endif
#else
#include <poll.h>
#include <fcntl.h>
#endif

namespace kj {

// =======================================================================================
// Signal code common to multiple implementations

namespace {

int reservedSignal = SIGUSR1;
bool tooLateToSetReserved = false;
bool capturedChildExit = false;

#if !KJ_USE_KQUEUE
bool threadClaimedChildExits = false;
#endif

}  // namespace

#if KJ_USE_EPOLL

namespace {

KJ_THREADLOCAL_PTR(UnixEventPort) threadEventPort = nullptr;
// This is set to the current UnixEventPort just before epoll_pwait(), then back to null after it
// returns.

}  // namespace

void UnixEventPort::signalHandler(int, siginfo_t* siginfo, void*) noexcept {
  // Since this signal handler is *only* called during `epoll_pwait()`, we aren't subject to the
  // usual signal-safety concerns. We can treat this more like a callback. So, we can just call
  // gotSignal() directly, no biggy.

  // Note that, if somehow the signal hanlder is invoked when *not* running `epoll_pwait()`, then
  // `threadEventPort` will be null. We silently ignore the signal in this case. This should never
  // happen in normal execution, so you might argue we should assert-fail instead. However:
  // - We obviously can't throw from here, so we'd have to crash instead.
  // - The Cloudflare Workers runtime relies on this no-op behavior for a certain hack. The hack
  //   in question involves unblocking a signal from the signal mask and relying on it to interrupt
  //   certain blocking syscalls, causing them to fail with EINTR. The hack does not need the
  //   handler to do anything except return in this case. The hacky code makes sure to restore the
  //   signal mask before returning to the event loop.

  UnixEventPort* current = threadEventPort;
  if (current != nullptr) {
    current->gotSignal(*siginfo);
  }
}

#elif KJ_USE_KQUEUE

#if !KJ_HAS_SIGTIMEDWAIT
KJ_THREADLOCAL_PTR(siginfo_t) threadCapture = nullptr;
#endif

void UnixEventPort::signalHandler(int, siginfo_t* siginfo, void*) noexcept {
#if KJ_HAS_SIGTIMEDWAIT
  // This is never called because we use sigtimedwait() to dequeue the signal while it is still
  // blocked, without running the signal handler. However, if we don't register a handler at all,
  // and the default behavior is SIG_IGN, then the signal will be discarded before sigtimedwait()
  // can receive it.
#else
  // When sigtimedwait() isn't available, we use sigsuspend() and wait for the siginfo_t to be
  // delivered to the signal handler.
  siginfo_t* capture = threadCapture;
  if (capture !=  nullptr) {
    *capture = *siginfo;
  }
#endif
}

#else

namespace {

struct SignalCapture {
  sigjmp_buf jumpTo;
  siginfo_t siginfo;

#if __APPLE__
  sigset_t originalMask;
  // The signal mask to be restored when jumping out of the signal handler.
  //
  // "But wait!" you say, "Isn't the whole point of siglongjmp() that it does this for you?" Well,
  // yes, that is supposed to be the point. However, Apple implemented in wrong. On macOS,
  // siglongjmp() uses sigprocmask() -- not pthread_sigmask() -- to restore the signal mask.
  // Unfortunately, sigprocmask() on macOS affects threads other than the current thread. Arguably
  // this is conformant: sigprocmask() is documented as having unspecified behavior in the presence
  // of threads, and pthread_sigmask() must be used instead. However, this means siglongjmp()
  // cannot be used in the presence of threads.
  //
  // We'll just have to restore the signal mask ourselves, rather than rely on siglongjmp()...
  //
  // ... but we ONLY do that on Apple systems, because it turns out, ironically, on Android, this
  // hack breaks signal delivery. pthread_sigmask() vs. sigprocmask() is not the issue; we
  // apparently MUST let siglongjmp() itself deal with the signal mask, otherwise various tests in
  // async-unix-test.c++ end up hanging (I haven't gotten to the bottom of why). Note that on stock
  // Linux, _either_ strategy works fine; this appears to be a problem with Android's Bionic libc.
  // Since letting siglongjmp() do the work _seeems_ more "correct", we'll make it the default and
  // only do something different on Apple platforms.
#define KJ_BROKEN_SIGLONGJMP 1
#endif
};

KJ_THREADLOCAL_PTR(SignalCapture) threadCapture = nullptr;

}  // namespace

void UnixEventPort::signalHandler(int, siginfo_t* siginfo, void*) noexcept {
  SignalCapture* capture = threadCapture;
  if (capture != nullptr) {
    capture->siginfo = *siginfo;

#if KJ_BROKEN_SIGLONGJMP
    // See comments on SignalCapture::originalMask, above: We can't rely on siglongjmp() to restore
    // the signal mask; we must do it ourselves using pthread_sigmask(). We pass false as the
    // second parameter to siglongjmp() so that it skips changing the signal mask. This makes it
    // equivalent to `longjmp()` on Linux or `_longjmp()` on BSD/macOS. See comments on
    // SignalCapture::originalMask for explanation.
    pthread_sigmask(SIG_SETMASK, &capture->originalMask, nullptr);
    siglongjmp(capture->jumpTo, false);
#else
    siglongjmp(capture->jumpTo, true);
#endif
  }
}

#endif  // !KJ_USE_EPOLL && !KJ_USE_KQUEUE

void UnixEventPort::registerSignalHandler(int signum) {
  KJ_REQUIRE(signum != SIGBUS && signum != SIGFPE && signum != SIGILL && signum != SIGSEGV,
      "this signal is raised by erroneous code execution; you cannot capture it into the event "
      "loop");

  tooLateToSetReserved = true;

  // Block the signal from being delivered most of the time. We'll explicitly unblock it when we
  // want to receive it.
  sigset_t mask;
  KJ_SYSCALL(sigemptyset(&mask));
  KJ_SYSCALL(sigaddset(&mask, signum));
  KJ_SYSCALL(pthread_sigmask(SIG_BLOCK, &mask, nullptr));

  // Register the signal handler which should be invoked when we explicitly unblock the signal.
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_sigaction = &signalHandler;
  action.sa_flags = SA_SIGINFO;

  // Set up the signal mask applied while the signal handler runs. We want to block all other
  // signals from being raised during the handler, with the exception of the four "crash" signals,
  // which realistically can't be blocked.
  KJ_SYSCALL(sigfillset(&action.sa_mask));
  KJ_SYSCALL(sigdelset(&action.sa_mask, SIGBUS));
  KJ_SYSCALL(sigdelset(&action.sa_mask, SIGFPE));
  KJ_SYSCALL(sigdelset(&action.sa_mask, SIGILL));
  KJ_SYSCALL(sigdelset(&action.sa_mask, SIGSEGV));

  KJ_SYSCALL(sigaction(signum, &action, nullptr));
}

#if !KJ_USE_EPOLL && !KJ_USE_KQUEUE && !KJ_USE_PIPE_FOR_WAKEUP
void UnixEventPort::registerReservedSignal() {
  registerSignalHandler(reservedSignal);
}
#endif

void UnixEventPort::ignoreSigpipe() {
  // We disable SIGPIPE because users of UnixEventPort almost certainly don't want it.
  //
  // We've observed that when starting many threads at the same time, this can cause some
  // contention on the kernel's signal handler table lock, so we try to run it only once.
  static bool once KJ_UNUSED = []() {
    while (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
      int error = errno;
      if (error != EINTR) {
        KJ_FAIL_SYSCALL("signal(SIGPIPE, SIG_IGN)", error);
      }
    }
    return true;
  }();
}

#if !KJ_USE_KQUEUE  // kqueue systems handle child processes differently

struct UnixEventPort::ChildSet {
  std::map<pid_t, ChildExitPromiseAdapter*> waiters;

  void checkExits();
};

class UnixEventPort::ChildExitPromiseAdapter {
public:
  inline ChildExitPromiseAdapter(PromiseFulfiller<int>& fulfiller,
                                 ChildSet& childSet, Maybe<pid_t>& pidRef)
      : childSet(childSet),
        pid(KJ_REQUIRE_NONNULL(pidRef,
            "`pid` must be non-null at the time `onChildExit()` is called")),
        pidRef(pidRef), fulfiller(fulfiller) {
    KJ_REQUIRE(childSet.waiters.insert(std::make_pair(pid, this)).second,
        "already called onChildExit() for this pid");
  }

  ~ChildExitPromiseAdapter() noexcept(false) {
    childSet.waiters.erase(pid);
  }

  ChildSet& childSet;
  pid_t pid;
  Maybe<pid_t>& pidRef;
  PromiseFulfiller<int>& fulfiller;
};

void UnixEventPort::ChildSet::checkExits() {
  for (;;) {
    int status;
    pid_t pid;
    KJ_SYSCALL_HANDLE_ERRORS(pid = waitpid(-1, &status, WNOHANG)) {
      case ECHILD:
        return;
      default:
        KJ_FAIL_SYSCALL("waitpid()", error);
    }
    if (pid == 0) break;

    auto iter = waiters.find(pid);
    if (iter != waiters.end()) {
      iter->second->pidRef = nullptr;
      iter->second->fulfiller.fulfill(kj::cp(status));
    }
  }
}

Promise<int> UnixEventPort::onChildExit(Maybe<pid_t>& pid) {
  KJ_REQUIRE(capturedChildExit,
      "must call UnixEventPort::captureChildExit() to use onChildExit().");

  ChildSet* cs;
  KJ_IF_MAYBE(c, childSet) {
    cs = *c;
  } else {
    // In theory we should do an atomic compare-and-swap on threadClaimedChildExits, but this is
    // for debug purposes only so it's not a big deal.
    KJ_REQUIRE(!threadClaimedChildExits,
        "only one UnixEvertPort per process may listen for child exits");
    threadClaimedChildExits = true;

    auto newChildSet = kj::heap<ChildSet>();
    cs = newChildSet;
    childSet = kj::mv(newChildSet);
  }

  return kj::newAdaptedPromise<int, ChildExitPromiseAdapter>(*cs, pid);
}

void UnixEventPort::captureChildExit() {
  captureSignal(SIGCHLD);
  capturedChildExit = true;
}

class UnixEventPort::SignalPromiseAdapter {
public:
  inline SignalPromiseAdapter(PromiseFulfiller<siginfo_t>& fulfiller,
                              UnixEventPort& loop, int signum)
      : loop(loop), signum(signum), fulfiller(fulfiller) {
    prev = loop.signalTail;
    *loop.signalTail = this;
    loop.signalTail = &next;
  }

  ~SignalPromiseAdapter() noexcept(false) {
    if (prev != nullptr) {
      if (next == nullptr) {
        loop.signalTail = prev;
      } else {
        next->prev = prev;
      }
      *prev = next;
    }
  }

  SignalPromiseAdapter* removeFromList() {
    auto result = next;
    if (next == nullptr) {
      loop.signalTail = prev;
    } else {
      next->prev = prev;
    }
    *prev = next;
    next = nullptr;
    prev = nullptr;
    return result;
  }

  UnixEventPort& loop;
  int signum;
  PromiseFulfiller<siginfo_t>& fulfiller;
  SignalPromiseAdapter* next = nullptr;
  SignalPromiseAdapter** prev = nullptr;
};

Promise<siginfo_t> UnixEventPort::onSignal(int signum) {
  KJ_REQUIRE(signum != SIGCHLD || !capturedChildExit,
      "can't call onSigal(SIGCHLD) when kj::UnixEventPort::captureChildExit() has been called");
  return newAdaptedPromise<siginfo_t, SignalPromiseAdapter>(*this, signum);
}

#endif  // !KJ_USE_KQUEUE

void UnixEventPort::captureSignal(int signum) {
  if (reservedSignal == SIGUSR1) {
    KJ_REQUIRE(signum != SIGUSR1,
               "Sorry, SIGUSR1 is reserved by the UnixEventPort implementation.  You may call "
               "UnixEventPort::setReservedSignal() to reserve a different signal.");
  } else {
    KJ_REQUIRE(signum != reservedSignal,
               "Can't capture signal reserved using setReservedSignal().", signum);
  }
  registerSignalHandler(signum);
}

void UnixEventPort::setReservedSignal(int signum) {
  KJ_REQUIRE(!tooLateToSetReserved,
             "setReservedSignal() must be called before any calls to `captureSignal()` and "
             "before any `UnixEventPort` is constructed.");
  if (reservedSignal != SIGUSR1 && reservedSignal != signum) {
    KJ_FAIL_REQUIRE("Detected multiple conflicting calls to setReservedSignal().  Please only "
                    "call this once, or always call it with the same signal number.");
  }
  reservedSignal = signum;
}

#if !KJ_USE_KQUEUE

void UnixEventPort::gotSignal(const siginfo_t& siginfo) {
  // If onChildExit() has been called and this is SIGCHLD, check for child exits.
  KJ_IF_MAYBE(cs, childSet) {
    if (siginfo.si_signo == SIGCHLD) {
      cs->get()->checkExits();
      return;
    }
  }

  // Fire any events waiting on this signal.
  auto ptr = signalHead;
  while (ptr != nullptr) {
    if (ptr->signum == siginfo.si_signo) {
      ptr->fulfiller.fulfill(kj::cp(siginfo));
      ptr = ptr->removeFromList();
    } else {
      ptr = ptr->next;
    }
  }
}

#endif  // !KJ_USE_KQUEUE

#if KJ_USE_EPOLL
// =======================================================================================
// epoll FdObserver implementation

UnixEventPort::UnixEventPort()
    : clock(systemPreciseMonotonicClock()),
      timerImpl(clock.now()) {
  ignoreSigpipe();

  int fd;
  KJ_SYSCALL(fd = epoll_create1(EPOLL_CLOEXEC));
  epollFd = AutoCloseFd(fd);

  KJ_SYSCALL(fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
  eventFd = AutoCloseFd(fd);

  struct epoll_event event;
  memset(&event, 0, sizeof(event));
  event.events = EPOLLIN;
  event.data.u64 = 0;
  KJ_SYSCALL(epoll_ctl(epollFd, EPOLL_CTL_ADD, eventFd, &event));

  // Get the current signal mask, from which we'll compute the appropriate mask to pass to
  // epoll_pwait() on each loop. (We explicitly memset to 0 first to make sure we can compare
  // this against another mask with memcmp() for debug purposes.)
  memset(&originalMask, 0, sizeof(originalMask));
  KJ_SYSCALL(sigprocmask(0, nullptr, &originalMask));
}

UnixEventPort::~UnixEventPort() noexcept(false) {
  if (childSet != nullptr) {
    // We had claimed the exclusive right to call onChildExit(). Release that right.
    threadClaimedChildExits = false;
  }
}

UnixEventPort::FdObserver::FdObserver(UnixEventPort& eventPort, int fd, uint flags)
    : eventPort(eventPort), fd(fd), flags(flags) {
  struct epoll_event event;
  memset(&event, 0, sizeof(event));

  if (flags & OBSERVE_READ) {
    event.events |= EPOLLIN | EPOLLRDHUP;
  }
  if (flags & OBSERVE_WRITE) {
    event.events |= EPOLLOUT;
  }
  if (flags & OBSERVE_URGENT) {
    event.events |= EPOLLPRI;
  }
  event.events |= EPOLLET;  // Set edge-triggered mode.

  event.data.ptr = this;

  KJ_SYSCALL(epoll_ctl(eventPort.epollFd, EPOLL_CTL_ADD, fd, &event));
}

UnixEventPort::FdObserver::~FdObserver() noexcept(false) {
  KJ_SYSCALL(epoll_ctl(eventPort.epollFd, EPOLL_CTL_DEL, fd, nullptr)) { break; }
}

void UnixEventPort::FdObserver::fire(short events) {
  if (events & (EPOLLIN | EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
    if (events & (EPOLLHUP | EPOLLRDHUP)) {
      atEnd = true;
    } else {
      // Since we didn't receive EPOLLRDHUP, we know that we're not at the end.
      atEnd = false;
    }

    KJ_IF_MAYBE(f, readFulfiller) {
      f->get()->fulfill();
      readFulfiller = nullptr;
    }
  }

  if (events & (EPOLLOUT | EPOLLHUP | EPOLLERR)) {
    KJ_IF_MAYBE(f, writeFulfiller) {
      f->get()->fulfill();
      writeFulfiller = nullptr;
    }
  }

  if (events & (EPOLLHUP | EPOLLERR)) {
    KJ_IF_MAYBE(f, hupFulfiller) {
      f->get()->fulfill();
      hupFulfiller = nullptr;
    }
  }

  if (events & EPOLLPRI) {
    KJ_IF_MAYBE(f, urgentFulfiller) {
      f->get()->fulfill();
      urgentFulfiller = nullptr;
    }
  }
}

Promise<void> UnixEventPort::FdObserver::whenBecomesReadable() {
  KJ_REQUIRE(flags & OBSERVE_READ, "FdObserver was not set to observe reads.");

  auto paf = newPromiseAndFulfiller<void>();
  readFulfiller = kj::mv(paf.fulfiller);
  return kj::mv(paf.promise);
}

Promise<void> UnixEventPort::FdObserver::whenBecomesWritable() {
  KJ_REQUIRE(flags & OBSERVE_WRITE, "FdObserver was not set to observe writes.");

  auto paf = newPromiseAndFulfiller<void>();
  writeFulfiller = kj::mv(paf.fulfiller);
  return kj::mv(paf.promise);
}

Promise<void> UnixEventPort::FdObserver::whenUrgentDataAvailable() {
  KJ_REQUIRE(flags & OBSERVE_URGENT,
      "FdObserver was not set to observe availability of urgent data.");

  auto paf = newPromiseAndFulfiller<void>();
  urgentFulfiller = kj::mv(paf.fulfiller);
  return kj::mv(paf.promise);
}

Promise<void> UnixEventPort::FdObserver::whenWriteDisconnected() {
  auto paf = newPromiseAndFulfiller<void>();
  hupFulfiller = kj::mv(paf.fulfiller);
  return kj::mv(paf.promise);
}

void UnixEventPort::wake() const {
  uint64_t one = 1;
  ssize_t n;
  KJ_NONBLOCKING_SYSCALL(n = write(eventFd, &one, sizeof(one)));
  KJ_ASSERT(n < 0 || n == sizeof(one));
}

bool UnixEventPort::wait() {
#ifdef KJ_DEBUG
  // In debug mode, verify the current signal mask matches the original.
  {
    sigset_t currentMask;
    memset(&currentMask, 0, sizeof(currentMask));
    KJ_SYSCALL(sigprocmask(0, nullptr, &currentMask));
    if (memcmp(&currentMask, &originalMask, sizeof(currentMask)) != 0) {
      kj::Vector<kj::String> changes;
      for (int i = 0; i <= SIGRTMAX; i++) {
        if (sigismember(&currentMask, i) && !sigismember(&originalMask, i)) {
          changes.add(kj::str("signal #", i, " (", strsignal(i), ") was added"));
        } else if (!sigismember(&currentMask, i) && sigismember(&originalMask, i)) {
          changes.add(kj::str("signal #", i, " (", strsignal(i), ") was removed"));
        }
      }

      KJ_FAIL_REQUIRE(
          "Signal mask has changed since UnixEventPort was constructed. You are required to "
          "ensure that whenever control returns to the event loop, the signal mask is the same "
          "as it was when UnixEventPort was created. In non-debug builds, this check is skipped, "
          "and this situation may instead lead to unexpected results. In particular, while the "
          "system is waiting for I/O events, the signal mask may be reverted to what it was at "
          "construction time, ignoring your subsequent changes.", changes);
    }
  }
#endif

  int timeout = timerImpl.timeoutToNextEvent(clock.now(), MILLISECONDS, int(maxValue))
          .map([](uint64_t t) -> int { return t; })
          .orDefault(-1);

  struct epoll_event events[16];
  int n;
  if (signalHead != nullptr || childSet != nullptr) {
    // We are interested in some signals. Use epoll_pwait().
    //
    // Note: Once upon a time, we used signalfd for this. However, this turned out to be more
    // trouble than it was worth. Some problems with signalfd:
    // - It required opening an additional file descriptor per thread.
    // - If the set of interesting signals changed, the signalfd would have to be updated before
    //   calling epoll_wait(), which was an extra syscall.
    // - When a signal arrives, it requires extra syscalls to read the signal info from the
    //   signalfd, as well as code to translate from signalfd_siginfo to siginfo_t, which are
    //   different for some reason.
    // - signalfd suffers from surprising lock contention during epoll_wait or when the signalfd's
    //   mask is updated in programs with many threads. Because the lock is a spinlock, this
    //   could consume exorbitant CPU.
    // - When a signalfd is in an epoll, it will be flagged readable based on signals which are
    //   pending in the process/thread which called epoll_ctl_add() to register the signalfd.
    //   This is mostly fine for our usage, except that it breaks one useful case that otherwise
    //   works: many servers are designed to "daemonize" themselves by fork()ing and then having
    //   the parent process exit while the child thread lives on. In this case, if a UnixEventPort
    //   had been created before daemonizing, signal handling would be forever broken in the child.

    sigset_t waitMask = originalMask;

    // Unblock the signals we care about.
    {
      auto ptr = signalHead;
      while (ptr != nullptr) {
        KJ_SYSCALL(sigdelset(&waitMask, ptr->signum));
        ptr = ptr->next;
      }
      if (childSet != nullptr) {
        KJ_SYSCALL(sigdelset(&waitMask, SIGCHLD));
      }
    }

    threadEventPort = this;
    n = epoll_pwait(epollFd, events, kj::size(events), timeout, &waitMask);
    threadEventPort = nullptr;
  } else {
    // Not waiting on any signals. Regular epoll_wait() will be fine.
    n = epoll_wait(epollFd, events, kj::size(events), timeout);
  }

  if (n < 0) {
    int error = errno;
    if (error == EINTR) {
      // We received a singal. The signal handler may have queued an event to the event loop. Even
      // if it didn't, we can't simply restart the epoll call because we need to recompute the
      // timeout. Instead, we pretend epoll_wait() returned zero events. This will cause the event
      // loop to spin once, decide it has nothing to do, recompute timeouts, then return to waiting.
      n = 0;
    } else {
      KJ_FAIL_SYSCALL("epoll_pwait()", error);
    }
  }

  return processEpollEvents(events, n);
}

bool UnixEventPort::processEpollEvents(struct epoll_event events[], int n) {
  bool woken = false;

  for (int i = 0; i < n; i++) {
    if (events[i].data.u64 == 0) {
      // Someone called wake() from another thread. Consume the event.
      uint64_t value;
      ssize_t n;
      KJ_NONBLOCKING_SYSCALL(n = read(eventFd, &value, sizeof(value)));
      KJ_ASSERT(n < 0 || n == sizeof(value));

      // We were woken. Need to return true.
      woken = true;
    } else {
      FdObserver* observer = reinterpret_cast<FdObserver*>(events[i].data.ptr);
      observer->fire(events[i].events);
    }
  }

  timerImpl.advanceTo(clock.now());

  return woken;
}

bool UnixEventPort::poll() {
  // Unfortunately, epoll_pwait() with a timeout of zero will never deliver actually deliver any
  // pending signals. Therefore, we need a completely different approach to poll for signals. We
  // might as well use regular epoll_wait() in this case, too, to save the kernel some effort.

  if (signalHead != nullptr || childSet != nullptr) {
    // Use sigtimedwait() to poll for signals.

    // Construct a sigset of all signals we are interested in.
    sigset_t sigset;
    KJ_SYSCALL(sigemptyset(&sigset));
    uint count = 0;

    {
      auto ptr = signalHead;
      while (ptr != nullptr) {
        KJ_SYSCALL(sigaddset(&sigset, ptr->signum));
        ++count;
        ptr = ptr->next;
      }
      if (childSet != nullptr) {
        KJ_SYSCALL(sigaddset(&sigset, SIGCHLD));
        ++count;
      }
    }

    // While that set is non-empty, poll for signals.
    while (count > 0) {
      struct timespec timeout;
      timeout.tv_sec = 0;
      timeout.tv_nsec = 0;

      siginfo_t siginfo;
      int n;
      KJ_NONBLOCKING_SYSCALL(n = sigtimedwait(&sigset, &siginfo, &timeout));
      if (n < 0) break;  // EAGAIN: no signals in set are raised

      KJ_ASSERT(n == siginfo.si_signo);
      gotSignal(siginfo);

      // Remove that signal from the set so we don't receive it again, but keep checking for others
      // if there are any.
      KJ_SYSCALL(sigdelset(&sigset, n));
      --count;
    }
  }

  struct epoll_event events[16];
  int n;
  KJ_SYSCALL(n = epoll_wait(epollFd, events, kj::size(events), 0));

  return processEpollEvents(events, n);
}

#elif KJ_USE_KQUEUE
// =======================================================================================
// kqueue FdObserver implementation

UnixEventPort::UnixEventPort()
    : clock(systemPreciseMonotonicClock()),
      timerImpl(clock.now()) {
  ignoreSigpipe();

  int fd;
  KJ_SYSCALL(fd = kqueue());
  kqueueFd = AutoCloseFd(fd);

  // NetBSD has kqueue1() which can set CLOEXEC atomically, but FreeBSD, MacOS, and others don't
  // have this... oh well.
  KJ_SYSCALL(fcntl(kqueueFd, F_SETFD, FD_CLOEXEC));

  // Register the EVFILT_USER event used by wake().
  struct kevent event;
  EV_SET(&event, 0, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
  KJ_SYSCALL(kevent(kqueueFd, &event, 1, nullptr, 0, nullptr));
}

UnixEventPort::~UnixEventPort() noexcept(false) {}

UnixEventPort::FdObserver::FdObserver(UnixEventPort& eventPort, int fd, uint flags)
    : eventPort(eventPort), fd(fd), flags(flags) {
  struct kevent events[3];
  int nevents = 0;

  if (flags & OBSERVE_URGENT) {
#ifdef EVFILT_EXCEPT
    EV_SET(&events[nevents++], fd, EVFILT_EXCEPT, EV_ADD | EV_CLEAR, NOTE_OOB, 0, this);
#else
    // TODO(someday): Can we support this without reverting to poll()?
    //   Related: https://sandstorm.io/news/2015-04-08-osx-security-bug
    KJ_FAIL_ASSERT("kqueue() on this system doesn't support EVFILT_EXCEPT (for OBSERVE_URGENT). "
        "If you really need to observe OOB events, compile KJ (and your application) with "
        "-DKJ_USE_KQUEUE=0 to disable use of kqueue().");
#endif
  }
  if (flags & OBSERVE_READ) {
    EV_SET(&events[nevents++], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, this);
  }
  if (flags & OBSERVE_WRITE) {
    EV_SET(&events[nevents++], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, this);
  }

  KJ_SYSCALL(kevent(eventPort.kqueueFd, events, nevents, nullptr, 0, nullptr));
}

UnixEventPort::FdObserver::~FdObserver() noexcept(false) {
  struct kevent events[3];
  int nevents = 0;

  if (flags & OBSERVE_URGENT) {
#ifdef EVFILT_EXCEPT
    EV_SET(&events[nevents++], fd, EVFILT_EXCEPT, EV_DELETE, 0, 0, nullptr);
#endif
  }
  if (flags & OBSERVE_READ) {
    EV_SET(&events[nevents++], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
  }
  if ((flags & OBSERVE_WRITE) || hupFulfiller != nullptr) {
    EV_SET(&events[nevents++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
  }

  // TODO(perf): Should we delay unregistration of events until the next time kqueue() is invoked?
  //   We can't delay registrations since it could lead to missed events, but we could delay
  //   unregistration safely. However, we'd have to be very careful about the possibility that
  //   the same FD is re-registered later.
  KJ_SYSCALL_HANDLE_ERRORS(kevent(eventPort.kqueueFd, events, nevents, nullptr, 0, nullptr)) {
    case ENOENT:
      // In the specific case of unnamed pipes, when read end of the pipe is destroyed, FreeBSD
      // seems to unregister the events on the write end automatically. Subsequently trying to
      // remove them then produces ENOENT. Let's ignore this.
      break;
    default:
      KJ_FAIL_SYSCALL("kevent(remove events)", error);
  }
}

void UnixEventPort::FdObserver::fire(struct kevent event) {
  switch (event.filter) {
    case EVFILT_READ:
      if (event.flags & EV_EOF) {
        atEnd = true;
      } else {
        atEnd = false;
      }

      KJ_IF_MAYBE(f, readFulfiller) {
        f->get()->fulfill();
        readFulfiller = nullptr;
      }
      break;

    case EVFILT_WRITE:
      if (event.flags & EV_EOF) {
        // EOF on write indicates disconnect.
        KJ_IF_MAYBE(f, hupFulfiller) {
          f->get()->fulfill();
          hupFulfiller = nullptr;
          if (!(flags & OBSERVE_WRITE)) {
            // We were only observing writes to get the disconnect event. Stop observing now.
            struct kevent rmEvent;
            EV_SET(&rmEvent, fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            KJ_SYSCALL_HANDLE_ERRORS(kevent(eventPort.kqueueFd, &rmEvent, 1, nullptr, 0, nullptr)) {
              case ENOENT:
                // In the specific case of unnamed pipes, when read end of the pipe is destroyed,
                // FreeBSD seems to unregister the events on the write end automatically.
                // Subsequently trying to remove them then produces ENOENT. Let's ignore this.
                break;
              default:
                KJ_FAIL_SYSCALL("kevent(remove events)", error);
            }
          }
        }
      }

      KJ_IF_MAYBE(f, writeFulfiller) {
        f->get()->fulfill();
        writeFulfiller = nullptr;
      }
      break;

#ifdef EVFILT_EXCEPT
    case EVFILT_EXCEPT:
      KJ_IF_MAYBE(f, urgentFulfiller) {
        f->get()->fulfill();
        urgentFulfiller = nullptr;
      }
      break;
#endif
  }
}

Promise<void> UnixEventPort::FdObserver::whenBecomesReadable() {
  KJ_REQUIRE(flags & OBSERVE_READ, "FdObserver was not set to observe reads.");

  auto paf = newPromiseAndFulfiller<void>();
  readFulfiller = kj::mv(paf.fulfiller);
  return kj::mv(paf.promise);
}

Promise<void> UnixEventPort::FdObserver::whenBecomesWritable() {
  KJ_REQUIRE(flags & OBSERVE_WRITE, "FdObserver was not set to observe writes.");

  auto paf = newPromiseAndFulfiller<void>();
  writeFulfiller = kj::mv(paf.fulfiller);
  return kj::mv(paf.promise);
}

Promise<void> UnixEventPort::FdObserver::whenUrgentDataAvailable() {
  KJ_REQUIRE(flags & OBSERVE_URGENT,
      "FdObserver was not set to observe availability of urgent data.");

  auto paf = newPromiseAndFulfiller<void>();
  urgentFulfiller = kj::mv(paf.fulfiller);
  return kj::mv(paf.promise);
}

Promise<void> UnixEventPort::FdObserver::whenWriteDisconnected() {
  if (!(flags & OBSERVE_WRITE) && hupFulfiller == nullptr) {
    // We aren't observing writes, but we need to if we want to detect disconnects.
    struct kevent event;
    EV_SET(&event, fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, this);
    KJ_SYSCALL(kevent(eventPort.kqueueFd, &event, 1, nullptr, 0, nullptr));
  }

  auto paf = newPromiseAndFulfiller<void>();
  hupFulfiller = kj::mv(paf.fulfiller);
  return kj::mv(paf.promise);
}

class UnixEventPort::SignalPromiseAdapter {
public:
  inline SignalPromiseAdapter(PromiseFulfiller<siginfo_t>& fulfiller,
                              UnixEventPort& eventPort, int signum)
      : eventPort(eventPort), signum(signum), fulfiller(fulfiller) {
    struct kevent event;
    EV_SET(&event, signum, EVFILT_SIGNAL, EV_ADD | EV_CLEAR, 0, 0, this);
    KJ_SYSCALL(kevent(eventPort.kqueueFd, &event, 1, nullptr, 0, nullptr));

    // We must check for the signal now in case it was delivered previously and is currently in
    // the blocked set. See comment in tryConsumeSignal(). (To avoid the race condition, we must
    // check *after* having registered the kevent!)
    tryConsumeSignal();
  }

  ~SignalPromiseAdapter() noexcept(false) {
    // Unregister the event. This is important because it contains a pointer to this object which
    // we don't want to see again.
    struct kevent event;
    EV_SET(&event, signum, EVFILT_SIGNAL, EV_DELETE, 0, 0, nullptr);
    KJ_SYSCALL(kevent(eventPort.kqueueFd, &event, 1, nullptr, 0, nullptr));
  }

  void tryConsumeSignal() {
    // Unfortunately KJ's signal semantics are not a great fit for kqueue. In particular, KJ
    // assumes that if no threads are waiting for a signal, it'll remain blocked until some
    // thread actually calls `onSignal()` to receive it. kqueue, however, doesn't care if a signal
    // is blocked -- the kqueue event will still be delivered. So, when `onSignal()` is called
    // we will need to check if the signal is already queued; it's too late to ask kqueue() to
    // tell us this.
    //
    // Alternatively we could maybe fix this by having every thread's kqueue wait on all captured
    // signals all the time, but this would result in a thundering herd on any signal even if only
    // one thread has actually registered interest.
    //
    // Another problem is per-thread signals, delivered with pthread_kill(). On FreeBSD, it appears
    // a pthread_kill will wake up all kqueues in the process waiting on the particular signal,
    // even if they are not associated with the target thread (kqueues don't really have any
    // association with threads anyway). Worse, though, on MacOS, pthread_kill() doesn't wake
    // kqueues at all. In fact, it appears they made it this way in 10.14, which broke stuff:
    // https://github.com/libevent/libevent/issues/765
    //
    // So, we have to:
    // - Block signals normally.
    // - Poll for a specific signal using sigtimedwait() or similar.
    // - Use kqueue only as a hint to tell us when polling might be a good idea.
    // - On MacOS, live with per-thread signals being broken I guess?

    // Anyway, this method here tries to have the signal delivered to this thread.

    if (fulfiller.isWaiting()) {
#if KJ_HAS_SIGTIMEDWAIT
      sigset_t mask;
      KJ_SYSCALL(sigemptyset(&mask));
      KJ_SYSCALL(sigaddset(&mask, signum));
      siginfo_t result;
      struct timespec timeout;
      memset(&timeout, 0, sizeof(timeout));

      KJ_SYSCALL_HANDLE_ERRORS(sigtimedwait(&mask, &result, &timeout)) {
        case EAGAIN:
          // Signal was not queued.
          return;
        default:
          KJ_FAIL_SYSCALL("sigtimedwait", error);
      }

      fulfiller.fulfill(kj::mv(result));
#else
      // This platform doesn't appear to have sigtimedwait(). Ugh! We are forced to do two separate
      // syscalls to see if the signal is pending, and then, if so, wait for it. There is an
      // inherent race condition since the signal could be dequeued in another thread concurrently.
      // We will try to work around that by locking a global mutex, so at least this code doesn't
      // race against itself.
      static pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
      pthread_mutex_lock(&mut);
      KJ_DEFER(pthread_mutex_unlock(&mut));

      sigset_t mask;
      KJ_SYSCALL(sigpending(&mask));
      int isset;
      KJ_SYSCALL(isset = sigismember(&mask, signum));
      if (isset) {
        KJ_SYSCALL(sigfillset(&mask));
        KJ_SYSCALL(sigdelset(&mask, signum));
        siginfo_t info;
        memset(&info, 0, sizeof(info));
        threadCapture = &info;
        KJ_DEFER(threadCapture = nullptr);
        int result = sigsuspend(&mask);
        KJ_ASSERT(result < 0 && errno == EINTR, "sigsuspend() didn't EINTR?", result, errno);
        KJ_ASSERT(info.si_signo == signum);
        fulfiller.fulfill(kj::mv(info));
      }
#endif
    }
  }

  UnixEventPort& eventPort;
  int signum;
  PromiseFulfiller<siginfo_t>& fulfiller;
};

Promise<siginfo_t> UnixEventPort::onSignal(int signum) {
  KJ_REQUIRE(signum != SIGCHLD || !capturedChildExit,
      "can't call onSigal(SIGCHLD) when kj::UnixEventPort::captureChildExit() has been called");

  return newAdaptedPromise<siginfo_t, SignalPromiseAdapter>(*this, signum);
}

class UnixEventPort::ChildExitPromiseAdapter {
public:
  inline ChildExitPromiseAdapter(PromiseFulfiller<int>& fulfiller,
                                 UnixEventPort& eventPort, Maybe<pid_t>& pid)
      : eventPort(eventPort), pid(pid), fulfiller(fulfiller) {
    pid_t p = KJ_ASSERT_NONNULL(pid);

    struct kevent event;
    EV_SET(&event, p, EVFILT_PROC, EV_ADD | EV_CLEAR, NOTE_EXIT, 0, this);
    KJ_SYSCALL(kevent(eventPort.kqueueFd, &event, 1, nullptr, 0, nullptr));

    // Check for race where child had already exited before the event was waiting.
    tryConsumeChild();
  }

  ~ChildExitPromiseAdapter() noexcept(false) {
    KJ_IF_MAYBE(p, pid) {
      // The process has not been reaped. The promise must have been canceled. So, we're still
      // registered with the kqueue. We'd better unregister because the kevent points back to this
      // object.
      struct kevent event;
      EV_SET(&event, *p, EVFILT_PROC, EV_DELETE, 0, 0, nullptr);
      KJ_SYSCALL(kevent(eventPort.kqueueFd, &event, 1, nullptr, 0, nullptr));

      // We leak the zombie process here. The caller is responsible for doing its own waitpid().
    }
  }

  void tryConsumeChild() {
    // Even though kqueue delivers the exit status to us, we still need to wait on the pid to
    // clear the zombie. We can't set SIGCHLD to SIG_IGN to ignore this because it creates a race
    // condition.

    KJ_IF_MAYBE(p, pid) {
      int status;
      pid_t result;
      KJ_SYSCALL(result = waitpid(*p, &status, WNOHANG));
      if (result != 0) {
        KJ_ASSERT(result == *p);

        // NOTE: The proc is automatically unregsitered from the kqueue on exit, so we should NOT
        //   attempt to unregister it here.

        pid = nullptr;
        fulfiller.fulfill(kj::mv(status));
      }
    }
  }

  UnixEventPort& eventPort;
  Maybe<pid_t>& pid;
  PromiseFulfiller<int>& fulfiller;
};

Promise<int> UnixEventPort::onChildExit(Maybe<pid_t>& pid) {
  KJ_REQUIRE(capturedChildExit,
      "must call UnixEventPort::captureChildExit() to use onChildExit().");

  return kj::newAdaptedPromise<int, ChildExitPromiseAdapter>(*this, pid);
}

void UnixEventPort::captureChildExit() {
  capturedChildExit = true;
}

void UnixEventPort::wake() const {
  // Trigger our user event.
  struct kevent event;
  EV_SET(&event, 0, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
  KJ_SYSCALL(kevent(kqueueFd, &event, 1, nullptr, 0, nullptr));
}

bool UnixEventPort::doKqueueWait(struct timespec* timeout) {
  struct kevent events[16];
  int n = kevent(kqueueFd, nullptr, 0, events, kj::size(events), timeout);

  if (n < 0) {
    int error = errno;
    if (error == EINTR) {
      // We received a singal. The signal handler may have queued an event to the event loop. Even
      // if it didn't, we can't simply restart the kevent call because we need to recompute the
      // timeout. Instead, we pretend kevent() returned zero events. This will cause the event
      // loop to spin once, decide it has nothing to do, recompute timeouts, then return to waiting.
      n = 0;
    } else {
      KJ_FAIL_SYSCALL("kevent()", error);
    }
  }

  bool woken = false;

  for (int i = 0; i < n; i++) {
    switch (events[i].filter) {
#ifdef EVFILT_EXCEPT
      case EVFILT_EXCEPT:
#endif
      case EVFILT_READ:
      case EVFILT_WRITE: {
        FdObserver* observer = reinterpret_cast<FdObserver*>(events[i].udata);
        observer->fire(events[i]);
        break;
      }

      case EVFILT_SIGNAL: {
        SignalPromiseAdapter* observer = reinterpret_cast<SignalPromiseAdapter*>(events[i].udata);
        observer->tryConsumeSignal();
        break;
      }

      case EVFILT_PROC: {
        ChildExitPromiseAdapter* observer =
            reinterpret_cast<ChildExitPromiseAdapter*>(events[i].udata);
        observer->tryConsumeChild();
        break;
      }

      case EVFILT_USER:
        // Someone called wake() from another thread.
        woken = true;
        break;

      default:
        KJ_FAIL_ASSERT("unexpected EVFILT", events[i].filter);
    }
  }

  timerImpl.advanceTo(clock.now());

  return woken;
}

bool UnixEventPort::wait() {
  KJ_IF_MAYBE(t, timerImpl.timeoutToNextEvent(clock.now(), NANOSECONDS, int(maxValue))) {
    struct timespec timeout;
    timeout.tv_sec = *t / 1'000'000'000;
    timeout.tv_nsec = *t % 1'000'000'000;
    return doKqueueWait(&timeout);
  } else {
    return doKqueueWait(nullptr);
  }
}

bool UnixEventPort::poll() {
  struct timespec timeout;
  memset(&timeout, 0, sizeof(timeout));
  return doKqueueWait(&timeout);
}

#else  // KJ_USE_EPOLL, else KJ_USE_KQUEUE
// =======================================================================================
// Traditional poll() FdObserver implementation.

#ifndef POLLRDHUP
#define POLLRDHUP 0
#endif

UnixEventPort::UnixEventPort()
    : clock(systemPreciseMonotonicClock()),
      timerImpl(clock.now()) {
#if KJ_USE_PIPE_FOR_WAKEUP
  // Allocate a pipe to which we'll write a byte in order to wake this thread.
  int fds[2];
  KJ_SYSCALL(pipe(fds));
  wakePipeIn = kj::AutoCloseFd(fds[0]);
  wakePipeOut = kj::AutoCloseFd(fds[1]);
  KJ_SYSCALL(fcntl(wakePipeIn, F_SETFD, FD_CLOEXEC));
  KJ_SYSCALL(fcntl(wakePipeOut, F_SETFD, FD_CLOEXEC));
#else
  static_assert(sizeof(threadId) >= sizeof(pthread_t),
                "pthread_t is larger than a long long on your platform.  Please port.");
  *reinterpret_cast<pthread_t*>(&threadId) = pthread_self();

  // Note: We used to use a pthread_once to call registerReservedSignal() only once per process.
  //   This didn't work correctly because registerReservedSignal() not only registers the
  //   (process-wide) signal handler, but also sets the (per-thread) signal mask to block the
  //   signal. Thus, if threads were spawned before the first UnixEventPort was created, and then
  //   multiple threads created UnixEventPorts, only one of them would have the signal properly
  //   blocked. We could have changed things so that only the handler registration was protected
  //   by the pthread_once and the mask update happened in every thread, but registering a signal
  //   handler is not an expensive operation, so whatever... we'll do it in every thread.
  registerReservedSignal();
#endif

  ignoreSigpipe();
}

UnixEventPort::~UnixEventPort() noexcept(false) {}

UnixEventPort::FdObserver::FdObserver(UnixEventPort& eventPort, int fd, uint flags)
    : eventPort(eventPort), fd(fd), flags(flags), next(nullptr), prev(nullptr) {}

UnixEventPort::FdObserver::~FdObserver() noexcept(false) {
  if (prev != nullptr) {
    if (next == nullptr) {
      eventPort.observersTail = prev;
    } else {
      next->prev = prev;
    }
    *prev = next;
  }
}

void UnixEventPort::FdObserver::fire(short events) {
  if (events & (POLLIN | POLLHUP | POLLRDHUP | POLLERR | POLLNVAL)) {
    if (events & (POLLHUP | POLLRDHUP)) {
      atEnd = true;
#if POLLRDHUP
    } else {
      // Since POLLRDHUP exists on this platform, and we didn't receive it, we know that we're not
      // at the end.
      atEnd = false;
#endif
    }

    KJ_IF_MAYBE(f, readFulfiller) {
      f->get()->fulfill();
      readFulfiller = nullptr;
    }
  }

  if (events & (POLLOUT | POLLHUP | POLLERR | POLLNVAL)) {
    KJ_IF_MAYBE(f, writeFulfiller) {
      f->get()->fulfill();
      writeFulfiller = nullptr;
    }
  }

  if (events & (POLLHUP | POLLERR | POLLNVAL)) {
    KJ_IF_MAYBE(f, hupFulfiller) {
      f->get()->fulfill();
      hupFulfiller = nullptr;
    }
  }

  if (events & POLLPRI) {
    KJ_IF_MAYBE(f, urgentFulfiller) {
      f->get()->fulfill();
      urgentFulfiller = nullptr;
    }
  }

  if (readFulfiller == nullptr && writeFulfiller == nullptr && urgentFulfiller == nullptr &&
      hupFulfiller == nullptr) {
    // Remove from list.
    if (next == nullptr) {
      eventPort.observersTail = prev;
    } else {
      next->prev = prev;
    }
    *prev = next;
    next = nullptr;
    prev = nullptr;
  }
}

short UnixEventPort::FdObserver::getEventMask() {
  return (readFulfiller == nullptr ? 0 : (POLLIN | POLLRDHUP)) |
         (writeFulfiller == nullptr ? 0 : POLLOUT) |
         (urgentFulfiller == nullptr ? 0 : POLLPRI) |
         // The POSIX standard says POLLHUP and POLLERR will be reported even if not requested.
         // But on MacOS, if `events` is 0, then POLLHUP apparently will not be reported:
         //   https://openradar.appspot.com/37537852
         // It seems that by settingc any non-zero value -- even one documented as ignored -- we
         // cause POLLHUP to be reported. Both POLLHUP and POLLERR are documented as being ignored.
         // So, we'll go ahead and set them. This has no effect on non-broken OSs, causes MacOS to
         // do the right thing, and sort of looks as if we're explicitly requesting notification of
         // these two conditions, which we do after all want to know about.
         POLLHUP | POLLERR;
}

Promise<void> UnixEventPort::FdObserver::whenBecomesReadable() {
  KJ_REQUIRE(flags & OBSERVE_READ, "FdObserver was not set to observe reads.");

  if (prev == nullptr) {
    KJ_DASSERT(next == nullptr);
    prev = eventPort.observersTail;
    *prev = this;
    eventPort.observersTail = &next;
  }

  auto paf = newPromiseAndFulfiller<void>();
  readFulfiller = kj::mv(paf.fulfiller);
  return kj::mv(paf.promise);
}

Promise<void> UnixEventPort::FdObserver::whenBecomesWritable() {
  KJ_REQUIRE(flags & OBSERVE_WRITE, "FdObserver was not set to observe writes.");

  if (prev == nullptr) {
    KJ_DASSERT(next == nullptr);
    prev = eventPort.observersTail;
    *prev = this;
    eventPort.observersTail = &next;
  }

  auto paf = newPromiseAndFulfiller<void>();
  writeFulfiller = kj::mv(paf.fulfiller);
  return kj::mv(paf.promise);
}

Promise<void> UnixEventPort::FdObserver::whenUrgentDataAvailable() {
  KJ_REQUIRE(flags & OBSERVE_URGENT,
      "FdObserver was not set to observe availability of urgent data.");

  if (prev == nullptr) {
    KJ_DASSERT(next == nullptr);
    prev = eventPort.observersTail;
    *prev = this;
    eventPort.observersTail = &next;
  }

  auto paf = newPromiseAndFulfiller<void>();
  urgentFulfiller = kj::mv(paf.fulfiller);
  return kj::mv(paf.promise);
}

Promise<void> UnixEventPort::FdObserver::whenWriteDisconnected() {
  if (prev == nullptr) {
    KJ_DASSERT(next == nullptr);
    prev = eventPort.observersTail;
    *prev = this;
    eventPort.observersTail = &next;
  }

  auto paf = newPromiseAndFulfiller<void>();
  hupFulfiller = kj::mv(paf.fulfiller);
  return kj::mv(paf.promise);
}

class UnixEventPort::PollContext {
public:
  PollContext(UnixEventPort& port) {
    for (FdObserver* ptr = port.observersHead; ptr != nullptr; ptr = ptr->next) {
      struct pollfd pollfd;
      memset(&pollfd, 0, sizeof(pollfd));
      pollfd.fd = ptr->fd;
      pollfd.events = ptr->getEventMask();
      pollfds.add(pollfd);
      pollEvents.add(ptr);
    }

#if KJ_USE_PIPE_FOR_WAKEUP
    {
      struct pollfd pollfd;
      memset(&pollfd, 0, sizeof(pollfd));
      pollfd.fd = port.wakePipeIn;
      pollfd.events = POLLIN;
      pollfds.add(pollfd);
    }
#endif
  }

  void run(int timeout) {
    pollResult = ::poll(pollfds.begin(), pollfds.size(), timeout);
    pollError = pollResult < 0 ? errno : 0;

    if (pollError == EINTR) {
      // We can't simply restart the poll call because we need to recompute the timeout. Instead,
      // we pretend poll() returned zero events. This will cause the event loop to spin once,
      // decide it has nothing to do, recompute timeouts, then return to waiting.
      pollResult = 0;
      pollError = 0;
    }
  }

  bool processResults() {
    if (pollResult < 0) {
      KJ_FAIL_SYSCALL("poll()", pollError);
    }

    bool woken = false;
    for (auto i: indices(pollfds)) {
      if (pollfds[i].revents != 0) {
#if KJ_USE_PIPE_FOR_WAKEUP
        if (i == pollEvents.size()) {
          // The last pollfd is our cross-thread wake pipe.
          woken = true;
          // Discard junk in the wake pipe.
          char junk[256];
          ssize_t n;
          do {
            KJ_NONBLOCKING_SYSCALL(n = read(pollfds[i].fd, junk, sizeof(junk)));
          } while (n >= 256);
        } else {
#endif
          pollEvents[i]->fire(pollfds[i].revents);
#if KJ_USE_PIPE_FOR_WAKEUP
        }
#endif
        if (--pollResult <= 0) {
          break;
        }
      }
    }
    return woken;
  }

private:
  kj::Vector<struct pollfd> pollfds;
  kj::Vector<FdObserver*> pollEvents;
  int pollResult = 0;
  int pollError = 0;
};

bool UnixEventPort::wait() {
  sigset_t newMask;
  sigemptyset(&newMask);

#if !KJ_USE_PIPE_FOR_WAKEUP
  sigaddset(&newMask, reservedSignal);
#endif

  {
    auto ptr = signalHead;
    while (ptr != nullptr) {
      sigaddset(&newMask, ptr->signum);
      ptr = ptr->next;
    }
    if (childSet != nullptr) {
      sigaddset(&newMask, SIGCHLD);
    }
  }

  PollContext pollContext(*this);

  // Capture signals.
  SignalCapture capture;

#if KJ_BROKEN_SIGLONGJMP
  if (sigsetjmp(capture.jumpTo, false)) {
#else
  if (sigsetjmp(capture.jumpTo, true)) {
#endif
    // We received a signal and longjmp'd back out of the signal handler.
    threadCapture = nullptr;

#if !KJ_USE_PIPE_FOR_WAKEUP
    if (capture.siginfo.si_signo == reservedSignal) {
      return true;
    } else {
#endif
      gotSignal(capture.siginfo);
      return false;
#if !KJ_USE_PIPE_FOR_WAKEUP
    }
#endif
  }

  // Enable signals, run the poll, then mask them again.
#if KJ_BROKEN_SIGLONGJMP
  auto& originalMask = capture.originalMask;
#else
  sigset_t originalMask;
#endif
  threadCapture = &capture;
  pthread_sigmask(SIG_UNBLOCK, &newMask, &originalMask);

  pollContext.run(
      timerImpl.timeoutToNextEvent(clock.now(), MILLISECONDS, int(maxValue))
          .map([](uint64_t t) -> int { return t; })
          .orDefault(-1));

  pthread_sigmask(SIG_SETMASK, &originalMask, nullptr);
  threadCapture = nullptr;

  // Queue events.
  bool result = pollContext.processResults();
  timerImpl.advanceTo(clock.now());

  return result;
}

bool UnixEventPort::poll() {
  // volatile so that longjmp() doesn't clobber it.
  volatile bool woken = false;

  sigset_t pending;
  sigset_t waitMask;
  sigemptyset(&pending);
  sigfillset(&waitMask);

  // Count how many signals that we care about are pending.
  KJ_SYSCALL(sigpending(&pending));
  uint signalCount = 0;

#if !KJ_USE_PIPE_FOR_WAKEUP
  if (sigismember(&pending, reservedSignal)) {
    ++signalCount;
    sigdelset(&pending, reservedSignal);
    sigdelset(&waitMask, reservedSignal);
  }
#endif

  {
    auto ptr = signalHead;
    while (ptr != nullptr) {
      if (sigismember(&pending, ptr->signum)) {
        ++signalCount;
        sigdelset(&pending, ptr->signum);
        sigdelset(&waitMask, ptr->signum);
      }
      ptr = ptr->next;
    }
  }

  // Wait for each pending signal.  It would be nice to use sigtimedwait() here but it is not
  // available on OSX.  :(  Instead, we call sigsuspend() once per expected signal.
  {
    SignalCapture capture;
#if KJ_BROKEN_SIGLONGJMP
    pthread_sigmask(SIG_SETMASK, nullptr, &capture.originalMask);
#endif
    threadCapture = &capture;
    KJ_DEFER(threadCapture = nullptr);
    while (signalCount-- > 0) {
#if KJ_BROKEN_SIGLONGJMP
      if (sigsetjmp(capture.jumpTo, false)) {
#else
      if (sigsetjmp(capture.jumpTo, true)) {
#endif
        // We received a signal and longjmp'd back out of the signal handler.
        sigdelset(&waitMask, capture.siginfo.si_signo);
#if !KJ_USE_PIPE_FOR_WAKEUP
        if (capture.siginfo.si_signo == reservedSignal) {
          woken = true;
        } else {
#endif
          gotSignal(capture.siginfo);
#if !KJ_USE_PIPE_FOR_WAKEUP
        }
#endif
      } else {
#if __CYGWIN__
        // Cygwin's sigpending() incorrectly reports signals pending for any thread, not just our
        // own thread. As a work-around, instead of using sigsuspend() (which would block forever
        // if the signal is not pending on *this* thread), we un-mask the signals and immediately
        // mask them again. If any signals are pending, they *should* be delivered before the first
        // sigprocmask() returns, and the handler will then longjmp() to the block above. If it
        // turns out no signal is pending, we'll block the signals again and break out of the
        // loop.
        //
        // Bug reported here: https://cygwin.com/ml/cygwin/2019-07/msg00051.html
        sigset_t origMask;
        sigprocmask(SIG_SETMASK, &waitMask, &origMask);
        sigprocmask(SIG_SETMASK, &origMask, nullptr);
        break;
#else
        sigsuspend(&waitMask);
        KJ_FAIL_ASSERT("sigsuspend() shouldn't return because the signal handler should "
                      "have siglongjmp()ed.");
#endif
      }
    }
  }

  {
    PollContext pollContext(*this);
    pollContext.run(0);
    if (pollContext.processResults()) {
      woken = true;
    }
  }
  timerImpl.advanceTo(clock.now());

  return woken;
}

void UnixEventPort::wake() const {
#if KJ_USE_PIPE_FOR_WAKEUP
  // We're going to write() a single byte to our wake pipe in order to cause poll() to complete in
  // the target thread.
  //
  // If this write() fails with EWOULDBLOCK, we don't care, because the target thread is already
  // scheduled to wake up.
  char c = 0;
  KJ_NONBLOCKING_SYSCALL(write(wakePipeOut, &c, 1));
#else
  int error = pthread_kill(*reinterpret_cast<const pthread_t*>(&threadId), reservedSignal);
  if (error != 0) {
    KJ_FAIL_SYSCALL("pthread_kill", error);
  }
#endif
}

#endif  // KJ_USE_EPOLL, else KJ_USE_KQUEUE, else

}  // namespace kj

#endif  // !_WIN32
