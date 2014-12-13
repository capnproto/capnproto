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

#ifndef CAPNP_RPC_H_
#define CAPNP_RPC_H_

#if defined(__GNUC__) && !CAPNP_HEADER_WARNINGS
#pragma GCC system_header
#endif

#include "capability.h"
#include "rpc-prelude.h"

namespace capnp {

template <typename VatId, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
class VatNetwork;
template <typename SturdyRefObjectId>
class SturdyRefRestorer;

template <typename VatId>
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
            typename ThirdPartyCapId, typename JoinResult>
  RpcSystem(
      VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
      kj::Maybe<Capability::Client> bootstrapInterface,
      kj::Maybe<RealmGateway<>::Client> gateway = nullptr);

  template <typename ProvisionId, typename RecipientId,
            typename ThirdPartyCapId, typename JoinResult,
            typename LocalSturdyRefObjectId>
  RpcSystem(
      VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
      SturdyRefRestorer<LocalSturdyRefObjectId>& restorer);

  RpcSystem(RpcSystem&& other) = default;

  Capability::Client bootstrap(typename VatId::Reader vatId);
  // Connect to the given vat and return its bootstrap interface.

  Capability::Client restore(typename VatId::Reader hostId, AnyPointer::Reader objectId)
      KJ_DEPRECATED("Please transition to using a bootstrap interface instead.");
  // ** DEPRECATED **
  //
  // Restores the given SturdyRef from the network and return the capability representing it.
  //
  // `hostId` identifies the host from which to request the ref, in the format specified by the
  // `VatNetwork` in use.  `objectId` is the object ID in whatever format is expected by said host.
  //
  // This method will be removed in a future version of Cap'n Proto. Instead, please transition
  // to using bootstrap(), which is equivalent to calling restore() with a null `objectId`.
  // You may emulate the old concept of object IDs by exporting a bootstrap interface which has
  // methods that can be used to obtain other capabilities by ID.
};

template <typename VatId, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
RpcSystem<VatId> makeRpcServer(
    VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
    Capability::Client bootstrapInterface);
// Make an RPC server.  Typical usage (e.g. in a main() function):
//
//    MyEventLoop eventLoop;
//    kj::WaitScope waitScope(eventLoop);
//    MyNetwork network;
//    MyMainInterface::Client bootstrap = makeMain();
//    auto server = makeRpcServer(network, bootstrap);
//    kj::NEVER_DONE.wait(waitScope);  // run forever
//
// See also ez-rpc.h, which has simpler instructions for the common case of a two-party
// client-server RPC connection.

template <typename VatId, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult, typename RealmGatewayClient,
          typename InternalRef = _::InternalRefFromRealmGatewayClient<RealmGatewayClient>,
          typename ExternalRef = _::ExternalRefFromRealmGatewayClient<RealmGatewayClient>>
RpcSystem<VatId> makeRpcServer(
    VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
    Capability::Client bootstrapInterface, RealmGatewayClient gateway);
// Make an RPC server for a VatNetwork that resides in a different realm from the application.
// The given RealmGateway is used to translate SturdyRefs between the app's ("internal") format
// and the network's ("external") format.

template <typename VatId, typename LocalSturdyRefObjectId,
          typename ProvisionId, typename RecipientId, typename ThirdPartyCapId, typename JoinResult>
RpcSystem<VatId> makeRpcServer(
    VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
    SturdyRefRestorer<LocalSturdyRefObjectId>& restorer)
    KJ_DEPRECATED("Please transition to using a bootstrap interface instead.");
// ** DEPRECATED **
//
// Create an RPC server which exports multiple main interfaces by object ID. The `restorer` object
// can be used to look up objects by ID.
//
// Please transition to exporting only one interface, which is known as the "bootstrap" interface.
// For backwards-compatibility with old clients, continue to implement SturdyRefRestorer, but
// return the new bootstrap interface when the request object ID is null. When new clients connect
// and request the bootstrap interface, they will get that interface. Eventually, once all clients
// are updated to request only the bootstrap interface, stop implementing SturdyRefRestorer and
// switch to passing the bootstrap capability itself as the second parameter to `makeRpcServer()`.

template <typename VatId, typename ProvisionId,
          typename RecipientId, typename ThirdPartyCapId, typename JoinResult>
RpcSystem<VatId> makeRpcClient(
    VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network);
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

template <typename VatId, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult, typename RealmGatewayClient,
          typename InternalRef = _::InternalRefFromRealmGatewayClient<RealmGatewayClient>,
          typename ExternalRef = _::ExternalRefFromRealmGatewayClient<RealmGatewayClient>>
RpcSystem<VatId> makeRpcClient(
    VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
    RealmGatewayClient gateway);
// Make an RPC client for a VatNetwork that resides in a different realm from the application.
// The given RealmGateway is used to translate SturdyRefs between the app's ("internal") format
// and the network's ("external") format.

template <typename SturdyRefObjectId>
class SturdyRefRestorer: public _::SturdyRefRestorerBase {
  // ** DEPRECATED **
  //
  // In Cap'n Proto 0.4.x, applications could export multiple main interfaces identified by
  // object IDs. The callback used to map object IDs to objects was `SturdyRefRestorer`, as we
  // imagined this would eventually be used for restoring SturdyRefs as well. In practice, it was
  // never used for real SturdyRefs, only for exporting singleton objects under well-known names.
  //
  // The new preferred strategy is to export only a _single_ such interface, called the
  // "bootstrap interface". That interface can itself have methods for obtaining other objects, of
  // course, but that is up to the app. `SturdyRefRestorer` exists for backwards-compatibility.
  //
  // Hint:  Use SturdyRefRestorer<capnp::Text> to define a server that exports services under
  //   string names.

public:
  virtual Capability::Client restore(typename SturdyRefObjectId::Reader ref)
      KJ_DEPRECATED(
          "Please transition to using bootstrap interfaces instead of SturdyRefRestorer.") = 0;
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

template <typename VatId, typename ProvisionId, typename RecipientId,
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

    virtual kj::Promise<void> shutdown() = 0;
    // Waits until all outgoing messages have been sent, then shuts down the outgoing stream. The
    // returned promise resolves after shutdown is complete.
  };

  // Level 0 features ------------------------------------------------

  virtual kj::Maybe<kj::Own<Connection>> connect(typename VatId::Reader hostId) = 0;
  // Connect to a VatId.  Note that this method immediately returns a `Connection`, even
  // if the network connection has not yet been established.  Messages can be queued to this
  // connection and will be delivered once it is open.  The caller must attempt to read from the
  // connection to verify that it actually succeeded; the read will fail if the connection
  // couldn't be opened.  Some network implementations may actually start sending messages before
  // hearing back from the server at all, to avoid a round trip.
  //
  // Returns nullptr if `hostId` refers to the local host.

  virtual kj::Promise<kj::Own<Connection>> accept() = 0;
  // Wait for the next incoming connection and return it.

  // Level 4 features ------------------------------------------------
  // TODO(someday)

private:
  kj::Maybe<kj::Own<_::VatNetworkBase::Connection>>
      baseConnect(_::StructReader hostId) override final;
  kj::Promise<kj::Own<_::VatNetworkBase::Connection>> baseAccept() override final;
};

// =======================================================================================
// ***************************************************************************************
// Inline implementation details start here
// ***************************************************************************************
// =======================================================================================

template <typename SturdyRef, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
kj::Maybe<kj::Own<_::VatNetworkBase::Connection>>
    VatNetwork<SturdyRef, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>::
    baseConnect(_::StructReader ref) {
  auto maybe = connect(typename SturdyRef::Reader(ref));
  return maybe.map([](kj::Own<Connection>& conn) -> kj::Own<_::VatNetworkBase::Connection> {
    return kj::mv(conn);
  });
}

template <typename SturdyRef, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
kj::Promise<kj::Own<_::VatNetworkBase::Connection>>
    VatNetwork<SturdyRef, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>::baseAccept() {
  return accept().then(
      [](kj::Own<Connection>&& connection) -> kj::Own<_::VatNetworkBase::Connection> {
    return kj::mv(connection);
  });
}

template <typename SturdyRef>
Capability::Client SturdyRefRestorer<SturdyRef>::baseRestore(AnyPointer::Reader ref) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  return restore(ref.getAs<SturdyRef>());
#pragma GCC diagnostic pop
}

template <typename VatId>
template <typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
RpcSystem<VatId>::RpcSystem(
      VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
      kj::Maybe<Capability::Client> bootstrap,
      kj::Maybe<RealmGateway<>::Client> gateway)
    : _::RpcSystemBase(network, kj::mv(bootstrap), kj::mv(gateway)) {}

template <typename VatId>
template <typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult,
          typename LocalSturdyRefObjectId>
RpcSystem<VatId>::RpcSystem(
      VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
      SturdyRefRestorer<LocalSturdyRefObjectId>& restorer)
    : _::RpcSystemBase(network, restorer) {}

template <typename VatId>
Capability::Client RpcSystem<VatId>::bootstrap(typename VatId::Reader vatId) {
  return baseBootstrap(_::PointerHelpers<VatId>::getInternalReader(vatId));
}

template <typename VatId>
Capability::Client RpcSystem<VatId>::restore(
    typename VatId::Reader hostId, AnyPointer::Reader objectId) {
  return baseRestore(_::PointerHelpers<VatId>::getInternalReader(hostId), objectId);
}

template <typename VatId, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult>
RpcSystem<VatId> makeRpcServer(
    VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
    Capability::Client bootstrapInterface) {
  return RpcSystem<VatId>(network, kj::mv(bootstrapInterface));
}

template <typename VatId, typename ProvisionId, typename RecipientId,
          typename ThirdPartyCapId, typename JoinResult,
          typename RealmGatewayClient, typename InternalRef, typename ExternalRef>
RpcSystem<VatId> makeRpcServer(
    VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
    Capability::Client bootstrapInterface, RealmGatewayClient gateway) {
  return RpcSystem<VatId>(network, kj::mv(bootstrapInterface),
      gateway.template castAs<RealmGateway<>>());
}

template <typename VatId, typename LocalSturdyRefObjectId,
          typename ProvisionId, typename RecipientId, typename ThirdPartyCapId, typename JoinResult>
RpcSystem<VatId> makeRpcServer(
    VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
    SturdyRefRestorer<LocalSturdyRefObjectId>& restorer) {
  return RpcSystem<VatId>(network, restorer);
}

template <typename VatId, typename ProvisionId,
          typename RecipientId, typename ThirdPartyCapId, typename JoinResult>
RpcSystem<VatId> makeRpcClient(
    VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network) {
  return RpcSystem<VatId>(network, nullptr);
}

template <typename VatId, typename ProvisionId,
          typename RecipientId, typename ThirdPartyCapId, typename JoinResult,
          typename RealmGatewayClient, typename InternalRef, typename ExternalRef>
RpcSystem<VatId> makeRpcClient(
    VatNetwork<VatId, ProvisionId, RecipientId, ThirdPartyCapId, JoinResult>& network,
    RealmGatewayClient gateway) {
  return RpcSystem<VatId>(network, nullptr, gateway.template castAs<RealmGateway<>>());
}

}  // namespace capnp

#endif  // CAPNP_RPC_H_
