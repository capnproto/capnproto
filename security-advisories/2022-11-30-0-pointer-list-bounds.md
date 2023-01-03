Problem
=======

Out-of-bounds read due to logic error handling list-of-list.

Discovered by
=============

David Renshaw &lt;dwrenshaw@gmail.com>, the maintainer of Cap'n Proto's Rust
implementation, which is affected by the same bug. David discovered this bug
while running his own fuzzer.

Announced
=========

2022-11-30

CVE
===

CVE-2022-46149

Impact
======

- Remotely segfault a peer by sending it a malicious message, if the victim
  performs certain actions on a list-of-pointer type.
- Possible exfiltration of memory, if the victim performs additional certain
  actions on a list-of-pointer type.
- To be vulnerable, an application must perform a specific sequence of actions,
  described below. At present, **we are not aware of any vulnerable
  application**, but we advise updating regardless.

Fixed in
========

Unfortunately, the bug is present in inlined code, therefore the fix will
require rebuilding dependent applications.

C++ fix:

- git commit [25d34c67863fd960af34fc4f82a7ca3362ee74b9][0]
- release 0.11 (future)
- release 0.10.3:
  - Unix: https://capnproto.org/capnproto-c++-0.10.3.tar.gz
  - Windows: https://capnproto.org/capnproto-c++-win32-0.10.3.zip
- release 0.9.2:
  - Unix: https://capnproto.org/capnproto-c++-0.9.2.tar.gz
  - Windows: https://capnproto.org/capnproto-c++-win32-0.9.2.zip
- release 0.8.1:
  - Unix: https://capnproto.org/capnproto-c++-0.8.1.tar.gz
  - Windows: https://capnproto.org/capnproto-c++-win32-0.8.1.zip
- release 0.7.1:
  - Unix: https://capnproto.org/capnproto-c++-0.7.1.tar.gz
  - Windows: https://capnproto.org/capnproto-c++-win32-0.7.1.zip

Rust fix:

- `capnp` crate version `0.15.2`, `0.14.11`, or `0.13.7`.

[0]: https://github.com/capnproto/capnproto/commit/25d34c67863fd960af34fc4f82a7ca3362ee74b9

Details
=======

A specially-crafted pointer could escape bounds checking by exploiting
inconsistent handling of pointers when a list-of-structs is downgraded to a
list-of-pointers.

For an in-depth explanation of how this bug works, see [David Renshaw's
blog post][1]. This details below focus only on determining whether an
application is vulnerable.

In order to be vulnerable, an application must have certain properties.

First, the application must accept messages with a schema in which a field has
list-of-pointer type. This includes `List(Text)`, `List(Data)`,
`List(List(T))`, or `List(C)` where `C` is an interface type. In the following
discussion, we will assume this field is named `foo`.

Second, the application must accept a message of this schema from a malicious
source, where the attacker can maliciously encode the pointer representing the
field `foo`.

Third, the application must call `getFoo()` to obtain a `List<T>::Reader` for
the field, and then use it in one of the following two ways:

1. Pass it as the parameter to another message's `setFoo()`, thus copying the
   field into a new message. Note that copying the parent struct as a whole
   will *not* trigger the bug; the bug only occurs if the specific field `foo`
   is get/set on its own.

2. Convert it into `AnyList::Reader`, and then attempt to access it through
   that. This is much less likely; very few apps use the `AnyList` API.

The dynamic API equivalents of these actions (`capnp/dynamic.h`) are also
affected.

If the application does these steps, the attacker may be able to cause the
Cap'n Proto implementation to read beyond the end of the message. This could
induce a segmentation fault. Or, worse, data that happened to be in memory
immediately after the message might be returned as if it were part of the
message. In the latter case, if the application then forwards that data back
to the attacker or sends it to another third party, this could result in
exfiltration of secrets.

Any exfiltration of data would have the following limitations:

* The attacker could exfiltrate no more than 512 KiB of memory immediately
  following the message buffer.
  * The attacker chooses in advance how far past the end of the message to
    read.
  * The attacker's message itself must be larger than the exfiltrated data.
    Note that a sufficiently large message buffer will likely be allocated
    using mmap() in which case the attack will likely segfault.
* The attack can only work if the 8 bytes immediately following the
  exfiltrated data contains a valid in-bounds Cap'n Proto pointer. The
  easiest way to achieve this is if the pointer is null, i.e. 8 bytes of zero.
  * The attacker must specify exactly how much data to exfiltrate, so must
    guess exactly where such a valid pointer will exist.
  * If the exfiltrated data is not followed by a valid pointer, the attack
    will throw an exception. If an application has chosen to ignore exceptions
    (e.g. by compiling with `-fno-exceptions` and not registering an
    alternative exception callback) then the attack may be able to proceed
    anyway.

[1]: https://dwrensha.github.io/capnproto-rust/2022/11/30/out_of_bounds_memory_access_bug.html
