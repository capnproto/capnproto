---
layout: page
title: RPC Protocol
---

# RPC Protocol

The Cap'n Proto RPC protocol is not yet defined.  See the language spec's
[section on interfaces](language.html#interfaces) for a hint of what it will do.

Here are some misc planned / hoped-for features:

* **Shared memory IPC:**  When instructed to communicate over a Unix domain socket, Cap'n Proto may
  automatically negotiate to use shared memory, by creating a temporary file and then sending a
  file descriptor across the socket.  Once messages are being allocated in shared memory, RPCs
  can be initiated by merely signaling a [futex](http://man7.org/linux/man-pages/man2/futex.2.html)
  (on Linux, at least), which ought to be ridiculously fast.

* **Promise Pipelining:**  When an RPC will return a reference to a new remote object, the client
  will be able to initiate calls to the returned object before the initial RPC has actually
  completed.  Essentially, the client says to the server: "Call method `foo` of the object to be
  returned by RPC id N."  Obviously, if the original RPC fails, the dependent call also fails.
  Otherwise, the server can start executing the dependent call as soon as the original call
  completes, without the need for a network round-trip.  In the object-capability programming
  language <a href="http://en.wikipedia.org/wiki/E_(programming_language)">E</a> this is known as
  [promise pipelining](http://en.wikipedia.org/wiki/Futures_and_promises#Promise_pipelining).
