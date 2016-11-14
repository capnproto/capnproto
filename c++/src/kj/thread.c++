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
#endif

namespace kj {

#if _WIN32

Thread::Thread(Function<void()> func): state(new ThreadState { kj::mv(func), nullptr, 2 }) {
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

    KJ_IF_MAYBE(e, state->exception) {
      Exception ecopy = kj::mv(*e);
      state->exception = nullptr;  // don't complain of uncaught exception when deleting
      kj::throwRecoverableException(kj::mv(ecopy));
    }
  }
}

void Thread::detach() {
  KJ_ASSERT(CloseHandle(threadHandle));
  detached = true;
}

DWORD Thread::runThread(void* ptr) {
  ThreadState* state = reinterpret_cast<ThreadState*>(ptr);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    state->func();
  })) {
    state->exception = kj::mv(*exception);
  }
  state->unref();
  return 0;
}

#else  // _WIN32

Thread::Thread(Function<void()> func): state(new ThreadState { kj::mv(func), nullptr, 2 }) {
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

    KJ_IF_MAYBE(e, state->exception) {
      Exception ecopy = kj::mv(*e);
      state->exception = nullptr;  // don't complain of uncaught exception when deleting
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

void* Thread::runThread(void* ptr) {
  ThreadState* state = reinterpret_cast<ThreadState*>(ptr);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    state->func();
  })) {
    state->exception = kj::mv(*exception);
  }
  state->unref();
  return nullptr;
}

#endif  // _WIN32, else

void Thread::ThreadState::unref() {
#if _MSC_VER
  if (_InterlockedDecrement(&refcount) == 0) {
#else
  if (__atomic_sub_fetch(&refcount, 1, __ATOMIC_RELEASE) == 0) {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
#endif

    KJ_IF_MAYBE(e, exception) {
      KJ_LOG(ERROR, "uncaught exception thrown by detached thread", *e);
    }

    delete this;
  }
}

}  // namespace kj
