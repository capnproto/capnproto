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

#include "async-unix.h"
#include "thread.h"
#include "debug.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gtest/gtest.h>
#include <pthread.h>
#include <algorithm>

namespace kj {

inline void delay() { usleep(10000); }

// On OSX, si_code seems to be zero when SI_USER is expected.
#if __linux__ || __CYGWIN__
#define EXPECT_SI_CODE EXPECT_EQ
#else
#define EXPECT_SI_CODE(a,b)
#endif

class AsyncUnixTest: public testing::Test {
public:
  static void SetUpTestCase() {
    // We use SIGIO and SIGURG as our test signals because they're two signals that we can be
    // reasonably confident won't otherwise be delivered to any KJ or Cap'n Proto test.  We can't
    // use SIGUSR1 because it is reserved by UnixEventPort and SIGUSR2 is used by Valgrind on OSX.
    UnixEventPort::captureSignal(SIGURG);
    UnixEventPort::captureSignal(SIGIO);
  }
};

TEST_F(AsyncUnixTest, Signals) {
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  kill(getpid(), SIGURG);

  siginfo_t info = port.onSignal(SIGURG).wait(waitScope);
  EXPECT_EQ(SIGURG, info.si_signo);
  EXPECT_SI_CODE(SI_USER, info.si_code);
}

#ifdef SIGRTMIN
TEST_F(AsyncUnixTest, SignalWithValue) {
  // This tests that if we use sigqueue() to attach a value to the signal, that value is received
  // correctly.  Note that this only works on platforms that support real-time signals -- even
  // though the signal we're sending is SIGURG, the sigqueue() system call is introduced by RT
  // signals.  Hence this test won't run on e.g. Mac OSX.

  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  union sigval value;
  memset(&value, 0, sizeof(value));
  value.sival_int = 123;
  sigqueue(getpid(), SIGURG, value);

  siginfo_t info = port.onSignal(SIGURG).wait(waitScope);
  EXPECT_EQ(SIGURG, info.si_signo);
  EXPECT_SI_CODE(SI_QUEUE, info.si_code);
  EXPECT_EQ(123, info.si_value.sival_int);
}
#endif

TEST_F(AsyncUnixTest, SignalsMultiListen) {
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  port.onSignal(SIGIO).then([](siginfo_t&&) {
    ADD_FAILURE() << "Received wrong signal.";
  }).detach([](kj::Exception&& exception) {
    ADD_FAILURE() << kj::str(exception).cStr();
  });

  kill(getpid(), SIGURG);

  siginfo_t info = port.onSignal(SIGURG).wait(waitScope);
  EXPECT_EQ(SIGURG, info.si_signo);
  EXPECT_SI_CODE(SI_USER, info.si_code);
}

#if !__CYGWIN32__
// Cygwin32 (but not Cygwin64) appears not to deliver SIGURG in the following test (but it does
// deliver SIGIO, if you reverse the order of the waits).  Since this doesn't occur on any other
// platform I'm assuming it's a Cygwin bug.

TEST_F(AsyncUnixTest, SignalsMultiReceive) {
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  kill(getpid(), SIGURG);
  kill(getpid(), SIGIO);

  siginfo_t info = port.onSignal(SIGURG).wait(waitScope);
  EXPECT_EQ(SIGURG, info.si_signo);
  EXPECT_SI_CODE(SI_USER, info.si_code);

  info = port.onSignal(SIGIO).wait(waitScope);
  EXPECT_EQ(SIGIO, info.si_signo);
  EXPECT_SI_CODE(SI_USER, info.si_code);
}

#endif  // !__CYGWIN32__

TEST_F(AsyncUnixTest, SignalsAsync) {
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  // Arrange for a signal to be sent from another thread.
  pthread_t mainThread = pthread_self();
  Thread thread([&]() {
    delay();
    pthread_kill(mainThread, SIGURG);
  });

  siginfo_t info = port.onSignal(SIGURG).wait(waitScope);
  EXPECT_EQ(SIGURG, info.si_signo);
#if __linux__
  EXPECT_SI_CODE(SI_TKILL, info.si_code);
#endif
}

#if !__CYGWIN32__
// Cygwin32 (but not Cygwin64) appears not to deliver SIGURG in the following test (but it does
// deliver SIGIO, if you reverse the order of the waits).  Since this doesn't occur on any other
// platform I'm assuming it's a Cygwin bug.

TEST_F(AsyncUnixTest, SignalsNoWait) {
  // Verify that UnixEventPort::poll() correctly receives pending signals.

  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  bool receivedSigurg = false;
  bool receivedSigio = false;
  port.onSignal(SIGURG).then([&](siginfo_t&& info) {
    receivedSigurg = true;
    EXPECT_EQ(SIGURG, info.si_signo);
    EXPECT_SI_CODE(SI_USER, info.si_code);
  }).detach([](Exception&& e) { ADD_FAILURE() << str(e).cStr(); });
  port.onSignal(SIGIO).then([&](siginfo_t&& info) {
    receivedSigio = true;
    EXPECT_EQ(SIGIO, info.si_signo);
    EXPECT_SI_CODE(SI_USER, info.si_code);
  }).detach([](Exception&& e) { ADD_FAILURE() << str(e).cStr(); });

  kill(getpid(), SIGURG);
  kill(getpid(), SIGIO);

  EXPECT_FALSE(receivedSigurg);
  EXPECT_FALSE(receivedSigio);

  loop.run();

  EXPECT_FALSE(receivedSigurg);
  EXPECT_FALSE(receivedSigio);

  port.poll();

  EXPECT_FALSE(receivedSigurg);
  EXPECT_FALSE(receivedSigio);

  loop.run();

  EXPECT_TRUE(receivedSigurg);
  EXPECT_TRUE(receivedSigio);
}

#endif  // !__CYGWIN32__

TEST_F(AsyncUnixTest, Poll) {
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  int pipefds[2];
  KJ_DEFER({ close(pipefds[1]); close(pipefds[0]); });
  KJ_SYSCALL(pipe(pipefds));
  KJ_SYSCALL(write(pipefds[1], "foo", 3));

  EXPECT_EQ(POLLIN, port.onFdEvent(pipefds[0], POLLIN | POLLPRI).wait(waitScope));
}

TEST_F(AsyncUnixTest, PollMultiListen) {
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  int bogusPipefds[2];
  KJ_SYSCALL(pipe(bogusPipefds));
  KJ_DEFER({ close(bogusPipefds[1]); close(bogusPipefds[0]); });

  port.onFdEvent(bogusPipefds[0], POLLIN | POLLPRI).then([](short s) {
    ADD_FAILURE() << "Received wrong poll.";
  }).detach([](kj::Exception&& exception) {
    ADD_FAILURE() << kj::str(exception).cStr();
  });

  int pipefds[2];
  KJ_SYSCALL(pipe(pipefds));
  KJ_DEFER({ close(pipefds[1]); close(pipefds[0]); });
  KJ_SYSCALL(write(pipefds[1], "foo", 3));

  EXPECT_EQ(POLLIN, port.onFdEvent(pipefds[0], POLLIN | POLLPRI).wait(waitScope));
}

TEST_F(AsyncUnixTest, PollMultiReceive) {
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  int pipefds[2];
  KJ_SYSCALL(pipe(pipefds));
  KJ_DEFER({ close(pipefds[1]); close(pipefds[0]); });
  KJ_SYSCALL(write(pipefds[1], "foo", 3));

  int pipefds2[2];
  KJ_SYSCALL(pipe(pipefds2));
  KJ_DEFER({ close(pipefds2[1]); close(pipefds2[0]); });
  KJ_SYSCALL(write(pipefds2[1], "bar", 3));

  EXPECT_EQ(POLLIN, port.onFdEvent(pipefds[0], POLLIN | POLLPRI).wait(waitScope));
  EXPECT_EQ(POLLIN, port.onFdEvent(pipefds2[0], POLLIN | POLLPRI).wait(waitScope));
}

TEST_F(AsyncUnixTest, PollAsync) {
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  // Make a pipe and wait on its read end while another thread writes to it.
  int pipefds[2];
  KJ_DEFER({ close(pipefds[1]); close(pipefds[0]); });
  KJ_SYSCALL(pipe(pipefds));
  Thread thread([&]() {
    delay();
    KJ_SYSCALL(write(pipefds[1], "foo", 3));
  });

  // Wait for the event in this thread.
  EXPECT_EQ(POLLIN, port.onFdEvent(pipefds[0], POLLIN | POLLPRI).wait(waitScope));
}

TEST_F(AsyncUnixTest, PollNoWait) {
  // Verify that UnixEventPort::poll() correctly receives pending FD events.

  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  int pipefds[2];
  KJ_SYSCALL(pipe(pipefds));
  KJ_DEFER({ close(pipefds[1]); close(pipefds[0]); });

  int pipefds2[2];
  KJ_SYSCALL(pipe(pipefds2));
  KJ_DEFER({ close(pipefds2[1]); close(pipefds2[0]); });

  int receivedCount = 0;
  port.onFdEvent(pipefds[0], POLLIN | POLLPRI).then([&](short&& events) {
    receivedCount++;
    EXPECT_EQ(POLLIN, events);
  }).detach([](Exception&& e) { ADD_FAILURE() << str(e).cStr(); });
  port.onFdEvent(pipefds2[0], POLLIN | POLLPRI).then([&](short&& events) {
    receivedCount++;
    EXPECT_EQ(POLLIN, events);
  }).detach([](Exception&& e) { ADD_FAILURE() << str(e).cStr(); });

  KJ_SYSCALL(write(pipefds[1], "foo", 3));
  KJ_SYSCALL(write(pipefds2[1], "bar", 3));

  EXPECT_EQ(0, receivedCount);

  loop.run();

  EXPECT_EQ(0, receivedCount);

  port.poll();

  EXPECT_EQ(0, receivedCount);

  loop.run();

  EXPECT_EQ(2, receivedCount);
}

TEST_F(AsyncUnixTest, SteadyTimers) {
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  auto start = port.steadyTime();
  kj::Vector<TimePoint> expected;
  kj::Vector<TimePoint> actual;

  auto addTimer = [&](Duration delay) {
    expected.add(max(start + delay, start));
    port.atSteadyTime(start + delay).then([&]() {
      actual.add(port.steadyTime());
    }).detach([](Exception&& e) { ADD_FAILURE() << str(e).cStr(); });
  };

  addTimer(30 * MILLISECONDS);
  addTimer(40 * MILLISECONDS);
  addTimer(20350 * MICROSECONDS);
  addTimer(30 * MILLISECONDS);
  addTimer(-10 * MILLISECONDS);

  std::sort(expected.begin(), expected.end());
  port.atSteadyTime(expected.back() + MILLISECONDS).wait(waitScope);

  ASSERT_EQ(expected.size(), actual.size());
  for (int i = 0; i < expected.size(); ++i) {
    EXPECT_LE(expected[i], actual[i]) << "Actual time for timer " << i << " is "
        << ((expected[i] - actual[i]) / NANOSECONDS) << " ns too early.";
  }
}

}  // namespace kj
