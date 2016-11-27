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
#include <chrono>
#include <pthread.h>

#if KJ_USE_EPOLL
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/eventfd.h>
#else
#include <poll.h>
#endif

namespace kj {

// =======================================================================================
// Timer code common to multiple implementations

TimePoint UnixEventPort::readClock() {
  return origin<TimePoint>() + std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count() * NANOSECONDS;
}

// =======================================================================================
// Signal code common to multiple implementations

namespace {

int reservedSignal = SIGUSR1;
bool tooLateToSetReserved = false;

struct SignalCapture {
  sigjmp_buf jumpTo;
  siginfo_t siginfo;
};

#if !KJ_USE_EPOLL  // on Linux we'll use signalfd
KJ_THREADLOCAL_PTR(SignalCapture) threadCapture = nullptr;

void signalHandler(int, siginfo_t* siginfo, void*) {
  SignalCapture* capture = threadCapture;
  if (capture != nullptr) {
    capture->siginfo = *siginfo;
    siglongjmp(capture->jumpTo, 1);
  }
}
#endif

void registerSignalHandler(int signum) {
  tooLateToSetReserved = true;

  sigset_t mask;
  KJ_SYSCALL(sigemptyset(&mask));
  KJ_SYSCALL(sigaddset(&mask, signum));
  KJ_SYSCALL(sigprocmask(SIG_BLOCK, &mask, nullptr));

#if !KJ_USE_EPOLL  // on Linux we'll use signalfd
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_sigaction = &signalHandler;
  KJ_SYSCALL(sigfillset(&action.sa_mask));
  action.sa_flags = SA_SIGINFO;
  KJ_SYSCALL(sigaction(signum, &action, nullptr));
#endif
}

void registerReservedSignal() {
  registerSignalHandler(reservedSignal);

  // We also disable SIGPIPE because users of UnixEventPort almost certainly don't want it.
  while (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    int error = errno;
    if (error != EINTR) {
      KJ_FAIL_SYSCALL("signal(SIGPIPE, SIG_IGN)", error);
    }
  }
}

pthread_once_t registerReservedSignalOnce = PTHREAD_ONCE_INIT;

}  // namespace

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
    : timerImpl(readClock()),
      epollFd(-1),
      signalFd(-1),
      eventFd(-1) {
  pthread_once(&registerReservedSignalOnce, &registerReservedSignal);

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

UnixEventPort::~UnixEventPort() noexcept(false) {}

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

bool UnixEventPort::wait() {
  return doEpollWait(
      timerImpl.timeoutToNextEvent(readClock(), MILLISECONDS, int(maxValue))
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
  }

  if (memcmp(&newMask, &signalFdSigset, sizeof(newMask)) != 0) {
    // Apparently we're not waiting on the same signals as last time. Need to update the signal
    // FD's mask.
    signalFdSigset = newMask;
    KJ_SYSCALL(signalfd(signalFd, &signalFdSigset, SFD_NONBLOCK | SFD_CLOEXEC));
  }

  struct epoll_event events[16];
  int n;
  KJ_SYSCALL(n = epoll_wait(epollFd, events, kj::size(events), timeout));

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

  timerImpl.advanceTo(readClock());

  return woken;
}

#else  // KJ_USE_EPOLL
// =======================================================================================
// Traditional poll() FdObserver implementation.

#ifndef POLLRDHUP
#define POLLRDHUP 0
#endif

UnixEventPort::UnixEventPort()
    : timerImpl(readClock()) {
  static_assert(sizeof(threadId) >= sizeof(pthread_t),
                "pthread_t is larger than a long long on your platform.  Please port.");
  *reinterpret_cast<pthread_t*>(&threadId) = pthread_self();

  pthread_once(&registerReservedSignalOnce, &registerReservedSignal);
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

  if (events & POLLPRI) {
    KJ_IF_MAYBE(f, urgentFulfiller) {
      f->get()->fulfill();
      urgentFulfiller = nullptr;
    }
  }

  if (readFulfiller == nullptr && writeFulfiller == nullptr && urgentFulfiller == nullptr) {
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
         (urgentFulfiller == nullptr ? 0 : POLLPRI);
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

class UnixEventPort::PollContext {
public:
  PollContext(FdObserver* ptr) {
    while (ptr != nullptr) {
      struct pollfd pollfd;
      memset(&pollfd, 0, sizeof(pollfd));
      pollfd.fd = ptr->fd;
      pollfd.events = ptr->getEventMask();
      pollfds.add(pollfd);
      pollEvents.add(ptr);
      ptr = ptr->next;
    }
  }

  void run(int timeout) {
    do {
      pollResult = ::poll(pollfds.begin(), pollfds.size(), timeout);
      pollError = pollResult < 0 ? errno : 0;

      // EINTR should only happen if we received a signal *other than* the ones registered via
      // the UnixEventPort, so we don't care about that case.
    } while (pollError == EINTR);
  }

  void processResults() {
    if (pollResult < 0) {
      KJ_FAIL_SYSCALL("poll()", pollError);
    }

    for (auto i: indices(pollfds)) {
      if (pollfds[i].revents != 0) {
        pollEvents[i]->fire(pollfds[i].revents);
        if (--pollResult <= 0) {
          break;
        }
      }
    }
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
  sigaddset(&newMask, reservedSignal);

  {
    auto ptr = signalHead;
    while (ptr != nullptr) {
      sigaddset(&newMask, ptr->signum);
      ptr = ptr->next;
    }
  }

  PollContext pollContext(observersHead);

  // Capture signals.
  SignalCapture capture;

  if (sigsetjmp(capture.jumpTo, true)) {
    // We received a signal and longjmp'd back out of the signal handler.
    threadCapture = nullptr;

    if (capture.siginfo.si_signo == reservedSignal) {
      return true;
    } else {
      gotSignal(capture.siginfo);
      return false;
    }
  }

  // Enable signals, run the poll, then mask them again.
  sigset_t origMask;
  threadCapture = &capture;
  sigprocmask(SIG_UNBLOCK, &newMask, &origMask);

  pollContext.run(
      timerImpl.timeoutToNextEvent(readClock(), MILLISECONDS, int(maxValue))
          .map([](uint64_t t) -> int { return t; })
          .orDefault(-1));

  sigprocmask(SIG_SETMASK, &origMask, nullptr);
  threadCapture = nullptr;

  // Queue events.
  pollContext.processResults();
  timerImpl.advanceTo(readClock());

  return false;
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

  if (sigismember(&pending, reservedSignal)) {
    ++signalCount;
    sigdelset(&pending, reservedSignal);
    sigdelset(&waitMask, reservedSignal);
  }

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
  while (signalCount-- > 0) {
    SignalCapture capture;
    threadCapture = &capture;
    if (sigsetjmp(capture.jumpTo, true)) {
      // We received a signal and longjmp'd back out of the signal handler.
      sigdelset(&waitMask, capture.siginfo.si_signo);
      if (capture.siginfo.si_signo == reservedSignal) {
        woken = true;
      } else {
        gotSignal(capture.siginfo);
      }
    } else {
      sigsuspend(&waitMask);
      KJ_FAIL_ASSERT("sigsuspend() shouldn't return because the signal handler should "
                     "have siglongjmp()ed.");
    }
    threadCapture = nullptr;
  }

  {
    PollContext pollContext(observersHead);
    pollContext.run(0);
    pollContext.processResults();
  }
  timerImpl.advanceTo(readClock());

  return woken;
}

void UnixEventPort::wake() const {
  int error = pthread_kill(*reinterpret_cast<const pthread_t*>(&threadId), reservedSignal);
  if (error != 0) {
    KJ_FAIL_SYSCALL("pthread_kill", error);
  }
}

#endif  // KJ_USE_EPOLL, else

}  // namespace kj

#endif  // !_WIN32
