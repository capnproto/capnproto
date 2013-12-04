// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef KJ_ASYNC_IO_H_
#define KJ_ASYNC_IO_H_

#include "async.h"
#include "function.h"

namespace kj {

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

class RemoteAddress {
  // Represents a remote address to which the application can connect.

public:
  virtual Promise<Own<AsyncIoStream>> connect() = 0;
  // Make a new connection to this address.

  virtual String toString() = 0;
  // Produce a human-readable string which hopefully can be passed to Network::parseRemoteAddress()
  // to reproduce this address, although whether or not that works of course depends on the Network
  // implementation.  This should be called only to display the address to human users, who will
  // hopefully know what they are able to do with it.
};

class LocalAddress {
  // Represents a local address on which the application can potentially accept connections.

public:
  virtual Own<ConnectionReceiver> listen() = 0;
  // Listen for incoming connections on this address.

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
  virtual Promise<Own<LocalAddress>> parseLocalAddress(
      StringPtr addr, uint portHint = 0) = 0;
  virtual Promise<Own<RemoteAddress>> parseRemoteAddress(
      StringPtr addr, uint portHint = 0) = 0;
  // Construct a local or remote address from a user-provided string.  The format of the address
  // strings is not specified at the API level, and application code should make no assumptions
  // about them.  These strings should always be provided by humans, and said humans will know
  // what format to use in their particular context.
  //
  // `portHint`, if provided, specifies the "standard" IP port number for the application-level
  // service in play.  If the address turns out to be an IP address (v4 or v6), and it lacks a
  // port number, this port will be used.
  //
  // In practice, a local address is usually just a port number (or even an empty string, if a
  // reasonable `portHint` is provided), whereas a remote address usually requires a hostname.

  virtual Own<LocalAddress> getLocalSockaddr(const void* sockaddr, uint len) = 0;
  virtual Own<RemoteAddress> getRemoteSockaddr(const void* sockaddr, uint len) = 0;
  // Construct a local or remote address from a legacy struct sockaddr.
};

struct OneWayPipe {
  // A data pipe with an input end and an output end.  The two ends are safe to use in different
  // threads.  (Typically backed by pipe() system call.)

  Own<AsyncInputStream> in;
  Own<AsyncOutputStream> out;
};

struct TwoWayPipe {
  // A data pipe that supports sending in both directions.  Each end's output sends data to the
  // other end's input.  The ends can be used in separate threads.  (Typically backed by
  // socketpair() system call.)

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
  // - Local IP wildcard (local addresses only; covers both v4 and v6):  "*", "*:80", ":80", "80"
  // - Unix domain: "unix:/path/to/socket"

  virtual Own<AsyncIoStream> newPipeThread(
      Function<void(AsyncIoProvider& ioProvider, AsyncIoStream& stream)> startFunc) = 0;
  // Create a new thread and set up a two-way pipe (socketpair) which can be used to communicate
  // with it.  One end of the pipe is passed to the thread's starct function and the other end of
  // the pipe is returned.  The new thread also gets its own `AsyncIoProvider` instance and will
  // already have an active `EventLoop` when `startFunc` is called.
  //
  // The returned stream's destructor first closes its end of the pipe then waits for the thread to
  // finish (joins it).  The thread should therefore be designed to exit soon after receiving EOF
  // on the input stream.
  //
  // TODO(someday):  I'm not entirely comfortable with this interface.  It seems to be doing too
  //   much at once but I'm not sure how to cleanly break it down.

  // ---------------------------------------------------------------------------
  // Unix-only methods
  //
  // TODO(cleanup):  Should these be in a subclass?

  virtual Own<AsyncInputStream> wrapInputFd(int fd) = 0;
  // Create an AsyncInputStream wrapping a file descriptor.
  //
  // Does not take ownership of the descriptor.
  //
  // This will set `fd` to non-blocking mode (i.e. set O_NONBLOCK) if it isn't set already.

  virtual Own<AsyncOutputStream> wrapOutputFd(int fd) = 0;
  // Create an AsyncOutputStream wrapping a file descriptor.
  //
  // Does not take ownership of the descriptor.
  //
  // This will set `fd` to non-blocking mode (i.e. set O_NONBLOCK) if it isn't set already.

  virtual Own<AsyncIoStream> wrapSocketFd(int fd) = 0;
  // Create an AsyncIoStream wrapping a socket file descriptor.
  //
  // Does not take ownership of the descriptor.
  //
  // This will set `fd` to non-blocking mode (i.e. set O_NONBLOCK) if it isn't set already.

  // ---------------------------------------------------------------------------
  // Windows-only methods

  // TODO(port):  IOCP
};

Own<AsyncIoProvider> setupIoEventLoop();
// Convenience method which sets up the current thread with everything it needs to do async I/O.
// The returned object contains an `EventLoop` which is wrapping an appropriate `EventPort` for
// doing I/O on the host system, so everything is ready for the thread to start making async calls
// and waiting on promises.
//
// You would typically call this in your main() loop or in the start function of a thread.
// Example:
//
//     int main() {
//       auto ioSystem = kj::setupIoEventLoop();
//
//       // Now we can call an async function.
//       Promise<String> textPromise = getHttp(ioSystem->getNetwork(), "http://example.com");
//
//       // And we can wait for the promise to complete.  Note that you can only use `wait()`
//       // from the top level, not from inside a promise callback.
//       String text = textPromise.wait();
//       print(text);
//       return 0;
//     }

}  // namespace kj

#endif  // KJ_ASYNC_IO_H_
