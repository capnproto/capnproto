# Copyright (c) 2014 Sandstorm Development Group, Inc. and contributors
# Licensed under the MIT License:
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

@0xb8630836983feed7;

$import "/capnp/c++.capnp".namespace("capnp");

interface Persistent@0xc8cb212fcd9f5691(SturdyRef) {
  # Interface implemented by capabilities that outlive a single connection. A client may save()
  # the capability, producing a SturdyRef. The SturdyRef can be stored to disk, then later used to
  # obtain a new reference to the capability on a future connection.
  #
  # The exact format of SturdyRef depends on the "realm" in which the SturdyRef appears. A "realm"
  # is an abstract space in which all SturdyRefs have the same format and refer to the same set of
  # resources. Every vat is in exactly one realm. All capability clients within that vat must
  # produce SturdyRefs of the format appropriate for the realm.
  #
  # Similarly, every VatNetwork also resides in a particular realm. Usually, a vat's "realm"
  # corresponds to the realm of its main VatNetwork. However, a Vat can in fact communicate over
  # a VatNetwork in a different realm -- in this case, all SturdyRefs need to be transformed when
  # coming or going through said VatNetwork. The RPC system has hooks for registering
  # transformation callbacks for this purpose.
  #
  # Since the format of SturdyRef is realm-dependent, it is not defined here. An application should
  # choose an appropriate realm for itself as part of its design. Note that under Sandstorm, every
  # application exists in its own realm and is therefore free to define its own SturdyRef format;
  # the Sandstorm platform handles translating between realms.
  #
  # Note that whether a capability is persistent is often orthogonal to its type. In these cases,
  # the capability's interface should NOT inherit `Persistent`; instead, just perform a cast at
  # runtime. It's not type-safe, but trying to be type-safe in these cases will likely lead to
  # tears. In cases where a particular interface only makes sense on persistent capabilities, it
  # *might* make sense to explicitly inherit it. But, even in these cases, you probably don't want
  # to specify the `SturdyRef` parameter, since this type may differ from app to app or even host
  # to host.

  save @0 SaveParams -> SaveResults;
  # Save a capability persistently so that it can be restored by a future connection.  Not all
  # capabilities can be saved -- application interfaces should define which capabilities support
  # this and which do not.

  struct SaveParams {
    # Nothing for now.
  }
  struct SaveResults {
    sturdyRef @0 :SturdyRef;
  }
}

interface RealmGateway(InternalRef, ExternalRef) {
  # Interface invoked when a SturdyRef is about to cross realms. The RPC system supports providing
  # a RealmGateway as a callback hook when setting up RPC over some VatNetwork.

  import @0 (cap :Persistent(ExternalRef), params :Persistent(InternalRef).SaveParams)
         -> Persistent(InternalRef).SaveResults;
  # Given an external capability, save it and return an internal reference. Used when someone
  # inside the realm tries to save a capability from outside the realm.

  export @1 (cap :Persistent(InternalRef), params :Persistent(ExternalRef).SaveParams)
         -> Persistent(ExternalRef).SaveResults;
  # Given an internal capability, save it and return an external reference. Used when someone
  # outside the realm tries to save a capability from inside the realm.
}
