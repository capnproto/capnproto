---
layout: post
title: "Another security advisory -- Additional CPU amplification case"
author: kentonv
---

Unfortunately, it turns out that our fix for one of [the security advisories issued on Monday](2015-03-02-security-advisory-and-integer-overflow-protection.html) was not complete.

Fortunately, the incomplete fix is for the non-critical vulnerability. The worst case is that an attacker could consume excessive CPU time.

Nevertheless, we've issued [a new advisory](https://github.com/sandstorm-io/capnproto/tree/master/security-advisories/2015-03-05-0-c++-addl-cpu-amplification.md) and pushed a new release:

- Release 0.5.1.2: [source](https://capnproto.org/capnproto-c++-0.5.1.2.tar.gz), [win32](https://capnproto.org/capnproto-c++-win32-0.5.1.2.zip)
- Release 0.4.1.2: [source](https://capnproto.org/capnproto-c++-0.4.1.2.tar.gz)

Sorry for the rapid repeated releases, but we don't like sitting on security bugs.
