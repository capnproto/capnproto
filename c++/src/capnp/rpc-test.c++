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
#include <kj/function.h>
#include <kj/string-tree.h>
#include <capnp/rpc.capnp.h>
#include <kj/async-queue.h>
#include <kj/map.h>
#include <kj/miniposix.h>

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
    schemas.insert(schema.getProto().getId(), schema);
  }

  class Sender {
  public:
    explicit Sender(RpcDumper& parent, kj::StringPtr name, bool unique)
        : parent(parent), name(name), unique(unique) {}

    ~Sender() noexcept(false) {
      KJ_IF_SOME(p, partner) {
        p.partner = kj::none;
      }
    }

    void setPartner(Sender& p) {
      partner = p;
      p.partner = *this;
      partnerName = p.name;
      p.partnerName = name;
    }

    void dump(rpc::Message::Reader message) {
      kj::FdOutputStream(STDOUT_FILENO).write(dumpStr(message).asBytes());
    }

    kj::String dumpStr(rpc::Message::Reader message) {
      switch (message.which()) {
        case rpc::Message::CALL: {
          auto call = message.getCall();
          InterfaceSchema schema;
          KJ_IF_SOME(s, parent.schemas.find(call.getInterfaceId())) {
            schema = s;
          } else {
            break;
          }
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
            KJ_IF_SOME(p, partner) {
              p.returnTypes.insert(call.getQuestionId(), resultType);
            }
          }

          auto payload = call.getParams();

          auto capTable = payload.getCapTable();
          ReaderCapabilityTable capTableReader(
              KJ_MAP(i, kj::range(0, capTable.size())) -> kj::Maybe<kj::Own<ClientHook>> {
            return newBrokenCap("fake");
          });

          auto params = capTableReader.imbue(payload.getContent()).getAs<DynamicStruct>(paramType);

          auto sendResultsTo = call.getSendResultsTo();

          return kj::str(name, unique ? "=>" : "->", partnerName, ": call ", call.getQuestionId(),
                         ": ", call.getTarget(), " <- ", interfaceName, ".",
                         methodProto.getName(), params,
                         " caps:[", kj::strArray(capTable, ", "), "]",
                         sendResultsTo.isCaller() ? kj::str()
                                                  : kj::str(" sendResultsTo:", sendResultsTo),
                         '\n');
        }

        case rpc::Message::RETURN: {
          auto ret = message.getReturn();

          Schema schema;
          KJ_IF_SOME(entry, returnTypes.findEntry(ret.getAnswerId())) {
            schema = entry.value;
            returnTypes.erase(entry);
          } else {
            break;
          }

          if (ret.which() != rpc::Return::RESULTS) {
            // Oops, no results returned.  We don't check this earlier because we want to make sure
            // returnTypes.erase() gets a chance to happen.
            break;
          }

          auto payload = ret.getResults();

          auto capTable = payload.getCapTable();
          ReaderCapabilityTable capTableReader(
              KJ_MAP(i, kj::range(0, capTable.size())) -> kj::Maybe<kj::Own<ClientHook>> {
            return newBrokenCap("fake");
          });

          auto content = capTableReader.imbue(payload.getContent());

          if (schema.getProto().isStruct()) {
            auto results = content.getAs<DynamicStruct>(schema.asStruct());

            return kj::str(name, unique ? "=>" : "->", partnerName, ": return ", ret.getAnswerId(),
                           ": ", results, " caps:[", kj::strArray(capTable, ", "), "]\n");
          } else if (schema.getProto().isInterface()) {
            content.getAs<DynamicCapability>(schema.asInterface());
            return kj::str(name, unique ? "=>" : "->", partnerName, "(", ret.getAnswerId(),
                           "): return cap ", kj::strArray(capTable, ", "), '\n');
          } else {
            break;
          }
        }

        case rpc::Message::BOOTSTRAP: {
          auto restore = message.getBootstrap();

          KJ_IF_SOME(p, partner) {
            p.returnTypes.insert(restore.getQuestionId(), InterfaceSchema());
          }

          return kj::str(name, "->", partnerName, "(", restore.getQuestionId(), "): bootstrap\n");
        }

        default:
          break;
      }

      return kj::str(name, "->", partnerName, ": ", message, '\n');
    }

  private:
    RpcDumper& parent;
    kj::StringPtr name;
    bool unique;

    kj::Maybe<Sender&> partner;
    kj::StringPtr partnerName;
    // `Sender` representing the opposite direction of the same stream.

    kj::HashMap<uint32_t, Schema> returnTypes;
    // Maps answer IDs to expected return types. Partner populates this when sending the
    // corresponding `Call`.
  };

private:
  kj::HashMap<uint64_t, InterfaceSchema> schemas;
};

// =======================================================================================

class TestVat;

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

  TestVat& add(kj::StringPtr name);

  kj::Maybe<TestVat&> find(kj::StringPtr name) {
    KJ_IF_SOME(vat, map.find(name)) {
      return *vat;
    } else {
      return kj::none;
    }
  }

  uint64_t newToken() {
    // Make a unique token for three-party handoffs.
    //
    // In the real world, we'd be using cryptographically unguessable strings, but for testing
    // purposes a counter will do just fine.

    return ++tokenCounter;
  }

  RpcDumper dumper;

  bool forwardingEnabled = false;
  bool callHandoffEnabled = false;
  uint introCount = 0;
  uint callIntroCount = 0;
  uint forwardCount = 0;
  uint callForwardCount = 0;
  uint deniedForwardCount = 0;

private:
  kj::HashMap<kj::StringPtr, kj::Own<TestVat>> map;

  uint64_t tokenCounter = 0;
};

typedef VatNetwork<
    test::TestSturdyRefHostId, test::TestThirdPartyCompletion, test::TestThirdPartyToAwait,
    test::TestThirdPartyToContact, test::TestJoinResult> TestVatBase;

class TestVat final: public TestVatBase {
public:
  TestVat(TestNetwork& network, kj::StringPtr self)
      : network(network), self(self) {}

  ~TestVat() {
    kj::Exception exception = KJ_EXCEPTION(FAILED, "Network was destroyed.");
    for (auto& entry: connections) {
      entry.value->disconnect(kj::cp(exception));
    }
  }

  uint getSentCount() { return sent; }
  uint getReceivedCount() { return received; }

  void onSend(kj::Function<bool(MessageBuilder& message)> callback) {
    // Invokes the given callback every time a message is sent. Callback can return false to cause
    // send() to do nothing.
    sendCallback = kj::mv(callback);
  }

  typedef TestVatBase::Connection Connection;

  class ConnectionImpl final
      : public Connection, public kj::Refcounted, public kj::TaskSet::ErrorHandler {
  public:
    ConnectionImpl(TestVat& vat, TestVat& peerVat, kj::StringPtr name, bool unique)
        : vat(vat), peerVat(peerVat), unique(unique), dumper(vat.network.dumper, name, unique),
          tasks(kj::heap<kj::TaskSet>(*this)) {
      if (!unique) {
        vat.connections.insert(&peerVat, this);
      }
    }

    ~ConnectionImpl() noexcept(false) {
      KJ_IF_SOME(p, partner) {
        p.partner = kj::none;
      }
      if (!unique) {
        vat.connections.erase(&peerVat);
      }
    }

    bool isIdle() { return idle; }

    void attach(ConnectionImpl& other) {
      KJ_REQUIRE(partner == kj::none);
      KJ_REQUIRE(other.partner == kj::none);
      partner = other;
      partnerName = other.vat.self;
      other.partner = *this;
      other.partnerName = vat.self;
      dumper.setPartner(other.dumper);
    }

    void initiateIdleShutdown() {
      initiatedIdleShutdown = true;
      messageQueue.push(kj::none);
      KJ_IF_SOME(f, fulfillOnEnd) {
        f->fulfill();
      }
    }

    void block() {
      // Block all messages sent on this connection until unblock() is called.
      auto paf = kj::newPromiseAndFulfiller<void>();
      currentBlock = paf.promise.fork();
      currentBlockFulfiller = kj::mv(paf.fulfiller);
    }

    void blockAfter(kj::Function<bool(rpc::Message::Reader)> predicate) {
      // Automatically call block() immediately after a message matching `predicate` is sent
      // on this connection.
      blockAfterPredicate = kj::mv(predicate);
    }

    void unblock() {
      // Allow blocked messages to proceed.
      KJ_REQUIRE_NONNULL(currentBlockFulfiller)->fulfill();
      currentBlock = kj::none;
      currentBlockFulfiller = kj::none;
    }

    void disconnect(kj::Exception&& exception) {
      messageQueue.rejectAll(kj::cp(exception));
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
        KJ_EXPECT(!connection.idle);
        if (!connection.vat.sendCallback(message)) return;

        if (connection.networkException != kj::none) {
          return;
        }

        ++connection.vat.sent;

        // Uncomment to get a debug dump.
//        connection.dumper.dump(message.getRoot<rpc::Message>());

        auto incomingMessage = kj::heap<IncomingRpcMessageImpl>(messageToFlatArray(message));

        kj::Promise<void> blocker = nullptr;
        KJ_IF_SOME(b, connection.currentBlock) {
          blocker = b.addBranch();
        } else {
          blocker = kj::yield();

          // Check if we should start blocking.
          KJ_IF_SOME(p, connection.blockAfterPredicate) {
            if (p(message.getRoot<rpc::Message>())) {
              connection.block();
              connection.blockAfterPredicate = kj::none;
            }
          }
        }

        auto connectionPtr = &connection;
        connection.tasks->add(blocker.then(
            [connectionPtr,message=kj::mv(incomingMessage)]() mutable {
          KJ_IF_SOME(p, connectionPtr->partner) {
            p.messageQueue.push(kj::Own<IncomingRpcMessage>(kj::mv(message)));
          }
        }));
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
      KJ_EXPECT(!idle);
      return kj::heap<OutgoingRpcMessageImpl>(*this, firstSegmentWordSize);
    }
    kj::Promise<kj::Maybe<kj::Own<IncomingRpcMessage>>> receiveIncomingMessage() override {
      KJ_IF_SOME(e, networkException) {
        kj::throwFatalException(kj::cp(e));
      }

      if (initiatedIdleShutdown) {
        co_return kj::none;
      }

      auto result = co_await messageQueue.pop();

      if (result == kj::none) {
        KJ_IF_SOME(f, fulfillOnEnd) {
          f->fulfill();
        }
      } else {
        ++vat.received;
      }

      co_return result;
    }
    kj::Promise<void> shutdown() override {
      KJ_IF_SOME(e, vat.shutdownExceptionToThrow) {
        return kj::cp(e);
      }
      KJ_IF_SOME(p, partner) {
        if (p.initiatedIdleShutdown) {
          // Partner already initiated shutdown so don't wait for it to call
          // receiveIncomingMessage() again (it won't).
          return kj::READY_NOW;
        }

        // Make sure partner receives EOF. We have to evalLater() because
        // OutgoingMessageImpl::send() also does that and we need to deliver in order.
        return kj::evalLater([this]() -> kj::Promise<void> {
          KJ_IF_SOME(p, partner) {
            p.messageQueue.push(kj::none);
            auto paf = kj::newPromiseAndFulfiller<void>();
            p.fulfillOnEnd = kj::mv(paf.fulfiller);
            return kj::mv(paf.promise);
          } else {
            return kj::READY_NOW;
          }
        });
      } else {
        return kj::READY_NOW;
      }
    }

    void setIdle(bool idle) override {
      KJ_REQUIRE(idle != this->idle);
      this->idle = idle;
    }

    bool canIntroduceTo(Connection& other, ThreePartyHandoffPurpose purpose) override {
      return vat.network.callHandoffEnabled ||
          purpose != ThreePartyHandoffPurpose::CALL_FORWARDING;
    }

    void introduceTo(Connection& other, ThreePartyHandoffPurpose purpose,
        test::TestThirdPartyToContact::Builder otherContactInfo,
        test::TestThirdPartyToAwait::Builder thisAwaitInfo) override {
      switch (purpose) {
        case ThreePartyHandoffPurpose::CAPABILITY_PASSING:
          ++vat.network.introCount;
          break;
        case ThreePartyHandoffPurpose::CALL_FORWARDING:
          KJ_ASSERT(vat.network.callHandoffEnabled);
          ++vat.network.callIntroCount;
          break;
      }

      uint64_t token = vat.network.newToken();
      otherContactInfo.initPath().setHost(kj::downcast<ConnectionImpl>(other).partnerName);
      otherContactInfo.setToken(token);
      otherContactInfo.setSentBy(vat.self);
      thisAwaitInfo.setToken(token);
    }

    kj::Maybe<kj::Own<Connection>> connectToIntroduced(
        test::TestThirdPartyToContact::Reader contact,
        test::TestThirdPartyCompletion::Builder completion) override {
      KJ_EXPECT(contact.getSentBy() == partnerName);
      completion.setToken(contact.getToken());
      return vat.connect(contact.getPath());
    }

    bool canForwardThirdPartyToContact(
        test::TestThirdPartyToContact::Reader contact, Connection& destination,
        ThreePartyHandoffPurpose purpose) override {
      if (!vat.network.forwardingEnabled) {
        ++vat.network.deniedForwardCount;
      }
      return vat.network.forwardingEnabled;
    }

    void forwardThirdPartyToContact(
        test::TestThirdPartyToContact::Reader contact, Connection& destination,
        ThreePartyHandoffPurpose purpose, test::TestThirdPartyToContact::Builder result) override {
      KJ_EXPECT(vat.network.forwardingEnabled);
      KJ_EXPECT(contact.getSentBy() == partnerName);
      switch (purpose) {
        case ThreePartyHandoffPurpose::CAPABILITY_PASSING:
          ++vat.network.forwardCount;
          break;
        case ThreePartyHandoffPurpose::CALL_FORWARDING:
          KJ_ASSERT(vat.network.callHandoffEnabled);
          ++vat.network.callForwardCount;
          break;
      }
      result.setPath(contact.getPath());
      result.setToken(contact.getToken());
      result.setSentBy(vat.self);
    }

    kj::Own<void> awaitThirdParty(
        test::TestThirdPartyToAwait::Reader party,
        kj::Rc<kj::Refcounted> value) override {
      uint64_t token = party.getToken();
      auto& xchg = vat.getTphExchange(token);
      xchg.fulfiller->fulfill(kj::mv(value));

      return kj::heap(kj::defer([this, token]() {
        vat.tphExchanges.erase(token);
      }));
    }

    kj::Promise<kj::Rc<kj::Refcounted>> completeThirdParty(
        test::TestThirdPartyCompletion::Reader completion) override {
      auto& xchg = vat.getTphExchange(completion.getToken());
      return xchg.promise.addBranch();
    }

    kj::Array<byte> generateEmbargoId() override {
      static uint counter = 0;
      auto result = kj::heapArray<byte>(sizeof(counter));
      result.asPtr().copyFrom(kj::arrayPtr(&counter, 1).asBytes());
      ++counter;
      return result;
    }

    void taskFailed(kj::Exception&& exception) override {
      ADD_FAILURE() << kj::str(exception).cStr();
    }

  private:
    TestVat& vat;
    TestVat& peerVat;
    bool unique;
    RpcDumper::Sender dumper;
    kj::Maybe<ConnectionImpl&> partner;
    kj::StringPtr partnerName;

    kj::Maybe<kj::ForkedPromise<void>> currentBlock;
    kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> currentBlockFulfiller;
    // When present, all outgoing messages are blocked waiting for it.

    kj::Maybe<kj::Function<bool(rpc::Message::Reader)>> blockAfterPredicate;

    kj::Maybe<kj::Exception> networkException;

    kj::ProducerConsumerQueue<kj::Maybe<kj::Own<IncomingRpcMessage>>> messageQueue;
    kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> fulfillOnEnd;

    bool idle = true;
    bool initiatedIdleShutdown = false;

    kj::Own<kj::TaskSet> tasks;
  };

  kj::Maybe<kj::Own<Connection>> connect(test::TestSturdyRefHostId::Reader hostId) override {
    if (hostId.getHost() == self) {
      return kj::none;
    }

    TestVat& dst = KJ_REQUIRE_NONNULL(network.find(hostId.getHost()));

    bool unique = hostId.getUnique();
    if (!unique) {
      KJ_IF_SOME(conn, connections.find(&dst)) {
        // Return existing connection.
        return kj::Own<Connection>(kj::addRef(*conn));
      }
    }

    // Create new connection.
    auto local = kj::refcounted<ConnectionImpl>(*this, dst, self, unique);
    auto remote = kj::refcounted<ConnectionImpl>(dst, *this, dst.self, unique);
    local->attach(*remote);

    dst.acceptQueue.push(kj::mv(remote));
    return kj::Own<Connection>(kj::mv(local));
  }

  kj::Promise<kj::Own<Connection>> accept() override {
    return acceptQueue.pop();
  }

  void setShutdownExceptionToThrow(kj::Exception&& e) {
    shutdownExceptionToThrow = kj::mv(e);
  }

  kj::Maybe<ConnectionImpl&> getConnectionTo(TestVat& other) {
    KJ_IF_SOME(conn, connections.find(&other)) {
      return *conn;
    } else {
      return kj::none;
    }
  }

private:
  TestNetwork& network;
  kj::StringPtr self;
  uint sent = 0;
  uint received = 0;
  kj::Maybe<kj::Exception> shutdownExceptionToThrow = kj::none;

  kj::HashMap<const TestVat*, ConnectionImpl*> connections;
  kj::ProducerConsumerQueue<kj::Own<Connection>> acceptQueue;

  kj::Function<bool(MessageBuilder& message)> sendCallback = [](MessageBuilder&) { return true; };

  struct ThirdPartyExchange {
    kj::ForkedPromise<kj::Rc<kj::Refcounted>> promise;
    kj::Own<kj::PromiseFulfiller<kj::Rc<kj::Refcounted>>> fulfiller;

    ThirdPartyExchange(kj::PromiseFulfillerPair<kj::Rc<kj::Refcounted>> paf =
                       kj::newPromiseAndFulfiller<kj::Rc<kj::Refcounted>>())
        : promise(paf.promise.fork()), fulfiller(kj::mv(paf.fulfiller)) {}
  };
  kj::HashMap<uint64_t, ThirdPartyExchange> tphExchanges;

  ThirdPartyExchange& getTphExchange(uint64_t token) {
    return tphExchanges.findOrCreate(token, [&]() -> decltype(tphExchanges)::Entry {
      return {token, ThirdPartyExchange()};
    });
  }
};

TestNetwork::~TestNetwork() noexcept(false) {}

TestVat& TestNetwork::add(kj::StringPtr name) {
  return *map.insert(name, kj::heap<TestVat>(*this, name)).value;
}

// =======================================================================================

class TestRestorer {
  // The name of this class is historical. It used to implement an interface called
  // SturdyRefRestorer, but that interface was deprecated and removed in favor of singleton
  // bootstrap objects. So now this just holds the boostrap object.
public:
  int callCount = 0;
  int handleCount = 0;

  test::TestInterface::Client cap = kj::heap<TestInterfaceImpl>(callCount, handleCount);
};

struct TestContext {
  kj::EventLoop loop;
  kj::WaitScope waitScope;
  TestNetwork network;
  TestRestorer restorer;

  struct Vat {
    TestVat& vatNetwork;
    RpcSystem<test::TestSturdyRefHostId> rpcSystem;

    Vat(TestVat& vatNetwork)
        : vatNetwork(vatNetwork), rpcSystem(makeRpcClient(vatNetwork)) {}

    template <typename T>
    Vat(TestVat& vatNetwork, T bootstrap)
        : vatNetwork(vatNetwork), rpcSystem(makeRpcServer(vatNetwork, kj::mv(bootstrap))) {}

    template <typename T = test::TestInterface>
    typename T::Client connect(kj::StringPtr to, bool unique = false) {
      MallocMessageBuilder refMessage(128);
      auto hostId = refMessage.initRoot<test::TestSturdyRefHostId>();
      hostId.setHost(to);
      hostId.setUnique(unique);
      return rpcSystem.bootstrap(hostId).castAs<T>();
    }
  };
  kj::HashMap<kj::StringPtr, kj::Own<Vat>> vats;

  Vat& getVat(kj::StringPtr name) {
    // Get or create a vat with the given name. (Name must be a string literal or otherwise
    // outlive the TestContext.)
    return *vats.findOrCreate(name, [&]() -> decltype(vats)::Entry {
      return { name,  kj::heap<Vat>(network.add(name)) };
    });
  }

  template <typename T>
  Vat& initVat(kj::StringPtr name, T bootstrap) {
    // Create a vat with the given name and bootstrap capability.
    return *vats.insert(name, kj::heap<Vat>(network.add(name), kj::mv(bootstrap))).value;
  }

  Vat& alice;
  Vat& bob;
  // For convenience, we create two default vats:
  // - alice is a client (no bootstrap)
  // - bob is a server serving TestInterfaceImpl (restorer.cap) as its bootstrap.

  TestContext()
      : waitScope(loop),
        alice(getVat("alice")),
        bob(initVat("bob", restorer.cap)) {}

  test::TestInterface::Client connect() {
    // Create the default alice -> bob connection.
    return getVat("alice").connect<>("bob");
  }
};

KJ_TEST("basics") {
  TestContext context;

  auto client = context.connect();

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

  KJ_EXPECT(context.restorer.callCount == 0);

  auto response1 = promise1.wait(context.waitScope);

  KJ_EXPECT(response1.getX() == "foo");

  auto response2 = promise2.wait(context.waitScope);

  promise3.wait(context.waitScope);

  KJ_EXPECT(context.restorer.callCount == 2);
  KJ_EXPECT(barFailed);
}

KJ_TEST("pipelining") {
  TestContext context;

  auto client = context.connect().getTestPipelineRequest().send().getCap();

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

  KJ_EXPECT(context.restorer.callCount == 0);
  KJ_EXPECT(chainedCallCount == 0);

  auto response = pipelinePromise.wait(context.waitScope);
  KJ_EXPECT(response.getX() == "bar");

  auto response2 = pipelinePromise2.wait(context.waitScope);
  checkTestMessage(response2);

  KJ_EXPECT(context.restorer.callCount == 3);
  KJ_EXPECT(chainedCallCount == 1);
}

KJ_TEST("sendForPipeline()") {
  TestContext context;

  auto client = context.connect().getTestPipelineRequest().send().getCap();

  int chainedCallCount = 0;

  auto request = client.getCapRequest();
  request.setN(234);
  request.setInCap(kj::heap<TestInterfaceImpl>(chainedCallCount));

  auto pipeline = request.sendForPipeline();

  auto pipelineRequest = pipeline.getOutBox().getCap().fooRequest();
  pipelineRequest.setI(321);
  auto pipelinePromise = pipelineRequest.send();

  auto pipelineRequest2 = pipeline.getOutBox().getCap().castAs<test::TestExtends>().graultRequest();
  auto pipelinePromise2 = pipelineRequest2.send();

  pipeline = nullptr;  // Just to be annoying, drop the original pipeline.

  KJ_EXPECT(context.restorer.callCount == 0);
  KJ_EXPECT(chainedCallCount == 0);

  auto response = pipelinePromise.wait(context.waitScope);
  KJ_EXPECT(response.getX() == "bar");

  auto response2 = pipelinePromise2.wait(context.waitScope);
  checkTestMessage(response2);

  KJ_EXPECT(context.restorer.callCount == 3);
  KJ_EXPECT(chainedCallCount == 1);
}

KJ_TEST("context.setPipeline") {
  TestContext context;

  auto client = context.connect().getTestPipelineRequest().send().getCap();

  auto promise = client.getCapPipelineOnlyRequest().send();

  auto pipelineRequest = promise.getOutBox().getCap().fooRequest();
  pipelineRequest.setI(321);
  auto pipelinePromise = pipelineRequest.send();

  auto pipelineRequest2 = promise.getOutBox().getCap().castAs<test::TestExtends>().graultRequest();
  auto pipelinePromise2 = pipelineRequest2.send();

  KJ_EXPECT(context.restorer.callCount == 0);

  auto response = pipelinePromise.wait(context.waitScope);
  KJ_EXPECT(response.getX() == "bar");

  auto response2 = pipelinePromise2.wait(context.waitScope);
  checkTestMessage(response2);

  KJ_EXPECT(context.restorer.callCount == 3);

  // The original promise never completed.
  KJ_EXPECT(!promise.poll(context.waitScope));
}

KJ_TEST("release capability") {
  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

  auto handle1 = client.getHandleRequest().send().wait(context.waitScope).getHandle();
  auto promise = client.getHandleRequest().send();
  auto handle2 = promise.wait(context.waitScope).getHandle();

  KJ_EXPECT(context.restorer.handleCount == 2);

  handle1 = nullptr;

  for (uint i = 0; i < 16; i++) kj::yield().wait(context.waitScope);
  KJ_EXPECT(context.restorer.handleCount == 1);

  handle2 = nullptr;

  for (uint i = 0; i < 16; i++) kj::yield().wait(context.waitScope);
  KJ_EXPECT(context.restorer.handleCount == 1);

  promise = nullptr;

  for (uint i = 0; i < 16; i++) kj::yield().wait(context.waitScope);
  KJ_EXPECT(context.restorer.handleCount == 0);
}

KJ_TEST("release capabilities when canceled during return") {
  // At one time, there was a bug where if a Return contained capabilities, but the client had
  // canceled the request and already send a Finish (which presumably didn't reach the server before
  // the Return), then we'd leak those caps. Test for that.

  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();
  client.whenResolved().wait(context.waitScope);

  {
    auto promise = client.getHandleRequest().send();

    // If the server receives cancellation too early, it won't even return a capability in the
    // results, it will just return "canceled". We want to emulate the case where the return message
    // and the cancel (finish) message cross paths. It turns out that exactly two yield()s get us
    // there.
    //
    // TODO(cleanup): This is fragile, but I'm not sure how else to write it without a ton
    //   of scaffolding.
    kj::yield().wait(context.waitScope);
    kj::yield().wait(context.waitScope);
  }

  for (uint i = 0; i < 16; i++) kj::yield().wait(context.waitScope);
  KJ_EXPECT(context.restorer.handleCount == 0);
}

KJ_TEST("tail call") {
  TestContext context;

  auto caller = context.connect().getTestTailCallerRequest().send().getCap();

  int calleeCallCount = 0;

  test::TestTailCallee::Client callee(kj::heap<TestTailCalleeImpl>(calleeCallCount));

  auto request = caller.fooRequest();
  request.setI(456);
  request.setCallee(callee);

  auto promise = request.send();

  auto dependentCall0 = promise.getC().getCallSequenceRequest().send();

  auto response = promise.wait(context.waitScope);
  KJ_EXPECT(response.getI() == 456);
  KJ_EXPECT(response.getT() == "from TestTailCaller");

  auto dependentCall1 = promise.getC().getCallSequenceRequest().send();

  KJ_EXPECT(dependentCall0.wait(context.waitScope).getN() == 0);
  KJ_EXPECT(dependentCall1.wait(context.waitScope).getN() == 1);

  // TODO(someday): We used to initiate dependentCall2 here before waiting on the first two calls,
  //   and the ordering was still "correct". But this was apparently by accident. Calling getC() on
  //   the final response returns a different capability from calling getC() on the promise. There
  //   are no guarantees on the ordering of calls on the response capability vs. the earlier
  //   promise. When ordering matters, applications should take the original promise capability and
  //   keep using that. In theory the RPC system could create continuity here, but it would be
  //   annoying: for each capability that had been fetched on the promise, it would need to
  //   traverse to the same capability in the final response and swap it out in-place for the
  //   pipelined cap returned earlier. Maybe we'll determine later that that's really needed but
  //   for now I'm not gonna do it.
  auto dependentCall2 = response.getC().getCallSequenceRequest().send();

  KJ_EXPECT(dependentCall2.wait(context.waitScope).getN() == 2);

  KJ_EXPECT(calleeCallCount == 1);
  KJ_EXPECT(context.restorer.callCount == 1);
}

class TestHangingTailCallee final: public test::TestTailCallee::Server {
public:
  TestHangingTailCallee(int& callCount, int& cancelCount)
      : callCount(callCount), cancelCount(cancelCount) {}

  kj::Promise<void> foo(FooContext context) override {
    ++callCount;
    return kj::Promise<void>(kj::NEVER_DONE)
        .attach(kj::defer([&cancelCount = cancelCount]() { ++cancelCount; }));
  }

private:
  int& callCount;
  int& cancelCount;
};

class TestRacingTailCaller final: public test::TestTailCaller::Server {
public:
  TestRacingTailCaller(kj::Promise<void> unblock): unblock(kj::mv(unblock)) {}

  kj::Promise<void> foo(FooContext context) override {
    return unblock.then([context]() mutable {
      auto tailRequest = context.getParams().getCallee().fooRequest();
      return context.tailCall(kj::mv(tailRequest));
    });
  }

private:
  kj::Promise<void> unblock;
};

KJ_TEST("cancel tail call") {
  TestContext context;

  auto caller = context.connect().getTestTailCallerRequest().send().getCap();

  int callCount = 0, cancelCount = 0;

  test::TestTailCallee::Client callee(kj::heap<TestHangingTailCallee>(callCount, cancelCount));

  {
    auto request = caller.fooRequest();
    request.setCallee(callee);

    auto promise = request.send();

    KJ_ASSERT(callCount == 0);
    KJ_ASSERT(cancelCount == 0);

    KJ_ASSERT(!promise.poll(context.waitScope));

    KJ_ASSERT(callCount == 1);
    KJ_ASSERT(cancelCount == 0);
  }

  kj::Promise<void>(kj::NEVER_DONE).poll(context.waitScope);

  KJ_ASSERT(callCount == 1);
  KJ_ASSERT(cancelCount == 1);
}

KJ_TEST("tail call cancellation race") {
  TestContext context;
  auto paf = kj::newPromiseAndFulfiller<void>();
  context.initVat("carol", kj::heap<TestRacingTailCaller>(kj::mv(paf.promise)));

  auto caller = context.alice.connect<test::TestTailCaller>("carol");

  int callCount = 0, cancelCount = 0;

  test::TestTailCallee::Client callee(kj::heap<TestHangingTailCallee>(callCount, cancelCount));

  {
    auto request = caller.fooRequest();
    request.setCallee(callee);

    auto promise = request.send();

    KJ_ASSERT(callCount == 0);
    KJ_ASSERT(cancelCount == 0);

    KJ_ASSERT(!promise.poll(context.waitScope));

    KJ_ASSERT(callCount == 0);
    KJ_ASSERT(cancelCount == 0);

    // Unblock the server and at the same time cancel the client.
    paf.fulfiller->fulfill();
  }

  kj::Promise<void>(kj::NEVER_DONE).poll(context.waitScope);

  KJ_ASSERT(callCount == 1);
  KJ_ASSERT(cancelCount == 1);
}

KJ_TEST("cancellation") {
  // Tests cancellation.

  TestContext context;

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = paf.promise.then([&]() { destroyed = true; }).eagerlyEvaluate(nullptr);

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

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
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);

  // We can detect that the method was canceled because it will drop the cap.
  KJ_EXPECT(!destroyed);
  KJ_EXPECT(!returned);

  promise = nullptr;  // request cancellation
  destructionPromise.wait(context.waitScope);

  KJ_EXPECT(destroyed);
  KJ_EXPECT(!returned);
}

KJ_TEST("resolve promise") {
  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

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
  KJ_EXPECT(client.getCallSequenceRequest().send().wait(context.waitScope).getN() == 2);
  KJ_EXPECT(context.restorer.callCount == 3);

  // OK, now fulfill the local promise.
  paf.fulfiller->fulfill(kj::heap<TestInterfaceImpl>(chainedCallCount));

  // We should now be able to wait for getCap() to finish.
  KJ_EXPECT(promise.wait(context.waitScope).getS() == "bar");
  KJ_EXPECT(promise2.wait(context.waitScope).getS() == "bar");

  KJ_EXPECT(context.restorer.callCount == 3);
  KJ_EXPECT(chainedCallCount == 2);
}

KJ_TEST("retain and release") {
  TestContext context;

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = paf.promise.then([&]() { destroyed = true; }).eagerlyEvaluate(nullptr);

  {
    auto client = context.connect().getTestMoreStuffRequest().send().getCap();

    {
      auto request = client.holdRequest();
      request.setCap(kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller)));
      request.send().wait(context.waitScope);
    }

    // Do some other call to add a round trip.
    KJ_EXPECT(client.getCallSequenceRequest().send().wait(context.waitScope).getN() == 1);

    // Shouldn't be destroyed because it's being held by the server.
    KJ_EXPECT(!destroyed);

    // We can ask it to call the held capability.
    KJ_EXPECT(client.callHeldRequest().send().wait(context.waitScope).getS() == "bar");

    {
      // We can get the cap back from it.
      auto capCopy = client.getHeldRequest().send().wait(context.waitScope).getCap();

      {
        // And call it, without any network communications.
        uint oldSentCount = context.alice.vatNetwork.getSentCount();
        auto request = capCopy.fooRequest();
        request.setI(123);
        request.setJ(true);
        KJ_EXPECT(request.send().wait(context.waitScope).getX() == "foo");
        KJ_EXPECT(context.alice.vatNetwork.getSentCount() == oldSentCount);
      }

      {
        // We can send another copy of the same cap to another method, and it works.
        auto request = client.callFooRequest();
        request.setCap(capCopy);
        KJ_EXPECT(request.send().wait(context.waitScope).getS() == "bar");
      }
    }

    // Give some time to settle.
    KJ_EXPECT(client.getCallSequenceRequest().send().wait(context.waitScope).getN() == 5);
    KJ_EXPECT(client.getCallSequenceRequest().send().wait(context.waitScope).getN() == 6);
    KJ_EXPECT(client.getCallSequenceRequest().send().wait(context.waitScope).getN() == 7);

    // Can't be destroyed, we haven't released it.
    KJ_EXPECT(!destroyed);
  }

  // We released our client, which should cause the server to be released, which in turn will
  // release the cap pointing back to us.
  destructionPromise.wait(context.waitScope);
  KJ_EXPECT(destroyed);
}

KJ_TEST("cancel releases params") {
  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = paf.promise.then([&]() { destroyed = true; }).eagerlyEvaluate(nullptr);

  {
    auto request = client.neverReturnRequest();
    request.setCap(kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller)));

    {
      auto responsePromise = request.send();

      // Allow some time to settle.
      KJ_EXPECT(client.getCallSequenceRequest().send().wait(context.waitScope).getN() == 1);
      KJ_EXPECT(client.getCallSequenceRequest().send().wait(context.waitScope).getN() == 2);

      // The cap shouldn't have been destroyed yet because the call never returned.
      KJ_EXPECT(!destroyed);
    }
  }

  // Now the cap should be released.
  destructionPromise.wait(context.waitScope);
  KJ_EXPECT(destroyed);
}

KJ_TEST("send same cap twice") {
  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

  auto paf = kj::newPromiseAndFulfiller<void>();
  bool destroyed = false;
  auto destructionPromise = paf.promise.then([&]() { destroyed = true; }).eagerlyEvaluate(nullptr);

  auto cap = test::TestInterface::Client(kj::heap<TestCapDestructor>(kj::mv(paf.fulfiller)));

  {
    auto request = client.callFooRequest();
    request.setCap(cap);

    KJ_EXPECT(request.send().wait(context.waitScope).getS() == "bar");
  }

  // Allow some time for the server to release `cap`.
  KJ_EXPECT(client.getCallSequenceRequest().send().wait(context.waitScope).getN() == 1);

  {
    // More requests with the same cap.
    auto request = client.callFooRequest();
    auto request2 = client.callFooRequest();
    request.setCap(cap);
    request2.setCap(kj::mv(cap));

    auto promise = request.send();
    auto promise2 = request2.send();

    KJ_EXPECT(promise.wait(context.waitScope).getS() == "bar");
    KJ_EXPECT(promise2.wait(context.waitScope).getS() == "bar");
  }

  // Now the cap should be released.
  destructionPromise.wait(context.waitScope);
  KJ_EXPECT(destroyed);
}

RemotePromise<test::TestCallOrder::GetCallSequenceResults> getCallSequence(
    test::TestCallOrder::Client& client, uint expected) {
  auto req = client.getCallSequenceRequest();
  req.setExpected(expected);
  return req.send();
}

KJ_TEST("embargo") {
  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

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

  KJ_EXPECT(call0.wait(context.waitScope).getN() == 0);
  KJ_EXPECT(call1.wait(context.waitScope).getN() == 1);
  KJ_EXPECT(call2.wait(context.waitScope).getN() == 2);
  KJ_EXPECT(call3.wait(context.waitScope).getN() == 3);
  KJ_EXPECT(call4.wait(context.waitScope).getN() == 4);
  KJ_EXPECT(call5.wait(context.waitScope).getN() == 5);
}

KJ_TEST("embargos block CapabilityServerSet") {
  // Test that embargos properly block unwrapping a capability using CapabilityServerSet.

  TestContext context;

  capnp::CapabilityServerSet<test::TestCallOrder> capSet;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

  auto cap = capSet.add(kj::heap<TestCallOrderImpl>());

  auto earlyCall = client.getCallSequenceRequest().send();

  auto echoRequest = client.echoRequest();
  echoRequest.setCap(cap);
  auto echo = echoRequest.send();

  auto pipeline = echo.getCap();

  auto unwrap = capSet.getLocalServer(pipeline)
      .then([](kj::Maybe<test::TestCallOrder::Server&> unwrapped) {
    return kj::downcast<TestCallOrderImpl>(KJ_ASSERT_NONNULL(unwrapped)).getCount();
  }).eagerlyEvaluate(nullptr);

  auto call0 = getCallSequence(pipeline, 0);
  auto call1 = getCallSequence(pipeline, 1);

  earlyCall.wait(context.waitScope);

  auto call2 = getCallSequence(pipeline, 2);

  auto resolved = echo.wait(context.waitScope).getCap();

  auto call3 = getCallSequence(pipeline, 4);
  auto call4 = getCallSequence(pipeline, 4);
  auto call5 = getCallSequence(pipeline, 5);

  KJ_EXPECT(call0.wait(context.waitScope).getN() == 0);
  KJ_EXPECT(call1.wait(context.waitScope).getN() == 1);
  KJ_EXPECT(call2.wait(context.waitScope).getN() == 2);
  KJ_EXPECT(call3.wait(context.waitScope).getN() == 3);
  KJ_EXPECT(call4.wait(context.waitScope).getN() == 4);
  KJ_EXPECT(call5.wait(context.waitScope).getN() == 5);

  uint unwrappedAt = unwrap.wait(context.waitScope);
  KJ_EXPECT(unwrappedAt >= 3, unwrappedAt);
}

template <typename T>
void expectPromiseThrows(kj::Promise<T>&& promise, kj::WaitScope& waitScope) {
  KJ_EXPECT(promise.then([](T&&) { return false; }, [](kj::Exception&&) { return true; })
      .wait(waitScope));
}

template <>
void expectPromiseThrows(kj::Promise<void>&& promise, kj::WaitScope& waitScope) {
  KJ_EXPECT(promise.then([]() { return false; }, [](kj::Exception&&) { return true; })
      .wait(waitScope));
}

KJ_TEST("embargo error") {
  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

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

KJ_TEST("don't embargo null capability") {
  // Set up a situation where we pipeline on a capability that ends up coming back null. This
  // should NOT cause a Disembargo to be sent, but due to a bug in earlier versions of Cap'n Proto,
  // a Disembargo was indeed sent to the null capability, which caused the server to disconnect
  // due to protocol error.

  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

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

KJ_TEST("call promise that later rejects") {
  // Tell the server to call back to a promise client, then resolve the promise to an error.

  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();
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

  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);

  KJ_EXPECT(!returned);

  paf.fulfiller->rejectIfThrows([]() { KJ_FAIL_ASSERT("foo") { break; } });

  expectPromiseThrows(kj::mv(req), context.waitScope);
  KJ_EXPECT(returned);

  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);
  kj::yield().wait(context.waitScope);

  // Verify that we're still connected (there were no protocol errors).
  getCallSequence(client, 1).wait(context.waitScope);
}

KJ_TEST("abort") {
  // Verify that aborts are received.

  TestContext context;

  MallocMessageBuilder refMessage(128);
  auto hostId = refMessage.initRoot<test::TestSturdyRefHostId>();
  hostId.setHost("bob");

  auto conn = KJ_ASSERT_NONNULL(context.alice.vatNetwork.connect(hostId));
  conn->setIdle(false);

  {
    // Send an invalid message (Return to non-existent question).
    auto msg = conn->newOutgoingMessage(128);
    auto body = msg->getBody().initAs<rpc::Message>().initReturn();
    body.setAnswerId(1234);
    body.setCanceled();
    msg->send();
  }

  auto reply = KJ_ASSERT_NONNULL(conn->receiveIncomingMessage().wait(context.waitScope));
  KJ_EXPECT(reply->getBody().getAs<rpc::Message>().which() == rpc::Message::ABORT);

  KJ_EXPECT(conn->receiveIncomingMessage().wait(context.waitScope) == kj::none);
}

KJ_TEST("handles exceptions thrown during disconnect") {
  // This is similar to the earlier "abort" test, but throws an exception on
  // connection shutdown, to exercise the RpcConnectionState error handler.

  TestContext context;

  MallocMessageBuilder refMessage(128);
  auto hostId = refMessage.initRoot<test::TestSturdyRefHostId>();
  hostId.setHost("bob");

  context.bob.vatNetwork.setShutdownExceptionToThrow(
      KJ_EXCEPTION(FAILED, "a_disconnect_exception"));

  auto conn = KJ_ASSERT_NONNULL(context.alice.vatNetwork.connect(hostId));
  conn->setIdle(false);

  {
    // Send an invalid message (Return to non-existent question).
    auto msg = conn->newOutgoingMessage(128);
    auto body = msg->getBody().initAs<rpc::Message>().initReturn();
    body.setAnswerId(1234);
    body.setCanceled();
    msg->send();
  }

  {
    // The internal exception handler of RpcSystemBase logs exceptions thrown
    // during disconnect, which the test framework will flag as a failure if we
    // don't explicitly tell it to expect the logged output.
    KJ_EXPECT_LOG(ERROR, "a_disconnect_exception");

    // Force outstanding promises to completion.  The server should detect the
    // invalid message and disconnect, which should cause the connection's
    // disconnect() to throw an exception that will then be handled by a
    // RpcConnectionState handler.  Since other state instances were freed prior
    // to the handler invocation, this caused failures in earlier versions of
    // the code when run under asan.
    kj::Promise<void>(kj::NEVER_DONE).poll(context.waitScope);
  }
}

KJ_TEST("loopback bootstrap()") {
  TestContext context;

  int callCount = 0;
  test::TestInterface::Client bootstrap = kj::heap<TestInterfaceImpl>(callCount);
  auto& carol = context.initVat("carol", bootstrap);

  auto client = carol.connect("carol").castAs<test::TestInterface>();

  auto request = client.fooRequest();
  request.setI(123);
  request.setJ(true);
  auto response = request.send().wait(context.waitScope);

  KJ_EXPECT(response.getX() == "foo");
  KJ_EXPECT(callCount == 1);
}

KJ_TEST("method throws exception") {
  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

  kj::Maybe<kj::Exception> maybeException;
  client.throwExceptionRequest().send().ignoreResult()
      .catch_([&](kj::Exception&& e) {
    maybeException = kj::mv(e);
  }).wait(context.waitScope);

  auto exception = KJ_ASSERT_NONNULL(maybeException);
  KJ_EXPECT(exception.getDescription() == "remote exception: test exception");
  KJ_EXPECT(exception.getRemoteTrace() == nullptr);
}

KJ_TEST("method throws exception won't redundantly add remote exception prefix") {
  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

  kj::Maybe<kj::Exception> maybeException;
  client.throwRemoteExceptionRequest().send().ignoreResult()
      .catch_([&](kj::Exception&& e) {
    maybeException = kj::mv(e);
  }).wait(context.waitScope);

  auto exception = KJ_ASSERT_NONNULL(maybeException);
  KJ_EXPECT(exception.getDescription() == "remote exception: test exception");
  KJ_EXPECT(exception.getRemoteTrace() == nullptr);
}

KJ_TEST("method throws exception with trace encoder") {
  TestContext context;

  context.bob.rpcSystem.setTraceEncoder([](const kj::Exception& e) {
    return kj::str("trace for ", e.getDescription());
  });

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

  kj::Maybe<kj::Exception> maybeException;
  client.throwExceptionRequest().send().ignoreResult()
      .catch_([&](kj::Exception&& e) {
    maybeException = kj::mv(e);
  }).wait(context.waitScope);

  auto exception = KJ_ASSERT_NONNULL(maybeException);
  KJ_EXPECT(exception.getDescription() == "remote exception: test exception");
  KJ_EXPECT(exception.getRemoteTrace() == "trace for test exception");
}

KJ_TEST("method throws exception with detail") {
  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

  kj::Maybe<kj::Exception> maybeException;
  client.throwExceptionWithDetailRequest().send().ignoreResult()
      .catch_([&](kj::Exception&& e) {
    maybeException = kj::mv(e);
  }).wait(context.waitScope);

  auto exception = KJ_ASSERT_NONNULL(maybeException);
  KJ_EXPECT(exception.getDescription() == "remote exception: test exception");
  KJ_EXPECT(exception.getRemoteTrace() == nullptr);
  auto detail = KJ_ASSERT_NONNULL(exception.getDetail(1));
  KJ_EXPECT(kj::str(detail.asChars()) == "foo");
}

KJ_TEST("when OutgoingRpcMessage::send() throws, we don't leak exports") {
  // When OutgoingRpcMessage::send() throws an exception on a Call message, we need to clean up
  // anything that had been added to the export table as part of the call. At one point this
  // cleanup was missing, so exports would leak.

  TestContext context;

  uint32_t expectedExportNumber = 0;
  uint interceptCount = 0;
  bool shouldThrowFromSend = false;
  context.alice.vatNetwork.onSend([&](MessageBuilder& builder) {
    auto message = builder.getRoot<rpc::Message>().asReader();
    if (message.isCall()) {
      auto call = message.getCall();
      if (call.getInterfaceId() == capnp::typeId<test::TestMoreStuff>() &&
          call.getMethodId() == 0) {
        // callFoo() request, expect a capability in the param caps. Specifically we expect a
        // promise, because that's what we send below.
        auto capTable = call.getParams().getCapTable();
        KJ_ASSERT(capTable.size() == 1);
        auto desc = capTable[0];
        KJ_ASSERT(desc.isSenderPromise());
        KJ_ASSERT(desc.getSenderPromise() == expectedExportNumber);

        ++interceptCount;
        if (shouldThrowFromSend) {
          kj::throwFatalException(KJ_EXCEPTION(FAILED, "intercepted"));
        }
      }
    }
    return true;
  });

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

  {
    shouldThrowFromSend = true;
    auto req = client.callFooRequest();
    req.setCap(kj::Promise<test::TestInterface::Client>(kj::NEVER_DONE));
    req.send().then([](auto&&) {
      KJ_FAIL_ASSERT("should have thrown");
    }, [](kj::Exception&& e) {
      KJ_EXPECT(e.getDescription() == "intercepted", e);
    }).wait(context.waitScope);
  }

  KJ_EXPECT(interceptCount == 1);

  // Sending again should use the same export number, because the export table entry should have
  // been released when send() threw. (At one point, this was a bug...)
  {
    shouldThrowFromSend = true;
    auto req = client.callFooRequest();
    req.setCap(kj::Promise<test::TestInterface::Client>(kj::NEVER_DONE));
    req.send().then([](auto&&) {
      KJ_FAIL_ASSERT("should have thrown");
    }, [](kj::Exception&& e) {
      KJ_EXPECT(e.getDescription() == "intercepted", e);
    }).wait(context.waitScope);
  }

  KJ_EXPECT(interceptCount == 2);

  // Now lets start a call that doesn't throw. The export number should still be zero because
  // the previous exports were released.
  {
    shouldThrowFromSend = false;
    auto req = client.callFooRequest();
    req.setCap(kj::Promise<test::TestInterface::Client>(kj::NEVER_DONE));
    auto promise = req.send();
    KJ_EXPECT(!promise.poll(context.waitScope));

    KJ_EXPECT(interceptCount == 3);
  }

  // We canceled the previous call, BUT the exported capability is still present until the other
  // side drops it, which it won't because the call isn't marked cancelable and never completes.
  // Now, let's send another call. This time, we expect a new export number will actually be
  // allocated.
  {
    shouldThrowFromSend = false;
    expectedExportNumber = 1;
    auto req = client.callFooRequest();
    auto paf = kj::newPromiseAndFulfiller<test::TestInterface::Client>();
    req.setCap(kj::mv(paf.promise));
    auto promise = req.send();
    KJ_EXPECT(!promise.poll(context.waitScope));

    KJ_EXPECT(interceptCount == 4);

    // Now let's actually let the RPC complete so we can verify the RPC system isn't broken or
    // anything.
    int callCount = 0;
    paf.fulfiller->fulfill(kj::heap<TestInterfaceImpl>(callCount));
    auto resp = promise.wait(context.waitScope);
    KJ_EXPECT(resp.getS() == "bar");
    KJ_EXPECT(callCount == 1);
  }

  // Now if we do yet another call, it'll reuse export number 1.
  {
    shouldThrowFromSend = false;
    expectedExportNumber = 1;
    auto req = client.callFooRequest();
    req.setCap(kj::Promise<test::TestInterface::Client>(kj::NEVER_DONE));
    auto promise = req.send();
    KJ_EXPECT(!promise.poll(context.waitScope));

    KJ_EXPECT(interceptCount == 5);
  }
}

KJ_TEST("export the same promise twice") {
  TestContext context;

  bool exportIsPromise;
  uint32_t expectedExportNumber;
  uint interceptCount = 0;
  context.alice.vatNetwork.onSend([&](MessageBuilder& builder) {
    auto message = builder.getRoot<rpc::Message>().asReader();
    if (message.isCall()) {
      auto call = message.getCall();
      if (call.getInterfaceId() == capnp::typeId<test::TestMoreStuff>() &&
          call.getMethodId() == 0) {
        // callFoo() request, expect a capability in the param caps. Specifically we expect a
        // promise, because that's what we send below.
        auto capTable = call.getParams().getCapTable();
        KJ_ASSERT(capTable.size() == 1);
        auto desc = capTable[0];
        if (exportIsPromise) {
          KJ_ASSERT(desc.isSenderPromise());
          KJ_ASSERT(desc.getSenderPromise() == expectedExportNumber);
        } else {
          KJ_ASSERT(desc.isSenderHosted());
          KJ_ASSERT(desc.getSenderHosted() == expectedExportNumber);
        }

        ++interceptCount;
      }
    }
    return true;
  });

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();

  auto sendReq = [&](test::TestInterface::Client cap) {
    auto req = client.callFooRequest();
    req.setCap(kj::mv(cap));
    return req.send();
  };

  auto expectNeverDone = [&](auto& promise) {
    if (promise.poll(context.waitScope)) {
      promise.wait(context.waitScope);  // let it throw if it's going to
      KJ_FAIL_ASSERT("promise finished without throwing");
    }
  };

  int callCount = 0;
  test::TestInterface::Client normalCap = kj::heap<TestInterfaceImpl>(callCount);
  test::TestInterface::Client promiseCap = kj::Promise<test::TestInterface::Client>(kj::NEVER_DONE);

  // Send request with a promise capability in the params.
  exportIsPromise = true;
  expectedExportNumber = 0;
  auto promise1 = sendReq(promiseCap);
  expectNeverDone(promise1);
  KJ_EXPECT(interceptCount == 1);

  // Send a second request with the same promise should use the same export table entry.
  auto promise2 = sendReq(promiseCap);
  expectNeverDone(promise2);
  KJ_EXPECT(interceptCount == 2);

  // Sending a request with a different promise should use a different export table entry.
  expectedExportNumber = 1;
  auto promise3 = sendReq(kj::Promise<test::TestInterface::Client>(kj::NEVER_DONE));
  expectNeverDone(promise3);
  KJ_EXPECT(interceptCount == 3);

  // Now try sending a non-promise cap. We'll send all these requests at once before waiting on
  // any of them since these will actually complete.
  exportIsPromise = false;
  expectedExportNumber = 2;
  auto promise4 = sendReq(normalCap);
  auto promise5 = sendReq(normalCap);
  expectedExportNumber = 3;
  auto promise6 = sendReq(kj::heap<TestInterfaceImpl>(callCount));
  KJ_EXPECT(interceptCount == 6);

  KJ_EXPECT(promise4.wait(context.waitScope).getS() == "bar");
  KJ_EXPECT(promise5.wait(context.waitScope).getS() == "bar");
  KJ_EXPECT(promise6.wait(context.waitScope).getS() == "bar");
  KJ_EXPECT(callCount == 3);
}

KJ_TEST("connections set idle when appropriate") {
  TestContext context;

  auto client = context.connect().getTestMoreStuffRequest().send().getCap();
  context.waitScope.poll();  // let messages propagate

  auto& clientConn = KJ_ASSERT_NONNULL(
      context.alice.vatNetwork.getConnectionTo(context.bob.vatNetwork));
  auto& serverConn = KJ_ASSERT_NONNULL(
      context.bob.vatNetwork.getConnectionTo(context.alice.vatNetwork));

  KJ_EXPECT(!clientConn.isIdle());
  KJ_EXPECT(!serverConn.isIdle());

  {auto drop = kj::mv(client);}
  context.waitScope.poll();  // let messages propagate

  // We dropped the only capability, so now the connections are idle.
  KJ_EXPECT(clientConn.isIdle());
  KJ_EXPECT(serverConn.isIdle());

  // Establish bootstrap again.
  client = context.connect().getTestMoreStuffRequest().send().getCap();
  context.waitScope.poll();  // let messages propagate

  // Once again, not idle.
  KJ_EXPECT(!clientConn.isIdle());
  KJ_EXPECT(!serverConn.isIdle());

  // Start a call, and immediately drop the cap.
  auto promise = client.neverReturnRequest().send();
  {auto drop = kj::mv(client);}
  context.waitScope.poll();  // let messages propagate

  // Still not idle.
  KJ_EXPECT(!clientConn.isIdle());
  KJ_EXPECT(!serverConn.isIdle());

  // But if we cancel the call...
  {auto drop = kj::mv(promise);}
  context.waitScope.poll();  // let messages propagate

  // Now we are idle!
  KJ_EXPECT(clientConn.isIdle());
  KJ_EXPECT(serverConn.isIdle());
}

KJ_TEST("clean connection shutdown") {
  TestContext context;

  // Open a connection and immediately drop the capability.
  {
    auto client = context.connect().getTestMoreStuffRequest().send().getCap();
    context.waitScope.poll();  // let messages propagate
  }
  context.waitScope.poll();  // let messages propagate

  // How many messages have we sent at this point?
  auto sent = context.alice.vatNetwork.getSentCount();
  auto received = context.alice.vatNetwork.getReceivedCount();

  // Have the client connection decide to end itself due to idleness, as is allowed under the
  // setIdle() contract.
  auto& clientConn = KJ_ASSERT_NONNULL(
      context.alice.vatNetwork.getConnectionTo(context.bob.vatNetwork));
  KJ_EXPECT(clientConn.isIdle());
  clientConn.initiateIdleShutdown();

  context.waitScope.poll();  // let messages propagate

  // Connections should have gracefully shut down.
  KJ_EXPECT(context.alice.vatNetwork.getConnectionTo(context.bob.vatNetwork) == kj::none);
  KJ_EXPECT(context.bob.vatNetwork.getConnectionTo(context.alice.vatNetwork) == kj::none);

  // No more messages should have been sent during shutdown (not even errors).
  KJ_EXPECT(context.alice.vatNetwork.getSentCount() == sent);
  KJ_EXPECT(context.alice.vatNetwork.getReceivedCount() == received);
}

KJ_TEST("basic three-party handoff") {
  TestContext context;

  // Set up Alice, Bob, and Carol.
  auto& alice = context.alice;
  auto& bob = context.bob;

  int carolCallCount = 0;
  int carolHandleCount = 0;
  auto& carol = context.initVat("carol",
      kj::heap<TestMoreStuffImpl>(carolCallCount, carolHandleCount));

  // Alice connects to Bob and Carol.
  auto bobCap = context.alice.connect<test::TestInterface>("bob");
  auto carolCap = context.alice.connect<test::TestMoreStuff>("carol");

  // Alice should be connected to Bob. Get the connection for further inspection.
  auto& aliceToBob = KJ_ASSERT_NONNULL(alice.vatNetwork.getConnectionTo(bob.vatNetwork));

  // Alice -> Bob connection is not idle.
  KJ_EXPECT(!aliceToBob.isIdle());

  // Carol is not connected to Bob.
  KJ_EXPECT(carol.vatNetwork.getConnectionTo(bob.vatNetwork) == kj::none);

  // Send `bobCap` to Carol.
  {
    auto req = carolCap.holdRequest();
    req.setCap(kj::mv(bobCap));
    req.send().wait(context.waitScope);
  }

  // Pump event loop to allow things to settle.
  context.waitScope.poll();

  // Bob hasn't been called yet but Carol has.
  KJ_EXPECT(context.restorer.callCount == 0);
  KJ_EXPECT(carolCallCount == 1);

  // Carol is not connected to Bob yet because Carol accepts the capability lazily on first
  // invocation.
  KJ_EXPECT(carol.vatNetwork.getConnectionTo(bob.vatNetwork) == kj::none);

  // Tell Carol to invoke the held capability. This forces Carol to connect to Bob.
  {
    auto resp = carolCap.callHeldRequest().send().wait(context.waitScope);
    KJ_EXPECT(resp.getS() == "bar");
  }

  // Bob has been called.
  KJ_EXPECT(context.restorer.callCount == 1);

  // Carol is now connected to Bob, and not idle.
  auto& carolToBob = KJ_ASSERT_NONNULL(carol.vatNetwork.getConnectionTo(bob.vatNetwork));
  KJ_EXPECT(!carolToBob.isIdle());

  // Alice -> Bob connection is idle, since we moved away `bobCap`.
  KJ_EXPECT(aliceToBob.isIdle());

  // Let's shut it down, even, to prove it isn't needed.
  aliceToBob.initiateIdleShutdown();
  context.waitScope.poll();
  KJ_EXPECT(alice.vatNetwork.getConnectionTo(bob.vatNetwork) == kj::none);

  // Do another call to prove shutting down the connection didn't hurt.
  {
    auto resp = carolCap.callHeldRequest().send().wait(context.waitScope);
    KJ_EXPECT(resp.getS() == "bar");
  }

  // Bob has been called agani.
  KJ_EXPECT(context.restorer.callCount == 2);

  // Carol -> Bob connection is still not idle.
  KJ_EXPECT(!carolToBob.isIdle());

  // Tell Carol to drop the cap to Bob, by sending a hold request but leaving the capability null.
  carolCap.holdRequest().send().wait(context.waitScope);

  // Now Carol -> Bob connection is idle.
  KJ_EXPECT(carolToBob.isIdle());

  // Alice is still notn connected to Bob.
  KJ_EXPECT(alice.vatNetwork.getConnectionTo(bob.vatNetwork) == kj::none);

  // No forwarding should have occurred in this test.
  KJ_EXPECT(context.network.forwardCount == 0);
  KJ_EXPECT(context.network.deniedForwardCount == 0);
}

KJ_TEST("three-party handoff introduce to self") {
  TestContext context;

  // Create a CapabilityServerSet with a TestInterfaceImpl in it.
  CapabilityServerSet<test::TestInterface> capSet;
  TestInterfaceImpl* ptr;
  test::TestInterface::Client cap = nullptr;
  int callCount = 0;
  {
    auto obj = kj::heap<TestInterfaceImpl>(callCount);
    ptr = obj;
    cap = capSet.add(kj::mv(obj));
  }

  // Create Carol, whose bootstrap is a TestMoreStuffImpl. (Can't use Bob because calling
  // getTestMoreStuff() returns a new instance each time, but we want a single shared instance.)
  int carolCallCount = 0;
  int carolHandleCount = 0;
  auto& carol = context.initVat("carol",
      kj::heap<TestMoreStuffImpl>(carolCallCount, carolHandleCount));

  // Connect to Carol by two different means, getting the same capability back. The second
  // connection is "unique" so it isn't shared with the first.
  auto carolCap1 = context.alice.connect<test::TestMoreStuff>("carol", false);
  auto carolCap2 = context.alice.connect<test::TestMoreStuff>("carol", true);

  // Get a reference to the first Carol -> Alice connection, and tell it to block itself as soon
  // as a `Provide` message is sent. This should block the subsequent `Disembargo`.
  auto& conn1 = KJ_ASSERT_NONNULL(carol.vatNetwork.getConnectionTo(context.alice.vatNetwork));
  conn1.blockAfter([](rpc::Message::Reader msg) {
    return msg.isProvide();
  });

  // Pass a capability to Carol over the first connection.
  {
    auto req = carolCap1.holdRequest();
    req.setCap(cap);
    req.send().wait(context.waitScope);
  }

  // Receive it back over the second, while making a pipelined request.
  test::TestInterface::Client roundTripCap = nullptr;
  kj::Promise<void> call1 = nullptr;
  {
    auto pipeline = carolCap2.getHeldRequest().send();

    auto req = pipeline.getCap().fooRequest();
    req.setI(123);
    req.setJ(true);
    req.setExpectedCallCount(0);
    call1 = req.send().ignoreResult();

    roundTripCap = pipeline.wait(context.waitScope).getCap();
  }

  // Make a second call on the final returned capability.
  auto call2 = [&]() {
    auto req = roundTripCap.fooRequest();
    req.setI(123);
    req.setJ(true);
    req.setExpectedCallCount(1);
    return req.send();
  }();

  // Also try to unwrap it.
  auto unwrapPromise = capSet.getLocalServer(roundTripCap);

  // All of that should be blocked on the embargo which is blocked on conn1.
  KJ_EXPECT(!call1.poll(context.waitScope));
  KJ_EXPECT(!call2.poll(context.waitScope));
  KJ_EXPECT(!unwrapPromise.poll(context.waitScope));
  KJ_EXPECT(callCount == 0);

  // Unblock it.
  conn1.unblock();

  // Calls should complete (and shouldn't fail from incorrect ordering).
  call1.wait(context.waitScope);
  call2.wait(context.waitScope);
  KJ_EXPECT(callCount == 2);

  // We should now be able to fully unwrap the capability, too.
  auto& roundTripObj = KJ_ASSERT_NONNULL(unwrapPromise.wait(context.waitScope));

  // It should have unwrapped to the same original object.
  KJ_EXPECT(&roundTripObj == ptr);
}

class TestNoTailForwarder final: public test::TestMoreStuff::Server {
  // Capability which just forwards to some other capability, but without using a tail call.
  //
  // If `holdBouncer` is provided, then calls to `hold()` will additionally round-trip the
  // capability to the bouncer and back, adding a leg that should get path-shortened away if
  // everything is working correctly.

public:
  TestNoTailForwarder(test::TestMoreStuff::Client next,
                      kj::Maybe<test::TestMoreStuff::Client> holdBouncer)
      : next(kj::mv(next)), holdBouncer(kj::mv(holdBouncer)) {}

  DispatchCallResult dispatchCall(uint64_t interfaceId, uint16_t methodId,
                                  CallContext<AnyPointer, AnyPointer> context) override {
    if (interfaceId == capnp::typeId<test::TestMoreStuff>() && methodId == 3) {
      // Calling TestMoreStuff.hold(). Dispatch normally.
      return test::TestMoreStuff::Server::dispatchCall(interfaceId, methodId, context);
    }

    auto params = context.getParams();
    auto req = next.typelessRequest(interfaceId, methodId, params.targetSize(), {});
    req.set(params);
    auto promise = req.send().then([context](Response<AnyPointer> resp) mutable {
      context.getResults(resp.targetSize()).set(resp);
    });
    return { .promise = kj::mv(promise), .isStreaming = false };
  }

private:
  test::TestMoreStuff::Client next;
  kj::Maybe<test::TestMoreStuff::Client> holdBouncer;

  kj::Promise<void> hold(HoldContext context) override {
    auto cap = context.getParams().getCap();

    // If we have a holdBouncer, bounce the capability through it first before forwarding on.
    KJ_IF_SOME(hb, holdBouncer) {
      auto req = hb.echoRequest();

      // HACK: echo() just reflects the cap back, but it was written to expect TestCallOrder
      //   specifically. We'll just pretend that's what we have, it doesn't matter.
      // TODO(cleanup): Change echo() to take `Capability` or maybe even be a generic.
      req.setCap(cap.castAs<test::TestCallOrder>());
      auto resp = co_await req.send();
      cap = resp.getCap().castAs<test::TestInterface>();
    }

    auto req = next.holdRequest();
    req.setCap(kj::mv(cap));
    co_await req.send();

    // (hold() has no results)
  }
};

void doForwardingTest(bool allowForwarding, bool addReflectionLeg) {
  TestContext context;
  context.network.forwardingEnabled = allowForwarding;

  // Set up Alice, Bob, Carol, and Dave.
  auto& alice = context.alice;
  auto& bob = context.bob;

  int carolCallCount = 0;
  int carolHandleCount = 0;
  auto& carol = context.initVat("carol",
      kj::heap<TestMoreStuffImpl>(carolCallCount, carolHandleCount));

  int eveCallCount = 0;
  int eveHandleCount = 0;

  // Arrange for Dave to forward all calls to Carol.
  auto& dave = [&]() -> TestContext::Vat& {
    auto paf = kj::newPromiseAndFulfiller<Capability::Client>();
    auto& dave = context.initVat("dave", kj::mv(paf.promise));

    kj::Maybe<test::TestMoreStuff::Client> holdBouncer = kj::none;
    if (addReflectionLeg) {
      // Add a `holdBouncer`, so the capability gets bounced from Dave to Eve and back to Dave
      // again before forwarding on to Carol.
      context.initVat("eve", kj::heap<TestMoreStuffImpl>(eveCallCount, eveHandleCount));
      holdBouncer = dave.connect<test::TestMoreStuff>("eve");
    }

    paf.fulfiller->fulfill(kj::heap<TestNoTailForwarder>(
        dave.connect<test::TestMoreStuff>("carol"), kj::mv(holdBouncer)));
    return dave;
  }();

  // Alice connects to Bob and Dave.
  auto bobCap = context.alice.connect<test::TestInterface>("bob");
  auto daveCap = context.alice.connect<test::TestMoreStuff>("dave");

  // Send `bobCap` to Dave, who sends it on to Carol.
  {
    auto req = daveCap.holdRequest();
    req.setCap(kj::mv(bobCap));
    req.send().wait(context.waitScope);
  }

  // Pump event loop to allow things to settle.
  context.waitScope.poll();

  // Tell Dave to invoke the held capability. Dave forwards to Carol. This forces Carol to connect
  // to Bob.
  {
    auto resp = daveCap.callHeldRequest().send().wait(context.waitScope);
    KJ_EXPECT(resp.getS() == "bar");
  }

  // Dave is connected to Carol, who is connected to Bob.
  KJ_EXPECT(dave.vatNetwork.getConnectionTo(carol.vatNetwork) != kj::none);
  KJ_EXPECT(carol.vatNetwork.getConnectionTo(bob.vatNetwork) != kj::none);

  // Carol never connected to Alice.
  KJ_EXPECT(carol.vatNetwork.getConnectionTo(alice.vatNetwork) == kj::none);

  if (allowForwarding) {
    // Dave never connected to Bob.
    KJ_EXPECT(dave.vatNetwork.getConnectionTo(bob.vatNetwork) == kj::none);

    // Forwarding occurred.
    KJ_EXPECT(context.network.forwardCount == addReflectionLeg ? 3 : 1);
    KJ_EXPECT(context.network.deniedForwardCount == 0);
  } else {
    // Dave had to accept the capability from Bob before forwarding it, so had to connect to bob.
    KJ_EXPECT(dave.vatNetwork.getConnectionTo(bob.vatNetwork) != kj::none);

    // No forwarding occurred.
    KJ_EXPECT(context.network.forwardCount == 0);
    KJ_EXPECT(context.network.deniedForwardCount == 1);
  }
}

KJ_TEST("four-party handoff with forwarding") {
  doForwardingTest(true, false);
}

KJ_TEST("four-party handoff without forwarding") {
  doForwardingTest(false, false);
}

KJ_TEST("four-party handoff with forwarding through a reflected path") {
  doForwardingTest(true, true);
}

KJ_TEST("three-party handoff with embargos") {
  TestContext context;

  // Set up Alice, Bob, and Carol.
  auto& alice = context.alice;
  auto& bob = context.bob;

  int carolCallCount = 0;
  int carolHandleCount = 0;
  auto& carol = context.initVat("carol",
      kj::heap<TestMoreStuffImpl>(carolCallCount, carolHandleCount));

  // Alice connects to Bob and Carol.
  auto bobCap = context.alice.connect<test::TestInterface>("bob");
  auto carolCap = context.alice.connect<test::TestMoreStuff>("carol");

  // Send `bobCap` to Carol to hold for later.
  {
    auto req = carolCap.holdRequest();
    req.setCap(kj::mv(bobCap));
    req.send().wait(context.waitScope);
  }

  // Tell Carol to invoke the held capability. This forces Carol to connect to Bob and to release
  // the vine.
  {
    auto resp = carolCap.callHeldRequest().send().wait(context.waitScope);
    KJ_EXPECT(resp.getS() == "bar");
  }

  // Pump event loop to allow things to settle.
  context.waitScope.poll();

  // Alice -> Bob connection is idle since we passed the capability to Carol.
  auto& aliceToBob = KJ_ASSERT_NONNULL(alice.vatNetwork.getConnectionTo(bob.vatNetwork));
  KJ_EXPECT(aliceToBob.isIdle());

  // Block the connection from Carol -> Bob immediately after the `Provide` message is sent. This
  // will prevent pipelined calls and the Disembargo from going through.
  auto& carolToBob = KJ_ASSERT_NONNULL(carol.vatNetwork.getConnectionTo(bob.vatNetwork));
  carolToBob.blockAfter([](rpc::Message::Reader msg) {
    return msg.isProvide();
  });

  // Now get the cap back from Carol.
  auto promise = carolCap.getHeldRequest().send();

  // Immediately do a pipelined call, which should go Alice -> Carol -> Bob. (But the second hop
  // is blocked.)
  auto call1 = [&]() {
    auto req = promise.getCap().fooRequest();
    req.setI(123);
    req.setJ(true);
    req.setExpectedCallCount(1);
    return req.send();
  }();

  // Now actually wait for the call to Carol to return.
  auto response = promise.wait(context.waitScope);

  // And do a call on that.
  auto call2 = [&]() {
    auto req = response.getCap().fooRequest();
    req.setI(123);
    req.setJ(true);
    req.setExpectedCallCount(2);
    return req.send();
  }();

  // Neither call is able to make progress since call1 must be delivered first.
  KJ_EXPECT(!call1.poll(context.waitScope));
  KJ_EXPECT(!call2.poll(context.waitScope));
  KJ_EXPECT(context.restorer.callCount == 1);

  // Unblock Carol -> Bob so the provision can complete.
  carolToBob.unblock();

  call1.wait(context.waitScope);
  call2.wait(context.waitScope);
  KJ_EXPECT(context.restorer.callCount == 3);
}

class TestTailForwarder final: public Capability::Server {
  // Capability which just forwards to some other capability, always using a tail call.
  //
  // This prevents three-party capability handoff from simply shortening the path, but causes
  // calls through it to use three-party call forwarding.

public:
  TestTailForwarder(Capability::Client next): next(kj::mv(next)) {}

  DispatchCallResult dispatchCall(uint64_t interfaceId, uint16_t methodId,
                                  CallContext<AnyPointer, AnyPointer> context) override {
    auto params = context.getParams();
    auto req = next.typelessRequest(interfaceId, methodId, params.targetSize(), {});
    req.set(params);
    auto promise = context.tailCall(kj::mv(req));
    return { .promise = kj::mv(promise), .isStreaming = false };
  }

private:
  Capability::Client next;
};

KJ_TEST("basic three-party call handoff") {
  TestContext context;
  context.network.callHandoffEnabled = true;

  // Set up Alice, Bob, and Carol.
  auto& alice = context.alice;
  auto& bob = context.bob;

  // Arrange that Carol always tail-calls to Bob, without actually path-shortening to Bob.
  {
    auto paf = kj::newPromiseAndFulfiller<Capability::Client>();
    auto& carol = context.initVat("carol", kj::mv(paf.promise));
    paf.fulfiller->fulfill(kj::heap<TestTailForwarder>(carol.connect<Capability>("bob")));
  }

  auto carolCap = alice.connect<test::TestInterface>("carol");

  context.waitScope.poll();

  // Alice has not connected to Bob.
  KJ_EXPECT(alice.vatNetwork.getConnectionTo(bob.vatNetwork) == kj::none);

  // Call carol
  {
    auto req = carolCap.fooRequest();
    req.setI(123);
    req.setJ(true);
    req.setExpectedCallCount(0);
    auto resp = req.send().wait(context.waitScope);
    KJ_EXPECT(resp.getX() == "foo");
  }

  // Bob got a call.
  KJ_EXPECT(context.restorer.callCount == 1);

  // Alice now HAS connected to Bob.
  auto& aliceToBob = KJ_ASSERT_NONNULL(alice.vatNetwork.getConnectionTo(bob.vatNetwork));

  // The connection is idle at this point.
  KJ_EXPECT(aliceToBob.isIdle());

  // The introduction was for calls, not caps.
  KJ_EXPECT(context.network.introCount == 0);
  KJ_EXPECT(context.network.callIntroCount == 1);
}

KJ_TEST("complex three-party call handoff") {
  // Like previous test but we're going to do some pipelining and capability-passing.

  TestContext context;
  context.network.callHandoffEnabled = true;

  // Set up Alice, Bob, and Carol.
  auto& alice = context.alice;
  auto& bob = context.bob;

  // Arrange that Carol always tail-calls to Bob, without actually path-shortening to Bob.
  {
    auto paf = kj::newPromiseAndFulfiller<Capability::Client>();
    auto& carol = context.initVat("carol", kj::mv(paf.promise));
    paf.fulfiller->fulfill(kj::heap<TestTailForwarder>(carol.connect<Capability>("bob")));
  }

  auto carolCap = alice.connect<test::TestInterface>("carol");

  context.waitScope.poll();

  // Alice has not connected to Bob.
  KJ_EXPECT(alice.vatNetwork.getConnectionTo(bob.vatNetwork) == kj::none);

  int callCount = 0;
  test::TestInterface::Client cap = kj::heap<TestInterfaceImpl>(callCount);

  // Call Carol, make a pipeline call, and pass a capability to call back.
  {
    auto moreStuff = carolCap.getTestMoreStuffRequest().send().getCap();

    auto req = moreStuff.callFooRequest();
    req.setCap(cap);
    req.send().wait(context.waitScope);
  }

  KJ_EXPECT(callCount == 1);

  // Alice now HAS connected to Bob.
  auto& aliceToBob = KJ_ASSERT_NONNULL(alice.vatNetwork.getConnectionTo(bob.vatNetwork));

  // Pump event loop to give a chance to detect idleness.
  context.waitScope.poll();

  // The Alice -> Bob connection is idle at this point.
  KJ_EXPECT(aliceToBob.isIdle());

  // Two calls were forwarded (getTestMoreStuff() and callFoo()), and a capability (passed to
  // callFoo()).
  KJ_EXPECT(context.network.introCount == 1);
  KJ_EXPECT(context.network.callIntroCount == 2);
}

KJ_TEST("call handoff introduction to self") {
  // Like previous test but we're going to involve an introduction-to-self.

  TestContext context;
  context.network.callHandoffEnabled = true;

  auto& bob = context.bob;

  // Arrange that Carol always tail-calls to Bob, without actually path-shortening to Bob.
  {
    auto paf = kj::newPromiseAndFulfiller<Capability::Client>();
    auto& carol = context.initVat("carol", kj::mv(paf.promise));
    paf.fulfiller->fulfill(kj::heap<TestTailForwarder>(carol.connect<Capability>("bob")));
  }

  // Now have Bob connect to Carol through a unique connection -- separate from the existing
  // connection.
  auto carolCap = bob.connect<test::TestInterface>("carol", true);

  context.waitScope.poll();

  int callCount = 0;
  test::TestInterface::Client cap = kj::heap<TestInterfaceImpl>(callCount);

  // Call Carol from Bob, make a pipeline call, and pass a capability to call back.
  {
    auto moreStuff = carolCap.getTestMoreStuffRequest().send().getCap();

    auto req = moreStuff.callFooRequest();
    req.setCap(cap);
    req.send().wait(context.waitScope);
  }

  KJ_EXPECT(callCount == 1);

  // Two calls were forwarded (getTestMoreStuff() and callFoo()), and a capability (passed to
  // callFoo()).
  KJ_EXPECT(context.network.introCount == 1);
  KJ_EXPECT(context.network.callIntroCount == 2);
}

void doCallForwardingTest(bool allowForwarding) {
  TestContext context;
  context.network.callHandoffEnabled = true;
  context.network.forwardingEnabled = allowForwarding;

  // Set up Alice, Bob, and Carol.
  auto& alice = context.alice;
  auto& bob = context.bob;

  // Arrange that Carol always tail-calls to Bob, without actually path-shortening to Bob.
  auto& carol = [&]() -> TestContext::Vat& {
    auto paf = kj::newPromiseAndFulfiller<Capability::Client>();
    auto& carol = context.initVat("carol", kj::mv(paf.promise));
    paf.fulfiller->fulfill(kj::heap<TestTailForwarder>(carol.connect<Capability>("bob")));
    return carol;
  }();

  // Arrange that Dave similarly tail-calls to Carol.
  {
    auto paf = kj::newPromiseAndFulfiller<Capability::Client>();
    auto& dave = context.initVat("dave", kj::mv(paf.promise));
    paf.fulfiller->fulfill(kj::heap<TestTailForwarder>(dave.connect<Capability>("carol")));
  }

  auto daveCap = alice.connect<test::TestInterface>("dave");

  context.waitScope.poll();

  // Alice has not connected to Bob.
  KJ_EXPECT(alice.vatNetwork.getConnectionTo(bob.vatNetwork) == kj::none);

  // Call Dave
  {
    auto req = daveCap.fooRequest();
    req.setI(123);
    req.setJ(true);
    req.setExpectedCallCount(0);
    auto resp = req.send().wait(context.waitScope);
    KJ_EXPECT(resp.getX() == "foo");
  }

  // Bob got a call.
  KJ_EXPECT(context.restorer.callCount == 1);

  // Alice now HAS connected to Bob.
  auto& aliceToBob = KJ_ASSERT_NONNULL(alice.vatNetwork.getConnectionTo(bob.vatNetwork));

  // The connection is idle at this point.
  KJ_EXPECT(aliceToBob.isIdle());

  if (allowForwarding) {
    // With forwarding, Alice never should have received a connection from Carol.
    KJ_EXPECT(alice.vatNetwork.getConnectionTo(carol.vatNetwork) == kj::none);

    KJ_EXPECT(context.network.introCount == 0);
    KJ_EXPECT(context.network.callIntroCount == 1);
    KJ_EXPECT(context.network.callForwardCount == 1);
  } else {
    // Without forwarding, Carol had to connect to Alice just to tell Alice to expect another
    // connection from Bob.
    auto& aliceToCarol = KJ_ASSERT_NONNULL(alice.vatNetwork.getConnectionTo(carol.vatNetwork));
    KJ_EXPECT(aliceToCarol.isIdle());

    KJ_EXPECT(context.network.introCount == 0);
    KJ_EXPECT(context.network.callIntroCount == 2);
    KJ_EXPECT(context.network.callForwardCount == 0);
  }
}

KJ_TEST("three-party call handoff with forwarding") {
  doCallForwardingTest(true);
}

KJ_TEST("three-party call handoff with forwarding blocked") {
  doCallForwardingTest(false);
}

enum EmbargoTestArrangement {
  THREE_PARTY,
  SELF_INTRODUCTION,
  FOUR_PARTY_BLOCK_CAROL,
  FOUR_PARTY_BLOCK_DAVE,
};

void doEmbargoTest(EmbargoTestArrangement arrangement,
                   bool forwardingEnabled,
                   bool disconnect) {
  // Like previous test but we're going to do some pipelining and capability-passing.

  TestContext context;
  context.network.callHandoffEnabled = true;
  context.network.forwardingEnabled = forwardingEnabled;

  // Set up Alice, Bob, and Carol.
  auto& alice = context.alice;
  auto& bob = context.bob;

  // Arrange that Carol always tail-calls to Bob, without actually path-shortening to Bob.
  auto& carol = [&]() -> TestContext::Vat& {
    auto paf = kj::newPromiseAndFulfiller<Capability::Client>();
    auto& carol = context.initVat("carol", kj::mv(paf.promise));
    paf.fulfiller->fulfill(kj::heap<TestTailForwarder>(carol.connect<Capability>("bob")));
    return carol;
  }();

  // Arrange that Dave tail-calls to Carol.
  auto& dave = [&]() -> TestContext::Vat& {
    auto paf = kj::newPromiseAndFulfiller<Capability::Client>();
    auto& dave = context.initVat("dave", kj::mv(paf.promise));
    paf.fulfiller->fulfill(kj::heap<TestTailForwarder>(dave.connect<Capability>("carol")));
    return dave;
  }();

  auto clientCap =
      arrangement == SELF_INTRODUCTION ? bob.connect<test::TestInterface>("carol", true)
    : arrangement == THREE_PARTY       ? alice.connect<test::TestInterface>("carol")
                                       : alice.connect<test::TestInterface>("dave");

  context.waitScope.poll();

  int callCount = 0;
  test::TestInterface::Client cap = kj::heap<TestInterfaceImpl>(callCount);

  // Arrange to block the Carol -> Bob connection immediately after the `Call` message for
  // `echo()` goes through. (Or Dave -> Carol if desired.)
  auto& connToBlock = arrangement == FOUR_PARTY_BLOCK_DAVE
      ? KJ_ASSERT_NONNULL(dave.vatNetwork.getConnectionTo(carol.vatNetwork))
      : KJ_ASSERT_NONNULL(carol.vatNetwork.getConnectionTo(bob.vatNetwork));

  connToBlock.blockAfter([](rpc::Message::Reader msg) {
    if (!msg.isCall()) return false;
    auto call = msg.getCall();
    return call.getInterfaceId() == capnp::typeId<test::TestMoreStuff>() &&
           call.getMethodId() == 6;
  });

  // Bounce our capability through the carol->bob path and immediately make a pipelined call on
  // it.
  kj::Promise<void> promise = nullptr;
  test::TestInterface::Client echoCap = nullptr;
  {
    auto moreStuff = clientCap.getTestMoreStuffRequest().send().getCap();

    // Echo `cap` through `echo()`.
    echoCap = [&]() {
      auto req = moreStuff.echoRequest();

      // HACK: echo() just reflects the cap back, but it was written to expect TestCallOrder
      //   specifically. We'll just pretend that's what we have, it doesn't matter.
      // TODO(cleanup): Change echo() to take `Capability` or maybe even be a generic.
      req.setCap(cap.castAs<test::TestCallOrder>());
      return req.send().getCap().castAs<test::TestInterface>();
    }();

    // Make a pipelined call.
    auto req = echoCap.fooRequest();
    req.setI(123);
    req.setJ(true);
    req.setExpectedCallCount(0);
    promise = req.send().ignoreResult();
  }

  // The pipelined call doesn't complete yet.
  KJ_EXPECT(!promise.poll(context.waitScope));

  // echoCap doesn't resolve yet.
  KJ_EXPECT(!echoCap.whenResolved().poll(context.waitScope));

  // A second call doesn't complete yet.
  auto promise2 = [&]() {
    auto req = echoCap.fooRequest();
    req.setI(123);
    req.setJ(true);
    req.setExpectedCallCount(1);
    return req.send();
  }();
  KJ_EXPECT(!promise2.poll(context.waitScope));

  if (disconnect) {
    connToBlock.disconnect(KJ_EXCEPTION(DISCONNECTED, "test disconnect"));
  } else {
    connToBlock.unblock();
  }

  if (disconnect) {
    kj::StringPtr expectedMessage = "Peer disconnected";
    if (arrangement == FOUR_PARTY_BLOCK_DAVE && forwardingEnabled) {
      expectedMessage = "Embargo failed because a connection along the call forwarding path";
    }

    KJ_EXPECT_THROW_MESSAGE(expectedMessage, echoCap.whenResolved().wait(context.waitScope));

    // TODO(someday): Unfortunately, `promise` does not resolve, becauese Carol already send a
    //   `Return` to Alice telling her to await a result from a third party, but Bob never received
    //   the message from Carol saying that he should take over the call. Hence, there's no way
    //   for Carol to communicate to Alice that the handoff failed. Should we add a special message
    //   for this?
    KJ_EXPECT(!promise.poll(context.waitScope));

    KJ_EXPECT_THROW_MESSAGE(expectedMessage, promise2.wait(context.waitScope));

    KJ_EXPECT(callCount == 0);
  } else {
    echoCap.whenResolved().wait(context.waitScope);
    promise.wait(context.waitScope);
    promise2.wait(context.waitScope);

    KJ_EXPECT(callCount == 2);
  }
}

KJ_TEST("three-party call handoff embargo") {
  doEmbargoTest(THREE_PARTY, false, false);
}

KJ_TEST("four-party call handoff embargo, no forwarding") {
  doEmbargoTest(FOUR_PARTY_BLOCK_CAROL, false, false);
}

KJ_TEST("four-party call handoff embargo, no forwarding, block dave") {
  doEmbargoTest(FOUR_PARTY_BLOCK_DAVE, false, false);
}

KJ_TEST("four-party call handoff embargo, with forwarding") {
  doEmbargoTest(FOUR_PARTY_BLOCK_CAROL, true, false);
}

KJ_TEST("four-party call handoff embargo, with forwarding, block dave") {
  doEmbargoTest(FOUR_PARTY_BLOCK_DAVE, true, false);
}

KJ_TEST("self-introduction call handoff embargo") {
  doEmbargoTest(SELF_INTRODUCTION, false, false);
}

KJ_TEST("three-party call handoff embargo disconnected") {
  doEmbargoTest(THREE_PARTY, false, true);
}

KJ_TEST("four-party call handoff embargo carol disconnected") {
  doEmbargoTest(FOUR_PARTY_BLOCK_CAROL, false, true);
}

KJ_TEST("four-party call handoff embargo carol disconnected, with forwarding") {
  doEmbargoTest(FOUR_PARTY_BLOCK_CAROL, true, true);
}

KJ_TEST("four-party call handoff embargo dave disconnected") {
  doEmbargoTest(FOUR_PARTY_BLOCK_DAVE, false, true);
}

KJ_TEST("four-party call handoff embargo dave disconnected, with forwarding") {
  doEmbargoTest(FOUR_PARTY_BLOCK_DAVE, true, true);
}

KJ_TEST("self-introduction call handoff embargo disconnected") {
  doEmbargoTest(SELF_INTRODUCTION, false, true);
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
