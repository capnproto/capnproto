// Copyright (c) 2016 Sandstorm Development Group, Inc. and contributors
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

#include "thread.h"
#include "test.h"
#include "mutex.h"
#include <atomic>

#if _WIN32
#define NOGDI
#include <windows.h>
#undef NOGDI
#else
#include <unistd.h>
#endif

namespace kj {
namespace {

#if _WIN32
inline void delay() { Sleep(10); }
#else
inline void delay() { usleep(10000); }
#endif

KJ_TEST("detaching thread doesn't delete function") {
  struct Functor {
    // Functor that sets *b = true on destruction, not counting moves.

    std::atomic<bool>* destroyed;
    const std::atomic<bool>* canExit;

    Functor(std::atomic<bool>* destroyed, const std::atomic<bool>* canExit)
        : destroyed(destroyed), canExit(canExit) {}

    ~Functor() {
      if (destroyed != nullptr) *destroyed = true;
    }
    KJ_DISALLOW_COPY(Functor);
    Functor(Functor&& other): destroyed(other.destroyed), canExit(other.canExit) {
      other.destroyed = nullptr;
    }
    Functor& operator=(Functor&& other) = delete;

    void operator()() {
      while (!*canExit) delay();
    }
  };

  std::atomic<bool> destroyed(false);
  std::atomic<bool> canExit(false);
  Functor f(&destroyed, &canExit);

  kj::Thread(kj::mv(f)).detach();

  // detach() should not have destroyed the function.
  KJ_ASSERT(!destroyed);

  // Delay a bit to make sure the thread has had time to start up, and then make sure the function
  // still isn't destroyed.
  delay();
  delay();
  KJ_ASSERT(!destroyed);

  // Notify the thread that it's safe to exit.
  canExit = true;
  while (!destroyed) {
    delay();
  }
}

class CapturingExceptionCallback final: public ExceptionCallback {
public:
  CapturingExceptionCallback(String& target): target(target) {}

  void logMessage(LogSeverity severity, const char* file, int line, int contextDepth,
                  String&& text) override {
    target = kj::mv(text);
  }

private:
  String& target;
};

class ThreadedExceptionCallback final: public ExceptionCallback {
public:
  Function<void(Function<void()>)> getThreadInitializer() override {
    return [this](Function<void()> func) {
      CapturingExceptionCallback context(captured);
      func();
    };
  }

  String captured;
};

KJ_TEST("threads pick up exception callback initializer") {
  ThreadedExceptionCallback context;
  KJ_EXPECT(context.captured != "foobar");
  Thread([]() {
    KJ_LOG(ERROR, "foobar");
  });
  KJ_EXPECT(context.captured == "foobar", context.captured);
}

#if !__APPLE__  // Mac doesn't implement pthread_getcpuclockid().
static kj::Duration threadCpuNow() {
#if _WIN32
  FILETIME creationTime;
  FILETIME exitTime;
  FILETIME kernelTime;
  FILETIME userTime;

  KJ_WIN32(GetThreadTimes(GetCurrentThread(), &creationTime, &exitTime, &kernelTime, &userTime));

  // FILETIME is a 64-bit integer split into two 32-bit pieces, with a base unit of 100ns.
  int64_t utime = (static_cast<uint64_t>(userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;
  int64_t ktime = (static_cast<uint64_t>(kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
  return (utime + ktime) * 100 * kj::NANOSECONDS;
#else
  struct timespec ts;
  KJ_SYSCALL(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts));
  return ts.tv_sec * kj::SECONDS + ts.tv_nsec * kj::NANOSECONDS;
#endif
}

KJ_TEST("Thread::getCpuTime()") {
  enum State { INIT, RUNNING, DONE, EXIT };
  MutexGuarded<State> state(INIT);
  auto waitForState = [&](State expected) {
    state.when([&](State s) { return s == expected; }, [](State) {});
  };

  Thread thread([&]() {
    waitForState(RUNNING);
    while (threadCpuNow() < 10 * MILLISECONDS) {}
    *state.lockExclusive() = DONE;
    waitForState(EXIT);
  });

  // CPU time should start at zero.
  KJ_EXPECT(thread.getCpuTime() <= 5 * MILLISECONDS);

  // Signal thread to start running, and wait for it to finish.
  *state.lockExclusive() = RUNNING;
  waitForState(DONE);

  // Now CPU time should reflect that the thread burned 10ms.
  KJ_EXPECT(thread.getCpuTime() >= 10 * MILLISECONDS);

  // The thread should have gone to sleep basically immediately after hitting 10ms but we'll give
  // it a wide margin for error to avoid flakes.
  KJ_EXPECT(thread.getCpuTime() < 100 * MILLISECONDS);

  // Signal the thread to exit. Note that getCpuTime() may fail if called after the thread exit,
  // hence why we had to delay it.
  *state.lockExclusive() = EXIT;
}
#endif  // !__APPLE__

}  // namespace
}  // namespace kj
