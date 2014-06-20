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
#include <pthread.h>
#include <signal.h>

namespace kj {

Thread::Thread(Function<void()> func): func(kj::mv(func)) {
  static_assert(sizeof(threadId) >= sizeof(pthread_t),
                "pthread_t is larger than a long long on your platform.  Please port.");

  int pthreadResult = pthread_create(reinterpret_cast<pthread_t*>(&threadId),
                                     nullptr, &runThread, this);
  if (pthreadResult != 0) {
    KJ_FAIL_SYSCALL("pthread_create", pthreadResult);
  }
}

Thread::~Thread() noexcept(false) {
  if (!detached) {
    int pthreadResult = pthread_join(*reinterpret_cast<pthread_t*>(&threadId), nullptr);
    if (pthreadResult != 0) {
      KJ_FAIL_SYSCALL("pthread_join", pthreadResult) { break; }
    }

    KJ_IF_MAYBE(e, exception) {
      kj::throwRecoverableException(kj::mv(*e));
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
}

void* Thread::runThread(void* ptr) {
  Thread* thread = reinterpret_cast<Thread*>(ptr);
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    thread->func();
  })) {
    thread->exception = kj::mv(*exception);
  }
  return nullptr;
}

}  // namespace kj
