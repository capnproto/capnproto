// Copyright (c) 2019 Cloudflare, Inc. and contributors
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

#include "http-over-capnp.h"
#include <kj/debug.h>
#include <capnp/schema.h>
#include <capnp/message.h>

namespace capnp {

using kj::uint;
using kj::byte;

class HttpOverCapnpFactory::RequestState final
    : public kj::Refcounted, public kj::TaskSet::ErrorHandler {
public:
  RequestState() {
    tasks.emplace(*this);
  }

  template <typename Func>
  auto wrap(Func&& func) -> decltype(func()) {
    if (tasks == nullptr) {
      return KJ_EXCEPTION(DISCONNECTED, "client canceled HTTP request");
    } else {
      return canceler.wrap(func());
    }
  }

  void cancel() {
    if (tasks != nullptr) {
      if (!canceler.isEmpty()) {
        canceler.cancel(KJ_EXCEPTION(DISCONNECTED, "request canceled"));
      }
      tasks = nullptr;
      webSocket = nullptr;
    }
  }

  void assertNotCanceled() {
    if (tasks == nullptr) {
      kj::throwFatalException(KJ_EXCEPTION(DISCONNECTED, "client canceled HTTP request"));
    }
  }

  void addTask(kj::Promise<void> task) {
    KJ_IF_MAYBE(t, tasks) {
      t->add(kj::mv(task));
    } else {
      // Just drop the task.
    }
  }

  kj::Promise<void> finishTasks() {
    // This is merged into the final promise, so we don't need to worry about wrapping it for
    // cancellation.
    return KJ_REQUIRE_NONNULL(tasks).onEmpty()
        .then([this]() {
      KJ_IF_MAYBE(e, error) {
        kj::throwRecoverableException(kj::mv(*e));
      }
    });
  }

  void taskFailed(kj::Exception&& exception) override {
    if (error == nullptr) {
      error = kj::mv(exception);
    }
  }

  void holdWebSocket(kj::Own<kj::WebSocket> webSocket) {
    // Hold on to this WebSocket until cancellation.
    KJ_REQUIRE(this->webSocket == nullptr);
    KJ_REQUIRE(tasks != nullptr);
    this->webSocket = kj::mv(webSocket);
  }

  void disconnectWebSocket() {
    KJ_IF_MAYBE(t, tasks) {
      t->add(kj::evalNow([&]() { return KJ_ASSERT_NONNULL(webSocket)->disconnect(); }));
    }
  }

private:
  kj::Maybe<kj::Exception> error;
  kj::Maybe<kj::Own<kj::WebSocket>> webSocket;
  kj::Canceler canceler;
  kj::Maybe<kj::TaskSet> tasks;
};

// =======================================================================================

class HttpOverCapnpFactory::CapnpToKjWebSocketAdapter final: public capnp::WebSocket::Server {
public:
  CapnpToKjWebSocketAdapter(kj::Own<RequestState> state, kj::WebSocket& webSocket,
                            kj::Promise<Capability::Client> shorteningPromise)
      : state(kj::mv(state)), webSocket(webSocket),
        shorteningPromise(kj::mv(shorteningPromise)) {}

  ~CapnpToKjWebSocketAdapter() noexcept(false) {
    state->disconnectWebSocket();
  }

  kj::Maybe<kj::Promise<Capability::Client>> shortenPath() override {
    auto onAbort = webSocket.whenAborted()
        .then([]() -> kj::Promise<Capability::Client> {
      return KJ_EXCEPTION(DISCONNECTED, "WebSocket was aborted");
    });
    return shorteningPromise.exclusiveJoin(kj::mv(onAbort));
  }

  kj::Promise<void> sendText(SendTextContext context) override {
    return state->wrap([&]() { return webSocket.send(context.getParams().getText()); });
  }
  kj::Promise<void> sendData(SendDataContext context) override {
    return state->wrap([&]() { return webSocket.send(context.getParams().getData()); });
  }
  kj::Promise<void> close(CloseContext context) override {
    auto params = context.getParams();
    return state->wrap([&]() { return webSocket.close(params.getCode(), params.getReason()); });
  }

private:
  kj::Own<RequestState> state;
  kj::WebSocket& webSocket;
  kj::Promise<Capability::Client> shorteningPromise;
};

class HttpOverCapnpFactory::KjToCapnpWebSocketAdapter final: public kj::WebSocket {
public:
  KjToCapnpWebSocketAdapter(
      kj::Maybe<kj::Own<kj::WebSocket>> in, capnp::WebSocket::Client out,
      kj::Own<kj::PromiseFulfiller<kj::Promise<Capability::Client>>> shorteningFulfiller)
      : in(kj::mv(in)), out(kj::mv(out)), shorteningFulfiller(kj::mv(shorteningFulfiller)) {}
  ~KjToCapnpWebSocketAdapter() noexcept(false) {
    if (shorteningFulfiller->isWaiting()) {
      // We want to make sure the fulfiller is not rejected with a bogus "PromiseFulfiller
      // destroyed" error, so fulfill it with never-done.
      shorteningFulfiller->fulfill(kj::NEVER_DONE);
    }
  }

  kj::Promise<void> send(kj::ArrayPtr<const byte> message) override {
    auto req = KJ_REQUIRE_NONNULL(out, "already called disconnect()").sendDataRequest(
        MessageSize { 8 + message.size() / sizeof(word), 0 });
    req.setData(message);
    sentBytes += message.size();
    return req.send();
  }

  kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
    auto req = KJ_REQUIRE_NONNULL(out, "already called disconnect()").sendTextRequest(
        MessageSize { 8 + message.size() / sizeof(word), 0 });
    memcpy(req.initText(message.size()).begin(), message.begin(), message.size());
    sentBytes += message.size();
    return req.send();
  }

  kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
    auto req = KJ_REQUIRE_NONNULL(out, "already called disconnect()").closeRequest();
    req.setCode(code);
    req.setReason(reason);
    sentBytes += reason.size() + 2;
    return req.send().ignoreResult();
  }

  kj::Promise<void> disconnect() override {
    out = nullptr;
    return kj::READY_NOW;
  }

  void abort() override {
    KJ_ASSERT_NONNULL(in)->abort();
  }

  kj::Promise<void> whenAborted() override {
    return KJ_ASSERT_NONNULL(out).whenResolved()
        .then([]() -> kj::Promise<void> {
      // It would seem this capability resolved to an implementation of the WebSocket RPC interface
      // that does not support further path-shortening (so, it's not the implementation found in
      // this file). Since the path-shortening facility is also how we discover disconnects, we
      // apparently have no way to be alerted on disconnect. We have to assume the other end
      // never aborts.
      return kj::NEVER_DONE;
    }, [](kj::Exception&& e) -> kj::Promise<void> {
      if (e.getType() == kj::Exception::Type::DISCONNECTED) {
        // Looks like we were aborted!
        return kj::READY_NOW;
      } else {
        // Some other error... propagate it.
        return kj::mv(e);
      }
    });
  }

  kj::Promise<Message> receive(size_t maxSize) override {
    return KJ_ASSERT_NONNULL(in)->receive(maxSize);
  }

  kj::Promise<void> pumpTo(WebSocket& other) override {
    KJ_IF_MAYBE(optimized, kj::dynamicDowncastIfAvailable<KjToCapnpWebSocketAdapter>(other)) {
      shorteningFulfiller->fulfill(
          kj::cp(KJ_REQUIRE_NONNULL(optimized->out, "already called disconnect()")));

      // We expect the `in` pipe will stop receiving messages after the redirect, but we need to
      // pump anything already in-flight.
      return KJ_ASSERT_NONNULL(in)->pumpTo(other);
    } else KJ_IF_MAYBE(promise, other.tryPumpFrom(*this)) {
      // We may have unwrapped some layers around `other` leading to a shorter path.
      return kj::mv(*promise);
    } else {
      return KJ_ASSERT_NONNULL(in)->pumpTo(other);
    }
  }

  uint64_t sentByteCount() override { return sentBytes; }
  uint64_t receivedByteCount() override { return KJ_ASSERT_NONNULL(in)->receivedByteCount(); }

private:
  kj::Maybe<kj::Own<kj::WebSocket>> in;   // One end of a WebSocketPipe, used only for receiving.
  kj::Maybe<capnp::WebSocket::Client> out;  // Used only for sending.
  kj::Own<kj::PromiseFulfiller<kj::Promise<Capability::Client>>> shorteningFulfiller;
  uint64_t sentBytes = 0;
};

// =======================================================================================

class HttpOverCapnpFactory::ClientRequestContextImpl final
    : public capnp::HttpService::ClientRequestContext::Server {
public:
  ClientRequestContextImpl(HttpOverCapnpFactory& factory,
                           kj::Own<RequestState> state,
                           kj::HttpService::Response& kjResponse)
      : factory(factory), state(kj::mv(state)), kjResponse(kjResponse) {}

  ~ClientRequestContextImpl() noexcept(false) {
    // Note this implicitly cancels the upstream pump task.
  }

  kj::Promise<void> startResponse(StartResponseContext context) override {
    KJ_REQUIRE(!sent, "already called startResponse() or startWebSocket()");
    sent = true;
    state->assertNotCanceled();

    auto params = context.getParams();
    auto rpcResponse = params.getResponse();

    auto bodySize = rpcResponse.getBodySize();
    kj::Maybe<uint64_t> expectedSize;
    bool hasBody = true;
    if (bodySize.isFixed()) {
      auto size = bodySize.getFixed();
      expectedSize = bodySize.getFixed();
      hasBody = size > 0;
    }

    auto bodyStream = kjResponse.send(rpcResponse.getStatusCode(), rpcResponse.getStatusText(),
        factory.headersToKj(rpcResponse.getHeaders()), expectedSize);

    auto results = context.getResults(MessageSize { 16, 1 });
    if (hasBody) {
      auto pipe = kj::newOneWayPipe();
      results.setBody(factory.streamFactory.kjToCapnp(kj::mv(pipe.out)));
      state->addTask(pipe.in->pumpTo(*bodyStream)
          .ignoreResult()
          .attach(kj::mv(bodyStream), kj::mv(pipe.in)));
    }
    return kj::READY_NOW;
  }

  kj::Promise<void> startWebSocket(StartWebSocketContext context) override {
    KJ_REQUIRE(!sent, "already called startResponse() or startWebSocket()");
    sent = true;
    state->assertNotCanceled();

    auto params = context.getParams();

    auto shorteningPaf = kj::newPromiseAndFulfiller<kj::Promise<Capability::Client>>();

    auto ownWebSocket = kjResponse.acceptWebSocket(factory.headersToKj(params.getHeaders()));
    auto& webSocket = *ownWebSocket;
    state->holdWebSocket(kj::mv(ownWebSocket));

    auto upWrapper = kj::heap<KjToCapnpWebSocketAdapter>(
        nullptr, params.getUpSocket(), kj::mv(shorteningPaf.fulfiller));
    state->addTask(webSocket.pumpTo(*upWrapper).attach(kj::mv(upWrapper))
        .catch_([&webSocket=webSocket](kj::Exception&& e) -> kj::Promise<void> {
      // The pump in the client -> server direction failed. The error may have originated from
      // either the client or the server. In case it came from the server, we want to call .abort()
      // to propagate the problem back to the client. If the error came from the client, then
      // .abort() probably is a noop.
      webSocket.abort();
      return kj::mv(e);
    }));

    auto results = context.getResults(MessageSize { 16, 1 });
    results.setDownSocket(kj::heap<CapnpToKjWebSocketAdapter>(
        kj::addRef(*state), webSocket, kj::mv(shorteningPaf.promise)));

    return kj::READY_NOW;
  }

private:
  HttpOverCapnpFactory& factory;
  kj::Own<RequestState> state;
  bool sent = false;

  kj::HttpService::Response& kjResponse;
  // Must check state->assertNotCanceled() before using this.
};

class HttpOverCapnpFactory::ConnectClientRequestContextImpl final
    : public capnp::HttpService::ConnectClientRequestContext::Server {
public:
  ConnectClientRequestContextImpl(HttpOverCapnpFactory& factory,
      kj::HttpService::ConnectResponse& connResponse)
      : factory(factory), connResponse(connResponse) {}

  kj::Promise<void> startConnect(StartConnectContext context) override {
    KJ_REQUIRE(!sent, "already called startConnect() or startError()");
    sent = true;

    auto params = context.getParams();
    auto resp = params.getResponse();

    auto headers = factory.headersToKj(resp.getHeaders());
    connResponse.accept(resp.getStatusCode(), resp.getStatusText(), headers);

    return kj::READY_NOW;
  }

  kj::Promise<void> startError(StartErrorContext context) override {
    KJ_REQUIRE(!sent, "already called startConnect() or startError()");
    sent = true;

    auto params = context.getParams();
    auto resp = params.getResponse();

    auto headers = factory.headersToKj(resp.getHeaders());

    auto bodySize = resp.getBodySize();
    kj::Maybe<uint64_t> expectedSize;
    if (bodySize.isFixed()) {
      expectedSize = bodySize.getFixed();
    }

    auto stream = connResponse.reject(
        resp.getStatusCode(), resp.getStatusText(), headers, expectedSize);

    context.initResults().setBody(factory.streamFactory.kjToCapnp(kj::mv(stream)));

    return kj::READY_NOW;
  }

private:
  HttpOverCapnpFactory& factory;
  bool sent = false;

  kj::HttpService::ConnectResponse& connResponse;
};

class HttpOverCapnpFactory::KjToCapnpHttpServiceAdapter final: public kj::HttpService {
public:
  KjToCapnpHttpServiceAdapter(HttpOverCapnpFactory& factory, capnp::HttpService::Client inner)
      : factory(factory), inner(kj::mv(inner)) {}

  template <typename Params, typename Results, typename AwaitCompletionFunc>
  kj::Promise<void> requestImpl(
      Request<Params, Results> rpcRequest,
      kj::HttpMethod method, kj::StringPtr url, const kj::HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, kj::HttpService::Response& kjResponse,
      AwaitCompletionFunc&& awaitCompletion) {
    // Common implementation calling request() or startRequest(). awaitCompletion() waits for
    // final completion in a method-specific way.
    //
    // TODO(cleanup): When we move to C++17 or newer we can use `if constexpr` instead of a
    //   callback.

    auto metadata = rpcRequest.initRequest();
    metadata.setMethod(static_cast<capnp::HttpMethod>(method));
    metadata.setUrl(url);
    metadata.adoptHeaders(factory.headersToCapnp(
        headers, Orphanage::getForMessageContaining(metadata)));

    kj::Maybe<kj::AsyncInputStream&> maybeRequestBody;

    KJ_IF_MAYBE(s, requestBody.tryGetLength()) {
      metadata.getBodySize().setFixed(*s);
      if (*s == 0) {
        maybeRequestBody = nullptr;
      } else {
        maybeRequestBody = requestBody;
      }
    } else if ((method == kj::HttpMethod::GET || method == kj::HttpMethod::HEAD) &&
               headers.get(kj::HttpHeaderId::TRANSFER_ENCODING) == nullptr) {
      maybeRequestBody = nullptr;
      metadata.getBodySize().setFixed(0);
    } else {
      metadata.getBodySize().setUnknown();
      maybeRequestBody = requestBody;
    }

    auto state = kj::refcounted<RequestState>();
    auto deferredCancel = kj::defer([state = kj::addRef(*state)]() mutable {
      state->cancel();
    });

    rpcRequest.setContext(
        kj::heap<ClientRequestContextImpl>(factory, kj::addRef(*state), kjResponse));

    auto pipeline = rpcRequest.send();

    // Pump upstream -- unless we don't expect a request body.
    kj::Maybe<kj::Promise<void>> pumpRequestTask;
    KJ_IF_MAYBE(rb, maybeRequestBody) {
      auto bodyOut = factory.streamFactory.capnpToKjExplicitEnd(pipeline.getRequestBody());
      pumpRequestTask = rb->pumpTo(*bodyOut)
          .then([&bodyOut = *bodyOut](uint64_t) mutable {
        return bodyOut.end();
      }).eagerlyEvaluate([state = kj::addRef(*state), bodyOut = kj::mv(bodyOut)]
                         (kj::Exception&& e) mutable {
        // A DISCONNECTED exception probably means the server decided not to read the whole request
        // before responding. In that case we simply want the pump to end, so that on this end it
        // also appears that the service simply didn't read everything. So we don't propagate the
        // exception in that case. For any other exception, we want to merge the exception with
        // the final result.
        if (e.getType() != kj::Exception::Type::DISCONNECTED) {
          state->taskFailed(kj::mv(e));
        }
      });
    }

    // Wait for the server to indicate completion. Meanwhile, if the
    // promise is canceled from the client side, we propagate cancellation naturally, and we
    // also call state->cancel().
    return awaitCompletion(pipeline)
        // Once the server indicates it is done, then we can cancel pumping the request, because
        // obviously the server won't use it. We should not cancel pumping the response since there
        // could be data in-flight still.
        .attach(kj::mv(pumpRequestTask))
        // finishTasks() will wait for the respones to complete.
        .then([state = kj::mv(state)]() mutable { return state->finishTasks(); })
        .attach(kj::mv(deferredCancel));
  }

  kj::Promise<void> request(
      kj::HttpMethod method, kj::StringPtr url, const kj::HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, kj::HttpService::Response& kjResponse) override {
    if (factory.peerOptimizationLevel < LEVEL_2) {
      return requestImpl(inner.startRequestRequest(), method, url, headers, requestBody, kjResponse,
          [](auto& pipeline) { return pipeline.getContext().whenResolved(); });
    } else {
      return requestImpl(inner.requestRequest(), method, url, headers, requestBody, kjResponse,
          [](auto& pipeline) { return pipeline.ignoreResult(); });
    }
  }

  kj::Promise<void> connect(
      kj::StringPtr host, const kj::HttpHeaders& headers, kj::AsyncIoStream& connection,
      ConnectResponse& tunnel, kj::HttpConnectSettings settings) override {
    auto rpcRequest = inner.connectRequest();
    auto downPipe = kj::newOneWayPipe();
    rpcRequest.setHost(host);
    rpcRequest.setDown(factory.streamFactory.kjToCapnp(kj::mv(downPipe.out)));
    rpcRequest.initSettings().setUseTls(settings.useTls);

    auto context = kj::heap<ConnectClientRequestContextImpl>(factory, tunnel);
    RevocableServer<capnp::HttpService::ConnectClientRequestContext> revocableContext(*context);

    auto builder = capnp::Request<
        capnp::HttpService::ConnectParams,
        capnp::HttpService::ConnectResults>::Builder(rpcRequest);
    rpcRequest.adoptHeaders(factory.headersToCapnp(headers,
        Orphanage::getForMessageContaining(builder)));
    rpcRequest.setContext(revocableContext.getClient());
    RemotePromise<capnp::HttpService::ConnectResults> pipeline = rpcRequest.send();

    // We read from `downPipe` (the other side writes into it.)
    auto downPumpTask = downPipe.in->pumpTo(connection)
        .then([&connection, down = kj::mv(downPipe.in)](uint64_t) -> kj::Promise<void> {
      connection.shutdownWrite();
      return kj::NEVER_DONE;
    });
    // We write to `up` (the other side reads from it).
    auto up = pipeline.getUp();

    // We need to create a tlsStarter callback which sends a startTls request to the capnp server.
    KJ_IF_MAYBE(tlsStarter, settings.tlsStarter) {
      kj::Function<kj::Promise<void>(kj::StringPtr)> cb =
          [upForStartTls = kj::cp(up)]
          (kj::StringPtr expectedServerHostname)
          mutable -> kj::Promise<void> {
        auto startTlsRpcRequest = upForStartTls.startTlsRequest();
        startTlsRpcRequest.setExpectedServerHostname(expectedServerHostname);
        return startTlsRpcRequest.send();
      };
      *tlsStarter = kj::mv(cb);
    }

    auto upStream = factory.streamFactory.capnpToKjExplicitEnd(up);
    auto upPumpTask = connection.pumpTo(*upStream)
        .then([&upStream = *upStream](uint64_t) mutable {
      return upStream.end();
    }).then([up = kj::mv(up), upStream = kj::mv(upStream)]() mutable
        -> kj::Promise<void> {
      return kj::NEVER_DONE;
    });

    return pipeline.ignoreResult()
        .attach(kj::mv(downPumpTask), kj::mv(upPumpTask), kj::mv(revocableContext))
        // Separate attach to make sure `revocableContext` is destroyed before `context`.
        .attach(kj::mv(context));
  }


private:
  HttpOverCapnpFactory& factory;
  capnp::HttpService::Client inner;
};

kj::Own<kj::HttpService> HttpOverCapnpFactory::capnpToKj(capnp::HttpService::Client rpcService) {
  return kj::heap<KjToCapnpHttpServiceAdapter>(*this, kj::mv(rpcService));
}

// =======================================================================================

namespace {

class NullInputStream final: public kj::AsyncInputStream {
  // TODO(cleanup): This class has been replicated in a bunch of places now, make it public
  //   somewhere.

public:
  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return kj::constPromise<size_t, 0>();
  }

  kj::Maybe<uint64_t> tryGetLength() override {
    return uint64_t(0);
  }

  kj::Promise<uint64_t> pumpTo(kj::AsyncOutputStream& output, uint64_t amount) override {
    return kj::constPromise<uint64_t, 0>();
  }
};

class NullOutputStream final: public kj::AsyncOutputStream {
  // TODO(cleanup): This class has been replicated in a bunch of places now, make it public
  //   somewhere.

public:
  kj::Promise<void> write(const void* buffer, size_t size) override {
    return kj::READY_NOW;
  }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    return kj::READY_NOW;
  }
  kj::Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }

  // We can't really optimize tryPumpFrom() unless AsyncInputStream grows a skip() method.
};

class ResolvedServerRequestContext final: public capnp::HttpService::ServerRequestContext::Server {
public:
  // Nothing! It's done.
};

}  // namespace

class HttpOverCapnpFactory::HttpServiceResponseImpl
    : public kj::HttpService::Response {
public:
  HttpServiceResponseImpl(HttpOverCapnpFactory& factory,
                          capnp::HttpRequest::Reader request,
                          capnp::HttpService::ClientRequestContext::Client clientContext)
      : factory(factory),
        method(validateMethod(request.getMethod())),
        url(request.getUrl()),
        headers(factory.headersToKj(request.getHeaders())),
        clientContext(kj::mv(clientContext)) {}

  kj::Own<kj::AsyncOutputStream> send(
      uint statusCode, kj::StringPtr statusText, const kj::HttpHeaders& headers,
      kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
    KJ_REQUIRE(replyTask == nullptr, "already called send() or acceptWebSocket()");

    auto req = clientContext.startResponseRequest();

    if (method == kj::HttpMethod::HEAD ||
        statusCode == 204 || statusCode == 304) {
      expectedBodySize = uint64_t(0);
    }

    auto rpcResponse = req.initResponse();
    rpcResponse.setStatusCode(statusCode);
    rpcResponse.setStatusText(statusText);
    rpcResponse.adoptHeaders(factory.headersToCapnp(
        headers, Orphanage::getForMessageContaining(rpcResponse)));
    bool hasBody = true;
    KJ_IF_MAYBE(s, expectedBodySize) {
      rpcResponse.getBodySize().setFixed(*s);
      hasBody = *s > 0;
    }

    auto logError = [hasBody](kj::Exception&& e) {
      KJ_LOG(INFO, "HTTP-over-RPC startResponse() failed", hasBody, e);
    };
    if (hasBody) {
      auto pipeline = req.send();
      auto result = factory.streamFactory.capnpToKj(pipeline.getBody());
      replyTask = pipeline.ignoreResult().eagerlyEvaluate(kj::mv(logError));
      return result;
    } else {
      replyTask = req.send().ignoreResult().eagerlyEvaluate(kj::mv(logError));
      return kj::heap<NullOutputStream>();
    }

    // We don't actually wait for replyTask anywhere, because we may be all done with this HTTP
    // message before the client gets a chance to respond, and we don't want to force an extra
    // network round trip. If the client fails this call that's the client's problem, really.
  }

  kj::Own<kj::WebSocket> acceptWebSocket(const kj::HttpHeaders& headers) override {
    KJ_REQUIRE(replyTask == nullptr, "already called send() or acceptWebSocket()");

    auto req = clientContext.startWebSocketRequest();

    req.adoptHeaders(factory.headersToCapnp(
        headers, Orphanage::getForMessageContaining(
            capnp::HttpService::ClientRequestContext::StartWebSocketParams::Builder(req))));

    auto pipe = kj::newWebSocketPipe();
    auto shorteningPaf = kj::newPromiseAndFulfiller<kj::Promise<Capability::Client>>();

    // We don't need the RequestState mechanism on the server side because
    // CapnpToKjWebSocketAdapter wraps a pipe end, and that pipe end can continue to exist beyond
    // the lifetime of the request, because the other end will have been dropped. We only create
    // a RequestState here so that we can reuse the implementation of CapnpToKjWebSocketAdapter
    // that needs this for the client side.
    auto dummyState = kj::refcounted<RequestState>();
    auto& pipeEnd0Ref = *pipe.ends[0];
    dummyState->holdWebSocket(kj::mv(pipe.ends[0]));
    req.setUpSocket(kj::heap<CapnpToKjWebSocketAdapter>(
        kj::mv(dummyState), pipeEnd0Ref, kj::mv(shorteningPaf.promise)));

    auto pipeline = req.send();
    auto result = kj::heap<KjToCapnpWebSocketAdapter>(
        kj::mv(pipe.ends[1]), pipeline.getDownSocket(), kj::mv(shorteningPaf.fulfiller));

    // Note we need eagerlyEvaluate() here to force proactively discarding the response object,
    // since it holds a reference to `downSocket`.
    replyTask = pipeline.ignoreResult()
        .eagerlyEvaluate([](kj::Exception&& e) {
      KJ_LOG(INFO, "HTTP-over-RPC startWebSocketRequest() failed", e);
    });

    return result;
  }

  HttpOverCapnpFactory& factory;
  kj::HttpMethod method;
  kj::StringPtr url;
  kj::HttpHeaders headers;
  capnp::HttpService::ClientRequestContext::Client clientContext;
  kj::Maybe<kj::Promise<void>> replyTask;

  static kj::HttpMethod validateMethod(capnp::HttpMethod method) {
    KJ_REQUIRE(method <= capnp::HttpMethod::UNSUBSCRIBE, "unknown method", method);
    return static_cast<kj::HttpMethod>(method);
  }
};

class HttpOverCapnpFactory::HttpOverCapnpConnectResponseImpl final :
    public kj::HttpService::ConnectResponse {
public:
  HttpOverCapnpConnectResponseImpl(
      HttpOverCapnpFactory& factory,
      capnp::HttpService::ConnectClientRequestContext::Client context) :
      context(context), factory(factory) {}

  void accept(uint statusCode, kj::StringPtr statusText, const kj::HttpHeaders& headers) override {
    KJ_REQUIRE(replyTask == nullptr, "already called accept() or reject()");

    auto req = context.startConnectRequest();
    auto rpcResponse = req.initResponse();
    rpcResponse.setStatusCode(statusCode);
    rpcResponse.setStatusText(statusText);
    rpcResponse.adoptHeaders(factory.headersToCapnp(
        headers, Orphanage::getForMessageContaining(rpcResponse)));

    replyTask = req.send().ignoreResult();
  }

  kj::Own<kj::AsyncOutputStream> reject(
      uint statusCode,
      kj::StringPtr statusText,
      const kj::HttpHeaders& headers,
      kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
    KJ_REQUIRE(replyTask == nullptr, "already called accept() or reject()");
    auto pipe = kj::newOneWayPipe(expectedBodySize);

    auto req = context.startErrorRequest();
    auto rpcResponse = req.initResponse();
    rpcResponse.setStatusCode(statusCode);
    rpcResponse.setStatusText(statusText);
    rpcResponse.adoptHeaders(factory.headersToCapnp(
        headers, Orphanage::getForMessageContaining(rpcResponse)));

    auto errorBody = kj::mv(pipe.in);
    // Set the body size if the error body exists.
    KJ_IF_MAYBE(size, errorBody->tryGetLength()) {
      rpcResponse.getBodySize().setFixed(*size);
    }

    replyTask = req.send().then(
        [this, errorBody = kj::mv(errorBody)](auto resp) mutable -> kj::Promise<void> {
      auto body = factory.streamFactory.capnpToKjExplicitEnd(resp.getBody());
      return errorBody->pumpTo(*body)
          .then([&body = *body](uint64_t) mutable {
        return body.end();
      }).attach(kj::mv(errorBody), kj::mv(body));
    });

    return kj::mv(pipe.out);
  }

  capnp::HttpService::ConnectClientRequestContext::Client context;
  capnp::HttpOverCapnpFactory& factory;
  kj::Maybe<kj::Promise<void>> replyTask;
};


class HttpOverCapnpFactory::ServerRequestContextImpl final
    : public capnp::HttpService::ServerRequestContext::Server,
      public HttpServiceResponseImpl {
public:
  ServerRequestContextImpl(HttpOverCapnpFactory& factory,
                           HttpService::Client serviceCap,
                           kj::Own<capnp::HttpRequest::Reader> request,
                           capnp::HttpService::ClientRequestContext::Client clientContext,
                           kj::Own<kj::AsyncInputStream> requestBodyIn,
                           kj::HttpService& kjService)
      : HttpServiceResponseImpl(factory, *request, kj::mv(clientContext)),
        request(kj::mv(request)),
        serviceCap(kj::mv(serviceCap)),
        // Note we attach `requestBodyIn` to `task` so that we will implicitly cancel reading
        // the request body as soon as the service returns. This is important in particular when
        // the request body is not fully consumed, in order to propagate cancellation.
        task(kjService.request(method, url, headers, *requestBodyIn, *this)
                      .attach(kj::mv(requestBodyIn))) {}

  kj::Maybe<kj::Promise<Capability::Client>> shortenPath() override {
    return task.then([]() -> Capability::Client {
      // If all went well, resolve to a settled capability.
      // TODO(perf): Could save a message by resolving to a capability hosted by the client, or
      //     some special "null" capability that isn't an error but is still transmitted by value.
      //     Otherwise we need a Release message from client -> server just to drop this...
      return kj::heap<ResolvedServerRequestContext>();
    });
  }

  KJ_DISALLOW_COPY_AND_MOVE(ServerRequestContextImpl);

private:
  kj::Own<capnp::HttpRequest::Reader> request;
  HttpService::Client serviceCap;  // ensures the inner kj::HttpService isn't destroyed
  kj::Promise<void> task;
};

class HttpOverCapnpFactory::CapnpToKjHttpServiceAdapter final: public capnp::HttpService::Server {
public:
  CapnpToKjHttpServiceAdapter(HttpOverCapnpFactory& factory, kj::Own<kj::HttpService> inner)
      : factory(factory), inner(kj::mv(inner)) {}

  template <typename Params, typename Results, typename Callback>
  kj::Promise<void> requestImpl(CallContext<Params, Results> context, Callback&& callback) {
    // Common implementation of request() and startRequest(). callback() performs the
    // method-specific stuff at the end.
    //
    // TODO(cleanup): When we move to C++17 or newer we can use `if constexpr` instead of a
    //   callback.

    auto params = context.getParams();
    auto metadata = params.getRequest();

    auto bodySize = metadata.getBodySize();
    kj::Maybe<uint64_t> expectedSize;
    bool hasBody = true;
    if (bodySize.isFixed()) {
      auto size = bodySize.getFixed();
      expectedSize = bodySize.getFixed();
      hasBody = size > 0;
    }

    auto results = context.getResults(MessageSize {8, 2});
    kj::Own<kj::AsyncInputStream> requestBody;
    if (hasBody) {
      auto pipe = kj::newOneWayPipe(expectedSize);
      auto requestBodyCap = factory.streamFactory.kjToCapnp(kj::mv(pipe.out));

      if (kj::isSameType<Results, RequestResults>()) {
        // For request(), use context.setPipeline() to enable pipelined calls to the request body
        // stream before this RPC completes. (We don't bother when using startRequest() because
        // it returns immediately anyway, so this would just waste effort.)
        PipelineBuilder<Results> pipeline;
        pipeline.setRequestBody(kj::cp(requestBodyCap));
        context.setPipeline(pipeline.build());
      }

      results.setRequestBody(kj::mv(requestBodyCap));
      requestBody = kj::mv(pipe.in);
    } else {
      requestBody = kj::heap<NullInputStream>();
    }

    return callback(results, metadata, params, requestBody);
  }

  kj::Promise<void> request(RequestContext context) override {
    return requestImpl(kj::mv(context),
        [&](auto& results, auto& metadata, auto& params, auto& requestBody) {
      class FinalHttpServiceResponseImpl final: public HttpServiceResponseImpl {
      public:
        using HttpServiceResponseImpl::HttpServiceResponseImpl;
      };
      auto impl = kj::heap<FinalHttpServiceResponseImpl>(factory, metadata, params.getContext());
      auto promise = inner->request(impl->method, impl->url, impl->headers, *requestBody, *impl);
      return promise.attach(kj::mv(requestBody), kj::mv(impl));
    });
  }

  kj::Promise<void> startRequest(StartRequestContext context) override {
    return requestImpl(kj::mv(context),
        [&](auto& results, auto& metadata, auto& params, auto& requestBody) {
      results.setContext(kj::heap<ServerRequestContextImpl>(
          factory, thisCap(), capnp::clone(metadata), params.getContext(), kj::mv(requestBody),
          *inner));

      return kj::READY_NOW;
    });
  }

  kj::Promise<void> connect(ConnectContext context) override {
    auto params = context.getParams();
    auto host = params.getHost();
    kj::Own<kj::TlsStarterCallback> tlsStarter = kj::heap<kj::TlsStarterCallback>();
    kj::HttpConnectSettings settings = { .useTls = params.getSettings().getUseTls()};
    settings.tlsStarter = tlsStarter;
    auto headers = factory.headersToKj(params.getHeaders());
    auto pipe = kj::newTwoWayPipe();

    class EofDetector final: public kj::AsyncOutputStream {
    public:
      EofDetector(kj::Own<kj::AsyncIoStream> inner)
          : inner(kj::mv(inner)) {}
      ~EofDetector() {
        inner->shutdownWrite();
      }

      kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(
          kj::AsyncInputStream& input, uint64_t amount = kj::maxValue) override {
        return inner->tryPumpFrom(input, amount);
      }

      kj::Promise<void> write(const void* buffer, size_t size) override {
        return inner->write(buffer, size);
      }

      kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
        return inner->write(pieces);
      }

      kj::Promise<void> whenWriteDisconnected() override {
        return inner->whenWriteDisconnected();
      }
    private:
      kj::Own<kj::AsyncIoStream> inner;
    };

    auto stream = factory.streamFactory.capnpToKjExplicitEnd(context.getParams().getDown());

    // We want to keep the stream alive even after EofDetector is destroyed, so we need to create
    // a refcounted AsyncIoStream.
    auto refcounted = kj::refcountedWrapper(kj::mv(pipe.ends[1]));
    kj::Own<kj::AsyncIoStream> ref1 = refcounted->addWrappedRef();
    kj::Own<kj::AsyncIoStream> ref2 = refcounted->addWrappedRef();

    // We write to the `down` pipe.
    auto pumpTask = ref1->pumpTo(*stream)
          .then([&stream = *stream](uint64_t) mutable {
      return stream.end();
    }).then([httpProxyStream = kj::mv(ref1), stream = kj::mv(stream)]() mutable
        -> kj::Promise<void> {
      return kj::NEVER_DONE;
    });

    PipelineBuilder<ConnectResults> pb;
    auto eofWrapper = kj::heap<EofDetector>(kj::mv(ref2));
    auto up = factory.streamFactory.kjToCapnp(kj::mv(eofWrapper), kj::mv(tlsStarter));
    pb.setUp(kj::cp(up));

    context.setPipeline(pb.build());
    context.initResults(capnp::MessageSize { 4, 1 }).setUp(kj::mv(up));

    auto response = kj::heap<HttpOverCapnpConnectResponseImpl>(
        factory, context.getParams().getContext());

    return inner->connect(host, headers, *pipe.ends[0], *response, settings).attach(
        kj::mv(host), kj::mv(headers), kj::mv(response), kj::mv(pipe))
        .exclusiveJoin(kj::mv(pumpTask));
  }

private:
  HttpOverCapnpFactory& factory;
  kj::Own<kj::HttpService> inner;
};

capnp::HttpService::Client HttpOverCapnpFactory::kjToCapnp(kj::Own<kj::HttpService> service) {
  return kj::heap<CapnpToKjHttpServiceAdapter>(*this, kj::mv(service));
}

// =======================================================================================

static constexpr uint64_t COMMON_TEXT_ANNOTATION = 0x857745131db6fc83ull;
// Type ID of `commonText` from `http.capnp`.
// TODO(cleanup): Cap'n Proto should auto-generate constants for these.

HttpOverCapnpFactory::HeaderIdBundle::HeaderIdBundle(kj::HttpHeaderTable::Builder& builder)
    : table(builder.getFutureTable()) {
  auto commonHeaderNames = Schema::from<capnp::CommonHeaderName>().getEnumerants();
  nameCapnpToKj = kj::heapArray<kj::HttpHeaderId>(commonHeaderNames.size());
  for (size_t i = 1; i < commonHeaderNames.size(); i++) {
    kj::StringPtr nameText;
    for (auto ann: commonHeaderNames[i].getProto().getAnnotations()) {
      if (ann.getId() == COMMON_TEXT_ANNOTATION) {
        nameText = ann.getValue().getText();
        break;
      }
    }
    KJ_ASSERT(nameText != nullptr);
    kj::HttpHeaderId headerId = builder.add(nameText);
    nameCapnpToKj[i] = headerId;
    maxHeaderId = kj::max(maxHeaderId, headerId.hashCode());
  }
}

HttpOverCapnpFactory::HeaderIdBundle::HeaderIdBundle(
    const kj::HttpHeaderTable& table, kj::Array<kj::HttpHeaderId> nameCapnpToKj, size_t maxHeaderId)
    : table(table), nameCapnpToKj(kj::mv(nameCapnpToKj)), maxHeaderId(maxHeaderId) {}

HttpOverCapnpFactory::HeaderIdBundle HttpOverCapnpFactory::HeaderIdBundle::clone() const {
  return HeaderIdBundle(table, kj::heapArray<kj::HttpHeaderId>(nameCapnpToKj), maxHeaderId);
}

HttpOverCapnpFactory::HttpOverCapnpFactory(ByteStreamFactory& streamFactory,
                                           HeaderIdBundle headerIds,
                                           OptimizationLevel peerOptimizationLevel)
    : streamFactory(streamFactory), headerTable(headerIds.table),
      peerOptimizationLevel(peerOptimizationLevel),
      nameCapnpToKj(kj::mv(headerIds.nameCapnpToKj)) {
  auto commonHeaderNames = Schema::from<capnp::CommonHeaderName>().getEnumerants();
  nameKjToCapnp = kj::heapArray<capnp::CommonHeaderName>(headerIds.maxHeaderId + 1);
  for (auto& slot: nameKjToCapnp) slot = capnp::CommonHeaderName::INVALID;

  for (size_t i = 1; i < commonHeaderNames.size(); i++) {
    auto& slot = nameKjToCapnp[nameCapnpToKj[i].hashCode()];
    KJ_ASSERT(slot == capnp::CommonHeaderName::INVALID);
    slot = static_cast<capnp::CommonHeaderName>(i);
  }

  auto commonHeaderValues = Schema::from<capnp::CommonHeaderValue>().getEnumerants();
  valueCapnpToKj = kj::heapArray<kj::StringPtr>(commonHeaderValues.size());
  for (size_t i = 1; i < commonHeaderValues.size(); i++) {
    kj::StringPtr valueText;
    for (auto ann: commonHeaderValues[i].getProto().getAnnotations()) {
      if (ann.getId() == COMMON_TEXT_ANNOTATION) {
        valueText = ann.getValue().getText();
        break;
      }
    }
    KJ_ASSERT(valueText != nullptr);
    valueCapnpToKj[i] = valueText;
    valueKjToCapnp.insert(valueText, static_cast<capnp::CommonHeaderValue>(i));
  }
}

Orphan<List<capnp::HttpHeader>> HttpOverCapnpFactory::headersToCapnp(
    const kj::HttpHeaders& headers, Orphanage orphanage) {
  auto result = orphanage.newOrphan<List<capnp::HttpHeader>>(headers.size());
  auto rpcHeaders = result.get();
  uint i = 0;
  headers.forEach([&](kj::HttpHeaderId id, kj::StringPtr value) {
    auto capnpName = id.hashCode() < nameKjToCapnp.size()
        ? nameKjToCapnp[id.hashCode()]
        : capnp::CommonHeaderName::INVALID;
    if (capnpName == capnp::CommonHeaderName::INVALID) {
      auto header = rpcHeaders[i++].initUncommon();
      header.setName(id.toString());
      header.setValue(value);
    } else {
      auto header = rpcHeaders[i++].initCommon();
      header.setName(capnpName);
      header.setValue(value);
    }
  }, [&](kj::StringPtr name, kj::StringPtr value) {
    auto header = rpcHeaders[i++].initUncommon();
    header.setName(name);
    header.setValue(value);
  });
  KJ_ASSERT(i == rpcHeaders.size());
  return result;
}

kj::HttpHeaders HttpOverCapnpFactory::headersToKj(
    List<capnp::HttpHeader>::Reader capnpHeaders) const {
  kj::HttpHeaders result(headerTable);

  for (auto header: capnpHeaders) {
    switch (header.which()) {
      case capnp::HttpHeader::COMMON: {
        auto nv = header.getCommon();
        auto nameInt = static_cast<uint>(nv.getName());
        KJ_REQUIRE(nameInt < nameCapnpToKj.size(), "unknown common header name", nv.getName());

        switch (nv.which()) {
          case capnp::HttpHeader::Common::COMMON_VALUE: {
            auto cvInt = static_cast<uint>(nv.getCommonValue());
            KJ_REQUIRE(nameInt < valueCapnpToKj.size(),
                "unknown common header value", nv.getCommonValue());
            result.set(nameCapnpToKj[nameInt], valueCapnpToKj[cvInt]);
            break;
          }
          case capnp::HttpHeader::Common::VALUE: {
            auto headerId = nameCapnpToKj[nameInt];
            if (result.get(headerId) == nullptr) {
              result.set(headerId, nv.getValue());
            } else {
              // Unusual: This is a duplicate header, so fall back to add(), which may trigger
              //   comma-concatenation, except in certain cases where comma-concatentaion would
              //   be problematic.
              result.add(headerId.toString(), nv.getValue());
            }
            break;
          }
        }
        break;
      }
      case capnp::HttpHeader::UNCOMMON: {
        auto nv = header.getUncommon();
        result.add(nv.getName(), nv.getValue());
      }
    }
  }

  return result;
}

}  // namespace capnp
