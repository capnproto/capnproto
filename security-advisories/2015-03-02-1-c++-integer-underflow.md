Problem
=======

Integer underflow in pointer validation.

Discovered by
=============

Kenton Varda &lt;kenton@sandstorm.io>

Announced
=========

2015-03-02

CVE
===

CVE-2015-2311

Impact
======

- Remotely segfault a peer by sending it a malicious message.
- Possible exfiltration of memory, depending on application behavior.
- If the application performs a sequence of operations that "probably" no
  application does (see below), possible memory corruption / code execution.

Fixed in
========

- git commit [26bcceda72372211063d62aab7e45665faa83633][0]
- release 0.5.1.1:
  - Unix: https://capnproto.org/capnproto-c++-0.5.1.1.tar.gz
  - Windows: https://capnproto.org/capnproto-c++-win32-0.5.1.1.zip
- release 0.4.1.1:
  - Unix: https://capnproto.org/capnproto-c++-0.4.1.1.tar.gz
- release 0.6 (future)

[0]: https://github.com/capnproto/capnproto/commit/26bcceda72372211063d62aab7e45665faa83633

Details
=======

*The following text contains speculation about the exploitability of this
bug. This is provided for informational purposes, but as such speculation is
often shown to be wrong, you should not rely on the accuracy of this
section for the safety of your service. Please update your library.*

A `Text` pointer, when non-null, must point to a NUL-terminated string, meaning
it must have a size of at least 1. Under most circumstances, Cap'n Proto will
reject zero-size text objects. However, if an application performs the
following sequence, they may hit a code path that was missing a check:

1. Receive a message containing a `Text` value, but do not actually look at
   that value.
2. Copy the message into a `MessageBuilder`.
3. Call the `get()` method for the `Text` value within the `MessageBuilder`,
   obtaining a `Text::Builder` for the *copy*.

In this case, the `Text::Builder` will appear to point at a string with size
2^32-1, starting at a location within the Cap'n Proto message.

The `Text::Builder` is writable. If the application decided to overwrite the
text in-place, it could overwrite arbitrary memory in the next 4GB of virtual
address space. However, there are several reasons to believe this is unusual:

- Usually, when an application `get()`s a text field, it only intends to
  read it. Overwriting the text in-place is unusual.
- Calling `set()` on the field -- the usual way to overwrite text -- will
  create an all-new text object and harmlessly discard the old, invalid
  pointer.

Note that even if an application does overwrite the text, it would still be
hard for the attacker to exploit this for code execution unless the attacker
also controls the data that the application writes into the field.

This vulnerability is somewhat more likely to allow exfiltration of memory.
However, this requires the app to additionally echo the text back to the
attacker. To do this without segfaulting, the app would either need to attempt
to read only a subset of the text, or would need to have 2^32 contiguous bytes
of virtual memory mapped into its address space.

A related problem, also fixed in this change, occurs when a `Text` value
has non-zero size but lacks a NUL terminator. Again, if an application
performs the series of operations described above, the NUL terminator check
may be bypassed. If the app then passes the string to an API that assumes
NUL-terminated strings, the contents of memory after the text blob may be
interpreted as being part of the string, up to the next zero-valued byte.
This again could lead to exfiltration of data, this time without the high
chance of segfault, although only up to the next zero-valued byte, which
are typically quite common.

Preventative measures
=====================

This problem was discovered through preventative measures implemented after
the security problem discussed in the [previous advisory][1]. Specifically, this
problem was found by using template metaprogramming to implement integer
bounds analysis in order to effectively "prove" that there are no integer
overflows in the core pointer validation code (capnp/layout.c++).
Tentatively, I believe that this analysis exhaustively covers this file.
The instrumentation has not been merged into master yet as it requires some
cleanup, but [check the Cap'n Proto blog for an in-depth discussion][2].

This problem is also caught by capnp/fuzz-test.c++, which *has* been
merged into master but likely doesn't have as broad coverage.

[1]: https://github.com/capnproto/capnproto/tree/master/security-advisories/2015-03-02-0-c++-integer-overflow.md
[2]: https://capnproto.org/news/2015-03-02-security-advisory-and-integer-overflow-protection.html
