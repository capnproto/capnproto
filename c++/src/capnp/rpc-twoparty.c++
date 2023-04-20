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
#include <kj/io.h>

namespace capnp {

TwoPartyVatNetwork::TwoPartyVatNetwork(
    kj::OneOf<MessageStream*, kj::Own<MessageStream>>&& stream,
    uint maxFdsPerMessage,
    rpc::twoparty::Side side,
    ReaderOptions receiveOptions,
    const kj::MonotonicClock& clock)

    : stream(kj::mv(stream)),
      maxFdsPerMessage(maxFdsPerMessage),
      side(side),
      peerVatId(4),
      receiveOptions(receiveOptions),
      previousWrite(kj::READY_NOW),
      clock(clock),
      currentOutgoingMessageSendTime(clock.now()) {
  peerVatId.initRoot<rpc::twoparty::VatId>().setSide(
      side == rpc::twoparty::Side::CLIENT ? rpc::twoparty::Side::SERVER
                                          : rpc::twoparty::Side::CLIENT);

  auto paf = kj::newPromiseAndFulfiller<void>();
  disconnectPromise = paf.promise.fork();
  disconnectFulfiller.fulfiller = kj::mv(paf.fulfiller);
}

TwoPartyVatNetwork::TwoPartyVatNetwork(capnp::MessageStream& stream,
                   rpc::twoparty::Side side, ReaderOptions receiveOptions,
                   const kj::MonotonicClock& clock)
  : TwoPartyVatNetwork(stream, 0, side, receiveOptions, clock) {}

TwoPartyVatNetwork::TwoPartyVatNetwork(
    capnp::MessageStream& stream,
    uint maxFdsPerMessage,
    rpc::twoparty::Side side,
    ReaderOptions receiveOptions,
    const kj::MonotonicClock& clock)
    : TwoPartyVatNetwork(&stream, maxFdsPerMessage, side, receiveOptions, clock) {}

TwoPartyVatNetwork::TwoPartyVatNetwork(kj::AsyncIoStream& stream, rpc::twoparty::Side side,
                                       ReaderOptions receiveOptions,
                                       const kj::MonotonicClock& clock)
    : TwoPartyVatNetwork(
          kj::Own<MessageStream>(kj::heap<BufferedMessageStream>(
              stream, IncomingRpcMessage::getShortLivedCallback())),
          0, side, receiveOptions, clock) {}

TwoPartyVatNetwork::TwoPartyVatNetwork(kj::AsyncCapabilityStream& stream, uint maxFdsPerMessage,
                                       rpc::twoparty::Side side, ReaderOptions receiveOptions,
                                       const kj::MonotonicClock& clock)
    : TwoPartyVatNetwork(
          kj::Own<MessageStream>(kj::heap<BufferedMessageStream>(
              stream, IncomingRpcMessage::getShortLivedCallback())),
          maxFdsPerMessage, side, receiveOptions, clock) {}

TwoPartyVatNetwork::~TwoPartyVatNetwork() noexcept(false) {};

MessageStream& TwoPartyVatNetwork::getStream() {
  KJ_SWITCH_ONEOF(stream) {
    KJ_CASE_ONEOF(s, MessageStream*) {
      return *s;
    }
    KJ_CASE_ONEOF(s, kj::Own<MessageStream>) {
      return *s;
    }
  }
  KJ_UNREACHABLE;
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

  void setFds(kj::Array<int> fds) override {
    if (network.maxFdsPerMessage > 0) {
      this->fds = kj::mv(fds);
    }
  }

  void send() override {
    size_t size = 0;
    for (auto& segment: message.getSegmentsForOutput()) {
      size += segment.size();
    }
    KJ_REQUIRE(size < network.receiveOptions.traversalLimitInWords, size,
               "Trying to send Cap'n Proto message larger than our single-message size limit. The "
               "other side probably won't accept it (assuming its traversalLimitInWords matches "
               "ours) and would abort the connection, so I won't send it.") {
      return;
    }

    auto sendTime = network.clock.now();
    if (network.queuedMessages.size() == 0) {
      // Optimistically set sendTime when there's no messages in the queue. Without this, sending
      // a message after a long delay could cause getOutgoingMessageWaitTime() to return excessively
      // long wait times if it is called during the time period after send() is called,
      // but before the write occurs, as we increment currentQueueCount synchronously, but
      // asynchronously update currentOutgoingMessageSendTime.
      network.currentOutgoingMessageSendTime = sendTime;
    }

    // Instead of sending each new message as soon as possible, we attempt to batch together small
    // messages by delaying when we send them using evalLast. This allows us to group together
    // related small messages, reducing the number of syscalls we make.
    auto& previousWrite = KJ_ASSERT_NONNULL(network.previousWrite, "already shut down");
    bool alreadyPendingSend = !network.queuedMessages.empty();
    network.currentQueueSize += message.sizeInWords() * sizeof(word);
    network.queuedMessages.add(kj::addRef(*this));
    if (alreadyPendingSend) {
      // The first send sets up an evalLast that will clear out pendingMessages when it's sent.
      // If pendingMessages is non-empty, then there must already be a callback waiting to send
      // them.
      return;
    }

    // On the other hand, if pendingMessages was empty, then we should set up the delayed write.
    network.previousWrite = previousWrite.then([this, sendTime]() {
      return kj::evalLast([this, sendTime]() -> kj::Promise<void> {
        network.currentOutgoingMessageSendTime = sendTime;
        // Swap out the connection's pending messages and write all of them together.
        auto ownMessages = kj::mv(network.queuedMessages);
        network.currentQueueSize = 0;
        auto messages =
          kj::heapArray<MessageAndFds>(ownMessages.size());
        for (int i = 0; i < messages.size(); ++i) {
          messages[i].segments = ownMessages[i]->message.getSegmentsForOutput();
          messages[i].fds = ownMessages[i]->fds;
        }
        return network.getStream().writeMessages(messages).attach(kj::mv(ownMessages), kj::mv(messages));
      }).catch_([this](kj::Exception&& e) {
        // Since no one checks write failures, we need to propagate them into read failures,
        // otherwise we might get stuck sending all messages into a black hole and wondering why
        // the peer never replies.
        network.readCancelReason = kj::cp(e);
        if (!network.readCanceler.isEmpty()) {
          network.readCanceler.cancel(kj::cp(e));
        }
        kj::throwRecoverableException(kj::mv(e));
      });
    }).attach(kj::addRef(*this))
      // Note that it's important that the eagerlyEvaluate() come *after* the attach() because
      // otherwise the message (and any capabilities in it) will not be released until a new
      // message is written! (Kenton once spent all afternoon tracking this down...)
      .eagerlyEvaluate(nullptr);
  }

  size_t sizeInWords() override {
    return message.sizeInWords();
  }

private:
  TwoPartyVatNetwork& network;
  MallocMessageBuilder message;
  kj::Array<int> fds;
};

kj::Duration TwoPartyVatNetwork::getOutgoingMessageWaitTime() {
  if (queuedMessages.size() > 0) {
    return clock.now() - currentOutgoingMessageSendTime;
  } else {
    return 0 * kj::SECONDS;
  }
}

class TwoPartyVatNetwork::IncomingMessageImpl final: public IncomingRpcMessage {
public:
  IncomingMessageImpl(kj::Own<MessageReader> message): message(kj::mv(message)) {}

  IncomingMessageImpl(MessageReaderAndFds init, kj::Array<kj::AutoCloseFd> fdSpace)
      : message(kj::mv(init.reader)),
        fdSpace(kj::mv(fdSpace)),
        fds(init.fds) {
    KJ_DASSERT(this->fds.begin() == this->fdSpace.begin());
  }

  AnyPointer::Reader getBody() override {
    return message->getRoot<AnyPointer>();
  }

  kj::ArrayPtr<kj::AutoCloseFd> getAttachedFds() override {
    return fds;
  }

  size_t sizeInWords() override {
    return message->sizeInWords();
  }

private:
  kj::Own<MessageReader> message;
  kj::Array<kj::AutoCloseFd> fdSpace;
  kj::ArrayPtr<kj::AutoCloseFd> fds;
};

kj::Own<RpcFlowController> TwoPartyVatNetwork::newStream() {
  return RpcFlowController::newVariableWindowController(*this);
}

size_t TwoPartyVatNetwork::getWindow() {
  // The socket's send buffer size -- as returned by getsockopt(SO_SNDBUF) -- tells us how much
  // data the kernel itself is willing to buffer. The kernel will increase the send buffer size if
  // needed to fill the connection's congestion window. So we can cheat and use it as our stream
  // window, too, to make sure we saturate said congestion window.
  //
  // TODO(perf): Unfortunately, this hack breaks down in the presence of proxying. What we really
  //   want is the window all the way to the endpoint, which could cross multiple connections. The
  //   first-hop window could be either too big or too small: it's too big if the first hop has
  //   much higher bandwidth than the full path (causing buffering at the bottleneck), and it's
  //   too small if the first hop has much lower latency than the full path (causing not enough
  //   data to be sent to saturate the connection). To handle this, we could either:
  //   1. Have proxies be aware of streaming, by flagging streaming calls in the RPC protocol. The
  //      proxies would then handle backpressure at each hop. This seems simple to implement but
  //      requires base RPC protocol changes and might require thinking carefully about e-ordering
  //      implications. Also, it only fixes underutilization; it does not fix buffer bloat.
  //   2. Do our own BBR-like computation, where the client measures the end-to-end latency and
  //      bandwidth based on the observed sends and returns, and then compute the window based on
  //      that. This seems complicated, but avoids the need for any changes to the RPC protocol.
  //      In theory it solves both underutilization and buffer bloat. Note that this approach would
  //      require the RPC system to use a clock, which feels dirty and adds non-determinism.

  if (solSndbufUnimplemented) {
    return RpcFlowController::DEFAULT_WINDOW_SIZE;
  } else {
    KJ_IF_MAYBE(bufSize, getStream().getSendBufferSize()) {
      return *bufSize;
    } else {
      solSndbufUnimplemented = true;
      return RpcFlowController::DEFAULT_WINDOW_SIZE;
    }
  }
}

rpc::twoparty::VatId::Reader TwoPartyVatNetwork::getPeerVatId() {
  return peerVatId.getRoot<rpc::twoparty::VatId>();
}

kj::Own<OutgoingRpcMessage> TwoPartyVatNetwork::newOutgoingMessage(uint firstSegmentWordSize) {
  return kj::refcounted<OutgoingMessageImpl>(*this, firstSegmentWordSize);
}

kj::Promise<kj::Maybe<kj::Own<IncomingRpcMessage>>> TwoPartyVatNetwork::receiveIncomingMessage() {
  return kj::evalLater([this]() -> kj::Promise<kj::Maybe<kj::Own<IncomingRpcMessage>>> {
    KJ_IF_MAYBE(e, readCancelReason) {
      // A previous write failed; propagate the failure to reads, too.
      return kj::cp(*e);
    }

    kj::Array<kj::AutoCloseFd> fdSpace = nullptr;
    if(maxFdsPerMessage > 0) {
      fdSpace = kj::heapArray<kj::AutoCloseFd>(maxFdsPerMessage);
    }
    auto promise = readCanceler.wrap(getStream().tryReadMessage(fdSpace, receiveOptions));
    return promise.then([fdSpace = kj::mv(fdSpace)]
                        (kj::Maybe<MessageReaderAndFds>&& messageAndFds) mutable
                      -> kj::Maybe<kj::Own<IncomingRpcMessage>> {
      KJ_IF_MAYBE(m, messageAndFds) {
        if (m->fds.size() > 0) {
          return kj::Own<IncomingRpcMessage>(
              kj::heap<IncomingMessageImpl>(kj::mv(*m), kj::mv(fdSpace)));
        } else {
          return kj::Own<IncomingRpcMessage>(kj::heap<IncomingMessageImpl>(kj::mv(m->reader)));
        }
      } else {
        return nullptr;
      }
    });
  });
}

kj::Promise<void> TwoPartyVatNetwork::shutdown() {
  kj::Promise<void> result = KJ_ASSERT_NONNULL(previousWrite, "already shut down").then([this]() {
    return getStream().end();
  });
  previousWrite = nullptr;
  return kj::mv(result);
}

// =======================================================================================

TwoPartyServer::TwoPartyServer(Capability::Client bootstrapInterface,
    kj::Maybe<kj::Function<kj::String(const kj::Exception&)>> traceEncoder)
    : bootstrapInterface(kj::mv(bootstrapInterface)),
      traceEncoder(kj::mv(traceEncoder)),
      tasks(*this) {}

struct TwoPartyServer::AcceptedConnection {
  kj::Own<kj::AsyncIoStream> connection;
  TwoPartyVatNetwork network;
  RpcSystem<rpc::twoparty::VatId> rpcSystem;

  explicit AcceptedConnection(TwoPartyServer& parent,
                              kj::Own<kj::AsyncIoStream>&& connectionParam)
      : connection(kj::mv(connectionParam)),
        network(*connection, rpc::twoparty::Side::SERVER),
        rpcSystem(makeRpcServer(network, kj::cp(parent.bootstrapInterface))) {
    init(parent);
  }

  explicit AcceptedConnection(TwoPartyServer& parent,
                              kj::Own<kj::AsyncCapabilityStream>&& connectionParam,
                              uint maxFdsPerMessage)
      : connection(kj::mv(connectionParam)),
        network(kj::downcast<kj::AsyncCapabilityStream>(*connection),
                maxFdsPerMessage, rpc::twoparty::Side::SERVER),
        rpcSystem(makeRpcServer(network, kj::cp(parent.bootstrapInterface))) {
    init(parent);
  }

  void init(TwoPartyServer& parent) {
    KJ_IF_MAYBE(t, parent.traceEncoder) {
      rpcSystem.setTraceEncoder([&func = *t](const kj::Exception& e) {
        return func(e);
      });
    }
  }
};

void TwoPartyServer::accept(kj::Own<kj::AsyncIoStream>&& connection) {
  auto connectionState = kj::heap<AcceptedConnection>(*this, kj::mv(connection));

  // Run the connection until disconnect.
  auto promise = connectionState->network.onDisconnect();
  tasks.add(promise.attach(kj::mv(connectionState)));
}

void TwoPartyServer::accept(
    kj::Own<kj::AsyncCapabilityStream>&& connection, uint maxFdsPerMessage) {
  auto connectionState = kj::heap<AcceptedConnection>(
      *this, kj::mv(connection), maxFdsPerMessage);

  // Run the connection until disconnect.
  auto promise = connectionState->network.onDisconnect();
  tasks.add(promise.attach(kj::mv(connectionState)));
}

kj::Promise<void> TwoPartyServer::accept(kj::AsyncIoStream& connection) {
  auto connectionState = kj::heap<AcceptedConnection>(*this,
      kj::Own<kj::AsyncIoStream>(&connection, kj::NullDisposer::instance));

  // Run the connection until disconnect.
  auto promise = connectionState->network.onDisconnect();
  return promise.attach(kj::mv(connectionState));
}

kj::Promise<void> TwoPartyServer::accept(
    kj::AsyncCapabilityStream& connection, uint maxFdsPerMessage) {
  auto connectionState = kj::heap<AcceptedConnection>(*this,
      kj::Own<kj::AsyncCapabilityStream>(&connection, kj::NullDisposer::instance),
      maxFdsPerMessage);

  // Run the connection until disconnect.
  auto promise = connectionState->network.onDisconnect();
  return promise.attach(kj::mv(connectionState));
}

kj::Promise<void> TwoPartyServer::listen(kj::ConnectionReceiver& listener) {
  return listener.accept()
      .then([this,&listener](kj::Own<kj::AsyncIoStream>&& connection) mutable {
    accept(kj::mv(connection));
    return listen(listener);
  });
}

kj::Promise<void> TwoPartyServer::listenCapStreamReceiver(
      kj::ConnectionReceiver& listener, uint maxFdsPerMessage) {
  return listener.accept()
      .then([this,&listener,maxFdsPerMessage](kj::Own<kj::AsyncIoStream>&& connection) mutable {
    accept(connection.downcast<kj::AsyncCapabilityStream>(), maxFdsPerMessage);
    return listenCapStreamReceiver(listener, maxFdsPerMessage);
  });
}

void TwoPartyServer::taskFailed(kj::Exception&& exception) {
  KJ_LOG(ERROR, exception);
}

TwoPartyClient::TwoPartyClient(kj::AsyncIoStream& connection)
    : network(connection, rpc::twoparty::Side::CLIENT),
      rpcSystem(makeRpcClient(network)) {}


TwoPartyClient::TwoPartyClient(kj::AsyncCapabilityStream& connection, uint maxFdsPerMessage)
    : network(connection, maxFdsPerMessage, rpc::twoparty::Side::CLIENT),
      rpcSystem(makeRpcClient(network)) {}

TwoPartyClient::TwoPartyClient(kj::AsyncIoStream& connection,
                               Capability::Client bootstrapInterface,
                               rpc::twoparty::Side side)
    : network(connection, side),
      rpcSystem(network, bootstrapInterface) {}

TwoPartyClient::TwoPartyClient(kj::AsyncCapabilityStream& connection, uint maxFdsPerMessage,
                               Capability::Client bootstrapInterface,
                               rpc::twoparty::Side side)
    : network(connection, maxFdsPerMessage, side),
      rpcSystem(network, bootstrapInterface) {}

Capability::Client TwoPartyClient::bootstrap() {
  capnp::word scratch[4];
  memset(&scratch, 0, sizeof(scratch));
  capnp::MallocMessageBuilder message(scratch);
  auto vatId = message.getRoot<rpc::twoparty::VatId>();
  vatId.setSide(network.getSide() == rpc::twoparty::Side::CLIENT
                ? rpc::twoparty::Side::SERVER
                : rpc::twoparty::Side::CLIENT);
  return rpcSystem.bootstrap(vatId);
}

void TwoPartyClient::setTraceEncoder(kj::Function<kj::String(const kj::Exception&)> func) {
  rpcSystem.setTraceEncoder(kj::mv(func));
}

}  // namespace capnp
