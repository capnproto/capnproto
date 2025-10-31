// Copyright (c) 2017 Cloudflare, Inc. and contributors
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

#include <capnp/compat/byte-stream.h>
#include <capnp/compat/http-over-capnp.h>
#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>
#include <kj/compat/http.h>
#include <kj/debug.h>
#include <kj/main.h>

#if __linux__ || __APPLE__
#include <unistd.h>
#endif

namespace kj {
class OkService final : public HttpService {
public:
  OkService(HttpHeaderTable &table) : responseHeaders(table) {}

  kj::Promise<void> request(HttpMethod method, kj::StringPtr url,
                            const HttpHeaders &headers,
                            kj::AsyncInputStream &requestBody,
                            Response &response) override {
    responseHeaders.clear();
    responseHeaders.setPtr(HttpHeaderId::CONTENT_TYPE, "text/plain");
    auto stream = response.send(200, "OK", responseHeaders);
    co_await stream->write("OK"_kjb);
  }

private:
  HttpHeaders responseHeaders;
};

class HttpBenchMain {
public:
  HttpBenchMain(kj::ProcessContext &context) : context(context) {}

  kj::MainBuilder::Validity setServer(kj::StringPtr serverStr) {
    server = serverStr;
#if __linux__ || __APPLE__
    if (server.startsWith("unix:")) {
      kj::StringPtr path = server.slice("unix:"_kjc.size());
      ::unlink(path.cStr());
    }
#endif
    return true;
  }

  kj::MainBuilder::Validity setClient(kj::StringPtr clientStr) {
    client = clientStr;
    return true;
  }

  kj::MainFunc getMain() {
    return kj::MainBuilder(context, "http-bench", "KJ HTTP stack benchmark")
        .addSubCommand("http-server", KJ_BIND_METHOD(*this, getHttpServer),
                       "Run an HTTP server.")
        .addSubCommand("capnp-server", KJ_BIND_METHOD(*this, getCapnpServer),
                       "Run an HTTP-over-Cap'n-Proto server.")
        .addSubCommand("http-to-http", KJ_BIND_METHOD(*this, getHttpToHttp),
                       "Proxy HTTP requests to another HTTP server.")
        .addSubCommand(
            "http-to-capnp", KJ_BIND_METHOD(*this, getHttpToCapnp),
            "Proxy HTTP requests to an HTTP-over-Cap'n-Proto server.")
        .build();
  }

  kj::MainFunc getHttpServer() {
    return kj::MainBuilder(context, "http-server", "Run an HTTP server.")
        .addOptionWithArg({'s', "server"}, KJ_BIND_METHOD(*this, setServer),
                          "<address>", "Server address to listen on.")
        .callAfterParsing(KJ_BIND_METHOD(*this, runHttpServer))
        .build();
  }

  kj::MainBuilder::Validity runHttpServer() {
    KJ_REQUIRE(server != nullptr, "Must specify --server");

    auto io = kj::setupAsyncIo();

    HttpHeaderTable::Builder tableBuilder;
    auto headerTable = tableBuilder.build();
    OkService service(*headerTable);

    kj::TimerImpl timer(kj::origin<kj::TimePoint>());
    HttpServer server(timer, *headerTable, service);

    auto addr =
        io.provider->getNetwork().parseAddress(this->server).wait(io.waitScope);
    auto listener = addr->listen();

    KJ_LOG(WARNING, "Http server listening", this->server);

    auto listenPromise = server.listenHttp(*listener).eagerlyEvaluate(nullptr);
    listenPromise.wait(io.waitScope);

    return true;
  }

  kj::MainFunc getCapnpServer() {
    return kj::MainBuilder(context, "capnp-server",
                           "Run an HTTP-over-Cap'n-Proto server.")
        .addOptionWithArg({'s', "server"}, KJ_BIND_METHOD(*this, setServer),
                          "<address>", "Server address to listen on.")
        .callAfterParsing(KJ_BIND_METHOD(*this, runCapnpServer))
        .build();
  }

  kj::MainBuilder::Validity runCapnpServer() {
    KJ_REQUIRE(server != nullptr, "Must specify --server");

    auto io = kj::setupAsyncIo();

    HttpHeaderTable::Builder tableBuilder;
    capnp::HttpOverCapnpFactory::HeaderIdBundle headerIds(tableBuilder);
    auto headerTable = tableBuilder.build();

    auto capnpAddr =
        io.provider->getNetwork().parseAddress(this->server).wait(io.waitScope);
    auto capnpListener = capnpAddr->listen();

    KJ_LOG(WARNING, "Cap'n Proto server listening", this->server);

    capnp::ByteStreamFactory streamFactory;
    capnp::HttpOverCapnpFactory hocFactory(
        streamFactory, kj::mv(headerIds), capnp::HttpOverCapnpFactory::LEVEL_2);

    OkService okService(*headerTable);
    auto capnpService = hocFactory.kjToCapnp(kj::heap<OkService>(*headerTable));

    capnp::TwoPartyServer capnpServer(kj::mv(capnpService));
    capnpServer.listen(*capnpListener).wait(io.waitScope);

    return true;
  }

  kj::MainFunc getHttpToHttp() {
    return kj::MainBuilder(context, "http-to-http",
                           "Proxy HTTP requests to another HTTP server.")
        .addOptionWithArg({'s', "server"}, KJ_BIND_METHOD(*this, setServer),
                          "<address>", "Proxy listen address.")
        .addOptionWithArg({'c', "client"}, KJ_BIND_METHOD(*this, setClient),
                          "<address>", "Target HTTP server address.")
        .callAfterParsing(KJ_BIND_METHOD(*this, runHttpToHttp))
        .build();
  }

  kj::MainBuilder::Validity runHttpToHttp() {
    KJ_REQUIRE(client != nullptr, "Must specify --client");
    KJ_REQUIRE(server != nullptr, "Must specify --server");

    auto io = kj::setupAsyncIo();

    HttpHeaderTable::Builder tableBuilder;
    auto headerTable = tableBuilder.build();

    kj::TimerImpl timer(kj::origin<kj::TimePoint>());

    auto targetAddr =
        io.provider->getNetwork().parseAddress(this->client).wait(io.waitScope);
    auto httpClient = kj::newHttpClient(timer, *headerTable, *targetAddr);

    KJ_LOG(WARNING, "Connected to target HTTP server at ", this->client);

    auto httpService = kj::newHttpService(*httpClient);
    HttpServer httpServer(timer, *headerTable, *httpService);

    auto httpAddr =
        io.provider->getNetwork().parseAddress(this->server).wait(io.waitScope);
    auto httpListener = httpAddr->listen();

    KJ_LOG(WARNING, "HTTP proxy listening", this->server);

    httpServer.listenHttp(*httpListener).wait(io.waitScope);

    return true;
  }

  kj::MainFunc getHttpToCapnp() {
    return kj::MainBuilder(context, "http-to-capnp",
                           "Bridge HTTP to HTTP-over-Cap'n-Proto.")
        .addOptionWithArg({'s', "server"}, KJ_BIND_METHOD(*this, setServer),
                          "<address>", "Proxy listen address.")
        .addOptionWithArg({'c', "client"}, KJ_BIND_METHOD(*this, setClient),
                          "<address>", "Target Cap'n Proto server address.")
        .callAfterParsing(KJ_BIND_METHOD(*this, runHttpToCapnp))
        .build();
  }

  kj::MainBuilder::Validity runHttpToCapnp() {
    KJ_REQUIRE(client != nullptr, "Must specify --client");
    KJ_REQUIRE(server != nullptr, "Must specify --server");

    auto io = kj::setupAsyncIo();

    HttpHeaderTable::Builder tableBuilder;
    capnp::HttpOverCapnpFactory::HeaderIdBundle headerIds(tableBuilder);
    auto headerTable = tableBuilder.build();

    auto capnpAddr =
        io.provider->getNetwork().parseAddress(this->client).wait(io.waitScope);
    auto capnpConnection = capnpAddr->connect().wait(io.waitScope);

    KJ_LOG(WARNING, "Connected to Cap'n Proto server at ", this->client);

    capnp::ByteStreamFactory streamFactory;
    capnp::HttpOverCapnpFactory hocFactory(
        streamFactory, kj::mv(headerIds), capnp::HttpOverCapnpFactory::LEVEL_2);

    capnp::TwoPartyClient capnpClient(*capnpConnection);
    auto capnpHttpService =
        capnpClient.bootstrap().castAs<capnp::HttpService>();

    auto kjHttpService = hocFactory.capnpToKj(kj::mv(capnpHttpService));

    kj::TimerImpl timer(kj::origin<kj::TimePoint>());
    HttpServer httpServer(timer, *headerTable, *kjHttpService);

    auto httpAddr =
        io.provider->getNetwork().parseAddress(this->server).wait(io.waitScope);
    auto httpListener = httpAddr->listen();

    KJ_LOG(WARNING, "HTTP proxy listening", this->server);

    httpServer.listenHttp(*httpListener).wait(io.waitScope);

    return true;
  }

private:
  kj::ProcessContext &context;
  kj::StringPtr server;
  kj::StringPtr client;
};

} // namespace kj

KJ_MAIN(kj::HttpBenchMain);
