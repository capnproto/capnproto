---
layout: page
title: FAQ
---

# FAQ

## Design

### Isn't I/O bandwidth more important than CPU usage?  Is Cap'n Proto barking up the wrong tree?

It depends.  What is your use case?

Are you communicating between two processes on the same machine?  If so, you have unlimited
bandwidth, and you should be entirely concerned with CPU.

Are you communicating between two machines within the same datacenter?  If so, it's unlikely that
you will saturate your network connection before your CPU.  Possible, but unlikely.

Are you communicating across the general internet?  In that case, bandwidth is probably your main
concern.  Luckily, Cap'n Proto lets you choose to enable "packing" in this case, achieving similar
encoding size to Protocol Buffers while still being faster.  And you can always add extra
compression on top of that.

### Have you considered building the RPC system on ZeroMQ?

ZeroMQ (and its successor, Nanomsg) is a powerful technology for distributed computing.  Its
design focuses on scenarios involving lots of stateless, fault-tolerant worker processes
communicating via various patterns, such as request/response, produce/consume, and
publish/subscribe.  For big data processing where armies of stateless nodes make sense, pairing
Cap'n Proto with ZeroMQ would be an excellent choice -- and this is easy to do today, as ZeroMQ
is entirely serialization-agnostic.

That said, Cap'n Proto RPC takes a very different approach.  Cap'n Proto's model focuses on
stateful servers interacting in complex, object-oriented ways.  The model is better suited to
tasks involving applications with many heterogeneous components and interactions between
mutually-distrusting parties.  Requests and responses can go in any direction.  Objects have
state and so two calls to the same object had best go to the same machine.  Load balancing and
fault tolerance is pushed up the stack, because without a large pool of homogeneous work there's
just no way to make them transparent at a low level.

Put concretely, you might build a search engine indexing pipeline on ZeroMQ, but an online
interactive spreadsheet editor would be better built on Cap'n Proto RPC.

(Actually, a distributed programming framework providing similar features to ZeroMQ could itself be
built on top of Cap'n Proto RPC.)

### Aren't messages that contain pointers a huge security problem?

Not at all.  Cap'n Proto bounds-checks each pointer when it is read and throws an exception or
returns a safe dummy value (your choice) if the pointer is out-of-bounds.

### So it's not that you've eliminated parsing, you've just moved it to happen lazily?

No.  Compared to Protobuf decoding, the time spent validating pointers while traversing a Cap'n
Proto message is negligible.

### I think I heard somewhere that capability-based security doesn't work?

This was a popular myth in security circles way back in the 80's and 90's, based on an incomplete
understanding of what capabilities are and how to use them effectively.  Read
[Capability Myths Demolished](http://zesty.ca/capmyths/usenix.pdf).  (No really, read it;
it's awesome.)

## Usage

### How do I make a field "required", like in Protocol Buffers?

You don't.  You may find this surprising, but the "required" keyword in Protocol Buffers turned
out to be a horrible mistake.

For background, in protocol buffers, a field could be marked "required" to indicate that parsing
should fail if the sender forgot to set the field before sending the message.  Required fields were
encoded exactly the same as optional ones; the only difference was the extra validation.

The problem with this is, validation is sometimes more subtle than that.  Sometimes, different
applications -- or different parts of the same application, or different versions of the same
application -- place different requirements on the same protocol.  An application may want to
pass around partially-complete messages internally.  A particular field that used to be required
might become optional.  A new use case might call for almost exactly the same message type, minus
one field, at which point it may make more sense to reuse the type than to define a new one.

A field declared required, unfortunately, is required everywhere.  The validation is baked into
the parser, and there's nothing you can do about it.  Nothing, that is, except change the field
from "required" to "optional".  But that's where the _real_ problems start.

Imagine a production environment in which two servers, Alice and Bob, exchange messages through a
message bus infrastructure running on a big corporate network.  The message bus parses each message
just to examine the envelope and decide how to route it, without paying attention to any other
content.  Often, messages from various applications are batched together and then split up again
downstream.

Now, at some point, Alice's developers decide that one of the fields in a deeply-nested message
commonly sent to Bob has become obsolete.  To clean things up, they decide to remove it, so they
change the field from "required" to "optional".  The developers aren't idiots, so they realize that
Bob needs to be updated as well.  They make the changes to Bob, and just to be thorough they
run an integration test with Alice and Bob running in a test environment.  The test environment
is always running the latest build of the message bus, but that's irrelevant anyway because the
message bus doesn't actually care about message contents; it only does routing.  Protocols are
modified all the time without updating the message bus.

Satisfied with their testing, the devs push a new version of Alice to prod.  Immediately,
everything breaks.  And by "everything" I don't just mean Alice and Bob.  Completely unrelated
servers are getting strange errors or failing to receive messages.  The whole data center has
ground to a halt and the sysadmins are running around with their hair on fire.

What happened?  Well, the message bus running in prod was still an older build from before the
protocol change.  And even though the message bus doesn't care about message content, it _does_
need to parse every message just to read the envelope.  And the protobuf parser checks the _entire_
message for missing required fields.  So when Alice stopped sending that newly-optional field, the
whole message failed to parse, envelope and all.  And to make matters worse, any other messages
that happened to be in the same batch _also_ failed to parse, causing errors in seemingly-unrelated
systems that share the bus.

Things like this have actually happened.  At Google.  Many times.

The right answer is for applications to do validation as-needed in application-level code.  If you
want to detect when a client fails to set a particular field, give the field an invalid default
value and then check for that value on the server.  Low-level infrastructure that doesn't care
about message content should not validate it at all.

Oh, and also, Cap'n Proto doesn't have any parsing step during which to check for required
fields.  :)

### How do I make a field optional?

Cap'n Proto has no notion of "optional" fields.

A primitive field always takes space on the wire whether you set it or not (although default-valued
fields will be compressed away if you enable packing).  Such a field can be made semantically
optional by placing it in a union with a `Void` field:

{% highlight capnp %}
union {
  age @0 :Int32;
  ageUnknown @1 :Void;
}
{% endhighlight %}

However, this field still takes space on the wire, and in fact takes an extra 16 bits of space
for the union tag.  A better approach may be to give the field a bogus default value and interpret
that value to mean "not present".

Pointer fields are a bit different.  They start out "null", and you can check for nullness using
the `hasFoo()` accessor.  You could use a null pointer to mean "not present".  Note, though, that
calling `getFoo()` on a null pointer returns the default value, which is indistinguishable from a
legitimate value, so checking `hasFoo()` is in fact the _only_ way to detect nullness.

### How do I resize a list?

Unfortunately, you can't.  You have to know the size of your list upfront, before you initialize
any of the elements.  This is an annoying side effect of arena allocation, which is a fundamental
part of Cap'n Proto's design:  in order to avoid making a copy later, all of the pieces of the
message must be allocated in a tightly-packed segment of memory, with each new piece being added
to the end.  If a previously-allocated piece is discarded, it leaves a hole, which wastes space.
Since Cap'n Proto lists are flat arrays, the only way to resize a list would be to discard the
existing list and allocate a new one, which would thus necessarily waste space.

In theory, a more complicated memory allocation algorithm could attempt to reuse the "holes" left
behind by discarded message pieces.  However, it would be hard to make sure any new data inserted
into the space is exactly the right size.  Fragmentation would result.  And the allocator would
have to do a lot of extra bookkeeping that could be expensive.  This would be sad, as arena
allocation is supposed to be cheap!

The only solution is to temporarily place your data into some other data structure (an
`std::vector`, perhaps) until you know how many elements you have, then allocate the list and copy.
On the bright side, you probably aren't losing much performance this way -- using vectors already
involves making copies every time the backing array grows.  It's just annoying to code.

Keep in mind that you can use [orphans](cxx.html#orphans) to allocate sub-objects before you have
a place to put them.  But, also note that you cannot allocate elements of a struct list as orphans
and then put them together as a list later, because struct lists are encoded as a flat array of
struct values, not an array of pointers to struct values.  You can, however, allocate any inner
objects embedded within those structs as orphans.

## Security

### Is Cap'n Proto secure?

What is your threat model?

### Sorry. Can Cap'n Proto be used to deserialize malicious messages?

Cap'n Proto's serialization layer is designed to be safe against malicious input. The Cap'n Proto implementation should never segfault, corrupt memory, leak secrets, execute attacker-specified code, consume excessive resources, etc. as a result of any sequence of input bytes. Moreover, the API is carefully designed to avoid putting app developers into situations where it is easy to write insecure code -- we consider it a bug in Cap'n Proto if apps commonly misuse it in a way that is a security problem.

With all that said, Cap'n Proto's C++ reference implementation has not yet undergone a formal security review. It may have bugs.

### Is it safe to use Cap'n Proto RPC with a malicious peer?

Cap'n Proto's RPC layer is explicitly designed to be useful for interactions between mutually-distrusting parties. Its capability-based security model makes it easy to express complex interactions securely.

At this time, the RPC layer is not robust against resource exhaustion attacks, possibly allowing denials of service.

### Is Cap'n Proto encrypted?

Cap'n Proto may be layered on top of an existing encrypted transport, such as TLS, but at this time it is the application's responsibility to add this layer. We plan to integrate this into the Cap'n Proto library proper in the future.

### How do I report security bugs?

Please email [kenton@cloudflare.com](mailto:kenton@cloudflare.com).

## Sandstorm

### How does Cap'n Proto relate to Sandstorm.io?

[Sandstorm.io](https://sandstorm.io) is an Open Source project and startup founded by Kenton, the author of Cap'n Proto. Cap'n Proto was developed by Sandstorm the company and heavily used in Sandstorm the project. Sandstorm ceased most operations in 2017 and formally dissolved as a company in 2022, but the open source project continues to be developed by the community.

### How does Sandstorm use Cap'n Proto?

See [this Sandstorm blog post](https://blog.sandstorm.io/news/2014-12-15-capnproto-0.5.html).

## Cloudflare

### How does Cap'n Proto relate to Cloudflare?

[Cloudflare Workers](https://workers.dev) is a next-generation cloud application platform. Kenton, the author of Cap'n Proto, is the lead engineer on the Workers project. Workers heavily uses Cap'n Proto in its implementation, and the Cloudflare Workers team are now the primarily developers and maintainers of Cap'n Proto's primary C++ implementation.

### How does Cloudflare use Cap'n Proto?

The Cloudflare Workers runtime is built on Cap'n Proto and it's associated C++ toolkit library, KJ. Cap'n Proto is used for a variety of things, such as communication between sandbox processes and their supervisors, as well between machines and datacenters, especially in the implementation of [Durable Objects](https://blog.cloudflare.com/introducing-workers-durable-objects/).

Cloudflare has also [long used Cap'n Proto in its logging pipeline](http://www.thedotpost.com/2015/06/john-graham-cumming-i-got-10-trillion-problems-but-logging-aint-one) and [developed the Lua implementation of Cap'n Proto](https://blog.cloudflare.com/introducing-lua-capnproto-better-serialization-in-lua/) -- both of these actually predate Kenton joining the company.
