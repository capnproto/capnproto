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
#include <kj/async-unix.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
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
      size_t n = rand() % size + 1;
      inner.write(buffer, n);
      usleep(5);
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
    KJ_SYSCALL(pipe(fds));

#ifdef F_SETPIPE_SZ
    // Force pipe to be small, to test what happens when the write buffer fills up.  Note that
    // Linux rounds this up to the page size, so we'll still need a large message to hit the limit.
    KJ_SYSCALL(fcntl(fds[1], F_SETPIPE_SZ, (int)64));
#endif
  }
  ~SerializeAsyncTest() {
    close(fds[0]);
    close(fds[1]);
  }
};

TEST_F(SerializeAsyncTest, ParseAsync) {
  kj::UnixEventLoop loop;

  auto input = kj::AsyncInputStream::wrapFd(fds[0]);
  kj::FdOutputStream rawOutput(fds[1]);
  FragmentingOutputStream output(rawOutput);

  TestMessageBuilder message(1);
  initTestMessage(message.getRoot<TestAllTypes>());

  auto promise = loop.evalLater([&]() {
    return readMessage(*input);
  });

  kj::Thread thread([&]() {
    writeMessage(output, message);
  });

  auto received = loop.wait(kj::mv(promise));
  checkTestMessage(received->getRoot<TestAllTypes>());
}

TEST_F(SerializeAsyncTest, ParseAsyncOddSegmentCount) {
  kj::UnixEventLoop loop;

  auto input = kj::AsyncInputStream::wrapFd(fds[0]);
  kj::FdOutputStream rawOutput(fds[1]);
  FragmentingOutputStream output(rawOutput);

  TestMessageBuilder message(7);
  initTestMessage(message.getRoot<TestAllTypes>());

  auto promise = loop.evalLater([&]() {
    return readMessage(*input);
  });

  kj::Thread thread([&]() {
    writeMessage(output, message);
  });

  auto received = loop.wait(kj::mv(promise));
  checkTestMessage(received->getRoot<TestAllTypes>());
}

TEST_F(SerializeAsyncTest, ParseAsyncEvenSegmentCount) {
  kj::UnixEventLoop loop;

  auto input = kj::AsyncInputStream::wrapFd(fds[0]);
  kj::FdOutputStream rawOutput(fds[1]);
  FragmentingOutputStream output(rawOutput);

  TestMessageBuilder message(10);
  initTestMessage(message.getRoot<TestAllTypes>());

  auto promise = loop.evalLater([&]() {
    return readMessage(*input);
  });

  kj::Thread thread([&]() {
    writeMessage(output, message);
  });

  auto received = loop.wait(kj::mv(promise));
  checkTestMessage(received->getRoot<TestAllTypes>());
}

TEST_F(SerializeAsyncTest, WriteAsync) {
  kj::UnixEventLoop loop;

  auto output = kj::AsyncOutputStream::wrapFd(fds[1]);

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

  loop.wait(loop.evalLater([&]() {
    return writeMessage(*output, message);
  }));
}

TEST_F(SerializeAsyncTest, WriteAsyncOddSegmentCount) {
  kj::UnixEventLoop loop;

  auto output = kj::AsyncOutputStream::wrapFd(fds[1]);

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

  loop.wait(loop.evalLater([&]() {
    return writeMessage(*output, message);
  }));
}

TEST_F(SerializeAsyncTest, WriteAsyncEvenSegmentCount) {
  kj::UnixEventLoop loop;

  auto output = kj::AsyncOutputStream::wrapFd(fds[1]);

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

  loop.wait(loop.evalLater([&]() {
    return writeMessage(*output, message);
  }));
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
