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

#ifndef CAPNP_EZ_RPC_H_
#define CAPNP_EZ_RPC_H_

#include "rpc.h"

struct sockaddr;

namespace kj { class AsyncIoProvider; class LowLevelAsyncIoProvider; }

namespace capnp {

class EzRpcContext;

class EzRpcClient {
  // Super-simple interface for setting up a Cap'n Proto RPC client.  Example:
  //
  //     # Cap'n Proto schema
  //     interface Adder {
  //       add @0 (left :Int32, right :Int32) -> (value :Int32);
  //     }
  //
  //     // C++ client
  //     int main() {
  //       capnp::EzRpcClient client("localhost:3456");
  //       Adder::Client adder = client.importCap<Adder>("adder");
  //       auto request = adder.addRequest();
  //       request.setLeft(12);
  //       request.setRight(34);
  //       auto response = request.send().wait(client.getWaitScope());
  //       assert(response.getValue() == 46);
  //       return 0;
  //     }
  //
  //     // C++ server
  //     class AdderImpl final: public Adder::Server {
  //     public:
  //       kj::Promise<void> add(AddContext context) override {
  //         auto params = context.getParams();
  //         context.getResults().setValue(params.getLeft() + params.getRight());
  //         return kj::READY_NOW;
  //       }
  //     };
  //
  //     int main() {
  //       capnp::EzRpcServer server("*:3456");
  //       server.exportCap("adder", kj::heap<AdderImpl>());
  //       kj::NEVER_DONE.wait(server.getWaitScope());
  //     }
  //
  // This interface is easy, but it hides a lot of useful features available from the lower-level
  // classes:
  // - The server can only export a small set of public, singleton capabilities under well-known
  //   string names.  This is fine for transient services where no state needs to be kept between
  //   connections, but hides the power of Cap'n Proto when it comes to long-lived resources.
  // - EzRpcClient/EzRpcServer automatically set up a `kj::EventLoop` and make it current for the
  //   thread.  Only one `kj::EventLoop` can exist per thread, so you cannot use these interfaces
  //   if you wish to set up your own event loop.  (However, you can safely create multiple
  //   EzRpcClient / EzRpcServer objects in a single thread; they will make sure to make no more
  //   than one EventLoop.)
  // - These classes only support simple two-party connections, not multilateral VatNetworks.
  // - These classes only support communication over a raw, unencrypted socket.  If you want to
  //   build on an abstract stream (perhaps one which supports encryption), you must use the
  //   lower-level interfaces.
  //
  // Some of these restrictions will probably be lifted in future versions, but some things will
  // always require using the low-level interfaces directly.  If you are interested in working
  // at a lower level, start by looking at these interfaces:
  // - `kj::startAsyncIo()` in `kj/async-io.h`.
  // - `RpcSystem` in `capnp/rpc.h`.
  // - `TwoPartyVatNetwork` in `capnp/rpc-twoparty.h`.

public:
  explicit EzRpcClient(kj::StringPtr serverAddress, uint defaultPort = 0);
  // Construct a new EzRpcClient and connect to the given address.  The connection is formed in
  // the background -- if it fails, calls to capabilities returned by importCap() will fail with an
  // appropriate exception.
  //
  // `defaultPort` is the IP port number to use if `serverAddress` does not include it explicitly.
  // If unspecified, the port is required in `serverAddress`.
  //
  // The address is parsed by `kj::Network` in `kj/async-io.h`.  See that interface for more info
  // on the address format, but basically it's what you'd expect.

  EzRpcClient(const struct sockaddr* serverAddress, uint addrSize);
  // Like the above constructor, but connects to an already-resolved socket address.  Any address
  // format supported by `kj::Network` in `kj/async-io.h` is accepted.

  explicit EzRpcClient(int socketFd);
  // Create a client on top of an already-connected socket.

  ~EzRpcClient() noexcept(false);

  template <typename Type>
  typename Type::Client importCap(kj::StringPtr name);
  Capability::Client importCap(kj::StringPtr name);
  // Ask the sever for the capability with the given name.  You may specify a type to automatically
  // down-cast to that type.  It is up to you to specify the correct expected type.

  kj::WaitScope& getWaitScope();
  // Get the `WaitScope` for the client's `EventLoop`, which allows you to synchronously wait on
  // promises.

  kj::AsyncIoProvider& getIoProvider();
  // Get the underlying AsyncIoProvider set up by the RPC system.  This is useful if you want
  // to do some non-RPC I/O in asynchronous fashion.

  kj::LowLevelAsyncIoProvider& getLowLevelIoProvider();
  // Get the underlying LowLevelAsyncIoProvider set up by the RPC system.  This is useful if you
  // want to do some non-RPC I/O in asynchronous fashion.

private:
  struct Impl;
  kj::Own<Impl> impl;
};

class EzRpcServer {
  // The server counterpart to `EzRpcClient`.  See `EzRpcClient` for an example.

public:
  explicit EzRpcServer(kj::StringPtr bindAddress, uint defaultPort = 0);
  // Construct a new `EzRpcServer` that binds to the given address.  An address of "*" means to
  // bind to all local addresses.
  //
  // `defaultPort` is the IP port number to use if `serverAddress` does not include it explicitly.
  // If unspecified, a port is chosen automatically, and you must call getPort() to find out what
  // it is.
  //
  // The address is parsed by `kj::Network` in `kj/async-io.h`.  See that interface for more info
  // on the address format, but basically it's what you'd expect.
  //
  // The server might not begin listening immediately, especially if `bindAddress` needs to be
  // resolved.  If you need to wait until the server is definitely up, wait on the promise returned
  // by `getPort()`.

  EzRpcServer(struct sockaddr* bindAddress, uint addrSize);
  // Like the above constructor, but binds to an already-resolved socket address.  Any address
  // format supported by `kj::Network` in `kj/async-io.h` is accepted.

  EzRpcServer(int socketFd, uint port);
  // Create a server on top of an already-listening socket (i.e. one on which accept() may be
  // called).  `port` is returned by `getPort()` -- it serves no other purpose.

  ~EzRpcServer() noexcept(false);

  void exportCap(kj::StringPtr name, Capability::Client cap);
  // Export a capability publicly under the given name, so that clients can import it.
  //
  // Keep in mind that you can implicitly convert `kj::Own<MyType::Server>&&` to
  // `Capability::Client`, so it's typical to pass something like
  // `kj::heap<MyImplementation>(<constructor params>)` as the second parameter.

  kj::Promise<uint> getPort();
  // Get the IP port number on which this server is listening.  This promise won't resolve until
  // the server is actually listening.  If the address was not an IP address (e.g. it was a Unix
  // domain socket) then getPort() resolves to zero.

  kj::WaitScope& getWaitScope();
  // Get the `WaitScope` for the client's `EventLoop`, which allows you to synchronously wait on
  // promises.

  kj::AsyncIoProvider& getIoProvider();
  // Get the underlying AsyncIoProvider set up by the RPC system.  This is useful if you want
  // to do some non-RPC I/O in asynchronous fashion.

  kj::LowLevelAsyncIoProvider& getLowLevelIoProvider();
  // Get the underlying LowLevelAsyncIoProvider set up by the RPC system.  This is useful if you
  // want to do some non-RPC I/O in asynchronous fashion.

private:
  struct Impl;
  kj::Own<Impl> impl;
};

// =======================================================================================
// inline implementation details

template <typename Type>
inline typename Type::Client EzRpcClient::importCap(kj::StringPtr name) {
  return importCap(name).castAs<Type>();
}

}  // namespace capnp

#endif  // CAPNP_EZ_RPC_H_
