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

// This file contains a bunch of internal declarations that must appear before rpc.h can start.
// We don't define these directly in rpc.h because it makes the file hard to read.

#ifndef CAPNP_RPC_PRELUDE_H_
#define CAPNP_RPC_PRELUDE_H_

#include "capability.h"

namespace capnp {

class OutgoingRpcMessage;
class IncomingRpcMessage;

template <typename SturdyRefHostId>
class RpcSystem;

namespace _ {  // private

class VatNetworkBase {
  // Non-template version of VatNetwork.  Ignore this class; see VatNetwork, below.

public:
  class Connection;

  struct ConnectionAndProvisionId {
    kj::Own<Connection> connection;
    kj::Own<OutgoingRpcMessage> firstMessage;
    Orphan<AnyPointer> provisionId;
  };

  class Connection {
  public:
    virtual kj::Own<OutgoingRpcMessage> newOutgoingMessage(uint firstSegmentWordSize) = 0;
    virtual kj::Promise<kj::Maybe<kj::Own<IncomingRpcMessage>>> receiveIncomingMessage() = 0;
    virtual void baseIntroduceTo(Connection& recipient,
        AnyPointer::Builder sendToRecipient, AnyPointer::Builder sendToTarget) = 0;
    virtual ConnectionAndProvisionId baseConnectToIntroduced(AnyPointer::Reader capId) = 0;
    virtual kj::Own<Connection> baseAcceptIntroducedConnection(AnyPointer::Reader recipientId) = 0;
  };
  virtual kj::Maybe<kj::Own<Connection>> baseConnectToRefHost(_::StructReader hostId) = 0;
  virtual kj::Promise<kj::Own<Connection>> baseAcceptConnectionAsRefHost() = 0;
};

class SturdyRefRestorerBase {
public:
  virtual Capability::Client baseRestore(AnyPointer::Reader ref) = 0;
};

class RpcSystemBase {
public:
  RpcSystemBase(VatNetworkBase& network, kj::Maybe<SturdyRefRestorerBase&> restorer);
  RpcSystemBase(RpcSystemBase&& other) noexcept;
  ~RpcSystemBase() noexcept(false);

private:
  class Impl;
  kj::Own<Impl> impl;

  Capability::Client baseRestore(_::StructReader hostId, AnyPointer::Reader objectId);
  // TODO(someday):  Maybe define a public API called `TypelessStruct` so we don't have to rely
  // on `_::StructReader` here?

  template <typename>
  friend class capnp::RpcSystem;
};

}  // namespace _ (private)
}  // namespace capnp

#endif  // CAPNP_RPC_PRELUDE_H_
