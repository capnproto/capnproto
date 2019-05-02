// Copyright (c) 2019 Cloudflare, Inc. and contributors
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

#include "byte-stream.h"
#include <kj/test.h>

namespace capnp {
namespace {

kj::Promise<void> expectRead(kj::AsyncInputStream& in, kj::StringPtr expected) {
  if (expected.size() == 0) return kj::READY_NOW;

  auto buffer = kj::heapArray<char>(expected.size());

  auto promise = in.tryRead(buffer.begin(), 1, buffer.size());
  return promise.then(kj::mvCapture(buffer, [&in,expected](kj::Array<char> buffer, size_t amount) {
    if (amount == 0) {
      KJ_FAIL_ASSERT("expected data never sent", expected);
    }

    auto actual = buffer.slice(0, amount);
    if (memcmp(actual.begin(), expected.begin(), actual.size()) != 0) {
      KJ_FAIL_ASSERT("data from stream doesn't match expected", expected, actual);
    }

    return expectRead(in, expected.slice(amount));
  }));
}

KJ_TEST("KJ -> ByteStream -> KJ without shortening") {
  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory factory1;
  ByteStreamFactory factory2;

  auto pipe = kj::newOneWayPipe();

  auto wrapped = factory1.capnpToKj(factory2.kjToCapnp(kj::mv(pipe.out)));

  {
    auto promise = wrapped->write("foo", 3);
    KJ_EXPECT(!promise.poll(waitScope));
    expectRead(*pipe.in, "foo").wait(waitScope);
    promise.wait(waitScope);
  }

  wrapped = nullptr;
  KJ_EXPECT(pipe.in->readAllText().wait(waitScope) == "");
}

class ExactPointerWriter: public kj::AsyncOutputStream {
public:
  kj::ArrayPtr<const char> receivedBuffer;
  kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> fulfiller;

  kj::Promise<void> write(const void* buffer, size_t size) override {
    receivedBuffer = kj::arrayPtr(reinterpret_cast<const char*>(buffer), size);
    auto paf = kj::newPromiseAndFulfiller<void>();
    fulfiller = kj::mv(paf.fulfiller);
    return kj::mv(paf.promise);
  }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    KJ_UNIMPLEMENTED("not implemented for test");
  }
  kj::Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }
};

KJ_TEST("KJ -> ByteStream -> KJ with shortening") {
  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory factory;

  auto pipe = kj::newOneWayPipe();

  ExactPointerWriter exactPointerWriter;
  auto pumpPromise = pipe.in->pumpTo(exactPointerWriter);

  auto wrapped = factory.capnpToKj(factory.kjToCapnp(kj::mv(pipe.out)));

  {
    char buffer[4] = "foo";
    auto promise = wrapped->write(buffer, 3);
    KJ_EXPECT(!promise.poll(waitScope));

    // This first write won't have been path-shortened because we didn't know about the shorter
    // path yet when it started.
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() != buffer);
    KJ_EXPECT(kj::str(exactPointerWriter.receivedBuffer) == "foo");
    KJ_ASSERT_NONNULL(exactPointerWriter.fulfiller)->fulfill();
    promise.wait(waitScope);
  }

  {
    char buffer[4] = "foo";
    auto promise = wrapped->write(buffer, 3);
    KJ_EXPECT(!promise.poll(waitScope));

    // The second write was path-shortened so passes through the exact buffer!
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer);
    KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
    KJ_ASSERT_NONNULL(exactPointerWriter.fulfiller)->fulfill();
    promise.wait(waitScope);
  }

  wrapped = nullptr;
  KJ_EXPECT(pipe.in->readAllText().wait(waitScope) == "");
}

KJ_TEST("KJ -> ByteStream -> KJ -> ByteStream -> KJ with shortening") {
  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory factory;

  auto pipe = kj::newOneWayPipe();

  ExactPointerWriter exactPointerWriter;
  auto pumpPromise = pipe.in->pumpTo(exactPointerWriter);

  auto wrapped = factory.capnpToKj(factory.kjToCapnp(
                 factory.capnpToKj(factory.kjToCapnp(kj::mv(pipe.out)))));

  {
    char buffer[4] = "foo";
    auto promise = wrapped->write(buffer, 3);
    KJ_EXPECT(!promise.poll(waitScope));

    // This first write won't have been path-shortened because we didn't know about the shorter
    // path yet when it started.
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() != buffer);
    KJ_EXPECT(kj::str(exactPointerWriter.receivedBuffer) == "foo");
    KJ_ASSERT_NONNULL(exactPointerWriter.fulfiller)->fulfill();
    promise.wait(waitScope);
  }

  {
    char buffer[4] = "bar";
    auto promise = wrapped->write(buffer, 3);
    KJ_EXPECT(!promise.poll(waitScope));

    // The second write was path-shortened so passes through the exact buffer!
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer);
    KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
    KJ_ASSERT_NONNULL(exactPointerWriter.fulfiller)->fulfill();
    promise.wait(waitScope);
  }

  wrapped = nullptr;
  KJ_EXPECT(pumpPromise.wait(waitScope) == 6);
}

KJ_TEST("KJ -> ByteStream -> KJ pipe -> ByteStream -> KJ with shortening") {
  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory factory;

  auto backPipe = kj::newOneWayPipe();
  auto middlePipe = kj::newOneWayPipe();

  ExactPointerWriter exactPointerWriter;
  auto backPumpPromise = backPipe.in->pumpTo(exactPointerWriter);

  auto backWrapped = factory.capnpToKj(factory.kjToCapnp(kj::mv(backPipe.out)));
  auto midPumpPormise = middlePipe.in->pumpTo(*backWrapped, 3);

  auto wrapped = factory.capnpToKj(factory.kjToCapnp(kj::mv(middlePipe.out)));

  // Poll whenWriteDisconnected(), mainly as a way to let all the path-shortening settle.
  auto disconnectPromise = wrapped->whenWriteDisconnected();
  KJ_EXPECT(!disconnectPromise.poll(waitScope));

  char buffer[7] = "foobar";
  auto writePromise = wrapped->write(buffer, 6);
  KJ_EXPECT(!writePromise.poll(waitScope));

  // The first three bytes will tunnel all the way down to the destination.
  KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer);
  KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
  KJ_ASSERT_NONNULL(exactPointerWriter.fulfiller)->fulfill();

  KJ_EXPECT(midPumpPormise.wait(waitScope) == 3);

  ExactPointerWriter exactPointerWriter2;
  midPumpPormise = middlePipe.in->pumpTo(exactPointerWriter2, 6);
  KJ_EXPECT(!writePromise.poll(waitScope));

  // The second half of the "foobar" write will have taken a slow path, because the write was
  // restarted in the middle of the stream re-resolving itself.
  KJ_EXPECT(kj::str(exactPointerWriter2.receivedBuffer) == "bar");
  KJ_ASSERT_NONNULL(exactPointerWriter2.fulfiller)->fulfill();

  // Now that write is done.
  writePromise.wait(waitScope);
  KJ_EXPECT(!midPumpPormise.poll(waitScope));

  // If we write again, it'll hit the fast path.
  char buffer2[4] = "baz";
  writePromise = wrapped->write(buffer2, 3);
  KJ_EXPECT(!writePromise.poll(waitScope));
  KJ_EXPECT(exactPointerWriter2.receivedBuffer.begin() == buffer2);
  KJ_EXPECT(exactPointerWriter2.receivedBuffer.size() == 3);
  KJ_ASSERT_NONNULL(exactPointerWriter2.fulfiller)->fulfill();

  KJ_EXPECT(midPumpPormise.wait(waitScope) == 6);
  writePromise.wait(waitScope);
}

KJ_TEST("Two Substreams on one destination") {
  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory factory;

  auto backPipe = kj::newOneWayPipe();
  auto middlePipe1 = kj::newOneWayPipe();
  auto middlePipe2 = kj::newOneWayPipe();

  ExactPointerWriter exactPointerWriter;
  auto backPumpPromise = backPipe.in->pumpTo(exactPointerWriter);

  auto backWrapped = factory.capnpToKj(factory.kjToCapnp(kj::mv(backPipe.out)));

  auto wrapped1 = factory.capnpToKj(factory.kjToCapnp(kj::mv(middlePipe1.out)));
  auto wrapped2 = factory.capnpToKj(factory.kjToCapnp(kj::mv(middlePipe2.out)));

  // Declare these buffers out here so that they can't possibly end up with the same address.
  char buffer1[4] = "foo";
  char buffer2[4] = "bar";

  {
    auto wrapped = kj::mv(wrapped1);

    // First pump 3 bytes from the first stream.
    auto midPumpPormise = middlePipe1.in->pumpTo(*backWrapped, 3);

    // Poll whenWriteDisconnected(), mainly as a way to let all the path-shortening settle.
    auto disconnectPromise = wrapped->whenWriteDisconnected();
    KJ_EXPECT(!disconnectPromise.poll(waitScope));

    auto writePromise = wrapped->write(buffer1, 3);
    KJ_EXPECT(!writePromise.poll(waitScope));

    // The first write will tunnel all the way down to the destination.
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer1);
    KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
    KJ_ASSERT_NONNULL(exactPointerWriter.fulfiller)->fulfill();

    writePromise.wait(waitScope);
    KJ_EXPECT(midPumpPormise.wait(waitScope) == 3);
  }

  {
    auto wrapped = kj::mv(wrapped2);

    // Now pump another 3 bytes from the second stream.
    auto midPumpPormise = middlePipe2.in->pumpTo(*backWrapped, 3);

    // Poll whenWriteDisconnected(), mainly as a way to let all the path-shortening settle.
    auto disconnectPromise = wrapped->whenWriteDisconnected();
    KJ_EXPECT(!disconnectPromise.poll(waitScope));

    auto writePromise = wrapped->write(buffer2, 3);
    KJ_EXPECT(!writePromise.poll(waitScope));

    // The second write will also tunnel all the way down to the destination.
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer2);
    KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
    KJ_ASSERT_NONNULL(exactPointerWriter.fulfiller)->fulfill();

    writePromise.wait(waitScope);
    KJ_EXPECT(midPumpPormise.wait(waitScope) == 3);
  }
}

KJ_TEST("Two Substreams on one destination no limits (pump to EOF)") {
  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory factory;

  auto backPipe = kj::newOneWayPipe();
  auto middlePipe1 = kj::newOneWayPipe();
  auto middlePipe2 = kj::newOneWayPipe();

  ExactPointerWriter exactPointerWriter;
  auto backPumpPromise = backPipe.in->pumpTo(exactPointerWriter);

  auto backWrapped = factory.capnpToKj(factory.kjToCapnp(kj::mv(backPipe.out)));

  auto wrapped1 = factory.capnpToKj(factory.kjToCapnp(kj::mv(middlePipe1.out)));
  auto wrapped2 = factory.capnpToKj(factory.kjToCapnp(kj::mv(middlePipe2.out)));

  // Declare these buffers out here so that they can't possibly end up with the same address.
  char buffer1[4] = "foo";
  char buffer2[4] = "bar";

  {
    auto wrapped = kj::mv(wrapped1);

    // First pump from the first stream until EOF.
    auto midPumpPormise = middlePipe1.in->pumpTo(*backWrapped);

    // Poll whenWriteDisconnected(), mainly as a way to let all the path-shortening settle.
    auto disconnectPromise = wrapped->whenWriteDisconnected();
    KJ_EXPECT(!disconnectPromise.poll(waitScope));

    auto writePromise = wrapped->write(buffer1, 3);
    KJ_EXPECT(!writePromise.poll(waitScope));

    // The first write will tunnel all the way down to the destination.
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer1);
    KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
    KJ_ASSERT_NONNULL(exactPointerWriter.fulfiller)->fulfill();

    writePromise.wait(waitScope);
    { auto drop = kj::mv(wrapped); }
    KJ_EXPECT(midPumpPormise.wait(waitScope) == 3);
  }

  {
    auto wrapped = kj::mv(wrapped2);

    // Now pump from the second stream until EOF.
    auto midPumpPormise = middlePipe2.in->pumpTo(*backWrapped);

    // Poll whenWriteDisconnected(), mainly as a way to let all the path-shortening settle.
    auto disconnectPromise = wrapped->whenWriteDisconnected();
    KJ_EXPECT(!disconnectPromise.poll(waitScope));

    auto writePromise = wrapped->write(buffer2, 3);
    KJ_EXPECT(!writePromise.poll(waitScope));

    // The second write will also tunnel all the way down to the destination.
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer2);
    KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
    KJ_ASSERT_NONNULL(exactPointerWriter.fulfiller)->fulfill();

    writePromise.wait(waitScope);
    { auto drop = kj::mv(wrapped); }
    KJ_EXPECT(midPumpPormise.wait(waitScope) == 3);
  }
}

// TODO:
// - Parallel writes (requires streaming)
// - Write to KJ -> capnp -> RPC -> capnp -> KJ loopback without shortening, verify we can write
//   several things to buffer (requires streaming).
// - Again, but with shortening which only occurs after some promise resolve.

}  // namespace
}  // namespace capnp
