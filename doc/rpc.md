---
layout: page
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
