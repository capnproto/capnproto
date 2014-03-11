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
#include "threadlocal.h"
#include <setjmp.h>
#include <errno.h>

namespace kj {

// =======================================================================================

namespace {

int reservedSignal = SIGUSR1;
bool tooLateToSetReserved = false;

struct SignalCapture {
  sigjmp_buf jumpTo;
  siginfo_t siginfo;
};

KJ_THREADLOCAL_PTR(SignalCapture) threadCapture = nullptr;

void signalHandler(int, siginfo_t* siginfo, void*) {
  SignalCapture* capture = threadCapture;
  if (capture != nullptr) {
    capture->siginfo = *siginfo;
    siglongjmp(capture->jumpTo, 1);
  }
}

void registerSignalHandler(int signum) {
  tooLateToSetReserved = true;

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

void registerReservedSignal() {
  registerSignalHandler(reservedSignal);

  // We also disable SIGPIPE because users of UnixEventLoop almost certainly don't want it.
  signal(SIGPIPE, SIG_IGN);
}

pthread_once_t registerReservedSignalOnce = PTHREAD_ONCE_INIT;

}  // namespace

// =======================================================================================

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

class UnixEventPort::PollPromiseAdapter {
public:
  inline PollPromiseAdapter(PromiseFulfiller<short>& fulfiller,
                            UnixEventPort& loop, int fd, short eventMask)
      : loop(loop), fd(fd), eventMask(eventMask), fulfiller(fulfiller) {
    prev = loop.pollTail;
    *loop.pollTail = this;
    loop.pollTail = &next;
  }

  ~PollPromiseAdapter() noexcept(false) {
    if (prev != nullptr) {
      if (next == nullptr) {
        loop.pollTail = prev;
      } else {
        next->prev = prev;
      }
      *prev = next;
    }
  }

  void removeFromList() {
    if (next == nullptr) {
      loop.pollTail = prev;
    } else {
      next->prev = prev;
    }
    *prev = next;
    next = nullptr;
    prev = nullptr;
  }

  UnixEventPort& loop;
  int fd;
  short eventMask;
  PromiseFulfiller<short>& fulfiller;
  PollPromiseAdapter* next = nullptr;
  PollPromiseAdapter** prev = nullptr;
};

UnixEventPort::UnixEventPort() {
  pthread_once(&registerReservedSignalOnce, &registerReservedSignal);
}

UnixEventPort::~UnixEventPort() {}

Promise<short> UnixEventPort::onFdEvent(int fd, short eventMask) {
  return newAdaptedPromise<short, PollPromiseAdapter>(*this, fd, eventMask);
}

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

class UnixEventPort::PollContext {
public:
  PollContext(PollPromiseAdapter* ptr) {
    while (ptr != nullptr) {
      struct pollfd pollfd;
      memset(&pollfd, 0, sizeof(pollfd));
      pollfd.fd = ptr->fd;
      pollfd.events = ptr->eventMask;
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
        pollEvents[i]->fulfiller.fulfill(kj::mv(pollfds[i].revents));
        pollEvents[i]->removeFromList();
        if (--pollResult <= 0) {
          break;
        }
      }
    }
  }

private:
  kj::Vector<struct pollfd> pollfds;
  kj::Vector<PollPromiseAdapter*> pollEvents;
  int pollResult = 0;
  int pollError = 0;
};

void UnixEventPort::wait() {
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

  PollContext pollContext(pollHead);

  // Capture signals.
  SignalCapture capture;

  if (sigsetjmp(capture.jumpTo, true)) {
    // We received a signal and longjmp'd back out of the signal handler.
    threadCapture = nullptr;

    if (capture.siginfo.si_signo != reservedSignal) {
      gotSignal(capture.siginfo);
    }

    return;
  }

  // Enable signals, run the poll, then mask them again.
  sigset_t origMask;
  threadCapture = &capture;
  sigprocmask(SIG_UNBLOCK, &newMask, &origMask);

  pollContext.run(-1);

  sigprocmask(SIG_SETMASK, &origMask, nullptr);
  threadCapture = nullptr;

  // Queue events.
  pollContext.processResults();
}

void UnixEventPort::poll() {
  sigset_t pending;
  sigset_t waitMask;
  sigemptyset(&pending);
  sigfillset(&waitMask);

  // Count how many signals that we care about are pending.
  KJ_SYSCALL(sigpending(&pending));
  uint signalCount = 0;

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
      gotSignal(capture.siginfo);
    } else {
      sigsuspend(&waitMask);
      KJ_FAIL_ASSERT("sigsuspend() shouldn't return because the signal handler should "
                     "have siglongjmp()ed.");
    }
    threadCapture = nullptr;
  }

  {
    PollContext pollContext(pollHead);
    pollContext.run(0);
    pollContext.processResults();
  }
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

}  // namespace kj
