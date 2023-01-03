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

#if _WIN32
#include <kj/win32-api-version.h>
#endif

#include "serialize-async.h"
#include "serialize.h"
#include <kj/debug.h>
#include <kj/thread.h>
#include <stdlib.h>
#include <kj/miniposix.h>
#include "test-util.h"
#include <kj/compat/gtest.h>
#include <kj/io.h>

#if _WIN32
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

TEST(SerializeAsyncTest, WriteMultipleMessagesAsync) {
  PipeWithSmallBuffer fds;
  auto ioContext = kj::setupAsyncIo();
  auto output = ioContext.lowLevelProvider->wrapOutputFd(fds[1]);

  const int numMessages = 5;
  const int baseListSize = 16;
  auto messages = kj::heapArrayBuilder<TestMessageBuilder>(numMessages);
  for (int i = 0; i < numMessages; ++i) {
    messages.add(i+1);
    auto root = messages[i].getRoot<TestAllTypes>();
    auto list = root.initStructList(baseListSize+i);
    for (auto element: list) {
      initTestMessage(element);
    }
  }

  kj::Thread thread([&]() {
    SocketInputStream input(fds[0]);
    for (int i = 0; i < numMessages; ++i) {
      InputStreamMessageReader reader(input);
      auto listReader = reader.getRoot<TestAllTypes>().getStructList();
      EXPECT_EQ(baseListSize+i, listReader.size());
      for (auto element: listReader) {
        checkTestMessage(element);
      }
    }
  });

  auto msgs = kj::heapArray<capnp::MessageBuilder*>(numMessages);
  for (int i = 0; i < numMessages; ++i) {
    msgs[i] = &messages[i];
  }
  writeMessages(*output, msgs).wait(ioContext.waitScope);
}

void writeSmallMessage(kj::OutputStream& output, kj::StringPtr text) {
  capnp::MallocMessageBuilder message;
  message.getRoot<test::TestAnyPointer>().getAnyPointerField().setAs<Text>(text);
  writeMessage(output, message);
}

void expectSmallMessage(MessageStream& stream, kj::StringPtr text, kj::WaitScope& waitScope) {
  auto msg = stream.readMessage().wait(waitScope);
  KJ_EXPECT(msg->getRoot<test::TestAnyPointer>().getAnyPointerField().getAs<Text>() == text);
}

void writeBigMessage(kj::OutputStream& output) {
  capnp::MallocMessageBuilder message(4);  // first segment is small
  initTestMessage(message.getRoot<test::TestAllTypes>());
  writeMessage(output, message);
}

void expectBigMessage(MessageStream& stream, kj::WaitScope& waitScope) {
  auto msg = stream.readMessage().wait(waitScope);
  checkTestMessage(msg->getRoot<test::TestAllTypes>());
}

KJ_TEST("BufferedMessageStream basics") {
  // Encode input data.
  kj::VectorOutputStream data;

  writeSmallMessage(data, "foo");

  KJ_EXPECT(data.getArray().size() / sizeof(word) == 4);

  // A big message (more than half a buffer)
  writeBigMessage(data);

  KJ_EXPECT(data.getArray().size() / sizeof(word) > 16);

  writeSmallMessage(data, "bar");
  writeSmallMessage(data, "baz");
  writeSmallMessage(data, "qux");

  // Run the test.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto pipe = kj::newTwoWayPipe();
  auto writePromise = pipe.ends[1]->write(data.getArray().begin(), data.getArray().size());

  uint callbackCallCount = 0;
  auto callback = [&](MessageReader& reader) {
    ++callbackCallCount;
    return false;
  };

  BufferedMessageStream stream(*pipe.ends[0], callback, 16);
  expectSmallMessage(stream, "foo", waitScope);
  KJ_EXPECT(callbackCallCount == 1);

  KJ_EXPECT(!writePromise.poll(waitScope));

  expectBigMessage(stream, waitScope);
  KJ_EXPECT(callbackCallCount == 1);  // no callback on big message

  KJ_EXPECT(!writePromise.poll(waitScope));

  expectSmallMessage(stream, "bar", waitScope);
  KJ_EXPECT(callbackCallCount == 2);

  // All data is now in the buffer, so this part is done.
  KJ_EXPECT(writePromise.poll(waitScope));

  expectSmallMessage(stream, "baz", waitScope);
  expectSmallMessage(stream, "qux", waitScope);
  KJ_EXPECT(callbackCallCount == 4);

  auto eofPromise = stream.MessageStream::tryReadMessage();
  KJ_EXPECT(!eofPromise.poll(waitScope));

  pipe.ends[1]->shutdownWrite();
  KJ_EXPECT(eofPromise.wait(waitScope) == nullptr);
}

KJ_TEST("BufferedMessageStream fragmented reads") {
  // Encode input data.
  kj::VectorOutputStream data;
  writeBigMessage(data);

  // Run the test.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto pipe = kj::newTwoWayPipe();
  auto callback = [&](MessageReader& reader) {
    return false;
  };
  BufferedMessageStream stream(*pipe.ends[0], callback, 16);

  // Arrange to read a big message.
  auto readPromise = stream.MessageStream::tryReadMessage();
  KJ_EXPECT(!readPromise.poll(waitScope));

  auto remainingData = data.getArray();

  // Write 5 bytes. This won't even fulfill the first read's minBytes.
  pipe.ends[1]->write(remainingData.begin(), 5).wait(waitScope);
  remainingData = remainingData.slice(5, remainingData.size());
  KJ_EXPECT(!readPromise.poll(waitScope));

  // Write 4 more. Now the MessageStream will only see the first word which contains the first
  // segment size. This size is small so the MessageStream won't yet fall back to
  // readEntireMessage().
  pipe.ends[1]->write(remainingData.begin(), 4).wait(waitScope);
  remainingData = remainingData.slice(4, remainingData.size());
  KJ_EXPECT(!readPromise.poll(waitScope));

  // Drip 10 more bytes. Now the MessageStream will realize that it needs to try
  // readEntireMessage().
  pipe.ends[1]->write(remainingData.begin(), 10).wait(waitScope);
  remainingData = remainingData.slice(10, remainingData.size());
  KJ_EXPECT(!readPromise.poll(waitScope));

  // Give it all except the last byte.
  pipe.ends[1]->write(remainingData.begin(), remainingData.size() - 1).wait(waitScope);
  remainingData = remainingData.slice(remainingData.size() - 1, remainingData.size());
  KJ_EXPECT(!readPromise.poll(waitScope));

  // Finish it off.
  pipe.ends[1]->write(remainingData.begin(), 1).wait(waitScope);
  KJ_ASSERT(readPromise.poll(waitScope));

  auto msg = readPromise.wait(waitScope);
  checkTestMessage(KJ_ASSERT_NONNULL(msg)->getRoot<test::TestAllTypes>());
}

KJ_TEST("BufferedMessageStream many small messages") {
  // Encode input data.
  kj::VectorOutputStream data;

  for (auto i: kj::zeroTo(16)) {
    // Intentionally make these 5 words each so they cross buffer boundaries.
    writeSmallMessage(data, kj::str("12345678-", i));
    KJ_EXPECT(data.getArray().size() / sizeof(word) == (i+1) * 5);
  }

  // Run the test.
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto pipe = kj::newTwoWayPipe();
  auto writePromise = pipe.ends[1]->write(data.getArray().begin(), data.getArray().size())
      .then([&]() {
    // Write some garbage at the end.
    return pipe.ends[1]->write("bogus", 5);
  }).then([&]() {
    // EOF.
    return pipe.ends[1]->shutdownWrite();
  }).eagerlyEvaluate(nullptr);

  uint callbackCallCount = 0;
  auto callback = [&](MessageReader& reader) {
    ++callbackCallCount;
    return false;
  };

  BufferedMessageStream stream(*pipe.ends[0], callback, 16);

  for (auto i: kj::zeroTo(16)) {
    // Intentionally make these 5 words each so they cross buffer boundaries.
    expectSmallMessage(stream, kj::str("12345678-", i), waitScope);
    KJ_EXPECT(callbackCallCount == i + 1);
  }

  KJ_EXPECT_THROW(DISCONNECTED, stream.MessageStream::tryReadMessage().wait(waitScope));
  KJ_EXPECT(callbackCallCount == 16);
}

// TODO(test): We should probably test BufferedMessageStream's FD handling here... but really it
//   gets tested well enough by rpc-twoparty-test.

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
