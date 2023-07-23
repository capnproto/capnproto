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

#if _WIN32
#error "This file is Unix-specific. On Windows, include async-win32.h instead."
#endif

#include "async.h"
#include "timer.h"
#include "vector.h"
#include "io.h"
#include <signal.h>

KJ_BEGIN_HEADER

#if __linux__ && !__BIONIC__ && !defined(KJ_USE_EPOLL)
// Default to epoll on Linux, except on Bionic (Android) which doesn't have signalfd.h.
#define KJ_USE_EPOLL 1
#endif

#if __CYGWIN__ && !defined(KJ_USE_PIPE_FOR_WAKEUP)
// Cygwin has serious issues with the intersection of signals and threads, reported here:
//     https://cygwin.com/ml/cygwin/2019-07/msg00052.html
// On Cygwin, therefore, we do not use signals to wake threads. Instead, each thread allocates a
// pipe, and we write a byte to the pipe to wake the thread... ick.
#define KJ_USE_PIPE_FOR_WAKEUP 1
#endif

namespace kj {

class UnixEventPort: public EventPort {
  // An EventPort implementation which can wait for events on file descriptors as well as signals.
  // This API only makes sense on Unix.
  //
  // The implementation uses `poll()` or possibly a platform-specific API (e.g. epoll, kqueue).
  // To also wait on signals without race conditions, the implementation may block signals until
  // just before `poll()` while using a signal handler which `siglongjmp()`s back to just before
  // the signal was unblocked, or it may use a nicer platform-specific API like signalfd.
  //
  // The implementation reserves a signal for internal use.  By default, it uses SIGUSR1.  If you
  // need to use SIGUSR1 for something else, you must offer a different signal by calling
  // setReservedSignal() at startup.
  //
  // WARNING: A UnixEventPort can only be used in the thread and process that created it. In
  //   particular, note that after a fork(), a UnixEventPort created in the parent process will
  //   not work correctly in the child, even if the parent ceases to use its copy. In particular
  //   note that this means that server processes which daemonize themselves at startup must wait
  //   until after daemonization to create a UnixEventPort.

public:
  UnixEventPort();
  ~UnixEventPort() noexcept(false);

  class FdObserver;
  // Class that watches an fd for readability or writability. See definition below.

  Promise<siginfo_t> onSignal(int signum);
  // When the given signal is delivered to this thread, return the corresponding siginfo_t.
  // The signal must have been captured using `captureSignal()`.
  //
  // If `onSignal()` has not been called, the signal will remain blocked in this thread.
  // Therefore, a signal which arrives before `onSignal()` was called will not be "missed" -- the
  // next call to 'onSignal()' will receive it.  Also, you can control which thread receives a
  // process-wide signal by only calling `onSignal()` on that thread's event loop.
  //
  // The result of waiting on the same signal twice at once is undefined.

  static void captureSignal(int signum);
  // Arranges for the given signal to be captured and handled via UnixEventPort, so that you may
  // then pass it to `onSignal()`.  This method is static because it registers a signal handler
  // which applies process-wide.  If any other threads exist in the process when `captureSignal()`
  // is called, you *must* set the signal mask in those threads to block this signal, otherwise
  // terrible things will happen if the signal happens to be delivered to those threads.  If at
  // all possible, call `captureSignal()` *before* creating threads, so that threads you create in
  // the future will inherit the proper signal mask.
  //
  // To un-capture a signal, simply install a different signal handler and then un-block it from
  // the signal mask.

  static void setReservedSignal(int signum);
  // Sets the signal number which `UnixEventPort` reserves for internal use.  If your application
  // needs to use SIGUSR1, call this at startup (before any calls to `captureSignal()` and before
  // constructing an `UnixEventPort`) to offer a different signal.

  Timer& getTimer() { return timerImpl; }

  Promise<int> onChildExit(Maybe<pid_t>& pid);
  // When the given child process exits, resolves to its wait status, as returned by wait(2). You
  // will need to use the WIFEXITED() etc. macros to interpret the status code.
  //
  // You must call onChildExit() immediately after the child is created, before returning to the
  // event loop. Otherwise, you may miss the child exit event.
  //
  // `pid` is a reference to a Maybe<pid_t> which must be non-null at the time of the call. When
  // wait() is invoked (and indicates this pid has finished), `pid` will be nulled out. This is
  // necessary to avoid a race condition: as soon as the child has been wait()ed, the PID table
  // entry is freed and can then be reused. So, if you ever want safely to call `kill()` on the
  // PID, it's necessary to know whether it has been wait()ed already. Since the promise's
  // .then() continuation may not run immediately, we need a more precise way, hence we null out
  // the Maybe.
  //
  // You must call `kj::UnixEventPort::captureChildExit()` early in your program if you want to use
  // `onChildExit()`.
  //
  // WARNING: Only one UnixEventPort per process is allowed to use onChildExit(). This is because
  //   child exit is signaled to the process via SIGCHLD, and Unix does not allow the program to
  //   control which thread receives the signal. (We may fix this in the future by automatically
  //   coordinating between threads when multiple threads are expecting child exits.)
  // WARNING 2: If any UnixEventPort in the process is currently waiting for onChildExit(), then
  //   *only* that port's thread can safely wait on child processes, even synchronously. This is
  //   because the thread which used onChildExit() uses wait() to reap children, without specifying
  //   which child, and therefore it may inadvertently reap children created by other threads.

  static void captureChildExit();
  // Arranges for child process exit to be captured and handled via UnixEventPort, so that you may
  // call `onChildExit()`. Much like `captureSignal()`, this static method must be called early on
  // in program startup.
  //
  // This method may capture the `SIGCHLD` signal. You must not use `captureSignal(SIGCHLD)` nor
  // `onSignal(SIGCHLD)` in your own code if you use `captureChildExit()`.

  // implements EventPort ------------------------------------------------------
  bool wait() override;
  bool poll() override;
  void wake() const override;

private:
  class SignalPromiseAdapter;
  class ChildExitPromiseAdapter;

  const MonotonicClock& clock;
  TimerImpl timerImpl;

  SignalPromiseAdapter* signalHead = nullptr;
  SignalPromiseAdapter** signalTail = &signalHead;

  void gotSignal(const siginfo_t& siginfo);

  friend class TimerPromiseAdapter;

#if KJ_USE_EPOLL
  AutoCloseFd epollFd;
  AutoCloseFd signalFd;
  AutoCloseFd eventFd;   // Used for cross-thread wakeups.

  sigset_t signalFdSigset;
  // Signal mask as currently set on the signalFd. Tracked so we can detect whether or not it
  // needs updating.

  bool doEpollWait(int timeout);

#else
  class PollContext;

  FdObserver* observersHead = nullptr;
  FdObserver** observersTail = &observersHead;

#if KJ_USE_PIPE_FOR_WAKEUP
  AutoCloseFd wakePipeIn;
  AutoCloseFd wakePipeOut;
#else
  unsigned long long threadId;  // actually pthread_t
#endif
#endif

  struct ChildSet;
  Maybe<Own<ChildSet>> childSet;
};

class UnixEventPort::FdObserver {
  // Object which watches a file descriptor to determine when it is readable or writable.
  //
  // For listen sockets, "readable" means that there is a connection to accept(). For everything
  // else, it means that read() (or recv()) will return data.
  //
  // The presence of out-of-band data should NOT fire this event. However, the event may
  // occasionally fire spuriously (when there is actually no data to read), and one thing that can
  // cause such spurious events is the arrival of OOB data on certain platforms whose event
  // interfaces fail to distinguish between regular and OOB data (e.g. Mac OSX).
  //
  // WARNING: The exact behavior of this class differs across systems, since event interfaces
  //   vary wildly. Be sure to read the documentation carefully and avoid depending on unspecified
  //   behavior. If at all possible, use the higher-level AsyncInputStream interface instead.

public:
  enum Flags {
    OBSERVE_READ = 1,
    OBSERVE_WRITE = 2,
    OBSERVE_URGENT = 4,
    OBSERVE_READ_WRITE = OBSERVE_READ | OBSERVE_WRITE
  };

  FdObserver(UnixEventPort& eventPort, int fd, uint flags);
  // Begin watching the given file descriptor for readability. Only one ReadObserver may exist
  // for a given file descriptor at a time.

  ~FdObserver() noexcept(false);

  KJ_DISALLOW_COPY(FdObserver);

  Promise<void> whenBecomesReadable();
  // Resolves the next time the file descriptor transitions from having no data to read to having
  // some data to read.
  //
  // KJ uses "edge-triggered" event notification whenever possible. As a result, it is an error
  // to call this method when there is already data in the read buffer which has been there since
  // prior to the last turn of the event loop or prior to creation FdWatcher. In this case, it is
  // unspecified whether the promise will ever resolve -- it depends on the underlying event
  // mechanism being used.
  //
  // In order to avoid this problem, make sure that you only call `whenBecomesReadable()`
  // only at times when you know the buffer is empty. You know this for sure when one of the
  // following happens:
  // * read() or recv() fails with EAGAIN or EWOULDBLOCK. (You MUST have non-blocking mode
  //   enabled on the fd!)
  // * The file descriptor is a regular byte-oriented object (like a socket or pipe),
  //   read() or recv() returns fewer than the number of bytes requested, and `atEndHint()`
  //   returns false. This can only happen if the buffer is empty but EOF is not reached. (Note,
  //   though, that for record-oriented file descriptors like Linux's inotify interface, this
  //   rule does not hold, because it could simply be that the next record did not fit into the
  //   space available.)
  //
  // It is an error to call `whenBecomesReadable()` again when the promise returned previously
  // has not yet resolved. If you do this, the previous promise may throw an exception.

  inline Maybe<bool> atEndHint() { return atEnd; }
  // Returns true if the event system has indicated that EOF has been received. There may still
  // be data in the read buffer, but once that is gone, there's nothing left.
  //
  // Returns false if the event system has indicated that EOF had NOT been received as of the
  // last turn of the event loop.
  //
  // Returns nullptr if the event system does not know whether EOF has been reached. In this
  // case, the only way to know for sure is to call read() or recv() and check if it returns
  // zero.
  //
  // This hint may be useful as an optimization to avoid an unnecessary system call.

  Promise<void> whenBecomesWritable();
  // Resolves the next time the file descriptor transitions from having no space available in the
  // write buffer to having some space available.
  //
  // KJ uses "edge-triggered" event notification whenever possible. As a result, it is an error
  // to call this method when there is already space in the write buffer which has been there
  // since prior to the last turn of the event loop or prior to creation FdWatcher. In this case,
  // it is unspecified whether the promise will ever resolve -- it depends on the underlying
  // event mechanism being used.
  //
  // In order to avoid this problem, make sure that you only call `whenBecomesWritable()`
  // only at times when you know the buffer is full. You know this for sure when one of the
  // following happens:
  // * write() or send() fails with EAGAIN or EWOULDBLOCK. (You MUST have non-blocking mode
  //   enabled on the fd!)
  // * write() or send() succeeds but accepts fewer than the number of bytes provided. This can
  //   only happen if the buffer is full.
  //
  // It is an error to call `whenBecomesWritable()` again when the promise returned previously
  // has not yet resolved. If you do this, the previous promise may throw an exception.

  Promise<void> whenUrgentDataAvailable();
  // Resolves the next time the file descriptor's read buffer contains "urgent" data.
  //
  // The conditions for availability of urgent data are specific to the file descriptor's
  // underlying implementation.
  //
  // It is an error to call `whenUrgentDataAvailable()` again when the promise returned previously
  // has not yet resolved. If you do this, the previous promise may throw an exception.
  //
  // WARNING: This has some known weird behavior on macOS. See
  //   https://github.com/sandstorm-io/capnproto/issues/374.

  Promise<void> whenWriteDisconnected();
  // Resolves when poll() on the file descriptor reports POLLHUP or POLLERR.

private:
  UnixEventPort& eventPort;
  int fd;
  uint flags;

  kj::Maybe<Own<PromiseFulfiller<void>>> readFulfiller;
  kj::Maybe<Own<PromiseFulfiller<void>>> writeFulfiller;
  kj::Maybe<Own<PromiseFulfiller<void>>> urgentFulfiller;
  kj::Maybe<Own<PromiseFulfiller<void>>> hupFulfiller;
  // Replaced each time `whenBecomesReadable()` or `whenBecomesWritable()` is called. Reverted to
  // null every time an event is fired.

  Maybe<bool> atEnd;

  void fire(short events);

#if !KJ_USE_EPOLL
  FdObserver* next;
  FdObserver** prev;
  // Linked list of observers which currently have a non-null readFulfiller or writeFulfiller.
  // If `prev` is null then the observer is not currently in the list.

  short getEventMask();
#endif

  friend class UnixEventPort;
};

}  // namespace kj

KJ_END_HEADER
