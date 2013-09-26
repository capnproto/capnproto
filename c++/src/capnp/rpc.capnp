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
# can call methods on those capabilities.  In the doc comments below, we use the words "sender"
# and "receiver".  These refer to the sender and receiver of an instance of the struct or field
# being documented.  Sometimes we refer to a "third-party" which is neither the sender nor the
# receiver.
#
# It is generally up to the vat network implementation to securely verify that connections are made
# to the intended vat as well as to encrypt transmitted data for privacy and integrity.  See the
# `VatNetwork` example interface near the end of this file.
#
# Once a connection is formed, nothing interesting can happen until one side sends a Restore
# message to obtain a persistent capability.
#
# Unless otherwise specified, messages must be delivered to the receiving application in the same
# order in which they were initiated by the sending application, just like in E:
#     http://erights.org/elib/concurrency/partial-order.html
#
# Since the full protocol is complicated, we define multiple levels of support which an
# implementation may target.  For typical applications, level 1 support will be sufficient.
# Comments in this file indicate which level requires the corresponding feature to be
# implemented.
#
# * **Level 0:** The implementation does not support object references.  `Restore` is supported
#   only for looking up singleton objects which exist for the lifetime of the server, and only
#   these singleton objects can receive calls.  At this level, the implementation does not support
#   object-oriented protocols and is similar in complexity to JSON-RPC or Protobuf "generic
#   services".  This level should be considered only a temporary stepping-stone toward level 1 as
#   the lack of object references drastically changes how protocols are designed.  Applications
#   _should not_ attempt to design their protocols around the limitations of level 0
#   implementations.
#
# * **Level 1:** The implementation supports simple bilateral interaction with object references
#   and promise pipelining, but interactions between three or more parties are supported only via
#   proxying of objects.  E.g. if Alice wants to send Bob a capability pointing to Carol, Alice
#   must host a local proxy of Carol and send Bob a reference to that; Bob cannot form a direct
#   connection to Carol.  Level 1 implementations do not support "join" or "eq" across capabilities
#   received from different vats, although they should be supported on capabilities received from
#   the same vat.  `Restore` is supported only for looking up singleton objects as in level 0.
#
# * **Level 2:** The implementation supports saving, restoring, and deleting persistent
#   capabilities.
#
# * **Level 3:** The implementation supports three-way interactions but does not implement "Join"
#   operations.  The implementation can be used effectively on networks that do not require joins,
#   or to implement objects that never need to be joined.
#
# * **Level 4:** The entire protocol is implemented, including joins.
#
# Note that an implementation must also support specific networks (transports), as described in
# the "Network-specific Parameters" section below.  An implementation might have different levels
# depending on the network used.
#
# New implementations of Cap'n Proto should start out targeting the simplistic "confined" network
# type as defined in `rpc-confined.capnp`.  With this network type, level 3 is irrelevant and
# levels 2 and 4 are much easier than usual to implement.  When such an implementation is actually
# run inside a container, the contained app effectively gets to make full use of the container's
# network at level 4.  And since Cap'n Proto IPC is extremely fast, it may never make sense to
# bother implementing any other vat network protocol -- just use the correct container type and get
# it for free.

using Cxx = import "c++.capnp";
$Cxx.namespace("capnp::rpc");

# ========================================================================================
# The Four Tables
#
# Cap'n Proto RPC connections are stateful (although an application built on Cap'n Proto could
# export a stateless interface).  As in CapTP, for each open connection, a vat maintains four state
# tables: questions, answers, imports, and exports.  See the diagram at:
#     http://www.erights.org/elib/distrib/captp/4tables.html
#
# The question table corresponds to the other end's answer table, and the imports table corresponds
# to the other end's exports table.
#
# The entries in each table are identified by ID numbers (defined below as 32-bit integers).  These
# numbers are always specific to the connection; a newly-established connection starts with no
# valid IDs.  Since low-numbered IDs will pack better, it is suggested that IDs be assigned like
# Unix file descriptors -- prefer the lowest-number ID that is currently available.
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
#
# When a Cap'n Proto connection is lost, everything on the four tables is lost.  All questions are
# canceled and throw exceptions.  All imports become broken (all methods throw exceptions).  All
# exports and answers are implicitly released.  The only things not lost are persistent
# capabilities (`SturdyRef`s).  The application must plan for this and should respond by
# establishing a new connection and restoring from these persistent capabilities.

using QuestionId = UInt32;
# **(level 0)**
#
# Identifies a question in the questions/answers table.  The questioner (caller) chooses an ID
# when making a call.  The ID remains valid in caller -> callee messages until a Finish
# message is sent, and remains valid in callee -> caller messages until a Return message is sent.

using ExportId = UInt32;
# **(level 1)**
#
# Identifies an exported capability or promise in the exports/imports table.  The exporter chooses
# an ID before sending a capability over the wire.  If the capability is already in the table, the
# exporter should reuse the same ID.  If the ID is a promise (as opposed to a settled capability),
# this must be indicated at the time the ID is introduced; in this case, the importer shall expect
# a later Resolve message which replaces the promise.
#
# ExportIds are subject to reference counting.  When an `ExportId` is received embedded in an
# question or answer, the export has an implicit reference until that question or answer is
# released (questions are released by `Return`, answers are released by `Finish`).  Such an
# export can be retained beyond that point by including it in the `retainedCaps` list at the time
# the question/answer is released, thus incrementing its reference count.  The reference count is
# later decremented by a `Release` message.  Since the `Release` message can specify an arbitrary
# number by which to reduce the reference count, the importer should usually batch reference
# decrements and only send a `Release` when it believes the reference count has hit zero.  Of
# course, it is possible that a new reference to the released object is in-flight at the time
# that the `Release` message is sent, so it is necessary for the exporter to keep track of the
# reference count on its end as well to avoid race conditions.
#
# When an `ExportId` is received as part of a exporter -> importer message but not embedded in a
# question or answer, its reference count must be incremented unless otherwise specified.
#
# An `ExportId` remains valid in importer -> exporter messages until its reference count reaches
# zero and a `Release` message has been sent to release it.
#
# When a connection is lost, all exports are implicitly released.  It is not possible to restore
# a connection state (or, restoration should be implemented at the transport layer without the RPC
# layer knowing that anything happened).

# ========================================================================================
# Messages

struct Message {
  # An RPC connection is a bi-directional stream of Messages.

  union {
    unimplemented @0 :Message;
    # When a peer receives a message of a type it doesn't recognize or doesn't support, it
    # must immediately echo the message back to the sender in `unimplemented`.  The sender is
    # then able to examine the message and decide how to deal with it being unimplemented.
    #
    # For example, say `resolve` is received by a level 0 implementation (because a previous call
    # or return happened to contain a promise).  The receiver will echo it back as `unimplemented`.
    # The sender can then simply release the cap to which the promise had resolved, thus avoiding
    # a leak.
    #
    # For any message type that introduces a question, if the message comes back unimplemented,
    # the sender may simply treat it as if the question failed with an exception.
    #
    # In cases where there is no sensible way to react to an `unimplemented` message (without
    # resource leaks or other serious problems), the connection may need to be aborted.  This is
    # a gray area; different implementations may take different approaches.

    abort @1 :Exception;
    # Sent when a connection is being aborted due to an unrecoverable error.  This could be e.g.
    # because the sender received an invalid or nonsensical message (`isCallersFault` is true) or
    # because the sender had an internal error (`isCallersFault` is false).  The sender will shut
    # down the outgoing half of the connection after `abort` and will completely close the
    # connection shortly thereafter (it's up to the sender how much of a time buffer they want to
    # offer for the client to receive the `abort` before the connection is reset).

    # Level 0 features -----------------------------------------------

    call @2 :Call;         # Begin a method call.
    return @3 :Return;     # Complete a method call.
    finish @4 :Finish;     # Release a returned answer / cancel a call.

    # Level 1 features -----------------------------------------------

    resolve @5 :Resolve;   # Resolve a previously-sent promise.
    release @6 :Release;   # Release a capability so that the remote object can be deallocated.

    # Level 2 features -----------------------------------------------

    save @7 :Save;         # Save a capability persistently.
    restore @8 :Restore;   # Restore a persistent capability from a previous connection.
    delete @9 :Delete;     # Delete a persistent capability.

    # Level 3 features -----------------------------------------------

    provide @10 :Provide;  # Provide a capability to a third party.
    accept @11 :Accept;    # Accept a capability provided by a third party.

    # Level 4 features -----------------------------------------------

    join @12 :Join;        # Directly connect to the common root of two or more proxied caps.
  }
}

# Level 0 message types ----------------------------------------------

struct Call {
  # **(level 0)**
  #
  # Message type initiating a method call on a capability.

  questionId @0 :QuestionId;
  # A number, chosen by the caller, which identifies this call in future messages.  This number
  # must be different from all other calls originating from the same end of the connection (but
  # may overlap with question IDs originating from the opposite end).  A fine strategy is to use
  # sequential question IDs, but the recipient should not assume this.
  #
  # A question ID can be reused once both:
  # - A matching Return has been received from the callee.
  # - A matching Finish has been sent from the caller.

  target :union {
    exportedCap @1 :ExportId;
    # This call is to a capability or promise previously exported by the receiver.

    promisedAnswer @2 :PromisedAnswer;
    # This call is to a capability that is expected to be returned by another call that has not
    # yet been completed.
    #
    # At level 0, this is supported only for addressing the result of a previous `Restore`, so that
    # initial startup doesn't require a round trip.
  }

  interfaceId @3 :UInt64;
  # The type ID of the interface being called.  Each capability may implement multiple interfaces.

  methodId @4 :UInt16;
  # The ordinal number of the method to call within the requested interface.

  request @5 :Object;
  # The request struct.  The fields of this struct correspond to the parameters of the method.
  #
  # The request may contain capabilities.  These capabilities are automatically released when the
  # call returns *unless* the Return message explicitly indicates that they are being retained.
}

struct Return {
  # **(level 0)**
  #
  # Message type sent from callee to caller indicating that the call has completed.

  questionId @0 :QuestionId;
  # Question ID which is being answered, as specified in the corresponding Call.

  retainedCaps @1 :List(ExportId);
  # **(level 1)**
  #
  # List of capabilities from the request to which the callee continues to hold references.  Any
  # other capabilities from the request are implicitly released.

  union {
    answer @2 :Object;
    # Result object.  If the method returns a struct, this is it.  Otherwise, this points to
    # a struct which contains exactly one field, corresponding to the method's return type.
    # (This implies that an method's return type can be upgraded from a non-struct to a struct
    # without breaking wire compatibility.)
    #
    # For a `Return` in response to an `Accept`, `answer` is a capability pointer (and therefore
    # points to a `CapDescriptor`, but is tagged as a capability rather than a struct).  A
    # `Finish` is still required in this case, and the capability must still be listed in
    # `retainedCaps` if it is to be retained.

    exception @3 :Exception;
    # Indicates that the call failed and explains why.

    canceled @4 :Void;
    # Indicates that the call was canceled due to the caller sending a Finish message
    # before the call had completed.

    unsupportedPipelineOp @5 :Void;
    # The call was addressed to a `PromisedAnswer` that was not understood by the callee because
    # it used features that the callee's RPC implementation does not support.  The caller should
    # wait for the first call to return and then retry the dependent call as a regular,
    # non-pipelined call.
  }
}

struct Finish {
  # **(level 0)**
  #
  # Message type sent from the caller to the callee to indicate:
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
  # **(level 1)**
  #
  # List of capabilities from the answer to which the callee continues to hold references.  Any
  # other capabilities from the answer that need to be released are implicitly released along
  # with the answer itself.
}

# Level 1 message types ----------------------------------------------

struct Resolve {
  # **(level 1)**
  #
  # Message type sent to indicate that a previously-sent promise has now been resolved to some other
  # object (possibly another promise) -- or broken, or canceled.
  #
  # Keep in mind that it's possible for a `Resolve` to be sent to a level 0 implementation that
  # doesn't implement it.  For example, a method call or return might contain a capability in the
  # payload.  Normally this is fine even if the receiver is level 0, because they will implicitly
  # release all such capabilities on return / finish.  But if the cap happens to be a promise, then
  # a follow-up `Resolve` will be sent regardless of this release.  The level 0 receiver will reply
  # with an `unimplemented` message.  The sender (of the `Resolve`) can respond to this as if the
  # receiver had immediately released any capability to which the promise resolved.

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
  # **(level 1)**
  #
  # Message type sent to indicate that the sender is done with the given capability and the receiver
  # can free resources allocated to it.

  id @0 :ExportId;
  # What to release.

  referenceCount @1 :UInt32;
  # The amount by which to decrement the reference count.  The export is only actually released
  # when the reference count reaches zero.
}

# Level 2 message types ----------------------------------------------

struct Save {
  # **(level 2)**
  #
  # Message type sent to save a capability persistently so that it can be restored by a future
  # connection.  Not all capabilities can be saved -- application interfaces should define which
  # capabilities support this and which do not.

  questionId @0 :QuestionId;
  # A new question ID identifying this request, which will eventually receive a Return
  # message whose `answer` is a SturdyRef.

  target :union {
    # What is to be saved.

    exportedCap @1 :ExportId;
    # An exported capability.

    promisedAnswer @2 :PromisedAnswer;
    # A capability expected to be returned in the answer to an outstanding question.
  }
}

struct Restore {
  # **(mostly level 2)**
  #
  # Message type sent to restore a persistent capability obtained during a previous connection, or
  # through other means.
  #
  # Level 0/1 implementations need to implement a limited version of `Restore` only for the purpose
  # of bootstrapping a new connection (otherwise, there would be no objects to which to address
  # methods).  These levels may simply implement singleton services that exist for the lifetime of
  # the host process and probably have non-secret names.  A level 0 receiver of `Restore` should
  # never actually send a `Return` message, but should simply expect `Call` messages addressed to
  # the `PromisedAnswer` corresponding to the `Restore`.  A level 0 sender of `Restore` can ignore
  # the corresponding `Return` and just keep addressing the `PromisedAnswer`.

  questionId @0 :QuestionId;
  # A new question ID identifying this request, which will eventually receive a Return message
  # containing the restored capability.

  ref @1 :SturdyRef;
  # Designates the capability to restore.
}

struct Delete {
  # **(level 2)**
  #
  # Message type sent to delete a previously-saved persistent capability.  In other words, this
  # means "this ref will no longer be used in the future", so that the host can potentially
  # garbage collect resources associated with it.  Note that if any ExportId still refers to a
  # capability restored from this ref, that export should still remain valid until released.
  #
  # Different applications may define different policies regarding saved capability lifetimes that
  # may or may not rely on `Delete`.  For the purpose of implementation freedom, a receiver is
  # allowed to silently ignore a delete request for a reference it doesn't recognize.  This way,
  # a persistent capability could be given an expiration time, after which the capability is
  # automatically deleted, and any future `Delete` message is ignored.
  #
  # A client must send no more than one `Delete` message for any given `Save`, so that a host
  # can potentially implement reference counting.  However, hosts should be wary of reference
  # counting across multiple clients, as a malicious client could of course send multiple
  # `Delete`s.

  questionId @0 :QuestionId;
  # A new question ID identifying this request, which will eventually receive a Return message
  # with an empty answer.

  ref @1 :SturdyRef;
  # Designates the capability to delete.
}

# Level 3 message types ----------------------------------------------

struct Provide {
  # **(level 3)**
  #
  # Message type sent to indicate that the sender wishes to make a particular capability implemented
  # by the receiver available to a third party for direct access (without the need for the third
  # party to proxy through the sender).
  #
  # (In CapTP, `Provide` and `Accept` are methods of the global `NonceLocator` object exported by
  # every vat.  In Cap'n Proto, we bake this into the core protocol.)

  questionId @0 :QuestionId;
  # Question ID to be held open until the recipient has received the capability.  An answer will
  # be returned once the third party has successfully received the capability.  The sender must
  # at some point send a `Finish` message as with any other call, and such a message can be
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
  # **(level 3)**
  #
  # Message type sent to pick up a capability hosted by the receiving vat and provided by a third
  # party.  The third party previously designated the capability using `Provide`.

  questionId @0 :QuestionId;
  # A new question ID identifying this accept message, which will eventually receive a Return
  # message containing the provided capability.

  provision @1 :ProvisionId;
  # Identifies the provided object to be picked up.
}

# Level 4 message types ----------------------------------------------

struct Join {
  # **(level 4)**
  #
  # Message type sent to implement E.join(), which, given a number of capabilities which are
  # expected to be equivalent, finds the underlying object upon which they all agree and forms a
  # direct connection to it, skipping any proxies which may have been constructed by other vats
  # while transmitting the capability.  See:
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
  # - Bob receives the responses from Alice and Carol.  He uses the returned JoinAnswers to
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
  #
  # The receiver will reply with a `Return` whose `answer` is a JoinAnswer.  This `JoinAnswer`
  # is relayed from the joined object's host, possibly with transformation applied as needed
  # by the network.
  #
  # Like any answer, the answer must be released using a `Finish`.  However, this release
  # should not occur until the joiner has either successfully connected to the joined object.
  # Vats relaying a `Join` message similarly must not release the answer they receive until the
  # answer they relayed back towards the joiner has itself been released.  This allows the
  # joined object's host to detect when the Join operation is canceled before completing -- if
  # it receives a `Finish` for one of the join answers before the joiner successfully
  # connects.  It can then free any resources it had allocated as part of the join.

  capId @1 :ExportId;
  # The capability to join.

  keyPart @2 :JoinKeyPart;
  # A part of the join key.  These combine to form the complete join key which is used to establish
  # a direct connection.
}

# ========================================================================================
# Common structures used in messages

struct CapDescriptor {
  # **(level 1)**
  #
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
    # **(level 3)**
    #
    # A capability that lives in neither the sender's nor the receiver's vat.  The sender needs
    # to form a direct connection to a third party to pick up the capability.
  }
}

struct PromisedAnswer {
  # **(mostly level 1)**
  #
  # Specifies how to derive a promise from an unanswered question, by specifying the path of fields
  # to follow from the root of the eventual answer struct to get to the desired capability.  Used
  # to address method calls to a not-yet-returned capability or to pass such a capability as an
  # input to some other method call.
  #
  # Level 0 implementations must support `PromisedAnswer` only for the case where the answer is
  # to a `Restore` message.  In this case, `path` is always empty since `Restore` always returns
  # a raw capability.

  questionId @0 :QuestionId;
  # ID of the question (in the sender's question table / receiver's answer table) whose answer is
  # expected to contain the capability.

  transform @1 :List(Op);
  # Operations / transformations to apply to the answer in order to get the capability actually
  # being addressed.  E.g. if the answer is a struct and you want to call a method on a capability
  # pointed to by a field of the struct, you need a `getPointerField` op.

  struct Op {
    # If an RPC implementation receives an `Op` of a type it doesn't recognize, it should
    # reply with a `Return` with `unsupportedPipelineOp` set.

    union {
      noop @0 :Void;
      # Does nothing.  This member is mostly defined so that we can make `Op` a union even
      # though (as of this writing) only one real operation is defined.

      getPointerField @1 :UInt16;
      # Get a pointer field within a struct.  The number is an index into the pointer section, NOT
      # a field ordinal, so that the receiver does not need to understand the schema.

      # TODO(someday):  We could add:
      # - For lists, the ability to address every member of the list, or a slice of the list, the
      #   answer to which would be another list.  This is useful for implementing the equivalent of
      #   a SQL table join (not to be confused with the `Join` message type).
      # - Maybe some ability to test a union.
      # - Probably not a good idea:  the ability to specify an arbitrary script to run on the
      #   result.  We could define a little stack-based language where `PathPart` specifies one
      #   "instruction" or transformation to apply.  Although this is not a good idea
      #   (over-engineered), any narrower additions to `PathPart` should be designed as if this
      #   were the eventual goal.
    }
  }
}

struct ThirdPartyCapDescriptor {
  # **(level 3)**
  #
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
  # **(level 0)**
  #
  # Describes an arbitrary error that prevented an operation (e.g. a call) from completing.

  reason @0 :Text;
  # Human-readable failure description.

  isCallersFault @1 :Bool;
  # In the best estimate of the error source, is it the caller's fault that this error occurred
  # (like HTTP 400), or is it the callee's fault (like HTTP 500)?  Or, put another way, if an
  # automated bug report were to be generated for this error, should it be initially filed on the
  # caller's code or the callee's?  This is a guess.  Generally guesses should err towards blaming
  # the callee -- at the very least, the callee should be on the hook for improving their error
  # handling to be more confident.

  isPermanent @2 :Bool;
  # In the best estimate of the error source, is this error likely to repeat if the same call is
  # executed again?  Callers might use this to decide when to retry a request.

  isOverloaded @3 :Bool;
  # In the best estimate of the error source, is it likely this error was caused by the system
  # being overloaded?  If so, the caller probably should not retry the request now, but may
  # consider retrying it later.
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
# all, and joins are unusually simple to implement, so implementing at level 4 is barely more
# complicated than implementing at level 1.  Moreover, if you pair an app implementing the confined
# network with a container that implements some other network, the app can then participate on
# the container's network just as if it implemented that network directly.  The types used by the
# "confined" network are defined in `rpc-confined.capnp`.
#
# The things which we need to parameterize are:
# - How to store capabilities long-term without holding a connection open (mostly level 2).
# - How to authenticate vats in three-party introductions (level 3).
# - How to implement `Join` (level 4).
#
# Persistent references
# ---------------------
#
# **(mostly level 2)**
#
# We want to allow some capabilities to be stored long-term, even if a connection is lost and later
# recreated.  ExportId is a short-term identifier that is specific to a connection, so it doesn't
# help here.  We need a way to specify long-term identifiers, as well as a strategy for
# reconnecting to a referenced capability later.
#
# Three-party interactions
# ------------------------
#
# **(level 3)**
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
# **(level 4)**
#
# The `Join` message type and corresponding operation arranges for a direct connection to be formed
# between the joiner and the host of the joined object, and this connection must be authenticated.
# Thus, the details are network-dependent.

using SturdyRef = Object;
# **(mostly level 2)**
#
# Identifies a long-lived capability which can be obtained again in a future connection by sending
# a `Restore` message.  A SturdyRef is a lot like a URL, but possibly with additional
# considerations e.g. to support authentication without a certificate authority.
#
# The base RPC protocol does not specify under what conditions a SturdyRef can
# be restored.  For example:
# - Do you have to connect to a specific vat to restore the reference?
# - Is just any vat allowed to restore the SturdyRef, or is it tied to a specific vat requiring
#   some form of authentication?
#
# At the very least, a SturdyRef must contain at least enough information to determine where to
# connect to restore the ref.  Ideally, this information is not a physical machine address, but a
# logical identifier that can be passed to some lookup service to locate an appropriate vat.  Using
# a physical machine address would make the network brittle -- a change in topology could
# invalidate all SturdyRefs.
#
# The ref should also contain some kind of signature or certificate which can be used to
# authenticate the vat, to protect against a malicious lookup service without the need for a
# centralized certificate authority.
#
# For example, a simple internet-friendly SturdyRef might contain a DNS host name, a public key
# fingerprint, and a Swiss number (large, unguessable random number;
# http://wiki.erights.org/wiki/Swiss_number) to identify the specific object within that vat.
# This construction does have the disadvantage, though, that a compromised private key could
# invalidate all existing refs that share that key, and a compromise of any one client's storage
# could require revoking all existing refs to that object.  Various more-sophisticated mechanisms
# can solve these problems but these are beyond the scope of this protocol.

using ProvisionId = Object;
# **(level 3)**
#
# The information which must be sent in an `Accept` message to identify the object being accepted.
#
# In a network where each vat has a public/private key pair, this could simply be the public key
# fingerprint of the provider vat along with the questionId used in the `Provide` message sent from
# that provider.

using RecipientId = Object;
# **(level 3)**
#
# The information which must be sent in a `Provide` message to identify the recipient of the
# capability.
#
# In a network where each vat has a public/private key pair, this could simply be the public key
# fingerprint of the recipient.

using ThirdPartyCapId = Object;
# **(level 3)**
#
# The information needed to connect to a third party and accept a capability from it.
#
# In a network where each vat has a public/private key pair, this could be a combination of the
# third party's public key fingerprint, hints on how to connect to the third party (e.g. an IP
# address), and the question ID used in the corresponding `Provide` mesasge sent to that third party
# (used to identify which capability to pick up).

using JoinKeyPart = Object;
# **(level 4)**
#
# A piece of a secret key.  One piece is sent along each path that is expected to lead to the same
# place.  Once the pieces are combined, a direct connection may be formed between the sender and
# the receiver, bypassing any men-in-the-middle along the paths.  See the `Join` message type.
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

using JoinAnswer = Object;
# **(level 4)**
#
# Information returned in the answer to a `Join` message, needed by the joiner in order to form a
# direct connection to a joined object.  This might simply be the address of the joined object's
# host vat, since the `JoinKey` has already been communicated so the two vats already have a shared
# secret to use to authenticate each other.
#
# The `JoinAnswer` should also contain information that can be used to detect when the Join
# requests ended up reaching different objects, so that this situation can be detected easily.
# This could be a simple matter of including a sequence number -- if the joiner receives two
# `JoinAnswer`s with sequence number 0, then they must have come from different objects and the
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
#   # Level 0 features -----------------------------------------------
#
#   connectToHostOf(ref :SturdyRef) :Connection;
#   # Connect to a host which can restore the given SturdyRef.  The transport should return a
#   # promise which does not resolve until authentication has completed, but allows messages to be
#   # pipelined in before that; the transport either queues these messages until authenticated, or
#   # sends them encrypted such that only the authentic vat would be able to decrypt them.  The
#   # latter approach avoids a round trip for authentication.
#   #
#   # Once connected, the caller should start by sending a `Restore` message.
#
#   acceptConnectionAsRefHost() :Connection;
#   # Wait for the next incoming connection and return it.  Only connections formed by
#   # connectToHostOf() are returned by this method.
#   #
#   # Once connected, the first received message will usually be a `Restore`.
#
#   # Level 4 features -----------------------------------------------
#
#   newJoiner(count :UInt32): NewJoinerResponse;
#   # Prepare a new Join operation, which will eventually lead to forming a new direct connection
#   # to the host of the joined capability.  `count` is the number of capabilities to join.
#
#   struct NewJoinerResponse {
#     joinKeyParts :List(JoinKeyPart);
#     # Key parts to send in Join messages to each capability.
#
#     joiner :Joiner;
#     # Used to establish the final connection.
#   }
#
#   interface Joiner {
#     addJoinAnswer(answer :JoinAnswer) :Void;
#     # Add a JoinAnswer received in response to one of the `Join` messages.  All `JoinAnswer`s
#     # returned from all paths must be added before trying to connect.
#
#     connect() :ConnectionAndProvisionId;
#     # Try to form a connection to the joined capability's host, verifying that it has received
#     # all of the JoinKeyParts.  Once the connection is formed, the caller should send an `Accept`
#     # message on it with the specified `ProvisionId` in order to receive the final capability.
#   }
#
#   acceptConnectionFromJoiner(parts: List(JoinKeyPart), paths :List(VatPath))
#       :ConnectionAndProvisionId;
#   # Called on a joined capability's host to receive the connection from the joiner, once all
#   # key parts have arrived.  The caller should expect to receive an `Accept` message over the
#   # connection with the given ProvisionId.
# }
#
# interface Connection {
#   # Level 0 features -----------------------------------------------
#
#   send(message :Message) :Void;
#   # Send the message.  Returns successfully when the message (and all preceding messages) has
#   # been acknowledged by the recipient.
#
#   receive() :Message;
#   # Receive the next message, and acknowledges receipt to the sender.  Messages are received in
#   # the order in which they are sent.
#
#   # Level 3 features -----------------------------------------------
#
#   introduceTo(recipient :Connection) :IntroductionInfo;
#   # Call before starting a three-way introduction, assuming a `Provide` message is to be sent on
#   # this connection and a `ThirdPartyCapId` is to be sent to `recipient`.
#
#   struct IntroductionInfo {
#     sendToRecipient :ThirdPartyCapId;
#     sendToTarget :RecipientId;
#   }
#
#   connectToIntroduced(capId: ThirdPartyCapId) :ConnectionAndProvisionId;
#   # Given a ThirdPartyCapId received over this connection, connect to the third party.  The
#   # caller should then send an `Accept` message over the new connection.
#
#   acceptIntroducedConnection(recipientId: RecipientId): Connection
#   # Given a RecipientId received in a `Provide` message on this `Connection`, wait for the
#   # recipient to connect, and return the connection formed.  Usually, the first message received
#   # on the new connection will be an `Accept` message.
# }
#
# sturct ConnectionAndProvisionId {
#   # **(level 3)**
#
#   connection :Connection;
#   # Connection on which to issue `Accept` message.
#
#   provision :ProvisionId;
#   # `ProvisionId` to send in the `Accept` message.
# }
