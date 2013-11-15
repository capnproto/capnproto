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
#include "work-queue.h"
#include <setjmp.h>
#include <errno.h>

namespace kj {

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

struct UnixEventLoop::Impl final: public _::NewJobCallback {
  Impl(UnixEventLoop& loop): loop(loop) {}

  UnixEventLoop& loop;

  _::WorkQueue<PollJob> pollQueue;
  _::WorkQueue<SignalJob> signalQueue;

  void receivedNewJob() const override {
    loop.wake();
  }
};

class UnixEventLoop::SignalJob {
public:
  inline SignalJob(PromiseFulfiller<siginfo_t>& fulfiller, int signum)
      : fulfiller(fulfiller), signum(signum) {}

  inline void cancel() {}

  void complete(const siginfo_t& siginfo) {
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
                              const _::WorkQueue<SignalJob>& signalQueue,
                              int signum)
      : job(signalQueue.createJob(fulfiller, signum)) {
    job->addToQueue();
  }

  ~SignalPromiseAdapter() noexcept(false) {
    job->cancel();
  }

private:
  Own<_::WorkQueue<SignalJob>::JobWrapper> job;
};

class UnixEventLoop::PollJob {
public:
  inline PollJob(PromiseFulfiller<short>& fulfiller, int fd, short eventMask)
      : fulfiller(fulfiller), fd(fd), eventMask(eventMask) {}

  inline void cancel() {}

  void complete(const struct pollfd& pollfd) {
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
                            const _::WorkQueue<PollJob>& pollQueue,
                            int fd, short eventMask)
      : job(pollQueue.createJob(fulfiller, fd, eventMask)) {
    job->addToQueue();
  }

  ~PollPromiseAdapter() noexcept(false) {
    job->cancel();
  }

private:
  Own<_::WorkQueue<PollJob>::JobWrapper> job;
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
      auto job = impl->signalQueue.peek(nullptr);
      for (;;) {
        KJ_IF_MAYBE(j, job) {
          if (j->get().getSignum() == capture.siginfo.si_signo) {
            j->complete(capture.siginfo);
          }
          job = j->getNext();
        } else {
          break;
        }
      }
    }

    return;
  }

  sigset_t newMask;
  sigemptyset(&newMask);
  sigaddset(&newMask, SIGUSR1);

  {
    auto job = impl->signalQueue.peek(*impl);
    for (;;) {
      KJ_IF_MAYBE(j, job) {
        sigaddset(&newMask, j->get().getSignum());
        job = j->getNext();
      } else {
        break;
      }
    }
  }

  kj::Vector<struct pollfd> pollfds;
  kj::Vector<_::WorkQueue<PollJob>::JobWrapper*> pollJobs;

  {
    auto job = impl->pollQueue.peek(*impl);
    for (;;) {
      KJ_IF_MAYBE(j, job) {
        struct pollfd pollfd;
        memset(&pollfd, 0, sizeof(pollfd));
        j->get().prepare(pollfd);
        pollfds.add(pollfd);
        pollJobs.add(j);
        job = j->getNext();
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
      pollJobs[i]->complete(pollfds[i]);
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
