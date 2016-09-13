---
layout: post
title: Cap'n Proto, FlatBuffers, and SBE
author: kentonv
---

**Update Jun 18, 2014:** I have made [some corrections](https://github.com/kentonv/capnproto/commit/e4e6c9076ae16804c07968cd3bdf6107155df7ee) since the original version of this post.

**Update Dec 15, 2014:** Updated to reflect that Cap'n Proto 0.5 now supports Visual Studio and that
Java is now well-supported.

Yesterday, some engineers at Google released [FlatBuffers](http://google-opensource.blogspot.com/2014/06/flatbuffers-memory-efficient.html), a new serialization protocol and library with similar design principles to Cap'n Proto. Also, a few months back, Real Logic released [Simple Binary Encoding](http://mechanical-sympathy.blogspot.com/2014/05/simple-binary-encoding.html), another protocol and library of this nature.

It seems we now have some friendly rivalry. :)

It's great to see that the concept of `mmap()`-able, zero-copy serialization formats are catching on, and it's wonderful that all are open source under liberal licenses. But as a user, you might be left wondering how all these systems compare. You have a vague idea that all these encodings are "fast", particularly compared to Protobufs or other more-traditional formats. But there is more to a serialization protocol than speed, and you may be wondering what else you should be considering.

The goal of this blog post is to highlight some of the main _qualitative_ differences between these libraries as I see them. Obviously, I am biased, and you should consider that as you read. Hopefully, though, this provides a good starting point for your own investigation of the alternatives.

### Feature Matrix

The following are a set of considerations I think are important. See something I missed? Please [let me know](mailto:kenton@sandstorm.io) and I'll add it. I'd like in particular to invite the SBE and FlatBuffers authors to suggest advantages of their libraries that I may have missed.

I will go into more detail on each item below.

Note: For features which are properties of the implementation rather than the protocol or project, unless otherwise stated, I am judging the C++ implementations.

<table class="pass-fail">
<tr><td>Feature</td><td>Protobuf</td><td>Cap'n Proto</td><td>SBE</td><td>FlatBuffers</td></tr>
<tr><td>Schema evolution</td><td class="pass">yes</td><td class="pass">yes</td><td class="warn">caveats</td><td class="pass">yes</td></tr>
<tr><td>Zero-copy</td><td class="fail">no</td><td class="pass">yes</td><td class="pass">yes</td><td class="pass">yes</td></tr>
<tr><td>Random-access reads</td><td class="fail">no</td><td class="pass">yes</td><td class="fail">no</td><td class="pass">yes</td></tr>
<tr><td>Safe against malicious input</td><td class="pass">yes</td><td class="pass">yes</td><td class="pass">yes</td><td class="warn">opt-in upfront</td></tr>
<tr><td>Reflection / generic algorithms</td><td class="pass">yes</td><td class="pass">yes</td><td class="pass">yes</td><td class="pass">yes</td></tr>
<tr><td>Initialization order</td><td class="pass">any</td><td class="pass">any</td><td class="fail">preorder</td><td class="warn">bottom-up</td></tr>
<tr><td>Unknown field retention</td><td class="warn">removed<br>in proto3</td><td class="pass">yes</td><td class="fail">no</td><td class="fail">no</td></tr>
<tr><td>Object-capability RPC system</td><td class="fail">no</td><td class="pass">yes</td><td class="fail">no</td><td class="fail">no</td></tr>
<tr><td>Schema language</td><td class="pass">custom</td><td class="pass">custom</td><td class="warn">XML</td><td class="pass">custom</td></tr>
<tr><td>Usable as mutable state</td><td class="pass">yes</td><td class="fail">no</td><td class="fail">no</td><td class="fail">no</td></tr>
<tr><td>Padding takes space on wire?</td><td class="pass">no</td><td class="warn">optional</td><td class="fail">yes</td><td class="fail">yes</td></tr>
<tr><td>Unset fields take space on wire?</td><td class="pass">no</td><td class="fail">yes</td><td class="fail">yes</td><td class="pass">no</td></tr>
<tr><td>Pointers take space on wire?</td><td class="pass">no</td><td class="fail">yes</td><td class="pass">no</td><td class="fail">yes</td></tr>
<tr><td>C++</td><td class="pass">yes</td><td class="pass">yes (C++11)*</td><td class="pass">yes</td><td class="pass">yes</td></tr>
<tr><td>Java</td><td class="pass">yes</td><td class="pass">yes*</td><td class="pass">yes</td><td class="pass">yes</td></tr>
<tr><td>C#</td><td class="pass">yes</td><td class="pass">yes*</td><td class="pass">yes</td><td class="pass">yes*</td></tr>
<tr><td>Go</td><td class="pass">yes</td><td class="pass">yes</td><td class="fail">no</td><td class="pass">yes*</td></tr>
<tr><td>Other languages</td><td class="pass">lots!</td><td class="warn">6+ others*</td><td class="fail">no</td><td class="fail">no</td></tr>
<tr><td>Authors' preferred use case</td><td>distributed<br>computing</td><td><a href="https://sandstorm.io">platforms /<br>sandboxing</a></td><td>financial<br>trading</td><td>games</td></tr>
</table>

\* Updated Dec 15, 2014 (Cap'n Proto 0.5.0).

**Schema Evolution**

All four protocols allow you to add new fields to a schema over time, without breaking backwards-compatibility. New fields will be ignored by old binaries, and new binaries will fill in a default value when reading old data.

SBE, however, as far as I can tell from reading the code, does not allow you to add new variable-width fields inside of a sub-object (group), as it is the application's responsibility to explicitly iterate over every variable-width field when reading. When an old app not knowing about the new nested field fails to cover it, its buffer pointer will get out-of-sync. Variable-width fields can be added to the topmost object since they'll end up at the end of the message, so there's no need for old code to traverse past them.

**Zero-copy**

The central thesis of all three competitors is that data should be structured the same way in-memory and on the wire, thus avoiding costly encode/decode steps.

Protobufs represents the old way of thinking.

**Random-access reads**

Can you traverse the message content in an arbitrary order? Relatedly, can you `mmap()` in a large (say, 2GB) file -- where the entire file is one enormous serialized message -- then traverse to and read one particular field without causing the entire file to be paged in from disk?

Protobufs does not allow this because the entire file must be parsed upfront before any of the content can be used. Even with a streaming Protobuf parser (which most libraries don't provide), you would at least need to parse all data appearing before the bit you want. The Protobuf documentation recommends splitting large files up into many small pieces and implementing some other framing format that allows seeking between them, but this is left entirely up to the app.

SBE does not allow random access because the message tree is written in preorder with no information that would allow one to skip over an entire sub-tree. While the primitive fields within a single object can be accessed in random order, sub-objects must be traversed strictly in preorder. SBE apparently chose to design around this restriction because sequential memory access is faster than random access, therefore this forces application code to be ordered to be as fast as possible. Similar to Protobufs, SBE recommends using some other framing format for large files.

Cap'n Proto permits random access via the use of pointers, exactly as in-memory data structures in C normally do. These pointers are not quite native pointers -- they are relative rather than absolute, to allow the message to be loaded at an arbitrary memory location.

FlatBuffers permits random access by having each record store a table of offsets to all of the field positions, and by using pointers between objects like Cap'n Proto does.

**Safe against malicious input**

Protobufs is carefully designed to be resiliant in the face of all kinds of malicious input, and has undergone a security review by Google's world-class security team. Not only is the Protobuf implementation secure, but the API is explicitly designed to discourage security mistakes in application code. It is considered a security flaw in Protobufs if the interface makes client apps likely to write insecure code.

Cap'n Proto inherits Protocol Buffers' security stance, and is believed to be similarly secure. However, it has not yet undergone security review.

SBE's C++ library does bounds checking as of the resolution of [this bug](https://github.com/real-logic/simple-binary-encoding/issues/130).

*Update July 12, 2014:* FlatBuffers [now supports](https://github.com/google/flatbuffers/commit/a0b6ffc25b9a3c726a21e52d6453779265186dbd) performing an optional upfront verification pass over a message to ensure that all pointers are in-bounds. You must explicitly call the verifier, otherwise no bounds checking is performed. The verifier performs a pass over the entire message; it should be very fast, but it is O(n), so you lose the "random access" advantage if you are mmap()ing in a very large file. FlatBuffers is primarily designed for use as a format for static, trusted data files, not network messages.

**Reflection / generic algorithms**

_Update: I originally failed to discover that SBE and FlatBuffers do in fact have reflection APIs. Sorry!_

Protobuf provides a "reflection" interface which allows dynamically iterating over all the fields of a message, getting their names and other metadata, and reading and modifying their values in a particular instance. Cap'n Proto also supports this, calling it the "Dynamic API". SBE provides the "OTF decoder" API with the usual SBE restriction that you can only iterate over the content in order. FlatBuffers has the `Parser` API in `idl.h`.

Having a reflection/dynamic API opens up a wide range of use cases. You can write reflection-based code which converts the message to/from another format such as JSON -- useful not just for interoperability, but for debugging, because it is human-readable. Another popular use of reflection is writing bindings for scripting languages. For example, Python's Cap'n Proto implementation is simply a wrapper around the C++ dynamic API. Note that you can do all these things with types that are not even known at compile time, by parsing the schemas at runtime.

The down side of reflection is that it is generally very slow (compared to generated code) and can lead to code bloat. Cap'n Proto is designed such that the reflection APIs need not be linked into your app if you do not use them, although this requires statically linking the library to get the benefit.

**Initialization order**

When building a message, depending on how your code is organized, it may be convenient to have flexibility in the order in which you fill in the data. If that flexibility is missing, you may find you have to do extra bookkeeping to store data off to the side until its time comes to be added to the message.

Protocol Buffers is natually completely flexible in terms of initialization order because the mesasge is being built on the heap. There is no reason to impose restrictions. (Although, the C++ Protobuf library heavily encourages top-down building.)

All the zero-copy systems, though, have to use some form of arena allocation to make sure that the message is built in a contiguous block of memory that can be written out all at once. So, things get more complicated.

SBE specifically requires the message tree to be written in preorder (though, as with reads, the primitive fields within a single object can be initialized in arbitrary order).

FlatBuffers requires that you completely finish one object before you can start building the next, because the size of an object depends on its content so the amount of space needed isn't known until it is finalized. This also implies that FlatBuffer messages must be built bottom-up, starting from the leaves.

Cap'n Proto imposes no ordering constraints. The size of an object is known when it is allocated, so more objects can be allocated immediately. Messages are normally built top-down, but bottom-up ordering is supported through the "orphans" API.

**Unknown field retention?**

Say you read in a message, then copy one sub-object of that message over to a sub-object of a new message, then write out the new message. Say that the copied object was created using a newer version of the schema than you have, and so contains fields you don't know about. Do those fields get copied over?

This question is extremely important for any kind of service that acts as a proxy or broker, forwarding messages on to others. It can be inconvenient if you have to update these middlemen every time a particular backend protocol changes, when the middlemen often don't care about the protocol details anyway.

When Protobufs sees an unknown field tag on the wire, it stores the value into the message's `UnknownFieldSet`, which can be copied and written back out later. (UPDATE: Apparently, version 3 of Protocol Buffers, aka "proto3", removes this feature. I honestly don't know what they're thinking. This feature has been absolutely essential in many of Google's internal systems.)

Cap'n Proto's wire format was very carefully designed to contain just enough information to make it possible to recursively copy its target from one message to another without knowing the object's schema. This is why Cap'n Proto pointers contain bits to indicate if they point to a struct or a list and how big it is -- seemingly redundant information.

SBE and FlatBuffers do not store any such type information on the wire, and thus it is not possible to copy an object without its schema. (Note that, however, if you are willing to require that the sender sends its full schema on the wire, you can always use reflection-based code to effectively make all fields known. This takes some work, though.)

**Object-capability RPC system**

Cap'n Proto features an object-capability RPC system. While this article is not intended to discuss RPC features, there is an important effect on the serialization format: in an object-capability RPC system, references to remote objects must be a first-class type. That is, a struct field's type can be "reference to remote object implementing RPC interface Foo".

Protobufs, SBC, and FlatBuffers do not support this type. Note that it is _not_ sufficient to simply store a string URL, or define some custom struct to represent a reference, because a proper capability-based RPC system must be aware of all references embedded in any message it sends. There are many reasons for this requirement, the most obvious of which is that the system must export the reference or change its permissions to make it available to the receiver.

**Schema language**

Protobufs, Cap'n Proto, and FlatBuffers have custom, concise schema languages.

SBE uses XML schemas, which are verbose.

**Usable as mutable state**

Protobuf generated classes have often been (ab)used as a convenient way to store an application's mutable internal state. There's mostly no problem with modifying a message gradually over time and then serializing it when needed.

This usage pattern does not work well with any zero-copy serialization format because these formats must use arena-style allocation to make sure the message is built in contiguous memory. Arena allocation has the property that you cannot free any object unless you free the entire arena. Therefore, when objects are discarded, the memory ends up leaked until the message as a whole is destroyed. A long-lived message that is modified many times will thus leak memory.

**Padding takes space on wire?**

Does the protocol tend to write a lot of zero-valued padding bytes to the wire?

This is a problem with zero-copy protocols: fixed-width integers tend to have a lot of zeros in the high-order bits, and padding sometimes needs to be inserted for alignment. This padding can easily double or triple the size of a message.

Protocol Buffers avoids padding by encoding integers using variable widths, which is only possible given a separate encoding/decoding step.

SBE and FlatBuffers leave the padding in to achieve zero-copy.

Cap'n Proto normally leaves the padding in, but comes with a built-in option to apply a very fast compression algorithm called "packing" which aims only to deflate zeros. This algorithm tends to achieve similar sizes to Protobufs while still being faster (and _much_ faster than general-purpose compression). In this mode, however, Cap'n Proto is no longer zero-copy.

Note that Cap'n Proto's packing algorithm would be appropriate for SBE and FlatBuffers as well. Feel free to steal it. :)

**Unset fields take space on wire?**

If a field has not been explicitly assigned a value, will it take any space on the wire?

Protobuf encodes tag-value pairs, so it simply skips pairs that have not been set.

Cap'n Proto and SBE position fields at fixed offsets from the start of the struct. The struct is always allocated large enough for all known fields according to the schema. So, unused fields waste space. (But Cap'n Proto's optional packing will tend to compress away this space.)

FlatBuffers uses a separate table of offsets (the vtable) to indicate the position of each field, with zero meaning the field isn't present. So, unset fields take no space on the wire -- although they do take space in the vtable. vtables can apparently be shared between instances where the offsets are all the same, amortizing this cost.

Of course, all this applies to primitive fields and pointer values, not the sub-objects to which those pointers point. All of these formats elide sub-objects that haven't been initialized.

**Pointers take space on wire?**

Do non-primitive fields require storing a pointer?

Protobufs uses tag-length-value for variable-width fields.

Cap'n Proto uses pointers for variable-width fields, so that the size of the parent object is independent of the size of any children. These pointers take some space on the wire.

SBE requires variable-width fields to be embedded in preorder, which means pointers aren't necessary.

FlatBuffers also uses pointers, even though most objects are variable-width, possibly because the vtables only store 16-bit offsets, limiting the size of any one object. However, note that FlatBuffers' "structs" (which are fixed-width and not extensible) are stored inline (what Cap'n Proto calls a "struct', FlatBuffer calls a "table").

**Platform Support**

As of Dec 15, 2014, Cap'n Proto supports a superset of the languages supported by FlatBuffers and
SBE, but is still far behind Protocol Buffers.

While Cap'n Proto C++ is well-supported on POSIX platforms using GCC or Clang as their compiler,
Cap'n Proto has only limited support for Visual C++: the basic serialization library works, but
reflection and RPC do not yet work. Support will be expanded once Visual Studio's C++ compiler
completes support for C++11.

In comparison, SBE and FlatBuffers have reflection interfaces that work in Visual C++, though
neither one has built-in RPC. Reflection is critical for certain use cases, but the majority of
users won't need it.

(This section has been updated. When originally written, Cap'n Proto did not support MSVC at all.)

### Benchmarks?

I do not provide benchmarks. I did not provide them when I launched Protobufs, nor when I launched Cap'n Proto, even though I had some with nice numbers (which you can find in git). And I don't see any reason to start now.

Why? Because they would tell you nothing. I could easily construct a benchmark to make any given library "win", by exploiting the relative tradeoffs each one makes. I can even construct one where Protobufs -- supposedly infinitely slower than the others -- wins.

The fact of the matter is that the relative performance of these libraries depends deeply on the use case. To know which one will be fastest for _your_ project, you really need to benchmark them in _your_ project, end-to-end. No contrived benchmark will give you the answer.

With that said, my intuition is that SBE will probably edge Cap'n Proto and FlatBuffers on performance in the average case, due to its decision to forgo support for random access. Between Cap'n Proto and FlatBuffers, it's harder to say. FlatBuffers' vtable approach seems like it would make access more expensive, though its simpler pointer format may be cheaper to follow. FlatBuffers also appears to do a lot of bookkeeping at encoding time which could get costly (such as de-duping vtables), but I don't know how costly.

For most people, the performance difference is probably small enough that qualitative (feature) differences in the libraries matter more.

