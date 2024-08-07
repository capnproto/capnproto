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

#pragma once

#include <capnp/capability.h>
#include "rpc-prelude.h"

CAPNP_BEGIN_HEADER

namespace kj { class AutoCloseFd; }

namespace capnp {

template <typename VatId, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
class VatNetwork;

class MessageReader;

template <typename VatId>
class BootstrapFactory: public _::BootstrapFactoryBase {
  // Interface that constructs per-client bootstrap interfaces. Use this if you want each client
  // who connects to see a different bootstrap interface based on their (authenticated) VatId.
  // This allows an application to bootstrap off of the authentication performed at the VatNetwork
  // level. (Typically VatId is some sort of public key.)
  //
  // This is only useful for multi-party networks. For TwoPartyVatNetwork, there's no reason to
  // use a BootstrapFactory; just specify a single bootstrap capability in this case.

public:
  virtual Capability::Client createFor(typename VatId::Reader clientId) = 0;
  // Create a bootstrap capability appropriate for exposing to the given client. VatNetwork will
  // have authenticated the client VatId before this is called.

private:
  Capability::Client baseCreateFor(AnyStruct::Reader clientId) override;
};

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

public:
  template <typename ThirdPartyCompletion, typename ThirdPartyToAwait,
            typename ThirdPartyToContact, typename JoinResult>
  RpcSystem(
      VatNetwork<VatId, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact,
                 JoinResult>& network,
      kj::Maybe<Capability::Client> bootstrapInterface);

  template <typename ThirdPartyCompletion, typename ThirdPartyToAwait,
            typename ThirdPartyToContact, typename JoinResult>
  RpcSystem(
      VatNetwork<VatId, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact,
                 JoinResult>& network,
      BootstrapFactory<VatId>& bootstrapFactory);

  RpcSystem(RpcSystem&& other) = default;

  Capability::Client bootstrap(typename VatId::Reader vatId);
  // Connect to the given vat and return its bootstrap interface.

  void setFlowLimit(size_t words);
  // Sets the incoming call flow limit. If more than `words` worth of call messages have not yet
  // received responses, the RpcSystem will not read further messages from the stream. This can be
  // used as a crude way to prevent a resource exhaustion attack (or bug) in which a peer makes an
  // excessive number of simultaneous calls that consume the receiver's RAM.
  //
  // There are some caveats. When over the flow limit, all messages are blocked, including returns.
  // If the outstanding calls are themselves waiting on calls going in the opposite direction, the
  // flow limit may prevent those calls from completing, leading to deadlock. However, a
  // sufficiently high limit should make this unlikely.
  //
  // Note that a call's parameter size counts against the flow limit until the call returns, even
  // if the recipient calls releaseParams() to free the parameter memory early. This is because
  // releaseParams() may simply indicate that the parameters have been forwarded to another
  // machine, but are still in-memory there. For illustration, say that Alice made a call to Bob
  // who forwarded the call to Carol. Bob has imposed a flow limit on Alice. Alice's calls are
  // being forwarded to Carol, so Bob never keeps the parameters in-memory for more than a brief
  // period. However, the flow limit counts all calls that haven't returned, even if Bob has
  // already freed the memory they consumed. You might argue that the right solution here is
  // instead for Carol to impose her own flow limit on Bob. This has a serious problem, though:
  // Bob might be forwarding requests to Carol on behalf of many different parties, not just Alice.
  // If Alice can pump enough data to hit the Bob -> Carol flow limit, then those other parties
  // will be disrupted. Thus, we can only really impose the limit on the Alice -> Bob link, which
  // only affects Alice. We need that one flow limit to limit Alice's impact on the whole system,
  // so it has to count all in-flight calls.
  //
  // In Sandstorm, flow limits are imposed by the supervisor on calls coming out of a grain, in
  // order to prevent a grain from inundating the system with in-flight calls. In practice, the
  // main time this happens is when a grain is pushing a large file download and doesn't implement
  // proper cooperative flow control.

  // void setTraceEncoder(kj::Function<kj::String(const kj::Exception&)> func);
  //
  // (Inherited from _::RpcSystemBase)
  //
  // Set a function to call to encode exception stack traces for transmission to remote parties.
  // By default, traces are not transmitted at all. If a callback is provided, then the returned
  // string will be sent with the exception. If the remote end is KJ/C++ based, then this trace
  // text ends up being accessible as kj::Exception::getRemoteTrace().
  //
  // Stack traces can sometimes contain sensitive information, so you should think carefully about
  // what information you are willing to reveal to the remote party.

  kj::Promise<void> run() { return RpcSystemBase::run(); }
  // Listens for incoming RPC connections and handles them. Never returns normally, but could throw
  // an exception if the system becomes unable to accept new connections (e.g. because the
  // underlying listen socket becomes broken somehow).
  //
  // For historical reasons, the RpcSystem will actually run itself even if you do not call this.
  // However, if an exception is thrown, the RpcSystem will log the exception to the console and
  // then cease accepting new connections. In this case, your server may be in a broken state, but
  // without restarting. All servers should therefore call run() and handle failures in some way.
};

template <typename VatId, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
RpcSystem<VatId> makeRpcServer(
    VatNetwork<VatId, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact,
               JoinResult>& network,
    Capability::Client bootstrapInterface);
// Make an RPC server.  Typical usage (e.g. in a main() function):
//
//    MyEventLoop eventLoop;
//    kj::WaitScope waitScope(eventLoop);
//    MyNetwork network;
//    MyMainInterface::Client bootstrap = makeMain();
//    auto server = makeRpcServer(network, bootstrap);
//    kj::NEVER_DONE.wait(waitScope);  // run forever

template <typename VatId, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
RpcSystem<VatId> makeRpcServer(
    VatNetwork<VatId, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact,
               JoinResult>& network,
    BootstrapFactory<VatId>& bootstrapFactory);
// Make an RPC server that can serve different bootstrap interfaces to different clients via a
// BootstrapInterface.

template <typename VatId, typename ThirdPartyCompletion,
          typename ThirdPartyToAwait, typename ThirdPartyToContact, typename JoinResult>
RpcSystem<VatId> makeRpcClient(
    VatNetwork<VatId, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact,
               JoinResult>& network);
// Make an RPC client.  Typical usage (e.g. in a main() function):
//
//    MyEventLoop eventLoop;
//    kj::WaitScope waitScope(eventLoop);
//    MyNetwork network;
//    auto client = makeRpcClient(network);
//    MyCapability::Client cap = client.bootstrap(hostId).castAs<MyCapability>();
//    auto response = cap.fooRequest().send().wait(waitScope);
//    handleMyResponse(response);

// =======================================================================================
// VatNetwork

class OutgoingRpcMessage {
  // A message to be sent by a `VatNetwork`.

public:
  virtual AnyPointer::Builder getBody() = 0;
  // Get the message body, which the caller may fill in any way it wants.  (The standard RPC
  // implementation initializes it as a Message as defined in rpc.capnp.)

  virtual void setFds(kj::Array<int> fds) {}
  // Set the list of file descriptors to send along with this message, if FD passing is supported.
  // An implementation may ignore this.

  virtual void send() = 0;
  // Send the message, or at least put it in a queue to be sent later.  Note that the builder
  // returned by `getBody()` remains valid at least until the `OutgoingRpcMessage` is destroyed.

  virtual size_t sizeInWords() = 0;
  // Get the total size of the message, for flow control purposes. Although the caller could
  // also call getBody().targetSize(), doing that would walk the message tree, whereas typical
  // implementations can compute the size more cheaply by summing segment sizes.
};

class IncomingRpcMessage {
  // A message received from a `VatNetwork`.

public:
  virtual AnyPointer::Reader getBody() = 0;
  // Get the message body, to be interpreted by the caller.  (The standard RPC implementation
  // interprets it as a Message as defined in rpc.capnp.)

  virtual kj::ArrayPtr<kj::AutoCloseFd> getAttachedFds() { return nullptr; }
  // If the transport supports attached file descriptors and some were attached to this message,
  // returns them. Otherwise returns an empty array. It is intended that the caller will move the
  // FDs out of this table when they are consumed, possibly leaving behind a null slot. Callers
  // should be careful to check if an FD was already consumed by comparing the slot with `nullptr`.
  // (We don't use Maybe here because moving from a Maybe doesn't make it null, so it would only
  // add confusion. Moving from an AutoCloseFd does in fact make it null.)

  virtual size_t sizeInWords() = 0;
  // Get the total size of the message, for flow control purposes. Although the caller could
  // also call getBody().targetSize(), doing that would walk the message tree, whereas typical
  // implementations can compute the size more cheaply by summing segment sizes.

  static bool isShortLivedRpcMessage(AnyPointer::Reader body);
  // Helper function which computes whether the standard RpcSystem implementation would consider
  // the given message body to be short-lived, meaning it will be dropped before the next message
  // is read. This is useful to implement BufferedMessageStream::IsShortLivedCallback.

  static kj::Function<bool(MessageReader&)> getShortLivedCallback();
  // Returns a function that wraps isShortLivedRpcMessage(). The returned function type matches
  // `BufferedMessageStream::IsShortLivedCallback` (defined in serialize-async.h), but we don't
  // include that header here.
};

class RpcFlowController {
  // Tracks a particular RPC stream in order to implement a flow control algorithm.

public:
  virtual kj::Promise<void> send(kj::Own<OutgoingRpcMessage> message, kj::Promise<void> ack) = 0;
  // Like calling message->send(), but the promise resolves when it's a good time to send the
  // next message.
  //
  // `ack` is a promise that resolves when the message has been acknowledged from the other side.
  // In practice, `message` is typically a `Call` message and `ack` is a `Return`. Note that this
  // means `ack` counts not only time to transmit the message but also time for the remote
  // application to process the message. The flow controller is expected to apply backpressure if
  // the remote application responds slowly. If `ack` rejects, then all outstanding and future
  // sends will propagate the exception.
  //
  // Note that messages sent with this method must still be delivered in the same order as if they
  // had been sent with `message->send()`; they cannot be delayed until later. This is important
  // because the message may introduce state changes in the RPC system that later messages rely on,
  // such as introducing a new Question ID that a later message may reference. Thus, the controller
  // can only create backpressure by having the returned promise resolve slowly.
  //
  // Dropping the returned promise does not cancel the send. Once send() is called, there's no way
  // to stop it.

  virtual kj::Promise<void> waitAllAcked() = 0;
  // Wait for all `ack`s previously passed to send() to finish. It is an error to call send() again
  // after this.

  // ---------------------------------------------------------------------------
  // Common implementations.

  static kj::Own<RpcFlowController> newFixedWindowController(size_t windowSize);
  // Constructs a flow controller that implements a strict fixed window of the given size. In other
  // words, the controller will throttle the stream when the total bytes in-flight exceeds the
  // window.

  class WindowGetter {
  public:
    virtual size_t getWindow() = 0;
  };

  static kj::Own<RpcFlowController> newVariableWindowController(WindowGetter& getter);
  // Like newFixedWindowController(), but the window size is allowed to vary over time. Useful if
  // you have a technique for estimating one good window size for the connection as a whole but not
  // for individual streams. Keep in mind, though, that in situations where the other end of the
  // connection is merely proxying capabilities from a variety of final destinations across a
  // variety of networks, no single window will be appropriate for all streams.

  static constexpr size_t DEFAULT_WINDOW_SIZE = 65536;
  // The window size used by the default implementation of Connection::newStream().
};

template <typename VatId, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
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
  // simple client-server apps will want to use it.
  //
  // TODO(someday):  Provide a standard implementation for the public internet.

public:
  class Connection;

  struct ConnectionAndThirdPartyCompletion {
    // Result of connecting to a vat introduced by another vat.

    kj::Own<Connection> connection;
    // Connection to the new vat.

    kj::Own<OutgoingRpcMessage> firstMessage;
    // An already-allocated `OutgoingRpcMessage` associated with `connection`.  The RPC system will
    // construct this as an `Accept` message and send it.

    Orphan<ThirdPartyCompletion> provision;
    // A `ThirdPartyCompletion` already allocated inside `firstMessage`, which the RPC system will
    // use to build the `Accept` message.
  };

  class Connection: public _::VatNetworkBase::Connection {
    // A two-way RPC connection.
    //
    // This object may represent a connection that doesn't exist yet, but is expected to exist
    // in the future.  In this case, sent messages will automatically be queued and sent once the
    // connection is ready, so that the caller doesn't need to know the difference.

  public:
    virtual kj::Own<RpcFlowController> newStream() override
        { return RpcFlowController::newFixedWindowController(65536); }
    // Construct a flow controller for a new stream on this connection. The controller can be
    // passed into OutgoingRpcMessage::sendStreaming().
    //
    // The default implementation returns a dummy stream controller that just applies a fixed
    // window of 64k to everything. This always works but may constrain throughput on networks
    // where the bandwidth-delay product is high, while conversely providing too much buffer when
    // the bandwidth-delay product is low.
    //
    // WARNING: The RPC system may keep the `RpcFlowController` object alive past the lifetime of
    //   the `Connection` itself. However, it will not call `send()` any more after the
    //   `Connection` is destroyed.
    //
    // TODO(perf): We should introduce a flow controller implementation that uses a clock to
    //   measure RTT and bandwidth and dynamically update the window size, like BBR.

    // Level 0 features ----------------------------------------------

    virtual typename VatId::Reader getPeerVatId() = 0;
    // Returns the connected vat's authenticated VatId. It is the VatNetwork's responsibility to
    // authenticate this, so that the caller can be assured that they are really talking to the
    // identified vat and not an imposter.

    virtual kj::Own<OutgoingRpcMessage> newOutgoingMessage(uint firstSegmentWordSize) override = 0;
    // Allocate a new message to be sent on this connection.
    //
    // If `firstSegmentWordSize` is non-zero, it should be treated as a hint suggesting how large
    // to make the first segment.  This is entirely a hint and the connection may adjust it up or
    // down.  If it is zero, the connection should choose the size itself.
    //
    // WARNING: The RPC system may keep the `OutgoingRpcMessage` object alive past the lifetime of
    //   the `Connection` itself. However, it will not call `send()` any more after the
    //   `Connection` is destroyed.

    virtual kj::Promise<kj::Maybe<kj::Own<IncomingRpcMessage>>> receiveIncomingMessage() override = 0;
    // Wait for a message to be received and return it.  If the read stream cleanly terminates,
    // return null.  If any other problem occurs, throw an exception.
    //
    // WARNING: The RPC system may keep the `IncomingRpcMessage` object alive past the lifetime of
    //   the `Connection` itself.

    virtual kj::Promise<void> shutdown() override KJ_WARN_UNUSED_RESULT = 0;
    // Waits until all outgoing messages have been sent, then shuts down the outgoing stream. The
    // returned promise resolves after shutdown is complete.

    virtual void setIdle(bool idle) override {}
    // Called by the RPC system whenever the connection transitions into or out of the "idle"
    // state. "Idle" means that there are no outstanding calls or capabilities held over this
    // connection in either direction. When a connection is idle, there are only two ways that
    // it can become no longer idle:
    //
    // a. A new incoming messages is received on this connection.
    // b. A call to some other VatNetwork method (e.g. `connect()` or `accept()`) returns this same
    //    Connection object again.
    //
    // In either of these two cases, the connection is no longer idle. The RpcSystem will call
    // `setIdle(false)` and then continue te service the connection.
    //
    // The RpcSystem will never send a message on a connection while it is idle.
    //
    // A VatNetwork may be able to use `setIdle()` to opportunistically end connections that are
    // no longer needed. This is easiest to do with networks that do not reuse incoming connections
    // for outgoing requests, i.e. `connect()` never returns a connection object previously
    // returned by `accept()`. The `RpcSystem` always sends bootstrap requests on connections
    // returned by `connect()`. So, in this case, it will only *receive* bootstrap requests on
    // incoming connections returned by `accept()`. Hence, when an outgoing connection becomes
    // idle, the VatNetwork can assume that no more messages will be received on it at all, unless
    // a message is sent first to start a new bootstrap. In this case, the VatNetwork can safely
    // close the connection instead.
    //
    // NOTE: If implementing such behavior, be careful about event loop concurrency. It is possible
    //   that your implementation recently returned a result from receiveIncomingMessage(), but
    //   this result is still in the event queue, and the RpcSystem has not received it yet. One
    //   way to avoid this is to `co_await kj::yieldUntilQueueEmpty()` (or use `kj::evalLast()`)
    //   to make sure any such return values have been delivered. If `setIdle(false)` is called in
    //   the meantime, cancel what you were going to do.

    // Level 3 features ----------------------------------------------

    virtual bool canIntroduceTo(Connection& other) { return false; }
    // Determines whether three-party handoff is supported, i.e. introduceTo(other, ...) can be
    // called. The caller promises that `other` is a `Connection` produced by the same
    // `VatNetwork`.
    //
    // Returns false if an introduction is not possible, in which case the RPC system must fall
    // back to proxying. This is the default implementation for VatNetworks that do not support
    // three-party handoff.

    virtual void introduceTo(Connection& other,
        typename ThirdPartyToContact::Builder otherContactInfo,
        typename ThirdPartyToAwait::Builder thisAwaitInfo) { _::throwNo3ph(); }
    // Introduce the vat at the other end of this connection ("this peer") to the vat at the other
    // end of `other` ("the other peeer").
    //
    // `otherContactInfo` will be filled in with information needed to contact the other peer. This
    // information should be passed to this peer. Conversely, `thisAwaitInfo` will be filled in with
    // information identifying this peer; it should be passed to the other peer.
    //
    // This will not be called unless a previous call to `canIntroduceTo(other)` returned true.
    //
    // TODO(someday): Define a way to attach FDs here, useful for 3PH between local processes over
    //   unix sockets.

    virtual kj::Maybe<kj::Own<Connection>> connectToIntroduced(
        typename ThirdPartyToContact::Reader contact,
        typename ThirdPartyCompletion::Builder completion) { _::throwNo3ph(); }
    // Given a `ThirdPartyToContact` that was received across this connection, form a direct
    // connection to that contact, and fill in `completion` as appropriate to send to that contact
    // in order to complete a three-party operation.
    //
    // Simlar to VatNetwork::connect(), this returns null if the target is actually the current
    // vat, and could also return an existing `Connection` if there already is one connected to
    // the requested Vat.

    virtual bool canForwardThirdPartyToContact(
        typename ThirdPartyToContact::Reader contact, Connection& destination) { return false; }
    // Determines whether `contact`, a `ThirdPartyToContact` received over *this* connection, can
    // be forwarded to another (fourth) party without actually connecting to `contact` first, i.e.
    // `forwardThirdPartyToContact(contact, destination, ...)` can be called. The caller promises
    // that `destination` is a `Connection` produced by the same `VatNetwork`.
    //
    // Returns true if forwarding can be accomplished without actually connecting to `contact`, or
    // returns false if the VatNetwork does not support this. In the latter case, the RpcSystem
    // will respond by forming a direct connection to `contact` and then initiating a second
    // handoff to the destination.

    virtual void forwardThirdPartyToContact(
        typename ThirdPartyToContact::Reader contact, Connection& destination,
        typename ThirdPartyToContact::Builder result) { _::throwNo3ph(); }
    // Given `contact`, a `ThirdPartyToContact` received over *this* connection, construct a new
    // `ThirdPartyToContact` that is valid to send over `destination` representing the same
    // three-party handoff.

    virtual kj::Own<void> awaitThirdParty(
        typename ThirdPartyToAwait::Reader party,
        kj::Rc<kj::Refcounted> value) { _::throwNo3ph(); }
    // Expect completion of a three-party handoff that is supposed to rendezvous at this node.
    //
    // `ThirdPartyToAwait` was received over this connection. A corresponding call to
    // `completeThirdParty()` will receive `value` as its result. Multiple matching calls to
    // `completeThirdParty()` are permitted and will all receive references to the value, hence
    // why it is refcounted. Once the returned `Own<void>` is dropped, `value` will be dropped
    // and any later matching call to `completeThirdParty()` will either throw or just hang.

    virtual kj::Promise<kj::Rc<kj::Refcounted>> completeThirdParty(
        typename ThirdPartyCompletion::Reader completion) { _::throwNo3ph(); }
    // Complete a three-party handoff that is supposed to rendezvous at this node.
    //
    // `ThirdPartyCompletion` was received over this connection. The promise resolves when some
    // other connection on this VatNetwork calls `awaitThirdParty()` with the corresponding
    // `ThirdPartyToAwait`. The two calls can happen in any order; `completeThirdParty()` will
    // wait for a corresponding `awaitThirdParty()` if it hasn't happened already.
    //
    // Returns a reference to the `value` passed to `awaitThirdParty()`.

    virtual kj::Array<byte> generateEmbargoId() override { _::throwNo3ph(); }
    // Generate an embargo ID for a three-party handoff. The returned ID must be unique among all
    // embargos on a particular provision. If the VatNetwork does not support forwarding (i.e.
    // canForwardThirdPartyToContact() always returns false), then it can safely return null (an
    // empty array), since there can be no more than one embargo per provision in this case.

  private:
    AnyStruct::Reader baseGetPeerVatId() override;
    bool canIntroduceTo(VatNetworkBase::Connection& other) override;
    void introduceTo(VatNetworkBase::Connection& other,
        AnyPointer::Builder otherContactInfo,
        AnyPointer::Builder thisAwaitInfo) override;
    kj::Maybe<kj::Own<VatNetworkBase::Connection>> connectToIntroduced(
        AnyPointer::Reader contact,
        AnyPointer::Builder completion) override;
    bool canForwardThirdPartyToContact(
        AnyPointer::Reader contact, VatNetworkBase::Connection& destination) override;
    void forwardThirdPartyToContact(
        AnyPointer::Reader contact, VatNetworkBase::Connection& destination,
        AnyPointer::Builder result) override;
    kj::Own<void> awaitThirdParty(
        AnyPointer::Reader party, kj::Rc<kj::Refcounted> value) override;
    kj::Promise<kj::Rc<kj::Refcounted>> completeThirdParty(AnyPointer::Reader completion) override;
    // Implements VatNetworkBase::Connection methods in terms of templated methods.
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
  //
  // The RpcSystem will call `connect()` every time the application invokes `bootstrap(vatId)`,
  // even if the vatId given is the same as a previous call. It is entirely up to the VatNetwork
  // implementation to decide whether to reuse an existing connection or make a new one each time.
  // If the VatNetwork returns a `Connection` object that it has returned before, the RpcSystem
  // will recognize this and will multiplex the new session on that existing connection.
  //
  // Similarly, `connect()` can return a connection that was previously returned by `accept()`, if
  // the VatNetwork knows that that connection goes to the right place.

  virtual kj::Promise<kj::Own<Connection>> accept() = 0;
  // Wait for the next incoming connection and return it.

  // Level 4 features ------------------------------------------------
  // TODO(someday)

private:
  kj::Maybe<kj::Own<_::VatNetworkBase::Connection>>
      baseConnect(AnyStruct::Reader hostId) override final;
  kj::Promise<kj::Own<_::VatNetworkBase::Connection>> baseAccept() override final;
};

// =======================================================================================
// ***************************************************************************************
// Inline implementation details start here
// ***************************************************************************************
// =======================================================================================

template <typename VatId>
Capability::Client BootstrapFactory<VatId>::baseCreateFor(AnyStruct::Reader clientId) {
  return createFor(clientId.as<VatId>());
}

template <typename SturdyRef, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
kj::Maybe<kj::Own<_::VatNetworkBase::Connection>>
    VatNetwork<SturdyRef, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact,
               JoinResult>::baseConnect(AnyStruct::Reader ref) {
  auto maybe = connect(ref.as<SturdyRef>());
  return maybe.map([](kj::Own<Connection>& conn) -> kj::Own<_::VatNetworkBase::Connection> {
    return kj::mv(conn);
  });
}

template <typename SturdyRef, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
kj::Promise<kj::Own<_::VatNetworkBase::Connection>>
    VatNetwork<SturdyRef, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact,
               JoinResult>::baseAccept() {
  return accept().then(
      [](kj::Own<Connection>&& connection) -> kj::Own<_::VatNetworkBase::Connection> {
    return kj::mv(connection);
  });
}

template <typename SturdyRef, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
AnyStruct::Reader VatNetwork<
    SturdyRef, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact, JoinResult>::
    Connection::baseGetPeerVatId() {
  return getPeerVatId();
}

template <typename SturdyRef, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
bool VatNetwork<
    SturdyRef, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact, JoinResult>::
    Connection::canIntroduceTo(VatNetworkBase::Connection& other) {
  return canIntroduceTo(kj::downcast<Connection>(other));
}

template <typename SturdyRef, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
void VatNetwork<
    SturdyRef, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact, JoinResult>::
    Connection::introduceTo(
        VatNetworkBase::Connection& other,
        AnyPointer::Builder otherContactInfo,
        AnyPointer::Builder thisAwaitInfo) {
  return introduceTo(kj::downcast<Connection>(other),
      otherContactInfo.initAs<ThirdPartyToContact>(),
      thisAwaitInfo.initAs<ThirdPartyToAwait>());
}

template <typename SturdyRef, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
kj::Maybe<kj::Own<_::VatNetworkBase::Connection>> VatNetwork<
    SturdyRef, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact, JoinResult>::
    Connection::connectToIntroduced(
        AnyPointer::Reader contact,
        AnyPointer::Builder completion) {
  return connectToIntroduced(
      contact.getAs<ThirdPartyToContact>(),
      completion.initAs<ThirdPartyCompletion>());
}

template <typename SturdyRef, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
bool VatNetwork<
    SturdyRef, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact, JoinResult>::
    Connection::canForwardThirdPartyToContact(
        AnyPointer::Reader contact, VatNetworkBase::Connection& destination) {
  return canForwardThirdPartyToContact(
      contact.getAs<ThirdPartyToContact>(),
      kj::downcast<Connection>(destination));
}

template <typename SturdyRef, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
void VatNetwork<
    SturdyRef, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact, JoinResult>::
    Connection::forwardThirdPartyToContact(
      AnyPointer::Reader contact, VatNetworkBase::Connection& destination,
      AnyPointer::Builder result) {
  forwardThirdPartyToContact(
      contact.getAs<ThirdPartyToContact>(),
      kj::downcast<Connection>(destination),
      result.initAs<ThirdPartyToContact>());
}

template <typename SturdyRef, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
kj::Own<void> VatNetwork<
    SturdyRef, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact, JoinResult>::
    Connection::awaitThirdParty(
        AnyPointer::Reader party, kj::Rc<kj::Refcounted> value) {
  return awaitThirdParty(party.getAs<ThirdPartyToAwait>(), kj::mv(value));
}

template <typename SturdyRef, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
kj::Promise<kj::Rc<kj::Refcounted>> VatNetwork<
    SturdyRef, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact, JoinResult>::
    Connection::completeThirdParty(AnyPointer::Reader completion) {
  return completeThirdParty(completion.getAs<ThirdPartyCompletion>());
}

template <typename VatId>
template <typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
RpcSystem<VatId>::RpcSystem(
      VatNetwork<VatId, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact,
                 JoinResult>& network,
      kj::Maybe<Capability::Client> bootstrap)
    : _::RpcSystemBase(network, kj::mv(bootstrap)) {}

template <typename VatId>
template <typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
RpcSystem<VatId>::RpcSystem(
      VatNetwork<VatId, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact,
                 JoinResult>& network,
      BootstrapFactory<VatId>& bootstrapFactory)
    : _::RpcSystemBase(network, bootstrapFactory) {}

template <typename VatId>
Capability::Client RpcSystem<VatId>::bootstrap(typename VatId::Reader vatId) {
  return baseBootstrap(_::PointerHelpers<VatId>::getInternalReader(vatId));
}

template <typename VatId>
inline void RpcSystem<VatId>::setFlowLimit(size_t words) {
  baseSetFlowLimit(words);
}

template <typename VatId, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
RpcSystem<VatId> makeRpcServer(
    VatNetwork<VatId, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact,
               JoinResult>& network,
    Capability::Client bootstrapInterface) {
  return RpcSystem<VatId>(network, kj::mv(bootstrapInterface));
}

template <typename VatId, typename ThirdPartyCompletion, typename ThirdPartyToAwait,
          typename ThirdPartyToContact, typename JoinResult>
RpcSystem<VatId> makeRpcServer(
    VatNetwork<VatId, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact,
               JoinResult>& network,
    BootstrapFactory<VatId>& bootstrapFactory) {
  return RpcSystem<VatId>(network, bootstrapFactory);
}

template <typename VatId, typename ThirdPartyCompletion,
          typename ThirdPartyToAwait, typename ThirdPartyToContact, typename JoinResult>
RpcSystem<VatId> makeRpcClient(
    VatNetwork<VatId, ThirdPartyCompletion, ThirdPartyToAwait, ThirdPartyToContact,
               JoinResult>& network) {
  return RpcSystem<VatId>(network, kj::none);
}

}  // namespace capnp

CAPNP_END_HEADER
