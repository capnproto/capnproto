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

#include "rpc.h"
#include "capability-context.h"
#include "test-util.h"
#include <kj/debug.h>
#include <gtest/gtest.h>
#include <capnp/rpc.capnp.h>
#include <map>
#include <queue>

namespace capnp {
namespace _ {  // private
namespace {

class TestNetworkAdapter;

class TestNetwork {
public:
  ~TestNetwork() noexcept(false);

  TestNetworkAdapter& add(kj::StringPtr name);

  kj::Maybe<TestNetworkAdapter&> find(kj::StringPtr name) {
    auto iter = map.find(name);
    if (iter == map.end()) {
      return nullptr;
    } else {
      return *iter->second;
    }
  }

private:
  std::map<kj::StringPtr, kj::Own<TestNetworkAdapter>> map;
};

typedef VatNetwork<
    test::TestSturdyRefHostId, test::TestProvisionId, test::TestRecipientId,
    test::TestThirdPartyCapId, test::TestJoinResult> TestNetworkAdapterBase;

class TestNetworkAdapter final: public TestNetworkAdapterBase {
public:
  TestNetworkAdapter(TestNetwork& network): network(network) {}

  uint getSentCount() { return sent; }
  uint getReceivedCount() { return received; }

  typedef TestNetworkAdapterBase::Connection Connection;

  class ConnectionImpl final: public Connection, public kj::Refcounted {
  public:
    ConnectionImpl(TestNetworkAdapter& network, const char* name): network(network), name(name) {}

    void attach(ConnectionImpl& other) {
      KJ_REQUIRE(partner == nullptr);
      KJ_REQUIRE(other.partner == nullptr);
      partner = other;
      other.partner = *this;
    }

    class IncomingRpcMessageImpl final: public IncomingRpcMessage {
    public:
      IncomingRpcMessageImpl(uint firstSegmentWordSize)
          : message(firstSegmentWordSize == 0 ? SUGGESTED_FIRST_SEGMENT_WORDS
                                              : firstSegmentWordSize) {}

      ObjectPointer::Reader getBody() override {
        return message.getRoot<ObjectPointer>().asReader();
      }

      MallocMessageBuilder message;
    };

    class OutgoingRpcMessageImpl final: public OutgoingRpcMessage {
    public:
      OutgoingRpcMessageImpl(const ConnectionImpl& connection, uint firstSegmentWordSize)
          : connection(connection),
            message(kj::heap<IncomingRpcMessageImpl>(firstSegmentWordSize)) {}

      ObjectPointer::Builder getBody() override {
        return message->message.getRoot<ObjectPointer>();
      }
      void send() override {
        ++connection.network.sent;

        kj::String msg = kj::str(connection.name, ": ", message->message.getRoot<rpc::Message>());
        //KJ_DBG(msg);

        KJ_IF_MAYBE(p, connection.partner) {
          auto lock = p->queues.lockExclusive();
          if (lock->fulfillers.empty()) {
            lock->messages.push(kj::mv(message));
          } else {
            ++connection.network.received;
            lock->fulfillers.front()->fulfill(kj::Own<IncomingRpcMessage>(kj::mv(message)));
            lock->fulfillers.pop();
          }
        }
      }

    private:
      const ConnectionImpl& connection;
      kj::Own<IncomingRpcMessageImpl> message;
    };

    kj::Own<OutgoingRpcMessage> newOutgoingMessage(uint firstSegmentWordSize) const override {
      return kj::heap<OutgoingRpcMessageImpl>(*this, firstSegmentWordSize);
    }
    kj::Promise<kj::Maybe<kj::Own<IncomingRpcMessage>>> receiveIncomingMessage() override {
      auto lock = queues.lockExclusive();
      if (lock->messages.empty()) {
        auto paf = kj::newPromiseAndFulfiller<kj::Maybe<kj::Own<IncomingRpcMessage>>>();
        lock->fulfillers.push(kj::mv(paf.fulfiller));
        return kj::mv(paf.promise);
      } else {
        ++network.received;
        auto result = kj::mv(lock->messages.front());
        lock->messages.pop();
        return kj::Maybe<kj::Own<IncomingRpcMessage>>(kj::mv(result));
      }
    }
    void introduceTo(Connection& recipient,
        test::TestThirdPartyCapId::Builder sendToRecipient,
        test::TestRecipientId::Builder sendToTarget) override {
      KJ_FAIL_ASSERT("not implemented");
    }
    ConnectionAndProvisionId connectToIntroduced(
        test::TestThirdPartyCapId::Reader capId) override {
      KJ_FAIL_ASSERT("not implemented");
    }
    kj::Own<Connection> acceptIntroducedConnection(
        test::TestRecipientId::Reader recipientId) override {
      KJ_FAIL_ASSERT("not implemented");
    }

  private:
    TestNetworkAdapter& network;
    const char* name;
    kj::Maybe<ConnectionImpl&> partner;

    struct Queues {
      std::queue<kj::Own<kj::PromiseFulfiller<kj::Maybe<kj::Own<IncomingRpcMessage>>>>> fulfillers;
      std::queue<kj::Own<IncomingRpcMessage>> messages;
    };
    kj::MutexGuarded<Queues> queues;
  };

  kj::Maybe<kj::Own<Connection>> connectToRefHost(
      test::TestSturdyRefHostId::Reader hostId) override {
    TestNetworkAdapter& dst = KJ_REQUIRE_NONNULL(network.find(hostId.getHost()));

    kj::Locked<State> myLock;
    kj::Locked<State> dstLock;

    if (&dst < this) {
      dstLock = dst.state.lockExclusive();
      myLock = state.lockExclusive();
    } else {
      myLock = state.lockExclusive();
      dstLock = dst.state.lockExclusive();
    }

    auto iter = myLock->connections.find(&dst);
    if (iter == myLock->connections.end()) {
      auto local = kj::refcounted<ConnectionImpl>(*this, "client");
      auto remote = kj::refcounted<ConnectionImpl>(dst, "server");
      local->attach(*remote);

      myLock->connections[&dst] = kj::addRef(*local);
      dstLock->connections[this] = kj::addRef(*remote);

      if (dstLock->fulfillerQueue.empty()) {
        dstLock->connectionQueue.push(kj::mv(remote));
      } else {
        dstLock->fulfillerQueue.front()->fulfill(kj::mv(remote));
        dstLock->fulfillerQueue.pop();
      }

      return kj::Own<Connection>(kj::mv(local));
    } else {
      return kj::Own<Connection>(kj::addRef(*iter->second));
    }
  }

  kj::Promise<kj::Own<Connection>> acceptConnectionAsRefHost() override {
    auto lock = state.lockExclusive();
    if (lock->connectionQueue.empty()) {
      auto paf = kj::newPromiseAndFulfiller<kj::Own<Connection>>();
      lock->fulfillerQueue.push(kj::mv(paf.fulfiller));
      return kj::mv(paf.promise);
    } else {
      auto result = kj::mv(lock->connectionQueue.front());
      lock->connectionQueue.pop();
      return kj::mv(result);
    }
  }

private:
  TestNetwork& network;
  uint sent = 0;
  uint received = 0;

  struct State {
    std::map<const TestNetworkAdapter*, kj::Own<ConnectionImpl>> connections;
    std::queue<kj::Own<kj::PromiseFulfiller<kj::Own<Connection>>>> fulfillerQueue;
    std::queue<kj::Own<Connection>> connectionQueue;
  };
  kj::MutexGuarded<State> state;
};

TestNetwork::~TestNetwork() noexcept(false) {}

TestNetworkAdapter& TestNetwork::add(kj::StringPtr name) {
  return *(map[name] = kj::heap<TestNetworkAdapter>(*this));
}

// =======================================================================================

class TestRestorer final: public SturdyRefRestorer<test::TestSturdyRefObjectId> {
public:
  int callCount = 0;

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
        return kj::heap<TestMoreStuffImpl>(callCount);
    }
    KJ_UNREACHABLE;
  }
};

class RpcTest: public testing::Test {
protected:
  TestNetwork network;
  TestRestorer restorer;
  kj::SimpleEventLoop loop;
  TestNetworkAdapter& clientNetwork;
  TestNetworkAdapter& serverNetwork;
  RpcSystem<test::TestSturdyRefHostId> rpcClient;
  RpcSystem<test::TestSturdyRefHostId> rpcServer;

  Capability::Client connect(test::TestSturdyRefObjectId::Tag tag) {
    MallocMessageBuilder refMessage(128);
    auto ref = refMessage.initRoot<rpc::SturdyRef>();
    auto hostId = ref.getHostId().initAs<test::TestSturdyRefHostId>();
    hostId.setHost("server");
    ref.getObjectId().initAs<test::TestSturdyRefObjectId>().setTag(tag);

    return rpcClient.restore(hostId, ref.getObjectId());
  }

  RpcTest()
      : clientNetwork(network.add("client")),
        serverNetwork(network.add("server")),
        rpcClient(makeRpcClient(clientNetwork, loop)),
        rpcServer(makeRpcServer(serverNetwork, restorer, loop)) {}

  ~RpcTest() noexcept {}
  // Need to declare this with explicit noexcept otherwise it conflicts with testing::Test::~Test.
  // (Urgh, C++11, why did you change this?)
};

TEST_F(RpcTest, Basic) {
  auto client = connect(test::TestSturdyRefObjectId::Tag::TEST_INTERFACE)
      .castAs<test::TestInterface>();

  auto request1 = client.fooRequest();
  request1.setI(123);
  request1.setJ(true);
  auto promise1 = request1.send();

  // We used to call bar() after baz(), hence the numbering, but this masked the case where the
  // RPC system actually disconnected on bar() (thus returning an exception, which we decided
  // was expected).
  bool barFailed = false;
  auto request3 = client.barRequest();
  auto promise3 = loop.there(request3.send(),
      [](Response<test::TestInterface::BarResults>&& response) {
        ADD_FAILURE() << "Expected bar() call to fail.";
      }, [&](kj::Exception&& e) {
        barFailed = true;
      });

  auto request2 = client.bazRequest();
  initTestMessage(request2.initS());
  auto promise2 = request2.send();

  EXPECT_EQ(0, restorer.callCount);

  auto response1 = loop.wait(kj::mv(promise1));

  EXPECT_EQ("foo", response1.getX());

  auto response2 = loop.wait(kj::mv(promise2));

  loop.wait(kj::mv(promise3));

  EXPECT_EQ(2, restorer.callCount);
  EXPECT_TRUE(barFailed);
}

TEST_F(RpcTest, Pipelining) {
  auto client = connect(test::TestSturdyRefObjectId::Tag::TEST_PIPELINE)
      .castAs<test::TestPipeline>();

  int chainedCallCount = 0;

  auto request = client.getCapRequest();
  request.setN(234);
  request.setInCap(test::TestInterface::Client(
      kj::heap<TestInterfaceImpl>(chainedCallCount), loop));

  auto promise = request.send();

  auto pipelineRequest = promise.getOutBox().getCap().fooRequest();
  pipelineRequest.setI(321);
  auto pipelinePromise = pipelineRequest.send();

  auto pipelineRequest2 = promise.getOutBox().getCap().castAs<test::TestExtends>().graultRequest();
  auto pipelinePromise2 = pipelineRequest2.send();

  promise = nullptr;  // Just to be annoying, drop the original promise.

  EXPECT_EQ(0, restorer.callCount);
  EXPECT_EQ(0, chainedCallCount);

  auto response = loop.wait(kj::mv(pipelinePromise));
  EXPECT_EQ("bar", response.getX());

  auto response2 = loop.wait(kj::mv(pipelinePromise2));
  checkTestMessage(response2);

  EXPECT_EQ(3, restorer.callCount);
  EXPECT_EQ(1, chainedCallCount);
}

TEST_F(RpcTest, TailCall) {
  auto caller = connect(test::TestSturdyRefObjectId::Tag::TEST_TAIL_CALLER)
      .castAs<test::TestTailCaller>();

  int calleeCallCount = 0;

  test::TestTailCallee::Client callee(kj::heap<TestTailCalleeImpl>(calleeCallCount), loop);

  auto request = caller.fooRequest();
  request.setI(456);
  request.setCallee(callee);

  auto promise = request.send();

  auto dependentCall0 = promise.getC().getCallSequenceRequest().send();

  auto response = loop.wait(kj::mv(promise));
  EXPECT_EQ(456, response.getI());
  EXPECT_EQ(456, response.getI());

  auto dependentCall1 = promise.getC().getCallSequenceRequest().send();

  auto dependentCall2 = response.getC().getCallSequenceRequest().send();

  EXPECT_EQ(0, loop.wait(kj::mv(dependentCall0)).getN());
  EXPECT_EQ(1, loop.wait(kj::mv(dependentCall1)).getN());
  EXPECT_EQ(2, loop.wait(kj::mv(dependentCall2)).getN());

  EXPECT_EQ(1, calleeCallCount);
  EXPECT_EQ(1, restorer.callCount);
}

TEST_F(RpcTest, PromiseResolve) {
  auto client = connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  int chainedCallCount = 0;

  auto request = client.callFooRequest();
  auto request2 = client.callFooWhenResolvedRequest();

  auto paf = kj::newPromiseAndFulfiller<test::TestInterface::Client>();

  {
    auto fork = loop.fork(kj::mv(paf.promise));
    request.setCap(test::TestInterface::Client(fork.addBranch(), loop));
    request2.setCap(test::TestInterface::Client(fork.addBranch(), loop));
  }

  auto promise = request.send();
  auto promise2 = request2.send();

  // Make sure getCap() has been called on the server side by sending another call and waiting
  // for it.
  EXPECT_EQ(2, loop.wait(client.getCallSequenceRequest().send()).getN());
  EXPECT_EQ(3, restorer.callCount);

  // OK, now fulfill the local promise.
  paf.fulfiller->fulfill(test::TestInterface::Client(
      kj::heap<TestInterfaceImpl>(chainedCallCount), loop));

  // We should now be able to wait for getCap() to finish.
  EXPECT_EQ("bar", loop.wait(kj::mv(promise)).getS());
  EXPECT_EQ("bar", loop.wait(kj::mv(promise2)).getS());

  EXPECT_EQ(3, restorer.callCount);
  EXPECT_EQ(2, chainedCallCount);
}

class TestCapDestructor final: public test::TestInterface::Server {
public:
  TestCapDestructor(kj::Own<kj::PromiseFulfiller<void>>&& fulfiller)
      : fulfiller(kj::mv(fulfiller)), impl(dummy) {}

  ~TestCapDestructor() {
    fulfiller->fulfill();
  }

  kj::Promise<void> foo(FooParams::Reader params, FooResults::Builder result) {
    return impl.foo(params, result);
  }

private:
  kj::Own<kj::PromiseFulfiller<void>> fulfiller;
  int dummy = 0;
  TestInterfaceImpl impl;
};

TEST_F(RpcTest, RetainAndRelease) {
  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = loop.there(kj::mv(paf.promise), [&]() { destroyed = true; });
  destructionPromise.eagerlyEvaluate(loop);

  {
    auto client = connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
        .castAs<test::TestMoreStuff>();

    {
      auto request = client.holdRequest();
      request.setCap(test::TestInterface::Client(
          kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller)), loop));
      loop.wait(request.send());
    }

    // Do some other call to add a round trip.
    EXPECT_EQ(1, loop.wait(client.getCallSequenceRequest().send()).getN());

    // Shouldn't be destroyed because it's being held by the server.
    EXPECT_FALSE(destroyed);

    // We can ask it to call the held capability.
    EXPECT_EQ("bar", loop.wait(client.callHeldRequest().send()).getS());

    {
      // We can get the cap back from it.
      auto capCopy = loop.wait(client.getHeldRequest().send()).getCap();

      {
        // And call it, without any network communications.
        uint oldSentCount = clientNetwork.getSentCount();
        auto request = capCopy.fooRequest();
        request.setI(123);
        request.setJ(true);
        EXPECT_EQ("foo", loop.wait(request.send()).getX());
        EXPECT_EQ(oldSentCount, clientNetwork.getSentCount());
      }

      {
        // We can send another copy of the same cap to another method, and it works.
        auto request = client.callFooRequest();
        request.setCap(capCopy);
        EXPECT_EQ("bar", loop.wait(request.send()).getS());
      }
    }

    // Give some time to settle.
    EXPECT_EQ(5, loop.wait(client.getCallSequenceRequest().send()).getN());
    EXPECT_EQ(6, loop.wait(client.getCallSequenceRequest().send()).getN());
    EXPECT_EQ(7, loop.wait(client.getCallSequenceRequest().send()).getN());

    // Can't be destroyed, we haven't released it.
    EXPECT_FALSE(destroyed);
  }

  // We released our client, which should cause the server to be released, which in turn will
  // release the cap pointing back to us.
  loop.wait(kj::mv(destructionPromise));
  EXPECT_TRUE(destroyed);
}

TEST_F(RpcTest, Cancel) {
  auto client = connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = loop.there(kj::mv(paf.promise), [&]() { destroyed = true; });
  destructionPromise.eagerlyEvaluate(loop);

  {
    auto request = client.neverReturnRequest();
    request.setCap(test::TestInterface::Client(
        kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller)), loop));

    {
      auto responsePromise = request.send();

      // Allow some time to settle.
      EXPECT_EQ(1, loop.wait(client.getCallSequenceRequest().send()).getN());
      EXPECT_EQ(2, loop.wait(client.getCallSequenceRequest().send()).getN());

      // The cap shouldn't have been destroyed yet because the call never returned.
      EXPECT_FALSE(destroyed);
    }
  }

  // Now the cap should be released.
  loop.wait(kj::mv(destructionPromise));
  EXPECT_TRUE(destroyed);
}

TEST_F(RpcTest, SendTwice) {
  auto client = connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = loop.there(kj::mv(paf.promise), [&]() { destroyed = true; });
  destructionPromise.eagerlyEvaluate(loop);

  auto cap = test::TestInterface::Client(kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller)), loop);

  {
    auto request = client.callFooRequest();
    request.setCap(cap);

    EXPECT_EQ("bar", loop.wait(request.send()).getS());
  }

  // Allow some time for the server to release `cap`.
  EXPECT_EQ(1, loop.wait(client.getCallSequenceRequest().send()).getN());

  {
    // More requests with the same cap.
    auto request = client.callFooRequest();
    auto request2 = client.callFooRequest();
    request.setCap(cap);
    request2.setCap(kj::mv(cap));

    auto promise = request.send();
    auto promise2 = request2.send();

    EXPECT_EQ("bar", loop.wait(kj::mv(promise)).getS());
    EXPECT_EQ("bar", loop.wait(kj::mv(promise2)).getS());
  }

  // Now the cap should be released.
  loop.wait(kj::mv(destructionPromise));
  EXPECT_TRUE(destroyed);
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
