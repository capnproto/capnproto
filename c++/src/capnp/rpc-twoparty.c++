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

#include "rpc-twoparty.h"
#include "serialize-async.h"
#include <kj/debug.h>

namespace capnp {

TwoPartyVatNetwork::TwoPartyVatNetwork(kj::AsyncIoStream& stream, rpc::twoparty::Side side,
                                       ReaderOptions receiveOptions)
    : stream(stream), side(side), peerVatId(4),
      receiveOptions(receiveOptions), previousWrite(kj::READY_NOW) {
  peerVatId.initRoot<rpc::twoparty::VatId>().setSide(
      side == rpc::twoparty::Side::CLIENT ? rpc::twoparty::Side::SERVER
                                          : rpc::twoparty::Side::CLIENT);

  auto paf = kj::newPromiseAndFulfiller<void>();
  disconnectPromise = paf.promise.fork();
  disconnectFulfiller.fulfiller = kj::mv(paf.fulfiller);
}

void TwoPartyVatNetwork::FulfillerDisposer::disposeImpl(void* pointer) const {
  if (--refcount == 0) {
    fulfiller->fulfill();
  }
}

kj::Own<TwoPartyVatNetworkBase::Connection> TwoPartyVatNetwork::asConnection() {
  ++disconnectFulfiller.refcount;
  return kj::Own<TwoPartyVatNetworkBase::Connection>(this, disconnectFulfiller);
}

kj::Maybe<kj::Own<TwoPartyVatNetworkBase::Connection>> TwoPartyVatNetwork::connect(
    rpc::twoparty::VatId::Reader ref) {
  if (ref.getSide() == side) {
    return nullptr;
  } else {
    return asConnection();
  }
}

kj::Promise<kj::Own<TwoPartyVatNetworkBase::Connection>> TwoPartyVatNetwork::accept() {
  if (side == rpc::twoparty::Side::SERVER && !accepted) {
    accepted = true;
    return asConnection();
  } else {
    // Create a promise that will never be fulfilled.
    auto paf = kj::newPromiseAndFulfiller<kj::Own<TwoPartyVatNetworkBase::Connection>>();
    acceptFulfiller = kj::mv(paf.fulfiller);
    return kj::mv(paf.promise);
  }
}

class TwoPartyVatNetwork::OutgoingMessageImpl final
    : public OutgoingRpcMessage, public kj::Refcounted {
public:
  OutgoingMessageImpl(TwoPartyVatNetwork& network, uint firstSegmentWordSize)
      : network(network),
        message(firstSegmentWordSize == 0 ? SUGGESTED_FIRST_SEGMENT_WORDS : firstSegmentWordSize) {}

  AnyPointer::Builder getBody() override {
    return message.getRoot<AnyPointer>();
  }

  void send() override {
    size_t size = 0;
    for (auto& segment: message.getSegmentsForOutput()) {
      size += segment.size();
    }
    KJ_REQUIRE(size < ReaderOptions().traversalLimitInWords, size,
               "Trying to send Cap'n Proto message larger than the single-message size limit. The "
               "other side probably won't accept it and would abort the connection, so I won't "
               "send it.") {
      return;
    }

    network.previousWrite = KJ_ASSERT_NONNULL(network.previousWrite, "already shut down")
        .then([&]() {
      // Note that if the write fails, all further writes will be skipped due to the exception.
      // We never actually handle this exception because we assume the read end will fail as well
      // and it's cleaner to handle the failure there.
      return writeMessage(network.stream, message);
    }).attach(kj::addRef(*this))
      // Note that it's important that the eagerlyEvaluate() come *after* the attach() because
      // otherwise the message (and any capabilities in it) will not be released until a new
      // message is written! (Kenton once spent all afternoon tracking this down...)
      .eagerlyEvaluate(nullptr);
  }

private:
  TwoPartyVatNetwork& network;
  MallocMessageBuilder message;
};

class TwoPartyVatNetwork::IncomingMessageImpl final: public IncomingRpcMessage {
public:
  IncomingMessageImpl(kj::Own<MessageReader> message): message(kj::mv(message)) {}

  AnyPointer::Reader getBody() override {
    return message->getRoot<AnyPointer>();
  }

private:
  kj::Own<MessageReader> message;
};

rpc::twoparty::VatId::Reader TwoPartyVatNetwork::getPeerVatId() {
  return peerVatId.getRoot<rpc::twoparty::VatId>();
}

kj::Own<OutgoingRpcMessage> TwoPartyVatNetwork::newOutgoingMessage(uint firstSegmentWordSize) {
  return kj::refcounted<OutgoingMessageImpl>(*this, firstSegmentWordSize);
}

kj::Promise<kj::Maybe<kj::Own<IncomingRpcMessage>>> TwoPartyVatNetwork::receiveIncomingMessage() {
  return kj::evalLater([&]() {
    return tryReadMessage(stream, receiveOptions)
        .then([&](kj::Maybe<kj::Own<MessageReader>>&& message)
              -> kj::Maybe<kj::Own<IncomingRpcMessage>> {
      KJ_IF_MAYBE(m, message) {
        return kj::Own<IncomingRpcMessage>(kj::heap<IncomingMessageImpl>(kj::mv(*m)));
      } else {
        return nullptr;
      }
    });
  });
}

kj::Promise<void> TwoPartyVatNetwork::shutdown() {
  kj::Promise<void> result = KJ_ASSERT_NONNULL(previousWrite, "already shut down").then([this]() {
    stream.shutdownWrite();
  });
  previousWrite = nullptr;
  return kj::mv(result);
}

// =======================================================================================

TwoPartyServer::TwoPartyServer(Capability::Client bootstrapInterface)
    : bootstrapInterface(kj::mv(bootstrapInterface)), tasks(*this) {}

struct TwoPartyServer::AcceptedConnection {
  kj::Own<kj::AsyncIoStream> connection;
  TwoPartyVatNetwork network;
  RpcSystem<rpc::twoparty::VatId> rpcSystem;

  explicit AcceptedConnection(Capability::Client bootstrapInterface,
                              kj::Own<kj::AsyncIoStream>&& connectionParam)
      : connection(kj::mv(connectionParam)),
        network(*connection, rpc::twoparty::Side::SERVER),
        rpcSystem(makeRpcServer(network, kj::mv(bootstrapInterface))) {}
};

void TwoPartyServer::accept(kj::Own<kj::AsyncIoStream>&& connection) {
  auto connectionState = kj::heap<AcceptedConnection>(bootstrapInterface, kj::mv(connection));

  // Run the connection until disconnect.
  auto promise = connectionState->network.onDisconnect();
  tasks.add(promise.attach(kj::mv(connectionState)));
}

kj::Promise<void> TwoPartyServer::listen(kj::ConnectionReceiver& listener) {
  return listener.accept()
      .then([this,&listener](kj::Own<kj::AsyncIoStream>&& connection) mutable {
    accept(kj::mv(connection));
    return listen(listener);
  });
}

void TwoPartyServer::taskFailed(kj::Exception&& exception) {
  KJ_LOG(ERROR, exception);
}

TwoPartyClient::TwoPartyClient(kj::AsyncIoStream& connection)
    : network(connection, rpc::twoparty::Side::CLIENT),
      rpcSystem(makeRpcClient(network)) {}


TwoPartyClient::TwoPartyClient(kj::AsyncIoStream& connection,
                               Capability::Client bootstrapInterface,
                               rpc::twoparty::Side side)
    : network(connection, side),
      rpcSystem(network, bootstrapInterface) {}

Capability::Client TwoPartyClient::bootstrap() {
  MallocMessageBuilder message(4);
  auto vatId = message.getRoot<rpc::twoparty::VatId>();
  vatId.setSide(network.getSide() == rpc::twoparty::Side::CLIENT
                ? rpc::twoparty::Side::SERVER
                : rpc::twoparty::Side::CLIENT);
  return rpcSystem.bootstrap(vatId);
}

}  // namespace capnp
