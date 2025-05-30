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
#include <capnp/rpc-twoparty.h>
#include <stdlib.h>

namespace capnp {
namespace {

kj::Promise<void> expectRead(kj::AsyncInputStream& in, kj::StringPtr expected) {
  if (expected.size() == 0) return kj::READY_NOW;

  auto buffer = kj::heapArray<char>(expected.size());

  auto promise = in.tryRead(buffer.begin(), 1, buffer.size());
  return promise.then([&in,expected,buffer=kj::mv(buffer)](size_t amount) {
    if (amount == 0) {
      KJ_FAIL_ASSERT("expected data never sent", expected);
    }

    auto actual = buffer.first(amount);
    if (actual != expected.first(amount)) {
      KJ_FAIL_ASSERT("data from stream doesn't match expected", expected, actual);
    }

    return expectRead(in, expected.slice(amount));
  });
}

kj::String makeString(size_t size) {
  auto bytes = kj::heapArray<char>(size);
  for (char& c: bytes) {
    c = 'a' + rand() % 26;
  }
  bytes[bytes.size() - 1] = 0;
  return kj::String(kj::mv(bytes));
};

KJ_TEST("KJ -> ByteStream -> KJ without shortening") {
  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory factory1;
  ByteStreamFactory factory2;

  auto pipe = kj::newOneWayPipe();

  auto wrapped = factory1.capnpToKj(factory2.kjToCapnp(kj::mv(pipe.out)));

  {
    auto promise = wrapped->write("foo"_kjb);
    KJ_EXPECT(!promise.poll(waitScope));
    expectRead(*pipe.in, "foo").wait(waitScope);
    promise.wait(waitScope);
  }

  {
    // Write more than 1 << 16 bytes at once to exercise write splitting.
    auto str = makeString(1 << 17);
    auto promise = wrapped->write(str.asBytes());
    KJ_EXPECT(!promise.poll(waitScope));
    expectRead(*pipe.in, str).wait(waitScope);
    promise.wait(waitScope);
  }

  {
    // Write more than 1 << 16 bytes via an array to exercise write splitting.
    auto str = makeString(1 << 18);
    auto pieces = kj::heapArrayBuilder<kj::ArrayPtr<const kj::byte>>(4);

    // Two 2^15 pieces will be combined.
    pieces.add(kj::arrayPtr(reinterpret_cast<kj::byte*>(str.begin()), 1 << 15));
    pieces.add(kj::arrayPtr(reinterpret_cast<kj::byte*>(str.begin() + (1 << 15)), 1 << 15));

    // One 2^16 piece will be written alone.
    pieces.add(kj::arrayPtr(reinterpret_cast<kj::byte*>(
        str.begin() + (1 << 16)), 1 << 16));

    // One 2^17 piece will be split.
    pieces.add(kj::arrayPtr(reinterpret_cast<kj::byte*>(
        str.begin() + (1 << 17)), str.size() - (1 << 17)));

    auto promise = wrapped->write(pieces);
    KJ_EXPECT(!promise.poll(waitScope));
    expectRead(*pipe.in, str).wait(waitScope);
    promise.wait(waitScope);
  }

  wrapped = nullptr;
  KJ_EXPECT(pipe.in->readAllText().wait(waitScope) == "");
}

class ExactPointerWriter: public kj::AsyncOutputStream {
public:
  kj::ArrayPtr<const kj::byte> receivedBuffer;

  void fulfill() {
    KJ_ASSERT_NONNULL(fulfiller)->fulfill();
    fulfiller = kj::none;
    receivedBuffer = nullptr;
  }

  kj::Promise<void> write(kj::ArrayPtr<const byte> buffer) override {
    KJ_ASSERT(fulfiller == kj::none);
    receivedBuffer = buffer;
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

  void expectBuffer(kj::StringPtr expected) {
    KJ_EXPECT(receivedBuffer == expected.asArray(), receivedBuffer, expected);
  }

private:
  kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> fulfiller;
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
    auto buffer = "foo"_kjb;
    auto promise = wrapped->write(buffer);
    KJ_EXPECT(!promise.poll(waitScope));

    // This first write won't have been path-shortened because we didn't know about the shorter
    // path yet when it started.
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() != buffer.begin());
    KJ_EXPECT(exactPointerWriter.receivedBuffer == "foo"_kjb);
    exactPointerWriter.fulfill();
    promise.wait(waitScope);
  }

  {
    auto buffer = "foo"_kjb;
    auto promise = wrapped->write(buffer);
    KJ_EXPECT(!promise.poll(waitScope));

    // The second write was path-shortened so passes through the exact buffer!
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer.begin());
    KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
    exactPointerWriter.fulfill();
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
    auto buffer = "foo"_kjb;
    auto promise = wrapped->write(buffer);
    KJ_EXPECT(!promise.poll(waitScope));

    // This first write won't have been path-shortened because we didn't know about the shorter
    // path yet when it started.
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() != buffer.begin());
    KJ_EXPECT(exactPointerWriter.receivedBuffer == "foo"_kjb);
    exactPointerWriter.fulfill();
    promise.wait(waitScope);
  }

  {
    auto buffer = "bar"_kjb;
    auto promise = wrapped->write(buffer);
    KJ_EXPECT(!promise.poll(waitScope));

    // The second write was path-shortened so passes through the exact buffer!
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer.begin());
    KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
    exactPointerWriter.fulfill();
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

  auto buffer = "foobar"_kjb;
  auto writePromise = wrapped->write(buffer);
  KJ_EXPECT(!writePromise.poll(waitScope));

  // The first three bytes will tunnel all the way down to the destination.
  KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer.begin());
  KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
  exactPointerWriter.fulfill();

  KJ_EXPECT(midPumpPormise.wait(waitScope) == 3);

  ExactPointerWriter exactPointerWriter2;
  midPumpPormise = middlePipe.in->pumpTo(exactPointerWriter2, 6);
  KJ_EXPECT(!writePromise.poll(waitScope));

  // The second half of the "foobar" write will have taken a slow path, because the write was
  // restarted in the middle of the stream re-resolving itself.
  KJ_EXPECT(exactPointerWriter2.receivedBuffer == "bar"_kjb);
  exactPointerWriter2.fulfill();

  // Now that write is done.
  writePromise.wait(waitScope);
  KJ_EXPECT(!midPumpPormise.poll(waitScope));

  // If we write again, it'll hit the fast path.
  auto buffer2 = "baz"_kjb;
  writePromise = wrapped->write(buffer2);
  KJ_EXPECT(!writePromise.poll(waitScope));
  KJ_EXPECT(exactPointerWriter2.receivedBuffer.begin() == buffer2.begin());
  KJ_EXPECT(exactPointerWriter2.receivedBuffer.size() == 3);
  exactPointerWriter2.fulfill();

  KJ_EXPECT(midPumpPormise.wait(waitScope) == 6);
  writePromise.wait(waitScope);
}

KJ_TEST("KJ -> ByteStream RPC -> KJ pipe -> ByteStream RPC -> KJ with shortening") {
  // For this test, we're going to verify that if we have ByteStreams over RPC in both directions
  // and we pump a ByteStream to another ByteStream at one end of the connection, it gets shortened
  // all the way to the other end!

  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory clientFactory;
  ByteStreamFactory serverFactory;

  auto backPipe = kj::newOneWayPipe();
  auto middlePipe = kj::newOneWayPipe();

  ExactPointerWriter exactPointerWriter;
  auto backPumpPromise = backPipe.in->pumpTo(exactPointerWriter);

  auto rpcConnection = kj::newTwoWayPipe();
  capnp::TwoPartyClient client(*rpcConnection.ends[0],
      clientFactory.kjToCapnp(kj::mv(backPipe.out)),
      rpc::twoparty::Side::CLIENT);
  capnp::TwoPartyClient server(*rpcConnection.ends[1],
      serverFactory.kjToCapnp(kj::mv(middlePipe.out)),
      rpc::twoparty::Side::SERVER);

  auto backWrapped = serverFactory.capnpToKj(server.bootstrap().castAs<ByteStream>());
  auto midPumpPormise = middlePipe.in->pumpTo(*backWrapped, 3);

  auto wrapped = clientFactory.capnpToKj(client.bootstrap().castAs<ByteStream>());

  // Poll whenWriteDisconnected(), mainly as a way to let all the path-shortening settle.
  auto disconnectPromise = wrapped->whenWriteDisconnected();
  KJ_EXPECT(!disconnectPromise.poll(waitScope));

  auto buffer = "foobar"_kjb;
  auto writePromise = wrapped->write(buffer);

  // The server side did a 3-byte pump. Path-shortening magic kicks in, and the first three bytes
  // of the write on the client side go *directly* to the endpoint without a copy!
  KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer.begin());
  KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
  exactPointerWriter.fulfill();

  KJ_EXPECT(midPumpPormise.wait(waitScope) == 3);

  ExactPointerWriter exactPointerWriter2;
  midPumpPormise = middlePipe.in->pumpTo(exactPointerWriter2, 6);
  midPumpPormise.poll(waitScope);

  // The second half of the "foobar" write will have taken a slow path, because the write was
  // restarted in the middle of the stream re-resolving itself.
  KJ_EXPECT(exactPointerWriter2.receivedBuffer == "bar"_kjb);
  exactPointerWriter2.fulfill();

  // Now that write is done.
  writePromise.wait(waitScope);
  KJ_EXPECT(!midPumpPormise.poll(waitScope));

  // If we write again, it'll finish the server-side pump (but won't be a zero-copy write since
  // it has to go over RPC).
  auto buffer2 = "baz"_kjb;
  writePromise = wrapped->write(buffer2);
  KJ_EXPECT(!midPumpPormise.poll(waitScope));
  KJ_EXPECT(exactPointerWriter2.receivedBuffer == "baz"_kjb);
  exactPointerWriter2.fulfill();

  KJ_EXPECT(midPumpPormise.wait(waitScope) == 6);
  writePromise.wait(waitScope);
}

KJ_TEST("KJ -> ByteStream RPC -> KJ pipe -> ByteStream RPC -> KJ with concurrent shortening") {
  // This is similar to the previous test, but we start writing before the path-shortening has
  // settled. This should result in some writes optimistically bouncing back and forth before
  // the stream settles in.

  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory clientFactory;
  ByteStreamFactory serverFactory;

  auto backPipe = kj::newOneWayPipe();
  auto middlePipe = kj::newOneWayPipe();

  ExactPointerWriter exactPointerWriter;
  auto backPumpPromise = backPipe.in->pumpTo(exactPointerWriter);

  auto rpcConnection = kj::newTwoWayPipe();
  capnp::TwoPartyClient client(*rpcConnection.ends[0],
      clientFactory.kjToCapnp(kj::mv(backPipe.out)),
      rpc::twoparty::Side::CLIENT);
  capnp::TwoPartyClient server(*rpcConnection.ends[1],
      serverFactory.kjToCapnp(kj::mv(middlePipe.out)),
      rpc::twoparty::Side::SERVER);

  auto backWrapped = serverFactory.capnpToKj(server.bootstrap().castAs<ByteStream>());
  auto midPumpPormise = middlePipe.in->pumpTo(*backWrapped);

  auto wrapped = clientFactory.capnpToKj(client.bootstrap().castAs<ByteStream>());

  auto buffer = "foobar"_kjb;
  auto writePromise = wrapped->write(buffer);

  // The write went to RPC so it's not immediately received.
  KJ_EXPECT(exactPointerWriter.receivedBuffer == nullptr);

  // Write should be received after we turn the event loop.
  waitScope.poll();
  KJ_EXPECT(exactPointerWriter.receivedBuffer != nullptr);

  // Note that the promise that write() returned above has already resolved, because it hit RPC
  // and went into the streaming window.
  KJ_ASSERT(writePromise.poll(waitScope));
  writePromise.wait(waitScope);

  // Let's start a second write. Even though the first write technically isn't done yet, it's
  // legal for us to start a second one because the first write's returned promise optimistically
  // resolved for streaming window reasons. This ends up being a very tricky case for our code!
  auto buffer2 = "bazqux"_kjb;
  auto writePromise2 = wrapped->write(buffer2);

  // Now check the first write was correct, and close it out.
  KJ_EXPECT(exactPointerWriter.receivedBuffer == "foobar"_kjb);
  exactPointerWriter.fulfill();

  // Turn event loop again. Now the second write arrives.
  waitScope.poll();
  KJ_EXPECT(exactPointerWriter.receivedBuffer == "bazqux"_kjb);
  exactPointerWriter.fulfill();
  writePromise2.wait(waitScope);

  // If we do another write now, it should be zero-copy, because everything has settled.
  auto buffer3 = "corge"_kjb;
  auto writePromise3 = wrapped->write(buffer3);
  KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer3.begin());
  KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 5);
  KJ_EXPECT(!writePromise3.poll(waitScope));
  exactPointerWriter.fulfill();
  writePromise3.wait(waitScope);
}

KJ_TEST("KJ -> KJ pipe -> ByteStream RPC -> KJ pipe -> ByteStream RPC -> KJ with concurrent shortening") {
  // Same as previous test, except we add a KJ pipe at the beginning and pump it into the top of
  // the pipe, which invokes tryPumpFrom() on the KjToCapnpStreamAdapter.

  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory clientFactory;
  ByteStreamFactory serverFactory;

  auto backPipe = kj::newOneWayPipe();
  auto middlePipe = kj::newOneWayPipe();
  auto frontPipe = kj::newOneWayPipe();

  ExactPointerWriter exactPointerWriter;
  auto backPumpPromise = backPipe.in->pumpTo(exactPointerWriter);

  auto rpcConnection = kj::newTwoWayPipe();
  capnp::TwoPartyClient client(*rpcConnection.ends[0],
      clientFactory.kjToCapnp(kj::mv(backPipe.out)),
      rpc::twoparty::Side::CLIENT);
  capnp::TwoPartyClient server(*rpcConnection.ends[1],
      serverFactory.kjToCapnp(kj::mv(middlePipe.out)),
      rpc::twoparty::Side::SERVER);

  auto backWrapped = serverFactory.capnpToKj(server.bootstrap().castAs<ByteStream>());
  auto midPumpPormise = middlePipe.in->pumpTo(*backWrapped);

  auto wrapped = clientFactory.capnpToKj(client.bootstrap().castAs<ByteStream>());
  auto frontPumpPromise = frontPipe.in->pumpTo(*wrapped);

  auto buffer = "foobar"_kjb;
  auto writePromise = frontPipe.out->write(buffer);

  // The write went to RPC so it's not immediately received.
  KJ_EXPECT(exactPointerWriter.receivedBuffer == nullptr);

  // Write should be received after we turn the event loop.
  waitScope.poll();
  KJ_EXPECT(exactPointerWriter.receivedBuffer != nullptr);

  // Note that the promise that write() returned above has already resolved, because it hit RPC
  // and went into the streaming window.
  KJ_ASSERT(writePromise.poll(waitScope));
  writePromise.wait(waitScope);

  // Let's start a second write. Even though the first write technically isn't done yet, it's
  // legal for us to start a second one because the first write's returned promise optimistically
  // resolved for streaming window reasons. This ends up being a very tricky case for our code!
  auto buffer2 = "bazqux"_kjb;
  auto writePromise2 = frontPipe.out->write(buffer2);

  // Now check the first write was correct, and close it out.
  KJ_EXPECT(exactPointerWriter.receivedBuffer == "foobar"_kjb);
  exactPointerWriter.fulfill();

  // Turn event loop again. Now the second write arrives.
  waitScope.poll();
  KJ_EXPECT(exactPointerWriter.receivedBuffer == "bazqux"_kjb);
  exactPointerWriter.fulfill();
  writePromise2.wait(waitScope);

  // If we do another write now, it should be zero-copy, because everything has settled.
  auto buffer3 = "corge"_kjb;
  auto writePromise3 = frontPipe.out->write(buffer3);
  KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer3.begin());
  KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 5);
  KJ_EXPECT(!writePromise3.poll(waitScope));
  exactPointerWriter.fulfill();
  writePromise3.wait(waitScope);
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
  auto buffer1 = "foo"_kjb;
  auto buffer2 = "bar"_kjb;

  {
    auto wrapped = kj::mv(wrapped1);

    // First pump 3 bytes from the first stream.
    auto midPumpPormise = middlePipe1.in->pumpTo(*backWrapped, 3);

    // Poll whenWriteDisconnected(), mainly as a way to let all the path-shortening settle.
    auto disconnectPromise = wrapped->whenWriteDisconnected();
    KJ_EXPECT(!disconnectPromise.poll(waitScope));

    auto writePromise = wrapped->write(buffer1);
    KJ_EXPECT(!writePromise.poll(waitScope));

    // The first write will tunnel all the way down to the destination.
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer1.begin());
    KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
    exactPointerWriter.fulfill();

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

    auto writePromise = wrapped->write(buffer2);
    KJ_EXPECT(!writePromise.poll(waitScope));

    // The second write will also tunnel all the way down to the destination.
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer2.begin());
    KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
    exactPointerWriter.fulfill();

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
  auto buffer1 = "foo"_kjb;
  auto buffer2 = "bar"_kjb;

  {
    auto wrapped = kj::mv(wrapped1);

    // First pump from the first stream until EOF.
    auto midPumpPormise = middlePipe1.in->pumpTo(*backWrapped);

    // Poll whenWriteDisconnected(), mainly as a way to let all the path-shortening settle.
    auto disconnectPromise = wrapped->whenWriteDisconnected();
    KJ_EXPECT(!disconnectPromise.poll(waitScope));

    auto writePromise = wrapped->write(buffer1);
    KJ_EXPECT(!writePromise.poll(waitScope));

    // The first write will tunnel all the way down to the destination.
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer1.begin());
    KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
    exactPointerWriter.fulfill();

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

    auto writePromise = wrapped->write(buffer2);
    KJ_EXPECT(!writePromise.poll(waitScope));

    // The second write will also tunnel all the way down to the destination.
    KJ_EXPECT(exactPointerWriter.receivedBuffer.begin() == buffer2.begin());
    KJ_EXPECT(exactPointerWriter.receivedBuffer.size() == 3);
    exactPointerWriter.fulfill();

    writePromise.wait(waitScope);
    { auto drop = kj::mv(wrapped); }
    KJ_EXPECT(midPumpPormise.wait(waitScope) == 3);
  }
}

KJ_TEST("KJ -> ByteStream RPC -> KJ promise stream -> ByteStream -> KJ") {
  // Test what happens if we queue up several requests on a ByteStream and then it resolves to
  // a shorter path.

  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory factory;
  ExactPointerWriter exactPointerWriter;

  auto paf = kj::newPromiseAndFulfiller<kj::Own<kj::AsyncOutputStream>>();
  auto backCap = factory.kjToCapnp(kj::newPromisedStream(kj::mv(paf.promise)));

  auto rpcPipe = kj::newTwoWayPipe();
  capnp::TwoPartyClient client(*rpcPipe.ends[0]);
  capnp::TwoPartyClient server(*rpcPipe.ends[1], kj::mv(backCap), rpc::twoparty::Side::SERVER);
  auto front = factory.capnpToKj(client.bootstrap().castAs<ByteStream>());

  // These will all queue up in the RPC layer.
  front->write("foo"_kjb).wait(waitScope);
  front->write("bar"_kjb).wait(waitScope);
  front->write("baz"_kjb).wait(waitScope);
  front->write("qux"_kjb).wait(waitScope);

  // Make sure those writes manage to get all the way through the RPC system and queue up in the
  // LocalClient wrapping the CapnpToKjStreamAdapter at the other end.
  waitScope.poll();

  // Fulfill the promise.
  paf.fulfiller->fulfill(factory.capnpToKj(factory.kjToCapnp(kj::attachRef(exactPointerWriter))));
  waitScope.poll();

  // Now:
  // - "foo" should have made it all the way down to the final output stream.
  // - "bar", "baz", and "qux" are queued on the CapnpToKjStreamAdapter immediately wrapping the
  //   KJ promise stream.
  // - But that stream adapter has discovered that there's another capnp stream downstream and has
  //   resolved itself to the later stream.
  // - A new call at this time should NOT be allowed to hop the queue.

  exactPointerWriter.expectBuffer("foo");

  front->write("corge"_kjb).wait(waitScope);
  waitScope.poll();

  exactPointerWriter.fulfill();

  waitScope.poll();
  exactPointerWriter.expectBuffer("bar");
  exactPointerWriter.fulfill();

  waitScope.poll();
  exactPointerWriter.expectBuffer("baz");
  exactPointerWriter.fulfill();

  waitScope.poll();
  exactPointerWriter.expectBuffer("qux");
  exactPointerWriter.fulfill();

  waitScope.poll();
  exactPointerWriter.expectBuffer("corge");
  exactPointerWriter.fulfill();

  // There may still be some detach()ed promises holding on to some capabilities that transitively
  // hold a fake Own<AsyncOutputStream> pointing at exactPointerWriter, which is actually on the
  // stack. We created a fake Own pointing to a stack variable by using
  // kj::attachRef(exactPointerWriter), above; it does not actually own the object it points to.
  // We need to make sure those Owns are dropped before exactPoniterWriter is destroyed, otherwise
  // ASAN will flag some invalid reads (of exactPointerWriter's vtable, in particular).
  waitScope.cancelAllDetached();
}

class ExplicitEndTest final: public ExplicitEndOutputStream {
public:
  kj::Vector<byte> bytes;
  kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> endFulfiller;

  void clear() {
    bytes.clear();
    endFulfiller = kj::none;
  }

  kj::Promise<void> write(kj::ArrayPtr<const byte> buffer) override {
    bytes.addAll(buffer);
    return kj::READY_NOW;
  }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    for (auto piece: pieces) {
      bytes.addAll(piece);
    }
    return kj::READY_NOW;
  }
  kj::Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }

  kj::Promise<void> end() override {
    KJ_REQUIRE(endFulfiller == kj::none);

    auto paf = kj::newPromiseAndFulfiller<void>();
    endFulfiller = kj::mv(paf.fulfiller);
    return kj::mv(paf.promise);
  }
};

class PathProbeBlocker final: public ExplicitEndOutputStream {
public:
  PathProbeBlocker(kj::Own<ExplicitEndOutputStream> inner): inner(kj::mv(inner)) {}
  PathProbeBlocker(kj::Own<kj::AsyncOutputStream> inner)
      : inner(inner.downcast<ExplicitEndOutputStream>()) {}

  kj::Promise<void> write(kj::ArrayPtr<const byte> buffer) override {
    return inner->write(buffer);
  }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    return inner->write(pieces);
  }
  kj::Promise<void> whenWriteDisconnected() override {
    return inner->whenWriteDisconnected();
  }

  kj::Promise<void> end() override {
    return inner->end();
  }

  // Don't implement tryPumpFrom(), to block path shortening through this wrapper.

private:
  kj::Own<ExplicitEndOutputStream> inner;
};

KJ_TEST("ExplicitEndOutputStream round trip") {
  // Test what happens if we queue up several requests on a ByteStream and then it resolves to
  // a shorter path.

  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);

  ByteStreamFactory factories[4];
  ExplicitEndTest endpoint;

  auto doTest = [&](kj::StringPtr context, kj::Own<ExplicitEndOutputStream> outer) {
    KJ_CONTEXT(context);

    // Poll to let all path shortening settle.
    waitScope.poll();

    outer->write("foobar"_kjb).wait(waitScope);

    KJ_EXPECT(kj::str(endpoint.bytes.asPtr().asChars()) == "foobar");
    KJ_EXPECT(endpoint.endFulfiller == kj::none);

    auto promise = outer->end();
    if (promise.poll(waitScope)) {
      // Maybe it threw, if so propagate that.
      promise.wait(waitScope);
      KJ_FAIL_ASSERT("end() returned too soon");
    }

    KJ_ASSERT_NONNULL(endpoint.endFulfiller)->fulfill();
    KJ_ASSERT(promise.poll(waitScope));
    promise.wait(waitScope);

    endpoint.clear();
  };

  // Run blocking path shortening by using lots of different factoires and a PathProbeBlocker.
  doTest("no path shortening",
      factories[3].capnpToKjExplicitEnd(
          factories[2].kjToCapnp(
              kj::heap<PathProbeBlocker>(
                  factories[1].capnpToKj(
                      factories[0].kjToCapnp(kj::attachRef(endpoint)))))));

  // Run allowing shortening of capnp layers, by using the same factory in all cases.
  doTest("capnp path shortening",
      factories[0].capnpToKjExplicitEnd(
          factories[0].kjToCapnp(
              kj::heap<PathProbeBlocker>(
                  factories[0].capnpToKj(
                      factories[0].kjToCapnp(kj::attachRef(endpoint)))))));

  // Run allowing shortening of KJ layers, by not inserting PathProbeBlocker.
  doTest("kj path shortening",
      factories[3].capnpToKjExplicitEnd(
          factories[2].kjToCapnp(
              factories[1].capnpToKj(
                  factories[0].kjToCapnp(kj::attachRef(endpoint))))));

  // Run with full path shortening.
  doTest("full path shortening",
      factories[0].capnpToKjExplicitEnd(
          factories[0].kjToCapnp(
              factories[0].capnpToKj(
                  factories[0].kjToCapnp(kj::attachRef(endpoint))))));
}

namespace {

class DummyStream final: public kj::AsyncOutputStream {
public:
  kj::Vector<byte> written;

  kj::Promise<void> write(kj::ArrayPtr<const byte> buffer) override {
    written.addAll(buffer);
    return kj::READY_NOW;
  }

  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    for (auto piece: pieces) {
      written.addAll(piece);
    }
    return kj::READY_NOW;
  }

  kj::Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }
};

}  // namespace

KJ_TEST("ExplicitEndOutputStream::wrap") {
  // Test that uncleanEnd is called iff end is not called.

  kj::EventLoop eventLoop;
  kj::WaitScope waitScope(eventLoop);
  bool uncleanEndCalled = false;

  {
    auto stream = ExplicitEndOutputStream::wrap(
        kj::heap<DummyStream>(),
        [&] { uncleanEndCalled = true; });
    stream->write("Hello, world!"_kjb).wait(waitScope);
    stream->end().wait(waitScope);
  }

  KJ_EXPECT(!uncleanEndCalled, "uncleanEnd should not be called when end was called");

  {
    auto stream = ExplicitEndOutputStream::wrap(
        kj::heap<DummyStream>(),
        [&] { uncleanEndCalled = true; });
    stream->write("world"_kjb).wait(waitScope);
    KJ_EXPECT(!uncleanEndCalled, "uncleanEnd should not be called until stream goes out of scope");
  }

  KJ_EXPECT(uncleanEndCalled, "uncleanEnd should be called when end was not called");
}

// TODO:
// - Parallel writes (requires streaming)
// - Write to KJ -> capnp -> RPC -> capnp -> KJ loopback without shortening, verify we can write
//   several things to buffer (requires streaming).
// - Again, but with shortening which only occurs after some promise resolve.

}  // namespace
}  // namespace capnp
