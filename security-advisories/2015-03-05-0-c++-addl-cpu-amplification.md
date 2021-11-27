Problem
=======

CPU usage amplification attack, similar to previous vulnerability
[2015-03-02-2][1].

Discovered by
=============

David Renshaw &lt;david@sandstorm.io>

Announced
=========

2015-03-05

CVE
===

CVE-2015-2313

Impact
======

- Remotely cause a peer to execute a tight `for` loop counting from 0 to
  2^29, possibly repeatedly, by sending it a small message. This could enable
  a DoS attack by consuming CPU resources.

Fixed in
========

- git commit [80149744bdafa3ad4eedc83f8ab675e27baee868][0]
- release 0.5.1.2:
  - Unix: https://capnproto.org/capnproto-c++-0.5.1.2.tar.gz
  - Windows: https://capnproto.org/capnproto-c++-win32-0.5.1.2.zip
- release 0.4.1.1:
  - Unix: https://capnproto.org/capnproto-c++-0.4.1.2.tar.gz
- release 0.6 (future)

[0]: https://github.com/capnproto/capnproto/commit/80149744bdafa3ad4eedc83f8ab675e27baee868

Details
=======

Advisory [2015-03-02-2][1] described a bug allowing a remote attacker to
consume excessive CPU time or other resources using a specially-crafted message.
The present advisory is simply another case of the same bug which was initially
missed.

The new case occurs only if the application invokes the `totalSize()` method
on an object reader.

The new case is somewhat less severe, in that it only spins in a tight `for`
loop that doesn't call any application code. Only CPU time is possibly
consumed, not RAM or other resources. However, it is still possible to create
significant delays for the receiver with a specially-crafted message.

[1]: https://github.com/capnproto/capnproto/blob/master/security-advisories/2015-03-02-2-all-cpu-amplification.md

Preventative measures
=====================

Our fuzz test actually covered this case, but we didn't notice the problem
because the loop actually completes in less than a second. We've added a new
test case which is more demanding, and will make sure that when we do extended
testing with American Fuzzy Lop, we treat unexpectedly long run times as
failures.
