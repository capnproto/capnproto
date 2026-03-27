Problem
=======

A maliciously-crafted message beginning with the four bytes 0xFFFFFFFF, when read by the asynchronous version of `capnp::readMessage()`, would lead to an integer overflow, in turn leading to a zero-byte allocation, which then had one pointer written to it.

This is technically undefined behavior (a buffer overrun), but we suspect that it is benign with all known memory allocators. In C++, a zero-sized allocation (made with `operator new(0)`, as is the case here) is required to return a unique pointer, different from any other such allocation. Because of this, all common memory allocators round up a zero-byte allocation to a word-sized allocation (32-bit or 64-bit, depending on the architecture). The overrun written to this allocation was exactly one pointer in size, so always fits into the actual allocation space.

Nevertheless, the code is in fact relying on undefined behavior, and it is theoretically possible that some memory allocator implements zero-sized allocations in a way that would make this overrun dangerous.

Discovered by
=============

Chanho Kim ([@HO-9](https://github.com/HO-9))

Jihyeok Han ([@HanJeouk](https://github.com/HanJeouk))

Announced
=========

2026-03-12

CVE
===

None: Because we believe this bug to have no impact in practice, we have chosen not to burden the CVE system with a CVE.

Impact
======

We believe there is no actual impact since, as described above, we were unable to find any common C++ memory allocator implementation that does not have a minimum allocation size of at least one pointer.

Hypothetically, however, a memory allocator could exist which handles zero-byte allocations in such a way that writing a pointer over them would be dangerous. The pointer value in question which would be written to this location ends up being a pointer into a buffer containing bytes sent by the attacker. Depending on the details of the memory allocator, this could be exploitable to cause a denial of service or possibly even remote code execution.

Fixed in
========

- git commits:
  - `master` (1.x) branch: [28731820130b3b06a658fa061cfc40a3917bceaa][1]
  - `v2` branch: [a93493ca46f563b9d2689917836a841c12ace557][2]
- release 1.4.0:
  - Unix: https://capnproto.org/capnproto-c++-1.4.0.tar.gz
  - Windows: https://capnproto.org/capnproto-c++-win32-1.4.0.zip

[1]: https://github.com/capnproto/capnproto/commit/28731820130b3b06a658fa061cfc40a3917bceaa
[2]: https://github.com/capnproto/capnproto/commit/a93493ca46f563b9d2689917836a841c12ace557
