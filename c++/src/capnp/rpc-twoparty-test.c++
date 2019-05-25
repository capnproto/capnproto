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

#include "rpc-twoparty.h"
#include "test-util.h"
#include <capnp/rpc.capnp.h>
#include <kj/debug.h>
#include <kj/thread.h>
#include <kj/compat/gtest.h>
#include <kj/miniposix.h>

// Includes just for need SOL_SOCKET and SO_SNDBUF
#if _WIN32
#define WIN32_LEAN_AND_MEAN  // ::eyeroll::
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
  int callCount = 0;
  int handleCount = 0;

  auto serverThread = runServer(*ioContext.provider, callCount, handleCount);
  TwoPartyVatNetwork network(*serverThread.pipe, rpc::twoparty::Side::CLIENT);
  auto rpcClient = makeRpcClient(network);

  // Request the particular capability from the server.
  auto client = getPersistentCap(rpcClient, rpc::twoparty::Side::SERVER,
      test::TestSturdyRefObjectId::Tag::TEST_INTERFACE).castAs<test::TestInterface>();

  // Use the capability.
  auto request1 = client.fooRequest();
  request1.setI(123);
  request1.setJ(true);
  auto promise1 = request1.send();

  auto request2 = client.bazRequest();
  initTestMessage(request2.initS());
  auto promise2 = request2.send();

  bool barFailed = false;
  auto request3 = client.barRequest();
  auto promise3 = request3.send().then(
      [](Response<test::TestInterface::BarResults>&& response) {
        ADD_FAILURE() << "Expected bar() call to fail.";
      }, [&](kj::Exception&& e) {
        barFailed = true;
      });

  EXPECT_EQ(0, callCount);

  auto response1 = promise1.wait(ioContext.waitScope);

  EXPECT_EQ("foo", response1.getX());

  auto response2 = promise2.wait(ioContext.waitScope);

  promise3.wait(ioContext.waitScope);

  EXPECT_EQ(2, callCount);
  EXPECT_TRUE(barFailed);
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

      EXPECT_ANY_THROW(pipelinePromise.wait(ioContext.waitScope));
      EXPECT_ANY_THROW(pipelinePromise2.wait(ioContext.waitScope));

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
  // becaues if a client received a message and then released a capability from it but then did
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

  auto reply = KJ_ASSERT_NONNULL(conn->receiveIncomingMessage().wait(ioContext.waitScope));
  EXPECT_EQ(rpc::Message::ABORT, reply->getBody().getAs<rpc::Message>().which());

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

}  // namespace
}  // namespace _
}  // namespace capnp
