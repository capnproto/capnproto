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

namespace kj {

class AsyncInputStream {
  // Asynchronous equivalent of InputStream (from io.h).

public:
  virtual Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) = 0;
  virtual Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) = 0;

  Promise<void> read(void* buffer, size_t bytes);

  static Own<AsyncInputStream> wrapFd(int fd);
  // Create an AsyncInputStream wrapping a file descriptor.
  //
  // This will set `fd` to non-blocking mode (i.e. set O_NONBLOCK) if it isn't set already.
  //
  // The returned object can only be called from within a system event loop (e.g. `UnixEventLoop`).
};

class AsyncOutputStream {
  // Asynchronous equivalent of OutputStream (from io.h).

public:
  virtual Promise<void> write(const void* buffer, size_t size) = 0;
  virtual Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) = 0;

  static Own<AsyncOutputStream> wrapFd(int fd);
  // Create an AsyncOutputStream wrapping a file descriptor.
  //
  // This will set `fd` to non-blocking mode (i.e. set O_NONBLOCK) if it isn't set already.
  //
  // The returned object can only be called from within a system event loop (e.g. `UnixEventLoop`).
};

class AsyncIoStream: public AsyncInputStream, public AsyncOutputStream {
  // A combination input and output stream.

public:
  static Own<AsyncIoStream> wrapFd(int fd);
  // Create an AsyncIoStream wrapping a file descriptor.
  //
  // This will set `fd` to non-blocking mode (i.e. set O_NONBLOCK) if it isn't set already.
  //
  // The returned object can only be called from within a system event loop (e.g. `UnixEventLoop`).
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
  static Own<Network> newSystemNetwork();
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
  //
  // The system network -- and all objects it creates -- can only be used from threads running
  // a system event loop (e.g. `UnixEventLoop`).

  virtual Promise<Own<LocalAddress>> parseLocalAddress(
      StringPtr addr, uint portHint = 0) const = 0;
  virtual Promise<Own<RemoteAddress>> parseRemoteAddress(
      StringPtr addr, uint portHint = 0) const = 0;
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

  virtual Own<LocalAddress> getLocalSockaddr(const void* sockaddr, uint len) const = 0;
  virtual Own<RemoteAddress> getRemoteSockaddr(const void* sockaddr, uint len) const = 0;
  // Construct a local or remote address from a legacy struct sockaddr.
};

struct OneWayPipe {
  Own<AsyncInputStream> in;
  Own<AsyncOutputStream> out;
};
OneWayPipe newOneWayPipe();
// Creates an input/output stream pair representing the ends of a one-way OS pipe (created with
// pipe(2)).

struct TwoWayPipe {
  Own<AsyncIoStream> ends[2];
};
TwoWayPipe newTwoWayPipe();
// Creates two AsyncIoStreams representing the two ends of a two-way OS pipe (created with
// socketpair(2)).  Data written to one end can be read from the other.

}  // namespace kj

#endif  // KJ_ASYNC_IO_H_
