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

// =======================================================================================

class HttpOverCapnpFactory::CapnpToKjWebSocketAdapter final: public capnp::WebSocket::Server {
public:
  CapnpToKjWebSocketAdapter(kj::WebSocket& webSocket,
                            kj::Promise<Capability::Client> shorteningPromise,
                            kj::Maybe<kj::Maybe<CapnpToKjWebSocketAdapter&>&> selfRef)
      : webSocket(webSocket),
        shorteningPromise(kj::mv(shorteningPromise)),
        selfRef(selfRef) {
    KJ_IF_SOME(s, selfRef) {
      s = *this;
    }
  }
  CapnpToKjWebSocketAdapter(kj::Own<kj::WebSocket> webSocket,
                            kj::Promise<Capability::Client> shorteningPromise)
      : webSocket(*webSocket), ownWebSocket(kj::mv(webSocket)),
        shorteningPromise(kj::mv(shorteningPromise)) {}
  // `onEnd` is resolved if and when the stream (in this direction) ends cleanly.
  //
  // `selfRef`, if given, will be initialized to point back to this object, and will be nulled
  // out in the destructor. This is intended to allow the caller to arrange to call cancel() if
  // the capability still exists when the underlying `webSocket` is about to go away.
  //
  // The second version of the constructor takes ownership of the underlying `webSocket`. In
  // this case, a `selfRef` isn't needed since there's no need to call `cancel()`.

  ~CapnpToKjWebSocketAdapter() noexcept(false) {
    // The peer dropped the capability, which means the WebSocket stream has ended. We want to
    // translate this to a `disconnect()` call on the `kj::WebSocket`, if it is still around.

    // Null out our self-ref, if any.
    KJ_IF_SOME(s, selfRef) {
      s = kj::none;
    }

    if (!shortened) {
      KJ_IF_SOME(ws, webSocket) {
        // We didn't get a close(), so this is an unexpected disconnect.
        //
        // Exception: If we path-shortened, then we expect that this capability will be dropped in
        // favor of the new shorter path, but we do NOT want to call `disconnect()` in this case
        // because it will raise an error. TODO(bug): This seems to be dependent on the
        // implementation details of WebSocketPipeImpl. WebSocketPipeEnd invokes in->abort() and
        // out->abort() in its destructor. BlockedPumpTo treats abort() as a non-erroneous
        // shutdown, which seems wrong, but treats disconnect() as erronous, which seems right, but
        // is what leads to the problem here. What we really want to do is cancel the pump when
        // the path is shortened. See KjToCapnpWebSocketAdapter::pumpTo() where
        // `shorteningFulfiller` is fulfilled, and then we start pumping -- that pump should be
        // canceled / complete without error at this point. Once that is fixed, we should change
        // WebSocketPipeImpl so that it doesn't treat simply dropping one of the ends as
        // successfully completing the pump! This a bit more refactoring than I want to do right
        // this moment.
        ws.disconnect();
      }
    }
  }

  void cancel() {
    // Called when the overall HTTP request completes or is canceled while this capability still
    // exists. Since we can't force the peer to drop the capability, we have to disable it.
    // Further access to `webSocket` must be blocked since it is no longer valid.
    //
    // Arguably we could instead use capnp::RevocableServer to accomplish something similar. The
    // problem is, we also do actually want to know when the peer drops this capability. With
    // RevocableServer, we no longer get notification of that -- the destructor runs when we tell
    // it to, rather than when the peer drops the cap.
    //
    // TODO(cleanup): Could RevocableServer be improved to allow us to notice the drop?
    //   Alternatively, maybe it's not really that important for us to call disconnect()
    //   proactively, considering:
    //   - The application can send a close message for explicit end.
    //   - A client->server disconnect will presumably cancel the whole request anyway.
    //   - A server->client disconnect will presumably be followed by the server returning from
    //     the request() RPC.

    // cancel() is only invoked in cases where we don't own the WebSocket.
    KJ_REQUIRE(ownWebSocket.get() == nullptr);

    // We don't call webSocket.disconnect() because the immediate caller is about to drop the
    // WebSocket anyway.

    selfRef = kj::none;
    webSocket = kj::none;

    if (!canceler.isEmpty()) {
      canceler.cancel(KJ_EXCEPTION(DISCONNECTED, "request canceled"));
    }
  }

  kj::Maybe<kj::Promise<Capability::Client>> shortenPath() override {
    auto onAbort = canceler.wrap(KJ_ASSERT_NONNULL(webSocket).whenAborted())
        .then([]() -> kj::Promise<Capability::Client> {
      return KJ_EXCEPTION(DISCONNECTED, "WebSocket was aborted");
    });
    return shorteningPromise
        .then([this](Capability::Client&& shorterPath) {
      shortened = true;
      return kj::mv(shorterPath);
    }).exclusiveJoin(kj::mv(onAbort));
  }

  kj::Promise<void> sendText(SendTextContext context) override {
    return wrap([&](kj::WebSocket& ws) { return ws.send(context.getParams().getText()); });
  }
  kj::Promise<void> sendData(SendDataContext context) override {
    return wrap([&](kj::WebSocket& ws) { return ws.send(context.getParams().getData()); });
  }
  kj::Promise<void> close(CloseContext context) override {
    auto params = context.getParams();
    return wrap([&](kj::WebSocket& ws) {
      // We shouldn't receive any more messages after close(), so null out `webSocket` here.
      // (This is actually important to prevent concurrent writes, since close() is NOT a
      // streaming method so won't block other method delivery.)
      webSocket = kj::none;

      return ws.close(params.getCode(), params.getReason())
          .attach(kj::mv(ownWebSocket));
    });
  }

private:
  kj::Maybe<kj::WebSocket&> webSocket;  // becomes none when canceled
  kj::Own<kj::WebSocket> ownWebSocket;
  kj::Promise<Capability::Client> shorteningPromise;
  kj::Maybe<kj::Maybe<CapnpToKjWebSocketAdapter&>&> selfRef;

  kj::Canceler canceler;

  kj::Maybe<kj::Own<kj::Exception>> error;

  bool shortened = false;

  kj::WebSocket& getWebSocket() {
    return KJ_REQUIRE_NONNULL(webSocket, "request canceled");
  }

  template <typename Func>
  kj::Promise<void> wrap(Func&& func) {
    KJ_IF_SOME(e, error) {
      kj::throwFatalException(kj::cp(*e));
    }

    // Detect cancellation (of the operation) and mark the object broken in this case.
    bool done = false;
    KJ_DEFER({
      if (!done && error == kj::none) {
        error = kj::heap(KJ_EXCEPTION(FAILED,
            "a write was canceled before completing, breaking the WebSocket"));
      }
    });

    try {
      KJ_IF_SOME(ws, webSocket) {
        co_await canceler.wrap(func(ws));
      } else {
        kj::throwFatalException(KJ_EXCEPTION(DISCONNECTED, "request canceled"));
      }
    } catch (...) {
      auto e = kj::getCaughtExceptionAsKj();
      error = kj::heap(kj::cp(e));
      kj::throwFatalException(kj::mv(e));
    }

    done = true;
  }
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

  void disconnect() override {
    out = kj::none;
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
    KJ_IF_SOME(optimized, kj::dynamicDowncastIfAvailable<KjToCapnpWebSocketAdapter>(other)) {
      shorteningFulfiller->fulfill(
          kj::cp(KJ_REQUIRE_NONNULL(optimized.out, "already called disconnect()")));

      // We expect the `in` pipe will stop receiving messages after the redirect, but we need to
      // pump anything already in-flight.
      return KJ_ASSERT_NONNULL(in)->pumpTo(other);
    } else KJ_IF_SOME(promise, other.tryPumpFrom(*this)) {
      // We may have unwrapped some layers around `other` leading to a shorter path.
      return kj::mv(promise);
    } else {
      return KJ_ASSERT_NONNULL(in)->pumpTo(other);
    }
  }

  uint64_t sentByteCount() override { return sentBytes; }
  uint64_t receivedByteCount() override { return KJ_ASSERT_NONNULL(in)->receivedByteCount(); }

  kj::Maybe<kj::String> getPreferredExtensions(ExtensionsContext ctx) override {
    // TODO(someday): Optimized pump is tricky with HttpOverCapnp, we may want to revisit
    // this but for now we always return none (indicating no preference).
    return kj::none;
  };

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
                           kj::HttpService::Response& kjResponse)
      : factory(factory), kjResponse(kjResponse) {}

  ~ClientRequestContextImpl() noexcept(false) {
    KJ_IF_SOME(ws, maybeWebSocket) {
      ws.cancel();
    }
  }

  kj::Promise<void> startResponse(StartResponseContext context) override {
    KJ_REQUIRE(!sentResponse, "already called startResponse() or startWebSocket()");
    sentResponse = true;

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
        factory.capnpToKj(rpcResponse.getHeaders()), expectedSize);

    auto results = context.getResults(MessageSize { 16, 1 });
    if (hasBody) {
      auto pipe = kj::newOneWayPipe();
      results.setBody(factory.streamFactory.kjToCapnp(kj::mv(pipe.out)));
      responsePumpTask = pipe.in->pumpTo(*bodyStream)
          .ignoreResult()
          .attach(kj::mv(bodyStream), kj::mv(pipe.in));
    }
    return kj::READY_NOW;
  }

  kj::Promise<void> startWebSocket(StartWebSocketContext context) override {
    KJ_REQUIRE(!sentResponse, "already called startResponse() or startWebSocket()");
    sentResponse = true;

    auto params = context.getParams();

    auto shorteningPaf = kj::newPromiseAndFulfiller<kj::Promise<Capability::Client>>();

    auto webSocket = kjResponse.acceptWebSocket(factory.capnpToKj(params.getHeaders()));

    auto upWrapper = kj::heap<KjToCapnpWebSocketAdapter>(
        kj::none, params.getUpSocket(), kj::mv(shorteningPaf.fulfiller));
    responsePumpTask = webSocket->pumpTo(*upWrapper).attach(kj::mv(upWrapper))
        .catch_([&webSocket=*webSocket](kj::Exception&& e) -> kj::Promise<void> {
      // The pump in the client -> server direction failed. The error may have originated from
      // either the client or the server. In case it came from the server, we want to call .abort()
      // to propagate the problem back to the client. If the error came from the client, then
      // .abort() probably is a noop.
      webSocket.abort();
      return kj::mv(e);
    });

    auto results = context.getResults(MessageSize { 16, 1 });
    auto downSocket = kj::heap<CapnpToKjWebSocketAdapter>(
        *webSocket, kj::mv(shorteningPaf.promise), maybeWebSocket);
    results.setDownSocket(kj::mv(downSocket));

    // We need to hold onto this WebSocket until `CapnpToKjWebSocketAdapter` is canceled or
    // destroyed. If `responsePumpTask`completes successfully, then `CapnpToKjWebSocketAdapter`
    // has to have been destroyed, since `downPaf.promise` doesn't resolve until that point. But
    // in the case of request cancellation, it is our own destructor that will call `cancel()`
    // on the `CapnpToKjWebSocketAdapter`, so we should make sure the `webSocket` outlives that.
    //
    // (Additionally, the WebSocket must outlive `responsePumpTask` itself, even when it is
    // canceled.)
    ownWebSocket = kj::mv(webSocket);

    return kj::READY_NOW;
  }

  kj::Promise<void> finishPump() {
    if (sentResponse) {
      return finishPumpInner();
    } else {
      // It seems the client->server request() RPC finished before we got a server->client
      // startResponse() call. Maybe the server forgot to call it, but another possibility is
      // an ordering issue: When a call is received, it is delayed by kj::evalLater(). It's
      // possible the return message was delivered during this delay, giving the impression that
      // the return beat the call. We can compensate by yielding here.
      //
      // TODO(someday): Perhaps calls *shouldn't* yield? The `evalLater()`'s purpose is really
      //   only to make sure the call doesn't occur synchronously, but it's perhaps a little
      //   too aggressive to schedule it breadth-first. A depth-first schedule probably makes
      //   more sense, and would allow the call to be fully delivered before more messages
      //   are handled. That's kind of a big change though, needs testing.
      return kj::yield().then([this]() { return finishPumpInner(); });
    }
  }

  bool hasSentResponse() {
    return sentResponse;
  }

private:
  HttpOverCapnpFactory& factory;
  kj::Maybe<kj::Own<kj::WebSocket>> ownWebSocket;
  kj::Maybe<kj::Promise<void>> responsePumpTask;
  kj::Maybe<CapnpToKjWebSocketAdapter&> maybeWebSocket;

  kj::HttpService::Response& kjResponse;
  bool sentResponse = false;

  kj::Promise<void> finishPumpInner() {
    KJ_IF_SOME(r, responsePumpTask) {
      return kj::mv(r);
    } else {
      return kj::READY_NOW;
    }
  }
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

    auto headers = factory.capnpToKj(resp.getHeaders());
    connResponse.accept(resp.getStatusCode(), resp.getStatusText(), headers);

    return kj::READY_NOW;
  }

  kj::Promise<void> startError(StartErrorContext context) override {
    KJ_REQUIRE(!sent, "already called startConnect() or startError()");
    sent = true;

    auto params = context.getParams();
    auto resp = params.getResponse();

    auto headers = factory.capnpToKj(resp.getHeaders());

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

  kj::Promise<void> request(
      kj::HttpMethod method, kj::StringPtr url, const kj::HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, kj::HttpService::Response& kjResponse) override {
    auto rpcRequest = inner.requestRequest();

    auto metadata = rpcRequest.initRequest();
    metadata.setMethod(static_cast<capnp::HttpMethod>(method));
    metadata.setUrl(url);
    metadata.adoptHeaders(factory.headersToCapnp(
        headers, Orphanage::getForMessageContaining(metadata)));

    kj::Maybe<kj::AsyncInputStream&> maybeRequestBody;

    KJ_IF_SOME(s, requestBody.tryGetLength()) {
      metadata.getBodySize().setFixed(s);
      if (s == 0) {
        maybeRequestBody = kj::none;
      } else {
        maybeRequestBody = requestBody;
      }
    } else if ((method == kj::HttpMethod::GET || method == kj::HttpMethod::HEAD) &&
               headers.get(kj::HttpHeaderId::TRANSFER_ENCODING) == kj::none) {
      maybeRequestBody = kj::none;
      metadata.getBodySize().setFixed(0);
    } else {
      metadata.getBodySize().setUnknown();
      maybeRequestBody = requestBody;
    }

    ClientRequestContextImpl context(factory, kjResponse);
    RevocableServer<capnp::HttpService::ClientRequestContext> revocableContext(context);
    KJ_DEFER({
      if (!context.hasSentResponse()) {
        // Client is disconnecting before server has sent a response. Make sure to revoke with a
        // DISCONNECTED exception here so that the server side doesn't log a spurious error.
        revocableContext.revoke(KJ_EXCEPTION(DISCONNECTED,
            "client disconnected before HTTP-over-capnp response was sent"));
      }
    });

    rpcRequest.setContext(revocableContext.getClient());

    auto pipeline = rpcRequest.send();

    // Make sure the request message isn't pinned into memory through the co_await below.
    { auto drop = kj::mv(rpcRequest); }

    // Pump upstream -- unless we don't expect a request body.
    kj::Maybe<kj::Own<kj::Exception>> pumpRequestFailedReason;
    kj::Maybe<kj::Promise<void>> pumpRequestTask;
    KJ_IF_SOME(rb, maybeRequestBody) {
      auto bodyOut = factory.streamFactory.capnpToKjExplicitEnd(pipeline.getRequestBody());
      pumpRequestTask = rb.pumpTo(*bodyOut)
          .then([&bodyOut = *bodyOut](uint64_t) mutable {
        return bodyOut.end();
      }).eagerlyEvaluate([&pumpRequestFailedReason, bodyOut = kj::mv(bodyOut)]
                         (kj::Exception&& e) mutable {
        // A DISCONNECTED exception probably means the server decided not to read the whole request
        // before responding. In that case we simply want the pump to end, so that on this end it
        // also appears that the service simply didn't read everything. So we don't propagate the
        // exception in that case. For any other exception, we want to merge the exception with
        // the final result.
        if (e.getType() != kj::Exception::Type::DISCONNECTED) {
          pumpRequestFailedReason = kj::heap(kj::mv(e));
        }
      });
    }

    // Wait for the server to indicate completion. Meanwhile, if the
    // promise is canceled from the client side, we propagate cancellation naturally.
    co_await pipeline.ignoreResult();

    // Once the server indicates it is done, then we can cancel pumping the request, because
    // obviously the server won't use it. We should not cancel pumping the response since there
    // could be data in-flight still.
    { auto drop = kj::mv(pumpRequestTask); }

    // If the request pump failed (for a non-disconnect reason) we'd better propagate that
    // exception.
    KJ_IF_SOME(e, pumpRequestFailedReason) {
      kj::throwFatalException(kj::mv(*e));
    }

    // Finish pumping the response or WebSocket. (Probably it's already finished.)
    try {
      co_await context.finishPump();
    } catch (...) {
      // Ignore DISCONNECTED exceptions from this pump, because it should have been the server's
      // responsibility to propagate any exceptions from pushing the response. If this were a local
      // HttpService (with no RPC layer), such exceptions would not propagate here, so we want to
      // do the same. Actually, technically, even non-DISCONNECTED exceptions arguably shouldn't
      // propagate here for the same reason. But, non-DISCONNECTED exceptions are more likely to
      // flag some real bug, so I'm leaving them alone for now. This could be revisited later.
      auto exception = kj::getCaughtExceptionAsKj();
      if (exception.getType() != kj::Exception::Type::DISCONNECTED) {
        kj::throwFatalException(kj::mv(exception));
      }
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

    ConnectClientRequestContextImpl context(factory, tunnel);
    RevocableServer<capnp::HttpService::ConnectClientRequestContext> revocableContext(context);

    auto builder = capnp::Request<
        capnp::HttpService::ConnectParams,
        capnp::HttpService::ConnectResults>::Builder(rpcRequest);
    rpcRequest.adoptHeaders(factory.headersToCapnp(headers,
        Orphanage::getForMessageContaining(builder)));
    rpcRequest.setContext(revocableContext.getClient());
    RemotePromise<capnp::HttpService::ConnectResults> pipeline = rpcRequest.send();

    // Make sure the request message isn't pinned into memory through the co_await below.
    { auto drop = kj::mv(rpcRequest); }

    // We read from `downPipe` (the other side writes into it.)
    auto downPumpTask = downPipe.in->pumpTo(connection)
        .then([&connection, down = kj::mv(downPipe.in)](uint64_t) -> kj::Promise<void> {
      connection.shutdownWrite();
      return kj::NEVER_DONE;
    });
    // We write to `up` (the other side reads from it).
    auto up = pipeline.getUp();

    // We need to create a tlsStarter callback which sends a startTls request to the capnp server.
    KJ_IF_SOME(tlsStarter, settings.tlsStarter) {
      kj::Function<kj::Promise<void>(kj::StringPtr)> cb =
          [upForStartTls = kj::cp(up)]
          (kj::StringPtr expectedServerHostname)
          mutable -> kj::Promise<void> {
        auto startTlsRpcRequest = upForStartTls.startTlsRequest();
        startTlsRpcRequest.setExpectedServerHostname(expectedServerHostname);
        return startTlsRpcRequest.send();
      };
      tlsStarter = kj::mv(cb);
    }

    auto upStream = factory.streamFactory.capnpToKjExplicitEnd(up);
    auto upPumpTask = connection.pumpTo(*upStream)
        .then([&upStream = *upStream](uint64_t) mutable {
      return upStream.end();
    }).then([up = kj::mv(up), upStream = kj::mv(upStream)]() mutable
        -> kj::Promise<void> {
      return kj::NEVER_DONE;
    });

    co_await pipeline.ignoreResult();
  }


private:
  HttpOverCapnpFactory& factory;
  capnp::HttpService::Client inner;
};

kj::Own<kj::HttpService> HttpOverCapnpFactory::capnpToKj(capnp::HttpService::Client rpcService) {
  return kj::heap<KjToCapnpHttpServiceAdapter>(*this, kj::mv(rpcService));
}

// =======================================================================================

class HttpOverCapnpFactory::HttpServiceResponseImpl final
    : public kj::HttpService::Response {
public:
  HttpServiceResponseImpl(HttpOverCapnpFactory& factory,
                          capnp::HttpRequest::Reader request,
                          capnp::HttpService::ClientRequestContext::Client clientContext)
      : factory(factory),
        method(validateMethod(request.getMethod())),
        url(request.getUrl()),
        headers(factory.capnpToKj(request.getHeaders())),
        clientContext(kj::mv(clientContext)) {}

  kj::Own<kj::AsyncOutputStream> send(
      uint statusCode, kj::StringPtr statusText, const kj::HttpHeaders& headers,
      kj::Maybe<uint64_t> expectedBodySize = kj::none) override {
    KJ_REQUIRE(!responseSent, "already called send() or acceptWebSocket()");
    responseSent = true;

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
    KJ_IF_SOME(s, expectedBodySize) {
      rpcResponse.getBodySize().setFixed(s);
      hasBody = s > 0;
    }

    if (hasBody) {
      auto pipeline = req.send();
      auto result = factory.streamFactory.capnpToKj(pipeline.getBody());
      dontWaitForRpc(kj::mv(pipeline));
      return result;
    } else {
      dontWaitForRpc(req.send());
      return kj::heap<kj::NullStream>();
    }
  }

  kj::Own<kj::WebSocket> acceptWebSocket(const kj::HttpHeaders& headers) override {
    KJ_REQUIRE(!responseSent, "already called send() or acceptWebSocket()");
    responseSent = true;

    auto req = clientContext.startWebSocketRequest();

    req.adoptHeaders(factory.headersToCapnp(
        headers, Orphanage::getForMessageContaining(
            capnp::HttpService::ClientRequestContext::StartWebSocketParams::Builder(req))));

    auto pipe = kj::newWebSocketPipe();
    auto shorteningPaf = kj::newPromiseAndFulfiller<kj::Promise<Capability::Client>>();

    // Note that since CapnpToKjWebSocketAdapter takes ownership of the pipe end, we don't need
    // to cancel it later. Dropping the other end of the pipe will have the same effect.
    req.setUpSocket(kj::heap<CapnpToKjWebSocketAdapter>(
        kj::mv(pipe.ends[0]), kj::mv(shorteningPaf.promise)));

    auto pipeline = req.send();
    auto result = kj::heap<KjToCapnpWebSocketAdapter>(
        kj::mv(pipe.ends[1]), pipeline.getDownSocket(), kj::mv(shorteningPaf.fulfiller));
    dontWaitForRpc(kj::mv(pipeline));

    return result;
  }

  HttpOverCapnpFactory& factory;
  kj::HttpMethod method;
  kj::StringPtr url;
  kj::HttpHeaders headers;
  capnp::HttpService::ClientRequestContext::Client clientContext;
  bool responseSent = false;

  static kj::HttpMethod validateMethod(capnp::HttpMethod method) {
    KJ_REQUIRE(method <= capnp::HttpMethod::UNSUBSCRIBE, "unknown method", method);
    return static_cast<kj::HttpMethod>(method);
  }

  template <typename T>
  void dontWaitForRpc(kj::Promise<T> promise) {
    // When we call clientContext.startResponse(), we really don't want to actually wait for
    // the reply to this RPC, because in many cases we will call this, write a response body,
    // and then immediately return from the HttpService::request() handler. At that point, we
    // would like CapnpToKjHttpServiceAdapter::request() to be able to propagate this return
    // immediately *without* waiting for a round trip to the client, so without waiting for
    // `startResponse()` to finish. However, we also do not want to inadvertently cancel
    // `startResponse()`, so we have to save the promise somewhere. Since this is an RPC that
    // doesn't depend on the lifetime of any other object... we will just detach() it.

    promise.detach([](kj::Exception&& e) {
      if (e.getType() == kj::Exception::Type::FAILED) {
        KJ_LOG(ERROR, e);
      }
    });
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
    KJ_REQUIRE(replyTask == kj::none, "already called accept() or reject()");

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
      kj::Maybe<uint64_t> expectedBodySize = kj::none) override {
    KJ_REQUIRE(replyTask == kj::none, "already called accept() or reject()");
    auto pipe = kj::newOneWayPipe(expectedBodySize);

    auto req = context.startErrorRequest();
    auto rpcResponse = req.initResponse();
    rpcResponse.setStatusCode(statusCode);
    rpcResponse.setStatusText(statusText);
    rpcResponse.adoptHeaders(factory.headersToCapnp(
        headers, Orphanage::getForMessageContaining(rpcResponse)));

    auto errorBody = kj::mv(pipe.in);
    // Set the body size if the error body exists.
    KJ_IF_SOME(size, errorBody->tryGetLength()) {
      rpcResponse.getBodySize().setFixed(size);
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


class HttpOverCapnpFactory::CapnpToKjHttpServiceAdapter final: public capnp::HttpService::Server {
public:
  CapnpToKjHttpServiceAdapter(HttpOverCapnpFactory& factory, kj::Own<kj::HttpService> inner)
      : factory(factory), inner(kj::mv(inner)) {}

  kj::Promise<void> request(RequestContext context) override {
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

      // For request(), use context.setPipeline() to enable pipelined calls to the request body
      // stream before this RPC completes.
      PipelineBuilder<RequestResults> pipeline;
      pipeline.setRequestBody(kj::cp(requestBodyCap));
      context.setPipeline(pipeline.build());

      results.setRequestBody(kj::mv(requestBodyCap));
      requestBody = kj::mv(pipe.in);
    } else {
      requestBody = kj::heap<kj::NullStream>();
    }

    HttpServiceResponseImpl impl(factory, metadata, params.getContext());
    co_await inner->request(impl.method, impl.url, impl.headers, *requestBody, impl);
  }

  kj::Promise<void> connect(ConnectContext context) override {
    auto params = context.getParams();
    auto host = params.getHost();
    kj::Own<kj::TlsStarterCallback> tlsStarter = kj::heap<kj::TlsStarterCallback>();
    kj::HttpConnectSettings settings = {
        .useTls = params.getSettings().getUseTls(),
        .tlsStarter = kj::none };
    settings.tlsStarter = tlsStarter;
    auto headers = factory.capnpToKj(params.getHeaders());
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

      kj::Promise<void> write(kj::ArrayPtr<const byte> buffer) override {
        return inner->write(buffer);
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

    {
      PipelineBuilder<ConnectResults> pb;
      auto eofWrapper = kj::heap<EofDetector>(kj::mv(ref2));
      auto up = factory.streamFactory.kjToCapnp(kj::mv(eofWrapper), kj::mv(tlsStarter));
      pb.setUp(kj::cp(up));

      context.setPipeline(pb.build());
      context.initResults(capnp::MessageSize { 4, 1 }).setUp(kj::mv(up));
    }

    { auto drop = kj::mv(refcounted); }

    HttpOverCapnpConnectResponseImpl response(factory, context.getParams().getContext());
    co_await inner->connect(host, headers, *pipe.ends[0], response, settings)
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

kj::HttpHeaders HttpOverCapnpFactory::capnpToKj(
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
            if (result.get(headerId) == kj::none) {
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
