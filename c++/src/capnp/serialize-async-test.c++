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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "serialize-async.h"
#include "serialize.h"
#include <kj/debug.h>
#include <kj/thread.h>
#include <stdlib.h>
#include <kj/miniposix.h>
#include "test-util.h"
#include <kj/compat/gtest.h>

#if _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <kj/windows-sanity.h>
namespace kj {
  namespace _ {
    int win32Socketpair(SOCKET socks[2]);
  }
}
#else
#include <sys/socket.h>
#endif

namespace capnp {
namespace _ {  // private
namespace {

#if _WIN32
inline void delay() { Sleep(5); }
#else
inline void delay() { usleep(5000); }
#endif

class FragmentingOutputStream: public kj::OutputStream {
public:
  FragmentingOutputStream(kj::OutputStream& inner): inner(inner) {}

  void write(const void* buffer, size_t size) override {
    while (size > 0) {
      delay();
      size_t n = rand() % size + 1;
      inner.write(buffer, n);
      buffer = reinterpret_cast<const byte*>(buffer) + n;
      size -= n;
    }
  }

private:
  kj::OutputStream& inner;
};

class TestMessageBuilder: public MallocMessageBuilder {
  // A MessageBuilder that tries to allocate an exact number of total segments, by allocating
  // minimum-size segments until it reaches the number, then allocating one large segment to
  // finish.

public:
  explicit TestMessageBuilder(uint desiredSegmentCount)
      : MallocMessageBuilder(0, AllocationStrategy::FIXED_SIZE),
        desiredSegmentCount(desiredSegmentCount) {}
  ~TestMessageBuilder() {
    EXPECT_EQ(0u, desiredSegmentCount);
  }

  kj::ArrayPtr<word> allocateSegment(uint minimumSize) override {
    if (desiredSegmentCount <= 1) {
      if (desiredSegmentCount < 1) {
        ADD_FAILURE() << "Allocated more segments than desired.";
      } else {
        --desiredSegmentCount;
      }
      return MallocMessageBuilder::allocateSegment(8192);
    } else {
      --desiredSegmentCount;
      return MallocMessageBuilder::allocateSegment(minimumSize);
    }
  }

private:
  uint desiredSegmentCount;
};

class PipeWithSmallBuffer {
public:
#ifdef _WIN32
#define KJ_SOCKCALL KJ_WINSOCK
#ifndef SHUT_WR
#define SHUT_WR SD_SEND
#endif
#define socketpair(family, type, flags, fds) kj::_::win32Socketpair(fds)
#else
#define KJ_SOCKCALL KJ_SYSCALL
#endif

  PipeWithSmallBuffer() {
    // Use a socketpair rather than a pipe so that we can set the buffer size extremely small.
    KJ_SOCKCALL(socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

    KJ_SOCKCALL(shutdown(fds[0], SHUT_WR));
    // Note:  OSX reports ENOTCONN if we also try to shutdown(fds[1], SHUT_RD).

    // Request that the buffer size be as small as possible, to force the event loop to kick in.
    // FUN STUFF:
    // - On Linux, the kernel rounds up to the smallest size it permits, so we can ask for a size of
    //   zero.
    // - On OSX, the kernel reports EINVAL on zero, but will dutifully use a 1-byte buffer if we
    //   set the size to 1.  This tends to cause stack overflows due to ridiculously long promise
    //   chains.
    // - Cygwin will apparently actually use a buffer size of 0 and therefore block forever waiting
    //   for buffer space.
    // - GNU HURD throws ENOPROTOOPT for SO_RCVBUF. Apparently, technically, a Unix domain socket
    //   has only one buffer, and it's controlled via SO_SNDBUF on the other end. OK, we'll ignore
    //   errors on SO_RCVBUF, then.
    //
    // Anyway, we now use 127 to avoid these issues (but also to screw around with non-word-boundary
    // writes).
    uint small = 127;
    setsockopt(fds[0], SOL_SOCKET, SO_RCVBUF, (const char*)&small, sizeof(small));
    KJ_SOCKCALL(setsockopt(fds[1], SOL_SOCKET, SO_SNDBUF, (const char*)&small, sizeof(small)));
  }
  ~PipeWithSmallBuffer() {
#if _WIN32
    closesocket(fds[0]);
    closesocket(fds[1]);
#else
    close(fds[0]);
    close(fds[1]);
#endif
  }

  inline int operator[](uint index) { return fds[index]; }

private:
#ifdef _WIN32
  SOCKET fds[2];
#else
  int fds[2];
#endif
};

#if _WIN32
// Sockets on win32 are not file descriptors. Ugh.
//
// TODO(cleanup): Maybe put these somewhere reusable? kj/io.h is inappropriate since we don't
//   really want to link against winsock.

class SocketOutputStream: public kj::OutputStream {
public:
  explicit SocketOutputStream(SOCKET fd): fd(fd) {}

  void write(const void* buffer, size_t size) override {
    const char* ptr = reinterpret_cast<const char*>(buffer);
    while (size > 0) {
      kj::miniposix::ssize_t n;
      KJ_SOCKCALL(n = send(fd, ptr, size, 0));
      size -= n;
      ptr += n;
    }
  }

private:
  SOCKET fd;
};

class SocketInputStream: public kj::InputStream {
public:
  explicit SocketInputStream(SOCKET fd): fd(fd) {}

  size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    char* ptr = reinterpret_cast<char*>(buffer);
    size_t total = 0;
    while (total < minBytes) {
      kj::miniposix::ssize_t n;
      KJ_SOCKCALL(n = recv(fd, ptr, maxBytes, 0));
      total += n;
      maxBytes -= n;
      ptr += n;
    }
    return total;
  }

private:
  SOCKET fd;
};
#else  // _WIN32
typedef kj::FdOutputStream SocketOutputStream;
typedef kj::FdInputStream SocketInputStream;
#endif  // _WIN32, else

TEST(SerializeAsyncTest, ParseAsync) {
  PipeWithSmallBuffer fds;
  auto ioContext = kj::setupAsyncIo();
  auto input = ioContext.lowLevelProvider->wrapInputFd(fds[0]);
  SocketOutputStream rawOutput(fds[1]);
  FragmentingOutputStream output(rawOutput);

  TestMessageBuilder message(1);
  initTestMessage(message.getRoot<TestAllTypes>());

  kj::Thread thread([&]() {
    writeMessage(output, message);
  });

  auto received = readMessage(*input).wait(ioContext.waitScope);

  checkTestMessage(received->getRoot<TestAllTypes>());
}

TEST(SerializeAsyncTest, ParseAsyncOddSegmentCount) {
  PipeWithSmallBuffer fds;
  auto ioContext = kj::setupAsyncIo();
  auto input = ioContext.lowLevelProvider->wrapInputFd(fds[0]);
  SocketOutputStream rawOutput(fds[1]);
  FragmentingOutputStream output(rawOutput);

  TestMessageBuilder message(7);
  initTestMessage(message.getRoot<TestAllTypes>());

  kj::Thread thread([&]() {
    writeMessage(output, message);
  });

  auto received = readMessage(*input).wait(ioContext.waitScope);

  checkTestMessage(received->getRoot<TestAllTypes>());
}

TEST(SerializeAsyncTest, ParseAsyncEvenSegmentCount) {
  PipeWithSmallBuffer fds;
  auto ioContext = kj::setupAsyncIo();
  auto input = ioContext.lowLevelProvider->wrapInputFd(fds[0]);
  SocketOutputStream rawOutput(fds[1]);
  FragmentingOutputStream output(rawOutput);

  TestMessageBuilder message(10);
  initTestMessage(message.getRoot<TestAllTypes>());

  kj::Thread thread([&]() {
    writeMessage(output, message);
  });

  auto received = readMessage(*input).wait(ioContext.waitScope);

  checkTestMessage(received->getRoot<TestAllTypes>());
}

TEST(SerializeAsyncTest, WriteAsync) {
  PipeWithSmallBuffer fds;
  auto ioContext = kj::setupAsyncIo();
  auto output = ioContext.lowLevelProvider->wrapOutputFd(fds[1]);

  TestMessageBuilder message(1);
  auto root = message.getRoot<TestAllTypes>();
  auto list = root.initStructList(16);
  for (auto element: list) {
    initTestMessage(element);
  }

  kj::Thread thread([&]() {
    SocketInputStream input(fds[0]);
    InputStreamMessageReader reader(input);
    auto listReader = reader.getRoot<TestAllTypes>().getStructList();
    EXPECT_EQ(list.size(), listReader.size());
    for (auto element: listReader) {
      checkTestMessage(element);
    }
  });

  writeMessage(*output, message).wait(ioContext.waitScope);
}

TEST(SerializeAsyncTest, WriteAsyncOddSegmentCount) {
  PipeWithSmallBuffer fds;
  auto ioContext = kj::setupAsyncIo();
  auto output = ioContext.lowLevelProvider->wrapOutputFd(fds[1]);

  TestMessageBuilder message(7);
  auto root = message.getRoot<TestAllTypes>();
  auto list = root.initStructList(16);
  for (auto element: list) {
    initTestMessage(element);
  }

  kj::Thread thread([&]() {
    SocketInputStream input(fds[0]);
    InputStreamMessageReader reader(input);
    auto listReader = reader.getRoot<TestAllTypes>().getStructList();
    EXPECT_EQ(list.size(), listReader.size());
    for (auto element: listReader) {
      checkTestMessage(element);
    }
  });

  writeMessage(*output, message).wait(ioContext.waitScope);
}

TEST(SerializeAsyncTest, WriteAsyncEvenSegmentCount) {
  PipeWithSmallBuffer fds;
  auto ioContext = kj::setupAsyncIo();
  auto output = ioContext.lowLevelProvider->wrapOutputFd(fds[1]);

  TestMessageBuilder message(10);
  auto root = message.getRoot<TestAllTypes>();
  auto list = root.initStructList(16);
  for (auto element: list) {
    initTestMessage(element);
  }

  kj::Thread thread([&]() {
    SocketInputStream input(fds[0]);
    InputStreamMessageReader reader(input);
    auto listReader = reader.getRoot<TestAllTypes>().getStructList();
    EXPECT_EQ(list.size(), listReader.size());
    for (auto element: listReader) {
      checkTestMessage(element);
    }
  });

  writeMessage(*output, message).wait(ioContext.waitScope);
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
