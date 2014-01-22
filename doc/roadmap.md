---
layout: page
title: Road Map
---

# Road Map

Here's what's (hopefully) in store for future versions of Cap'n Proto!  Of course, everything here
is subject to change.

## Next version: 0.5

* **Shared memory RPC:**  Zero-copy inter-process communication.  Synchronized with futexes.
* **Persistent capabilities (level 2 RPC):**  Allow system-assisted saving and restoring of
  capabilities across connections.
* **Three-way introductions (level 3 RPC):**  Allow RPC interactions between more than two parties,
  with new connections formed automatically as needed.
* **Fiber-based concurrency:**  The C++ runtime's event loop concurrency model will be augmented
  with support for fibers, which are like threads except that context switches happen only at
  well-defined points (thus avoiding the need for mutex locking).  Fibers essentially provide
  syntax sugar on top of the event loop model.
* **Dynamic schema transmission:**  Allow e.g. Python applications to obtain schemas directly from
  the RPC server so that they need not have a local copy.  Great for interactive debugging.

## Near future (after 0.5)

Provisionally, these are probably the things that will be worked on after 0.5.

* **C++98 Backport:**  Fork and backport the key functionality from the C++11 implementation to
  work under C++98/C++03.  This will make Cap'n Proto accessible to MSVC users.  The schema and
  dynamic reflection APIs will not be included.  This also means stringification will not be
  available (though you can use the external `capnp` tool for that).
* **Implement parameterized types:**  Template / generic types with type parameters.
* **Implement encapsulated types:**  This will allow you to create a hand-written wrapper around a
  type which will be automatically injected into the generated code, so that you can provide a
  nicer interface which encapsulates the type's inner state.
* **Implement maps:**  Based on encapsulated and parameterized types.

## Before version 1.0

These things absolutely must happen before any 1.0 release.  Note that it's not yet decided when
a 1.0 release would happen nor how many 0.x releases might precede it.

* **Expand test coverage:**  There are lots of tests now, but some important scenarios, such as
  handling invalid of invalid input, need better testing.
* **Performance review:**  Performance is already very good compared to competitors, but at some
  point we need to break out the profiler and really hone down on the details.
* **Security review:**  We need a careful security review to make sure malicious input cannot
  crash an application or corrupt memory.

## Wish List

These are features we'd like to implement some day but haven't decided yet how to prioritize.
Some of these features could make their way into Cap'n Proto before version 1.0.  Others will
certainly come after.  If you have opinions on what you'd like to see next,
[tell us](https://groups.google.com/group/capnproto)!

### Language Features

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

### C++ API Features

* **JSON codec:**  API for transcoding to JSON format, useful for interacting with legacy
  infrastructure.
* **Snappy integration:**  Integrate [Snappy compression](https://code.google.com/p/snappy/) into
  the API to further reduce bandwidth needs with minimal CPU overhead.
* **Annotations API:**  For each annotation definition, generate code which assists in extracting
  that annotation from schema objects in a type-safe way.

### Storage

* **ORM interface:**  Define a standard interface for capabilities that represent remotely-stored
  objects, with get, put, publish, and subscribe methods.  Ideally, parameterize this interface
  on the stored type.
* **mmap-friendly mutable storage format:**  Define a standard storage format that is friendly
  to mmap-based use while allowing modification.  (With the current serialization format, mmap
  is only useful for read-only structures.)  Possibly based on the ORM interface, updates only
  possible at the granularity of a whole ORM entry.

### Tools

* **Schema compatibility checker:**  Add a `capnp` command which, given two schemas, verifies
  that the latter is a compatible upgrade from the former.  This could be used as a git hook
  to prevent submission of schema changes that would break wire compatibility.
* **RPC debugger:**  Add a `capnp` command which sends an RPC from the command line and prints
  the result.  Useful for debugging RPC servers.

### Infrastructure

Note:  These are very large projects.

* **JSON-HTTP proxy:**  Develop a web server which can expose a Cap'n Proto RPC backend as a
  JSON-over-HTTP protocol.
* **Database:**  A fast storage database based on Cap'n Proto which implements the ORM interface
  on top of the mmap storage format.
