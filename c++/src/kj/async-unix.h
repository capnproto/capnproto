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

#ifndef KJ_ASYNC_UNIX_H_
#define KJ_ASYNC_UNIX_H_

#include "async.h"
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
  ~UnixEventPort();

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

  // implements EventPort ------------------------------------------------------
  void wait() override;
  void poll() override;

private:
  class PollPromiseAdapter;
  class SignalPromiseAdapter;
  class PollContext;

  PollPromiseAdapter* pollHead = nullptr;
  PollPromiseAdapter** pollTail = &pollHead;
  SignalPromiseAdapter* signalHead = nullptr;
  SignalPromiseAdapter** signalTail = &signalHead;

  void gotSignal(const siginfo_t& siginfo);
};

}  // namespace kj

#endif  // KJ_ASYNC_UNIX_H_
