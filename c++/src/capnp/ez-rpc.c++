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

#include "ez-rpc.h"
#include "rpc-twoparty.h"
#include <capnp/rpc.capnp.h>
#include <kj/async-io.h>
#include <kj/debug.h>
#include <kj/threadlocal.h>
#include <map>

namespace capnp {

KJ_THREADLOCAL_PTR(EzRpcContext) threadEzContext = nullptr;

class EzRpcContext: public kj::Refcounted {
public:
  EzRpcContext(): ioContext(kj::setupAsyncIo()) {
    threadEzContext = this;
  }

  ~EzRpcContext() noexcept(false) {
    KJ_REQUIRE(threadEzContext == this,
               "EzRpcContext destroyed from different thread than it was created.") {
      return;
    }
    threadEzContext = nullptr;
  }

  kj::WaitScope& getWaitScope() {
    return ioContext.waitScope;
  }

  kj::AsyncIoProvider& getIoProvider() {
    return *ioContext.provider;
  }

  kj::LowLevelAsyncIoProvider& getLowLevelIoProvider() {
    return *ioContext.lowLevelProvider;
  }

  static kj::Own<EzRpcContext> getThreadLocal() {
    EzRpcContext* existing = threadEzContext;
    if (existing != nullptr) {
      return kj::addRef(*existing);
    } else {
      return kj::refcounted<EzRpcContext>();
    }
  }

private:
  kj::AsyncIoContext ioContext;
};

// =======================================================================================

kj::Promise<kj::Own<kj::AsyncIoStream>> connectAttach(kj::Own<kj::NetworkAddress>&& addr) {
  return addr->connect().attach(kj::mv(addr));
}

struct EzRpcClient::Impl {
  kj::Own<EzRpcContext> context;

  struct ClientContext {
    kj::Own<kj::AsyncIoStream> stream;
    TwoPartyVatNetwork network;
    RpcSystem<rpc::twoparty::VatId> rpcSystem;

    ClientContext(kj::Own<kj::AsyncIoStream>&& stream, ReaderOptions readerOpts)
        : stream(kj::mv(stream)),
          network(*this->stream, rpc::twoparty::Side::CLIENT, readerOpts),
          rpcSystem(makeRpcClient(network)) {}

    Capability::Client getMain() {
      word scratch[4];
      memset(scratch, 0, sizeof(scratch));
      MallocMessageBuilder message(scratch);
      auto hostId = message.getRoot<rpc::twoparty::VatId>();
      hostId.setSide(rpc::twoparty::Side::SERVER);
      return rpcSystem.bootstrap(hostId);
    }

    Capability::Client restore(kj::StringPtr name) {
      word scratch[64];
      memset(scratch, 0, sizeof(scratch));
      MallocMessageBuilder message(scratch);

      auto hostIdOrphan = message.getOrphanage().newOrphan<rpc::twoparty::VatId>();
      auto hostId = hostIdOrphan.get();
      hostId.setSide(rpc::twoparty::Side::SERVER);

      auto objectId = message.getRoot<AnyPointer>();
      objectId.setAs<Text>(name);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
      return rpcSystem.restore(hostId, objectId);
#pragma GCC diagnostic pop
    }
  };

  kj::ForkedPromise<void> setupPromise;

  kj::Maybe<kj::Own<ClientContext>> clientContext;
  // Filled in before `setupPromise` resolves.

  Impl(kj::StringPtr serverAddress, uint defaultPort,
       ReaderOptions readerOpts)
      : context(EzRpcContext::getThreadLocal()),
        setupPromise(context->getIoProvider().getNetwork()
            .parseAddress(serverAddress, defaultPort)
            .then([](kj::Own<kj::NetworkAddress>&& addr) {
              return connectAttach(kj::mv(addr));
            }).then([this, readerOpts](kj::Own<kj::AsyncIoStream>&& stream) {
              clientContext = kj::heap<ClientContext>(kj::mv(stream),
                                                      readerOpts);
            }).fork()) {}

  Impl(const struct sockaddr* serverAddress, uint addrSize,
       ReaderOptions readerOpts)
      : context(EzRpcContext::getThreadLocal()),
        setupPromise(
            connectAttach(context->getIoProvider().getNetwork()
                .getSockaddr(serverAddress, addrSize))
            .then([this, readerOpts](kj::Own<kj::AsyncIoStream>&& stream) {
              clientContext = kj::heap<ClientContext>(kj::mv(stream),
                                                      readerOpts);
            }).fork()) {}

  Impl(int socketFd, ReaderOptions readerOpts)
      : context(EzRpcContext::getThreadLocal()),
        setupPromise(kj::Promise<void>(kj::READY_NOW).fork()),
        clientContext(kj::heap<ClientContext>(
            context->getLowLevelIoProvider().wrapSocketFd(socketFd),
            readerOpts)) {}
};

EzRpcClient::EzRpcClient(kj::StringPtr serverAddress, uint defaultPort, ReaderOptions readerOpts)
    : impl(kj::heap<Impl>(serverAddress, defaultPort, readerOpts)) {}

EzRpcClient::EzRpcClient(const struct sockaddr* serverAddress, uint addrSize, ReaderOptions readerOpts)
    : impl(kj::heap<Impl>(serverAddress, addrSize, readerOpts)) {}

EzRpcClient::EzRpcClient(int socketFd, ReaderOptions readerOpts)
    : impl(kj::heap<Impl>(socketFd, readerOpts)) {}

EzRpcClient::~EzRpcClient() noexcept(false) {}

Capability::Client EzRpcClient::getMain() {
  KJ_IF_MAYBE(client, impl->clientContext) {
    return client->get()->getMain();
  } else {
    return impl->setupPromise.addBranch().then([this]() {
      return KJ_ASSERT_NONNULL(impl->clientContext)->getMain();
    });
  }
}

Capability::Client EzRpcClient::importCap(kj::StringPtr name) {
  KJ_IF_MAYBE(client, impl->clientContext) {
    return client->get()->restore(name);
  } else {
    return impl->setupPromise.addBranch().then(kj::mvCapture(kj::heapString(name),
        [this](kj::String&& name) {
      return KJ_ASSERT_NONNULL(impl->clientContext)->restore(name);
    }));
  }
}

kj::WaitScope& EzRpcClient::getWaitScope() {
  return impl->context->getWaitScope();
}

kj::AsyncIoProvider& EzRpcClient::getIoProvider() {
  return impl->context->getIoProvider();
}

kj::LowLevelAsyncIoProvider& EzRpcClient::getLowLevelIoProvider() {
  return impl->context->getLowLevelIoProvider();
}

// =======================================================================================

namespace {

class DummyFilter: public kj::LowLevelAsyncIoProvider::NetworkFilter {
public:
  bool shouldAllow(const struct sockaddr* addr, uint addrlen) override {
    return true;
  }
};

static DummyFilter DUMMY_FILTER;

}  // namespace

struct EzRpcServer::Impl final: public SturdyRefRestorer<AnyPointer>,
                                public kj::TaskSet::ErrorHandler {
  Capability::Client mainInterface;
  kj::Own<EzRpcContext> context;

  struct ExportedCap {
    kj::String name;
    Capability::Client cap = nullptr;

    ExportedCap(kj::StringPtr name, Capability::Client cap)
        : name(kj::heapString(name)), cap(cap) {}

    ExportedCap() = default;
    ExportedCap(const ExportedCap&) = delete;
    ExportedCap(ExportedCap&&) = default;
    ExportedCap& operator=(const ExportedCap&) = delete;
    ExportedCap& operator=(ExportedCap&&) = default;
    // Make std::map happy...
  };

  std::map<kj::StringPtr, ExportedCap> exportMap;

  kj::ForkedPromise<uint> portPromise;

  kj::TaskSet tasks;

  struct ServerContext {
    kj::Own<kj::AsyncIoStream> stream;
    TwoPartyVatNetwork network;
    RpcSystem<rpc::twoparty::VatId> rpcSystem;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    ServerContext(kj::Own<kj::AsyncIoStream>&& stream, SturdyRefRestorer<AnyPointer>& restorer,
                  ReaderOptions readerOpts)
        : stream(kj::mv(stream)),
          network(*this->stream, rpc::twoparty::Side::SERVER, readerOpts),
          rpcSystem(makeRpcServer(network, restorer)) {}
#pragma GCC diagnostic pop
  };

  Impl(Capability::Client mainInterface, kj::StringPtr bindAddress, uint defaultPort,
       ReaderOptions readerOpts)
      : mainInterface(kj::mv(mainInterface)),
        context(EzRpcContext::getThreadLocal()), portPromise(nullptr), tasks(*this) {
    auto paf = kj::newPromiseAndFulfiller<uint>();
    portPromise = paf.promise.fork();

    tasks.add(context->getIoProvider().getNetwork().parseAddress(bindAddress, defaultPort)
        .then(kj::mvCapture(paf.fulfiller,
          [this, readerOpts](kj::Own<kj::PromiseFulfiller<uint>>&& portFulfiller,
                             kj::Own<kj::NetworkAddress>&& addr) {
      auto listener = addr->listen();
      portFulfiller->fulfill(listener->getPort());
      acceptLoop(kj::mv(listener), readerOpts);
    })));
  }

  Impl(Capability::Client mainInterface, struct sockaddr* bindAddress, uint addrSize,
       ReaderOptions readerOpts)
      : mainInterface(kj::mv(mainInterface)),
        context(EzRpcContext::getThreadLocal()), portPromise(nullptr), tasks(*this) {
    auto listener = context->getIoProvider().getNetwork()
        .getSockaddr(bindAddress, addrSize)->listen();
    portPromise = kj::Promise<uint>(listener->getPort()).fork();
    acceptLoop(kj::mv(listener), readerOpts);
  }

  Impl(Capability::Client mainInterface, int socketFd, uint port, ReaderOptions readerOpts)
      : mainInterface(kj::mv(mainInterface)),
        context(EzRpcContext::getThreadLocal()),
        portPromise(kj::Promise<uint>(port).fork()),
        tasks(*this) {
    acceptLoop(context->getLowLevelIoProvider().wrapListenSocketFd(socketFd, DUMMY_FILTER),
               readerOpts);
  }

  void acceptLoop(kj::Own<kj::ConnectionReceiver>&& listener, ReaderOptions readerOpts) {
    auto ptr = listener.get();
    tasks.add(ptr->accept().then(kj::mvCapture(kj::mv(listener),
        [this, readerOpts](kj::Own<kj::ConnectionReceiver>&& listener,
                           kj::Own<kj::AsyncIoStream>&& connection) {
      acceptLoop(kj::mv(listener), readerOpts);

      auto server = kj::heap<ServerContext>(kj::mv(connection), *this, readerOpts);

      // Arrange to destroy the server context when all references are gone, or when the
      // EzRpcServer is destroyed (which will destroy the TaskSet).
      tasks.add(server->network.onDisconnect().attach(kj::mv(server)));
    })));
  }

  Capability::Client restore(AnyPointer::Reader objectId) override {
    if (objectId.isNull()) {
      return mainInterface;
    } else {
      auto name = objectId.getAs<Text>();
      auto iter = exportMap.find(name);
      if (iter == exportMap.end()) {
        KJ_FAIL_REQUIRE("Server exports no such capability.", name) { break; }
        return nullptr;
      } else {
        return iter->second.cap;
      }
    }
  }

  void taskFailed(kj::Exception&& exception) override {
    kj::throwFatalException(kj::mv(exception));
  }
};

EzRpcServer::EzRpcServer(Capability::Client mainInterface, kj::StringPtr bindAddress,
                         uint defaultPort, ReaderOptions readerOpts)
    : impl(kj::heap<Impl>(kj::mv(mainInterface), bindAddress, defaultPort, readerOpts)) {}

EzRpcServer::EzRpcServer(Capability::Client mainInterface, struct sockaddr* bindAddress,
                         uint addrSize, ReaderOptions readerOpts)
    : impl(kj::heap<Impl>(kj::mv(mainInterface), bindAddress, addrSize, readerOpts)) {}

EzRpcServer::EzRpcServer(Capability::Client mainInterface, int socketFd, uint port,
                         ReaderOptions readerOpts)
    : impl(kj::heap<Impl>(kj::mv(mainInterface), socketFd, port, readerOpts)) {}

EzRpcServer::EzRpcServer(kj::StringPtr bindAddress, uint defaultPort,
                         ReaderOptions readerOpts)
    : EzRpcServer(nullptr, bindAddress, defaultPort, readerOpts) {}

EzRpcServer::EzRpcServer(struct sockaddr* bindAddress, uint addrSize,
                         ReaderOptions readerOpts)
    : EzRpcServer(nullptr, bindAddress, addrSize, readerOpts) {}

EzRpcServer::EzRpcServer(int socketFd, uint port, ReaderOptions readerOpts)
    : EzRpcServer(nullptr, socketFd, port, readerOpts) {}

EzRpcServer::~EzRpcServer() noexcept(false) {}

void EzRpcServer::exportCap(kj::StringPtr name, Capability::Client cap) {
  Impl::ExportedCap entry(kj::heapString(name), cap);
  impl->exportMap[entry.name] = kj::mv(entry);
}

kj::Promise<uint> EzRpcServer::getPort() {
  return impl->portPromise.addBranch();
}

kj::WaitScope& EzRpcServer::getWaitScope() {
  return impl->context->getWaitScope();
}

kj::AsyncIoProvider& EzRpcServer::getIoProvider() {
  return impl->context->getIoProvider();
}

kj::LowLevelAsyncIoProvider& EzRpcServer::getLowLevelIoProvider() {
  return impl->context->getLowLevelIoProvider();
}

}  // namespace capnp
