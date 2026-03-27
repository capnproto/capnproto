Problem
=======

The KJ-HTTP library was discovered to have two bugs related to integer overflows while handling message body sizes:
1. A negative `Content-Length` value was converted to unsigned, treating it as an impossibly large length instead.
2. When using `Transfer-Encoding: chunked`, if a chunk's size parsed to a value of 2^64 or larger, it would be truncated to a 64-bit integer.

In theory, these bugs could enable HTTP request/response smuggling.

While these are two distinct bugs, their impact is very similar, so we chose to group them together.

Discovered by
=============

Chanho Kim ([@HO-9](https://github.com/HO-9))

Jihyeok Han ([@HanJeouk](https://github.com/HanJeouk))

Announced
=========

2026-03-12

CVE
===

CVE-2026-32239 for the negative content-length bug.

CVE-2026-32240 for the chunk size overflow bug.

(A CVE is required to reference a single bug. Given the similarity in scope and impact of these bugs, we feel it makes more sense to treat them as a single advisory, but rules are rules.)

Impact
======

This only impacts users of KJ-HTTP. KJ is a C++ toolkit library developed alongside and distributed together with Cap'n Proto. KJ-HTTP is a separate add-on library. Cap'n Proto itself does not use nor even link against the KJ-HTTP library. If your application only uses Cap'n Proto and does not directly use KJ-HTTP, then you are not affected.

The biggest known user of KJ-HTTP is the Cloudflare Workers Runtime. The Cloudflare Workers Runtime team is also the maintainer of Cap'n Proto. We have determined that these bugs do not impact Workers in Cloudflare's production environment. They could, however, impact users of [workerd](https://github.com/cloudflare/workerd), the open source version of the Workers Runtime -- if paired with the right kind of proxy, as described below.

In order for smuggling to be possible, a client or server built on KJ-HTTP would have to be communicating through some sort of HTTP proxy which exhibits its own buggy behavior. The proxy would have to either:
* Accept negative values for `Content-Length`, and pass them through unmodified. This would be clearly buggy behavior on the part of the proxy.
* Accept chunk sizes of 2^64 or greater, and pass them through unmodified. This is arguably buggy behavior, unless the proxy is actually able to handle such chunk sizes without overflowing itself, which would be surprising.

It's worth noting that a proxy built on KJ-HTTP itself would not exhibit either of these behaviors. Due to the way KJ-HTTP works, it would end up rewriting either an invalid `Content-Length` or an invalid chunk size to the value it actually perceived. If it then forwarded this on to another KJ-based server, the second server would NOT be vulnerable to request/response smuggling as a result. Although the message content would be incorrect, the proxy and final server would agree upon the incorrect value, and hence smuggling would not be made possible.

We are unsure if any proxy exists which would actually forward the invalid sizes without rewriting them, but it seems very possible. It can be hard to tell for sure. We therefore recommend all users of KJ-HTTP update immediately.

Fixed in
========

- git commits:
  - `master` (1.x) branch: [2744b3c012b4aa3c31cefb61ec656829fa5c0e36][1]
  - `v2` branch: [e929f0ba7901a6b8f4b5ba9a4db00af43288cbb0][2]
- release 1.4.0:
  - Unix: https://capnproto.org/capnproto-c++-1.4.0.tar.gz
  - Windows: https://capnproto.org/capnproto-c++-win32-1.4.0.zip

[1]: https://github.com/capnproto/capnproto/commit/2744b3c012b4aa3c31cefb61ec656829fa5c0e36
[2]: https://github.com/capnproto/capnproto/commit/e929f0ba7901a6b8f4b5ba9a4db00af43288cbb0
