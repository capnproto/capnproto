# Cap'n Proto 1.5.0 security release

Like many projects, in recent months, Cap'n Proto has experienced an uptick in security reports, almost certainly driven by the use of AI to find vulnerabilities.

Mostly, I (Kenton) am happy about this. When a vulnerability exists, I would always rather it be found, reported, and fixed, than left unfound. AI is making our code more secure.

However, some practical details did not go as well as they could have. Several reports did not follow responsible disclosure -- instead, the reporter simply posted issues or PRs in public, often with no prominent indication that they were even security-related. Unfortunately, many of these also came during a time when, for unrelated reasons, I was too swamped to keep up with my GitHub notifications, and several of these went unreviewed for weeks. This is not a great state of affairs, and mostly my fault.

In the past, I have always issued a separate security advisory with a CVE for each and every bug. However, this is time-consuming, and I just don't have the bandwidth to keep up. Therefore, given the quantity of bugs reported, I have opted to issue a single combined security advisory for this release.

According to CVE rules, each distinct bug must have a separate CVE. Roll-ups are not allowed. Unfortunately, this means I cannot request a CVE for this advisory.

Cap'n Proto is de facto maintained by the Cloudflare Workers Runtime team (of which I'm the lead engineer), but it is de jure owned by me personally. Historically this has meant that releases and security advisories were my responsibility, which historically was not a problem as they didn't take a lot of time. This has clearly changed, and we may have to rethink how we handle things going forward.

## KJ HTTP issues

### http-over-capnp forwarding to plain HTTP does insufficient validation

**Reported by:** Kevin Stubbings <kwstubbs@github.com>, [GitHub Security Lab](https://securitylab.github.com/) (GHSL-2026-146)

**Fixed in:**
1.x release: c8a6c4f8385f76e309d8570d498414520936d020
v2 branch: ef279a63d517993744fb776bec23f7b9c638bba3

**Impact:** A proxy which speaks http-over-capnp to untrusted parties on one end and regular HTTP on the other could be vulnerable to HTTP desync / request smuggling.

**Mitigating factors:** You probably aren't using http-over-capnp. It is an undocumented add-on feature. It is not included in either the standard (autotools) build or the cmake build, only the Bazel build.

**Details:**

Cap'n Proto's http-over-capnp protocol defines a Cap'n Proto RPC interface isomorphic to the HTTP protocol. This enables an application to forward HTTP requests across a Cap'n Proto session, multiplexed with regular Cap'n Proto RPC.

The http-over-capnp implementation is designed to be interchangeable with KJ's real HTTP protocol implementation, such that you can easily switch between http-over-capnp vs. real HTTP, and also easily forward requests from one to the other.

The RPC protocol represents various parts of the HTTP protocol (request URLs, response status text, header names and values) as Cap'n Proto strings, which can contain arbitrary bytes. Of course, the actual HTTP protocol does not support arbitrary bytes in these strings; in particular, they cannot contain newline characters, as this would be interpreted as the end of the value and the start of a new header.

KJ's HTTP APIs were designed to assume that it is the caller's responsibility to pass valid values. Meanwhile, http-over-capnp treated these values as arbitrary strings. The result is if you had a server which proxies http-over-capnp to regular HTTP, and the peer on the other end of the http-over-capnp connection was not trusted, that peer could send invalid values that could lead to request smuggling on the plain-HTTP connection.

http-over-capnp is an obscure feature which has not been promoted much. We are only aware of one user: Cloudflare Workers' edge runtime. In Workers' use, http-over-capnp is currently not exposed to any untrusted peer. Hence, this is not exploitable in Workers.

The bug was fixed by hardening KJ's plain HTTP APIs to detect and reject invalid values. In fact, such hardening had already been done previously for header names and values; this has been extended to all metadata strings. This fixes the class of problems of application code that passes user-controlled values into these APIs unchecked.

### KJ HTTP skipped whitespace around header names instead of rejecting

**Reported by:** Shivam Singh <shivams0099@gmail.com>

**Fixed in:**
1.x release: 69a587156a5f505307fec0ae5ad828f3c3244db5
v2 branch: 9eff37c05a58d6a493e447dceffb98549b4ec879

**Impact:** If KJ-HTTP communicates with a peer HTTP implementation that incorrectly interprets spaces around a header name as being part of the header name itself, it may be possible to trigger HTTP desync / request smuggling.

**Mitigating factors:** Although KJ's behavior is against the spec, it would only lead to a vulnerability when paired with a peer HTTP implementation that is buggy in a different way. We are not aware of any such implementation.

**Details:**

KJ's header parser would ignore space characters between a header name and the colon character. For example:

```
Content-Length : 0
```

[RFC 9112 says](https://www.rfc-editor.org/rfc/rfc9112.html#section-5.1) that this header should be rejected with a 400 "Bad Request" error.

KJ, however, would parse this as the header `Content-Length`, value `0`. Historically, there have existed HTTP implementations which would parse the name as `Content-Length `, i.e. including the trailing space. If paired with such an implementation, this could lead to desync.

KJ now correctly responds with 400 Bad Request.

### KJ HTTP supported obsolete header folding

**Reported by:** 73Lab of Qingteng.cn

**Fixed in:**
1.x release: 08ddc350271ae520d83d806de8e46610cbf4f428
v2 branch: 489cc30aaa2d545de4173f03617f969c4f07604b

**Impact:** If KJ-HTTP communicates with a peer HTTP implementation that incorrectly implements header folding, it may be possible to trigger HTTP desync / request smuggling.

**Mitigating factors:** This is not a vulnerability, but rather a risk factor for exposing vulnerabilities in peer HTTP implementations. We are not aware of any peer HTTP implementation which is vulnerable today.

**Details:**

Historically, the HTTP protocol specified that header values could be wrapped over multiple lines, provided that continuation lines begin with a space, like:

```
Some-Header: A really long
  header value
```

The original HTTP/1.1 spec (RFC 2616) said that the line wrapping should be treated as whitespace, i.e. the above is equivalent to:

```
Some-Header: A really long header value
```

Historically, several HTTP implementations did not handle folding correctly, and would instead treat lines with leading whitespace as regular header lines. This meant that a header could be stashed within a folded line, causing it to be interpreted only by incorrect HTTP implementations, possibly leading to disagreement when a request or response passes through proxies.

In newer versions of the HTTP spec (e.g. RFC 9112), folding has been deprecated. [The spec states](https://www.rfc-editor.org/rfc/rfc9112.html#name-obsolete-line-folding):

> A server that receives an obs-fold in a request message that is not within a "message/http" container MUST either reject the message by sending a 400 (Bad Request), preferably with a representation explaining that obsolete line folding is unacceptable, or replace each received obs-fold with one or more SP octets prior to interpreting the field value or forwarding the message downstream.

KJ complies with this, in that it handles folding by rewriting the CRLF characters to spaces before delivering the value to the application.

However, since header folding is never legitimately used in the wild, it would be safer to implement the RFC's first suggestion: reject it with 400 Bad Request.

Hence, KJ now does that.

### Integer overflow in tryParseHttpRangeHeader()

**Reported by:** [@sahvx655-wq](https://github.com/sahvx655-wq) ([PR #2688](https://github.com/capnproto/capnproto/pull/2688))

**Fixed in:**
1.x release: N/A -- bug not present
v2 branch: 3542d53dea15ba1e30dc955d5929ab2cdecb09e1

**Impact:** The helper function tryParseHttpRangeHeader() truncated range endpoints to 32 bits.

**Mitigating factors:** tryParseHttpRangeHeader() is a freestanding helper function, not used directly by anything else -- your application would have to be calling it explicitly. This function is new in the v2 branch; it was never in a 1.x release. The bug would cause servers to return the wrong byte range for a request, but probably isn't an exploitable security bug.

**Details:**

`tryParseHttpRangeHeader()` is a helper function offered to applications to help them parse HTTP `Range` headers. It incorrectly parsed byte ranges using 32-bit integers instead of 64-bit. This is not a security bug per se, but would potentially result in a server returning the wrong range of bytes, which could have all sorts of downstream effects.

## Cap'n Proto schema issues

### Crash when parsing invalid binary schema

**Reported by:** Ben Liderman <benldrmn@gmail.com>

**Fixed in:**
1.x release: 1905352e0a4d795425774626320f8de879722f62
v2 branch: 915de588ea69cf668c390e5cfa8ed438eb202091

**Impact:** A maliciously-crafted binary schema ([`schema.capnp`](https://github.com/capnproto/capnproto/blob/v2/c++/src/capnp/schema.capnp) format) could trigger an infinite recursion bug causing a stack overflow, crashing the process.

**Mitigating factors:** It is unusual for applications to accept schemas at runtime. That said, this is intended to be supported.

**Details:**

Code in `SchemaLoader`, the library for loading binary schemas at runtime, contained this line:

```
KJ_CONTEXT("validating struct field", field.getName());
```

The `KJ_CONTEXT` macro adds context to any exception thrown later in the same scope. If no exception is thrown, it is a no-op. If an exception is thrown, then each of the parameters to `KJ_CONTEXT` is evaluated, converted to a string, and added to the exception message, with some formatting.

Unfortunately, if the process of evaluating these parameters itself throws an exception, that exception would also be considered within the scope of the `KJ_CONTEXT`, causing the context to be evaluated again. This led to an infinite recursive loop of throwing exceptions, eventually leading to a stack overflow.

An attacker could thus send a maliciously-crafted schema in which the `name` of a `Field` contained an invalid pointer, causing `field.getName()` to throw, leading to the recursive loop and crash.

`KJ_CONTEXT` has been fixed so that if exceptions are thrown while evaluating the context's parameters, the context is simply dropped.

### SchemaLoader insufficiently validates field offsets

**Reported by:** [@sahvx655-wq](https://github.com/sahvx655-wq) ([PR #2694](https://github.com/capnproto/capnproto/pull/2694))

**Fixed in:**
1.x release: 7eb4147955b3de3fc389d428cfd5d8f97872a59e
v2 branch: 985f7149833cbd17e9f84b0bf67494d31d692975

**Impact:** A maliciously-crafted binary schema ([`schema.capnp`](https://github.com/capnproto/capnproto/blob/v2/c++/src/capnp/schema.capnp) format) could define fields with offsets that overflowed 32-bit arithmetic. If the schemas were subsequently used by a `DynamicStruct::Builder` to write instances of the message, these invalid out-of-bounds offsets might be accessed, probably leading to a crash, though data corruption or leakage is theoretically possible.

**Mitigating factors:** Only affects apps that accept and manipulate untrusted schemas, which is unusual, though intended to be supported. The out-of-bounds access only happens when building a new message with the schema, not when reading a received message, due to additional bounds checking that occurs in the read path.

**Details:**

In the binary schema format, each field of a struct declares its own offset within the struct's data section. The validating code in `SchemaLoader` validates that these offsets are within the declared size of the struct. Unfortunately, the validation code used 32-bit arithmetic that could overflow with very large offset values, allowing the schema to pass validation despite containing out-of-bounds offsets.

These invalid schemas could then be fed into the `DynamicStruct` API to read and build instances of the struct. In the reader path (`DynamicStruct::Reader`), the out-of-bounds offsets were harmless because additional bounds checking occurs when each field is read. This read-time bounds checking exists because the received message is allowed to be smaller than the expected size, e.g. if it was created using an older version of the schema; out-of-bounds fields are assumed to have their default values. However, in the builder path (`DynamicStruct::Builder`), this bounds checking is skipped on the assumption that a freshly-created struct will always be allocated "big enough" for all the schema's fields.

### SchemaLoader off-by-one buffer overrun on `membersByDiscriminant`

**Reported by:** [@sahvx655-wq](https://github.com/sahvx655-wq) ([PR #2691](https://github.com/capnproto/capnproto/pull/2691))

**Fixed in:**
1.x release: 691582b5ecdda40ebf761ab67817abb852e9081b
v2 branch: 4064d56df6332cf54c6821ad1df09c74d25502d9

**Impact:** A maliciously-crafted binary schema ([`schema.capnp`](https://github.com/capnproto/capnproto/blob/v2/c++/src/capnp/schema.capnp) format) could define fields in such a way as to overwrite 16 bits past the end of a heap-allocated array.

**Mitigating factors:** Only affects apps that accept and manipulate untrusted schemas, which is unusual, though intended to be supported. The out-of-bounds write is limited to one 16-bit value. The schema will later be determined to be invalid, throwing an exception, but not until after the 16-bit overrun has occurred.

**Details:**

A bounds check in `SchemaValidator` used `<=` where it should have used `<`. See the PR for details.

### Stack overflow when parsing textual capnp schemas

**Reported by:** Ze Sheng [@OwenSanzas](https://github.com/OwenSanzas) ([issue #2608](https://github.com/capnproto/capnproto/issues/2608))

**Fixed in:**
1.x release: 679beae9d515b1f5f5fa860c94d305f78d11ac0b
v2 branch: 05e9bea2daf54adfc1ffbaaef6e06887685204e9

**Impact:** If you accept `.capnp` text schemas from untrusted sources and try to parse them, an attacker could crash your process.

**Mitigating factors:** We do not recommend accepting `.capnp` text format at runtime. Instead, we recommend using the binary schema format defined by [`schema.capnp`](https://github.com/capnproto/capnproto/blob/v2/c++/src/capnp/schema.capnp).

**Details:**

The parser for `.capnp` text format is a recursive descent parser with no depth limit. With a crafted input, it could end up overflowing the stack, crashing the process.

We don't recommend parsing untrusted `.capnp` inputs, but we have nevertheless implemented a limit on parsing depth to resolve this issue.

## Other Cap'n Proto issues

### Exception type enums received over RPC were not validated

**Reported by:** Dominik Picheta [@dom96](https://github.com/dom96) ([PR #2662](https://github.com/capnproto/capnproto/pull/2662))

**Fixed in:**
1.x release: 10163f15a5961ccae9e85c6ac64d47bd5d1ee47b
v2 branch: 4b6925d14e7f7a70b2b2bd4dfb6380f39f21e9d2

**Impact:** A maliciously-crafted RPC message could contain an out-of-bounds value for an exception's `type` enum, which might later lead to an out-of-bounds lookup into a table mapping types to names.

**Mitigating factors:** The enum value is 16 bits, limiting the out-of-bound access. The table is in the binary's data section. It's unlikely that anything outside the data section could be reached. Hence, it is unlikely that an attacker could leak secrets from memory. However, it is likely the attacker could cause a crash.

**Details:**

KJ exceptions have four "types", represented via an enum: `FAILED`, `OVERLOADED`, `DISCONNECTED`, and `UNIMPLEMENTED`.

On the wire, Cap'n Proto encodes enums as 16-bit values. There is no enforcement that a value on the wire matches one of the defined enumerants. Instead, in C++, the enum is defined with a backing type of `uint16_t`, and will simply contain whatever 16-bit value was received.

Unfortunately, the RPC system was missing validation when receiving an exception type enum received over the wire. It simply used `static_cast` to cast the Cap'n Proto enum to a `kj::Exception::Type` enum.

Other code expected that `kj::Exception::Type` values were always valid. In particular, the code to stringify an exception would map the type enum to a name via a string table:

```cpp
StringPtr KJ_STRINGIFY(Exception::Type type) {
  static const char* TYPE_STRINGS[] = {
    "failed",
    "overloaded",
    "disconnected",
    "unimplemented"
  };

  return TYPE_STRINGS[static_cast<uint>(type)];
}
```

This could lead to a crash if `type` were invalid.

In theory, if the lookup landed on a pointer to valid data, and the stringified exception were returned to the attacker, that data could be leaked. However, due to the enum value being limited to 16 bits, this lookup likely could not reach outside of the binary's data section, and so any valid pointer would most likely only point to compile-time constants, not sensitive data.

### Message size limit bypass on 32-bit systems

**Reported by:** [@sahvx655-wq](https://github.com/sahvx655-wq) ([PR #2696](https://github.com/capnproto/capnproto/pull/2696))

**Fixed in:**
1.x release: 3c09a05141a2ea739d7e92e8c0720999419f2574
v2 branch: 1609c43ec42c4e3a8debb910aae7cab717f1c16d

**Impact:** A maliciously-crafted message could overflow a size counter on 32-bit systems, tricking the app into reading message content from out-of-bounds locations.

**Mitigating factors:** Only affects 32-bit builds.

**Details:**

An encoded Cap'n Proto message begins with a "segment table", specifying the size of each "segment" of the message as a 32-bit integer. These integers are summed to determine the total size of the message. The sum is tracked in a variable of type `size_t`. On 32-bit systems, this is a 32-bit value, so could overflow.

The actual message size read from the underlying stream would be determined by the overflowed value. However, once read, the segment table would effectively reference memory addresses beyond the end of the actual message content. A maliciously-crafted message could incorporate data from arbitrary locations in memory as if they were part of the message. If the message were reflected back to the attacker, this could leak contents of memory to the attacker.
