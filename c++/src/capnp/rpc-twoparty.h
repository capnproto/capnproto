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

#ifndef CAPNP_RPC_TWOPARTY_H_
#define CAPNP_RPC_TWOPARTY_H_

#if defined(__GNUC__) && !CAPNP_HEADER_WARNINGS
#pragma GCC system_header
#endif

#include "rpc.h"
#include "message.h"
#include <kj/async-io.h>
#include <capnp/rpc-twoparty.capnp.h>

namespace capnp {

namespace rpc {
  namespace twoparty {
    typedef VatId SturdyRefHostId;  // For backwards-compatibility with version 0.4.
  }
}

typedef VatNetwork<rpc::twoparty::VatId, rpc::twoparty::ProvisionId,
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

  // implements VatNetwork -----------------------------------------------------

  kj::Maybe<kj::Own<TwoPartyVatNetworkBase::Connection>> connect(
      rpc::twoparty::VatId::Reader ref) override;
  kj::Promise<kj::Own<TwoPartyVatNetworkBase::Connection>> accept() override;

private:
  class OutgoingMessageImpl;
  class IncomingMessageImpl;

  kj::AsyncIoStream& stream;
  rpc::twoparty::Side side;
  ReaderOptions receiveOptions;
  bool accepted = false;

  kj::Maybe<kj::Promise<void>> previousWrite;
  // Resolves when the previous write completes.  This effectively serves as the write queue.
  // Becomes null when shutdown() is called.

  kj::Own<kj::PromiseFulfiller<kj::Own<TwoPartyVatNetworkBase::Connection>>> acceptFulfiller;
  // Fulfiller for the promise returned by acceptConnectionAsRefHost() on the client side, or the
  // second call on the server side.  Never fulfilled, because there is only one connection.

  kj::ForkedPromise<void> disconnectPromise = nullptr;

  class FulfillerDisposer: public kj::Disposer {
    // Hack:  TwoPartyVatNetwork is both a VatNetwork and a VatNetwork::Connection.  Whet the RPC
    //   system detects (or initiates) a disconnection, it drops its reference to the Connection.
    //   When all references have been dropped, then we want onDrained() to fire.  So we hand out
    //   Own<Connection>s with this disposer attached, so that we can detect when they are dropped.

  public:
    mutable kj::Own<kj::PromiseFulfiller<void>> fulfiller;
    mutable uint refcount = 0;

    void disposeImpl(void* pointer) const override;
  };
  FulfillerDisposer disconnectFulfiller;

  kj::Own<TwoPartyVatNetworkBase::Connection> asConnection();
  // Returns a pointer to this with the disposer set to drainedFulfiller.

  // implements Connection -----------------------------------------------------

  kj::Own<OutgoingRpcMessage> newOutgoingMessage(uint firstSegmentWordSize) override;
  kj::Promise<kj::Maybe<kj::Own<IncomingRpcMessage>>> receiveIncomingMessage() override;
  kj::Promise<void> shutdown() override;
};

}  // namespace capnp

#endif  // CAPNP_RPC_TWOPARTY_H_
