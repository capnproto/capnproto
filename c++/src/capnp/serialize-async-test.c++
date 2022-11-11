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
#include <kj/map.h>
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

class AsyncMultiArrayInputStream final : public kj::AsyncCapabilityStream {
public:
  struct Batch {
    kj::Array<kj::byte> bytes;
    kj::Maybe<kj::AutoCloseFd> fd = nullptr;
  };

  AsyncMultiArrayInputStream(kj::Array<Batch> batches, uint64_t maxBytesLimit)
    : batches(kj::mv(batches)), maxBytesLimit(maxBytesLimit) {}
  // maxBytesLimit allows you to set the maximum read size overriding what is provided as maxBytes to
  // tryRead, to emulate the buffer of a real socket.

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return tryReadWithFds(buffer, minBytes, maxBytes, nullptr, 0).then([](ReadResult result) {
      return result.byteCount;
    });
  }

  kj::Promise<ReadResult> tryReadWithFds(void* buffer, size_t minBytes, size_t maxBytes,
                                         kj::AutoCloseFd* fdBuffer, size_t maxFds) override {
    KJ_ASSERT(maxBytes >= minBytes);

    size_t bytesRead = 0;
    size_t fdsRead = 0;
    // We have to read at least minBytes, otherwise the caller will think they hit EOF
    maxBytes = kj::max(minBytes, kj::min(maxBytesLimit, maxBytes));

    while (minBytes > 0 && maxBytes > 0 && currentBatch < batches.size()) {
      // Read bytes from batches until at least minBytes have been read.
      // If minBytes has been satisfied, the read stops at the next batch boundary or maxBytes,
      // whichever comes first

      auto& batch = batches[currentBatch];

      auto start = batch.bytes.begin() + positionInCurrentBatch;
      auto bytesReadThisIteration = kj::min(batch.bytes.size() - positionInCurrentBatch, maxBytes);

      auto boundedSub = [](size_t a, size_t b) -> size_t {
        // Returns a - b or 0 if a - b < 0;

        if (b > a) {
          return 0;
        } else {
          return a - b;
        }
      };
      minBytes = boundedSub(minBytes, bytesReadThisIteration);
      maxBytes = boundedSub(maxBytes, bytesReadThisIteration);

      positionInCurrentBatch += bytesReadThisIteration;
      KJ_ASSERT(positionInCurrentBatch <= batch.bytes.size());

      // Excess FDs are discarded, no need to track position of current FD in the batch

      memcpy(reinterpret_cast<byte*>(buffer) + bytesRead, start, bytesReadThisIteration);
      bytesRead += bytesReadThisIteration;

      if (maxFds) {
        KJ_IF_MAYBE(fd, batch.fd) {
          fdBuffer[fdsRead] = kj::mv(*fd);
          fdsRead++;
          maxFds = boundedSub(maxFds, 1);
        }
      }

      if (positionInCurrentBatch == batch.bytes.size()) {
        currentBatch += 1;
        positionInCurrentBatch = 0;
      }

      if (minBytes == 0) break;
    }

    return ReadResult { bytesRead, fdsRead };
  }

  kj::Promise<ReadResult> tryReadWithStreams(void*, size_t, size_t, kj::Own<AsyncCapabilityStream>*,
      size_t) override {
    KJ_UNIMPLEMENTED("unimplemented");
  }
  kj::Promise<void> write(const void*, size_t) override { KJ_UNIMPLEMENTED("not writable"); }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>>) override {
    KJ_UNIMPLEMENTED("not writable");
  }
  kj::Promise<void> writeWithFds(kj::ArrayPtr<const byte>,
                                 kj::ArrayPtr<const kj::ArrayPtr<const byte>>,
                                 kj::ArrayPtr<const int>) override {
    KJ_UNIMPLEMENTED("not writable");
  }
  kj::Promise<void> writeWithStreams(kj::ArrayPtr<const byte>,
                                     kj::ArrayPtr<const kj::ArrayPtr<const byte>>,
                                     kj::Array<kj::Own<kj::AsyncCapabilityStream>>) override {
    KJ_UNIMPLEMENTED("not writable");
  }
  kj::Promise<void> whenWriteDisconnected() override { KJ_UNIMPLEMENTED("not writable"); }
  void shutdownWrite() override { KJ_UNIMPLEMENTED("not writable"); }

private:
  kj::Array<Batch> batches;
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

kj::Own<MessageStream> makeBufferedStream(kj::AsyncIoStream& stream) {
  return kj::heap<BufferedMessageStream>(stream, [](MessageReader&) { return true; });
}
kj::Own<MessageStream> makeBufferedStream(kj::AsyncIoStream& stream, size_t size) {
  return kj::heap<BufferedMessageStream>(stream, [](MessageReader&) { return true; }, size);
}
kj::Own<MessageStream> makeBufferedStream(kj::AsyncCapabilityStream& stream) {
  return kj::heap<BufferedMessageStream>(stream, [](MessageReader&) { return true; });
}

TEST(SerializeAsyncTest, ParseBufferedAsyncFullBuffer) {
  PipeWithSmallBuffer fds;
  auto ioContext = kj::setupAsyncIo();
  auto rawStream = ioContext.lowLevelProvider->wrapSocketFd(fds[0]);
  SocketOutputStream rawOutput(fds[1]);
  kj::BufferedOutputStreamWrapper output(rawOutput);

  TestMessageBuilder message(10);
  initTestMessage(message.getRoot<TestAllTypes>());

  kj::Thread thread([&]() {
    for(int i = 0; i < 5; i++) {
      writeMessage(output, message);
    };
    output.flush();
  });

  [&]() noexcept { // forces test process to abort on exception without waiting for the thread

  auto stream = makeBufferedStream(*rawStream);

  for(int i = 0; i < 5; i++) {
    auto maybeMessage = stream->tryReadMessage(nullptr).wait(ioContext.waitScope);
    auto& received = KJ_ASSERT_NONNULL(maybeMessage);

    checkTestMessage(received.reader->getRoot<TestAllTypes>());
  }

  }(); // noexcept
}

TEST(SerializeAsyncTest, ParseBufferedAsyncFragmented) {
  PipeWithSmallBuffer fds;
  auto ioContext = kj::setupAsyncIo();
  auto rawStream = ioContext.lowLevelProvider->wrapSocketFd(fds[0]);
  SocketOutputStream rawOutput(fds[1]);
  FragmentingOutputStream output(rawOutput);

  TestMessageBuilder message(10);
  initTestMessage(message.getRoot<TestAllTypes>());

  kj::Thread thread([&]() {
    for(int i = 0; i < 5; i++) {
      writeMessage(output, message);
    };
  });

  [&]() noexcept { // forces test process to abort on exception without waiting for the thread

  auto scratch = kj::heapArray<word>(8192);
  auto stream = makeBufferedStream(*rawStream, scratch.size());

  for(int i = 0; i < 5; i++) {
    auto maybeMessage = stream->tryReadMessage(nullptr, ReaderOptions{}, scratch.asPtr()).wait(ioContext.waitScope);
    auto& received = KJ_ASSERT_NONNULL(maybeMessage);

    checkTestMessage(received.reader->getRoot<TestAllTypes>());
  }

  }(); // noexcept
}

TEST(SerializeAsyncTest, ParseBufferedAsyncBoundaryCases) {
  auto ioContext = kj::setupAsyncIo();

  auto messageBytes = [](auto numSegments) {
    TestMessageBuilder builder(numSegments);
    initTestMessage(builder.getRoot<TestAllTypes>());
    return messageToFlatArray(builder);
  };

  auto evenMessage = messageBytes(8);
  auto oddMessage = messageBytes(7);

  kj::Vector<AsyncMultiArrayInputStream::Batch> batchBuilder;
  auto numMessages = 0;

  auto batchesFromMessage = [&numMessages, &evenMessage, &oddMessage](
      std::initializer_list<size_t> splits) {
    kj::Vector<AsyncMultiArrayInputStream::Batch> batches;

    auto message = ((numMessages++ % 2) ? evenMessage : oddMessage).asBytes();
    size_t start = 0;
    for(auto split : splits) {
      KJ_ASSERT(split < message.size());
      batches.add(AsyncMultiArrayInputStream::Batch{
        kj::heapArray(message.slice(start, split))
      });
      start = split;
    }

    if (start < message.size()) {
      batches.add(AsyncMultiArrayInputStream::Batch{
        kj::heapArray(message.slice(start, message.size()))
      });
    }

    return batches.releaseAsArray();
  };

  auto completeMessage = [&]() {
    auto message = ((numMessages++ % 2) ? evenMessage : oddMessage).asBytes();
    return AsyncMultiArrayInputStream::Batch{ kj::heapArray(message) };
  };

  auto concatFirstBatch = [](auto batches, AsyncMultiArrayInputStream::Batch msg) {
    auto& first = batches[0];

    auto builder = kj::heapArrayBuilder<byte>(first.bytes.size() + msg.bytes.size());
    builder.addAll(msg.bytes);
    builder.addAll(first.bytes);

    first.bytes = builder.finish();
    return kj::mv(batches);
  };

  auto concatLastBatch = [](auto batches, AsyncMultiArrayInputStream::Batch msg) {
    auto& lastBatch = batches[batches.size()-1];

    auto builder = kj::heapArrayBuilder<byte>(lastBatch.bytes.size() + msg.bytes.size());
    builder.addAll(lastBatch.bytes);
    builder.addAll(msg.bytes);

    lastBatch.bytes = builder.finish();
    return kj::mv(batches);
  };

  auto addBatches = [&](auto batches) {
    for(auto& batch : batches) {
      batchBuilder.add(kj::mv(batch));
    }
  };

  // Read one message in byte-chunked batches
  addBatches(batchesFromMessage({3, 7, 11, 13 }));

  // // Read one message in one batch
  batchBuilder.add(completeMessage());

  // Read a message in word-chunked batches, then a complete message in a separate batch after
  addBatches(batchesFromMessage({ sizeof(word), sizeof(word) * 4 }));
  batchBuilder.add(completeMessage());

  // Read a message in word-chunked batches, then a complete message as part of the same batch as the
  // last batch of the chunked message
  addBatches(
    concatLastBatch(batchesFromMessage({ sizeof(word), sizeof(word) * 4 }), completeMessage())
  );

  // Read a message in word-chunked batches, then multiple complete messages as part of one separate
  // batch
  addBatches(batchesFromMessage({ sizeof(word), sizeof(word) * 4 }));
  batchBuilder.add(completeMessage());
  batchBuilder = concatLastBatch(kj::mv(batchBuilder), completeMessage());
  batchBuilder = concatLastBatch(kj::mv(batchBuilder), completeMessage());

  // Read a message in word-chunked batches, then multiple complete messages as part of the same
  // batch as the last batch of the chunked message
  {
    auto batches = batchesFromMessage({ sizeof(word), sizeof(word) * 4 });
    batches = concatLastBatch(kj::mv(batches), completeMessage());
    batches = concatLastBatch(kj::mv(batches), completeMessage());
    batches = concatLastBatch(kj::mv(batches), completeMessage());
    addBatches(kj::mv(batches));
  }

  // Read multiple chunked messages in a row as separate batches
  addBatches(batchesFromMessage({ sizeof(word), sizeof(word) * 4 }));
  addBatches(batchesFromMessage({ sizeof(word) * 4, sizeof(word) * 8 }));
  addBatches(batchesFromMessage({ 3, 7, 11, 13 }));
  addBatches(batchesFromMessage({ sizeof(word) * 5, sizeof(word) * 7 }));

  // Read a message in byte-chunked batches, then multiple complete messages as part of one separate
  // batch
  addBatches(batchesFromMessage({3, 7, 11, 13 }));
  batchBuilder.add(completeMessage());
  batchBuilder = concatLastBatch(kj::mv(batchBuilder), completeMessage());
  batchBuilder = concatLastBatch(kj::mv(batchBuilder), completeMessage());

  // Read a message in byte-chunked batches, then multiple complete messages as part of the same
  // batch as the last batch of the chunked message
  {
    auto batches = batchesFromMessage({3, 7, 11, 13 });
    batches = concatLastBatch(kj::mv(batches), completeMessage());
    batches = concatLastBatch(kj::mv(batches), completeMessage());
    batches = concatLastBatch(kj::mv(batches), completeMessage());
    addBatches(kj::mv(batches));
  }

  // Read a complete message and the first chunk of a word-chunked message as one batch,
  // then the rest of the batches for the chunked message.
  addBatches(
    concatFirstBatch(batchesFromMessage({ sizeof(word), sizeof(word) * 4 }), completeMessage())
  );

  // Read a complete message and the first chunk of a byte-chunked message as one batch,
  // then the rest of the batches for the chunked message.
  addBatches(
    concatFirstBatch(batchesFromMessage({ 3, 7, 11, 13 }), completeMessage())
  );

  AsyncMultiArrayInputStream rawStream(batchBuilder.releaseAsArray(), 65536);
  auto stream = makeBufferedStream(static_cast<kj::AsyncIoStream&>(rawStream));

  for(auto i = 0; i < numMessages; i++) {
    auto maybeMessage = stream->tryReadMessage(nullptr).wait(ioContext.waitScope);
    auto& received = KJ_ASSERT_NONNULL(maybeMessage);

    checkTestMessage(received.reader->getRoot<TestAllTypes>());
  }
}

TEST(SerializeAsyncTest, ParseBufferedSmallSocketBuffer) {
  auto ioContext = kj::setupAsyncIo();

  auto batches = kj::heapArrayBuilder<AsyncMultiArrayInputStream::Batch>(1);
  TestMessageBuilder builder(10);
  initTestMessage(builder.getRoot<TestAllTypes>());
  batches.add(AsyncMultiArrayInputStream::Batch {
    kj::heapArray(messageToFlatArray(builder).asBytes())
  });

  constexpr uint64_t smallBufferSize = 7;
  KJ_ASSERT(batches[0].bytes.size() > smallBufferSize);

  AsyncMultiArrayInputStream rawStream(batches.finish(), smallBufferSize);
  auto stream = makeBufferedStream(static_cast<kj::AsyncIoStream&>(rawStream));

  auto maybeMessage = stream->tryReadMessage(nullptr).wait(ioContext.waitScope);
  auto& received = KJ_ASSERT_NONNULL(maybeMessage);

  checkTestMessage(received.reader->getRoot<TestAllTypes>());
}

TEST(SerializeAsyncTest, ParseBufferedSmallStreamBuffer) {
  auto ioContext = kj::setupAsyncIo();

  auto batches = kj::heapArrayBuilder<AsyncMultiArrayInputStream::Batch>(1);
  TestMessageBuilder builder(10);
  initTestMessage(builder.getRoot<TestAllTypes>());
  auto msg =
    kj::heapArray(messageToFlatArray(builder).asBytes());
  batches.add(AsyncMultiArrayInputStream::Batch {
    kj::mv(msg)
  });

  constexpr uint64_t smallBufferSize = 7;
  KJ_ASSERT(batches[0].bytes.size() > smallBufferSize);

  AsyncMultiArrayInputStream rawStream(batches.finish(), 65536);
  auto stream = makeBufferedStream(static_cast<kj::AsyncIoStream&>(rawStream), 1);

  auto maybeMessage = stream->tryReadMessage(nullptr).wait(ioContext.waitScope);
  auto& received = KJ_ASSERT_NONNULL(maybeMessage);

  checkTestMessage(received.reader->getRoot<TestAllTypes>());
}

#if !_WIN32 && !__CYGWIN__
TEST(SerializeAsyncTest, ParseBufferedAsyncFullBufferWithCaps) {
  PipeWithSmallBuffer capFds;

  auto ioContext = kj::setupAsyncIo();

  int pipeFds[2];
  KJ_SYSCALL(kj::miniposix::pipe(pipeFds));
  kj::AutoCloseFd in1(pipeFds[0]);
  kj::AutoCloseFd out1(pipeFds[1]);

  KJ_SYSCALL(kj::miniposix::pipe(pipeFds));
  kj::AutoCloseFd in2(pipeFds[0]);
  kj::AutoCloseFd out2(pipeFds[1]);

  TestMessageBuilder message(10);
  initTestMessage(message.getRoot<TestAllTypes>());

  auto thread = kj::heap<kj::Thread>(
      [&capFds, &message, out1 = kj::mv(out1), out2 = kj::mv(out2)]() mutable noexcept {
    auto ioContext = kj::setupAsyncIo();
    auto outputCapStream = ioContext.lowLevelProvider->wrapUnixSocketFd(capFds[1]);

    auto sendFd = [&ioContext, &outputCapStream, &message](kj::AutoCloseFd fd) {
      for(int i = 0; i < 4; i++) {
        writeMessage(*outputCapStream, message).wait(ioContext.waitScope);
      }
      int sendFds[1] = { fd.get() };
      writeMessage(*outputCapStream, sendFds, message).wait(ioContext.waitScope);
    };

    sendFd(kj::mv(out1));
    sendFd(kj::mv(out2));
  });

  auto inputCapStream = ioContext.lowLevelProvider->wrapUnixSocketFd(capFds[0]);
  auto stream = makeBufferedStream(*inputCapStream);

  auto receiveFd = [&ioContext, &stream](auto& expectedInFd) noexcept {
    for(int i = 0; i < 4; i++) {
      auto result = KJ_ASSERT_NONNULL(stream->tryReadMessage(nullptr).wait(ioContext.waitScope));
      KJ_EXPECT(result.fds.size() == 0);
      checkTestMessage(result.reader->getRoot<TestAllTypes>());
    }

    {
      kj::Array<kj::AutoCloseFd> fdSpace = kj::heapArray<kj::AutoCloseFd>(1);
      auto result = KJ_ASSERT_NONNULL(stream->tryReadMessage(fdSpace).wait(ioContext.waitScope));
      KJ_ASSERT(result.fds.size() == 1);
      checkTestMessage(result.reader->getRoot<TestAllTypes>());

      kj::FdOutputStream(kj::mv(result.fds[0])).write("bar", 3);
    }

    // We want to verify that the outFd closed without deadlocking if it wasn't. So, use nonblocking
    // mode for our read()s.
    KJ_SYSCALL(fcntl(expectedInFd, F_SETFL, O_NONBLOCK));

    char buffer[4];
    ssize_t n;

    // First we read "bar" from expectedInFd.
    KJ_NONBLOCKING_SYSCALL(n = read(expectedInFd, buffer, 4));
    KJ_ASSERT(n == 3);
    buffer[3] = '\0';
    KJ_ASSERT(kj::StringPtr(buffer) == "bar");
  };

  receiveFd(in1);
  receiveFd(in2);

  thread = nullptr;

  auto verifyEof = [](auto expectedInFd) noexcept {
    char buffer[4];
    ssize_t n;

    // Now it should be EOF.
    KJ_NONBLOCKING_SYSCALL(n = read(expectedInFd, buffer, 4));
    if (n < 0) {
      KJ_FAIL_ASSERT("fd was not closed", n, strerror(errno));
    }
    KJ_ASSERT(n == 0);
  };

  verifyEof(kj::mv(in1));
  verifyEof(kj::mv(in2));
}

TEST(SerializeAsyncTest, ParseBufferedAsyncBoundaryCasesWithCaps) {
  auto ioContext = kj::setupAsyncIo();

  int pipeFds[2];
  KJ_SYSCALL(kj::miniposix::pipe(pipeFds));
  kj::AutoCloseFd in1(pipeFds[0]);
  kj::AutoCloseFd out1(pipeFds[1]);

  KJ_SYSCALL(kj::miniposix::pipe(pipeFds));
  kj::AutoCloseFd in2(pipeFds[0]);
  kj::AutoCloseFd out2(pipeFds[1]);

  auto messageBytes = [](auto numSegments) {
    TestMessageBuilder builder(numSegments);
    initTestMessage(builder.getRoot<TestAllTypes>());
    return messageToFlatArray(builder);
  };

  auto evenMessage = messageBytes(8);
  auto oddMessage = messageBytes(7);

  kj::Vector<AsyncMultiArrayInputStream::Batch> batchBuilder;
  auto numMessages = 0;
  kj::HashSet<size_t> messagesWithFds;

  auto batchesFromMessage = [&messagesWithFds, &numMessages, &evenMessage, &oddMessage]
      (std::initializer_list<size_t> splits, kj::Maybe<kj::AutoCloseFd> fd = nullptr) {
    kj::Vector<AsyncMultiArrayInputStream::Batch> batches;

    if (fd != nullptr) {
      messagesWithFds.insert(numMessages);
    }

    auto message = ((numMessages++ % 2) ? evenMessage : oddMessage).asBytes();
    size_t start = 0;
    bool first = true;
    for(auto split : splits) {
      KJ_ASSERT(split < message.size());
      batches.add(AsyncMultiArrayInputStream::Batch{
        kj::heapArray(message.slice(start, split))
      });
      if (first) {
        batches[0].fd = kj::mv(fd);
        first = false;
      }

      start = split;
    }

    if (start < message.size()) {
      batches.add(AsyncMultiArrayInputStream::Batch{
        kj::heapArray(message.slice(start, message.size()))
      });
    }

    batches.back().fd = kj::mv(fd);

    return batches.releaseAsArray();
  };

  auto completeMessage = [&](kj::Maybe<kj::AutoCloseFd> fd = nullptr) {
    if (fd != nullptr) {
      messagesWithFds.insert(numMessages);
    }

    auto message = ((numMessages++ % 2) ? evenMessage : oddMessage).asBytes();
    return AsyncMultiArrayInputStream::Batch{ kj::heapArray(message), kj::mv(fd) };
  };

  auto concatFirstBatch = [](auto batches, AsyncMultiArrayInputStream::Batch msg) {
    auto& first = batches[0];

    // It's not valid to perform this operation of prepending a message to the first batch
    // if the message has FDs attached.
    //
    // See https://gist.github.com/kentonv/bc7592af98c68ba2738f4436920868dc:
    //   To prevent multiple ancillary messages being delivered at once,
    //   the recvmsg() call that receives the ancillary data will be artifically limited to read
    //   no further than the last byte in the range, even if more data is available in the buffer
    //   after that byte, and even if that later data is not actually associated with any
    //   ancillary message.
    //
    // The byte range of the message batch to prepend would end at the end of that batch if it
    // contained FDs, so adding it to the beginning of another batch is invalid.
    KJ_ASSERT(msg.fd == nullptr);

    auto builder = kj::heapArrayBuilder<byte>(first.bytes.size() + msg.bytes.size());
    builder.addAll(msg.bytes);
    builder.addAll(first.bytes);

    first.bytes = builder.finish();

    return kj::mv(batches);
  };

  auto concatLastBatch = [](auto batches, AsyncMultiArrayInputStream::Batch msg) {
    auto& lastBatch = batches[batches.size()-1];

    auto builder = kj::heapArrayBuilder<byte>(lastBatch.bytes.size() + msg.bytes.size());
    builder.addAll(lastBatch.bytes);
    builder.addAll(msg.bytes);

    lastBatch.bytes = builder.finish();
    if (msg.fd != nullptr) {
      KJ_ASSERT(lastBatch.fd == nullptr);
      lastBatch.fd = kj::mv(msg.fd);
    }
    return kj::mv(batches);
  };

  auto addBatches = [&](auto batches) {
    for(auto& batch : batches) {
      batchBuilder.add(kj::mv(batch));
    }
  };

  {
    // Test that fds are correctly attributed to split messages after a complete message

    // Make sure we create this before so messagesWithFds is correctly modified
    auto msg = completeMessage();
    addBatches(
      concatFirstBatch(batchesFromMessage({ 3 }, kj::mv(out1)), kj::mv(msg)));
  }

  {
    // Test that fds are correctly attributed to the last read message

    auto batches = batchesFromMessage({3});
    batches = concatFirstBatch(kj::mv(batches), completeMessage());

    // Make sure we create this last so messagesWithFds is correctly modified
    auto fdMsg = completeMessage(kj::mv(out2));
    batches = concatLastBatch(kj::mv(batches), kj::mv(fdMsg));

    addBatches(kj::mv(batches));
  }

  AsyncMultiArrayInputStream rawStream(batchBuilder.releaseAsArray(), 65536);
  auto stream = makeBufferedStream(rawStream);

  for(auto i = 0; i < numMessages; i++) {
    kj::Array<kj::AutoCloseFd> fdSpace = kj::heapArray<kj::AutoCloseFd>(1);
    auto result = KJ_ASSERT_NONNULL(stream->tryReadMessage(fdSpace).wait(ioContext.waitScope));

    checkTestMessage(result.reader->getRoot<TestAllTypes>());

    if (messagesWithFds.contains(i)) {
      KJ_EXPECT(result.fds.size() == 1);
    } else {
      KJ_EXPECT(result.fds.size() == 0);
    }
  }

}
#endif

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
