Problem
=======

Integer overflow in pointer validation.

Discovered by
=============

Ben Laurie &lt;ben@links.org> using [American Fuzzy Lop](http://lcamtuf.coredump.cx/afl/)

Announced
=========

2015-03-02

CVE
===

CVE-2015-2310

Impact
======

- Remotely segfault a peer by sending it a malicious message.
- Possible exfiltration of memory, depending on application behavior.

Fixed in
========

- git commit [f343f0dbd0a2e87f17cd74f14186ed73e3fbdbfa][0]
- release 0.5.1.1:
  - Unix: https://capnproto.org/capnproto-c++-0.5.1.1.tar.gz
  - Windows: https://capnproto.org/capnproto-c++-win32-0.5.1.1.zip
- release 0.4.1.1:
  - Unix: https://capnproto.org/capnproto-c++-0.4.1.1.tar.gz
- release 0.6 (future)

[0]: https://github.com/sandstorm-io/capnproto/commit/f343f0dbd0a2e87f17cd74f14186ed73e3fbdbfa

Details
=======

*The following text contains speculation about the exploitability of this
bug. This is provided for informational purposes, but as such speculation is
often shown to be wrong, you should not rely on the accuracy of this
section for the safety of your service. Please update your library.*

A specially-crafted pointer could escape bounds checking by triggering an
integer overflow in the check. This causes the message to appear as if it
contains an extremely long list (over 2^32 bytes), stretching far beyond the
memory actually allocated to the message. If the application reads that list,
it will likely segfault, but if it manages to avoid a segfault (e.g. because
it has mapped a very large contiguous block of memory following the message,
or because it only reads some parts of the list and not others), it could end
up treating arbitrary parts of memory as input. If the application happens to
pass that data back to the user in some way, this problem could lead to
exfiltration of secrets.

The pointer is transitively read-only, therefore it is believed that this
vulnerability on its own CANNOT lead to memory corruption nor code execution.

This vulnerability is NOT a Sandstorm sandbox breakout. A Sandstorm app's
Cap'n Proto communications pass through a supervisor process which performs a
deep copy of the structure. As the supervisor has a very small heap, this
will always lead to a segfault, which has the effect of killing the app, but
does not affect any other app or the system at large. If somehow the copy
succeeds, the copied message will no longer contain an invalid pointer and
so will not harm its eventual destination, and the supervisor itself has no
secrets to steal. These mitigations are by design.

Preventative measures
=====================

In order to gain confidence that this is a one-off bug rather than endemic,
and to help prevent new bugs from being added, we have taken / will take the
following preventative measures going forward:

1. A fuzz test of each pointer type has been added to the standard unit test
   suite. This test was confirmed to find the vulnerability in question.
2. We will additionally add fuzz testing with American Fuzzy Lop to our
   extended test suite. AFL was used to find the original vulnerability. Our
   current tests with AFL show only one other (less-critical) vulnerability
   which will be reported separately ([2015-03-02-2][2]).
3. In parallel, we will extend our use of template metaprogramming for
   compile-time unit analysis (kj::Quantity in kj/units.h) to also cover
   overflow detection (by tracking the maximum size of an integer value across
   arithmetic expressions and raising an error when it overflows). Preliminary
   work with this approach successfully detected the vulnerability reported
   here as well as one other vulnerability ([2015-03-02-1][3]).
   [See the blog post][4] for more details.
4. We will continue to require that all tests (including the new fuzz test) run
   cleanly under Valgrind before each release.
5. We will commission a professional security review before any 1.0 release.
   Until that time, we continue to recommend against using Cap'n Proto to
   interpret data from potentially-malicious sources.

I am pleased that measures 1, 2, and 3 all detected this bug, suggesting that
they have a high probability of catching any similar bugs.

[1]: https://github.com/sandstorm-io/capnproto/tree/master/security-advisories/2015-03-02-0-all-cpu-amplification.md
[2]: https://github.com/sandstorm-io/capnproto/tree/master/security-advisories/2015-03-02-1-c++-integer-underflow.md
[3]: https://capnproto.org/news/2015-03-02-security-advisory-and-integer-overflow-protection.html
