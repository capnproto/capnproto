# Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

@0xb312981b2552a250;
# Recall that Cap'n Proto RPC allows messages to contain references to remote objects that
# implement interfaces.  These references are called "capabilities", because they both designate
# the remote object to use and confer permission to use it.
#
# Recall also that Cap'n Proto RPC has the feature that when a method call itself returns a
# capability, the caller can begin calling methods on that capability _before the first call has
# returned_.  The caller essentially sends a message saying "Hey server, as soon as you finish
# that previous call, do this with the result!".  Cap'n Proto's RPC protocol makes this possible.
# As a result, it is more complicated than most.
#
# Cap'n Proto RPC is based heavily on CapTP:
#     http://www.erights.org/elib/distrib/captp/index.html
#
# Cap'n Proto RPC takes place between "vats".  A vat hosts some set of capabilities and talks to
# other vats through direct bilateral connections.  Typically, there is a 1:1 correspondence
# between vats and processes (in the unix sense of the word), although this is not strictly always
# true (one process could run multiple vats, or a distributed vat might live across many processes).
#
# Cap'n Proto does not distinguish between "clients" and "servers" -- this is up to the application.
# Either end of any connection can potentially hold capabilities pointing to the other end, and
# can call methods on those capabilities.  In the doc comments below, we user the words "sender"
# and "receiver".  These refer to the sender and receiver of an instance of the struct or field
# being documented.  Sometimes we refer to a "third-party" which is neither the sender nor the
# receiver.
#
# It is generally up to the vat network implementation to securely verify that connections are made
# to the intended vat as well as to encrypt transmitted data for privacy and integrity.  See the
# `VatNetwork` example interface near the end of this file.
#
# Once a connection is formed, nothing interesting can happen until one side sends a Restore frame
# to obtain a persistent capability.
#
# Unless otherwise specified, messages must be delivered to the receiving application in the same
# order in which they were initiated by the sending application, just like in E:
#     http://erights.org/elib/concurrency/partial-order.html
#
# Since the full protocol is complicated, we define multiple levels of support which an
# implementation may target.  Comments in this file indicate which level requires the corresponding
# feature to be implemented -- if unspecified, the feature must be implemented at level 1.
#
# * **Level 1:** The implementation supports simple bilateral interaction, but interactions between
#   three or more parties are supported only via proxying of objects.  E.g. if Alice wants to send
#   Bob a capability pointing to Carol, Alice must host a local proxy of Carol and send Bob a
#   reference to that; Bob cannot form a direct connection to Carol.  Level 1 implementations do
#   not support "join" or "eq" across capabilities received from different vats, although they
#   should be supported on capabilities received from the same vat.
#
# * **Level 2:** The implementation supports three-way interactions but does not implement "Join"
#   operations.  The implementation can be used effectively on networks that do not require joins,
#   or to implement objects that never need to be joined.
#
# * **Level 3:** The entire protocol is implemented, including joins.
#
# Note that an implementation must also support specific networks (transports), as described in
# the "Network-specific Parameters" section below.  An implementation might have different levels
# depending on the network used.
#
# New implementations of Cap'n Proto should start out targeting the simplistic "confined" network
# type as defined in `rpc-confined.capnp`.  With this network type, "Level 2" is irrelevant and
# "Level 3" is much easier than usual to implement.  When such an implementation is actually run
# inside a container, the contained app effectively gets to make full use of the container's
# network at level 3.  And since Cap'n Proto IPC is extremely fast, it may never make sense to
# bother implementing any other vat network protocol -- just use the correct container type and get
# it for free.

using Cxx = import "c++.capnp";
$Cxx.namespace("capnp::rpc");

# ========================================================================================
# The Four Tables
#
# As in CapTP, for each open connection, a vat maintains four tables: questions, answers, imports,
# and exports.  See the diagram at:
#     http://www.erights.org/elib/distrib/captp/4tables.html
#
# The question table corresponds to the other end's answer table, and the imports table corresponds
# to the other end's exports table.
#
# IDs in the questions/answers tables are chosen by the questioner and generally represent method
# calls that are in progress.
#
# IDs in the imports/exports tables are chosen by the exporter and generally represent objects on
# which methods may be called.  Exports may be "settled", meaning the exported object is an actual
# object living in the exporter's vat, or they may be "promises", meaning the exported object is
# the as-yet-unknown result of an ongoing operation and will eventually be resolved to some other
# object once that operation completes.  Calls made to a promise will be forwarded to the eventual
# target once it is known.  The eventual replacement object does *not* take the same ID as the
# promise, as it may turn out to be an object that is already exported (so already has an ID) or
# may even live in a completely different vat (and so won't get an ID on the same export table
# at all).
#
# IDs can be reused over time.  To make this safe, we carefully define the lifetime of IDs.  Since
# messages using the ID could be traveling in both directions simultaneously, we must define the
# end of life of each ID _in each direction_.  The ID is only safe to reuse once it has been
# released by both sides.

using QuestionId = UInt32;
# Identifies a question in the questions/answers table.  The questioner (caller) chooses an ID
# when making a call.  The ID remains valid in caller -> callee messages until a ReleaseAnswer
# message is sent, and remains valid in callee -> caller messages until a Return message is sent.

using ExportId = UInt32;
# Identifies an exported capability or promise in the exports/imports table.  The exporter chooses
# an ID before sending a capability over the wire.  If the capability is already in the table, the
# exporter should reuse the same ID.  If the ID is a promise (as opposed to a settled capability),
# this must be indicated at the time the ID is introduced; in this case, the importer shall expect
# a later Resolve frame which replaces the promise.
#
# ExportIds are subject to reference counting.  When an `ExportId` is received embedded in an
# question or answer, the export has an implicit reference until that question or answer is
# released (questions are released by `Return`, answers are released by `ReleaseAnswer`).  Such an
# export can be retained beyond that point by including it in the `retainedCaps` list at the time
# the question/answer is released, thus incrementing its reference count.  The reference count is
# later decremented by a `Release` message.  Since the `Release` message can specify an arbitrary
# number by which to reduce the reference count, the importer should usually batch reference
# decrements and only send a `Release` when it believes the reference count has hit zero.  Of
# course, it is possible that a new reference to the released object is in-flight at the time
# that the `Release` message is sent, so it is necessary for the exporter to keep track of the
# reference count on its end as well to avoid race conditions.
#
# When an `ExportId` is received as part of a Frame but not embedded in a question or answer, its
# reference count is automatically incremented unless otherwise specified.
#
# An `ExportId` remains valid in importer -> exporter messages until its reference count reaches
# zero and a `Release` message has been sent to release it.

# ========================================================================================
# Frames

struct Frame {
  # An RPC connection is a bi-directional stream of Frames.
  #
  # When the RPC system wants to send a Frame, it instructs the transport layer to keep trying to
  # send the frame until the transport can guarantee that one of the following is true:
  # 1) The Frame was received.
  # 2) The sessing is broken, and no further Frames can be sent.

  union {
    # Level 1 features -----------------------------------------------

    call @0 :Call;        # Begin a method call.
    return @1 :Return;    # Complete a method call.
    resolve @2 :Resolve;  # Resolve a previously-sent promise.

    release @3 :Release;  # Release a capability so that the remote object can be deallocated.
    releaseAnswer @4 :ReleaseAnswer;  # Release a returned answer / cancel a call.

    restore @5 :Restore;  # Restore a persistent capability from a previous connection.

    # Level 2 features -----------------------------------------------

    provide @6 :Provide;  # Provide a capability to a third party.
    accept @7 :Accept;    # Accept a capability provided by a third party.

    # Level 3 features -----------------------------------------------

    join @8 :Join;        # Directly connect to the common root of two or more proxied caps.
  }
}

struct Call {
  # Frame type initiating a method call on a capability.

  questionId @0 :QuestionId;
  # A number, chosen by the caller, which identifies this call in future messages.  This number
  # must be different from all other calls originating from the same end of the connection (but
  # may overlap with call IDs originating from the opposite end).  A fine strategy is to use
  # sequential call IDs, but the recipient should not assume this.
  #
  # TODO:  Decide if it is safe to reuse a call ID.  If not, extend to 64 bits.

  target :union {
    exportedCap @1 :ExportId;
    # This call is to a capability or promise previously exported by the receiver.

    promisedAnswer @2 :PromisedAnswer;
    # This call is to a capability that is expected to be returned by another call that has not
    # yet been completed.
  }

  interfaceId @3 :UInt64;
  # The type ID of the interface being called.  Each capability may implement multiple interfaces.

  methodId @4 :UInt16;
  # The ordinal number of the method to call within the requested interface.

  request @5 :Object;
  # The request struct.  The fields of this struct correspond to the parameters of the method.
  #
  # The request may contain capabilities.  These capabilities are automatically released when the
  # call returns *unless* the Return frame explicitly indicates that they are being retained.
}

struct Return {
  # Frame type sent from callee to caller indicating that the call has completed.

  questionId @0 :QuestionId;
  # Question ID which is being answered, as specified in the corresponding Call.

  retainedCaps @1 :List(ExportId);
  # List of capabilities from the request to which the callee continues to hold references.  Any
  # other capabilities from the request are implicitly released.

  union {
    answer @2 :Object;
    # Result object.  If the method returns a struct, this is it.  Otherwise, this points to
    # a struct which contains exactly one field, corresponding to the method's return type.
    # (This implies that an method's return type can be upgraded from a non-struct to a struct
    # without breaking wire compatibility.)
    #
    # If the response contains any capabilities, the caller is expected to send a Release frame for
    # each one when done with them.

    exception @3 :Exception;
    # Indicates that the call failed and explains why.

    canceled @4 :Void;
    # Indicates that the call was canceled due to the caller sending a ReleaseAnswer message
    # before the call had completed.
  }
}

struct Resolve {
  # Frame type sent to indicate that a previously-sent promise has now been resolved to some other
  # object (possibly another promise) -- or broken, or canceled.

  promiseId @0 :ExportId;
  # The ID of the promise to be resolved.
  #
  # Unlike all other instances of `ExportId` sent from the exporter, the `Resolve` message does
  # _not_ increase the reference count of `promiseId`.
  #
  # When a promise ID is first sent over the wire (e.g. in a `CapDescriptor`), the sender (exporter)
  # guarantees that it will follow up at some point with exactly one `Resolve` message.  If the
  # same `promiseId` is sent again before `Resolve`, still only one `Resolve` is sent.  If the
  # same ID is reused again later _after_ a `Resolve`, it can only be because the export's
  # reference count hit zero in the meantime and the ID was re-assigned to a new export, therefore
  # this later promise does _not_ correspond to the earlier `Resolve`.
  #
  # If a promise ID's reference count reaches zero before a `Resolve` is sent, the `Resolve`
  # message must still be sent, and the ID cannot be reused in the meantime.  Moreover, the object
  # to which the promise resolved itself needs to be released, even if the promise was already
  # released before it resolved.  (Although, the exporter may notice that the promise was released
  # and send a `canceled` resolution, in which case nothing new is exported.)
  #
  # RPC implementations should keep in mind when they receive a `Resolve` that the promise ID may
  # have appeared in a previous question or answer which the application has not consumed yet.
  # The RPC implementation usually shouldn't dig through the question/answer itself looking for
  # capabilities, so it won't be aware of any promise IDs in that message until the application
  # actually goes through and extracts the capabilities it wishes to retain.  Therefore, when
  # a `Resolve` is received, the RPC implementation will have to keep track of this resolution
  # at least until all previously-received questions and answers have been consumed and released
  # by the application.

  union {
    cap @1 :CapDescriptor;
    # The object to which the promise resolved.

    exception @2 :Exception;
    # Indicates that the promise was broken.

    canceled @3 :Void;
    # Indicates that this promise won't be resolved because its reference count reached zero before
    # it had completed, so the operation was canceled.
  }
}

struct Release {
  # Frame type sent to indicate that the sender is done with the given capability and the receiver
  # can free resources allocated to it.

  id @0 :ExportId;
  # What to release.

  referenceCount @1 :UInt32;
  # The number of times this ID has been received by the importer.  The object is only truly
  # released when the referenceCount from all Release messages for the ID adds up to the number of
  # times the exporter has actually sent the object to the importer.  This avoids a race condition
  # where the exporter happens to send another copy of the same ID at the same time as the importer
  # is sending a Release message for it.
}

struct ReleaseAnswer {
  # Frame type sent from the caller to the callee to indicate:
  # 1) The questionId will no longer be used in any messages sent by the callee (no further
  #    pipelined requests).
  # 2) Any capabilities in the answer other than the ones listed below should be implicitly
  #    released.
  # 3) If the answer has not returned yet, the caller no longer cares about the answer, so the
  #    callee may wish to immediately cancel the operation and send back a Return message with
  #    "canceled" set.

  questionId @0 :QuestionId;
  # ID of the question whose answer is to be released.

  retainedCaps @1 :List(ExportId);
  # List of capabilities from the answer to which the callee continues to hold references.  Any
  # other capabilities from the answer that need to be released are implicitly released along
  # with the answer itself.
}

struct Restore {
  # Frame type sent to restore a persistent capability obtained during a previous connection, or
  # through other means.

  questionId @0 :QuestionId;
  # A new question ID identifying this restore message, which will eventually receive a Return
  # message containing the restored capability.

  ref @1 :SturdyRef;
  # An object designating the capability to restore.
}

struct Provide {
  # **Level 2 feature**
  #
  # Frame type sent to indicate that the sender wishes to make a particular capability implemented
  # by the receiver available to a third party for direct access (without the need for the third
  # party to proxy through the sender).
  #
  # (In CapTP, `Provide` and `Accept` are methods of the global `NonceLocator` object exported by
  # every vat.  In Cap'n Proto, we bake this into the core protocol.)

  questionId @0 :QuestionId;
  # Question ID to be held open until the recipient has received the capability.  An answer will
  # be returned once the third party has successfully received the capability.  The sender must
  # at some point send a ReleaseAnswer message as with any other call, and such a message can be
  # used to cancel the whole operation.

  target :union {
    # What is to be provided to the third party.

    exportedCap @1 :ExportId;
    # An exported capability.

    promisedAnswer @2 :PromisedAnswer;
    # A capability expected to be returned in the answer to an outstanding question.
  }

  recipient @3 :RecipientId;
  # Identity of the third party which is expected to pick up the capability.
}

struct Accept {
  # **Level 2 feature**
  #
  # Frame type sent to pick up a capability hosted by the receiving vat and provided by a third
  # party.  The third party previously designated the capability using `Provide`.

  questionId @0 :QuestionId;
  # A new question ID identifying this accept message, which will eventually receive a Return
  # message containing the provided capability.

  provision @1 :ProvisionId;
  # Identifies the provided object to be picked up.
}

struct Join {
  # **Level 3 feature**
  #
  # Frame type sent to implement E.join(), which, given a number of capabilities which are expected
  # to be equivalent, finds the underlying object upon which they all agree and forms a direct
  # connection to it, skipping any proxies which may have been constructed by other vats while
  # transmitting the capability.  See:
  #     http://erights.org/elib/equality/index.html
  #
  # Note that this should only serve to bypass fully-transparent proxies -- proxies that were
  # created merely for convenience, without any intention of hiding the underlying object.
  #
  # For example, say Bob holds two capabilities hosted by Alice and Carol, but he expects that both
  # are simply proxies for a capability hosted elsewhere.  He then issues a join request, which
  # operates as follows:
  # - Bob issues Join requests on both Alice and Carol.  Each request contains a different piece
  #   of the JoinKey.
  # - Alice is proxying a capability hosted by Dana, so forwards the request to Dana's cap.
  # - Dana receives the first request and sees that the JoinKeyPart is one of two.  She notes that
  #   she doesn't have the other part yet, so she records the request and responds with a
  #   JoinAnswer.
  # - Alice relays the JoinAswer back to Bob.
  # - Carol is also proxying a capability from Dana, and so forwards her Join request to Dana as
  #   well.
  # - Dana receives Carol's request and notes that she now has both parts of a JoinKey.  She
  #   combines them in order to form information needed to form a secure connection to Bob.  She
  #   also responds with another JoinAnswer.
  # - Bob receives the responses from Alice and Carol.  He uses the returned JoinHostIds to
  #   determine how to connect to Dana and attempts to form the connection.  Since Bob and Dana now
  #   agree on a secret key which neither Alice nor Carol ever saw, this connection can be made
  #   securely even if Alice or Carol is conspiring against the other.  (If Alice and Carol are
  #   conspiring _together_, they can obviously reproduce the key, but this doesn't matter because
  #   the whole point of the join is to verify that Alice and Carol agree on what capability they
  #   are proxying.)
  #
  # If the two capabilities aren't actually proxies of the same object, then the join requests
  # will come back with conflicting `hostId`s and the join will fail before attempting to form any
  # connection.

  questionId @0 :QuestionId;
  # Question ID used to respond to this Join.  (Note that this ID only identifies one part of the
  # request for one hop; each part has a different ID and relayed copies of the request have
  # (probably) different IDs still.)

  capId @1 :ExportId;
  # The capability to join.

  keyPart @2 :JoinKeyPart;
  # A part of the join key.  These combine to form the complete join key which is used to establish
  # a direct connection.

  struct Answer {
    # The answer to a `Join` frame (sent in a `Return` frame).

    hostId @0 :JoinHostId;
    # Information indicating where to connect to complete the join.  Also used to verify that all
    # the joins actually reached the same object.

    vineId @1 :ExportId;
    # A new capability in the sender's export table which must be released once the join is
    # complete.  This capability has no methods.  This allows the joined capability's host to
    # detect when a Join has failed and release the associated resources on its end.
  }
}

# ========================================================================================
# Common structures used in frames

struct CapDescriptor {
  # When an application-defined type contains an interface pointer, that pointer's encoding is the
  # same as a struct pointer except that the bottom two bits are 1's instead of 0's.  The pointer
  # actually points to an instance of `CapDescriptor`.  The runtime API should not reveal the
  # CapDescriptor directly to the application, but should instead wrap it in some kind of callable
  # object with methods corresponding to the interface that the capability implements.
  #
  # Keep in mind that `ExportIds` in a `CapDescriptor` are subject to reference counting.  See the
  # description of `ExportId`.

  union {
    senderHosted :group {
      # A capability newly exported by the sender.

      id @0 :ExportId;
      # The ID of the new capability in the sender's export table (receiver's import table).

      interfaces @1 :List(UInt64);
      # Type IDs of interfaces supported by this descriptor.  This must include at least the
      # interface type as defined in the schema file, but could include others.  The runtime API
      # should allow the interface wrapper to be dynamically cast to these other types (probably
      # not using the language's built-in cast syntax, but something equivalent).
    }

    senderPromise @2 :ExportId;
    # A promise which the sender will resolve later.  The sender will send exactly one Resolve
    # message at a future point in time to replace this promise.

    receiverHosted @3 :ExportId;
    # A capability (or promise) previously exported by the receiver.

    receiverAnswer @4 :PromisedAnswer;
    # A capability expected to be returned in the answer for a currently-outstanding question posed
    # by the sender.

    thirdPartyHosted @5 :ThirdPartyCapDescriptor;
    # A capability that lives in neither the sender's nor the receiver's vat.  The sender needs
    # to form a direct connection to a third party to pick up the capability.
  }

  sturdyRef @6 :SturdyRef;
  # If non-null, this is a SturdyRef that can be used to store this capability persistently and
  # restore access to in in the future (using a `Restore` frame).  If null, this capability will be
  # lost if the connection dies.  Generally, application interfaces should define when a client can
  # expect a capability to be persistent (and therefore have a SturdyRef attached).  However,
  # application protocols should never embed SturdyRefs directly, as various infrastructure like
  # transports, gateways, and sandboxes may need to be aware of SturdyRefs being passed over the
  # wire in order to transform them into different namespaces.
}

struct PromisedAnswer {
  questionId @0 :QuestionId;
  # ID of the question whose answer is expected to contain the capability.

  path @1 :List(UInt16);
  # Path to the capability in the response.  This is a list of indexes into the pointer
  # sections of structs forming a path from the root struct to a particular capability.  Each
  # pointer except for the last one must point to another struct -- it is an error if one ends
  # up pointing to a list or something else.
  #
  # TODO(someday):  Would it make sense to support lists in the path, and say that the method
  #   should be executed on every element of the list?
}

struct ThirdPartyCapDescriptor {
  # Identifies a capability in a third-party vat which the sender wants the receiver to pick up.

  id @0 :ThirdPartyCapId;
  # Identifies the third-party host and the specific capability to accept from it.

  vineId @1 :ExportId;
  # Object in the sender's export table which must be Released once the capability has been
  # successfully obtained from the third-party vat.  This allows the sender to ensure that the
  # final capability is not released before the receiver manages to accept it.  In CapTP
  # terminology this is called a "vine", because it is an indirect reference to the third-party
  # object that snakes through the sender vat.  The vine does not accept any method calls.
}

struct Exception {
  reason @0 :Text;
  # Human-readable failure description.

  isPermanent @1 :Bool;
  # In the best estimate of the error source, is this error likely to repeat if the same call is
  # executed again?  Callers might use this to decide when to retry a request.

  isOverloaded @2 :Bool;
  # In the best estimate of the error source, is it likely this error was caused by the system
  # being overloaded?  If so, the caller probably should not retry the request now, but may
  # consider retrying it later.

  nature @3 :Nature;
  # The nature of the failure.  This is intended mostly to allow classification of errors for
  # reporting and monitoring purposes -- the caller is not expected to handle different natures
  # differently.

  enum Nature {
    # These correspond to kj::Exception::Nature.

    precondition @0;
    localBug @1;
    osError @2;
    networkFailure @3;
    other @4;
  }
}

# ========================================================================================
# Network-specific Parameters
#
# Some parts of the Cap'n Proto RPC protocol are not specified here because different vat networks
# may wish to use different approaches to solving them.  For example, on the public internet, you
# may want to authenticate vats using public-key cryptography, but on a local intranet with trusted
# infrastructure, you may be happy to authenticate based on network address only, or some other
# lightweight mechanism.
#
# To accommodate this, we specify several "parameter" types.  Each type is defined here as an
# alias for `Object`, but a specific network will want to define a specific set of types to use.
# All vats in a vat network must agree on these parameters in order to be able to communicate.
# Inter-network communication can be accomplished through "gateways" that perform translation
# between the primitives used on each network; these gateways may need to be deeply stateful,
# depending on the translations they perform.
#
# For interaction over the global internet between parties with no other prior arrangement, a
# particular set of bindings for these types is defined elsewhere.  (TODO(soon): Specify where
# these common definitions live.)
#
# Another common network type is the "confined" network, in which a contained vat interacts with
# the outside world entirely through a container/supervisor.  All objects in the world that aren't
# hosted by the contained vat appear as if they were hosted by the container.  This network type is
# interesting because from the containee's point of view, there are no three-party interactions at
# all, and joins are unusually simple to implement, so implementing at level 3 is barely more
# complicated than implementing at level 1.  Moreover, if you pair an app implementing the confined
# network with a container that implements some other network, the app can then participate on
# the container's network just as if it implemented that network directly.  The types used by the
# "confined" network are defined in `rpc-confined.capnp`.
#
# The things which we need to parameterize are:
# - How to authenticate vats in three-party introductions.
# - How to implement `Join`.
# - How to store capabilities long-term without holding a connection open.
#
# Three-party interactions
# ------------------------
#
# **Level 2 feature**
#
# In cases where more than two vats are interacting, we have situations where VatA holds a
# capability hosted by VatB and wants to send that capability to VatC.  This can be accomplished
# by VatA proxying requests on the new capability, but doing so has two big problems:
# - It's inefficient, requiring an extra network hop.
# - If VatC receives another capability to the same object from VatD, it is difficult for VatC to
#   detect that the two capabilities are really the same and to implement the E "join" operation,
#   which is necessary for certain four-or-more-party interactions, such as the escrow pattern.
#   See:  http://www.erights.org/elib/equality/grant-matcher/index.html
#
# Instead, we want a way for VatC to form a direct, authenticated connection to VatB.
#
# Join
# ----
#
# **Level 3 feature**
#
# The `Join` frame type and corresponding operation arranges for a direct connection to be formed
# between the joiner and the host of the joined object, and this connection must be authenticated.
# Thus, the details are network-dependent.
#
# Persistent references
# ---------------------
#
# We want to allow some capabilities to be stored long-term, even if a connection is lost and later
# recreated.  ExportId is a short-term identifier that is specific to a connection, so it doesn't
# help here.  We need a way to specify long-term identifiers, as well as a strategy for
# reconnecting to a referenced capability later.

using SturdyRef = Object;
# Identifies a long-lived capability which can be obtained again in a future connection by sending
# a `Restore` frame.  The base RPC protocol does not specify under what conditions a SturdyRef can
# be restored.  For example:
# - Do you have to connect to a specific vat to restore the reference?
# - Is just any vat allowed to restore the SturdyRef, or is it tied to a specific vat requiring
#   some form of authentication?

using ProvisionId = Object;
# **Level 2 feature**
#
# The information which must be sent in an `Accept` frame to identify the object being accepted.
#
# In a network where each vat has a public/private key pair, this could simply be the public key
# fingerprint of the provider vat along with the questionId used in the `Provide` frame sent from
# that provider.

using RecipientId = Object;
# **Level 2 feature**
#
# The information which must be sent in a `Provide` frame to identify the recipient of the
# capability.
#
# In a network where each vat has a public/private key pair, this could simply be the public key
# fingerprint of the recipient.

using ThirdPartyCapId = Object;
# **Level 2 feature**
#
# The information needed to connect to a third party and accept a capability from it.
#
# In a network where each vat has a public/private key pair, this could be a combination of the
# third party's public key fingerprint, hints on how to connect to the third party (e.g. an IP
# address), and the question ID used in the corresponding `Provide` frame sent to that third party
# (used to identify which capability to pick up).

using JoinKeyPart = Object;
# A piece of a secret key.  One piece is sent along each path that is expected to lead to the same
# place.  Once the pieces are combined, a direct connection may be formed between the sender and
# the receiver, bypassing any men-in-the-middle along the paths.  See the "Join" frame type.
#
# The motivation for Joins is discussed under "Supporting Equality" in the "Unibus" protocol
# sketch: http://www.erights.org/elib/distrib/captp/unibus.html
#
# In a network where each vat has a public/private key pair and each vat forms no more than one
# connection to each other vat, Joins will rarely -- perhaps never -- be needed, as objects never
# need to be transparently proxied and references to the same object sent over the same connection
# have the same export ID.  Thus, a successful join requires only checking that the two objects
# come from the same connection and have the same ID, and then completes immediately.
#
# However, in networks where two vats may form more than one connection between each other, or
# where proxying of objects occurs, joins are necessary.
#
# Typically, each JoinKeyPart would include a fixed-length data value such that all value parts
# XOR'd together forms a shared secret which can be used to form an encrypted connection between
# the joiner and the joined object's host.  Each JoinKeyPart should also include an indication of
# how many parts to expect and a hash of the shared secret (used to match up parts).

using JoinHostId = Object;
# Information needed by the joiner in order to form a direct connection to a joined object.  One
# of these is returned in response to each `Join` frame.  This might simply be the address of the
# joined object's host vat, since the `JoinKey` has already been communicated so the two vats
# already have a shared secret to use to authenticate each other.
#
# The `JoinHostId` should also contain information that can be used to detect when the Join
# requests ended up reaching different objects, so that this situation can be detected easily.
# This could be a simple matter of including a sequence number -- if the joiner receives two
# `JoinHostId`s with sequence number 0, then they must have come from different objects and the
# whole join is a failure.

# ========================================================================================
# Network interface sketch
#
# The interfaces below are meant to be pseudo-code to illustrate how the details of a particular
# vat network might be abstracted away.  They are written like Cap'n Proto interfaces, but in
# practice you'd probably define these interfaces manually in the target programming language.  A
# Cap'n Proto RPC implementation should be able to use these interfaces without knowing the
# definitions of the various network-specific parameters defined above.

# interface VatNetwork {
#   # Represents a vat network, with the ability to connect to particular vats and receive
#   # connections from vats.
#   #
#   # Note that methods returning a `Connection` may return a pre-existing `Connection`, and the
#   # caller is expected to find and share state with existing users of the connection.
#
#   # Level 1 features -----------------------------------------------
#
#   connectToHostOf(ref :SturdyRef) :Connection;
#   # Connect to a host which can restore the given SturdyRef.  The transport should return a
#   # promise which does not resolve until authentication has completed, but allows messages to be
#   # pipelined in before that; the transport either queues these messages until authenticated, or
#   # sends them encrypted such that only the authentic vat would be able to decrypt them.  The
#   # latter approach avoids a round trip for authentication.
#   #
#   # Once connected, the caller should start by sending a `Restore` frame.
#
#   acceptConnectionAsRefHost() :Connection;
#   # Wait for the next incoming connection and return it.  Only connections formed by
#   # connectToHostOf() are returned by this method.
#   #
#   # Once connected, the first received frame will usually be a `Restore`.
#
#   # Level 3 features -----------------------------------------------
#
#   newJoiner(count :UInt32): NewJoinerResponse;
#   # Prepare a new Join operation, which will eventually lead to forming a new direct connection
#   # to the host of the joined capability.  `count` is the number of capabilities to join.
#
#   struct NewJoinerResponse {
#     joinKeyParts :List(JoinKeyPart);
#     # Key parts to send in Join frames to each capability.
#
#     joiner :Joiner;
#     # Used to establish the final connection.
#   }
#
#   interface Joiner {
#     addHostId(hostId :JoinHostId) :Void;
#     # Add a host ID at which the host might live.  All `JoinHostId`s returned from all paths
#     # must be added before trying to connect.
#
#     connect() :ConnectionAndProvisionId;
#     # Try to form a connection to the joined capability's host, verifying that it has received
#     # all of the JoinKeyParts.  Once the connection is formed, the caller should send an `Accept`
#     # frame on it with the specified `ProvisionId` in order to receive the final capability.
#   }
#
#   acceptConnectionFromJoiner(parts: List(JoinKeyPart), paths :List(VatPath))
#       :ConnectionAndProvisionId;
#   # Called on a joined capability's host to receive the connection from the joiner, once all
#   # key parts have arrived.  The caller should expect to receive an `Accept` frame over the
#   # connection with the given ProvisionId.
# }
#
# interface Connection {
#   # Level 1 features -----------------------------------------------
#
#   send(frame :Frame) :Void;
#   # Send the frame.  Returns successfully when the frame (and all preceding frames) has been
#   # acknowledged by the recipient.
#
#   receive() :Frame;
#   # Receive the next frame, and acknowledges receipt to the sender.  Frames are received in the
#   # order in which they are sent.
#
#   # Level 2 features -----------------------------------------------
#
#   introduceTo(recipient :Connection) :IntroductionInfo;
#   # Call before starting a three-way introduction, assuming a `Provide` frame is to be sent on
#   # this connection and a `ThirdPartyCapId` is to be sent to `recipient`.
#
#   struct IntroductionInfo {
#     sendToRecipient :ThirdPartyCapId;
#     sendToTarget :RecipientId;
#   }
#
#   connectToIntroduced(capId: ThirdPartyCapId) :ConnectionAndProvisionId;
#   # Given a ThirdPartyCapId received over this connection, connect to the third party.  The
#   # caller should then send an `Accept` frame over the new connection.
#
#   acceptIntroducedConnection(recipientId: RecipientId): Connection
#   # Given a RecipientId received in a `Provide` frame on this `Connection`, wait for the
#   # recipient to connect, and return the connection formed.  Usually, the first frame received
#   # on the new connection will be an `Accept` frame.
# }
#
# sturct ConnectionAndProvisionId {
#   connection :Connection;
#   # Connection on which to issue `Accept` frame.
#
#   provision :ProvisionId;
#   # `ProvisionId` to send in the `Accept` frame.
# }
