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
