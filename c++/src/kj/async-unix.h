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
#include <kj/vector.h>
#include <kj/io.h>
#include <signal.h>

KJ_BEGIN_HEADER

#if !defined(KJ_USE_EPOLL) && !defined(KJ_USE_KQUEUE)
#if __linux__
// Default to epoll on Linux.
#define KJ_USE_EPOLL 1
#elif __APPLE__ || __FreeBSD__ || __OpenBSD__ || __NetBSD__ || __DragonFly__
// MacOS and BSDs prefer kqueue() for event notification.
#define KJ_USE_KQUEUE 1
#endif
#endif

#if KJ_USE_EPOLL && KJ_USE_KQUEUE
#error "Both KJ_USE_EPOLL and KJ_USE_KQUEUE are set. Please choose only one of these."
#endif

#if __CYGWIN__ && !defined(KJ_USE_PIPE_FOR_WAKEUP)
// Cygwin has serious issues with the intersection of signals and threads, reported here:
//     https://cygwin.com/ml/cygwin/2019-07/msg00052.html
// On Cygwin, therefore, we do not use signals to wake threads. Instead, each thread allocates a
// pipe, and we write a byte to the pipe to wake the thread... ick.
#define KJ_USE_PIPE_FOR_WAKEUP 1
#endif

#if KJ_USE_EPOLL
struct epoll_event;
#elif KJ_USE_KQUEUE
struct kevent;
struct timespec;
#endif

namespace kj {

class UnixEventPort: public EventPort, private TimerImpl::SleepHooks {
  // An EventPort implementation which can wait for events on file descriptors as well as signals.
  // This API only makes sense on Unix.
  //
  // The implementation uses `poll()` or possibly a platform-specific API (e.g. epoll, kqueue).
  // To also wait on signals without race conditions, the implementation may block signals until
  // just before `poll()` while using a signal handler which `siglongjmp()`s back to just before
  // the signal was unblocked, or it may use a nicer platform-specific API.
  //
  // The implementation reserves a signal for internal use.  By default, it uses SIGUSR1.  If you
  // need to use SIGUSR1 for something else, you must offer a different signal by calling
  // setReservedSignal() at startup. (On Linux, no signal is reserved; eventfd is used instead.)
  //
  // WARNING: A UnixEventPort can only be used in the thread and process that created it. In
  //   particular, note that after a fork(), a UnixEventPort created in the parent process will
  //   not work correctly in the child, even if the parent ceases to use its copy. In particular
  //   note that this means that server processes which daemonize themselves at startup must wait
  //   until after daemonization to create a UnixEventPort.
  //
  // TODO(cleanup): The above warning is no longer accurate -- daemonizing after creating a
  //   UnixEventPort should now work since we no longer use signalfd. But do we want to commit to
  //   keeping it that way? Note it's still unsafe to fork() and then use UnixEventPort from both
  //   processes!

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
  //
  // WARNING: On MacOS and iOS, `onSignal()` will only see process-level signals, NOT
  //   thread-specific signals (i.e. not those sent with pthread_kill()). This is a limitation of
  //   Apple's implemnetation of kqueue() introduced in MacOS 10.14 which Apple says is not a bug.
  //   See: https://github.com/libevent/libevent/issues/765 Consider using kj::Executor or
  //   kj::newPromiseAndCrossThreadFulfiller() for cross-thread communications instead of signals.
  //   If you must have signals, build KJ and your app with `-DKJ_USE_KQUEUE=0`, which will cause
  //   KJ to fall back to a generic poll()-based implementation that is less efficient but handles
  //   thread-specific signals.

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
  // The caller must NOT null out `pid` on its own unless it cancels the Promise first. If the
  // caller decides to cancel the Promise, and `pid` is still non-null after this cancellation,
  // then the caller is expected to `waitpid()` on it BEFORE returning to the event loop again.
  // Probably, the caller should kill() the child before waiting to avoid a hang. If the caller
  // fails to do its own waitpid() before returning to the event loop, the child may become a
  // zombie, or may be reaped automatically, depending on the platform -- since the caller does not
  // know, the caller cannot try to reap the zombie later.
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

#if KJ_USE_EPOLL
  int getPollableFd();
  // Get a file descriptor which represents the EventPort's backing OS event queue, and becomes
  // readable when there are events to process. This may be an epoll FD or a kqueue FD depending on
  // the OS.
  //
  // You MUST use preparePollableFdForSleep() before waiting on this FD.
  //
  // The caller should not perform operations on this FD. It should only be used for polling in
  // some other event loop. This can be useful for allowing UnixEventPort to operate embedded in
  // an application that uses some other event loop as its main loop. Whenever this FD becomes
  // readable, call waitScope.poll() to handle all available events, then
  // `preparePollableFdForSleep()`.
  //
  // It's also possible to use this to drive multiple event loops from the same thread, or even
  // multiple threads in an m:n mapping. When an event loop becomes ready, create a temporary
  // WaitScope on the thread where you want to run it, pump the loop, and then destroy the
  // WaitScope. A WaitScope binds a loop to a thread while it exists, but an event loop is allowed
  // to change threads and a thread is allowed to change event loops as long as no WaitScope is
  // currently binding them.
  //
  // TODO(someday): Currently this is only implemented for epoll, NOT for kqueue. But in principle
  //   it should be possible to implement for kqueue as well.

  void preparePollableFdForSleep();
  // If you plan to monitor the FD return by getPollableFd() for notifications that this queue is ready,
  // you must call preparePollableFdForSleep() after each run of this port's event loop in order to
  // ensure that all event types will in fact wake up the queue.
  //
  // This call is needed in particular to arrange for timer events. Normally, timer events are not
  // implemented via the epoll/kqueue at all, but instead the call to epoll_wait() / kevent() is
  // given a timeout that causes it to return early when the next timer event is scheduled. This
  // doesn't work when the wait is being performed on an external event loop, so instead the
  // implementation must arrange to use timerfd (for epoll) of EVFILT_TIMER (for kqueue) so that
  // the queue FD actually becomes ready when the next timer event is ready to run. This requires
  // some extra syscalls, unfortunately.
  //
  // A second reason this call is needed is to wake up the event queue if any work is queued
  // directly to the event loop via function calls rather than OS events. For example, if anyone
  // calls `fulfill()` on a `PromiseFulfiller`, thus queuing work to this event loop, it needs
  // to wake up. The EventPort achieves this by implicitly calling `wake()` if any work is queued
  // while sleeping -- similar to a cross-thread wakup. NOTE: Queuing work like this while the
  // event loop is asleep is only safe if the EventLoop is still bound to the thread by the
  // existence of a WaitScope. If you are destroying the WaitScope while sleeping, then you cannot
  // manipulate promises attached to this loop at all while it is asleep. (Cross-thread events,
  // e.g. queued via newPromiseAndCrossThreadFulfiller(), or via kj::Executor, are always safe.
  // These will cause the queue fd to become ready so that the loop can wake up and respond to the
  // events.)
  //
  // This method will throw an exception if the port is currently waiting on any signals via
  // onSignal() or onChildExit(). Although it might theoretically be possible to support signals in
  // this mode, deep flaws in the relevant APIs across multiple OSs make it likely not worth
  // attempting.
#endif

  // implements EventPort ------------------------------------------------------
  bool wait() override;
  bool poll() override;
  void wake() const override;

#if KJ_USE_EPOLL
  void setRunnable(bool runnable) override;
#endif

private:
  class SignalPromiseAdapter;
  class ChildExitPromiseAdapter;

  const MonotonicClock& clock;
  TimerImpl timerImpl;

#if !KJ_USE_KQUEUE
  SignalPromiseAdapter* signalHead = nullptr;
  SignalPromiseAdapter** signalTail = &signalHead;

  void gotSignal(const siginfo_t& siginfo);
#endif

  friend class TimerPromiseAdapter;

#if KJ_USE_EPOLL
  sigset_t originalMask;
  AutoCloseFd epollFd;
  AutoCloseFd eventFd;   // Used for cross-thread wakeups.
  kj::Maybe<AutoCloseFd> timerFd;   // Used if preparePollableFdForSleep() is ever called.

  bool sleeping = false;  // Was preparePollableFdForSleep() called?
  bool runnable = false;  // Last value passed to setRunnable().
  bool timerfdIsArmed = false;

  bool processEpollEvents(struct epoll_event events[], int n);
#elif KJ_USE_KQUEUE
  AutoCloseFd kqueueFd;

  bool doKqueueWait(struct timespec* timeout);
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

#if !KJ_USE_KQUEUE
  struct ChildSet;
  Maybe<Own<ChildSet>> childSet;
#endif

  static void signalHandler(int, siginfo_t* siginfo, void*) noexcept;
  static void registerSignalHandler(int signum);
#if !KJ_USE_EPOLL && !KJ_USE_KQUEUE && !KJ_USE_PIPE_FOR_WAKEUP
  static void registerReservedSignal();
#endif
  static void ignoreSigpipe();

  // Implements TimerImpl::SleepHooks.
  void updateNextTimerEvent(kj::Maybe<TimePoint> time) override;
  kj::TimePoint getTimeWhileSleeping() override;
};

class UnixEventPort::FdObserver: private AsyncObject {
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

  KJ_DISALLOW_COPY_AND_MOVE(FdObserver);

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
  //   https://github.com/capnproto/capnproto/issues/374.

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

#if KJ_USE_KQUEUE
  void fire(struct kevent event);
#else
  void fire(short events);
#endif

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
