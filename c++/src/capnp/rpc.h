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

#ifndef CAPNP_RPC_H_
#define CAPNP_RPC_H_

#include "capability.h"
#include "rpc-prelude.h"

namespace capnp {

template <typename SturdyRefHostId, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
class VatNetwork;
template <typename SturdyRefObjectId>
class SturdyRefRestorer;

template <typename SturdyRefHostId>
class RpcSystem: public _::RpcSystemBase {
  // Represents the RPC system, which is the portal to objects available on the network.
  //
  // The RPC implementation sits on top of an implementation of `VatNetwork`.  The `VatNetwork`
  // determines how to form connections between vats -- specifically, two-way, private, reliable,
  // sequenced datagram connections.  The RPC implementation determines how to use such connections
  // to manage object references and make method calls.
  //
  // See `makeRpcServer()` and `makeRpcClient()` below for convenient syntax for setting up an
  // `RpcSystem` given a `VatNetwork`.
  //
  // See `ez-rpc.h` for an even simpler interface for setting up RPC in a typical two-party
  // client/server scenario.

public:
  template <typename ProvisionId, typename RecipientId,
            typename ThirdPartyCapId, typename JoinResult,
            typename LocalSturdyRefObjectId>
  RpcSystem(
      VatNetwork<SturdyRefHostId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
      kj::Maybe<SturdyRefRestorer<LocalSturdyRefObjectId>&> restorer);
  RpcSystem(RpcSystem&& other) = default;

  Capability::Client restore(typename SturdyRefHostId::Reader hostId, AnyPointer::Reader objectId);
  // Restore the given SturdyRef from the network and return the capability representing it.
  //
  // `hostId` identifies the host from which to request the ref, in the format specified by the
  // `VatNetwork` in use.  `objectId` is the object ID in whatever format is expected by said host.
};

template <typename SturdyRefHostId, typename LocalSturdyRefObjectId,
          typename ProvisionId, typename RecipientId, typename ThirdPartyCapId, typename JoinResult>
RpcSystem<SturdyRefHostId> makeRpcServer(
    VatNetwork<SturdyRefHostId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
    SturdyRefRestorer<LocalSturdyRefObjectId>& restorer);
// Make an RPC server.  Typical usage (e.g. in a main() function):
//
//    MyEventLoop eventLoop;
//    kj::WaitScope waitScope(eventLoop);
//    MyNetwork network;
//    MyRestorer restorer;
//    auto server = makeRpcServer(network, restorer);
//    kj::NEVER_DONE.wait(waitScope);  // run forever
//
// See also ez-rpc.h, which has simpler instructions for the common case of a two-party
// client-server RPC connection.

template <typename SturdyRefHostId, typename ProvisionId,
          typename RecipientId, typename ThirdPartyCapId, typename JoinResult>
RpcSystem<SturdyRefHostId> makeRpcClient(
    VatNetwork<SturdyRefHostId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network);
// Make an RPC client.  Typical usage (e.g. in a main() function):
//
//    MyEventLoop eventLoop;
//    kj::WaitScope waitScope(eventLoop);
//    MyNetwork network;
//    auto client = makeRpcClient(network);
//    MyCapability::Client cap = client.restore(hostId, objId).castAs<MyCapability>();
//    auto response = cap.fooRequest().send().wait(waitScope);
//    handleMyResponse(response);
//
// See also ez-rpc.h, which has simpler instructions for the common case of a two-party
// client-server RPC connection.

template <typename SturdyRefObjectId>
class SturdyRefRestorer: public _::SturdyRefRestorerBase {
  // Applications that can restore SturdyRefs must implement this interface and provide it to the
  // RpcSystem.
  //
  // Hint:  Use SturdyRefRestorer<capnp::Text> to define a server that exports services under
  //   string names.

public:
  virtual Capability::Client restore(typename SturdyRefObjectId::Reader ref) = 0;
  // Restore the given object, returning a capability representing it.

private:
  Capability::Client baseRestore(AnyPointer::Reader ref) override final;
};

// =======================================================================================
// VatNetwork

class OutgoingRpcMessage {
  // A message to be sent by a `VatNetwork`.

public:
  virtual AnyPointer::Builder getBody() = 0;
  // Get the message body, which the caller may fill in any way it wants.  (The standard RPC
  // implementation initializes it as a Message as defined in rpc.capnp.)

  virtual kj::ArrayPtr<kj::Maybe<kj::Own<ClientHook>>> getCapTable() = 0;
  // Calls getCapTable() on the underlying MessageBuilder.

  virtual void send() = 0;
  // Send the message, or at least put it in a queue to be sent later.  Note that the builder
  // returned by `getBody()` remains valid at least until the `OutgoingRpcMessage` is destroyed.
};

class IncomingRpcMessage {
  // A message received from a `VatNetwork`.

public:
  virtual AnyPointer::Reader getBody() = 0;
  // Get the message body, to be interpreted by the caller.  (The standard RPC implementation
  // interprets it as a Message as defined in rpc.capnp.)

  virtual void initCapTable(kj::Array<kj::Maybe<kj::Own<ClientHook>>>&& capTable) = 0;
  // Calls initCapTable() on the underlying MessageReader.
};

template <typename SturdyRefHostId, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
class VatNetwork: public _::VatNetworkBase {
  // Cap'n Proto RPC operates between vats, where a "vat" is some sort of host of objects.
  // Typically one Cap'n Proto process (in the Unix sense) is one vat.  The RPC system is what
  // allows calls between objects hosted in different vats.
  //
  // The RPC implementation sits on top of an implementation of `VatNetwork`.  The `VatNetwork`
  // determines how to form connections between vats -- specifically, two-way, private, reliable,
  // sequenced datagram connections.  The RPC implementation determines how to use such connections
  // to manage object references and make method calls.
  //
  // The most common implementation of VatNetwork is TwoPartyVatNetwork (rpc-twoparty.h).  Most
  // simple client-server apps will want to use it.  (You may even want to use the EZ RPC
  // interfaces in `ez-rpc.h` and avoid all of this.)
  //
  // TODO(someday):  Provide a standard implementation for the public internet.

public:
  class Connection;

  struct ConnectionAndProvisionId {
    // Result of connecting to a vat introduced by another vat.

    kj::Own<Connection> connection;
    // Connection to the new vat.

    kj::Own<OutgoingRpcMessage> firstMessage;
    // An already-allocated `OutgoingRpcMessage` associated with `connection`.  The RPC system will
    // construct this as an `Accept` message and send it.

    Orphan<ProvisionId> provisionId;
    // A `ProvisionId` already allocated inside `firstMessage`, which the RPC system will use to
    // build the `Accept` message.
  };

  class Connection: public _::VatNetworkBase::Connection {
    // A two-way RPC connection.
    //
    // This object may represent a connection that doesn't exist yet, but is expected to exist
    // in the future.  In this case, sent messages will automatically be queued and sent once the
    // connection is ready, so that the caller doesn't need to know the difference.

  public:
    // Level 0 features ----------------------------------------------

    virtual kj::Own<OutgoingRpcMessage> newOutgoingMessage(uint firstSegmentWordSize) = 0;
    // Allocate a new message to be sent on this connection.
    //
    // If `firstSegmentWordSize` is non-zero, it should be treated as a hint suggesting how large
    // to make the first segment.  This is entirely a hint and the connection may adjust it up or
    // down.  If it is zero, the connection should choose the size itself.

    virtual kj::Promise<kj::Maybe<kj::Own<IncomingRpcMessage>>> receiveIncomingMessage() = 0;
    // Wait for a message to be received and return it.  If the read stream cleanly terminates,
    // return null.  If any other problem occurs, throw an exception.

    // Level 3 features ----------------------------------------------

    virtual void introduceTo(Connection& recipient,
        typename ThirdPartyCapId::Builder sendToRecipient,
        typename RecipientId::Builder sendToTarget) = 0;
    // Call before starting a three-way introduction, assuming a `Provide` message is to be sent on
    // this connection and a `ThirdPartyCapId` is to be sent to `recipient`.  `sendToRecipient` and
    // `sendToTarget` are filled in with the identifiers that need to be sent to the recipient
    // (in a `CapDescriptor`) and on this connection (in the `Provide` message), respectively.
    //
    // `recipient` must be from the same `VatNetwork` as this connection.

    virtual ConnectionAndProvisionId connectToIntroduced(
        typename ThirdPartyCapId::Reader capId) = 0;
    // Given a ThirdPartyCapId received over this connection, connect to the third party.  The
    // caller should then send an `Accept` message over the new connection.

    virtual kj::Own<Connection> acceptIntroducedConnection(
        typename RecipientId::Reader recipientId) = 0;
    // Given a `RecipientId` received in a `Provide` message on this `Connection`, wait for the
    // recipient to connect, and return the connection formed.  Usually, the first message received
    // on the new connection will be an `Accept` message.

  private:
    void baseIntroduceTo(VatNetworkBase::Connection& recipient,
        AnyPointer::Builder sendToRecipient, AnyPointer::Builder sendToTarget) override final;
    _::VatNetworkBase::ConnectionAndProvisionId baseConnectToIntroduced(
        AnyPointer::Reader capId) override final;
    kj::Own<_::VatNetworkBase::Connection> baseAcceptIntroducedConnection(
        AnyPointer::Reader recipientId) override final;
  };

  // Level 0 features ------------------------------------------------

  virtual kj::Maybe<kj::Own<Connection>> connectToRefHost(
      typename SturdyRefHostId::Reader hostId) = 0;
  // Connect to a SturdyRef host.  Note that this method immediately returns a `Connection`, even
  // if the network connection has not yet been established.  Messages can be queued to this
  // connection and will be delivered once it is open.  The caller must attempt to read from the
  // connection to verify that it actually succeeded; the read will fail if the connection
  // couldn't be opened.  Some network implementations may actually start sending messages before
  // hearing back from the server at all, to avoid a round trip.
  //
  // Once connected, the caller should start by sending a `Restore` message for the associated
  // SturdyRefObjectId.
  //
  // Returns nullptr if `hostId` refers to the local host.

  virtual kj::Promise<kj::Own<Connection>> acceptConnectionAsRefHost() = 0;
  // Wait for the next incoming connection and return it.  Only connections formed by
  // connectToRefHost() are returned by this method.
  //
  // Once connected, the first received message will usually be a `Restore`.

  // Level 4 features ------------------------------------------------
  // TODO(someday)

private:
  kj::Maybe<kj::Own<_::VatNetworkBase::Connection>>
      baseConnectToRefHost(_::StructReader hostId) override final;
  kj::Promise<kj::Own<_::VatNetworkBase::Connection>>
      baseAcceptConnectionAsRefHost() override final;
};

// =======================================================================================
// ***************************************************************************************
// Inline implementation details start here
// ***************************************************************************************
// =======================================================================================

template <typename SturdyRef, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
void VatNetwork<SturdyRef, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>::
    Connection::baseIntroduceTo(VatNetworkBase::Connection& recipient,
                                AnyPointer::Builder sendToRecipient,
                                AnyPointer::Builder sendToTarget) {
  introduceTo(kj::downcast<Connection>(recipient), sendToRecipient.initAs<ThirdPartyCapId>(),
              sendToTarget.initAs<RecipientId>());
}

template <typename SturdyRef, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
_::VatNetworkBase::ConnectionAndProvisionId
    VatNetwork<SturdyRef, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>::
    Connection::baseConnectToIntroduced(AnyPointer::Reader capId) {
  auto result = connectToIntroduced(capId.getAs<ThirdPartyCapId>());
  return { kj::mv(result.connection), kj::mv(result.firstMessage), kj::mv(result.provisionId) };
}

template <typename SturdyRef, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
kj::Own<_::VatNetworkBase::Connection>
    VatNetwork<SturdyRef, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>::
    Connection::baseAcceptIntroducedConnection(AnyPointer::Reader recipientId) {
  return acceptIntroducedConnection(recipientId.getAs<RecipientId>());
}

template <typename SturdyRef, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
kj::Maybe<kj::Own<_::VatNetworkBase::Connection>>
    VatNetwork<SturdyRef, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>::
    baseConnectToRefHost(_::StructReader ref) {
  auto maybe = connectToRefHost(typename SturdyRef::Reader(ref));
  return maybe.map([](kj::Own<Connection>& conn) -> kj::Own<_::VatNetworkBase::Connection> {
    return kj::mv(conn);
  });
}

template <typename SturdyRef, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
kj::Promise<kj::Own<_::VatNetworkBase::Connection>>
    VatNetwork<SturdyRef, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>::
    baseAcceptConnectionAsRefHost() {
  return acceptConnectionAsRefHost().then(
      [](kj::Own<Connection>&& connection) -> kj::Own<_::VatNetworkBase::Connection> {
    return kj::mv(connection);
  });
}

template <typename SturdyRef>
Capability::Client SturdyRefRestorer<SturdyRef>::baseRestore(AnyPointer::Reader ref) {
  return restore(ref.getAs<SturdyRef>());
}

template <typename SturdyRefHostId>
template <typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult,
          typename LocalSturdyRefObjectId>
RpcSystem<SturdyRefHostId>::RpcSystem(
      VatNetwork<SturdyRefHostId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
      kj::Maybe<SturdyRefRestorer<LocalSturdyRefObjectId>&> restorer)
    : _::RpcSystemBase(network, restorer) {}

template <typename SturdyRefHostId>
Capability::Client RpcSystem<SturdyRefHostId>::restore(
    typename SturdyRefHostId::Reader hostId, AnyPointer::Reader objectId) {
  return baseRestore(_::PointerHelpers<SturdyRefHostId>::getInternalReader(hostId), objectId);
}

template <typename SturdyRefHostId, typename LocalSturdyRefObjectId,
          typename ProvisionId, typename RecipientId, typename ThirdPartyCapId, typename JoinResult>
RpcSystem<SturdyRefHostId> makeRpcServer(
    VatNetwork<SturdyRefHostId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
    SturdyRefRestorer<LocalSturdyRefObjectId>& restorer) {
  return RpcSystem<SturdyRefHostId>(network,
      kj::Maybe<SturdyRefRestorer<LocalSturdyRefObjectId>&>(restorer));
}

template <typename SturdyRefHostId, typename ProvisionId,
          typename RecipientId, typename ThirdPartyCapId, typename JoinResult>
RpcSystem<SturdyRefHostId> makeRpcClient(
    VatNetwork<SturdyRefHostId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network) {
  return RpcSystem<SturdyRefHostId>(network,
      kj::Maybe<SturdyRefRestorer<AnyPointer>&>(nullptr));
}

}  // namespace capnp

#endif  // CAPNP_RPC_H_
