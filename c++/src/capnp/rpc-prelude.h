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
