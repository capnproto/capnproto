---
layout: page
title: Introduction
---

# Introduction

<img src='images/infinity-times-faster.png' style='width:334px; height:306px; float: right;'>

Cap'n Proto is an insanely fast data interchange format and capability-based RPC system. Think
JSON, except binary. Or think [Protocol Buffers](http://protobuf.googlecode.com), except faster.
In fact, in benchmarks, Cap'n Proto is INFINITY TIMES faster than Protocol Buffers.

This benchmark is, of course, unfair. It is only measuring the time to encode and decode a message
in memory. Cap'n Proto gets a perfect score because _there is no encoding/decoding step_. The Cap'n
Proto encoding is appropriate both as a data interchange format and an in-memory representation, so
once your structure is built, you can simply write the bytes straight out to disk!

**_But doesn't that mean the encoding is platform-specific?_**

NO! The encoding is defined byte-for-byte independent of any platform. However, it is designed to
be efficiently manipulated on common modern CPUs. Data is arranged like a compiler would arrange a
struct -- with fixed widths, fixed offsets, and proper alignment. Variable-sized elements are
embedded as pointers. Pointers are offset-based rather than absolute so that messages are
position-independent. Integers use little-endian byte order because most CPUs are little-endian,
and even big-endian CPUs usually have instructions for reading little-endian data.

**_Doesn't that make backwards-compatibility hard?_**

Not at all! New fields are always added to the end of a struct (or replace padding space), so
existing field positions are unchanged. The recipient simply needs to do a bounds check when
reading each field. Fields are numbered in the order in which they were added, so Cap'n Proto
always knows how to arrange them for backwards-compatibility.

**_Won't fixed-width integers, unset optional fields, and padding waste space on the wire?_**

Yes. However, since all these extra bytes are zeros, when bandwidth matters, we can apply an
extremely fast Cap'n-Proto-specific compression scheme to remove them. Cap'n Proto calls this
"packing" the message; it achieves similar (better, even) message sizes to protobuf encoding, and
it's still faster.

When bandwidth really matters, you should apply general-purpose compression, like
[zlib](http://www.zlib.net/) or [LZ4](https://github.com/Cyan4973/lz4), regardless of your
encoding format.

**_Isn't this all horribly insecure?_**

No no no! To be clear, we're NOT just casting a buffer pointer to a struct pointer and calling it a day.

Cap'n Proto generates classes with accessor methods that you use to traverse the message. These accessors validate pointers before following them. If a pointer is invalid (e.g. out-of-bounds), the library can throw an exception or simply replace the value with a default / empty object (your choice).

Thus, Cap'n Proto checks the structural integrity of the message just like any other serialization protocol would. And, just like any other protocol, it is up to the app to check the validity of the content.

Cap'n Proto was built to be used in [Sandstorm.io](https://sandstorm.io), where security is a major concern. As of this writing, Cap'n Proto has not undergone a security review, therefore we suggest caution when handling messages from untrusted sources. That said, our response to security issues was once described by security guru Ben Laurie as ["the most awesome response I've ever had."](https://twitter.com/BenLaurie/status/575079375307153409) (Please report all security issues to [security@sandstorm.io](mailto:security@sandstorm.io).)

**_Are there other advantages?_**

Glad you asked!

* **Incremental reads:** It is easy to start processing a Cap'n Proto message before you have
  received all of it since outer objects appear entirely before inner objects (as opposed to most
  encodings, where outer objects encompass inner objects).
* **Random access:** You can read just one field of a message without parsing the whole thing.
* **mmap:** Read a large Cap'n Proto file by memory-mapping it. The OS won't even read in the
  parts that you don't access.
* **Inter-language communication:** Calling C++ code from, say, Java or Python tends to be painful
  or slow. With Cap'n Proto, the two languages can easily operate on the same in-memory data
  structure.
* **Inter-process communication:** Multiple processes running on the same machine can share a
  Cap'n Proto message via shared memory. No need to pipe data through the kernel. Calling another
  process can be just as fast and easy as calling another thread.
* **Arena allocation:** Manipulating Protobuf objects tends to be bogged down by memory
  allocation, unless you are very careful about object reuse. Cap'n Proto objects are always
  allocated in an "arena" or "region" style, which is faster and promotes cache locality.
* **Tiny generated code:** Protobuf generates dedicated parsing and serialization code for every
  message type, and this code tends to be enormous. Cap'n Proto generated code is smaller by an
  order of magnitude or more.  In fact, usually it's no more than some inline accessor methods!
* **Tiny runtime library:** Due to the simplicity of the Cap'n Proto format, the runtime library
  can be much smaller.
* **Time-traveling RPC:** Cap'n Proto features an RPC system that implements [time travel](rpc.html)
  such that call results are returned to the client before the request even arrives at the server!

<a href="rpc.html"><img src='images/time-travel.png' style='max-width:639px'></a>


**_Why do you pick on Protocol Buffers so much?_**

Because it's easy to pick on myself. :) I, Kenton Varda, was the primary author of Protocol Buffers
version 2, which is the version that Google released open source. Cap'n Proto is the result of
years of experience working on Protobufs, listening to user feedback, and thinking about how
things could be done better.

Note that I no longer work for Google. Cap'n Proto is not, and never has been, affiliated with Google; in fact, it is a property of [Sandstorm.io](https://sandstorm.io), of which I am co-founder.

**_OK, how do I get started?_**

To install Cap'n Proto, head over to the [installation page](install.html).  If you'd like to help
hack on Cap'n Proto, such as by writing bindings in other languages, let us know on the
[discussion group](https://groups.google.com/group/capnproto).  If you'd like to receive e-mail
updates about future releases, add yourself to the
[announcement list](https://groups.google.com/group/capnproto-announce).

{% include buttons.html %}
