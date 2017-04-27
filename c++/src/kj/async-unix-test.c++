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

#if !_WIN32

#include "async-unix.h"
#include "thread.h"
#include "debug.h"
#include "io.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <kj/compat/gtest.h>
#include <pthread.h>
#include <algorithm>

namespace kj {
namespace {

inline void delay() { usleep(10000); }

// On OSX, si_code seems to be zero when SI_USER is expected.
#if __linux__ || __CYGWIN__
#define EXPECT_SI_CODE EXPECT_EQ
#else
#define EXPECT_SI_CODE(a,b)
#endif

void captureSignals() {
  static bool captured = false;
  if (!captured) {
    captured = true;

    // We use SIGIO and SIGURG as our test signals because they're two signals that we can be
    // reasonably confident won't otherwise be delivered to any KJ or Cap'n Proto test.  We can't
    // use SIGUSR1 because it is reserved by UnixEventPort and SIGUSR2 is used by Valgrind on OSX.
    UnixEventPort::captureSignal(SIGURG);
    UnixEventPort::captureSignal(SIGIO);
  }
}

TEST(AsyncUnixTest, Signals) {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  kill(getpid(), SIGURG);

  siginfo_t info = port.onSignal(SIGURG).wait(waitScope);
  EXPECT_EQ(SIGURG, info.si_signo);
  EXPECT_SI_CODE(SI_USER, info.si_code);
}

#if defined(SIGRTMIN) && !__BIONIC__ && !(__linux__ && __mips__)
TEST(AsyncUnixTest, SignalWithValue) {
  // This tests that if we use sigqueue() to attach a value to the signal, that value is received
  // correctly.  Note that this only works on platforms that support real-time signals -- even
  // though the signal we're sending is SIGURG, the sigqueue() system call is introduced by RT
  // signals.  Hence this test won't run on e.g. Mac OSX.
  //
  // Also, Android's bionic does not appear to support sigqueue() even though the kernel does.
  //
  // Also, this test fails on Linux on mipsel. si_value comes back as zero. No one with a mips
  // machine wants to debug the problem but they demand a patch fixing it, so we disable the test.
  // Sad. https://github.com/sandstorm-io/capnproto/issues/204

  captureSignals();
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

TEST(AsyncUnixTest, SignalWithPointerValue) {
  // This tests that if we use sigqueue() to attach a value to the signal, that value is received
  // correctly.  Note that this only works on platforms that support real-time signals -- even
  // though the signal we're sending is SIGURG, the sigqueue() system call is introduced by RT
  // signals.  Hence this test won't run on e.g. Mac OSX.
  //
  // Also, Android's bionic does not appear to support sigqueue() even though the kernel does.
  //
  // Also, this test fails on Linux on mipsel. si_value comes back as zero. No one with a mips
  // machine wants to debug the problem but they demand a patch fixing it, so we disable the test.
  // Sad. https://github.com/sandstorm-io/capnproto/issues/204

  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  union sigval value;
  memset(&value, 0, sizeof(value));
  value.sival_ptr = &port;
  sigqueue(getpid(), SIGURG, value);

  siginfo_t info = port.onSignal(SIGURG).wait(waitScope);
  EXPECT_EQ(SIGURG, info.si_signo);
  EXPECT_SI_CODE(SI_QUEUE, info.si_code);
  EXPECT_EQ(&port, info.si_value.sival_ptr);
}
#endif

TEST(AsyncUnixTest, SignalsMultiListen) {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  port.onSignal(SIGIO).then([](siginfo_t&&) {
    KJ_FAIL_EXPECT("Received wrong signal.");
  }).detach([](kj::Exception&& exception) {
    KJ_FAIL_EXPECT(exception);
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

TEST(AsyncUnixTest, SignalsMultiReceive) {
  captureSignals();
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

TEST(AsyncUnixTest, SignalsAsync) {
  captureSignals();
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

TEST(AsyncUnixTest, SignalsNoWait) {
  // Verify that UnixEventPort::poll() correctly receives pending signals.

  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  bool receivedSigurg = false;
  bool receivedSigio = false;
  port.onSignal(SIGURG).then([&](siginfo_t&& info) {
    receivedSigurg = true;
    EXPECT_EQ(SIGURG, info.si_signo);
    EXPECT_SI_CODE(SI_USER, info.si_code);
  }).detach([](Exception&& e) { KJ_FAIL_EXPECT(e); });
  port.onSignal(SIGIO).then([&](siginfo_t&& info) {
    receivedSigio = true;
    EXPECT_EQ(SIGIO, info.si_signo);
    EXPECT_SI_CODE(SI_USER, info.si_code);
  }).detach([](Exception&& e) { KJ_FAIL_EXPECT(e); });

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

TEST(AsyncUnixTest, ReadObserver) {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  int pipefds[2];
  KJ_SYSCALL(pipe(pipefds));
  kj::AutoCloseFd infd(pipefds[0]), outfd(pipefds[1]);

  UnixEventPort::FdObserver observer(port, infd, UnixEventPort::FdObserver::OBSERVE_READ);

  KJ_SYSCALL(write(outfd, "foo", 3));

  observer.whenBecomesReadable().wait(waitScope);

#if __linux__  // platform known to support POLLRDHUP
  EXPECT_FALSE(KJ_ASSERT_NONNULL(observer.atEndHint()));

  char buffer[4096];
  ssize_t n;
  KJ_SYSCALL(n = read(infd, &buffer, sizeof(buffer)));
  EXPECT_EQ(3, n);

  KJ_SYSCALL(write(outfd, "bar", 3));
  outfd = nullptr;

  observer.whenBecomesReadable().wait(waitScope);

  EXPECT_TRUE(KJ_ASSERT_NONNULL(observer.atEndHint()));
#endif
}

TEST(AsyncUnixTest, ReadObserverMultiListen) {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  int bogusPipefds[2];
  KJ_SYSCALL(pipe(bogusPipefds));
  KJ_DEFER({ close(bogusPipefds[1]); close(bogusPipefds[0]); });

  UnixEventPort::FdObserver bogusObserver(port, bogusPipefds[0],
      UnixEventPort::FdObserver::OBSERVE_READ);

  bogusObserver.whenBecomesReadable().then([]() {
    ADD_FAILURE() << "Received wrong poll.";
  }).detach([](kj::Exception&& exception) {
    ADD_FAILURE() << kj::str(exception).cStr();
  });

  int pipefds[2];
  KJ_SYSCALL(pipe(pipefds));
  KJ_DEFER({ close(pipefds[1]); close(pipefds[0]); });

  UnixEventPort::FdObserver observer(port, pipefds[0],
      UnixEventPort::FdObserver::OBSERVE_READ);
  KJ_SYSCALL(write(pipefds[1], "foo", 3));

  observer.whenBecomesReadable().wait(waitScope);
}

TEST(AsyncUnixTest, ReadObserverMultiReceive) {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  int pipefds[2];
  KJ_SYSCALL(pipe(pipefds));
  KJ_DEFER({ close(pipefds[1]); close(pipefds[0]); });

  UnixEventPort::FdObserver observer(port, pipefds[0],
      UnixEventPort::FdObserver::OBSERVE_READ);
  KJ_SYSCALL(write(pipefds[1], "foo", 3));

  int pipefds2[2];
  KJ_SYSCALL(pipe(pipefds2));
  KJ_DEFER({ close(pipefds2[1]); close(pipefds2[0]); });

  UnixEventPort::FdObserver observer2(port, pipefds2[0],
      UnixEventPort::FdObserver::OBSERVE_READ);
  KJ_SYSCALL(write(pipefds2[1], "bar", 3));

  auto promise1 = observer.whenBecomesReadable();
  auto promise2 = observer2.whenBecomesReadable();
  promise1.wait(waitScope);
  promise2.wait(waitScope);
}

TEST(AsyncUnixTest, ReadObserverAsync) {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  // Make a pipe and wait on its read end while another thread writes to it.
  int pipefds[2];
  KJ_SYSCALL(pipe(pipefds));
  KJ_DEFER({ close(pipefds[1]); close(pipefds[0]); });
  UnixEventPort::FdObserver observer(port, pipefds[0],
      UnixEventPort::FdObserver::OBSERVE_READ);

  Thread thread([&]() {
    delay();
    KJ_SYSCALL(write(pipefds[1], "foo", 3));
  });

  // Wait for the event in this thread.
  observer.whenBecomesReadable().wait(waitScope);
}

TEST(AsyncUnixTest, ReadObserverNoWait) {
  // Verify that UnixEventPort::poll() correctly receives pending FD events.

  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  int pipefds[2];
  KJ_SYSCALL(pipe(pipefds));
  KJ_DEFER({ close(pipefds[1]); close(pipefds[0]); });
  UnixEventPort::FdObserver observer(port, pipefds[0],
      UnixEventPort::FdObserver::OBSERVE_READ);

  int pipefds2[2];
  KJ_SYSCALL(pipe(pipefds2));
  KJ_DEFER({ close(pipefds2[1]); close(pipefds2[0]); });
  UnixEventPort::FdObserver observer2(port, pipefds2[0],
      UnixEventPort::FdObserver::OBSERVE_READ);

  int receivedCount = 0;
  observer.whenBecomesReadable().then([&]() {
    receivedCount++;
  }).detach([](Exception&& e) { ADD_FAILURE() << str(e).cStr(); });
  observer2.whenBecomesReadable().then([&]() {
    receivedCount++;
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

static void setNonblocking(int fd) {
  int flags;
  KJ_SYSCALL(flags = fcntl(fd, F_GETFL));
  if ((flags & O_NONBLOCK) == 0) {
    KJ_SYSCALL(fcntl(fd, F_SETFL, flags | O_NONBLOCK));
  }
}

TEST(AsyncUnixTest, WriteObserver) {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  int pipefds[2];
  KJ_SYSCALL(pipe(pipefds));
  kj::AutoCloseFd infd(pipefds[0]), outfd(pipefds[1]);
  setNonblocking(outfd);
  setNonblocking(infd);

  UnixEventPort::FdObserver observer(port, outfd, UnixEventPort::FdObserver::OBSERVE_WRITE);

  // Fill buffer.
  ssize_t n;
  do {
    KJ_NONBLOCKING_SYSCALL(n = write(outfd, "foo", 3));
  } while (n >= 0);

  bool writable = false;
  auto promise = observer.whenBecomesWritable()
      .then([&]() { writable = true; }).eagerlyEvaluate(nullptr);

  loop.run();
  port.poll();
  loop.run();

  EXPECT_FALSE(writable);

  // Empty the read end so that the write end becomes writable. Note that Linux implements a
  // high watermark / low watermark heuristic which means that only reading one byte is not
  // sufficient. The amount we have to read is in fact architecture-dependent -- it appears to be
  // 1 page. To be safe, we read everything.
  char buffer[4096];
  do {
    KJ_NONBLOCKING_SYSCALL(n = read(infd, &buffer, sizeof(buffer)));
  } while (n > 0);

  loop.run();
  port.poll();
  loop.run();

  EXPECT_TRUE(writable);
}

#if !__APPLE__
// Disabled on macOS due to https://github.com/sandstorm-io/capnproto/issues/374.
TEST(AsyncUnixTest, UrgentObserver) {
  // Verify that FdObserver correctly detects availability of out-of-band data.
  // Availability of out-of-band data is implementation-specific.
  // Linux's and OS X's TCP/IP stack supports out-of-band messages for TCP sockets, which is used
  // for this test.

  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);
  int tmpFd;
  char c;

  // Spawn a TCP server
  KJ_SYSCALL(tmpFd = socket(AF_INET, SOCK_STREAM, 0));
  kj::AutoCloseFd serverFd(tmpFd);
  sockaddr_in saddr;
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  KJ_SYSCALL(bind(serverFd, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)));
  socklen_t saddrLen = sizeof(saddr);
  KJ_SYSCALL(getsockname(serverFd, reinterpret_cast<sockaddr*>(&saddr), &saddrLen));
  KJ_SYSCALL(listen(serverFd, 1));

  // Accept one connection, send in-band and OOB byte, wait for a quit message
  Thread thread([&]() {
    int tmpFd;
    char c;

    sockaddr_in caddr;
    socklen_t caddrLen = sizeof(caddr);
    KJ_SYSCALL(tmpFd = accept(serverFd, reinterpret_cast<sockaddr*>(&caddr), &caddrLen));
    kj::AutoCloseFd clientFd(tmpFd);
    delay();

    // Workaround: OS X won't signal POLLPRI without POLLIN. Also enqueue some in-band data.
    c = 'i';
    KJ_SYSCALL(send(clientFd, &c, 1, 0));
    c = 'o';
    KJ_SYSCALL(send(clientFd, &c, 1, MSG_OOB));

    KJ_SYSCALL(recv(clientFd, &c, 1, 0));
    EXPECT_EQ('q', c);
  });
  KJ_DEFER({ shutdown(serverFd, SHUT_RDWR); serverFd = nullptr; });

  KJ_SYSCALL(tmpFd = socket(AF_INET, SOCK_STREAM, 0));
  kj::AutoCloseFd clientFd(tmpFd);
  KJ_SYSCALL(connect(clientFd, reinterpret_cast<sockaddr*>(&saddr), saddrLen));

  UnixEventPort::FdObserver observer(port, clientFd,
      UnixEventPort::FdObserver::OBSERVE_READ | UnixEventPort::FdObserver::OBSERVE_URGENT);

  observer.whenUrgentDataAvailable().wait(waitScope);

#if __CYGWIN__
  // On Cygwin, reading the urgent byte first causes the subsequent regular read to block until
  // such a time as the connection closes -- and then the byte is successfully returned. This
  // seems to be a cygwin bug.
  KJ_SYSCALL(recv(clientFd, &c, 1, 0));
  EXPECT_EQ('i', c);
  KJ_SYSCALL(recv(clientFd, &c, 1, MSG_OOB));
  EXPECT_EQ('o', c);
#else
  // Attempt to read the urgent byte prior to reading the in-band byte.
  KJ_SYSCALL(recv(clientFd, &c, 1, MSG_OOB));
  EXPECT_EQ('o', c);
  KJ_SYSCALL(recv(clientFd, &c, 1, 0));
  EXPECT_EQ('i', c);
#endif

  // Allow server thread to let its clientFd go out of scope.
  c = 'q';
  KJ_SYSCALL(send(clientFd, &c, 1, 0));
  KJ_SYSCALL(shutdown(clientFd, SHUT_RDWR));
}
#endif

TEST(AsyncUnixTest, SteadyTimers) {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  auto& timer = port.getTimer();

  auto start = timer.now();
  kj::Vector<TimePoint> expected;
  kj::Vector<TimePoint> actual;

  auto addTimer = [&](Duration delay) {
    expected.add(max(start + delay, start));
    timer.atTime(start + delay).then([&]() {
      actual.add(timer.now());
    }).detach([](Exception&& e) { ADD_FAILURE() << str(e).cStr(); });
  };

  addTimer(30 * MILLISECONDS);
  addTimer(40 * MILLISECONDS);
  addTimer(20350 * MICROSECONDS);
  addTimer(30 * MILLISECONDS);
  addTimer(-10 * MILLISECONDS);

  std::sort(expected.begin(), expected.end());
  timer.atTime(expected.back() + MILLISECONDS).wait(waitScope);

  ASSERT_EQ(expected.size(), actual.size());
  for (int i = 0; i < expected.size(); ++i) {
    KJ_EXPECT(expected[i] <= actual[i], "Actual time for timer i is too early.",
              i, ((expected[i] - actual[i]) / NANOSECONDS));
  }
}

TEST(AsyncUnixTest, Wake) {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  EXPECT_FALSE(port.poll());
  port.wake();
  EXPECT_TRUE(port.poll());
  EXPECT_FALSE(port.poll());

  port.wake();
  EXPECT_TRUE(port.wait());

  {
    auto promise = port.getTimer().atTime(port.getTimer().now());
    EXPECT_FALSE(port.wait());
  }

  bool woken = false;
  Thread thread([&]() {
    delay();
    woken = true;
    port.wake();
  });

  EXPECT_TRUE(port.wait());
}

}  // namespace
}  // namespace kj

#endif  // !_WIN32
