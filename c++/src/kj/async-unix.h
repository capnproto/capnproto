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

#ifndef KJ_ASYNC_UNIX_H_
#define KJ_ASYNC_UNIX_H_

#include "async.h"
#include "time.h"
#include "vector.h"
#include <signal.h>
#include <poll.h>
#include <pthread.h>

namespace kj {

class UnixEventPort: public EventPort {
  // THIS INTERFACE IS LIKELY TO CHANGE; consider using only what is defined in async-io.h instead.
  //
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

public:
  UnixEventPort();
  ~UnixEventPort() noexcept(false);

  Promise<short> onFdEvent(int fd, short eventMask);
  // `eventMask` is a bitwise-OR of poll events (e.g. `POLLIN`, `POLLOUT`, etc.).  The next time
  // one or more of the given events occurs on `fd`, the set of events that occurred are returned.

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

  TimePoint steadyTime() { return frozenSteadyTime; }
  Promise<void> atSteadyTime(TimePoint time);

  // implements EventPort ------------------------------------------------------
  void wait() override;
  void poll() override;

private:
  class PollPromiseAdapter;
  class SignalPromiseAdapter;
  class TimerPromiseAdapter;
  class PollContext;

  struct TimerSet;  // Defined in source file to avoid STL include.

  PollPromiseAdapter* pollHead = nullptr;
  PollPromiseAdapter** pollTail = &pollHead;
  SignalPromiseAdapter* signalHead = nullptr;
  SignalPromiseAdapter** signalTail = &signalHead;
  Own<TimerSet> timers;
  TimePoint frozenSteadyTime;

  void gotSignal(const siginfo_t& siginfo);

  TimePoint currentSteadyTime();
  void processTimers();

  friend class TimerPromiseAdapter;
};

}  // namespace kj

#endif  // KJ_ASYNC_UNIX_H_
