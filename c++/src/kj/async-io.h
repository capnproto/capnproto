// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef KJ_ASYNC_IO_H_
#define KJ_ASYNC_IO_H_

#include "async.h"
#include "function.h"
#include "thread.h"
#include "time.h"

namespace kj {

class UnixEventPort;

class AsyncInputStream {
  // Asynchronous equivalent of InputStream (from io.h).

public:
  virtual Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) = 0;
  virtual Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) = 0;

  Promise<void> read(void* buffer, size_t bytes);
};

class AsyncOutputStream {
  // Asynchronous equivalent of OutputStream (from io.h).

public:
  virtual Promise<void> write(const void* buffer, size_t size) = 0;
  virtual Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) = 0;
};

class AsyncIoStream: public AsyncInputStream, public AsyncOutputStream {
  // A combination input and output stream.

public:
  virtual void shutdownWrite() = 0;
  // Cleanly shut down just the write end of the stream, while keeping the read end open.
};

class ConnectionReceiver {
  // Represents a server socket listening on a port.

public:
  virtual Promise<Own<AsyncIoStream>> accept() = 0;
  // Accept the next incoming connection.

  virtual uint getPort() = 0;
  // Gets the port number, if applicable (i.e. if listening on IP).  This is useful if you didn't
  // specify a port when constructing the LocalAddress -- one will have been assigned automatically.
};

class NetworkAddress {
  // Represents a remote address to which the application can connect.

public:
  virtual Promise<Own<AsyncIoStream>> connect() = 0;
  // Make a new connection to this address.
  //
  // The address must not be a wildcard ("*").  If it is an IP address, it must have a port number.

  virtual Own<ConnectionReceiver> listen() = 0;
  // Listen for incoming connections on this address.
  //
  // The address must be local.

  virtual String toString() = 0;
  // Produce a human-readable string which hopefully can be passed to Network::parseRemoteAddress()
  // to reproduce this address, although whether or not that works of course depends on the Network
  // implementation.  This should be called only to display the address to human users, who will
  // hopefully know what they are able to do with it.
};

class LocalAddress {
  // Represents a local address on which the application can potentially accept connections.

public:
  virtual String toString() = 0;
  // Produce a human-readable string which hopefully can be passed to Network::parseRemoteAddress()
  // to reproduce this address, although whether or not that works of course depends on the Network
  // implementation.  This should be called only to display the address to human users, who will
  // hopefully know what they are able to do with it.
};

class Network {
  // Factory for LocalAddress and RemoteAddress instances, representing the network services
  // offered by the operating system.
  //
  // This interface typically represents broad authority, and well-designed code should limit its
  // use to high-level startup code and user interaction.  Low-level APIs should accept
  // LocalAddress and/or RemoteAddress instances directly and work from there, if at all possible.

public:
  virtual Promise<Own<NetworkAddress>> parseAddress(StringPtr addr, uint portHint = 0) = 0;
  // Construct a network address from a user-provided string.  The format of the address
  // strings is not specified at the API level, and application code should make no assumptions
  // about them.  These strings should always be provided by humans, and said humans will know
  // what format to use in their particular context.
  //
  // `portHint`, if provided, specifies the "standard" IP port number for the application-level
  // service in play.  If the address turns out to be an IP address (v4 or v6), and it lacks a
  // port number, this port will be used.  If `addr` lacks a port number *and* `portHint` is
  // omitted, then the returned address will only support listen() (not connect()), and a port
  // will be chosen when listen() is called.

  virtual Own<NetworkAddress> getSockaddr(const void* sockaddr, uint len) = 0;
  // Construct a network address from a legacy struct sockaddr.
};

struct OneWayPipe {
  // A data pipe with an input end and an output end.  (Typically backed by pipe() system call.)

  Own<AsyncInputStream> in;
  Own<AsyncOutputStream> out;
};

struct TwoWayPipe {
  // A data pipe that supports sending in both directions.  Each end's output sends data to the
  // other end's input.  (Typically backed by socketpair() system call.)

  Own<AsyncIoStream> ends[2];
};

class AsyncIoProvider {
  // Class which constructs asynchronous wrappers around the operating system's I/O facilities.
  //
  // Generally, the implementation of this interface must integrate closely with a particular
  // `EventLoop` implementation.  Typically, the EventLoop implementation itself will provide
  // an AsyncIoProvider.

public:
  virtual OneWayPipe newOneWayPipe() = 0;
  // Creates an input/output stream pair representing the ends of a one-way pipe (e.g. created with
  // the pipe(2) system call).

  virtual TwoWayPipe newTwoWayPipe() = 0;
  // Creates two AsyncIoStreams representing the two ends of a two-way pipe (e.g. created with
  // socketpair(2) system call).  Data written to one end can be read from the other.

  virtual Network& getNetwork() = 0;
  // Creates a new `Network` instance representing the networks exposed by the operating system.
  //
  // DO NOT CALL THIS except at the highest levels of your code, ideally in the main() function.  If
  // you call this from low-level code, then you are preventing higher-level code from injecting an
  // alternative implementation.  Instead, if your code needs to use network functionality, it
  // should ask for a `Network` as a constructor or method parameter, so that higher-level code can
  // chose what implementation to use.  The system network is essentially a singleton.  See:
  //     http://www.object-oriented-security.org/lets-argue/singletons
  //
  // Code that uses the system network should not make any assumptions about what kinds of
  // addresses it will parse, as this could differ across platforms.  String addresses should come
  // strictly from the user, who will know how to write them correctly for their system.
  //
  // With that said, KJ currently supports the following string address formats:
  // - IPv4: "1.2.3.4", "1.2.3.4:80"
  // - IPv6: "1234:5678::abcd", "[1234:5678::abcd]:80"
  // - Local IP wildcard (covers both v4 and v6):  "*", "*:80"
  // - Symbolic names:  "example.com", "example.com:80", "example.com:http", "1.2.3.4:http"
  // - Unix domain: "unix:/path/to/socket"

  struct PipeThread {
    // A combination of a thread and a two-way pipe that communicates with that thread.
    //
    // The fields are intentionally ordered so that the pipe will be destroyed (and therefore
    // disconnected) before the thread is destroyed (and therefore joined).  Thus if the thread
    // arranges to exit when it detects disconnect, destruction should be clean.

    Own<Thread> thread;
    Own<AsyncIoStream> pipe;
  };

  virtual PipeThread newPipeThread(
      Function<void(AsyncIoProvider&, AsyncIoStream&, WaitScope&)> startFunc) = 0;
  // Create a new thread and set up a two-way pipe (socketpair) which can be used to communicate
  // with it.  One end of the pipe is passed to the thread's start function and the other end of
  // the pipe is returned.  The new thread also gets its own `AsyncIoProvider` instance and will
  // already have an active `EventLoop` when `startFunc` is called.
  //
  // TODO(someday):  I'm not entirely comfortable with this interface.  It seems to be doing too
  //   much at once but I'm not sure how to cleanly break it down.

  virtual Timer& getTimer() = 0;
  // Returns a `Timer` based on real time.  Time does not pass while event handlers are running --
  // it only updates when the event loop polls for system events.  This means that calling `now()`
  // on this timer does not require a system call.
  //
  // This timer is not affected by changes to the system date.  It is unspecified whether the timer
  // continues to count while the system is suspended.
};

class LowLevelAsyncIoProvider {
  // Similar to `AsyncIoProvider`, but represents a lower-level interface that may differ on
  // different operating systems.  You should prefer to use `AsyncIoProvider` over this interface
  // whenever possible, as `AsyncIoProvider` is portable and friendlier to dependency-injection.
  //
  // On Unix, this interface can be used to import native file descriptors into the async framework.
  // Different implementations of this interface might work on top of different event handling
  // primitives, such as poll vs. epoll vs. kqueue vs. some higher-level event library.
  //
  // On Windows, this interface can be used to import native HANDLEs into the async framework.
  // Different implementations of this interface might work on top of different event handling
  // primitives, such as I/O completion ports vs. completion routines.
  //
  // TODO(port):  Actually implement Windows support.

public:
  // ---------------------------------------------------------------------------
  // Unix-specific stuff

  enum Flags {
    // Flags controlling how to wrap a file descriptor.

    TAKE_OWNERSHIP = 1 << 0,
    // The returned object should own the file descriptor, automatically closing it when destroyed.
    // The close-on-exec flag will be set on the descriptor if it is not already.
    //
    // If this flag is not used, then the file descriptor is not automatically closed and the
    // close-on-exec flag is not modified.

    ALREADY_CLOEXEC = 1 << 1,
    // Indicates that the close-on-exec flag is known already to be set, so need not be set again.
    // Only relevant when combined with TAKE_OWNERSHIP.
    //
    // On Linux, all system calls which yield new file descriptors have flags or variants which
    // set the close-on-exec flag immediately.  Unfortunately, other OS's do not.

    ALREADY_NONBLOCK = 1 << 2
    // Indicates that the file descriptor is known already to be in non-blocking mode, so the flag
    // need not be set again.  Otherwise, all wrap*Fd() methods will enable non-blocking mode
    // automatically.
    //
    // On Linux, all system calls which yield new file descriptors have flags or variants which
    // enable non-blocking mode immediately.  Unfortunately, other OS's do not.
  };

  virtual Own<AsyncInputStream> wrapInputFd(int fd, uint flags = 0) = 0;
  // Create an AsyncInputStream wrapping a file descriptor.
  //
  // `flags` is a bitwise-OR of the values of the `Flags` enum.

  virtual Own<AsyncOutputStream> wrapOutputFd(int fd, uint flags = 0) = 0;
  // Create an AsyncOutputStream wrapping a file descriptor.
  //
  // `flags` is a bitwise-OR of the values of the `Flags` enum.

  virtual Own<AsyncIoStream> wrapSocketFd(int fd, uint flags = 0) = 0;
  // Create an AsyncIoStream wrapping a socket file descriptor.
  //
  // `flags` is a bitwise-OR of the values of the `Flags` enum.

  virtual Promise<Own<AsyncIoStream>> wrapConnectingSocketFd(int fd, uint flags = 0) = 0;
  // Create an AsyncIoStream wrapping a socket that is in the process of connecting.  The returned
  // promise should not resolve until connection has completed -- traditionally indicated by the
  // descriptor becoming writable.
  //
  // `flags` is a bitwise-OR of the values of the `Flags` enum.

  virtual Own<ConnectionReceiver> wrapListenSocketFd(int fd, uint flags = 0) = 0;
  // Create an AsyncIoStream wrapping a listen socket file descriptor.  This socket should already
  // have had `bind()` and `listen()` called on it, so it's ready for `accept()`.
  //
  // `flags` is a bitwise-OR of the values of the `Flags` enum.

  virtual Timer& getTimer() = 0;
  // Returns a `Timer` based on real time.  Time does not pass while event handlers are running --
  // it only updates when the event loop polls for system events.  This means that calling `now()`
  // on this timer does not require a system call.
  //
  // This timer is not affected by changes to the system date.  It is unspecified whether the timer
  // continues to count while the system is suspended.
};

Own<AsyncIoProvider> newAsyncIoProvider(LowLevelAsyncIoProvider& lowLevel);
// Make a new AsyncIoProvider wrapping a `LowLevelAsyncIoProvider`.

struct AsyncIoContext {
  Own<LowLevelAsyncIoProvider> lowLevelProvider;
  Own<AsyncIoProvider> provider;
  WaitScope& waitScope;

  UnixEventPort& unixEventPort;
  // TEMPORARY: Direct access to underlying UnixEventPort, mainly for waiting on signals. This
  //   field will go away at some point when we have a chance to improve these interfaces.
};

AsyncIoContext setupAsyncIo();
// Convenience method which sets up the current thread with everything it needs to do async I/O.
// The returned objects contain an `EventLoop` which is wrapping an appropriate `EventPort` for
// doing I/O on the host system, so everything is ready for the thread to start making async calls
// and waiting on promises.
//
// You would typically call this in your main() loop or in the start function of a thread.
// Example:
//
//     int main() {
//       auto ioContext = kj::setupAsyncIo();
//
//       // Now we can call an async function.
//       Promise<String> textPromise = getHttp(*ioContext.provider, "http://example.com");
//
//       // And we can wait for the promise to complete.  Note that you can only use `wait()`
//       // from the top level, not from inside a promise callback.
//       String text = textPromise.wait();
//       print(text);
//       return 0;
//     }

}  // namespace kj

#endif  // KJ_ASYNC_IO_H_
