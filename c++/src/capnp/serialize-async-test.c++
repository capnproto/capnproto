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

#include "serialize-async.h"
#include "serialize.h"
#include <kj/debug.h>
#include <kj/thread.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "test-util.h"
#include <gtest/gtest.h>

namespace capnp {
namespace _ {  // private
namespace {

class FragmentingOutputStream: public kj::OutputStream {
public:
  FragmentingOutputStream(kj::OutputStream& inner): inner(inner) {}

  void write(const void* buffer, size_t size) override {
    while (size > 0) {
      usleep(5000);
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

class SerializeAsyncTest: public testing::Test {
protected:
  int fds[2];

  SerializeAsyncTest() {
    // Use a socketpair rather than a pipe so that we can set the buffer size extremely small.
    KJ_SYSCALL(socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

    KJ_SYSCALL(shutdown(fds[0], SHUT_WR));
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
    //
    // Anyway, we now use 127 to avoid these issues (but also to screw around with non-word-boundary
    // writes).
    uint small = 127;
    KJ_SYSCALL(setsockopt(fds[0], SOL_SOCKET, SO_RCVBUF, &small, sizeof(small)));
    KJ_SYSCALL(setsockopt(fds[1], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)));
  }
  ~SerializeAsyncTest() {
    close(fds[0]);
    close(fds[1]);
  }
};

TEST_F(SerializeAsyncTest, ParseAsync) {
  auto ioContext = kj::setupAsyncIo();
  auto input = ioContext.lowLevelProvider->wrapInputFd(fds[0]);
  kj::FdOutputStream rawOutput(fds[1]);
  FragmentingOutputStream output(rawOutput);

  TestMessageBuilder message(1);
  initTestMessage(message.getRoot<TestAllTypes>());

  kj::Thread thread([&]() {
    writeMessage(output, message);
  });

  auto received = readMessage(*input).wait(ioContext.waitScope);

  checkTestMessage(received->getRoot<TestAllTypes>());
}

TEST_F(SerializeAsyncTest, ParseAsyncOddSegmentCount) {
  auto ioContext = kj::setupAsyncIo();
  auto input = ioContext.lowLevelProvider->wrapInputFd(fds[0]);
  kj::FdOutputStream rawOutput(fds[1]);
  FragmentingOutputStream output(rawOutput);

  TestMessageBuilder message(7);
  initTestMessage(message.getRoot<TestAllTypes>());

  kj::Thread thread([&]() {
    writeMessage(output, message);
  });

  auto received = readMessage(*input).wait(ioContext.waitScope);

  checkTestMessage(received->getRoot<TestAllTypes>());
}

TEST_F(SerializeAsyncTest, ParseAsyncEvenSegmentCount) {
  auto ioContext = kj::setupAsyncIo();
  auto input = ioContext.lowLevelProvider->wrapInputFd(fds[0]);
  kj::FdOutputStream rawOutput(fds[1]);
  FragmentingOutputStream output(rawOutput);

  TestMessageBuilder message(10);
  initTestMessage(message.getRoot<TestAllTypes>());

  kj::Thread thread([&]() {
    writeMessage(output, message);
  });

  auto received = readMessage(*input).wait(ioContext.waitScope);

  checkTestMessage(received->getRoot<TestAllTypes>());
}

TEST_F(SerializeAsyncTest, WriteAsync) {
  auto ioContext = kj::setupAsyncIo();
  auto output = ioContext.lowLevelProvider->wrapOutputFd(fds[1]);

  TestMessageBuilder message(1);
  auto root = message.getRoot<TestAllTypes>();
  auto list = root.initStructList(16);
  for (auto element: list) {
    initTestMessage(element);
  }

  kj::Thread thread([&]() {
    StreamFdMessageReader reader(fds[0]);
    auto listReader = reader.getRoot<TestAllTypes>().getStructList();
    EXPECT_EQ(list.size(), listReader.size());
    for (auto element: listReader) {
      checkTestMessage(element);
    }
  });

  writeMessage(*output, message).wait(ioContext.waitScope);
}

TEST_F(SerializeAsyncTest, WriteAsyncOddSegmentCount) {
  auto ioContext = kj::setupAsyncIo();
  auto output = ioContext.lowLevelProvider->wrapOutputFd(fds[1]);

  TestMessageBuilder message(7);
  auto root = message.getRoot<TestAllTypes>();
  auto list = root.initStructList(16);
  for (auto element: list) {
    initTestMessage(element);
  }

  kj::Thread thread([&]() {
    StreamFdMessageReader reader(fds[0]);
    auto listReader = reader.getRoot<TestAllTypes>().getStructList();
    EXPECT_EQ(list.size(), listReader.size());
    for (auto element: listReader) {
      checkTestMessage(element);
    }
  });

  writeMessage(*output, message).wait(ioContext.waitScope);
}

TEST_F(SerializeAsyncTest, WriteAsyncEvenSegmentCount) {
  auto ioContext = kj::setupAsyncIo();
  auto output = ioContext.lowLevelProvider->wrapOutputFd(fds[1]);

  TestMessageBuilder message(10);
  auto root = message.getRoot<TestAllTypes>();
  auto list = root.initStructList(16);
  for (auto element: list) {
    initTestMessage(element);
  }

  kj::Thread thread([&]() {
    StreamFdMessageReader reader(fds[0]);
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
