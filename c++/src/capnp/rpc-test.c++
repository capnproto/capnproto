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

#include "rpc.h"
#include "test-util.h"
#include "schema.h"
#include "serialize.h"
#include <kj/debug.h>
#include <kj/string-tree.h>
#include <kj/compat/gtest.h>
#include <capnp/rpc.capnp.h>
#include <map>
#include <queue>

// TODO(cleanup): Auto-generate stringification functions for union discriminants.
namespace capnp {
namespace rpc {
inline kj::String KJ_STRINGIFY(Message::Which which) {
  return kj::str(static_cast<uint16_t>(which));
}
}  // namespace rpc
}  // namespace capnp

namespace capnp {
namespace _ {  // private
namespace {

class RpcDumper {
  // Class which stringifies RPC messages for debugging purposes, including decoding params and
  // results based on the call's interface and method IDs and extracting cap descriptors.
  //
  // TODO(cleanup):  Clean this code up and move it to someplace reusable, so it can be used as
  //   a packet inspector / debugging tool for Cap'n Proto network traffic.

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
        auto paramType = method.getParamType();
        auto resultType = method.getResultType();

        if (call.getSendResultsTo().isCaller()) {
          returnTypes[std::make_pair(sender, call.getQuestionId())] = resultType;
        }

        auto payload = call.getParams();
        auto params = kj::str(payload.getContent().getAs<DynamicStruct>(paramType));

        auto sendResultsTo = call.getSendResultsTo();

        return kj::str(senderName, "(", call.getQuestionId(), "): call ",
                       call.getTarget(), " <- ", interfaceName, ".",
                       methodProto.getName(), params,
                       " caps:[", kj::strArray(payload.getCapTable(), ", "), "]",
                       sendResultsTo.isCaller() ? kj::str()
                                                : kj::str(" sendResultsTo:", sendResultsTo));
      }

      case rpc::Message::RETURN: {
        auto ret = message.getReturn();

        auto iter = returnTypes.find(
            std::make_pair(sender == CLIENT ? SERVER : CLIENT, ret.getAnswerId()));
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

        auto payload = ret.getResults();

        if (schema.getProto().isStruct()) {
          auto results = kj::str(payload.getContent().getAs<DynamicStruct>(schema.asStruct()));

          return kj::str(senderName, "(", ret.getAnswerId(), "): return ", results,
                         " caps:[", kj::strArray(payload.getCapTable(), ", "), "]");
        } else if (schema.getProto().isInterface()) {
          payload.getContent().getAs<DynamicCapability>(schema.asInterface());
          return kj::str(senderName, "(", ret.getAnswerId(), "): return cap ",
                         kj::strArray(payload.getCapTable(), ", "));
        } else {
          break;
        }
      }

      case rpc::Message::BOOTSTRAP: {
        auto restore = message.getBootstrap();

        returnTypes[std::make_pair(sender, restore.getQuestionId())] = InterfaceSchema();

        return kj::str(senderName, "(", restore.getQuestionId(), "): bootstrap ",
                       restore.getDeprecatedObjectId().getAs<test::TestSturdyRefObjectId>());
      }

      default:
        break;
    }

    return kj::str(senderName, ": ", message);
  }

private:
  std::map<uint64_t, InterfaceSchema> schemas;
  std::map<std::pair<Sender, uint32_t>, Schema> returnTypes;
};

// =======================================================================================

class TestNetworkAdapter;

class TestNetwork {
public:
  TestNetwork() {
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
  std::map<kj::StringPtr, kj::Own<TestNetworkAdapter>> map;
};

typedef VatNetwork<
    test::TestSturdyRefHostId, test::TestProvisionId, test::TestRecipientId,
    test::TestThirdPartyCapId, test::TestJoinResult> TestNetworkAdapterBase;

class TestNetworkAdapter final: public TestNetworkAdapterBase {
public:
  TestNetworkAdapter(TestNetwork& network, kj::StringPtr self): network(network), self(self) {}

  ~TestNetworkAdapter() {
    kj::Exception exception = KJ_EXCEPTION(FAILED, "Network was destroyed.");
    for (auto& entry: connections) {
      entry.second->disconnect(kj::cp(exception));
    }
  }

  uint getSentCount() { return sent; }
  uint getReceivedCount() { return received; }

  typedef TestNetworkAdapterBase::Connection Connection;

  class ConnectionImpl final
      : public Connection, public kj::Refcounted, public kj::TaskSet::ErrorHandler {
  public:
    ConnectionImpl(TestNetworkAdapter& network, RpcDumper::Sender sender)
        : network(network), sender(sender), tasks(kj::heap<kj::TaskSet>(*this)) {}

    void attach(ConnectionImpl& other) {
      KJ_REQUIRE(partner == nullptr);
      KJ_REQUIRE(other.partner == nullptr);
      partner = other;
      other.partner = *this;
    }

    void disconnect(kj::Exception&& exception) {
      while (!fulfillers.empty()) {
        fulfillers.front()->reject(kj::cp(exception));
        fulfillers.pop();
      }

      networkException = kj::mv(exception);

      tasks = nullptr;
    }

    class IncomingRpcMessageImpl final: public IncomingRpcMessage, public kj::Refcounted {
    public:
      IncomingRpcMessageImpl(kj::Array<word> data)
          : data(kj::mv(data)),
            message(this->data) {}

      AnyPointer::Reader getBody() override {
        return message.getRoot<AnyPointer>();
      }

      size_t sizeInWords() override {
        return data.size();
      }

      kj::Array<word> data;
      FlatArrayMessageReader message;
    };

    class OutgoingRpcMessageImpl final: public OutgoingRpcMessage {
    public:
      OutgoingRpcMessageImpl(ConnectionImpl& connection, uint firstSegmentWordSize)
          : connection(connection),
            message(firstSegmentWordSize == 0 ? SUGGESTED_FIRST_SEGMENT_WORDS
                                              : firstSegmentWordSize) {}

      AnyPointer::Builder getBody() override {
        return message.getRoot<AnyPointer>();
      }

      void send() override {
        if (connection.networkException != nullptr) {
          return;
        }

        ++connection.network.sent;

        // Uncomment to get a debug dump.
//        kj::String msg = connection.network.network.dumper.dump(
//            message.getRoot<rpc::Message>(), connection.sender);
//        KJ_ DBG(msg);

        auto incomingMessage = kj::heap<IncomingRpcMessageImpl>(messageToFlatArray(message));

        auto connectionPtr = &connection;
        connection.tasks->add(kj::evalLater(kj::mvCapture(incomingMessage,
            [connectionPtr](kj::Own<IncomingRpcMessageImpl>&& message) {
          KJ_IF_MAYBE(p, connectionPtr->partner) {
            if (p->fulfillers.empty()) {
              p->messages.push(kj::mv(message));
            } else {
              ++p->network.received;
              p->fulfillers.front()->fulfill(
                  kj::Own<IncomingRpcMessage>(kj::mv(message)));
              p->fulfillers.pop();
            }
          }
        })));
      }

      size_t sizeInWords() override {
        return message.sizeInWords();
      }

    private:
      ConnectionImpl& connection;
      MallocMessageBuilder message;
    };

    test::TestSturdyRefHostId::Reader getPeerVatId() override {
      // Not actually implemented for the purpose of this test.
      return test::TestSturdyRefHostId::Reader();
    }

    kj::Own<OutgoingRpcMessage> newOutgoingMessage(uint firstSegmentWordSize) override {
      return kj::heap<OutgoingRpcMessageImpl>(*this, firstSegmentWordSize);
    }
    kj::Promise<kj::Maybe<kj::Own<IncomingRpcMessage>>> receiveIncomingMessage() override {
      KJ_IF_MAYBE(e, networkException) {
        return kj::cp(*e);
      }

      if (messages.empty()) {
        KJ_IF_MAYBE(f, fulfillOnEnd) {
          f->get()->fulfill();
          return kj::Maybe<kj::Own<IncomingRpcMessage>>(nullptr);
        } else {
          auto paf = kj::newPromiseAndFulfiller<kj::Maybe<kj::Own<IncomingRpcMessage>>>();
          fulfillers.push(kj::mv(paf.fulfiller));
          return kj::mv(paf.promise);
        }
      } else {
        ++network.received;
        auto result = kj::mv(messages.front());
        messages.pop();
        return kj::Maybe<kj::Own<IncomingRpcMessage>>(kj::mv(result));
      }
    }
    kj::Promise<void> shutdown() override {
      KJ_IF_MAYBE(p, partner) {
        auto paf = kj::newPromiseAndFulfiller<void>();
        p->fulfillOnEnd = kj::mv(paf.fulfiller);
        return kj::mv(paf.promise);
      } else {
        return kj::READY_NOW;
      }
    }

    void taskFailed(kj::Exception&& exception) override {
      ADD_FAILURE() << kj::str(exception).cStr();
    }

  private:
    TestNetworkAdapter& network;
    RpcDumper::Sender sender KJ_UNUSED_MEMBER;
    kj::Maybe<ConnectionImpl&> partner;

    kj::Maybe<kj::Exception> networkException;

    std::queue<kj::Own<kj::PromiseFulfiller<kj::Maybe<kj::Own<IncomingRpcMessage>>>>> fulfillers;
    std::queue<kj::Own<IncomingRpcMessage>> messages;
    kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> fulfillOnEnd;

    kj::Own<kj::TaskSet> tasks;
  };

  kj::Maybe<kj::Own<Connection>> connect(test::TestSturdyRefHostId::Reader hostId) override {
    if (hostId.getHost() == self) {
      return nullptr;
    }

    TestNetworkAdapter& dst = KJ_REQUIRE_NONNULL(network.find(hostId.getHost()));

    auto iter = connections.find(&dst);
    if (iter == connections.end()) {
      auto local = kj::refcounted<ConnectionImpl>(*this, RpcDumper::CLIENT);
      auto remote = kj::refcounted<ConnectionImpl>(dst, RpcDumper::SERVER);
      local->attach(*remote);

      connections[&dst] = kj::addRef(*local);
      dst.connections[this] = kj::addRef(*remote);

      if (dst.fulfillerQueue.empty()) {
        dst.connectionQueue.push(kj::mv(remote));
      } else {
        dst.fulfillerQueue.front()->fulfill(kj::mv(remote));
        dst.fulfillerQueue.pop();
      }

      return kj::Own<Connection>(kj::mv(local));
    } else {
      return kj::Own<Connection>(kj::addRef(*iter->second));
    }
  }

  kj::Promise<kj::Own<Connection>> accept() override {
    if (connectionQueue.empty()) {
      auto paf = kj::newPromiseAndFulfiller<kj::Own<Connection>>();
      fulfillerQueue.push(kj::mv(paf.fulfiller));
      return kj::mv(paf.promise);
    } else {
      auto result = kj::mv(connectionQueue.front());
      connectionQueue.pop();
      return kj::mv(result);
    }
  }

private:
  TestNetwork& network;
  kj::StringPtr self;
  uint sent = 0;
  uint received = 0;

  std::map<const TestNetworkAdapter*, kj::Own<ConnectionImpl>> connections;
  std::queue<kj::Own<kj::PromiseFulfiller<kj::Own<Connection>>>> fulfillerQueue;
  std::queue<kj::Own<Connection>> connectionQueue;
};

TestNetwork::~TestNetwork() noexcept(false) {}

TestNetworkAdapter& TestNetwork::add(kj::StringPtr name) {
  return *(map[name] = kj::heap<TestNetworkAdapter>(*this, name));
}

// =======================================================================================

class TestRestorer final: public SturdyRefRestorer<test::TestSturdyRefObjectId> {
public:
  int callCount = 0;
  int handleCount = 0;

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
};

struct TestContext {
  kj::EventLoop loop;
  kj::WaitScope waitScope;
  TestNetwork network;
  TestRestorer restorer;
  TestNetworkAdapter& clientNetwork;
  TestNetworkAdapter& serverNetwork;
  RpcSystem<test::TestSturdyRefHostId> rpcClient;
  RpcSystem<test::TestSturdyRefHostId> rpcServer;

  TestContext()
      : waitScope(loop),
        clientNetwork(network.add("client")),
        serverNetwork(network.add("server")),
        rpcClient(makeRpcClient(clientNetwork)),
        rpcServer(makeRpcServer(serverNetwork, restorer)) {}
  TestContext(Capability::Client bootstrap)
      : waitScope(loop),
        clientNetwork(network.add("client")),
        serverNetwork(network.add("server")),
        rpcClient(makeRpcClient(clientNetwork)),
        rpcServer(makeRpcServer(serverNetwork, bootstrap)) {}
  TestContext(Capability::Client bootstrap,
              RealmGateway<test::TestSturdyRef, Text>::Client gateway)
      : waitScope(loop),
        clientNetwork(network.add("client")),
        serverNetwork(network.add("server")),
        rpcClient(makeRpcClient(clientNetwork, gateway)),
        rpcServer(makeRpcServer(serverNetwork, bootstrap)) {}
  TestContext(Capability::Client bootstrap,
              RealmGateway<test::TestSturdyRef, Text>::Client gateway,
              bool)
      : waitScope(loop),
        clientNetwork(network.add("client")),
        serverNetwork(network.add("server")),
        rpcClient(makeRpcClient(clientNetwork)),
        rpcServer(makeRpcServer(serverNetwork, bootstrap, gateway)) {}

  Capability::Client connect(test::TestSturdyRefObjectId::Tag tag) {
    MallocMessageBuilder refMessage(128);
    auto ref = refMessage.initRoot<test::TestSturdyRef>();
    auto hostId = ref.initHostId();
    hostId.setHost("server");
    ref.getObjectId().initAs<test::TestSturdyRefObjectId>().setTag(tag);

    return rpcClient.restore(hostId, ref.getObjectId());
  }
};

TEST(Rpc, Basic) {
  TestContext context;

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
  auto promise3 = request3.send().then(
      [](Response<test::TestInterface::BarResults>&& response) {
        ADD_FAILURE() << "Expected bar() call to fail.";
      }, [&](kj::Exception&& e) {
        barFailed = true;
      });

  auto request2 = client.bazRequest();
  initTestMessage(request2.initS());
  auto promise2 = request2.send();

  EXPECT_EQ(0, context.restorer.callCount);

  auto response1 = promise1.wait(context.waitScope);

  EXPECT_EQ("foo", response1.getX());

  auto response2 = promise2.wait(context.waitScope);

  promise3.wait(context.waitScope);

  EXPECT_EQ(2, context.restorer.callCount);
  EXPECT_TRUE(barFailed);
}

TEST(Rpc, Pipelining) {
  TestContext context;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_PIPELINE)
      .castAs<test::TestPipeline>();

  int chainedCallCount = 0;

  auto request = client.getCapRequest();
  request.setN(234);
  request.setInCap(kj::heap<TestInterfaceImpl>(chainedCallCount));

  auto promise = request.send();

  auto pipelineRequest = promise.getOutBox().getCap().fooRequest();
  pipelineRequest.setI(321);
  auto pipelinePromise = pipelineRequest.send();

  auto pipelineRequest2 = promise.getOutBox().getCap().castAs<test::TestExtends>().graultRequest();
  auto pipelinePromise2 = pipelineRequest2.send();

  promise = nullptr;  // Just to be annoying, drop the original promise.

  EXPECT_EQ(0, context.restorer.callCount);
  EXPECT_EQ(0, chainedCallCount);

  auto response = pipelinePromise.wait(context.waitScope);
  EXPECT_EQ("bar", response.getX());

  auto response2 = pipelinePromise2.wait(context.waitScope);
  checkTestMessage(response2);

  EXPECT_EQ(3, context.restorer.callCount);
  EXPECT_EQ(1, chainedCallCount);
}

TEST(Rpc, Release) {
  TestContext context;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  auto handle1 = client.getHandleRequest().send().wait(context.waitScope).getHandle();
  auto promise = client.getHandleRequest().send();
  auto handle2 = promise.wait(context.waitScope).getHandle();

  EXPECT_EQ(2, context.restorer.handleCount);

  handle1 = nullptr;

  for (uint i = 0; i < 16; i++) kj::evalLater([]() {}).wait(context.waitScope);
  EXPECT_EQ(1, context.restorer.handleCount);

  handle2 = nullptr;

  for (uint i = 0; i < 16; i++) kj::evalLater([]() {}).wait(context.waitScope);
  EXPECT_EQ(1, context.restorer.handleCount);

  promise = nullptr;

  for (uint i = 0; i < 16; i++) kj::evalLater([]() {}).wait(context.waitScope);
  EXPECT_EQ(0, context.restorer.handleCount);
}

TEST(Rpc, ReleaseOnCancel) {
  // At one time, there was a bug where if a Return contained capabilities, but the client had
  // canceled the request and already send a Finish (which presumably didn't reach the server before
  // the Return), then we'd leak those caps. Test for that.

  TestContext context;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();
  client.whenResolved().wait(context.waitScope);

  {
    auto promise = client.getHandleRequest().send();

    // If the server receives cancellation too early, it won't even return a capability in the
    // results, it will just return "canceled". We want to emulate the case where the return message
    // and the cancel (finish) message cross paths. It turns out that exactly two evalLater()s get
    // us there.
    //
    // TODO(cleanup): This is fragile, but I'm not sure how else to write it without a ton
    //   of scaffolding.
    kj::evalLater([]() {}).wait(context.waitScope);
    kj::evalLater([]() {}).wait(context.waitScope);
  }

  for (uint i = 0; i < 16; i++) kj::evalLater([]() {}).wait(context.waitScope);
  EXPECT_EQ(0, context.restorer.handleCount);
}

TEST(Rpc, TailCall) {
  TestContext context;

  auto caller = context.connect(test::TestSturdyRefObjectId::Tag::TEST_TAIL_CALLER)
      .castAs<test::TestTailCaller>();

  int calleeCallCount = 0;

  test::TestTailCallee::Client callee(kj::heap<TestTailCalleeImpl>(calleeCallCount));

  auto request = caller.fooRequest();
  request.setI(456);
  request.setCallee(callee);

  auto promise = request.send();

  auto dependentCall0 = promise.getC().getCallSequenceRequest().send();

  auto response = promise.wait(context.waitScope);
  EXPECT_EQ(456, response.getI());
  EXPECT_EQ("from TestTailCaller", response.getT());

  auto dependentCall1 = promise.getC().getCallSequenceRequest().send();

  auto dependentCall2 = response.getC().getCallSequenceRequest().send();

  EXPECT_EQ(0, dependentCall0.wait(context.waitScope).getN());
  EXPECT_EQ(1, dependentCall1.wait(context.waitScope).getN());
  EXPECT_EQ(2, dependentCall2.wait(context.waitScope).getN());

  EXPECT_EQ(1, calleeCallCount);
  EXPECT_EQ(1, context.restorer.callCount);
}

TEST(Rpc, Cancelation) {
  // Tests allowCancellation().

  TestContext context;

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = paf.promise.then([&]() { destroyed = true; }).eagerlyEvaluate(nullptr);

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  kj::Promise<void> promise = nullptr;

  bool returned = false;
  {
    auto request = client.expectCancelRequest();
    request.setCap(kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller)));
    promise = request.send().then(
        [&](Response<test::TestMoreStuff::ExpectCancelResults>&& response) {
      returned = true;
    }).eagerlyEvaluate(nullptr);
  }
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);

  // We can detect that the method was canceled because it will drop the cap.
  EXPECT_FALSE(destroyed);
  EXPECT_FALSE(returned);

  promise = nullptr;  // request cancellation
  destructionPromise.wait(context.waitScope);

  EXPECT_TRUE(destroyed);
  EXPECT_FALSE(returned);
}

TEST(Rpc, PromiseResolve) {
  TestContext context;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  int chainedCallCount = 0;

  auto request = client.callFooRequest();
  auto request2 = client.callFooWhenResolvedRequest();

  auto paf = kj::newPromiseAndFulfiller<test::TestInterface::Client>();

  {
    auto fork = paf.promise.fork();
    request.setCap(fork.addBranch());
    request2.setCap(fork.addBranch());
  }

  auto promise = request.send();
  auto promise2 = request2.send();

  // Make sure getCap() has been called on the server side by sending another call and waiting
  // for it.
  EXPECT_EQ(2, client.getCallSequenceRequest().send().wait(context.waitScope).getN());
  EXPECT_EQ(3, context.restorer.callCount);

  // OK, now fulfill the local promise.
  paf.fulfiller->fulfill(kj::heap<TestInterfaceImpl>(chainedCallCount));

  // We should now be able to wait for getCap() to finish.
  EXPECT_EQ("bar", promise.wait(context.waitScope).getS());
  EXPECT_EQ("bar", promise2.wait(context.waitScope).getS());

  EXPECT_EQ(3, context.restorer.callCount);
  EXPECT_EQ(2, chainedCallCount);
}

TEST(Rpc, RetainAndRelease) {
  TestContext context;

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = paf.promise.then([&]() { destroyed = true; }).eagerlyEvaluate(nullptr);

  {
    auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
        .castAs<test::TestMoreStuff>();

    {
      auto request = client.holdRequest();
      request.setCap(kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller)));
      request.send().wait(context.waitScope);
    }

    // Do some other call to add a round trip.
    EXPECT_EQ(1, client.getCallSequenceRequest().send().wait(context.waitScope).getN());

    // Shouldn't be destroyed because it's being held by the server.
    EXPECT_FALSE(destroyed);

    // We can ask it to call the held capability.
    EXPECT_EQ("bar", client.callHeldRequest().send().wait(context.waitScope).getS());

    {
      // We can get the cap back from it.
      auto capCopy = client.getHeldRequest().send().wait(context.waitScope).getCap();

      {
        // And call it, without any network communications.
        uint oldSentCount = context.clientNetwork.getSentCount();
        auto request = capCopy.fooRequest();
        request.setI(123);
        request.setJ(true);
        EXPECT_EQ("foo", request.send().wait(context.waitScope).getX());
        EXPECT_EQ(oldSentCount, context.clientNetwork.getSentCount());
      }

      {
        // We can send another copy of the same cap to another method, and it works.
        auto request = client.callFooRequest();
        request.setCap(capCopy);
        EXPECT_EQ("bar", request.send().wait(context.waitScope).getS());
      }
    }

    // Give some time to settle.
    EXPECT_EQ(5, client.getCallSequenceRequest().send().wait(context.waitScope).getN());
    EXPECT_EQ(6, client.getCallSequenceRequest().send().wait(context.waitScope).getN());
    EXPECT_EQ(7, client.getCallSequenceRequest().send().wait(context.waitScope).getN());

    // Can't be destroyed, we haven't released it.
    EXPECT_FALSE(destroyed);
  }

  // We released our client, which should cause the server to be released, which in turn will
  // release the cap pointing back to us.
  destructionPromise.wait(context.waitScope);
  EXPECT_TRUE(destroyed);
}

TEST(Rpc, Cancel) {
  TestContext context;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = paf.promise.then([&]() { destroyed = true; }).eagerlyEvaluate(nullptr);

  {
    auto request = client.neverReturnRequest();
    request.setCap(kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller)));

    {
      auto responsePromise = request.send();

      // Allow some time to settle.
      EXPECT_EQ(1, client.getCallSequenceRequest().send().wait(context.waitScope).getN());
      EXPECT_EQ(2, client.getCallSequenceRequest().send().wait(context.waitScope).getN());

      // The cap shouldn't have been destroyed yet because the call never returned.
      EXPECT_FALSE(destroyed);
    }
  }

  // Now the cap should be released.
  destructionPromise.wait(context.waitScope);
  EXPECT_TRUE(destroyed);
}

TEST(Rpc, SendTwice) {
  TestContext context;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = paf.promise.then([&]() { destroyed = true; }).eagerlyEvaluate(nullptr);

  auto cap = test::TestInterface::Client(kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller)));

  {
    auto request = client.callFooRequest();
    request.setCap(cap);

    EXPECT_EQ("bar", request.send().wait(context.waitScope).getS());
  }

  // Allow some time for the server to release `cap`.
  EXPECT_EQ(1, client.getCallSequenceRequest().send().wait(context.waitScope).getN());

  {
    // More requests with the same cap.
    auto request = client.callFooRequest();
    auto request2 = client.callFooRequest();
    request.setCap(cap);
    request2.setCap(kj::mv(cap));

    auto promise = request.send();
    auto promise2 = request2.send();

    EXPECT_EQ("bar", promise.wait(context.waitScope).getS());
    EXPECT_EQ("bar", promise2.wait(context.waitScope).getS());
  }

  // Now the cap should be released.
  destructionPromise.wait(context.waitScope);
  EXPECT_TRUE(destroyed);
}

RemotePromise<test::TestCallOrder::GetCallSequenceResults> getCallSequence(
    test::TestCallOrder::Client& client, uint expected) {
  auto req = client.getCallSequenceRequest();
  req.setExpected(expected);
  return req.send();
}

TEST(Rpc, Embargo) {
  TestContext context;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  auto cap = test::TestCallOrder::Client(kj::heap<TestCallOrderImpl>());

  auto earlyCall = client.getCallSequenceRequest().send();

  auto echoRequest = client.echoRequest();
  echoRequest.setCap(cap);
  auto echo = echoRequest.send();

  auto pipeline = echo.getCap();

  auto call0 = getCallSequence(pipeline, 0);
  auto call1 = getCallSequence(pipeline, 1);

  earlyCall.wait(context.waitScope);

  auto call2 = getCallSequence(pipeline, 2);

  auto resolved = echo.wait(context.waitScope).getCap();

  auto call3 = getCallSequence(pipeline, 3);
  auto call4 = getCallSequence(pipeline, 4);
  auto call5 = getCallSequence(pipeline, 5);

  EXPECT_EQ(0, call0.wait(context.waitScope).getN());
  EXPECT_EQ(1, call1.wait(context.waitScope).getN());
  EXPECT_EQ(2, call2.wait(context.waitScope).getN());
  EXPECT_EQ(3, call3.wait(context.waitScope).getN());
  EXPECT_EQ(4, call4.wait(context.waitScope).getN());
  EXPECT_EQ(5, call5.wait(context.waitScope).getN());
}

template <typename T>
void expectPromiseThrows(kj::Promise<T>&& promise, kj::WaitScope& waitScope) {
  EXPECT_TRUE(promise.then([](T&&) { return false; }, [](kj::Exception&&) { return true; })
      .wait(waitScope));
}

template <>
void expectPromiseThrows(kj::Promise<void>&& promise, kj::WaitScope& waitScope) {
  EXPECT_TRUE(promise.then([]() { return false; }, [](kj::Exception&&) { return true; })
      .wait(waitScope));
}

TEST(Rpc, EmbargoError) {
  TestContext context;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  auto paf = kj::newPromiseAndFulfiller<test::TestCallOrder::Client>();

  auto cap = test::TestCallOrder::Client(kj::mv(paf.promise));

  auto earlyCall = client.getCallSequenceRequest().send();

  auto echoRequest = client.echoRequest();
  echoRequest.setCap(cap);
  auto echo = echoRequest.send();

  auto pipeline = echo.getCap();

  auto call0 = getCallSequence(pipeline, 0);
  auto call1 = getCallSequence(pipeline, 1);

  earlyCall.wait(context.waitScope);

  auto call2 = getCallSequence(pipeline, 2);

  auto resolved = echo.wait(context.waitScope).getCap();

  auto call3 = getCallSequence(pipeline, 3);
  auto call4 = getCallSequence(pipeline, 4);
  auto call5 = getCallSequence(pipeline, 5);

  paf.fulfiller->rejectIfThrows([]() { KJ_FAIL_ASSERT("foo") { break; } });

  expectPromiseThrows(kj::mv(call0), context.waitScope);
  expectPromiseThrows(kj::mv(call1), context.waitScope);
  expectPromiseThrows(kj::mv(call2), context.waitScope);
  expectPromiseThrows(kj::mv(call3), context.waitScope);
  expectPromiseThrows(kj::mv(call4), context.waitScope);
  expectPromiseThrows(kj::mv(call5), context.waitScope);

  // Verify that we're still connected (there were no protocol errors).
  getCallSequence(client, 1).wait(context.waitScope);
}

TEST(Rpc, EmbargoNull) {
  // Set up a situation where we pipeline on a capability that ends up coming back null. This
  // should NOT cause a Disembargo to be sent, but due to a bug in earlier versions of Cap'n Proto,
  // a Disembargo was indeed sent to the null capability, which caused the server to disconnect
  // due to protocol error.

  TestContext context;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();

  auto promise = client.getNullRequest().send();

  auto cap = promise.getNullCap();

  auto call0 = cap.getCallSequenceRequest().send();

  promise.wait(context.waitScope);

  auto call1 = cap.getCallSequenceRequest().send();

  expectPromiseThrows(kj::mv(call0), context.waitScope);
  expectPromiseThrows(kj::mv(call1), context.waitScope);

  // Verify that we're still connected (there were no protocol errors).
  getCallSequence(client, 0).wait(context.waitScope);
}

TEST(Rpc, CallBrokenPromise) {
  // Tell the server to call back to a promise client, then resolve the promise to an error.

  TestContext context;

  auto client = context.connect(test::TestSturdyRefObjectId::Tag::TEST_MORE_STUFF)
      .castAs<test::TestMoreStuff>();
  auto paf = kj::newPromiseAndFulfiller<test::TestInterface::Client>();

  {
    auto req = client.holdRequest();
    req.setCap(kj::mv(paf.promise));
    req.send().wait(context.waitScope);
  }

  bool returned = false;
  auto req = client.callHeldRequest().send()
      .then([&](capnp::Response<test::TestMoreStuff::CallHeldResults>&&) {
    returned = true;
  }, [&](kj::Exception&& e) {
    returned = true;
    kj::throwRecoverableException(kj::mv(e));
  }).eagerlyEvaluate(nullptr);

  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);

  EXPECT_FALSE(returned);

  paf.fulfiller->rejectIfThrows([]() { KJ_FAIL_ASSERT("foo") { break; } });

  expectPromiseThrows(kj::mv(req), context.waitScope);
  EXPECT_TRUE(returned);

  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);
  kj::evalLater([]() {}).wait(context.waitScope);

  // Verify that we're still connected (there were no protocol errors).
  getCallSequence(client, 1).wait(context.waitScope);
}

TEST(Rpc, Abort) {
  // Verify that aborts are received.

  TestContext context;

  MallocMessageBuilder refMessage(128);
  auto hostId = refMessage.initRoot<test::TestSturdyRefHostId>();
  hostId.setHost("server");

  auto conn = KJ_ASSERT_NONNULL(context.clientNetwork.connect(hostId));

  {
    // Send an invalid message (Return to non-existent question).
    auto msg = conn->newOutgoingMessage(128);
    auto body = msg->getBody().initAs<rpc::Message>().initReturn();
    body.setAnswerId(1234);
    body.setCanceled();
    msg->send();
  }

  auto reply = KJ_ASSERT_NONNULL(conn->receiveIncomingMessage().wait(context.waitScope));
  EXPECT_EQ(rpc::Message::ABORT, reply->getBody().getAs<rpc::Message>().which());

  EXPECT_TRUE(conn->receiveIncomingMessage().wait(context.waitScope) == nullptr);
}

// =======================================================================================

typedef RealmGateway<test::TestSturdyRef, Text> TestRealmGateway;

class TestGateway final: public TestRealmGateway::Server {
public:
  kj::Promise<void> import(ImportContext context) override {
    auto cap = context.getParams().getCap();
    context.releaseParams();
    return cap.saveRequest().send()
        .then([KJ_CPCAP(context)](Response<Persistent<Text>::SaveResults> response) mutable {
      context.getResults().initSturdyRef().getObjectId().setAs<Text>(
          kj::str("imported-", response.getSturdyRef()));
    });
  }

  kj::Promise<void> export_(ExportContext context) override {
    auto cap = context.getParams().getCap();
    context.releaseParams();
    return cap.saveRequest().send()
        .then([KJ_CPCAP(context)]
            (Response<Persistent<test::TestSturdyRef>::SaveResults> response) mutable {
      context.getResults().setSturdyRef(kj::str("exported-",
          response.getSturdyRef().getObjectId().getAs<Text>()));
    });
  }
};

class TestPersistent final: public Persistent<test::TestSturdyRef>::Server {
public:
  TestPersistent(kj::StringPtr name): name(name) {}

  kj::Promise<void> save(SaveContext context) override {
    context.initResults().initSturdyRef().getObjectId().setAs<Text>(name);
    return kj::READY_NOW;
  }

private:
  kj::StringPtr name;
};

class TestPersistentText final: public Persistent<Text>::Server {
public:
  TestPersistentText(kj::StringPtr name): name(name) {}

  kj::Promise<void> save(SaveContext context) override {
    context.initResults().setSturdyRef(name);
    return kj::READY_NOW;
  }

private:
  kj::StringPtr name;
};

TEST(Rpc, RealmGatewayImport) {
  TestRealmGateway::Client gateway = kj::heap<TestGateway>();
  Persistent<Text>::Client bootstrap = kj::heap<TestPersistentText>("foo");

  MallocMessageBuilder hostIdBuilder;
  auto hostId = hostIdBuilder.getRoot<test::TestSturdyRefHostId>();
  hostId.setHost("server");

  TestContext context(bootstrap, gateway);
  auto client = context.rpcClient.bootstrap(hostId).castAs<Persistent<test::TestSturdyRef>>();

  auto response = client.saveRequest().send().wait(context.waitScope);

  EXPECT_EQ("imported-foo", response.getSturdyRef().getObjectId().getAs<Text>());
}

TEST(Rpc, RealmGatewayExport) {
  TestRealmGateway::Client gateway = kj::heap<TestGateway>();
  Persistent<test::TestSturdyRef>::Client bootstrap = kj::heap<TestPersistent>("foo");

  MallocMessageBuilder hostIdBuilder;
  auto hostId = hostIdBuilder.getRoot<test::TestSturdyRefHostId>();
  hostId.setHost("server");

  TestContext context(bootstrap, gateway, true);
  auto client = context.rpcClient.bootstrap(hostId).castAs<Persistent<Text>>();

  auto response = client.saveRequest().send().wait(context.waitScope);

  EXPECT_EQ("exported-foo", response.getSturdyRef());
}

TEST(Rpc, RealmGatewayImportExport) {
  // Test that a save request which leaves the realm, bounces through a promise capability, and
  // then comes back into the realm, does not actually get translated both ways.

  TestRealmGateway::Client gateway = kj::heap<TestGateway>();
  Persistent<test::TestSturdyRef>::Client bootstrap = kj::heap<TestPersistent>("foo");

  MallocMessageBuilder serverHostIdBuilder;
  auto serverHostId = serverHostIdBuilder.getRoot<test::TestSturdyRefHostId>();
  serverHostId.setHost("server");

  MallocMessageBuilder clientHostIdBuilder;
  auto clientHostId = clientHostIdBuilder.getRoot<test::TestSturdyRefHostId>();
  clientHostId.setHost("client");

  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  TestNetwork network;
  TestNetworkAdapter& clientNetwork = network.add("client");
  TestNetworkAdapter& serverNetwork = network.add("server");
  RpcSystem<test::TestSturdyRefHostId> rpcClient =
      makeRpcServer(clientNetwork, bootstrap, gateway);
  auto paf = kj::newPromiseAndFulfiller<Capability::Client>();
  RpcSystem<test::TestSturdyRefHostId> rpcServer =
      makeRpcServer(serverNetwork, kj::mv(paf.promise));

  auto client = rpcClient.bootstrap(serverHostId).castAs<Persistent<test::TestSturdyRef>>();

  bool responseReady = false;
  auto responsePromise = client.saveRequest().send()
      .then([&](Response<Persistent<test::TestSturdyRef>::SaveResults>&& response) {
    responseReady = true;
    return kj::mv(response);
  }).eagerlyEvaluate(nullptr);

  // Crank the event loop to give the message time to reach the server and block on the promise
  // resolution.
  kj::evalLater([]() {}).wait(waitScope);
  kj::evalLater([]() {}).wait(waitScope);
  kj::evalLater([]() {}).wait(waitScope);
  kj::evalLater([]() {}).wait(waitScope);

  EXPECT_FALSE(responseReady);

  paf.fulfiller->fulfill(rpcServer.bootstrap(clientHostId));

  auto response = responsePromise.wait(waitScope);

  // Should have the original value. If it went through export and re-import, though, then this
  // will be "imported-exported-foo", which is wrong.
  EXPECT_EQ("foo", response.getSturdyRef().getObjectId().getAs<Text>());
}

TEST(Rpc, RealmGatewayImportExport) {
  // Test that a save request which enters the realm, bounces through a promise capability, and
  // then goes back out of the realm, does not actually get translated both ways.

  TestRealmGateway::Client gateway = kj::heap<TestGateway>();
  Persistent<Text>::Client bootstrap = kj::heap<TestPersistentText>("foo");

  MallocMessageBuilder serverHostIdBuilder;
  auto serverHostId = serverHostIdBuilder.getRoot<test::TestSturdyRefHostId>();
  serverHostId.setHost("server");

  MallocMessageBuilder clientHostIdBuilder;
  auto clientHostId = clientHostIdBuilder.getRoot<test::TestSturdyRefHostId>();
  clientHostId.setHost("client");

  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);
  TestNetwork network;
  TestNetworkAdapter& clientNetwork = network.add("client");
  TestNetworkAdapter& serverNetwork = network.add("server");
  RpcSystem<test::TestSturdyRefHostId> rpcClient =
      makeRpcServer(clientNetwork, bootstrap);
  auto paf = kj::newPromiseAndFulfiller<Capability::Client>();
  RpcSystem<test::TestSturdyRefHostId> rpcServer =
      makeRpcServer(serverNetwork, kj::mv(paf.promise), gateway);

  auto client = rpcClient.bootstrap(serverHostId).castAs<Persistent<Text>>();

  bool responseReady = false;
  auto responsePromise = client.saveRequest().send()
      .then([&](Response<Persistent<Text>::SaveResults>&& response) {
    responseReady = true;
    return kj::mv(response);
  }).eagerlyEvaluate(nullptr);

  // Crank the event loop to give the message time to reach the server and block on the promise
  // resolution.
  kj::evalLater([]() {}).wait(waitScope);
  kj::evalLater([]() {}).wait(waitScope);
  kj::evalLater([]() {}).wait(waitScope);
  kj::evalLater([]() {}).wait(waitScope);

  EXPECT_FALSE(responseReady);

  paf.fulfiller->fulfill(rpcServer.bootstrap(clientHostId));

  auto response = responsePromise.wait(waitScope);

  // Should have the original value. If it went through import and re-export, though, then this
  // will be "exported-imported-foo", which is wrong.
  EXPECT_EQ("foo", response.getSturdyRef());
}

KJ_TEST("loopback bootstrap()") {
  int callCount = 0;
  test::TestInterface::Client bootstrap = kj::heap<TestInterfaceImpl>(callCount);

  MallocMessageBuilder hostIdBuilder;
  auto hostId = hostIdBuilder.getRoot<test::TestSturdyRefHostId>();
  hostId.setHost("server");

  TestContext context(bootstrap);
  auto client = context.rpcServer.bootstrap(hostId).castAs<test::TestInterface>();

  auto request = client.fooRequest();
  request.setI(123);
  request.setJ(true);
  auto response = request.send().wait(context.waitScope);

  KJ_EXPECT(response.getX() == "foo");
  KJ_EXPECT(callCount == 1);
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
