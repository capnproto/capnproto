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

#ifndef CAPNP_RPC_TWOPARTY_H_
#define CAPNP_RPC_TWOPARTY_H_

#include "rpc.h"
#include "message.h"
#include <kj/async-io.h>
#include <capnp/rpc-twoparty.capnp.h>

namespace capnp {

typedef VatNetwork<rpc::twoparty::SturdyRefHostId, rpc::twoparty::ProvisionId,
    rpc::twoparty::RecipientId, rpc::twoparty::ThirdPartyCapId, rpc::twoparty::JoinResult>
    TwoPartyVatNetworkBase;

class TwoPartyVatNetwork: public TwoPartyVatNetworkBase,
                          private TwoPartyVatNetworkBase::Connection {
  // A `VatNetwork` that consists of exactly two parties communicating over an arbitrary byte
  // stream.  This is used to implement the common case of a client/server network.
  //
  // See `ez-rpc.h` for a simple interface for setting up two-party clients and servers.
  // Use `TwoPartyVatNetwork` only if you need the advanced features.

public:
  TwoPartyVatNetwork(kj::AsyncIoStream& stream, rpc::twoparty::Side side,
                     ReaderOptions receiveOptions = ReaderOptions());

  kj::Promise<void> onDisconnect() { return disconnectPromise.addBranch(); }
  // Returns a promise that resolves when the peer disconnects.

  kj::Promise<void> onDrained() { return drainedPromise.addBranch(); }
  // Returns a promise that resolves once the peer has disconnected *and* all local objects
  // referencing this connection have been destroyed.  A caller might use this to decide when it
  // is safe to destroy the RpcSystem, if it isn't able to reliably destroy all objects using it
  // directly.

  // implements VatNetwork -----------------------------------------------------

  kj::Maybe<kj::Own<TwoPartyVatNetworkBase::Connection>> connectToRefHost(
      rpc::twoparty::SturdyRefHostId::Reader ref) override;
  kj::Promise<kj::Own<TwoPartyVatNetworkBase::Connection>> acceptConnectionAsRefHost() override;

private:
  class OutgoingMessageImpl;
  class IncomingMessageImpl;

  kj::AsyncIoStream& stream;
  rpc::twoparty::Side side;
  ReaderOptions receiveOptions;
  bool accepted = false;

  kj::Promise<void> previousWrite;
  // Resolves when the previous write completes.  This effectively serves as the write queue.

  kj::Own<kj::PromiseFulfiller<kj::Own<TwoPartyVatNetworkBase::Connection>>> acceptFulfiller;
  // Fulfiller for the promise returned by acceptConnectionAsRefHost() on the client side, or the
  // second call on the server side.  Never fulfilled, because there is only one connection.

  kj::ForkedPromise<void> disconnectPromise = nullptr;
  kj::Own<kj::PromiseFulfiller<void>> disconnectFulfiller;
  kj::ForkedPromise<void> drainedPromise = nullptr;

  class FulfillerDisposer: public kj::Disposer {
  public:
    mutable kj::Own<kj::PromiseFulfiller<void>> fulfiller;

    void disposeImpl(void* pointer) const override { fulfiller->fulfill(); }
  };
  FulfillerDisposer drainedFulfiller;

  // implements Connection -----------------------------------------------------

  kj::Own<OutgoingRpcMessage> newOutgoingMessage(uint firstSegmentWordSize) override;
  kj::Promise<kj::Maybe<kj::Own<IncomingRpcMessage>>> receiveIncomingMessage() override;
  void introduceTo(TwoPartyVatNetworkBase::Connection& recipient,
      rpc::twoparty::ThirdPartyCapId::Builder sendToRecipient,
      rpc::twoparty::RecipientId::Builder sendToTarget) override;
  ConnectionAndProvisionId connectToIntroduced(
      rpc::twoparty::ThirdPartyCapId::Reader capId) override;
  kj::Own<TwoPartyVatNetworkBase::Connection> acceptIntroducedConnection(
      rpc::twoparty::RecipientId::Reader recipientId) override;
};

}  // namespace capnp

#endif  // CAPNP_RPC_TWOPARTY_H_
