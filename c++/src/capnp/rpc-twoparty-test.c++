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

#define CAPNP_TESTING_CAPNP 1

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// Includes just for need SOL_SOCKET and SO_SNDBUF
#if _WIN32
#include <kj/win32-api-version.h>
#endif

#include "rpc-twoparty.h"
#include "test-util.h"
#include <capnp/rpc.capnp.h>
#include <kj/debug.h>
#include <kj/thread.h>
#include <kj/compat/gtest.h>
#include <kj/miniposix.h>

#if _WIN32
#include <winsock2.h>
#include <mswsock.h>
#include <kj/windows-sanity.h>
#else
#include <sys/socket.h>
#endif

// TODO(cleanup): Auto-generate stringification functions for union discriminants.
namespace capnp {
namespace rpc {
inline kj::String KJ_STRINGIFY(Message::Which which) {
  return kj::str(static_cast<uint16_t>(which));
}
}  // namespace rpc
}  // namespace capnp

namespace capnp {
namespace _ {
namespace {

class TestRestorer final: public SturdyRefRestorer<test::TestSturdyRefObjectId> {
public:
  TestRestorer(int& callCount, int& handleCount)
      : callCount(callCount), handleCount(handleCount) {}

  Capability::Client restore(test::TestSturdyRefObjectId::Reader objectId) override {
    switch (objectId.getTag()) {
      case test::TestSturdyRefObjectId::Tag::TEST_INTERFACE:
        return kj::heap<TestInterfaceImpl>(callCount);
      case test::TestSturdyRefObjectId::Tag::TEST_EXTENDS:
        return Capability::Client(newBrokenCap("No TestExtends implemented."));
      case test::TestSturdyRefObjectId::Tag::TEST_PIPELINE:
        return kj::heap<TestPipelineImpl>(callCount);
      case test::TestSturdyRefObjectId::Tag::TEST_TAIL_CALLEE:
        return kj::heap<TestTailCalleeImpl>(callCount);
      case test::TestSturdyRefObjectId::Tag::TEST_TAIL_CALLER:
        return kj::heap<TestTailCallerImpl>(callCount);
      case test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF:
        return kj::heap<TestMoreStuffImpl>(callCount, handleCount);
    }
    KJ_UNREACHABLE;
  }

private:
  int& callCount;
  int& handleCount;
};

class TestMonotonicClock final: public kj::MonotonicClock {
public:
  kj::TimePoint now() const override {
    return time;
  }

  void reset() { time = kj::systemCoarseMonotonicClock().now(); }
  void increment(kj::Duration d) { time += d; }
private:
  kj::TimePoint time = kj::systemCoarseMonotonicClock().now();
};

kj::AsyncIoProvider::PipeThread runServer(kj::AsyncIoProvider& ioProvider,
                                          int& callCount, int& handleCount) {
  return ioProvider.newPipeThread(
      [&callCount, &handleCount](
       kj::AsyncIoProvider& ioProvider, kj::AsyncIoStream& stream, kj::WaitScope& waitScope) {
    TwoPartyVatNetwork network(stream, rpc::twoparty::Side::SERVER);
    TestRestorer restorer(callCount, handleCount);
    auto server = makeRpcServer(network, restorer);
    network.onDisconnect().wait(waitScope);
  });
}

Capability::Client getPersistentCap(RpcSystem<rpc::twoparty::VatId>& client,
                                    rpc::twoparty::Side side,
                                    test::TestSturdyRefObjectId::Tag tag) {
  // Create the VatId.
  MallocMessageBuilder hostIdMessage(8);
  auto hostId = hostIdMessage.initRoot<rpc::twoparty::VatId>();
  hostId.setSide(side);

  // Create the SturdyRefObjectId.
  MallocMessageBuilder objectIdMessage(8);
  objectIdMessage.initRoot<test::TestSturdyRefObjectId>().setTag(tag);

  // Connect to the remote capability.
  return client.restore(hostId, objectIdMessage.getRoot<AnyPointer>());
}

TEST(TwoPartyNetwork, Basic) {
  auto ioContext = kj::setupAsyncIo();
  TestMonotonicClock clock;
  int callCount = 0;
  int handleCount = 0;

  auto serverThread = runServer(*ioContext.provider, callCount, handleCount);
  TwoPartyVatNetwork network(*serverThread.pipe, rpc::twoparty::Side::CLIENT, capnp::ReaderOptions(), clock);
  auto rpcClient = makeRpcClient(network);

  KJ_EXPECT(network.getCurrentQueueCount() == 0);
  KJ_EXPECT(network.getCurrentQueueSize() == 0);
  KJ_EXPECT(network.getOutgoingMessageWaitTime() == 0 * kj::SECONDS);

  // Request the particular capability from the server.
  auto client = getPersistentCap(rpcClient, rpc::twoparty::Side::SERVER,
      test::TestSturdyRefObjectId::Tag::TEST_INTERFACE).castAs<test::TestInterface>();
  clock.increment(1 * kj::SECONDS);

  KJ_EXPECT(network.getCurrentQueueCount() == 1);
  KJ_EXPECT(network.getCurrentQueueSize() % sizeof(word) == 0);
  KJ_EXPECT(network.getCurrentQueueSize() > 0);
  KJ_EXPECT(network.getOutgoingMessageWaitTime() == 1 * kj::SECONDS);
  size_t oldSize = network.getCurrentQueueSize();

  // Use the capability.
  auto request1 = client.fooRequest();
  request1.setI(123);
  request1.setJ(true);
  auto promise1 = request1.send();

  KJ_EXPECT(network.getCurrentQueueCount() == 2);
  KJ_EXPECT(network.getCurrentQueueSize() % sizeof(word) == 0);
  KJ_EXPECT(network.getCurrentQueueSize() > oldSize);
  KJ_EXPECT(network.getOutgoingMessageWaitTime() == 1 * kj::SECONDS);
  oldSize = network.getCurrentQueueSize();

  auto request2 = client.bazRequest();
  initTestMessage(request2.initS());
  auto promise2 = request2.send();

  KJ_EXPECT(network.getCurrentQueueCount() == 3);
  KJ_EXPECT(network.getCurrentQueueSize() % sizeof(word) == 0);
  KJ_EXPECT(network.getCurrentQueueSize() > oldSize);
  oldSize = network.getCurrentQueueSize();

  clock.increment(1 * kj::SECONDS);

  bool barFailed = false;
  auto request3 = client.barRequest();
  auto promise3 = request3.send().then(
      [](Response<test::TestInterface::BarResults>&& response) {
        ADD_FAILURE() << "Expected bar() call to fail.";
      }, [&](kj::Exception&& e) {
        barFailed = true;
      });

  EXPECT_EQ(0, callCount);

  KJ_EXPECT(network.getCurrentQueueCount() == 4);
  KJ_EXPECT(network.getCurrentQueueSize() % sizeof(word) == 0);
  KJ_EXPECT(network.getCurrentQueueSize() > oldSize);
  // Oldest message is now 2 seconds old
  KJ_EXPECT(network.getOutgoingMessageWaitTime() == 2 * kj::SECONDS);
  oldSize = network.getCurrentQueueSize();

  auto response1 = promise1.wait(ioContext.waitScope);

  EXPECT_EQ("foo", response1.getX());

  auto response2 = promise2.wait(ioContext.waitScope);

  promise3.wait(ioContext.waitScope);

  EXPECT_EQ(2, callCount);
  EXPECT_TRUE(barFailed);

  // There's still a `Finish` message queued.
  KJ_EXPECT(network.getCurrentQueueCount() > 0);
  KJ_EXPECT(network.getCurrentQueueSize() > 0);
  // Oldest message was sent, next oldest should be 0 seconds old since we haven't incremented
  // the clock yet.
  KJ_EXPECT(network.getOutgoingMessageWaitTime() == 0 * kj::SECONDS);

  // Let any I/O finish.
  kj::Promise<void>(kj::NEVER_DONE).poll(ioContext.waitScope);

  // Now nothing is queued.
  KJ_EXPECT(network.getCurrentQueueCount() == 0);
  KJ_EXPECT(network.getCurrentQueueSize() == 0);

  // Ensure that sending a message after not sending one for some time
  // doesn't return incorrect waitTime statistics.
  clock.increment(10 * kj::SECONDS);

  auto request4 = client.fooRequest();
  request4.setI(123);
  request4.setJ(true);
  auto promise4 = request4.send();

  KJ_EXPECT(network.getCurrentQueueCount() == 1);
  KJ_EXPECT(network.getOutgoingMessageWaitTime() == 0 * kj::SECONDS);
}

TEST(TwoPartyNetwork, Pipelining) {
  auto ioContext = kj::setupAsyncIo();
  int callCount = 0;
  int handleCount = 0;
  int reverseCallCount = 0;  // Calls back from server to client.

  auto serverThread = runServer(*ioContext.provider, callCount, handleCount);
  TwoPartyVatNetwork network(*serverThread.pipe, rpc::twoparty::Side::CLIENT);
  auto rpcClient = makeRpcClient(network);

  bool disconnected = false;
  kj::Promise<void> disconnectPromise = network.onDisconnect().then([&]() { disconnected = true; });

  {
    // Request the particular capability from the server.
    auto client = getPersistentCap(rpcClient, rpc::twoparty::Side::SERVER,
        test::TestSturdyRefObjectId::Tag::TEST_PIPELINE).castAs<test::TestPipeline>();

    {
      // Use the capability.
      auto request = client.getCapRequest();
      request.setN(234);
      request.setInCap(kj::heap<TestInterfaceImpl>(reverseCallCount));

      auto promise = request.send();

      auto pipelineRequest = promise.getOutBox().getCap().fooRequest();
      pipelineRequest.setI(321);
      auto pipelinePromise = pipelineRequest.send();

      auto pipelineRequest2 = promise.getOutBox().getCap()
          .castAs<test::TestExtends>().graultRequest();
      auto pipelinePromise2 = pipelineRequest2.send();

      promise = nullptr;  // Just to be annoying, drop the original promise.

      EXPECT_EQ(0, callCount);
      EXPECT_EQ(0, reverseCallCount);

      auto response = pipelinePromise.wait(ioContext.waitScope);
      EXPECT_EQ("bar", response.getX());

      auto response2 = pipelinePromise2.wait(ioContext.waitScope);
      checkTestMessage(response2);

      EXPECT_EQ(3, callCount);
      EXPECT_EQ(1, reverseCallCount);
    }

    EXPECT_FALSE(disconnected);

    // What if we disconnect?
    // TODO(cleanup): This is kind of cheating, we are shutting down the underlying socket to
    //   simulate a disconnect, but it's weird to pull the rug out from under our VatNetwork like
    //   this and it causes a bit of a race between write failures and read failures. This part of
    //   the test should maybe be restructured.
    serverThread.pipe->shutdownWrite();

    // The other side should also disconnect.
    disconnectPromise.wait(ioContext.waitScope);

    {
      // Use the now-broken capability.
      auto request = client.getCapRequest();
      request.setN(234);
      request.setInCap(kj::heap<TestInterfaceImpl>(reverseCallCount));

      auto promise = request.send();

      auto pipelineRequest = promise.getOutBox().getCap().fooRequest();
      pipelineRequest.setI(321);
      auto pipelinePromise = pipelineRequest.send();

      auto pipelineRequest2 = promise.getOutBox().getCap()
          .castAs<test::TestExtends>().graultRequest();
      auto pipelinePromise2 = pipelineRequest2.send();

      pipelinePromise.then([](auto) {
        KJ_FAIL_EXPECT("should have thrown");
      }, [](kj::Exception&& e) {
        KJ_EXPECT(e.getType() == kj::Exception::Type::DISCONNECTED);
        // I wish we could test stack traces somehow... oh well.
      }).wait(ioContext.waitScope);

      pipelinePromise2.then([](auto) {
        KJ_FAIL_EXPECT("should have thrown");
      }, [](kj::Exception&& e) {
        KJ_EXPECT(e.getType() == kj::Exception::Type::DISCONNECTED);
        // I wish we could test stack traces somehow... oh well.
      }).wait(ioContext.waitScope);

      EXPECT_EQ(3, callCount);
      EXPECT_EQ(1, reverseCallCount);
    }
  }
}

TEST(TwoPartyNetwork, Release) {
  auto ioContext = kj::setupAsyncIo();
  int callCount = 0;
  int handleCount = 0;

  auto serverThread = runServer(*ioContext.provider, callCount, handleCount);
  TwoPartyVatNetwork network(*serverThread.pipe, rpc::twoparty::Side::CLIENT);
  auto rpcClient = makeRpcClient(network);

  // Request the particular capability from the server.
  auto client = getPersistentCap(rpcClient, rpc::twoparty::Side::SERVER,
      test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF).castAs<test::TestMoreStuff>();

  auto handle1 = client.getHandleRequest().send().wait(ioContext.waitScope).getHandle();
  auto promise = client.getHandleRequest().send();
  auto handle2 = promise.wait(ioContext.waitScope).getHandle();

  EXPECT_EQ(2, handleCount);

  handle1 = nullptr;

  // There once was a bug where the last outgoing message (and any capabilities attached) would
  // not get cleaned up (until a new message was sent). This appeared to be a bug in Release,
  // because if a client received a message and then released a capability from it but then did
  // not make any further calls, then the capability would not be released because the message
  // introducing it remained the last server -> client message (because a "Release" message has
  // no reply). Here we are explicitly trying to catch this bug. This proves tricky, because when
  // we drop a reference on the client side, there's no particular way to wait for the release
  // message to reach the server except to make a subsequent call and wait for the return -- but
  // that would mask the bug. So, we wait spin waiting for handleCount to change.

  uint maxSpins = 1000;

  while (handleCount > 1) {
    ioContext.provider->getTimer().afterDelay(10 * kj::MILLISECONDS).wait(ioContext.waitScope);
    KJ_ASSERT(--maxSpins > 0);
  }
  EXPECT_EQ(1, handleCount);

  handle2 = nullptr;

  ioContext.provider->getTimer().afterDelay(10 * kj::MILLISECONDS).wait(ioContext.waitScope);
  EXPECT_EQ(1, handleCount);

  promise = nullptr;

  while (handleCount > 0) {
    ioContext.provider->getTimer().afterDelay(10 * kj::MILLISECONDS).wait(ioContext.waitScope);
    KJ_ASSERT(--maxSpins > 0);
  }
  EXPECT_EQ(0, handleCount);
}

TEST(TwoPartyNetwork, Abort) {
  // Verify that aborts are received.

  auto ioContext = kj::setupAsyncIo();
  int callCount = 0;
  int handleCount = 0;

  auto serverThread = runServer(*ioContext.provider, callCount, handleCount);
  TwoPartyVatNetwork network(*serverThread.pipe, rpc::twoparty::Side::CLIENT);

  MallocMessageBuilder refMessage(128);
  auto hostId = refMessage.initRoot<rpc::twoparty::VatId>();
  hostId.setSide(rpc::twoparty::Side::SERVER);

  auto conn = KJ_ASSERT_NONNULL(network.connect(hostId));

  {
    // Send an invalid message (Return to non-existent question).
    auto msg = conn->newOutgoingMessage(128);
    auto body = msg->getBody().initAs<rpc::Message>().initReturn();
    body.setAnswerId(1234);
    body.setCanceled();
    msg->send();
  }

  {
    auto reply = KJ_ASSERT_NONNULL(conn->receiveIncomingMessage().wait(ioContext.waitScope));
    EXPECT_EQ(rpc::Message::ABORT, reply->getBody().getAs<rpc::Message>().which());
  }

  EXPECT_TRUE(conn->receiveIncomingMessage().wait(ioContext.waitScope) == nullptr);
}

TEST(TwoPartyNetwork, ConvenienceClasses) {
  auto ioContext = kj::setupAsyncIo();

  int callCount = 0;
  TwoPartyServer server(kj::heap<TestInterfaceImpl>(callCount));

  auto address = ioContext.provider->getNetwork()
      .parseAddress("127.0.0.1").wait(ioContext.waitScope);

  auto listener = address->listen();
  auto listenPromise = server.listen(*listener);

  address = ioContext.provider->getNetwork()
      .parseAddress("127.0.0.1", listener->getPort()).wait(ioContext.waitScope);

  auto connection = address->connect().wait(ioContext.waitScope);
  TwoPartyClient client(*connection);
  auto cap = client.bootstrap().castAs<test::TestInterface>();

  auto request = cap.fooRequest();
  request.setI(123);
  request.setJ(true);
  EXPECT_EQ(0, callCount);
  auto response = request.send().wait(ioContext.waitScope);
  EXPECT_EQ("foo", response.getX());
  EXPECT_EQ(1, callCount);
}

TEST(TwoPartyNetwork, HugeMessage) {
  auto ioContext = kj::setupAsyncIo();
  int callCount = 0;
  int handleCount = 0;

  auto serverThread = runServer(*ioContext.provider, callCount, handleCount);
  TwoPartyVatNetwork network(*serverThread.pipe, rpc::twoparty::Side::CLIENT);
  auto rpcClient = makeRpcClient(network);

  auto client = getPersistentCap(rpcClient, rpc::twoparty::Side::SERVER,
      test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF).castAs<test::TestMoreStuff>();

  // Oversized request fails.
  {
    auto req = client.methodWithDefaultsRequest();
    req.initA(100000000);  // 100 MB

    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("larger than our single-message size limit",
        req.send().ignoreResult().wait(ioContext.waitScope));
  }

  // Oversized response fails.
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("larger than our single-message size limit",
      client.getEnormousStringRequest().send().ignoreResult().wait(ioContext.waitScope));

  // Connection is still up.
  {
    auto req = client.getCallSequenceRequest();
    req.setExpected(0);
    KJ_EXPECT(req.send().wait(ioContext.waitScope).getN() == 0);
  }
}

class TestAuthenticatedBootstrapImpl final
    : public test::TestAuthenticatedBootstrap<rpc::twoparty::VatId>::Server {
public:
  TestAuthenticatedBootstrapImpl(rpc::twoparty::VatId::Reader clientId) {
    this->clientId.setRoot(clientId);
  }

protected:
  kj::Promise<void> getCallerId(GetCallerIdContext context) override {
    context.getResults().setCaller(clientId.getRoot<rpc::twoparty::VatId>());
    return kj::READY_NOW;
  }

private:
  MallocMessageBuilder clientId;
};

class TestBootstrapFactory: public BootstrapFactory<rpc::twoparty::VatId> {
public:
  Capability::Client createFor(rpc::twoparty::VatId::Reader clientId) {
    called = true;
    EXPECT_EQ(rpc::twoparty::Side::CLIENT, clientId.getSide());
    return kj::heap<TestAuthenticatedBootstrapImpl>(clientId);
  }

  bool called = false;
};

kj::AsyncIoProvider::PipeThread runAuthenticatingServer(
    kj::AsyncIoProvider& ioProvider, BootstrapFactory<rpc::twoparty::VatId>& bootstrapFactory) {
  return ioProvider.newPipeThread([&bootstrapFactory](
      kj::AsyncIoProvider& ioProvider, kj::AsyncIoStream& stream, kj::WaitScope& waitScope) {
    TwoPartyVatNetwork network(stream, rpc::twoparty::Side::SERVER);
    auto server = makeRpcServer(network, bootstrapFactory);
    network.onDisconnect().wait(waitScope);
  });
}

TEST(TwoPartyNetwork, BootstrapFactory) {
  auto ioContext = kj::setupAsyncIo();
  TestBootstrapFactory bootstrapFactory;
  auto serverThread = runAuthenticatingServer(*ioContext.provider, bootstrapFactory);
  TwoPartyClient client(*serverThread.pipe);
  auto resp = client.bootstrap().castAs<test::TestAuthenticatedBootstrap<rpc::twoparty::VatId>>()
      .getCallerIdRequest().send().wait(ioContext.waitScope);
  EXPECT_EQ(rpc::twoparty::Side::CLIENT, resp.getCaller().getSide());
  EXPECT_TRUE(bootstrapFactory.called);
}

// =======================================================================================

#if !_WIN32 && !__CYGWIN__  // Windows and Cygwin don't support SCM_RIGHTS.
KJ_TEST("send FD over RPC") {
  auto io = kj::setupAsyncIo();

  int callCount = 0;
  int handleCount = 0;
  TwoPartyServer server(kj::heap<TestMoreStuffImpl>(callCount, handleCount));
  auto pipe = io.provider->newCapabilityPipe();
  server.accept(kj::mv(pipe.ends[0]), 2);
  TwoPartyClient client(*pipe.ends[1], 2);

  auto cap = client.bootstrap().castAs<test::TestMoreStuff>();

  int pipeFds[2];
  KJ_SYSCALL(kj::miniposix::pipe(pipeFds));
  kj::AutoCloseFd in1(pipeFds[0]);
  kj::AutoCloseFd out1(pipeFds[1]);
  KJ_SYSCALL(kj::miniposix::pipe(pipeFds));
  kj::AutoCloseFd in2(pipeFds[0]);
  kj::AutoCloseFd out2(pipeFds[1]);

  capnp::RemotePromise<test::TestMoreStuff::WriteToFdResults> promise = nullptr;
  {
    auto req = cap.writeToFdRequest();

    // Order reversal intentional, just trying to mix things up.
    req.setFdCap1(kj::heap<TestFdCap>(kj::mv(out2)));
    req.setFdCap2(kj::heap<TestFdCap>(kj::mv(out1)));

    promise = req.send();
  }

  int in3 = KJ_ASSERT_NONNULL(promise.getFdCap3().getFd().wait(io.waitScope));
  KJ_EXPECT(io.lowLevelProvider->wrapInputFd(kj::mv(in3))->readAllText().wait(io.waitScope)
            == "baz");

  {
    auto promise2 = kj::mv(promise);  // make sure the PipelineHook also goes out of scope
    auto response = promise2.wait(io.waitScope);
    KJ_EXPECT(response.getSecondFdPresent());
  }

  KJ_EXPECT(io.lowLevelProvider->wrapInputFd(kj::mv(in1))->readAllText().wait(io.waitScope)
            == "bar");
  KJ_EXPECT(io.lowLevelProvider->wrapInputFd(kj::mv(in2))->readAllText().wait(io.waitScope)
            == "foo");
}

KJ_TEST("FD per message limit") {
  auto io = kj::setupAsyncIo();

  int callCount = 0;
  int handleCount = 0;
  TwoPartyServer server(kj::heap<TestMoreStuffImpl>(callCount, handleCount));
  auto pipe = io.provider->newCapabilityPipe();
  server.accept(kj::mv(pipe.ends[0]), 1);
  TwoPartyClient client(*pipe.ends[1], 1);

  auto cap = client.bootstrap().castAs<test::TestMoreStuff>();

  int pipeFds[2];
  KJ_SYSCALL(kj::miniposix::pipe(pipeFds));
  kj::AutoCloseFd in1(pipeFds[0]);
  kj::AutoCloseFd out1(pipeFds[1]);
  KJ_SYSCALL(kj::miniposix::pipe(pipeFds));
  kj::AutoCloseFd in2(pipeFds[0]);
  kj::AutoCloseFd out2(pipeFds[1]);

  capnp::RemotePromise<test::TestMoreStuff::WriteToFdResults> promise = nullptr;
  {
    auto req = cap.writeToFdRequest();

    // Order reversal intentional, just trying to mix things up.
    req.setFdCap1(kj::heap<TestFdCap>(kj::mv(out2)));
    req.setFdCap2(kj::heap<TestFdCap>(kj::mv(out1)));

    promise = req.send();
  }

  int in3 = KJ_ASSERT_NONNULL(promise.getFdCap3().getFd().wait(io.waitScope));
  KJ_EXPECT(io.lowLevelProvider->wrapInputFd(kj::mv(in3))->readAllText().wait(io.waitScope)
            == "baz");

  {
    auto promise2 = kj::mv(promise);  // make sure the PipelineHook also goes out of scope
    auto response = promise2.wait(io.waitScope);
    KJ_EXPECT(!response.getSecondFdPresent());
  }

  KJ_EXPECT(io.lowLevelProvider->wrapInputFd(kj::mv(in1))->readAllText().wait(io.waitScope)
            == "");
  KJ_EXPECT(io.lowLevelProvider->wrapInputFd(kj::mv(in2))->readAllText().wait(io.waitScope)
            == "foo");
}
#endif  // !_WIN32 && !__CYGWIN__

// =======================================================================================

class MockSndbufStream final: public kj::AsyncIoStream {
public:
  MockSndbufStream(kj::Own<AsyncIoStream> inner, size_t& window, size_t& written)
      : inner(kj::mv(inner)), window(window), written(written) {}

  kj::Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return inner->read(buffer, minBytes, maxBytes);
  }
  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return inner->tryRead(buffer, minBytes, maxBytes);
  }
  kj::Maybe<uint64_t> tryGetLength() override {
    return inner->tryGetLength();
  }
  kj::Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
    return inner->pumpTo(output, amount);
  }
  kj::Promise<void> write(const void* buffer, size_t size) override {
    written += size;
    return inner->write(buffer, size);
  }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    for (auto& piece: pieces) written += piece.size();
    return inner->write(pieces);
  }
  kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(
      kj::AsyncInputStream& input, uint64_t amount) override {
    return inner->tryPumpFrom(input, amount);
  }
  kj::Promise<void> whenWriteDisconnected() override { return inner->whenWriteDisconnected(); }
  void shutdownWrite() override { return inner->shutdownWrite(); }
  void abortRead() override { return inner->abortRead(); }

  void getsockopt(int level, int option, void* value, uint* length) override {
    if (level == SOL_SOCKET && option == SO_SNDBUF) {
      KJ_ASSERT(*length == sizeof(int));
      *reinterpret_cast<int*>(value) = window;
    } else {
      KJ_UNIMPLEMENTED("not implemented for test", level, option);
    }
  }

private:
  kj::Own<AsyncIoStream> inner;
  size_t& window;
  size_t& written;
};

KJ_TEST("Streaming over RPC") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto pipe = kj::newTwoWayPipe();

  size_t window = 1024;
  size_t clientWritten = 0;
  size_t serverWritten = 0;

  pipe.ends[0] = kj::heap<MockSndbufStream>(kj::mv(pipe.ends[0]), window, clientWritten);
  pipe.ends[1] = kj::heap<MockSndbufStream>(kj::mv(pipe.ends[1]), window, serverWritten);

  auto ownServer = kj::heap<TestStreamingImpl>();
  auto& server = *ownServer;
  test::TestStreaming::Client serverCap(kj::mv(ownServer));

  TwoPartyClient tpClient(*pipe.ends[0]);
  TwoPartyClient tpServer(*pipe.ends[1], serverCap, rpc::twoparty::Side::SERVER);

  auto cap = tpClient.bootstrap().castAs<test::TestStreaming>();

  // Send stream requests until we can't anymore.
  kj::Promise<void> promise = kj::READY_NOW;
  uint count = 0;
  while (promise.poll(waitScope)) {
    promise.wait(waitScope);

    auto req = cap.doStreamIRequest();
    req.setI(++count);
    promise = req.send();
  }

  // We should have sent... several.
  KJ_EXPECT(count > 5);

  // Now, cause calls to finish server-side one-at-a-time and check that this causes the client
  // side to be willing to send more.
  uint countReceived = 0;
  for (uint i = 0; i < 50; i++) {
    KJ_EXPECT(server.iSum == ++countReceived);
    server.iSum = 0;
    KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();

    KJ_ASSERT(promise.poll(waitScope));
    promise.wait(waitScope);

    auto req = cap.doStreamIRequest();
    req.setI(++count);
    promise = req.send();
    if (promise.poll(waitScope)) {
      // We'll see a couple of instances where completing one request frees up space to make two
      // more. This is because the first few requests we made are a little bit larger than the
      // rest due to being pipelined on the bootstrap. Once the bootstrap resolves, the request
      // size gets smaller.
      promise.wait(waitScope);
      req = cap.doStreamIRequest();
      req.setI(++count);
      promise = req.send();

      // We definitely shouldn't have freed up stream space for more than two additional requests!
      KJ_ASSERT(!promise.poll(waitScope));
    }
  }
}

KJ_TEST("Streaming over a chain of local and remote RPC calls") {
  // This test verifies that a local RPC call that eventually resolves to a remote RPC call will
  // still support streaming calls over the remote connection.

  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  // Set up a local server that will eventually delegate requests to a remote server.
  auto localPaf = kj::newPromiseAndFulfiller<test::TestStreaming::Client>();
  test::TestStreaming::Client promisedClient(kj::mv(localPaf.promise));

  uint count = 0;
  auto req = promisedClient.doStreamIRequest();
  req.setI(++count);
  auto promise = req.send();

  // Expect streaming request to be blocked on promised client.
  KJ_EXPECT(!promise.poll(waitScope));

  // Set up a remote server with a flow control window for streaming.
  auto pipe = kj::newTwoWayPipe();

  size_t window = 1024;
  size_t clientWritten = 0;
  size_t serverWritten = 0;

  pipe.ends[0] = kj::heap<MockSndbufStream>(kj::mv(pipe.ends[0]), window, clientWritten);
  pipe.ends[1] = kj::heap<MockSndbufStream>(kj::mv(pipe.ends[1]), window, serverWritten);

  auto remotePaf = kj::newPromiseAndFulfiller<test::TestStreaming::Client>();
  test::TestStreaming::Client serverCap(kj::mv(remotePaf.promise));

  TwoPartyClient tpClient(*pipe.ends[0]);
  TwoPartyClient tpServer(*pipe.ends[1], kj::mv(serverCap), rpc::twoparty::Side::SERVER);

  auto clientCap = tpClient.bootstrap().castAs<test::TestStreaming>();

  // Expect streaming request to be unblocked by fulfilling promised client with remote server.
  localPaf.fulfiller->fulfill(kj::mv(clientCap));
  KJ_EXPECT(promise.poll(waitScope));

  // Send stream requests until we can't anymore.
  while (promise.poll(waitScope)) {
    promise.wait(waitScope);

    auto req = promisedClient.doStreamIRequest();
    req.setI(++count);
    promise = req.send();
    KJ_ASSERT(count < 1000);
  }

  // Expect several stream requests to have fit in the flow control window.
  KJ_EXPECT(count > 5);

  auto finishReq = promisedClient.finishStreamRequest();
  auto finishPromise = finishReq.send();
  KJ_EXPECT(!finishPromise.poll(waitScope));

  // Finish calls on server
  auto ownServer = kj::heap<TestStreamingImpl>();
  auto& server = *ownServer;
  remotePaf.fulfiller->fulfill(kj::mv(ownServer));
  KJ_EXPECT(!promise.poll(waitScope));

  uint countReceived = 0;
  for (uint i = 0; i < count; i++) {
    KJ_EXPECT(server.iSum == ++countReceived);
    server.iSum = 0;
    KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();

    if (i < count - 1) {
      KJ_EXPECT(!finishPromise.poll(waitScope));
    }
  }

  KJ_EXPECT(finishPromise.poll(waitScope));
  finishPromise.wait(waitScope);
}

KJ_TEST("Streaming over RPC then unwrap with CapabilitySet") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto pipe = kj::newTwoWayPipe();

  CapabilityServerSet<test::TestStreaming> capSet;

  auto ownServer = kj::heap<TestStreamingImpl>();
  auto& server = *ownServer;
  auto serverCap = capSet.add(kj::mv(ownServer));

  auto paf = kj::newPromiseAndFulfiller<test::TestStreaming::Client>();

  TwoPartyClient tpClient(*pipe.ends[0], serverCap);
  TwoPartyClient tpServer(*pipe.ends[1], kj::mv(paf.promise), rpc::twoparty::Side::SERVER);

  auto clientCap = tpClient.bootstrap().castAs<test::TestStreaming>();

  // Send stream requests until we can't anymore.
  kj::Promise<void> promise = kj::READY_NOW;
  uint count = 0;
  while (promise.poll(waitScope)) {
    promise.wait(waitScope);

    auto req = clientCap.doStreamIRequest();
    req.setI(++count);
    promise = req.send();
  }

  // We should have sent... several.
  KJ_EXPECT(count > 10);

  // Now try to unwrap.
  auto unwrapPromise = capSet.getLocalServer(clientCap);

  // It won't work yet, obviously, because we haven't resolved the promise.
  KJ_EXPECT(!unwrapPromise.poll(waitScope));

  // So do that.
  paf.fulfiller->fulfill(tpServer.bootstrap().castAs<test::TestStreaming>());
  clientCap.whenResolved().wait(waitScope);

  // But the unwrap still doesn't resolve because streaming requests are queued up.
  KJ_EXPECT(!unwrapPromise.poll(waitScope));

  // OK, let's resolve a streaming request.
  KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();

  // All of our call promises have now completed from the client's perspective.
  promise.wait(waitScope);

  // But we still can't unwrap, because calls are queued server-side.
  KJ_EXPECT(!unwrapPromise.poll(waitScope));

  // Let's even make one more call now. But this is actually a local call since the promise
  // resolved.
  {
    auto req = clientCap.doStreamIRequest();
    req.setI(++count);
    promise = req.send();
  }

  // Because it's a local call, it doesn't resolve early. The window is no longer in effect.
  KJ_EXPECT(!promise.poll(waitScope));
  KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();
  KJ_EXPECT(!promise.poll(waitScope));
  KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();
  KJ_EXPECT(!promise.poll(waitScope));
  KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();
  KJ_EXPECT(!promise.poll(waitScope));
  KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();
  KJ_EXPECT(!promise.poll(waitScope));

  // Our unwrap promise is also still not resolved.
  KJ_EXPECT(!unwrapPromise.poll(waitScope));

  // Close out stream calls until it does resolve!
  while (!unwrapPromise.poll(waitScope)) {
    KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();
  }

  // Now we can unwrap!
  KJ_EXPECT(&KJ_ASSERT_NONNULL(unwrapPromise.wait(waitScope)) == &server);

  // But our last stream call still isn't done.
  KJ_EXPECT(!promise.poll(waitScope));

  // Finish it.
  KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();
  promise.wait(waitScope);
}

KJ_TEST("promise cap resolves between starting request and sending it") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  auto pipe = kj::newTwoWayPipe();

  // Client exports TestCallOrderImpl as its bootstrap.
  TwoPartyClient client(*pipe.ends[0], kj::heap<TestCallOrderImpl>(), rpc::twoparty::Side::CLIENT);

  // Server exports a promise, which will later resolve to loop back to the capability the client
  // exported.
  auto paf = kj::newPromiseAndFulfiller<Capability::Client>();
  TwoPartyClient server(*pipe.ends[1], kj::mv(paf.promise), rpc::twoparty::Side::SERVER);

  // Create a request but don't send it yet.
  auto cap = client.bootstrap().castAs<test::TestCallOrder>();
  auto req1 = cap.getCallSequenceRequest();

  // Fulfill the promise now so that the server's bootstrap loops back to the client's bootstrap.
  paf.fulfiller->fulfill(server.bootstrap());
  cap.whenResolved().wait(waitScope);

  // Send the request we created earlier, and also create and send a second request.
  auto promise1 = req1.send();
  auto promise2 = cap.getCallSequenceRequest().send();

  // They should arrive in order of send()s.
  auto n1 = promise1.wait(waitScope).getN();
  KJ_EXPECT(n1 == 0, n1);
  auto n2 = promise2.wait(waitScope).getN();
  KJ_EXPECT(n2 == 1, n2);
}

KJ_TEST("write error propagates to read error") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  auto frontPipe = kj::newTwoWayPipe();
  auto backPipe = kj::newTwoWayPipe();

  TwoPartyClient client(*frontPipe.ends[0]);

  int callCount;
  TwoPartyClient server(*backPipe.ends[1], kj::heap<TestInterfaceImpl>(callCount),
                        rpc::twoparty::Side::SERVER);

  auto pumpUpTask = frontPipe.ends[1]->pumpTo(*backPipe.ends[0]);
  auto pumpDownTask = backPipe.ends[0]->pumpTo(*frontPipe.ends[1]);

  auto cap = client.bootstrap().castAs<test::TestInterface>();

  // Make sure the connections work.
  {
    auto req = cap.fooRequest();
    req.setI(123);
    req.setJ(true);
    auto resp = req.send().wait(waitScope);
    EXPECT_EQ("foo", resp.getX());
  }

  // Disconnect upstream task in such a way that future writes on the client will fail, but the
  // server doesn't notice the disconnect and so won't react.
  pumpUpTask = nullptr;
  frontPipe.ends[1]->abortRead();  // causes write() on ends[0] to fail in the future

  {
    auto req = cap.fooRequest();
    req.setI(123);
    req.setJ(true);
    auto promise = req.send().then([](auto) {
      KJ_FAIL_EXPECT("expected exception");
    }, [](kj::Exception&& e) {
      KJ_ASSERT(e.getDescription() == "abortRead() has been called");
    });

    KJ_ASSERT(promise.poll(waitScope));
    promise.wait(waitScope);
  }
}

class TestStreamingCancellationBug final: public test::TestStreaming::Server {
public:
  uint iSum = 0;
  kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> fulfiller;

  kj::Promise<void> doStreamI(DoStreamIContext context) override {
    auto paf = kj::newPromiseAndFulfiller<void>();
    fulfiller = kj::mv(paf.fulfiller);
    return paf.promise.then([this,context]() mutable {
      // Don't count the sum until here so we actually detect if the call is canceled.
      iSum += context.getParams().getI();
    });
  }

  kj::Promise<void> finishStream(FinishStreamContext context) override {
    auto results = context.getResults();
    results.setTotalI(iSum);
    return kj::READY_NOW;
  }
};

KJ_TEST("Streaming over RPC no premature cancellation when client dropped") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto pipe = kj::newTwoWayPipe();

  auto ownServer = kj::heap<TestStreamingCancellationBug>();
  auto& server = *ownServer;
  test::TestStreaming::Client serverCap = kj::mv(ownServer);

  TwoPartyClient tpClient(*pipe.ends[0]);
  TwoPartyClient tpServer(*pipe.ends[1], kj::mv(serverCap), rpc::twoparty::Side::SERVER);

  auto client = tpClient.bootstrap().castAs<test::TestStreaming>();

  kj::Promise<void> promise1 = nullptr, promise2 = nullptr;

  {
    auto req = client.doStreamIRequest();
    req.setI(123);
    promise1 = req.send();
  }
  {
    auto req = client.doStreamIRequest();
    req.setI(456);
    promise2 = req.send();
  }

  auto finishPromise = client.finishStreamRequest().send();

  KJ_EXPECT(server.iSum == 0);

  // Drop the client. This shouldn't cause a problem for the already-running RPCs.
  { auto drop = kj::mv(client); }

  while (!finishPromise.poll(waitScope)) {
    KJ_ASSERT_NONNULL(server.fulfiller)->fulfill();
  }

  finishPromise.wait(waitScope);
  KJ_EXPECT(server.iSum == 579);
}

KJ_TEST("Dropping capability during call doesn't destroy server") {
  class TestInterfaceImpl final: public test::TestInterface::Server {
    // An object which increments a count in the constructor and decrements it in the destructor,
    // to detect when it is destroyed. The object's foo() method also sets a fulfiller to use to
    // cause the method to complete.
  public:
    TestInterfaceImpl(uint& count, kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>>& fulfillerSlot)
        : count(count), fulfillerSlot(fulfillerSlot) { ++count; }
    ~TestInterfaceImpl() noexcept(false) { --count; }

    kj::Promise<void> foo(FooContext context) override {
      auto paf = kj::newPromiseAndFulfiller<void>();
      fulfillerSlot = kj::mv(paf.fulfiller);
      return kj::mv(paf.promise);
    }

  private:
    uint& count;
    kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>>& fulfillerSlot;
  };

  class TestBootstrapImpl final: public test::TestMoreStuff::Server {
    // Bootstrap object which just vends instances of `TestInterfaceImpl`.
  public:
    TestBootstrapImpl(uint& count, kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>>& fulfillerSlot)
        : count(count), fulfillerSlot(fulfillerSlot) {}

    kj::Promise<void> getHeld(GetHeldContext context) override {
      context.initResults().setCap(kj::heap<TestInterfaceImpl>(count, fulfillerSlot));
      return kj::READY_NOW;
    }

  private:
    uint& count;
    kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>>& fulfillerSlot;
  };

  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  auto pipe = kj::newTwoWayPipe();

  uint count = 0;
  kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> fulfillerSlot;
  test::TestMoreStuff::Client bootstrap = kj::heap<TestBootstrapImpl>(count, fulfillerSlot);

  TwoPartyClient tpClient(*pipe.ends[0]);
  TwoPartyClient tpServer(*pipe.ends[1], kj::mv(bootstrap), rpc::twoparty::Side::SERVER);

  auto cap = tpClient.bootstrap().castAs<test::TestMoreStuff>().getHeldRequest().send().getCap();

  waitScope.poll();
  auto promise = cap.fooRequest().send();
  KJ_EXPECT(!promise.poll(waitScope));
  KJ_EXPECT(count == 1);
  KJ_EXPECT(fulfillerSlot != nullptr);

  // Dropping the capability should not destroy the server as long as the call is still
  // outstanding.
  {auto drop = kj::mv(cap);}

  KJ_EXPECT(!promise.poll(waitScope));
  KJ_EXPECT(count == 1);

  // Cancelling the call still should not destroy the server because the call is not marked to
  // allow cancellation. So the call should keep running.
  {auto drop = kj::mv(promise);}

  waitScope.poll();
  KJ_EXPECT(count == 1);

  // When the call completes, only then should the server be dropped.
  KJ_ASSERT_NONNULL(fulfillerSlot)->fulfill();

  waitScope.poll();
  KJ_EXPECT(count == 0);
}

RemotePromise<test::TestCallOrder::GetCallSequenceResults> getCallSequence(
    test::TestCallOrder::Client& client, uint expected) {
  auto req = client.getCallSequenceRequest();
  req.setExpected(expected);
  return req.send();
}

KJ_TEST("Two-hop embargo") {
  // Copied from `TEST(Rpc, Embargo)` in `rpc-test.c++`, adapted to involve a two-hop path through
  // a proxy. This tests what happens when disembargoes on multiple hops are happening in parallel.

  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0, handleCount = 0;

  // Set up two two-party RPC connections in series. The middle node just proxies requests through.
  auto frontPipe = kj::newTwoWayPipe();
  auto backPipe = kj::newTwoWayPipe();
  TwoPartyClient tpClient(*frontPipe.ends[0]);
  TwoPartyClient proxyBack(*backPipe.ends[0]);
  TwoPartyClient proxyFront(*frontPipe.ends[1], proxyBack.bootstrap(), rpc::twoparty::Side::SERVER);
  TwoPartyClient tpServer(*backPipe.ends[1], kj::heap<TestMoreStuffImpl>(callCount, handleCount),
      rpc::twoparty::Side::SERVER);

  // Perform some logic that does a bunch of promise pipelining, including passing a capability
  // from the client to the server and back to the client, and making promise-pipelined calls on
  // that capability. This should exercise the promise resolution and disembargo code.
  auto client = tpClient.bootstrap().castAs<test::TestMoreStuff>();

  auto cap = test::TestCallOrder::Client(kj::heap<TestCallOrderImpl>());

  auto earlyCall = client.getCallSequenceRequest().send();

  auto echoRequest = client.echoRequest();
  echoRequest.setCap(cap);
  auto echo = echoRequest.send();

  auto pipeline = echo.getCap();

  auto call0 = getCallSequence(pipeline, 0);
  auto call1 = getCallSequence(pipeline, 1);

  earlyCall.wait(waitScope);

  auto call2 = getCallSequence(pipeline, 2);

  auto resolved = echo.wait(waitScope).getCap();

  auto call3 = getCallSequence(pipeline, 3);
  auto call4 = getCallSequence(pipeline, 4);
  auto call5 = getCallSequence(pipeline, 5);

  EXPECT_EQ(0, call0.wait(waitScope).getN());
  EXPECT_EQ(1, call1.wait(waitScope).getN());
  EXPECT_EQ(2, call2.wait(waitScope).getN());
  EXPECT_EQ(3, call3.wait(waitScope).getN());
  EXPECT_EQ(4, call4.wait(waitScope).getN());
  EXPECT_EQ(5, call5.wait(waitScope).getN());
}

class TestCallOrderImplAsPromise final: public test::TestCallOrder::Server {
  // This is an implementation of TestCallOrder that presents itself as a promise by implementing
  // `shortenPath()`, although it never resolves to anything (`shortenPath()` never completes).
  // This tests deeper code paths in promise resolution and embargo code.
public:
  template <typename... Params>
  TestCallOrderImplAsPromise(Params&&... params): inner(kj::fwd<Params>(params)...) {}

  kj::Promise<void> getCallSequence(GetCallSequenceContext context) override {
    return inner.getCallSequence(context);
  }

  kj::Maybe<kj::Promise<Capability::Client>> shortenPath() override {
    // Make this object appear to be a promise.
    return kj::Promise<Capability::Client>(kj::NEVER_DONE);
  }

private:
  TestCallOrderImpl inner;
};

KJ_TEST("Two-hop embargo") {
  // Same as above, but the eventual resolution is itself a promise. This verifies that
  // handleDisembargo() only waits for the target to resolve back to the capability that the
  // disembargo should reflect to, but not beyond that.

  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  int callCount = 0, handleCount = 0;

  // Set up two two-party RPC connections in series. The middle node just proxies requests through.
  auto frontPipe = kj::newTwoWayPipe();
  auto backPipe = kj::newTwoWayPipe();
  TwoPartyClient tpClient(*frontPipe.ends[0]);
  TwoPartyClient proxyBack(*backPipe.ends[0]);
  TwoPartyClient proxyFront(*frontPipe.ends[1], proxyBack.bootstrap(), rpc::twoparty::Side::SERVER);
  TwoPartyClient tpServer(*backPipe.ends[1], kj::heap<TestMoreStuffImpl>(callCount, handleCount),
      rpc::twoparty::Side::SERVER);

  // Perform some logic that does a bunch of promise pipelining, including passing a capability
  // from the client to the server and back to the client, and making promise-pipelined calls on
  // that capability. This should exercise the promise resolution and disembargo code.
  auto client = tpClient.bootstrap().castAs<test::TestMoreStuff>();

  auto cap = test::TestCallOrder::Client(kj::heap<TestCallOrderImplAsPromise>());

  auto earlyCall = client.getCallSequenceRequest().send();

  auto echoRequest = client.echoRequest();
  echoRequest.setCap(cap);
  auto echo = echoRequest.send();

  auto pipeline = echo.getCap();

  auto call0 = getCallSequence(pipeline, 0);
  auto call1 = getCallSequence(pipeline, 1);

  earlyCall.wait(waitScope);

  auto call2 = getCallSequence(pipeline, 2);

  auto resolved = echo.wait(waitScope).getCap();

  auto call3 = getCallSequence(pipeline, 3);
  auto call4 = getCallSequence(pipeline, 4);
  auto call5 = getCallSequence(pipeline, 5);

  EXPECT_EQ(0, call0.wait(waitScope).getN());
  EXPECT_EQ(1, call1.wait(waitScope).getN());
  EXPECT_EQ(2, call2.wait(waitScope).getN());
  EXPECT_EQ(3, call3.wait(waitScope).getN());
  EXPECT_EQ(4, call4.wait(waitScope).getN());
  EXPECT_EQ(5, call5.wait(waitScope).getN());
}

}  // namespace
}  // namespace _
}  // namespace capnp
