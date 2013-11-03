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
#include "thread.h"
#include "debug.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gtest/gtest.h>

namespace kj {

inline void delay() { usleep(10000); }

// On OSX, si_code seems to be zero when SI_USER is expected.
#if __linux__ || __CYGWIN__
#define EXPECT_SI_CODE EXPECT_EQ
#else
#define EXPECT_SI_CODE(a,b)
#endif

class DummyErrorHandler: public TaskSet::ErrorHandler {
public:
  void taskFailed(kj::Exception&& exception) override {
    kj::throwRecoverableException(kj::mv(exception));
  }
};

class AsyncUnixTest: public testing::Test {
public:
  static void SetUpTestCase() {
    UnixEventLoop::captureSignal(SIGUSR2);
    UnixEventLoop::captureSignal(SIGIO);
  }
};

TEST_F(AsyncUnixTest, Signals) {
  UnixEventLoop loop;

  kill(getpid(), SIGUSR2);

  siginfo_t info = loop.wait(loop.onSignal(SIGUSR2));
  EXPECT_EQ(SIGUSR2, info.si_signo);
  EXPECT_SI_CODE(SI_USER, info.si_code);
}

#ifdef SIGRTMIN
TEST_F(AsyncUnixTest, SignalWithValue) {
  // This tests that if we use sigqueue() to attach a value to the signal, that value is received
  // correctly.  Note that this only works on platforms that support real-time signals -- even
  // though the signal we're sending is SIGUSR2, the sigqueue() system call is introduced by RT
  // signals.  Hence this test won't run on e.g. Mac OSX.

  UnixEventLoop loop;

  union sigval value;
  value.sival_int = 123;
  sigqueue(getpid(), SIGUSR2, value);

  siginfo_t info = loop.wait(loop.onSignal(SIGUSR2));
  EXPECT_EQ(SIGUSR2, info.si_signo);
  EXPECT_SI_CODE(SI_QUEUE, info.si_code);
  EXPECT_EQ(123, info.si_value.sival_int);
}
#endif

TEST_F(AsyncUnixTest, SignalsMulti) {
  UnixEventLoop loop;
  DummyErrorHandler dummyHandler;

  TaskSet tasks(loop, dummyHandler);
  tasks.add(loop.onSignal(SIGIO).thenInAnyThread([](siginfo_t&&) {
        ADD_FAILURE() << "Received wrong signal.";
      }));

  kill(getpid(), SIGUSR2);

  siginfo_t info = loop.wait(loop.onSignal(SIGUSR2));
  EXPECT_EQ(SIGUSR2, info.si_signo);
  EXPECT_SI_CODE(SI_USER, info.si_code);
}

TEST_F(AsyncUnixTest, SignalsAsync) {
  // Arrange for another thread to wait on a UnixEventLoop...
  auto exitThread = newPromiseAndFulfiller<void>();
  UnixEventLoop unixLoop;
  Thread thread([&]() {
    unixLoop.wait(kj::mv(exitThread.promise));
  });
  KJ_DEFER(exitThread.fulfiller->fulfill());

  // Arrange to catch a signal in the other thread.  But we haven't sent one yet.
  bool received = false;
  Promise<void> promise = unixLoop.there(unixLoop.onSignal(SIGUSR2),
      [&](siginfo_t&& info) {
    received = true;
    EXPECT_EQ(SIGUSR2, info.si_signo);
    EXPECT_SI_CODE(SI_TKILL, info.si_code);
  });

  delay();

  EXPECT_FALSE(received);

  thread.sendSignal(SIGUSR2);

  SimpleEventLoop mainLoop;
  mainLoop.wait(kj::mv(promise));

  EXPECT_TRUE(received);
}

TEST_F(AsyncUnixTest, Poll) {
  UnixEventLoop loop;

  int pipefds[2];
  KJ_DEFER({ close(pipefds[1]); close(pipefds[0]); });
  KJ_SYSCALL(pipe(pipefds));
  KJ_SYSCALL(write(pipefds[1], "foo", 3));

  EXPECT_EQ(POLLIN, loop.wait(loop.onFdEvent(pipefds[0], POLLIN | POLLPRI)));
}

TEST_F(AsyncUnixTest, PollMulti) {
  UnixEventLoop loop;
  DummyErrorHandler dummyHandler;

  int bogusPipefds[2];
  KJ_SYSCALL(pipe(bogusPipefds));
  KJ_DEFER({ close(bogusPipefds[1]); close(bogusPipefds[0]); });

  TaskSet tasks(loop, dummyHandler);
  tasks.add(loop.onFdEvent(bogusPipefds[0], POLLIN | POLLPRI).thenInAnyThread([](short s) {
        KJ_DBG(s);
        ADD_FAILURE() << "Received wrong poll.";
      }));

  int pipefds[2];
  KJ_SYSCALL(pipe(pipefds));
  KJ_DEFER({ close(pipefds[1]); close(pipefds[0]); });
  KJ_SYSCALL(write(pipefds[1], "foo", 3));

  EXPECT_EQ(POLLIN, loop.wait(loop.onFdEvent(pipefds[0], POLLIN | POLLPRI)));
}

TEST_F(AsyncUnixTest, PollAsync) {
  // Arrange for another thread to wait on a UnixEventLoop...
  auto exitThread = newPromiseAndFulfiller<void>();
  UnixEventLoop unixLoop;
  Thread thread([&]() {
    unixLoop.wait(kj::mv(exitThread.promise));
  });
  KJ_DEFER(exitThread.fulfiller->fulfill());

  // Make a pipe and wait on its read end in another thread.  But don't write to it yet.
  int pipefds[2];
  KJ_DEFER({ close(pipefds[1]); close(pipefds[0]); });
  KJ_SYSCALL(pipe(pipefds));
  bool received = false;
  Promise<void> promise = unixLoop.there(unixLoop.onFdEvent(pipefds[0], POLLIN | POLLPRI),
      [&](short events) {
    received = true;
    EXPECT_EQ(POLLIN, events);
  });

  delay();

  EXPECT_FALSE(received);

  KJ_SYSCALL(write(pipefds[1], "foo", 3));

  SimpleEventLoop mainLoop;
  mainLoop.wait(kj::mv(promise));

  EXPECT_TRUE(received);
}

}  // namespace kj
