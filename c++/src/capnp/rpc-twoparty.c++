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

// Includes just for need SOL_SOCKET and SO_SNDBUF
#if _WIN32
#define WIN32_LEAN_AND_MEAN  // ::eyeroll::
#include <winsock2.h>
#include <mswsock.h>
#include <kj/windows-sanity.h>
#else
#include <sys/socket.h>
#endif

namespace capnp {

TwoPartyVatNetwork::TwoPartyVatNetwork(kj::AsyncIoStream& stream, rpc::twoparty::Side side,
                                       ReaderOptions receiveOptions)
    : stream(&stream), maxFdsPerMessage(0), side(side), peerVatId(4),
      receiveOptions(receiveOptions), previousWrite(kj::READY_NOW) {
  peerVatId.initRoot<rpc::twoparty::VatId>().setSide(
      side == rpc::twoparty::Side::CLIENT ? rpc::twoparty::Side::SERVER
                                          : rpc::twoparty::Side::CLIENT);

  auto paf = kj::newPromiseAndFulfiller<void>();
  disconnectPromise = paf.promise.fork();
  disconnectFulfiller.fulfiller = kj::mv(paf.fulfiller);
}

TwoPartyVatNetwork::TwoPartyVatNetwork(kj::AsyncCapabilityStream& stream, uint maxFdsPerMessage,
                                       rpc::twoparty::Side side, ReaderOptions receiveOptions)
    : TwoPartyVatNetwork(stream, side, receiveOptions) {
  this->stream = &stream;
  this->maxFdsPerMessage = maxFdsPerMessage;
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
    if (network.stream.is<kj::AsyncCapabilityStream*>()) {
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

    network.previousWrite = KJ_ASSERT_NONNULL(network.previousWrite, "already shut down")
        .then([&]() {
      // Note that if the write fails, all further writes will be skipped due to the exception.
      // We never actually handle this exception because we assume the read end will fail as well
      // and it's cleaner to handle the failure there.
      KJ_SWITCH_ONEOF(network.stream) {
        KJ_CASE_ONEOF(ioStream, kj::AsyncIoStream*) {
          return writeMessage(*ioStream, message);
        }
        KJ_CASE_ONEOF(capStream, kj::AsyncCapabilityStream*) {
          return writeMessage(*capStream, fds, message);
        }
      }
      KJ_UNREACHABLE;
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
    // TODO(perf): It might be nice to have a tryGetsockopt() that doesn't require catching
    //   exceptions?
    int bufSize = 0;
    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
      uint len = sizeof(int);
      KJ_SWITCH_ONEOF(stream) {
        KJ_CASE_ONEOF(s, kj::AsyncIoStream*) {
          s->getsockopt(SOL_SOCKET, SO_SNDBUF, &bufSize, &len);
        }
        KJ_CASE_ONEOF(s, kj::AsyncCapabilityStream*) {
          s->getsockopt(SOL_SOCKET, SO_SNDBUF, &bufSize, &len);
        }
      }
      KJ_ASSERT(len == sizeof(bufSize));
    })) {
      if (exception->getType() != kj::Exception::Type::UNIMPLEMENTED) {
        // TODO(someday): Figure out why getting SO_SNDBUF sometimes throws EINVAL. I suspect it
        //   happens when the remote side has closed their read end, meaning we no longer have
        //   a send buffer, but I don't know what is the best way to verify that that was actually
        //   the reason. I'd prefer not to ignore EINVAL errors in general.

        // kj::throwRecoverableException(kj::mv(*exception));
      }
      solSndbufUnimplemented = true;
      bufSize = RpcFlowController::DEFAULT_WINDOW_SIZE;
    }
    return bufSize;
  }
}

rpc::twoparty::VatId::Reader TwoPartyVatNetwork::getPeerVatId() {
  return peerVatId.getRoot<rpc::twoparty::VatId>();
}

kj::Own<OutgoingRpcMessage> TwoPartyVatNetwork::newOutgoingMessage(uint firstSegmentWordSize) {
  return kj::refcounted<OutgoingMessageImpl>(*this, firstSegmentWordSize);
}

kj::Promise<kj::Maybe<kj::Own<IncomingRpcMessage>>> TwoPartyVatNetwork::receiveIncomingMessage() {
  return kj::evalLater([this]() {
    KJ_SWITCH_ONEOF(stream) {
      KJ_CASE_ONEOF(ioStream, kj::AsyncIoStream*) {
        return tryReadMessage(*ioStream, receiveOptions)
            .then([](kj::Maybe<kj::Own<MessageReader>>&& message)
                  -> kj::Maybe<kj::Own<IncomingRpcMessage>> {
          KJ_IF_MAYBE(m, message) {
            return kj::Own<IncomingRpcMessage>(kj::heap<IncomingMessageImpl>(kj::mv(*m)));
          } else {
            return nullptr;
          }
        });
      }
      KJ_CASE_ONEOF(capStream, kj::AsyncCapabilityStream*) {
        auto fdSpace = kj::heapArray<kj::AutoCloseFd>(maxFdsPerMessage);
        auto promise = tryReadMessage(*capStream, fdSpace, receiveOptions);
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
      }
    }
    KJ_UNREACHABLE;
  });
}

kj::Promise<void> TwoPartyVatNetwork::shutdown() {
  kj::Promise<void> result = KJ_ASSERT_NONNULL(previousWrite, "already shut down").then([this]() {
    KJ_SWITCH_ONEOF(stream) {
      KJ_CASE_ONEOF(ioStream, kj::AsyncIoStream*) {
        ioStream->shutdownWrite();
      }
      KJ_CASE_ONEOF(capStream, kj::AsyncCapabilityStream*) {
        capStream->shutdownWrite();
      }
    }
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

  explicit AcceptedConnection(Capability::Client bootstrapInterface,
                              kj::Own<kj::AsyncCapabilityStream>&& connectionParam,
                              uint maxFdsPerMessage)
      : connection(kj::mv(connectionParam)),
        network(kj::downcast<kj::AsyncCapabilityStream>(*connection),
                maxFdsPerMessage, rpc::twoparty::Side::SERVER),
        rpcSystem(makeRpcServer(network, kj::mv(bootstrapInterface))) {}
};

void TwoPartyServer::accept(kj::Own<kj::AsyncIoStream>&& connection) {
  auto connectionState = kj::heap<AcceptedConnection>(bootstrapInterface, kj::mv(connection));

  // Run the connection until disconnect.
  auto promise = connectionState->network.onDisconnect();
  tasks.add(promise.attach(kj::mv(connectionState)));
}

void TwoPartyServer::accept(
    kj::Own<kj::AsyncCapabilityStream>&& connection, uint maxFdsPerMessage) {
  auto connectionState = kj::heap<AcceptedConnection>(
      bootstrapInterface, kj::mv(connection), maxFdsPerMessage);

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

}  // namespace capnp
