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

#include "thread.h"
#include "debug.h"

#if _WIN32
#include <windows.h>
#include "windows-sanity.h"
#else
#include <pthread.h>
#include <signal.h>
#include <time.h>
#endif

namespace kj {

#if _WIN32

Thread::Thread(Function<void()> func): state(new ThreadState(kj::mv(func))) {
  threadHandle = CreateThread(nullptr, 0, &runThread, state, 0, nullptr);
  if (threadHandle == nullptr) {
    state->unref();
    KJ_FAIL_ASSERT("CreateThread failed.");
  }
}

Thread::~Thread() noexcept(false) {
  if (!detached) {
    KJ_DEFER(state->unref());

    KJ_ASSERT(WaitForSingleObject(threadHandle, INFINITE) != WAIT_FAILED);

    KJ_IF_SOME(e, state->exception) {
      Exception ecopy = kj::mv(e);
      state->exception = kj::none;  // don't complain of uncaught exception when deleting
      kj::throwRecoverableException(kj::mv(ecopy));
    }
  }
}

void Thread::detach() {
  KJ_ASSERT(CloseHandle(threadHandle));
  detached = true;
}

kj::Duration Thread::getCpuTime() const {
  FILETIME creationTime;
  FILETIME exitTime;
  FILETIME kernelTime;
  FILETIME userTime;

  KJ_WIN32(GetThreadTimes(threadHandle, &creationTime, &exitTime, &kernelTime, &userTime));

  // FILETIME is a 64-bit integer split into two 32-bit pieces, with a base unit of 100ns.
  int64_t utime = (static_cast<uint64_t>(userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;
  int64_t ktime = (static_cast<uint64_t>(kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
  return (utime + ktime) * 100 * kj::NANOSECONDS;
}

#else  // _WIN32

Thread::Thread(Function<void()> func): state(new ThreadState(kj::mv(func))) {
  static_assert(sizeof(threadId) >= sizeof(pthread_t),
                "pthread_t is larger than a long long on your platform.  Please port.");

  int pthreadResult = pthread_create(reinterpret_cast<pthread_t*>(&threadId),
                                     nullptr, &runThread, state);
  if (pthreadResult != 0) {
    state->unref();
    KJ_FAIL_SYSCALL("pthread_create", pthreadResult);
  }
}

Thread::~Thread() noexcept(false) {
  if (!detached) {
    KJ_DEFER(state->unref());

    int pthreadResult = pthread_join(*reinterpret_cast<pthread_t*>(&threadId), nullptr);
    if (pthreadResult != 0) {
      KJ_FAIL_SYSCALL("pthread_join", pthreadResult) { break; }
    }

    KJ_IF_SOME(e, state->exception) {
      Exception ecopy = kj::mv(e);
      state->exception = kj::none;  // don't complain of uncaught exception when deleting
      kj::throwRecoverableException(kj::mv(ecopy));
    }
  }
}

void Thread::sendSignal(int signo) {
  int pthreadResult = pthread_kill(*reinterpret_cast<pthread_t*>(&threadId), signo);
  if (pthreadResult != 0) {
    KJ_FAIL_SYSCALL("pthread_kill", pthreadResult) { break; }
  }
}

void Thread::detach() {
  int pthreadResult = pthread_detach(*reinterpret_cast<pthread_t*>(&threadId));
  if (pthreadResult != 0) {
    KJ_FAIL_SYSCALL("pthread_detach", pthreadResult) { break; }
  }
  detached = true;
  state->unref();
}

#if !__APPLE__  // Mac doesn't implement pthread_getcpuclockid().
kj::Duration Thread::getCpuTime() const {
  clockid_t clockId;
  int pthreadResult = pthread_getcpuclockid(
      *reinterpret_cast<const pthread_t*>(&threadId), &clockId);
  if (pthreadResult != 0) {
    KJ_FAIL_SYSCALL("pthread_getcpuclockid", pthreadResult);
  }

  struct timespec ts;
  KJ_SYSCALL(clock_gettime(clockId, &ts));

  return ts.tv_sec * kj::SECONDS + ts.tv_nsec * kj::NANOSECONDS;
}
#endif  // !__APPLE__

#endif  // _WIN32, else

Thread::ThreadState::ThreadState(Function<void()> func)
    : func(kj::mv(func)),
      initializer(getExceptionCallback().getThreadInitializer()),
      exception(kj::none),
      refcount(2) {}

void Thread::ThreadState::unref() {
#if _MSC_VER && !defined(__clang__)
  if (_InterlockedDecrement(&refcount) == 0) {
#else
  if (__atomic_sub_fetch(&refcount, 1, __ATOMIC_RELEASE) == 0) {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
#endif

    KJ_IF_SOME(e, exception) {
      // If the exception is still present in ThreadState, this must be a detached thread, so
      // the exception will never be rethrown. We should at least log it.
      //
      // We need to run the thread initializer again before we log anything because the main
      // purpose of the thread initializer is to set up a logging callback.
      initializer([&]() {
        KJ_LOG(ERROR, "uncaught exception thrown by detached thread", e);
      });
    }

    delete this;
  }
}

#if _WIN32
DWORD Thread::runThread(void* ptr) {
#else
void* Thread::runThread(void* ptr) {
#endif
  ThreadState* state = reinterpret_cast<ThreadState*>(ptr);
  KJ_IF_SOME(exception, kj::runCatchingExceptions([&]() {
    state->initializer(kj::mv(state->func));
  })) {
    state->exception = kj::mv(exception);
  }
  state->unref();
  return 0;
}

}  // namespace kj
