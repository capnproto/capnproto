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

@0xa184c7885cdaf2a1;
# This file defines the "network-specific parameters" in rpc.proto to support a network consisting
# of two vats:  a container, and the vat it contains.  The container may actually be a full sandbox
# that confines the inner app.  The contained app can only speak to the outside world through the
# container.  Therefore, from the point of view of the containee, the container represents the
# whole world and all capabilities in the rest of the world appear "hosted" by the container.
# Meanwhile, the container proxies any capabilities exported by the containee, and thus the rest of
# the world only sees the container, and treats it as if it were itself the host of all of the
# containee's objects.
#
# Since there are only two vats in this network, there is never a need for three-way introductions.
# Joins _could_ be needed in cases where the container itself participates in a network that uses
# joins, as the container may export two objects to the containee which, unbeknownst to it, are
# actually proxies of the same remote object.  However, from the containee's point of view, such
# a join is trivial to request, and the containee never needs to receive join requests.
#
# Therefore, a level 3 implementation of the confined network is barely more complicated than a
# level 1 implementation.  However, such an implementation is able to make use of the container's
# implementation of whatever network it lives in.  Thus, if you have an application that implements
# the confined network at level 3, your application can participate in _any_ network at level 3 so
# long as you pair it with the appropriate container.
#
# The "confined" network protocol may also be a reasonable basis for simple two-party client-server
# interactions, where the server plays the part of the container.

using Cxx = import "c++.capnp";
$Cxx.namespace("capnp::rpc::confined");

struct SturdyRef {
  union {
    external @0 :Object;
    # The object lives outside the container.  The container can handle `Restore` requests for this
    # ref.  The content of the ref is defined by the container implementation and opaque to the
    # containee.  The container ensures that the ref is encoded in such a way that the containee
    # cannot possibly derive the original bits of the external-world reference, probably by storing
    # the external ref on a table somewhere.  See:
    #     http://www.erights.org/elib/capability/dist-confine.html

    confined @1 :Object;
    # The object lives inside the container -- it is implemented by the contained app.  That app
    # handles `Restore` requests for this ref.  The content of the ref is defined by the app
    # and opaque to the container.  The container shall ensure that the raw bits of this ref are
    # not revealed to the outside world, so as long as the app trusts the container, it need not
    # worry about encrypting or signing the ref.
  }
}

struct ProvisionId {
  # Only used for joins, since three-way introductions never happen on a two-party network.

  joinId @0 :UInt32;
  # The ID from `JoinKeyPart`.
}

struct RecipientId {}
# Never used, because there are only two parties.

struct ThirdPartyCapId {}
# Never used, because there is no third party.

struct JoinKeyPart {
  joinId @0 :UInt32;
  # A number identifying this join, chosen by the contained app.  Since the container will never
  # issue a join _to_ the contained app, all ongoing joins across the whole (two-vat) network are
  # uniquely identified by this ID.

  partCount @1 :UInt16;
  # The number of capabilities to be joined.

  partNum @2 :UInt16;
  # Which part this request targets -- a number in the range [0, partCount).
}

struct JoinAnswer {
  joinId @0 :UInt32;
  # Matches `JoinKeyPart`.

  succeeded @1 :Bool;
  # All JoinAnswers in the set will have the same value for `succeeded`.  The container actually
  # implements the join by waiting for all the `JoinKeyParts` and then performing its own join on
  # them, then going back and answering all the join requests afterwards.
}
