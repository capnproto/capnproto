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
#include "schema.h"
#include <kj/debug.h>
#include <kj/string-tree.h>
#include <gtest/gtest.h>
#include <capnp/rpc.capnp.h>
#include <map>
#include <queue>

namespace capnp {
namespace _ {  // private
namespace {

class RpcDumper {
  // Class which stringifies RPC messages for debugging purposes, including decoding params and
  // results based on the call's interface and method IDs and extracting cap descriptors.

public:
  void addSchema(InterfaceSchema schema) {
    schemas[schema.getProto().getId()] = schema;
  }

  enum Sender {
    CLIENT,
    SERVER
  };

  kj::String dump(rpc::Message::Reader message, Sender sender) {
    const char* senderName = sender == CLIENT ? "client" : "server";

    switch (message.which()) {
      case rpc::Message::CALL: {
        auto call = message.getCall();
        auto iter = schemas.find(call.getInterfaceId());
        if (iter == schemas.end()) {
          break;
        }
        InterfaceSchema schema = iter->second;
        auto methods = schema.getMethods();
        if (call.getMethodId() >= methods.size()) {
          break;
        }
        InterfaceSchema::Method method = methods[call.getMethodId()];

        auto schemaProto = schema.getProto();
        auto interfaceName =
            schemaProto.getDisplayName().slice(schemaProto.getDisplayNamePrefixLength());

        auto methodProto = method.getProto();
        auto paramType = schema.getDependency(methodProto.getParamStructType()).asStruct();
        auto resultType = schema.getDependency(methodProto.getResultStructType()).asStruct();

        if (call.getSendResultsTo().isCaller()) {
          returnTypes[std::make_pair(sender, call.getQuestionId())] = resultType;
        }

        CapExtractorImpl extractor;
        CapReaderContext context(extractor);

        auto params = kj::str(context.imbue(call.getParams()).getAs<DynamicStruct>(paramType));

        auto sendResultsTo = call.getSendResultsTo();

        return kj::str(senderName, "(", call.getQuestionId(), "): call ",
                       call.getTarget(), " <- ", interfaceName, ".",
                       methodProto.getName(), params,
                       " caps:[", extractor.printCaps(), "]",
                       sendResultsTo.isCaller() ? kj::str()
                                                : kj::str(" sendResultsTo:", sendResultsTo));
      }

      case rpc::Message::RETURN: {
        auto ret = message.getReturn();

        auto iter = returnTypes.find(
            std::make_pair(sender == CLIENT ? SERVER : CLIENT, ret.getQuestionId()));
        if (iter == returnTypes.end()) {
          break;
        }

        auto schema = iter->second;
        returnTypes.erase(iter);
        if (ret.which() != rpc::Return::RESULTS) {
          // Oops, no results returned.  We don't check this earlier because we want to make sure
          // returnTypes.erase() gets a chance to happen.
          break;
        }

        CapExtractorImpl extractor;
        CapReaderContext context(extractor);
        auto imbued = context.imbue(ret.getResults());

        if (schema.getProto().isStruct()) {
          auto results = kj::str(imbued.getAs<DynamicStruct>(schema.asStruct()));

          return kj::str(senderName, "(", ret.getQuestionId(), "): return ", results,
                         " caps:[", extractor.printCaps(), "]");
        } else if (schema.getProto().isInterface()) {
          imbued.getAs<DynamicCapability>(schema.asInterface());
          return kj::str(senderName, "(", ret.getQuestionId(), "): return cap ",
                         extractor.printCaps());
        } else {
          break;
        }
      }

      case rpc::Message::RESTORE: {
        auto restore = message.getRestore();

        returnTypes[std::make_pair(sender, restore.getQuestionId())] = InterfaceSchema();

        return kj::str(senderName, "(", restore.getQuestionId(), "): restore ",
                       restore.getObjectId().getAs<test::TestSturdyRefObjectId>());
      }

      default:
        break;
    }

    return kj::str(senderName, ": ", message);
  }

private:
  std::map<uint64_t, InterfaceSchema> schemas;
  std::map<std::pair<Sender, uint32_t>, Schema> returnTypes;

  class CapExtractorImpl: public CapExtractor<rpc::CapDescriptor> {
  public:
    kj::Own<const ClientHook> extractCap(rpc::CapDescriptor::Reader descriptor) const {
      caps.add(kj::str(descriptor));
      return newBrokenCap("fake cap");
    }

    kj::String printCaps() {
      return kj::strArray(caps, ", ");
    }

  private:
    mutable kj::Vector<kj::String> caps;
  };
};

// =======================================================================================

class TestNetworkAdapter;

class TestNetwork {
public:
  TestNetwork(kj::EventLoop& loop): loop(loop) {
    dumper.addSchema(Schema::from<test::TestInterface>());
    dumper.addSchema(Schema::from<test::TestExtends>());
    dumper.addSchema(Schema::from<test::TestPipeline>());
    dumper.addSchema(Schema::from<test::TestCallOrder>());
    dumper.addSchema(Schema::from<test::TestTailCallee>());
    dumper.addSchema(Schema::from<test::TestTailCaller>());
    dumper.addSchema(Schema::from<test::TestMoreStuff>());
  }
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

  RpcDumper dumper;

private:
  kj::EventLoop& loop;
  std::map<kj::StringPtr, kj::Own<TestNetworkAdapter>> map;
};

typedef VatNetwork<
    test::TestSturdyRefHostId, test::TestProvisionId, test::TestRecipientId,
    test::TestThirdPartyCapId, test::TestJoinResult> TestNetworkAdapterBase;

class TestNetworkAdapter final: public TestNetworkAdapterBase {
public:
  TestNetworkAdapter(kj::EventLoop& loop, TestNetwork& network): loop(loop), network(network) {}

  uint getSentCount() { return sent; }
  uint getReceivedCount() { return received; }

  typedef TestNetworkAdapterBase::Connection Connection;

  class ConnectionImpl final
      : public Connection, public kj::Refcounted, public kj::TaskSet::ErrorHandler {
  public:
    ConnectionImpl(TestNetworkAdapter& network, RpcDumper::Sender sender)
        : network(network), sender(sender), tasks(network.loop, *this) {}

    void attach(ConnectionImpl& other) {
      KJ_REQUIRE(partner == nullptr);
      KJ_REQUIRE(other.partner == nullptr);
      partner = other;
      other.partner = *this;
    }

    class IncomingRpcMessageImpl final: public IncomingRpcMessage, public kj::Refcounted {
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
            message(kj::refcounted<IncomingRpcMessageImpl>(firstSegmentWordSize)) {}

      ObjectPointer::Builder getBody() override {
        return message->message.getRoot<ObjectPointer>();
      }
      void send() override {
        ++connection.network.sent;

        // Uncomment to get a debug dump.
//        kj::String msg = connection.network.network.dumper.dump(
//            message->message.getRoot<rpc::Message>(), connection.sender);
//        KJ_ DBG(msg);

        auto connectionPtr = &connection;
        connection.tasks.add(connection.network.loop.evalLater(
            kj::mvCapture(kj::addRef(*message),
                [connectionPtr](kj::Own<IncomingRpcMessageImpl>&& message) {
          KJ_IF_MAYBE(p, connectionPtr->partner) {
            auto lock = p->queues.lockExclusive();
            if (lock->fulfillers.empty()) {
              lock->messages.push(kj::mv(message));
            } else {
              ++connectionPtr->network.received;
              lock->fulfillers.front()->fulfill(kj::Own<IncomingRpcMessage>(kj::mv(message)));
              lock->fulfillers.pop();
            }
          }
        })));
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

    void taskFailed(kj::Exception&& exception) override {
      ADD_FAILURE() << kj::str(exception).cStr();
    }

  private:
    TestNetworkAdapter& network;
    RpcDumper::Sender sender KJ_UNUSED_MEMBER;
    kj::Maybe<ConnectionImpl&> partner;

    struct Queues {
      std::queue<kj::Own<kj::PromiseFulfiller<kj::Maybe<kj::Own<IncomingRpcMessage>>>>> fulfillers;
      std::queue<kj::Own<IncomingRpcMessage>> messages;
    };
    kj::MutexGuarded<Queues> queues;

    kj::TaskSet tasks;
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
      auto local = kj::refcounted<ConnectionImpl>(*this, RpcDumper::CLIENT);
      auto remote = kj::refcounted<ConnectionImpl>(dst, RpcDumper::SERVER);
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
  kj::EventLoop& loop;
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
  return *(map[name] = kj::heap<TestNetworkAdapter>(loop, *this));
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

struct TestContext {
  kj::SimpleEventLoop loop;
  TestNetwork network;
  TestRestorer restorer;
  TestNetworkAdapter& clientNetwork;
  TestNetworkAdapter& serverNetwork;
  RpcSystem<test::TestSturdyRefHostId> rpcClient;
  RpcSystem<test::TestSturdyRefHostId> rpcServer;

  TestContext()
      : network(loop),
        clientNetwork(network.add("client")),
        serverNetwork(network.add("server")),
        rpcClient(makeRpcClient(clientNetwork, loop)),
        rpcServer(makeRpcServer(serverNetwork, restorer, loop)) {}

  Capability::Client connect(test::TestSturdyRefObjectId::Tag tag) {
    MallocMessageBuilder refMessage(128);
    auto ref = refMessage.initRoot<rpc::SturdyRef>();
    auto hostId = ref.getHostId().initAs<test::TestSturdyRefHostId>();
    hostId.setHost("server");
    ref.getObjectId().initAs<test::TestSturdyRefObjectId>().setTag(tag);

    return rpcClient.restore(hostId, ref.getObjectId());
  }
};

TEST(Rpc, Basic) {
  TestContext context;
  auto& loop = context.loop;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_INTERFACE)
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

  EXPECT_EQ(0, context.restorer.callCount);

  auto response1 = loop.wait(kj::mv(promise1));

  EXPECT_EQ("foo", response1.getX());

  auto response2 = loop.wait(kj::mv(promise2));

  loop.wait(kj::mv(promise3));

  EXPECT_EQ(2, context.restorer.callCount);
  EXPECT_TRUE(barFailed);
}

TEST(Rpc, Pipelining) {
  TestContext context;
  auto& loop = context.loop;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_PIPELINE)
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

  EXPECT_EQ(0, context.restorer.callCount);
  EXPECT_EQ(0, chainedCallCount);

  auto response = loop.wait(kj::mv(pipelinePromise));
  EXPECT_EQ("bar", response.getX());

  auto response2 = loop.wait(kj::mv(pipelinePromise2));
  checkTestMessage(response2);

  EXPECT_EQ(3, context.restorer.callCount);
  EXPECT_EQ(1, chainedCallCount);
}

TEST(Rpc, TailCall) {
  TestContext context;
  auto& loop = context.loop;

  auto caller = context.connect(test::TestSturdyRefObjectId::Tag::TEST_TAIL_CALLER)
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
  EXPECT_EQ(1, context.restorer.callCount);
}

TEST(Rpc, AsyncCancelation) {
  // Tests allowAsyncCancellation().

  TestContext context;
  auto& loop = context.loop;

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = loop.there(kj::mv(paf.promise), [&]() { destroyed = true; });
  destructionPromise.eagerlyEvaluate(loop);

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  kj::Promise<void> promise = nullptr;

  bool returned = false;
  {
    auto request = client.expectAsyncCancelRequest();
    request.setCap(test::TestInterface::Client(
        kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller)), loop));
    promise = loop.there(request.send(),
        [&](Response<test::TestMoreStuff::ExpectAsyncCancelResults>&& response) {
      returned = true;
    });
    promise.eagerlyEvaluate(loop);
  }
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));

  // We can detect that the method was canceled because it will drop the cap.
  EXPECT_FALSE(destroyed);
  EXPECT_FALSE(returned);

  promise = nullptr;  // request cancellation
  loop.wait(kj::mv(destructionPromise));

  EXPECT_TRUE(destroyed);
  EXPECT_FALSE(returned);
}

TEST(Rpc, SyncCancelation) {
  // Tests isCanceled() without allowAsyncCancellation().

  TestContext context;
  auto& loop = context.loop;

  int innerCallCount = 0;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  kj::Promise<void> promise = nullptr;

  bool returned = false;
  {
    auto request = client.expectSyncCancelRequest();
    request.setCap(test::TestInterface::Client(
        kj::heap<TestInterfaceImpl>(innerCallCount), loop));
    promise = loop.there(request.send(),
        [&](Response<test::TestMoreStuff::ExpectSyncCancelResults>&& response) {
      returned = true;
    });
    promise.eagerlyEvaluate(loop);
  }
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));

  // expectSyncCancel() will make a call to the TestInterfaceImpl only once it noticed isCanceled()
  // is true.
  EXPECT_EQ(0, innerCallCount);
  EXPECT_FALSE(returned);

  promise = nullptr;  // request cancellation
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));
  loop.wait(loop.evalLater([]() {}));

  EXPECT_EQ(1, innerCallCount);
  EXPECT_FALSE(returned);
}

TEST(Rpc, PromiseResolve) {
  TestContext context;
  auto& loop = context.loop;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
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
  EXPECT_EQ(3, context.restorer.callCount);

  // OK, now fulfill the local promise.
  paf.fulfiller->fulfill(test::TestInterface::Client(
      kj::heap<TestInterfaceImpl>(chainedCallCount), loop));

  // We should now be able to wait for getCap() to finish.
  EXPECT_EQ("bar", loop.wait(kj::mv(promise)).getS());
  EXPECT_EQ("bar", loop.wait(kj::mv(promise2)).getS());

  EXPECT_EQ(3, context.restorer.callCount);
  EXPECT_EQ(2, chainedCallCount);
}

TEST(Rpc, RetainAndRelease) {
  TestContext context;
  auto& loop = context.loop;

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = loop.there(kj::mv(paf.promise), [&]() { destroyed = true; });
  destructionPromise.eagerlyEvaluate(loop);

  {
    auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
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
        uint oldSentCount = context.clientNetwork.getSentCount();
        auto request = capCopy.fooRequest();
        request.setI(123);
        request.setJ(true);
        EXPECT_EQ("foo", loop.wait(request.send()).getX());
        EXPECT_EQ(oldSentCount, context.clientNetwork.getSentCount());
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

TEST(Rpc, Cancel) {
  TestContext context;
  auto& loop = context.loop;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
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

TEST(Rpc, SendTwice) {
  TestContext context;
  auto& loop = context.loop;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
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

RemotePromise<test::TestCallOrder::GetCallSequenceResults> getCallSequence(
    const test::TestCallOrder::Client& client, uint expected) {
  auto req = client.getCallSequenceRequest();
  req.setExpected(expected);
  return req.send();
}

TEST(Rpc, Embargo) {
  TestContext context;
  auto& loop = context.loop;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  auto cap = test::TestCallOrder::Client(kj::heap<TestCallOrderImpl>(), loop);

  auto earlyCall = client.getCallSequenceRequest().send();

  auto echoRequest = client.echoRequest();
  echoRequest.setCap(cap);
  auto echo = echoRequest.send();

  auto pipeline = echo.getCap();

  auto call0 = getCallSequence(pipeline, 0);
  auto call1 = getCallSequence(pipeline, 1);

  loop.wait(kj::mv(earlyCall));

  auto call2 = getCallSequence(pipeline, 2);

  auto resolved = loop.wait(kj::mv(echo)).getCap();

  auto call3 = getCallSequence(pipeline, 3);
  auto call4 = getCallSequence(pipeline, 4);
  auto call5 = getCallSequence(pipeline, 5);

  EXPECT_EQ(0, loop.wait(kj::mv(call0)).getN());
  EXPECT_EQ(1, loop.wait(kj::mv(call1)).getN());
  EXPECT_EQ(2, loop.wait(kj::mv(call2)).getN());
  EXPECT_EQ(3, loop.wait(kj::mv(call3)).getN());
  EXPECT_EQ(4, loop.wait(kj::mv(call4)).getN());
  EXPECT_EQ(5, loop.wait(kj::mv(call5)).getN());
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
