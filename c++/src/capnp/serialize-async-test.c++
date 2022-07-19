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
#include <fcntl.h>
#include <errno.h>
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

class AsyncMultiArrayInputStream final : public kj::AsyncIoStream {
public:
  AsyncMultiArrayInputStream(kj::Array<kj::Array<kj::byte>> batches, uint64_t maxBytesLimit)
    : batches(kj::mv(batches)), maxBytesLimit(maxBytesLimit) {}
  // maxBytesLimit allows you to set the maximum read size overriding what is provided as maxBytes to
  // tryRead, to emulate the buffer of a real socket.

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    size_t bytesRead = 0;
    maxBytes = kj::min(maxBytesLimit, maxBytes);

    while (minBytes > 0 && maxBytes > 0 && currentBatch < batches.size()) {
      auto& batch = batches[currentBatch];
      auto start = batch.begin() + positionInCurrentBatch;
      auto bytesReadThisIteration = kj::min(batch.size() - positionInCurrentBatch, maxBytes);

      auto floorSub = [](size_t a, size_t b) -> size_t {
        if (b > a) {
          return 0;
        } else {
          return a - b;
        }
      };
      minBytes = floorSub(minBytes, bytesReadThisIteration);
      maxBytes = floorSub(maxBytes, bytesReadThisIteration);

      positionInCurrentBatch += bytesReadThisIteration;
      KJ_ASSERT(positionInCurrentBatch <= batch.size());

      memcpy(reinterpret_cast<byte*>(buffer) + bytesRead, start, bytesReadThisIteration);
      bytesRead += bytesReadThisIteration;

      if (positionInCurrentBatch == batch.size()) {
        currentBatch += 1;
        positionInCurrentBatch = 0;
      }
    }

    return bytesRead;
  }

  kj::Promise<void> write(const void*, size_t) override { KJ_UNIMPLEMENTED("not writable"); }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>>) override {
    KJ_UNIMPLEMENTED("not writable");
  }
  kj::Promise<void> whenWriteDisconnected() override { KJ_UNIMPLEMENTED("not writable"); }
  void shutdownWrite() override { KJ_UNIMPLEMENTED("not writable"); }

private:
  kj::Array<kj::Array<kj::byte>> batches;
  uint64_t maxBytesLimit;
  size_t currentBatch = 0;
  size_t positionInCurrentBatch = 0;
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

// TEST(SerializeAsyncTest, ParseAsync) {
//   PipeWithSmallBuffer fds;
//   auto ioContext = kj::setupAsyncIo();
//   auto input = ioContext.lowLevelProvider->wrapInputFd(fds[0]);
//   SocketOutputStream rawOutput(fds[1]);
//   FragmentingOutputStream output(rawOutput);

//   TestMessageBuilder message(1);
//   initTestMessage(message.getRoot<TestAllTypes>());

//   kj::Thread thread([&]() {
//     writeMessage(output, message);
//   });

//   auto received = readMessage(*input).wait(ioContext.waitScope);

//   checkTestMessage(received->getRoot<TestAllTypes>());
// }

// TEST(SerializeAsyncTest, ParseAsyncOddSegmentCount) {
//   PipeWithSmallBuffer fds;
//   auto ioContext = kj::setupAsyncIo();
//   auto input = ioContext.lowLevelProvider->wrapInputFd(fds[0]);
//   SocketOutputStream rawOutput(fds[1]);
//   FragmentingOutputStream output(rawOutput);

//   TestMessageBuilder message(7);
//   initTestMessage(message.getRoot<TestAllTypes>());

//   kj::Thread thread([&]() {
//     writeMessage(output, message);
//   });

//   auto received = readMessage(*input).wait(ioContext.waitScope);

//   checkTestMessage(received->getRoot<TestAllTypes>());
// }

// TEST(SerializeAsyncTest, ParseAsyncEvenSegmentCount) {
//   PipeWithSmallBuffer fds;
//   auto ioContext = kj::setupAsyncIo();
//   auto input = ioContext.lowLevelProvider->wrapInputFd(fds[0]);
//   SocketOutputStream rawOutput(fds[1]);
//   FragmentingOutputStream output(rawOutput);

//   TestMessageBuilder message(10);
//   initTestMessage(message.getRoot<TestAllTypes>());

//   kj::Thread thread([&]() {
//     writeMessage(output, message);
//   });

//   auto received = readMessage(*input).wait(ioContext.waitScope);

//   checkTestMessage(received->getRoot<TestAllTypes>());
// }

// TEST(SerializeAsyncTest, ParseBufferedAsyncFullBuffer) {
//   PipeWithSmallBuffer fds;
//   auto ioContext = kj::setupAsyncIo();
//   auto rawStream = ioContext.lowLevelProvider->wrapSocketFd(fds[0]);
//   SocketOutputStream rawOutput(fds[1]);
//   kj::BufferedOutputStreamWrapper output(rawOutput);

//   TestMessageBuilder message(10);
//   initTestMessage(message.getRoot<TestAllTypes>());

//   kj::Thread thread([&]() {
//     for(int i = 0; i < 5; i++) {
//       writeMessage(output, message);
//     };
//     output.flush();
//   });

//   [&]() noexcept { // forces test process to abort on exception without waiting for the thread

//   BufferedAsyncIoMessageStream stream(*rawStream);

//   for(int i = 0; i < 5; i++) {
//     auto maybeMessage = stream.tryReadMessage(nullptr).wait(ioContext.waitScope);
//     auto& received = KJ_ASSERT_NONNULL(maybeMessage);

//     checkTestMessage(received.reader->getRoot<TestAllTypes>());
//   }

//   }(); // noexcept
// }

// TEST(SerializeAsyncTest, ParseBufferedAsyncFragmented) {
//   PipeWithSmallBuffer fds;
//   auto ioContext = kj::setupAsyncIo();
//   auto rawStream = ioContext.lowLevelProvider->wrapSocketFd(fds[0]);
//   SocketOutputStream rawOutput(fds[1]);
//   FragmentingOutputStream output(rawOutput);

//   TestMessageBuilder message(10);
//   initTestMessage(message.getRoot<TestAllTypes>());

//   kj::Thread thread([&]() {
//     for(int i = 0; i < 5; i++) {
//       writeMessage(output, message);
//     };
//   });

//   [&]() noexcept { // forces test process to abort on exception without waiting for the thread

//   BufferedAsyncIoMessageStream stream(*rawStream);

//   for(int i = 0; i < 5; i++) {
//     auto maybeMessage = stream.tryReadMessage(nullptr).wait(ioContext.waitScope);
//     auto& received = KJ_ASSERT_NONNULL(maybeMessage);

//     checkTestMessage(received.reader->getRoot<TestAllTypes>());
//   }

//   }(); // noexcept
// }

// TEST(SerializeAsyncTest, ParseBufferedAsyncBoundaryCases) {
//   auto ioContext = kj::setupAsyncIo();

//   auto messageBytes = [](auto numSegments) {
//     TestMessageBuilder builder(numSegments);
//     initTestMessage(builder.getRoot<TestAllTypes>());
//     return messageToFlatArray(builder);
//   };

//   auto evenMessage = messageBytes(8);
//   auto oddMessage = messageBytes(7);

//   kj::Vector<kj::Array<kj::byte>> batches;
//   auto numMessages = 0;

//   auto addBatchesFromMessage = [&](std::initializer_list<size_t> splits) {
//     auto message = ((numMessages++ % 2) ? evenMessage : oddMessage).asBytes();
//     size_t start = 0;
//     for(auto split : splits) {
//       KJ_ASSERT(split < message.size());
//       batches.add(kj::heapArray(message.slice(start, split)));
//       start = split;
//     }
//     batches.add(kj::heapArray(message.slice(start, message.size())));
//   };

//   auto addCompleteMessage = [&]() {
//     auto message = ((numMessages++ % 2) ? evenMessage : oddMessage).asBytes();
//     batches.add(kj::heapArray(message));
//   };

//   // Read message in byte chunks
//   addBatchesFromMessage({ 3, 7, 11, 13 });

//   // // Read message in one shot
//   addCompleteMessage();

//   // Read complete message after word chunked message
//   addBatchesFromMessage({ sizeof(word), sizeof(word) * 4 });
//   addCompleteMessage();

//   // Read multiple messages after word chunked message
//   addBatchesFromMessage({ sizeof(word) * 4, sizeof(word) * 8 });
//   addCompleteMessage();
//   addCompleteMessage();
//   addCompleteMessage();

//   // Read multiple chunked messages in a row
//   addBatchesFromMessage({ sizeof(word), sizeof(word) * 4 });
//   addBatchesFromMessage({ sizeof(word) * 4, sizeof(word) * 8 });
//   addBatchesFromMessage({ 3, 7, 11, 13 });
//   addBatchesFromMessage({ sizeof(word) * 5, sizeof(word) * 7 });

//   // Read multiple messages after byte chunked message
//   addBatchesFromMessage({ 3, 7, 11, 13 });
//   addCompleteMessage();
//   addCompleteMessage();
//   addCompleteMessage();

//   AsyncMultiArrayInputStream rawStream(batches.releaseAsArray(), 65536);
//   BufferedAsyncIoMessageStream stream(rawStream);

//   for(auto i = 0; i < numMessages; i++) {
//     auto maybeMessage = stream.tryReadMessage(nullptr).wait(ioContext.waitScope);
//     auto& received = KJ_ASSERT_NONNULL(maybeMessage);

//     checkTestMessage(received.reader->getRoot<TestAllTypes>());
//   }
// }

// TEST(SerializeAsyncTest, ParseBufferedSmallSocketBuffer) {
//   auto ioContext = kj::setupAsyncIo();

//   auto batches = kj::heapArrayBuilder<kj::Array<byte>>(1);
//   TestMessageBuilder builder(10);
//   initTestMessage(builder.getRoot<TestAllTypes>());
//   batches.add(kj::heapArray(messageToFlatArray(builder).asBytes()));

//   constexpr uint64_t smallBufferSize = 7;
//   KJ_ASSERT(batches[0].size() > smallBufferSize);

//   AsyncMultiArrayInputStream rawStream(batches.finish(), smallBufferSize);
//   BufferedAsyncIoMessageStream stream(rawStream);

//   auto maybeMessage = stream.tryReadMessage(nullptr).wait(ioContext.waitScope);
//   auto& received = KJ_ASSERT_NONNULL(maybeMessage);

//   checkTestMessage(received.reader->getRoot<TestAllTypes>());
// }

// TEST(SerializeAsyncTest, ParseBufferedSmallStreamBuffer) {
//   auto ioContext = kj::setupAsyncIo();

//   auto batches = kj::heapArrayBuilder<kj::Array<byte>>(1);
//   TestMessageBuilder builder(10);
//   initTestMessage(builder.getRoot<TestAllTypes>());
//   batches.add(kj::heapArray(messageToFlatArray(builder).asBytes()));

//   constexpr uint64_t smallBufferSize = 7;
//   KJ_ASSERT(batches[0].size() > smallBufferSize);

//   AsyncMultiArrayInputStream rawStream(batches.finish(), 65536);
//   BufferedAsyncIoMessageStream stream(rawStream, { smallBufferSize });

//   auto maybeMessage = stream.tryReadMessage(nullptr).wait(ioContext.waitScope);
//   auto& received = KJ_ASSERT_NONNULL(maybeMessage);

//   checkTestMessage(received.reader->getRoot<TestAllTypes>());
// }

#if !_WIN32 && !__CYGWIN__
TEST(SerializeAsyncTest, ParseBufferedAsyncFullBufferWithCaps) {
  PipeWithSmallBuffer capFds;

  auto ioContext = kj::setupAsyncIo();
  auto rawCapStreamA = ioContext.lowLevelProvider->wrapUnixSocketFd(capFds[0]);


  int pipeFds[2];
  KJ_SYSCALL(kj::miniposix::pipe(pipeFds));
  kj::AutoCloseFd in1(pipeFds[0]);
  kj::AutoCloseFd out1(pipeFds[1]);

  KJ_SYSCALL(kj::miniposix::pipe(pipeFds));
  kj::AutoCloseFd in2(pipeFds[0]);
  kj::AutoCloseFd out2(pipeFds[1]);

  TestMessageBuilder message(10);
  initTestMessage(message.getRoot<TestAllTypes>());

  kj::Thread thread([&capFds, &message, out1 = kj::mv(out1), out2 = kj::mv(out2)]() mutable noexcept {
    auto ioContext = kj::setupAsyncIo();
    auto rawCapStreamB = ioContext.lowLevelProvider->wrapUnixSocketFd(capFds[1]);

    auto sendFd = [&ioContext, &rawCapStreamB, &message](kj::AutoCloseFd fd) {
      for(int i = 0; i < 4; i++) {
        writeMessage(*rawCapStreamB, message).wait(ioContext.waitScope);
      }
      int sendFds[1] = { fd.get() };
      writeMessage(*rawCapStreamB, sendFds, message).wait(ioContext.waitScope);
    };

    sendFd(kj::mv(out1));
    sendFd(kj::mv(out2));
  });

  BufferedAsyncCapabilityMessageStream streamA(*rawCapStreamA,
      BufferedCapabilityStreamOptions{ {}, 2 });

  auto receiveFd = [&ioContext, &streamA](auto expectedInFd) noexcept {
    for(int i = 0; i < 4; i++) {
      auto result = KJ_ASSERT_NONNULL(streamA.tryReadMessage(nullptr).wait(ioContext.waitScope));
      KJ_EXPECT(result.fds.size() == 0);
      checkTestMessage(result.reader->getRoot<TestAllTypes>());
    }

    {
      kj::Array<kj::AutoCloseFd> fdSpace = kj::heapArray<kj::AutoCloseFd>(1);
      auto result = KJ_ASSERT_NONNULL(streamA.tryReadMessage(fdSpace).wait(ioContext.waitScope));
      KJ_ASSERT(result.fds.size() == 1);
      checkTestMessage(result.reader->getRoot<TestAllTypes>());

      kj::FdOutputStream(kj::mv(result.fds[0])).write("bar", 3);
    }

    // We want to verify that the outFd closed without deadlocking if it wasn't. So, use nonblocking
    // mode for our read()s.
    KJ_SYSCALL(fcntl(expectedInFd, F_SETFL, O_NONBLOCK));

    char buffer[4];
    ssize_t n;

    // First we read "bar" from in1.
    KJ_NONBLOCKING_SYSCALL(n = read(expectedInFd, buffer, 4));
    KJ_ASSERT(n == 3);
    buffer[3] = '\0';
    KJ_ASSERT(kj::StringPtr(buffer) == "bar");

    // Now it should be EOF.
    KJ_NONBLOCKING_SYSCALL(n = read(expectedInFd, buffer, 4));
    if (n < 0) {
      KJ_FAIL_ASSERT("out1 was not closed", n, strerror(errno));
    }
    KJ_ASSERT(n == 0);
  };

  receiveFd(kj::mv(in1));
  receiveFd(kj::mv(in2));
}
#endif

// TEST(SerializeAsyncTest, WriteAsync) {
//   PipeWithSmallBuffer fds;
//   auto ioContext = kj::setupAsyncIo();
//   auto output = ioContext.lowLevelProvider->wrapOutputFd(fds[1]);

//   TestMessageBuilder message(1);
//   auto root = message.getRoot<TestAllTypes>();
//   auto list = root.initStructList(16);
//   for (auto element: list) {
//     initTestMessage(element);
//   }

//   kj::Thread thread([&]() {
//     SocketInputStream input(fds[0]);
//     InputStreamMessageReader reader(input);
//     auto listReader = reader.getRoot<TestAllTypes>().getStructList();
//     EXPECT_EQ(list.size(), listReader.size());
//     for (auto element: listReader) {
//       checkTestMessage(element);
//     }
//   });

//   writeMessage(*output, message).wait(ioContext.waitScope);
// }

// TEST(SerializeAsyncTest, WriteAsyncOddSegmentCount) {
//   PipeWithSmallBuffer fds;
//   auto ioContext = kj::setupAsyncIo();
//   auto output = ioContext.lowLevelProvider->wrapOutputFd(fds[1]);

//   TestMessageBuilder message(7);
//   auto root = message.getRoot<TestAllTypes>();
//   auto list = root.initStructList(16);
//   for (auto element: list) {
//     initTestMessage(element);
//   }

//   kj::Thread thread([&]() {
//     SocketInputStream input(fds[0]);
//     InputStreamMessageReader reader(input);
//     auto listReader = reader.getRoot<TestAllTypes>().getStructList();
//     EXPECT_EQ(list.size(), listReader.size());
//     for (auto element: listReader) {
//       checkTestMessage(element);
//     }
//   });

//   writeMessage(*output, message).wait(ioContext.waitScope);
// }

// TEST(SerializeAsyncTest, WriteAsyncEvenSegmentCount) {
//   PipeWithSmallBuffer fds;
//   auto ioContext = kj::setupAsyncIo();
//   auto output = ioContext.lowLevelProvider->wrapOutputFd(fds[1]);

//   TestMessageBuilder message(10);
//   auto root = message.getRoot<TestAllTypes>();
//   auto list = root.initStructList(16);
//   for (auto element: list) {
//     initTestMessage(element);
//   }

//   kj::Thread thread([&]() {
//     SocketInputStream input(fds[0]);
//     InputStreamMessageReader reader(input);
//     auto listReader = reader.getRoot<TestAllTypes>().getStructList();
//     EXPECT_EQ(list.size(), listReader.size());
//     for (auto element: listReader) {
//       checkTestMessage(element);
//     }
//   });

//   writeMessage(*output, message).wait(ioContext.waitScope);
// }

// TEST(SerializeAsyncTest, WriteMultipleMessagesAsync) {
//   PipeWithSmallBuffer fds;
//   auto ioContext = kj::setupAsyncIo();
//   auto output = ioContext.lowLevelProvider->wrapOutputFd(fds[1]);

//   const int numMessages = 5;
//   const int baseListSize = 16;
//   auto messages = kj::heapArrayBuilder<TestMessageBuilder>(numMessages);
//   for (int i = 0; i < numMessages; ++i) {
//     messages.add(i+1);
//     auto root = messages[i].getRoot<TestAllTypes>();
//     auto list = root.initStructList(baseListSize+i);
//     for (auto element: list) {
//       initTestMessage(element);
//     }
//   }

//   kj::Thread thread([&]() {
//     SocketInputStream input(fds[0]);
//     for (int i = 0; i < numMessages; ++i) {
//       InputStreamMessageReader reader(input);
//       auto listReader = reader.getRoot<TestAllTypes>().getStructList();
//       EXPECT_EQ(baseListSize+i, listReader.size());
//       for (auto element: listReader) {
//         checkTestMessage(element);
//       }
//     }
//   });

//   auto msgs = kj::heapArray<capnp::MessageBuilder*>(numMessages);
//   for (int i = 0; i < numMessages; ++i) {
//     msgs[i] = &messages[i];
//   }
//   writeMessages(*output, msgs).wait(ioContext.waitScope);
// }

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
