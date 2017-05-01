---
layout: page
title: Road Map
---

# Road Map

This is a list of big ideas we'd like to implement in Cap'n Proto. We don't know in what order
these will actually happen; as always, real work is driven by real-world needs.

## Language Features

* **Inline lists:**  Lets you define a field composed of a fixed number of elements of the same
  type, and have those elements be stored directly within the struct rather than as a separate
  object.  Useful mainly to avoid the need to validate list lengths when the length should always
  be the same.  Also saves a pointer's worth of space.
* **Type aliases:**  Ability to define a type which is just an alias of some other type, and
  have it show up as e.g. a `typedef` in languages that support that.  (The current `using`
  keyword is intended only for local use and does not affect code generation.)
* **Doc comments:**  Harvest doc comments from schema files and use them to generate doc comments
  on generated code.  Also make them available in the compiled schema so that a documentation
  generator could use them.
* **Encapsulated types:**  This will allow you to create a hand-written wrapper around a
  type which will be automatically injected into the generated code, so that you can provide a
  nicer interface which encapsulates the type's inner state.
* **Maps:**  Based on encapsulated and parameterized types.

## RPC Protocol Features

* **Dynamic schema transmission:**  Allow e.g. Python applications to obtain schemas directly from
  the RPC server so that they need not have a local copy.  Great for interactive debugging.
* **Three-way introductions (level 3 RPC):**  Allow RPC interactions between more than two parties,
  with new connections formed automatically as needed.
* **Bulk and Realtime**: Add features that make it easier to design Cap'n Proto APIs for bulk
  data transfers (with flow control) and realtime communications (where it's better to drop
  messages than to deliver them late).
* **UDP transport**: Cap'n Proto RPC could benefit from implementing a UDP transport, in order
  to achieve zero-round-trip three-party introductions and to implement "realtime" APIs (see
  "bulk and realtime", above).
* **Encrypted transport**: Cap'n Proto RPC should support an encrypted transport which uses
  capability-based authorization (not PKI), can accomplish zero-round-trip three-party
  introductions (via a pre-shared key from the introducer) and based on modern crypto. TLS is
  not designed for this, but we don't want to invent new crypto; we intend to build on
  [libsodium](https://github.com/jedisct1/libsodium) and the
  [Noise Protocol Framework](http://noiseprotocol.org/) as much as possible.

## C++ Cap'n Proto API Features

* **Plain Old C Structs:** The code generator should also generate a POCS type corresponding
  to each struct type. The POCS type would use traditional memory allocation, thus would not
  support zero-copy, but would support a more traditional and easy-to-use C++ API, including
  the ability to mutate the object over time without convoluted memory management. POCS types
  could be extracted from an inserted into messages with a single copy, allowing them to be
  used easily in non-performance-critical code.
* **Multi-threading:**  It should be made easy to assign different Cap'n Proto RPC objects
  to different threads and have them be able to safely call each other. Each thread would still
  have an anyschronous event loop serving the objects that belong to it.
* **Shared memory RPC:**  Zero-copy inter-process communication.
* **JSON codec customization:**  Extend the JSON library to support customizing the JSON
  representation using annotations. For example, a field could be given a different name in
  JSON than it is in Cap'n Proto. The goal of these features would be to allow any reasonable
  pre-existing JSON schema to be representable as a Cap'n Proto type definition, so that
  servers implementing JSON APIs can use Cap'n Proto exclusively on the server side.
* **LZ4 integration:**  Integrate LZ4 compression into the API to further reduce bandwidth needs
  with minimal CPU overhead.
* **Annotations API:**  For each annotation definition, generate code which assists in extracting
  that annotation from schema objects in a type-safe way.

## C++ KJ API Features

KJ is a framework library that is bundled with Cap'n Proto, but is broadly applicable to C++
applications even if they don't use Cap'n Proto serialization.

* **Fiber-based concurrency:**  The C++ runtime's event loop concurrency model will be augmented
  with support for fibers, which are like threads except that context switches happen only at
  well-defined points (thus avoiding the need for mutex locking).  Fibers essentially provide
  syntax sugar on top of the event loop model.
* **TLS bindings:** Write bindings for e.g. OpenSSL to make it easy to integrate with the KJ
  I/O framework, Cap'n Proto RPC, and the KJ HTTP library.
* **Modern crypto bindings:** A thin wrapper around
  [libsodium](https://github.com/jedisct1/libsodium) with a nice C++ API, e.g. representing
  keys using fixed-size, trivially-copyable classes.
* **Event loop integrations:** We should provide off-the-shelf integrations with popular event
  loop libraries, such as libuv, libev, libevent, boost::asio, and others, so that it's easier
  to use Cap'n Proto RPC in applications that already use another event framework.

## Storage

* **ORM interface:**  Define a standard interface for capabilities that represent remotely-stored
  objects, with get, put, publish, and subscribe methods.  Ideally, parameterize this interface
  on the stored type.
* **mmap-friendly mutable storage format:**  Define a standard storage format that is friendly
  to mmap-based use while allowing modification.  (With the current serialization format, mmap
  is only useful for read-only structures.)  Possibly based on the ORM interface, updates only
  possible at the granularity of a whole ORM entry.

## Tools

* **Schema compatibility checker:**  Add a `capnp` command which, given two schemas, verifies
  that the latter is a compatible upgrade from the former.  This could be used as a git hook
  to prevent submission of schema changes that would break wire compatibility.
* **RPC debugger:**  Add a `capnp` command which sends an RPC from the command line and prints
  the result.  Useful for debugging RPC servers.

## Quality Assurance

These things absolutely must happen before any 1.0 release.  Note that it's not yet decided when
a 1.0 release would happen nor how many 0.x releases might precede it.

* **Expand test coverage:**  There are lots of tests now, but some important scenarios, such as
  handling invalid of invalid input, need better testing.
* **Performance review:**  Performance is already very good compared to competitors, but at some
  point we need to break out the profiler and really hone down on the details.
* **Security review:**  We need a careful security review to make sure malicious input cannot
  crash an application or corrupt memory.

## Infrastructure

Note:  These are very large projects.

* **JSON-HTTP proxy:**  Develop a web server which can expose a Cap'n Proto RPC backend as a
  JSON-over-HTTP protocol.
* **Database:**  A fast storage database based on Cap'n Proto which implements the ORM interface
  on top of the mmap storage format.
