Problem
=======

When using the KJ HTTP library with WebSocket compression enabled, a buffer underrun can be caused by a remote peer. The underrun always writes a constant value that is not attacker-controlled, likely resulting in a crash, enabling a remote denial-of-service attack.

Most Cap'n Proto and KJ users are unlikely to have this functionality enabled and so unlikely to be affected. We suspect only [the Cloudflare Workers Runtime][0] is affected.

[0]: https://github.com/cloudflare/workerd

Discovered by
=============

Cloudflare Workers team

Announced
=========

2023-11-21

CVE
===

CVE-2023-48230

Impact
======

- If KJ HTTP is used with WebSocket compression enabled, a malicious peer may be able to cause a buffer underrun on a heap-allocated buffer.
- KJ HTTP is an optional library bundled with Cap'n Proto, but is not directly used by Cap'n Proto. If you haven't heard of it, you probably aren't using it.
- WebSocket compression is disabled by default. It must be enabled via a setting passed to the KJ HTTP library via `HttpClientSettings` or `HttpServerSettings`. We suspect no one uses this setting today except for Cloudflare Workers.
- The bytes written out-of-bounds are always a specific constant 4-byte string `{ 0x00, 0x00, 0xFF, 0xFF }`. Because this string is not controlled by the attacker, we suspect it is unlikely that remote code execution is possible. However, it cannot be ruled out.
- This functionality first appeared in Cap'n Proto 1.0. Previous versions are not affected.

Fixed in
========

C++ fix:

- git commits:
  - `master` (1.x) branch: [e7f22da9c01286a2b0e1e5fbdf3ec9ab3aa128ff][1]
  - `v2` branch: [75c5c1499aa6e7690b741204ff9af91cce526c59][2]
- release 1.0.1.1:
  - Unix: https://capnproto.org/capnproto-c++-1.0.1.1.tar.gz
  - Windows: https://capnproto.org/capnproto-c++-win32-1.0.1.1.zip

[1]: https://github.com/capnproto/capnproto/commit/e7f22da9c01286a2b0e1e5fbdf3ec9ab3aa128ff
[2]: https://github.com/capnproto/capnproto/commit/75c5c1499aa6e7690b741204ff9af91cce526c59

Details
=======

KJ is a C++ toolkit library that evolved along with the Cap'n Proto C++ implementation. While it originally contained only a few utilities used by Cap'n Proto itself, it has grown over time with quite a few features. One of the larger features is an HTTP/1.1 implementation, complete with WebSocket support. KJ's HTTP library is used by the Cloudflare Workers runtime.

In 2022, the Cloudflare Workers team added support for the WebSocket compression extension to KJ HTTP. The feature is enabled by default for Workers whose [compatibility date][3] is `2023-08-15` or later. That is, this feature only became enabled by default recently, and only for new Workers or Workers that updated their compatibility date.

A bug in the code caused it to improperly handle the case where compression had been negotiated, but a particular message received from the peer was not compressed, i.e. did not have the compression bit set in the frame header. The code would correctly allocate buffer space for a non-compressed message. However, later on, the code incorrectly treated the message as compressed, and therefore attempted to prefix it with the constant string `{ 0x00, 0x00, 0xFF, 0xFF }` which must be added before passing the bytes to zlib. Since the buffer was not allocated with space for these bytes, they would be written into the four bytes preceding the buffer instead.

In most cases, this had the effect of overwriting state maintained by the memory allocator, causing a subsequent memory allocation operation to crash.

Because the bytes are always a constant string, not attacker-controlled, it seems difficult to leverage this overrun to achieve code execution. However, we cannot rule out an excessively clever exploit.

The bug was discovered when it was triggered in production, and was quickly patched. The occurrences in production were benign, not an attack. It is normal for some WebSocket implementations to negotiate compression but then opportunistically skip it when it doesn't help, which would trigger the bug.

[3]: https://developers.cloudflare.com/workers/configuration/compatibility-dates/#websocket-compression
