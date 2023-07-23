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
#include <sys/signalfd.h>
#include <sys/eventfd.h>
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
bool threadClaimedChildExits = false;

struct SignalCapture {
  sigjmp_buf jumpTo;
  siginfo_t siginfo;

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
};

#if !KJ_USE_EPOLL  // on Linux we'll use signalfd
KJ_THREADLOCAL_PTR(SignalCapture) threadCapture = nullptr;

void signalHandler(int, siginfo_t* siginfo, void*) {
  SignalCapture* capture = threadCapture;
  if (capture != nullptr) {
    capture->siginfo = *siginfo;

    // See comments on SignalCapture::originalMask, above: We can't rely on siglongjmp() to restore
    // the signal mask; we must do it ourselves using pthread_sigmask(). We pass false as the
    // second parameter to siglongjmp() so that it skips changing the signal mask. This makes it
    // equivalent to `longjmp()` on Linux or `_longjmp()` on BSD/macOS. See comments on
    // SignalCapture::originalMask for explanation.
    pthread_sigmask(SIG_SETMASK, &capture->originalMask, nullptr);
    siglongjmp(capture->jumpTo, false);
  }
}
#endif

void registerSignalHandler(int signum) {
  tooLateToSetReserved = true;

  sigset_t mask;
  KJ_SYSCALL(sigemptyset(&mask));
  KJ_SYSCALL(sigaddset(&mask, signum));
  KJ_SYSCALL(pthread_sigmask(SIG_BLOCK, &mask, nullptr));

#if !KJ_USE_EPOLL  // on Linux we'll use signalfd
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_sigaction = &signalHandler;
  KJ_SYSCALL(sigfillset(&action.sa_mask));
  action.sa_flags = SA_SIGINFO;
  KJ_SYSCALL(sigaction(signum, &action, nullptr));
#endif
}

#if !KJ_USE_EPOLL && !KJ_USE_PIPE_FOR_WAKEUP
void registerReservedSignal() {
  registerSignalHandler(reservedSignal);
}
#endif

void ignoreSigpipe() {
  // We disable SIGPIPE because users of UnixEventPort almost certainly don't want it.
  while (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    int error = errno;
    if (error != EINTR) {
      KJ_FAIL_SYSCALL("signal(SIGPIPE, SIG_IGN)", error);
    }
  }
}

}  // namespace

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

#if KJ_USE_EPOLL
// =======================================================================================
// epoll FdObserver implementation

UnixEventPort::UnixEventPort()
    : clock(systemPreciseMonotonicClock()),
      timerImpl(clock.now()),
      epollFd(-1),
      signalFd(-1),
      eventFd(-1) {
  ignoreSigpipe();

  int fd;
  KJ_SYSCALL(fd = epoll_create1(EPOLL_CLOEXEC));
  epollFd = AutoCloseFd(fd);

  KJ_SYSCALL(sigemptyset(&signalFdSigset));
  KJ_SYSCALL(fd = signalfd(-1, &signalFdSigset, SFD_NONBLOCK | SFD_CLOEXEC));
  signalFd = AutoCloseFd(fd);

  KJ_SYSCALL(fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
  eventFd = AutoCloseFd(fd);


  struct epoll_event event;
  memset(&event, 0, sizeof(event));
  event.events = EPOLLIN;
  event.data.u64 = 0;
  KJ_SYSCALL(epoll_ctl(epollFd, EPOLL_CTL_ADD, signalFd, &event));
  event.data.u64 = 1;
  KJ_SYSCALL(epoll_ctl(epollFd, EPOLL_CTL_ADD, eventFd, &event));
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

bool UnixEventPort::wait() {
  return doEpollWait(
      timerImpl.timeoutToNextEvent(clock.now(), MILLISECONDS, int(maxValue))
          .map([](uint64_t t) -> int { return t; })
          .orDefault(-1));
}

bool UnixEventPort::poll() {
  return doEpollWait(0);
}

void UnixEventPort::wake() const {
  uint64_t one = 1;
  ssize_t n;
  KJ_NONBLOCKING_SYSCALL(n = write(eventFd, &one, sizeof(one)));
  KJ_ASSERT(n < 0 || n == sizeof(one));
}

static siginfo_t toRegularSiginfo(const struct signalfd_siginfo& siginfo) {
  // Unfortunately, siginfo_t is mostly a big union and the correct set of fields to fill in
  // depends on the type of signal. OTOH, signalfd_siginfo is a flat struct that expands all
  // siginfo_t's union fields out to be non-overlapping. We can't just copy all the fields over
  // because of the unions; we have to carefully figure out which fields are appropriate to fill
  // in for this signal. Ick.

  siginfo_t result;
  memset(&result, 0, sizeof(result));

  result.si_signo = siginfo.ssi_signo;
  result.si_errno = siginfo.ssi_errno;
  result.si_code = siginfo.ssi_code;

  if (siginfo.ssi_code > 0) {
    // Signal originated from the kernel. The structure of the siginfo depends primarily on the
    // signal number.

    switch (siginfo.ssi_signo) {
    case SIGCHLD:
      result.si_pid = siginfo.ssi_pid;
      result.si_uid = siginfo.ssi_uid;
      result.si_status = siginfo.ssi_status;
      result.si_utime = siginfo.ssi_utime;
      result.si_stime = siginfo.ssi_stime;
      break;

    case SIGILL:
    case SIGFPE:
    case SIGSEGV:
    case SIGBUS:
    case SIGTRAP:
      result.si_addr = reinterpret_cast<void*>(static_cast<uintptr_t>(siginfo.ssi_addr));
#ifdef si_trapno
      result.si_trapno = siginfo.ssi_trapno;
#endif
#ifdef si_addr_lsb
      // ssi_addr_lsb is defined as coming immediately after ssi_addr in the kernel headers but
      // apparently the userspace headers were never updated. So we do a pointer hack. :(
      result.si_addr_lsb = *reinterpret_cast<const uint16_t*>(&siginfo.ssi_addr + 1);
#endif
      break;

    case SIGIO:
      static_assert(SIGIO == SIGPOLL, "SIGIO != SIGPOLL?");

      // Note: Technically, code can arrange for SIGIO signals to be delivered with a signal number
      //   other than SIGIO. AFAICT there is no way for us to detect this in the siginfo. Luckily
      //   SIGIO is totally obsoleted by epoll so it shouldn't come up.

      result.si_band = siginfo.ssi_band;
      result.si_fd = siginfo.ssi_fd;
      break;

    case SIGSYS:
      // Apparently SIGSYS's fields are not available in signalfd_siginfo?
      break;
    }

  } else {
    // Signal originated from userspace. The sender could specify whatever signal number they
    // wanted. The structure of the signal is determined by the API they used, which is identified
    // by SI_CODE.

    switch (siginfo.ssi_code) {
      case SI_USER:
      case SI_TKILL:
        // kill(), tkill(), or tgkill().
        result.si_pid = siginfo.ssi_pid;
        result.si_uid = siginfo.ssi_uid;
        break;

      case SI_QUEUE:
      case SI_MESGQ:
      case SI_ASYNCIO:
      default:
        result.si_pid = siginfo.ssi_pid;
        result.si_uid = siginfo.ssi_uid;

        // This is awkward. In siginfo_t, si_ptr and si_int are in a union together. In
        // signalfd_siginfo, they are not. We don't really know whether the app intended to send
        // an int or a pointer. Presumably since the pointer is always larger than the int, if
        // we write the pointer, we'll end up with the right value for the int? Presumably the
        // two fields of signalfd_siginfo are actually extracted from one of these unions
        // originally, so actually contain redundant data? Better write some tests...
        //
        // Making matters even stranger, siginfo.ssi_ptr is 64-bit even on 32-bit systems, and
        // it appears that instead of doing the obvious thing by casting the pointer value to
        // 64 bits, the kernel actually memcpy()s the 32-bit value into the 64-bit space. As
        // a result, on big-endian 32-bit systems, the original pointer value ends up in the
        // *upper* 32 bits of siginfo.ssi_ptr, which is totally weird. We play along and use
        // a memcpy() on our end too, to get the right result on all platforms.
        memcpy(&result.si_ptr, &siginfo.ssi_ptr, sizeof(result.si_ptr));
        break;

      case SI_TIMER:
        result.si_timerid = siginfo.ssi_tid;
        result.si_overrun = siginfo.ssi_overrun;

        // Again with this weirdness...
        result.si_ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(siginfo.ssi_ptr));
        break;
    }
  }

  return result;
}

bool UnixEventPort::doEpollWait(int timeout) {
  sigset_t newMask;
  sigemptyset(&newMask);

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

  if (memcmp(&newMask, &signalFdSigset, sizeof(newMask)) != 0) {
    // Apparently we're not waiting on the same signals as last time. Need to update the signal
    // FD's mask.
    signalFdSigset = newMask;
    KJ_SYSCALL(signalfd(signalFd, &signalFdSigset, SFD_NONBLOCK | SFD_CLOEXEC));
  }

  struct epoll_event events[16];
  int n = epoll_wait(epollFd, events, kj::size(events), timeout);
  if (n < 0) {
    int error = errno;
    if (error == EINTR) {
      // We can't simply restart the epoll call because we need to recompute the timeout. Instead,
      // we pretend epoll_wait() returned zero events. This will cause the event loop to spin once,
      // decide it has nothing to do, recompute timeouts, then return to waiting.
      n = 0;
    } else {
      KJ_FAIL_SYSCALL("epoll_wait()", error);
    }
  }

  bool woken = false;

  for (int i = 0; i < n; i++) {
    if (events[i].data.u64 == 0) {
      for (;;) {
        struct signalfd_siginfo siginfo;
        ssize_t n;
        KJ_NONBLOCKING_SYSCALL(n = read(signalFd, &siginfo, sizeof(siginfo)));
        if (n < 0) break;  // no more signals

        KJ_ASSERT(n == sizeof(siginfo));

        gotSignal(toRegularSiginfo(siginfo));

#ifdef SIGRTMIN
        if (siginfo.ssi_signo >= SIGRTMIN) {
          // This is an RT signal. There could be multiple copies queued. We need to remove it from
          // the signalfd's signal mask before we continue, to avoid accidentally reading and
          // discarding the extra copies.
          // TODO(perf): If high throughput of RT signals is desired then perhaps we should read
          //   them all into userspace and queue them here. Maybe we even need a better interface
          //   than onSignal() for receiving high-volume RT signals.
          KJ_SYSCALL(sigdelset(&signalFdSigset, siginfo.ssi_signo));
          KJ_SYSCALL(signalfd(signalFd, &signalFdSigset, SFD_NONBLOCK | SFD_CLOEXEC));
        }
#endif
      }
    } else if (events[i].data.u64 == 1) {
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

#else  // KJ_USE_EPOLL
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

  if (sigsetjmp(capture.jumpTo, false)) {
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
  threadCapture = &capture;
  pthread_sigmask(SIG_UNBLOCK, &newMask, &capture.originalMask);

  pollContext.run(
      timerImpl.timeoutToNextEvent(clock.now(), MILLISECONDS, int(maxValue))
          .map([](uint64_t t) -> int { return t; })
          .orDefault(-1));

  pthread_sigmask(SIG_SETMASK, &capture.originalMask, nullptr);
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
    pthread_sigmask(SIG_SETMASK, nullptr, &capture.originalMask);
    threadCapture = &capture;
    KJ_DEFER(threadCapture = nullptr);
    while (signalCount-- > 0) {
      if (sigsetjmp(capture.jumpTo, false)) {
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
        sigprocmask(SIG_SETMASK, &waitMask, nullptr);
        sigprocmask(SIG_SETMASK, &capture.originalMask, nullptr);
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

#endif  // KJ_USE_EPOLL, else

}  // namespace kj

#endif  // !_WIN32
