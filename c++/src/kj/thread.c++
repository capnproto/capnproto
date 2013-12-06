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
