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
#include <sys/wait.h>
#include <sys/time.h>
#include <errno.h>
#include <atomic>
#include "mutex.h"

#if KJ_USE_EPOLL
#include <sys/epoll.h>
#endif

#if KJ_USE_KQUEUE
#include <sys/event.h>
#endif

#if __BIONIC__
// Android's Bionic defines SIGRTMIN but using it in sigaddset() throws EINVAL, which means we
// definitely can't actually use RT signals.
#undef SIGRTMIN
#endif

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
    // We use SIGIO and SIGURG as our test signals because they're two signals that we can be
    // reasonably confident won't otherwise be delivered to any KJ or Cap'n Proto test.  We can't
    // use SIGUSR1 because it is reserved by UnixEventPort and SIGUSR2 is used by Valgrind on OSX.
    UnixEventPort::captureSignal(SIGURG);
    UnixEventPort::captureSignal(SIGIO);

#ifdef SIGRTMIN
    UnixEventPort::captureSignal(SIGRTMIN);
#endif

    UnixEventPort::captureChildExit();

    captured = true;
  }
}

#if KJ_USE_EPOLL
bool qemuBugTestSignalHandlerRan = false;
void qemuBugTestSignalHandler(int, siginfo_t* siginfo, void*) {
  qemuBugTestSignalHandlerRan = true;
}

bool checkForQemuEpollPwaitBug() {
  // Under qemu-user, when a signal is delivered during epoll_pwait(), the signal successfully
  // interrupts the wait, but the correct signal handler is not run. This ruins all our tests so
  // we check for it and skip tests in this case. This does imply UnixEventPort won't be able to
  // handle signals correctly under qemu-user.

  sigset_t mask;
  sigset_t origMask;
  KJ_SYSCALL(sigemptyset(&mask));
  KJ_SYSCALL(sigaddset(&mask, SIGURG));
  KJ_SYSCALL(pthread_sigmask(SIG_BLOCK, &mask, &origMask));
  KJ_DEFER(KJ_SYSCALL(pthread_sigmask(SIG_SETMASK, &origMask, nullptr)));

  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_sigaction = &qemuBugTestSignalHandler;
  action.sa_flags = SA_SIGINFO;

  KJ_SYSCALL(sigfillset(&action.sa_mask));
  KJ_SYSCALL(sigdelset(&action.sa_mask, SIGBUS));
  KJ_SYSCALL(sigdelset(&action.sa_mask, SIGFPE));
  KJ_SYSCALL(sigdelset(&action.sa_mask, SIGILL));
  KJ_SYSCALL(sigdelset(&action.sa_mask, SIGSEGV));

  KJ_SYSCALL(sigaction(SIGURG, &action, nullptr));

  int efd;
  KJ_SYSCALL(efd = epoll_create1(EPOLL_CLOEXEC));
  KJ_DEFER(close(efd));

  kill(getpid(), SIGURG);
  KJ_ASSERT(!qemuBugTestSignalHandlerRan);

  struct epoll_event event;
  int n = epoll_pwait(efd, &event, 1, -1, &origMask);
  KJ_ASSERT(n < 0);
  KJ_ASSERT(errno == EINTR);

#if !__aarch64__
  // qemu-user should only be used to execute aarch64 binaries so we should'nt see this bug
  // elsewhere!
  KJ_ASSERT(qemuBugTestSignalHandlerRan);
#endif

  return !qemuBugTestSignalHandlerRan;
}

const bool BROKEN_QEMU = checkForQemuEpollPwaitBug();
#else
const bool BROKEN_QEMU = false;
#endif

TEST(AsyncUnixTest, Signals) {
  if (BROKEN_QEMU) return;

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
  // Sad. https://github.com/capnproto/capnproto/issues/204

  if (BROKEN_QEMU) return;

  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  union sigval value;
  memset(&value, 0, sizeof(value));
  value.sival_int = 123;
  KJ_SYSCALL_HANDLE_ERRORS(sigqueue(getpid(), SIGURG, value)) {
    case ENOSYS:
      // sigqueue() not supported. Maybe running on WSL.
      KJ_LOG(WARNING, "sigqueue() is not implemented by your system; skipping test");
      return;
    default:
      KJ_FAIL_SYSCALL("sigqueue(getpid(), SIGURG, value)", error);
  }

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
  // Sad. https://github.com/capnproto/capnproto/issues/204

  if (BROKEN_QEMU) return;

  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  union sigval value;
  memset(&value, 0, sizeof(value));
  value.sival_ptr = &port;
  KJ_SYSCALL_HANDLE_ERRORS(sigqueue(getpid(), SIGURG, value)) {
    case ENOSYS:
      // sigqueue() not supported. Maybe running on WSL.
      KJ_LOG(WARNING, "sigqueue() is not implemented by your system; skipping test");
      return;
    default:
      KJ_FAIL_SYSCALL("sigqueue(getpid(), SIGURG, value)", error);
  }

  siginfo_t info = port.onSignal(SIGURG).wait(waitScope);
  EXPECT_EQ(SIGURG, info.si_signo);
  EXPECT_SI_CODE(SI_QUEUE, info.si_code);
  EXPECT_EQ(&port, info.si_value.sival_ptr);
}
#endif

TEST(AsyncUnixTest, SignalsMultiListen) {
  if (BROKEN_QEMU) return;

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
  if (BROKEN_QEMU) return;

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
  if (BROKEN_QEMU) return;

  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  // Arrange for a signal to be sent from another thread.
  pthread_t mainThread KJ_UNUSED = pthread_self();
  Thread thread([&]() {
    delay();
#if __APPLE__ && KJ_USE_KQUEUE
    // MacOS kqueue only receives process-level signals and there's nothing much we can do about
    // that.
    kill(getpid(), SIGURG);
#else
    pthread_kill(mainThread, SIGURG);
#endif
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

TEST(AsyncUnixTest, ReadObserverAndSignals) {
  // Get FD events while also waiting on a signal. This specifically exercises epoll_pwait() for
  // FD events on Linux.

  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  auto signalPromise = port.onSignal(SIGIO);

  int pipefds[2];
  KJ_SYSCALL(pipe(pipefds));
  kj::AutoCloseFd infd(pipefds[0]), outfd(pipefds[1]);

  UnixEventPort::FdObserver observer(port, infd, UnixEventPort::FdObserver::OBSERVE_READ);

  KJ_SYSCALL(write(outfd, "foo", 3));

  observer.whenBecomesReadable().wait(waitScope);

  KJ_EXPECT(!signalPromise.poll(waitScope))
  kill(getpid(), SIGIO);
  KJ_EXPECT(signalPromise.poll(waitScope))
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

#if !__APPLE__ && !(KJ_USE_KQUEUE && !defined(EVFILT_EXCEPT))
// Disabled on macOS due to https://github.com/capnproto/capnproto/issues/374.
// Disabled on kqueue systems that lack EVFILT_EXCEPT because it doesn't work there.
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

  // Create a pipe that we'll use to signal if MSG_OOB return EINVAL.
  int failpipe[2];
  KJ_SYSCALL(pipe(failpipe));
  KJ_DEFER({
    close(failpipe[0]);
    close(failpipe[1]);
  });

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
    KJ_SYSCALL_HANDLE_ERRORS(send(clientFd, &c, 1, MSG_OOB)) {
      case EINVAL:
        // Looks like MSG_OOB is not supported. (This is the case e.g. on WSL.)
        KJ_SYSCALL(write(failpipe[1], &c, 1));
        break;
      default:
        KJ_FAIL_SYSCALL("send(..., MSG_OOB)", error);
    }

    KJ_SYSCALL(recv(clientFd, &c, 1, 0));
    EXPECT_EQ('q', c);
  });
  KJ_DEFER({ shutdown(serverFd, SHUT_RDWR); serverFd = nullptr; });

  KJ_SYSCALL(tmpFd = socket(AF_INET, SOCK_STREAM, 0));
  kj::AutoCloseFd clientFd(tmpFd);
  KJ_SYSCALL(connect(clientFd, reinterpret_cast<sockaddr*>(&saddr), saddrLen));

  UnixEventPort::FdObserver observer(port, clientFd,
      UnixEventPort::FdObserver::OBSERVE_READ | UnixEventPort::FdObserver::OBSERVE_URGENT);
  UnixEventPort::FdObserver failObserver(port, failpipe[0],
      UnixEventPort::FdObserver::OBSERVE_READ | UnixEventPort::FdObserver::OBSERVE_URGENT);

  auto promise = observer.whenUrgentDataAvailable().then([]() { return true; });
  auto failPromise = failObserver.whenBecomesReadable().then([]() { return false; });

  bool oobSupported = promise.exclusiveJoin(kj::mv(failPromise)).wait(waitScope);
  if (oobSupported) {
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
  } else {
    KJ_LOG(WARNING, "MSG_OOB doesn't seem to be supported on your platform.");
  }

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

bool dummySignalHandlerCalled = false;
void dummySignalHandler(int) {
  dummySignalHandlerCalled = true;
}

TEST(AsyncUnixTest, InterruptedTimer) {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

#if __linux__
  // Linux timeslices are 1ms.
  constexpr auto OS_SLOWNESS_FACTOR = 1;
#else
  // OSX timeslices are 10ms, so we need longer timeouts to avoid flakiness.
  // To be safe we'll assume other OS's are similar.
  constexpr auto OS_SLOWNESS_FACTOR = 10;
#endif

  // Schedule a timer event in 100ms.
  auto& timer = port.getTimer();
  auto start = timer.now();
  constexpr auto timeout = 100 * MILLISECONDS * OS_SLOWNESS_FACTOR;

  // Arrange SIGALRM to be delivered in 50ms, handled in an empty signal handler. This will cause
  // our wait to be interrupted with EINTR. We should nevertheless continue waiting for the right
  // amount of time.
  dummySignalHandlerCalled = false;
  if (signal(SIGALRM, &dummySignalHandler) == SIG_ERR) {
    KJ_FAIL_SYSCALL("signal(SIGALRM)", errno);
  }
  struct itimerval itv;
  memset(&itv, 0, sizeof(itv));
  itv.it_value.tv_usec = 50000 * OS_SLOWNESS_FACTOR;  // signal after 50ms
  setitimer(ITIMER_REAL, &itv, nullptr);

  timer.afterDelay(timeout).wait(waitScope);

  KJ_EXPECT(dummySignalHandlerCalled);
  KJ_EXPECT(timer.now() - start >= timeout);
  KJ_EXPECT(timer.now() - start <= timeout + (timeout / 5));  // allow 20ms error
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

  // Test wake() when already wait()ing.
  {
    Thread thread([&]() {
      delay();
      port.wake();
    });

    EXPECT_TRUE(port.wait());
  }

  // Test wait() after wake() already happened.
  {
    Thread thread([&]() {
      port.wake();
    });

    delay();
    EXPECT_TRUE(port.wait());
  }

  // Test wake() during poll() busy loop.
  {
    Thread thread([&]() {
      delay();
      port.wake();
    });

    EXPECT_FALSE(port.poll());
    while (!port.poll()) {}
  }

  // Test poll() when wake() already delivered.
  {
    EXPECT_FALSE(port.poll());

    Thread thread([&]() {
      port.wake();
    });

    do {
      delay();
    } while (!port.poll());
  }
}

int exitCodeForSignal = 0;
[[noreturn]] void exitSignalHandler(int) {
  _exit(exitCodeForSignal);
}

struct TestChild {
  kj::Maybe<pid_t> pid;
  kj::Promise<int> promise = nullptr;

  TestChild(UnixEventPort& port, int exitCode) {
    pid_t p;
    KJ_SYSCALL(p = fork());
    if (p == 0) {
      // Arrange for SIGTERM to cause the process to exit normally.
      exitCodeForSignal = exitCode;
      signal(SIGTERM, &exitSignalHandler);
      sigset_t sigs;
      sigemptyset(&sigs);
      sigaddset(&sigs, SIGTERM);
      pthread_sigmask(SIG_UNBLOCK, &sigs, nullptr);

      for (;;) pause();
    }
    pid = p;
    promise = port.onChildExit(pid);
  }

  ~TestChild() noexcept(false) {
    KJ_IF_MAYBE(p, pid) {
      KJ_SYSCALL(::kill(*p, SIGKILL)) { return; }
      int status;
      KJ_SYSCALL(waitpid(*p, &status, 0)) { return; }
    }
  }

  void kill(int signo) {
    KJ_SYSCALL(::kill(KJ_REQUIRE_NONNULL(pid), signo));
  }

  KJ_DISALLOW_COPY_AND_MOVE(TestChild);
};

TEST(AsyncUnixTest, ChildProcess) {
  if (BROKEN_QEMU) return;

  captureSignals();

  // Block SIGTERM so that we can carefully un-block it in children.
  sigset_t sigs, oldsigs;
  KJ_SYSCALL(sigemptyset(&sigs));
  KJ_SYSCALL(sigaddset(&sigs, SIGTERM));
  KJ_SYSCALL(pthread_sigmask(SIG_BLOCK, &sigs, &oldsigs));
  KJ_DEFER(KJ_SYSCALL(pthread_sigmask(SIG_SETMASK, &oldsigs, nullptr)) { break; });

  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  TestChild child1(port, 123);
  KJ_EXPECT(!child1.promise.poll(waitScope));

  child1.kill(SIGTERM);

  {
    int status = child1.promise.wait(waitScope);
    KJ_EXPECT(WIFEXITED(status));
    KJ_EXPECT(WEXITSTATUS(status) == 123);
  }

  TestChild child2(port, 234);
  TestChild child3(port, 345);

  KJ_EXPECT(!child2.promise.poll(waitScope));
  KJ_EXPECT(!child3.promise.poll(waitScope));

  child2.kill(SIGKILL);

  {
    int status = child2.promise.wait(waitScope);
    KJ_EXPECT(!WIFEXITED(status));
    KJ_EXPECT(WIFSIGNALED(status));
    KJ_EXPECT(WTERMSIG(status) == SIGKILL);
  }

  KJ_EXPECT(!child3.promise.poll(waitScope));

  // child3 will be killed and synchronously waited on the way out.
}

#if !__CYGWIN__
// TODO(someday): Figure out why whenWriteDisconnected() never resolves on Cygwin.

KJ_TEST("UnixEventPort whenWriteDisconnected()") {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  int fds_[2];
  KJ_SYSCALL(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_));
  kj::AutoCloseFd fds[2] = { kj::AutoCloseFd(fds_[0]), kj::AutoCloseFd(fds_[1]) };

  UnixEventPort::FdObserver observer(port, fds[0], UnixEventPort::FdObserver::OBSERVE_READ);

  // At one point, the poll()-based version of UnixEventPort had a bug where if some other event
  // had completed previously, whenWriteDisconnected() would stop being watched for. So we watch
  // for readability as well and check that that goes away first.
  auto readablePromise = observer.whenBecomesReadable();
  auto hupPromise = observer.whenWriteDisconnected();

  KJ_EXPECT(!readablePromise.poll(waitScope));
  KJ_EXPECT(!hupPromise.poll(waitScope));

  KJ_SYSCALL(write(fds[1], "foo", 3));

  KJ_ASSERT(readablePromise.poll(waitScope));
  readablePromise.wait(waitScope);

  {
    char junk[16];
    ssize_t n;
    KJ_SYSCALL(n = read(fds[0], junk, 16));
    KJ_EXPECT(n == 3);
  }

  KJ_EXPECT(!hupPromise.poll(waitScope));

  fds[1] = nullptr;
  KJ_ASSERT(hupPromise.poll(waitScope));
  hupPromise.wait(waitScope);
}

KJ_TEST("UnixEventPort FdObserver(..., flags=0)::whenWriteDisconnected()") {
  // Verifies that given `0' as a `flags' argument,
  // FdObserver still observes whenWriteDisconnected().
  //
  // This can be useful to watch disconnection on a blocking file descriptor.
  // See discussion: https://github.com/capnproto/capnproto/issues/924

  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  int pipefds[2];
  KJ_SYSCALL(pipe(pipefds));
  kj::AutoCloseFd infd(pipefds[0]), outfd(pipefds[1]);

  UnixEventPort::FdObserver observer(port, outfd, 0);

  auto hupPromise = observer.whenWriteDisconnected();

  KJ_EXPECT(!hupPromise.poll(waitScope));

  infd = nullptr;
  KJ_ASSERT(hupPromise.poll(waitScope));
  hupPromise.wait(waitScope);
}

#endif

KJ_TEST("UnixEventPort poll for signals") {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  auto promise1 = port.onSignal(SIGURG);
  auto promise2 = port.onSignal(SIGIO);

  KJ_EXPECT(!promise1.poll(waitScope));
  KJ_EXPECT(!promise2.poll(waitScope));

  KJ_SYSCALL(kill(getpid(), SIGURG));
  KJ_SYSCALL(kill(getpid(), SIGIO));
  port.wake();

  KJ_EXPECT(port.poll());
  KJ_EXPECT(promise1.poll(waitScope));
  KJ_EXPECT(promise2.poll(waitScope));

  promise1.wait(waitScope);
  promise2.wait(waitScope);
}

#if defined(SIGRTMIN) && !__CYGWIN__ && !__aarch64__
// TODO(someday): Figure out why RT signals don't seem to work correctly on Cygwin. It looks like
//   only the first signal is delivered, like how non-RT signals work. Is it possible Cygwin
//   advertites RT signal support but doesn't actually implement them correctly? I can't find any
//   information on the internet about this and TBH I don't care about Cygwin enough to dig in.
// TODO(someday): Figure out why RT signals don't work under qemu-user emulating aarch64 on
//   Debian Buster.

void testRtSignals(UnixEventPort& port, WaitScope& waitScope, bool doPoll) {
  union sigval value;
  memset(&value, 0, sizeof(value));

  // Queue three copies of the signal upfront.
  for (uint i = 0; i < 3; i++) {
    value.sival_int = 123 + i;
    KJ_SYSCALL(sigqueue(getpid(), SIGRTMIN, value));
  }

  // Now wait for them.
  for (uint i = 0; i < 3; i++) {
    auto promise = port.onSignal(SIGRTMIN);
    if (doPoll) {
      KJ_ASSERT(promise.poll(waitScope));
    }
    auto info = promise.wait(waitScope);
    KJ_EXPECT(info.si_value.sival_int == 123 + i);
  }

  KJ_EXPECT(!port.onSignal(SIGRTMIN).poll(waitScope));
}

KJ_TEST("UnixEventPort can receive multiple queued instances of an RT signal") {
  captureSignals();
  UnixEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  testRtSignals(port, waitScope, true);

  // Test again, but don't poll() the promises. This may test a different code path, if poll() and
  // wait() are very different in how they read signals. (For the poll(2)-based implementation of
  // UnixEventPort, they are indeed pretty different.)
  testRtSignals(port, waitScope, false);
}
#endif

#if !(__APPLE__ && KJ_USE_KQUEUE)
KJ_TEST("UnixEventPort thread-specific signals") {
  // Verify a signal directed to a thread is only received on the intended thread.
  //
  // MacOS kqueue only receives process-level signals and there's nothing much we can do about
  // that, so this test won't work there.

  if (BROKEN_QEMU) return;

  captureSignals();

  Vector<Own<Thread>> threads;
  std::atomic<uint> readyCount(0);
  std::atomic<uint> doneCount(0);
  for (auto i KJ_UNUSED: kj::zeroTo(16)) {
    threads.add(kj::heap<Thread>([&]() noexcept {
      UnixEventPort port;
      EventLoop loop(port);
      WaitScope waitScope(loop);

      readyCount.fetch_add(1, std::memory_order_relaxed);
      port.onSignal(SIGIO).wait(waitScope);
      doneCount.fetch_add(1, std::memory_order_relaxed);
    }));
  }

  do {
    usleep(1000);
  } while (readyCount.load(std::memory_order_relaxed) < 16);

  KJ_ASSERT(doneCount.load(std::memory_order_relaxed) == 0);

  uint count = 0;
  for (uint i: {5, 14, 4, 6, 7, 11, 1, 3, 8, 0, 12, 9, 10, 15, 2, 13}) {
    threads[i]->sendSignal(SIGIO);
    threads[i] = nullptr;  // wait for that one thread to exit
    usleep(1000);
    KJ_ASSERT(doneCount.load(std::memory_order_relaxed) == ++count);
  }
}
#endif

}  // namespace
}  // namespace kj

#endif  // !_WIN32
