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

#include "rpc-twoparty.h"
#include "test-util.h"
#include <capnp/rpc.capnp.h>
#include <kj/debug.h>
#include <kj/thread.h>
#include <kj/compat/gtest.h>

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

    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("larger than the single-message size limit",
        req.send().ignoreResult().wait(ioContext.waitScope));
  }

  // Oversized response fails.
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("larger than the single-message size limit",
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

}  // namespace
}  // namespace _
}  // namespace capnp
