---
layout: page
title: Encoding Spec
---

# Encoding Spec

## Organization

### 64-bit Words

For the purpose of Cap'n Proto, a "word" is defined as 8 bytes, or 64 bits.  Since alignment of
data is important, all objects (structs, lists, and blobs) are aligned to word boundaries, and
sizes are usually expressed in terms of words.  (Primitive values are aligned to a multiple of
their size within a struct or list.)

### Messages

The unit of communication in Cap'n Proto is a "message".  A message is a tree of objects, with
the root always being a struct.

Physically, messages may be split into several "segments", each of which is a flat blob of bytes.
Typically, a segment must be loaded into a contiguous block of memory before it can be accessed,
so that the relative pointers within the segment can be followed quickly.  However, when a message
has multiple segments, it does not matter where those segments are located in memory relative to
each other; inter-segment pointers are encoded differently, as we'll see later.

Ideally, every message would have only one segment.  However, there are a few reasons why splitting
a message into multiple segments may be convenient:

* It can be difficult to predict how large a message might be until you start writing it, and you
  can't start writing it until you have a segment to write to.  If it turns out the segment you
  allocated isn't big enough, you can allocate additional segments without the need to relocate the
  data you've already written.
* Allocating excessively large blocks of memory can make life difficult for memory allocators,
  especially on 32-bit systems with limited address space.

The first word of the first segment of the message is always a pointer pointing to the message's
root struct.

### Objects

Each segment in a message contains a series of objects.  For the purpose of Cap'n Proto, an "object"
is any value which may have a pointer pointing to it.  Pointers can only point to the beginning of
objects, not into the middle, and no more than one pointer can point at each object.  Thus, objects
and the pointers connecting them form a tree, not a graph.  An object is itself composed of
primitive data values and pointers, in a layout that depends on the kind of object.

At the moment, there are three kinds of objects:  structs, lists, and far-pointer landing pads.
Blobs might also be considered to be a kind of object, but are encoded identically to lists of
bytes.

## Value Encoding

### Primitive Values

The built-in primitive types are encoded as follows:

* `Void`:  Not encoded at all.  It has only one possible value thus carries no information.
* `Bool`:  One bit.  1 = true, 0 = false.
* Integers:  Encoded in little-endian format.  Signed integers use two's complement.
* Floating-points:  Encoded in little-endian IEEE-754 format.

Primitive types must always be aligned to a multiple of their size.  Note that since the size of
a `Bool` is one bit, this means eight `Bool` values can be encoded in a single byte -- this differs
from C++, where the `bool` type takes a whole byte.

### Enums

Enums are encoded the same as `UInt16`.

## Object Encoding

### Blobs

The built-in blob types are encoded as follows:

* `Data`:  Encoded as a pointer, identical to `List(UInt8)`.
* `Text`:  Like `Data`, but the content must be valid UTF-8, and the last byte of the content must
  be zero.  The encoding allows bytes other than the last to be zero, but some applications
  (especially ones written in languages that use NUL-terminated strings) may truncate at the first
  zero.  If a particular text field is explicitly intended to support zero bytes, it should
  document this, but otherwise senders should assume that zero bytes are not allowed to be safe.
  Note that the NUL terminator is included in the size sent on the wire, but the runtime library
  should not count it in any size reported to the application.

### Lists

A list value is encoded as a pointer to a flat array of values.

    lsb                       list pointer                        msb
    +-+-----------------------------+--+----------------------------+
    |A|             B               |C |             D              |
    +-+-----------------------------+--+----------------------------+

    A (2 bits) = 1, to indicate that this is a list pointer.
    B (30 bits) = Offset, in words, from the end of the pointer to the
        start of the first element of the list.  Signed.
    C (3 bits) = Size of each element:
        0 = 0 (e.g. List(Void))
        1 = 1 bit
        2 = 1 byte
        3 = 2 bytes
        4 = 4 bytes
        5 = 8 bytes (non-pointer)
        6 = 8 bytes (pointer)
        7 = composite (see below)
    D (29 bits) = Number of elements in the list, except when C is 7
        (see below).

The pointed-to values are tightly-packed.  In particular, `Bool`s are packed bit-by-bit in
little-endian order (the first bit is the least-significant bit of the first byte).

Lists of structs use the smallest element size in which the struct can fit.  So, a
list of structs that each contain two `UInt8` fields and nothing else could be encoded with C = 3
(2-byte elements).  A list of structs that each contain a single `Text` field would be encoded as
C = 6 (pointer elements).  A list of structs that each contain a single `Bool` field would be
encoded using C = 1 (1-bit elements).  A list structs which are each more than one word in size
must be be encoded using C = 7 (composite).

When C = 7, the elements of the list are fixed-width composite values -- usually, structs.  In
this case, the list content is prefixed by a "tag" word that describes each individual element.
The tag has the same layout as a struct pointer, except that the pointer offset (B) instead
indicates the number of elements in the list.  Meanwhile, section (D) of the list pointer -- which
normally would store this element count -- instead stores the total number of _words_ in the list
(not counting the tag word).  The reason we store a word count in the pointer rather than an element
count is to ensure that the extents of the list's location can always be determined by inspecting
the pointer alone, without having to look at the tag; this may allow more-efficient prefetching in
some use cases.  The reason we don't store struct lists as a list of pointers is because doing so
would take significantly more space (an extra pointer per element) and may be less cache-friendly.

In the future, we could consider implementing matrixes using the "composite" element type, with the
elements being fixed-size lists rather than structs.  In this case, the tag would look like a list
pointer rather than a struct pointer.  As of this writing, no such feature has been implemented.

Notice that because a small struct is encoded as if it were a primitive value, this means that
if you have a field of type `List(T)` where `T` is a primitive or blob type, it
is possible to change that field to `List(U)` where `U` is a struct whose `@0` field has type `T`,
without breaking backwards-compatibility.  This comes in handy when you discover too late that you
need to associate some extra data with each value in a primitive list -- instead of using parallel
lists (eww), you can just replace it with a struct list.

### Structs

A struct value is encoded as a pointer to its content.  The content is split into two sections:
data and pointers, with the pointer section appearing immediately after the data section.  This
split allows structs to be traversed (e.g., copied) without knowing their type.

A struct pointer looks like this:

    lsb                      struct pointer                       msb
    +-+-----------------------------+---------------+---------------+
    |A|             B               |       C       |       D       |
    +-+-----------------------------+---------------+---------------+

    A (2 bits) = 0, to indicate that this is a struct pointer.
    B (30 bits) = Offset, in words, from the end of the pointer to the
        start of the struct's data section.  Signed.
    C (16 bits) = Size of the struct's data section, in words.
    D (16 bits) = Size of the struct's pointer section, in words.

Fields are positioned within the struct according to an algorithm with the following principles:

* The position of each field depends only on its definition and the definitions of lower-numbered
  fields, never on the definitions of higher-numbered fields.  This ensures backwards-compatibility
  when new fields are added.
* Due to alignment reqirements, fields in the data section may be separated by padding.  However,
  later-numbered fields may be positioned into the padding left between earlier-numbered fields.
  Because of this, a struct will never contain more than 63 bits of padding.  Since objects are
  rounded up to a whole number of words anyway, padding never ends up wasting space.
* Unions and groups need not occupy contiguous memory.  Indeed, they may have to be split into
  multiple slots if new fields are added later on.

Field offsets are computed by the Cap'n Proto compiler.  The precise algorithm is too complicated
to describe here, but you need not implement it yourself, as the compiler can produce a compiled
schema format which includes offset information.

#### Default Values

A default struct is always all-zeros.  To achieve this, fields in the data section are stored xor'd
with their defined default values.  An all-zero pointer is considered "null" (such a pointer would
otherwise point to a zero-size struct, which might as well be considered null); accessor methods
for pointer fields check for null and return a pointer to their default value in this case.

There are several reasons why this is desirable:

* Cap'n Proto messages are often "packed" with a simple compression algorithm that deflates
  zero-value bytes.
* Newly-allocated structs only need to be zero-initialized, which is fast and requires no knowledge
  of the struct type except its size.
* If a newly-added field is placed in space that was previously padding, messages written by old
  binaries that do not know about this field will still have its default value set correctly --
  because it is always zero.

### Inter-Segment Pointers

When a pointer needs to point to a different segment, offsets no longer work.  We instead encode
the pointer as a "far pointer", which looks like this:

    lsb                        far pointer                        msb
    +-+-+---------------------------+-------------------------------+
    |A|B|            C              |               D               |
    +-+-+---------------------------+-------------------------------+

    A (2 bits) = 2, to indicate that this is a far pointer.
    B (1 bit) = 0 if the landing pad is one word, 1 if it is two words.
        See explanation below.
    C (29 bits) = Offset, in words, from the start of the target segment
        to the location of the far-pointer landing-pad within that
        segment.  Unsigned.
    D (32 bits) = ID of the target segment.  (Segments are numbered
        sequentially starting from zero.)

If B == 0, then the "landing pad" of a far pointer is normally just another pointer, which in turn
points to the actual object.

If B == 1, then the "landing pad" is itself another far pointer that is interpreted differently:
This far pointer (which always has B = 0) points to the start of the object's _content_, located in
some other segment.  The landing pad is itself immediately followed by a tag word.  The tag word
looks exactly like an intra-segment pointer to the target object would look, except that the offset
is always zero.

The reason for the convoluted double-far convention is to make it possible to form a new pointer
to an object in a segment that is full.  If you can't allocate even one word in the segment where
the target resides, then you will need to allocate a landing pad in some other segment, and use
this double-far approach.  This should be exceedingly rare in practice since pointers are normally
set to point to new objects, not existing ones.

### Capabilities (Interfaces)

When using Cap'n Proto for [RPC](rpc.html), every message has an associated "capability table"
which is a flat list of all capabilities present in the message body.  The details of what this
table contains and where it is stored are the responsibility of the RPC system; in some cases, the
table may not even be part of the message content.

A capability pointer, then, simply contains an index into the separate capability table.

    lsb                    capability pointer                     msb
    +-+-----------------------------+-------------------------------+
    |A|              B              |               C               |
    +-+-----------------------------+-------------------------------+

    A (2 bits) = 3, to indicate that this is an "other" pointer.
    B (30 bits) = 0, to indicate that this is a capability pointer.
        (All other values are reserved for future use.)
    C (32 bits) = Index of the capability in the message's capability
        table.

In [rpc.capnp](https://github.com/kentonv/capnproto/blob/master/c++/src/capnp/rpc.capnp), the
capability table is encoded as a list of `CapDescriptors`, appearing along-side the message content
in the `Payload` struct.  However, some use cases may call for different approaches.  A message
that is built and consumed within the same process need not encode the capability table at all
(it can just keep the table as a separate array).  A message that is going to be stored to disk
would need to store a table of `SturdyRef`s instead of `CapDescriptor`s.

## Serialization Over a Stream

When transmitting a message, the segments must be framed in some way, i.e. to communicate the
number of segments and their sizes before communicating the actual data.  The best framing approach
may differ depending on the medium -- for example, messages read via `mmap` or shared memory may
call for a different approach than messages sent over a socket or a pipe.  Cap'n Proto does not
attempt to specify a framing format for every situation.  However, since byte streams are by far
the most common transmission medium, Cap'n Proto does define and implement a recommended framing
format for them.

When transmitting over a stream, the following should be sent.  All integers are unsigned and
little-endian.

* (4 bytes) The number of segments, minus one (since there is always at least one segment).
* (N * 4 bytes) The size of each segment, in words.
* (0 or 4 bytes) Padding up to the next word boundary.
* The content of each segment, in order.

### Packing

For cases where bandwidth usage matters, Cap'n Proto defines a simple compression scheme called
"packing".  This scheme is based on the observation that Cap'n Proto messages contain lots of
zero bytes: padding bytes, unset fields, and high-order bytes of small-valued integers.

In packed format, each word of the message is reduced to a tag byte followed by zero to eight
content bytes.  The bits of the tag byte correspond to the bytes of the unpacked word, with the
least-significant bit corresponding to the first byte.  Each zero bit indicates that the
corresponding byte is zero.  The non-zero bytes are packed following the tag.

For example, here is some typical Cap'n Proto data (a struct pointer (offset = 2, data size = 3,
pointer count = 2) followed by a text pointer (offset = 6, length = 53)) and its packed form:

    unpacked (hex):  08 00 00 00 03 00 02 00   19 00 00 00 aa 01 00 00
    packed (hex):  51 08 03 02   31 19 aa 01

In addition to the above, there are two tag values which are treated specially:  0x00 and 0xff.

* 0x00:  The tag is followed by a single byte which indicates a count of consecutive zero-valued
  words, minus 1.  E.g. if the tag 0x00 is followed by 0x05, the sequence unpacks to 6 words of
  zero.

  Or, put another way: the tag is first decoded as if it were not special.  Since none of the bits
  are set, it is followed by no bytes and expands to a word full of zeros.  After that, the next
  byte is interpreted as a count of _additional_ words that are also all-zero.

* 0xff:  The tag is followed by the bytes of the word (as if it weren't special), but after those
  bytes is another byte with value N.  Following that byte is N unpacked words that should be copied
  directly.  These unpacked words may or may not contain zeros -- it is up to the compressor to
  decide when to end the unpacked span and return to packing each word.  The purpose of this rule
  is to minimize the impact of packing on data that doesn't contain any zeros -- in particular,
  long text blobs.  Because of this rule, the worst-case space overhead of packing is 2 bytes per
  2 KiB of input (256 words = 2KiB).

Examples:

    unpacked (hex):  00 (x 32 bytes)
    packed (hex):  00 03

    unpacked (hex):  8a (x 32 bytes)
    packed (hex):  ff 8a (x 8 bytes) 03 8a (x 24 bytes)

Notice that both of the special cases begin by treating the tag as if it weren't special.  This
is intentionally designed to make encoding faster:  you can compute the tag value and encode the
bytes in a single pass through the input word.  Only after you've finished with that word do you
need to check whether the tag ended up being 0x00 or 0xff.

It is possible to write both an encoder and a decoder which only branch at the end of each word,
and only to handle the two special tags.  It is not necessary to branch on every byte.  See the
C++ reference implementation for an example.

Packing is normally applied on top of the standard stream framing described in the previous
section.

### Compression

When Cap'n Proto messages may contain repetitive data (especially, large text blobs), it makes sense
to apply a standard compression algorithm in addition to packing.  When CPU time is scarce, we
recommend Google's [Snappy](https://code.google.com/p/snappy/).  Otherwise,
[zlib](http://www.zlib.net) is slower but will compress more.

## Security Notes

A naive implementation of a Cap'n Proto reader may be vulnerable to DoS attacks based on two types
of malicious input:

* A message containing cyclic (or even just overlapping) pointers can cause the reader to go into
  an infinite loop while traversing the content.
* A message with deeply-nested objects can cause a stack overflow in typical code which processes
  messages recursively.

To defend against these attacks, every Cap'n Proto implementation should implemented the following
restrictions by default:

* As the application traverses the message, each time a pointer is dereferenced, a counter should
  be incremented by the size of the data to which it points.  If this counter goes over some limit,
  an error should be raised, and/or default values should be returned.  The C++ implementation
  currently defaults to a limit of 64MiB, but allows the caller to set a different limit if desired.
* As the application traverses the message, the pointer depth should be tracked.  Again, if it goes
  over some limit, an error should be raised.  The C++ implementation currently defaults to a limit
  of 64 pointers, but allows the caller to set a different limit.
