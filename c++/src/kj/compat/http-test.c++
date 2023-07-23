// Copyright (c) 2017 Sandstorm Development Group, Inc. and contributors
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

#define KJ_TESTING_KJ 1

#include "http.h"
#include <kj/debug.h>
#include <kj/test.h>
#include <kj/encoding.h>
#include <kj/vector.h>
#include <map>

#if KJ_HTTP_TEST_USE_OS_PIPE
// Run the test using OS-level socketpairs. (See http-socketpair-test.c++.)
#define KJ_HTTP_TEST_SETUP_IO \
  auto io = kj::setupAsyncIo(); \
  auto& waitScope KJ_UNUSED = io.waitScope
#define KJ_HTTP_TEST_SETUP_LOOPBACK_LISTENER_AND_ADDR \
  auto listener = io.provider->getNetwork().parseAddress("localhost", 0) \
      .wait(waitScope)->listen(); \
  auto addr = io.provider->getNetwork().parseAddress("localhost", listener->getPort()) \
      .wait(waitScope)
#define KJ_HTTP_TEST_CREATE_2PIPE \
  io.provider->newTwoWayPipe()
#else
// Run the test using in-process two-way pipes.
#define KJ_HTTP_TEST_SETUP_IO \
  kj::EventLoop eventLoop; \
  kj::WaitScope waitScope(eventLoop)
#define KJ_HTTP_TEST_SETUP_LOOPBACK_LISTENER_AND_ADDR \
  auto capPipe = newCapabilityPipe(); \
  auto listener = kj::heap<kj::CapabilityStreamConnectionReceiver>(*capPipe.ends[0]); \
  auto addr = kj::heap<kj::CapabilityStreamNetworkAddress>(nullptr, *capPipe.ends[1])
#define KJ_HTTP_TEST_CREATE_2PIPE \
  kj::newTwoWayPipe()
#endif

namespace kj {
namespace {

KJ_TEST("HttpMethod parse / stringify") {
#define TRY(name) \
  KJ_EXPECT(kj::str(HttpMethod::name) == #name); \
  KJ_IF_MAYBE(parsed, tryParseHttpMethodAllowingConnect(#name)) { \
    KJ_SWITCH_ONEOF(*parsed) { \
      KJ_CASE_ONEOF(method, HttpMethod) { \
        KJ_EXPECT(method == HttpMethod::name); \
      } \
      KJ_CASE_ONEOF(method, HttpConnectMethod) { \
        KJ_FAIL_EXPECT("http method parsed as CONNECT", #name); \
      } \
    } \
  } else { \
    KJ_FAIL_EXPECT("couldn't parse \"" #name "\" as HttpMethod"); \
  }

  KJ_HTTP_FOR_EACH_METHOD(TRY)
#undef TRY

  KJ_EXPECT(tryParseHttpMethod("FOO") == nullptr);
  KJ_EXPECT(tryParseHttpMethod("") == nullptr);
  KJ_EXPECT(tryParseHttpMethod("G") == nullptr);
  KJ_EXPECT(tryParseHttpMethod("GE") == nullptr);
  KJ_EXPECT(tryParseHttpMethod("GET ") == nullptr);
  KJ_EXPECT(tryParseHttpMethod("get") == nullptr);

  KJ_EXPECT(KJ_ASSERT_NONNULL(tryParseHttpMethodAllowingConnect("CONNECT"))
      .is<HttpConnectMethod>());
  KJ_EXPECT(tryParseHttpMethod("connect") == nullptr);
}

KJ_TEST("HttpHeaderTable") {
  HttpHeaderTable::Builder builder;

  auto host = builder.add("Host");
  auto host2 = builder.add("hOsT");
  auto fooBar = builder.add("Foo-Bar");
  auto bazQux = builder.add("baz-qux");
  auto bazQux2 = builder.add("Baz-Qux");

  auto table = builder.build();

  uint builtinHeaderCount = 0;
#define INCREMENT(id, name) ++builtinHeaderCount;
  KJ_HTTP_FOR_EACH_BUILTIN_HEADER(INCREMENT)
#undef INCREMENT

  KJ_EXPECT(table->idCount() == builtinHeaderCount + 2);

  KJ_EXPECT(host == HttpHeaderId::HOST);
  KJ_EXPECT(host != HttpHeaderId::DATE);
  KJ_EXPECT(host2 == host);

  KJ_EXPECT(host != fooBar);
  KJ_EXPECT(host != bazQux);
  KJ_EXPECT(fooBar != bazQux);
  KJ_EXPECT(bazQux == bazQux2);

  KJ_EXPECT(kj::str(host) == "Host");
  KJ_EXPECT(kj::str(host2) == "Host");
  KJ_EXPECT(kj::str(fooBar) == "Foo-Bar");
  KJ_EXPECT(kj::str(bazQux) == "baz-qux");
  KJ_EXPECT(kj::str(HttpHeaderId::HOST) == "Host");

  KJ_EXPECT(table->idToString(HttpHeaderId::DATE) == "Date");
  KJ_EXPECT(table->idToString(fooBar) == "Foo-Bar");

  KJ_EXPECT(KJ_ASSERT_NONNULL(table->stringToId("Date")) == HttpHeaderId::DATE);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table->stringToId("dATE")) == HttpHeaderId::DATE);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table->stringToId("Foo-Bar")) == fooBar);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table->stringToId("foo-BAR")) == fooBar);
  KJ_EXPECT(table->stringToId("foobar") == nullptr);
  KJ_EXPECT(table->stringToId("barfoo") == nullptr);
}

KJ_TEST("HttpHeaders::parseRequest") {
  HttpHeaderTable::Builder builder;

  auto fooBar = builder.add("Foo-Bar");
  auto bazQux = builder.add("baz-qux");

  auto table = builder.build();

  HttpHeaders headers(*table);
  auto text = kj::heapString(
      "POST   /some/path \t   HTTP/1.1\r\n"
      "Foo-BaR: Baz\r\n"
      "Host: example.com\r\n"
      "Content-Length: 123\r\n"
      "DATE:     early\r\n"
      "other-Header: yep\r\n"
      "with.dots: sure\r\n"
      "\r\n");
  auto result = headers.tryParseRequest(text.asArray()).get<HttpHeaders::Request>();

  KJ_EXPECT(result.method == HttpMethod::POST);
  KJ_EXPECT(result.url == "/some/path");
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(HttpHeaderId::HOST)) == "example.com");
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(HttpHeaderId::DATE)) == "early");
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(fooBar)) == "Baz");
  KJ_EXPECT(headers.get(bazQux) == nullptr);
  KJ_EXPECT(headers.get(HttpHeaderId::CONTENT_TYPE) == nullptr);
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(HttpHeaderId::CONTENT_LENGTH)) == "123");
  KJ_EXPECT(headers.get(HttpHeaderId::TRANSFER_ENCODING) == nullptr);

  std::map<kj::StringPtr, kj::StringPtr> unpackedHeaders;
  headers.forEach([&](kj::StringPtr name, kj::StringPtr value) {
    KJ_EXPECT(unpackedHeaders.insert(std::make_pair(name, value)).second);
  });
  KJ_EXPECT(unpackedHeaders.size() == 6);
  KJ_EXPECT(unpackedHeaders["Content-Length"] == "123");
  KJ_EXPECT(unpackedHeaders["Host"] == "example.com");
  KJ_EXPECT(unpackedHeaders["Date"] == "early");
  KJ_EXPECT(unpackedHeaders["Foo-Bar"] == "Baz");
  KJ_EXPECT(unpackedHeaders["other-Header"] == "yep");
  KJ_EXPECT(unpackedHeaders["with.dots"] == "sure");

  KJ_EXPECT(headers.serializeRequest(result.method, result.url) ==
      "POST /some/path HTTP/1.1\r\n"
      "Content-Length: 123\r\n"
      "Host: example.com\r\n"
      "Date: early\r\n"
      "Foo-Bar: Baz\r\n"
      "other-Header: yep\r\n"
      "with.dots: sure\r\n"
      "\r\n");
}

KJ_TEST("HttpHeaders::parseResponse") {
  HttpHeaderTable::Builder builder;

  auto fooBar = builder.add("Foo-Bar");
  auto bazQux = builder.add("baz-qux");

  auto table = builder.build();

  HttpHeaders headers(*table);
  auto text = kj::heapString(
      "HTTP/1.1\t\t  418\t    I'm a teapot\r\n"
      "Foo-BaR: Baz\r\n"
      "Host: example.com\r\n"
      "Content-Length: 123\r\n"
      "DATE:     early\r\n"
      "other-Header: yep\r\n"
      "\r\n");
  auto result = headers.tryParseResponse(text.asArray()).get<HttpHeaders::Response>();

  KJ_EXPECT(result.statusCode == 418);
  KJ_EXPECT(result.statusText == "I'm a teapot");
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(HttpHeaderId::HOST)) == "example.com");
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(HttpHeaderId::DATE)) == "early");
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(fooBar)) == "Baz");
  KJ_EXPECT(headers.get(bazQux) == nullptr);
  KJ_EXPECT(headers.get(HttpHeaderId::CONTENT_TYPE) == nullptr);
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(HttpHeaderId::CONTENT_LENGTH)) == "123");
  KJ_EXPECT(headers.get(HttpHeaderId::TRANSFER_ENCODING) == nullptr);

  std::map<kj::StringPtr, kj::StringPtr> unpackedHeaders;
  headers.forEach([&](kj::StringPtr name, kj::StringPtr value) {
    KJ_EXPECT(unpackedHeaders.insert(std::make_pair(name, value)).second);
  });
  KJ_EXPECT(unpackedHeaders.size() == 5);
  KJ_EXPECT(unpackedHeaders["Content-Length"] == "123");
  KJ_EXPECT(unpackedHeaders["Host"] == "example.com");
  KJ_EXPECT(unpackedHeaders["Date"] == "early");
  KJ_EXPECT(unpackedHeaders["Foo-Bar"] == "Baz");
  KJ_EXPECT(unpackedHeaders["other-Header"] == "yep");

  KJ_EXPECT(headers.serializeResponse(
        result.statusCode, result.statusText) ==
      "HTTP/1.1 418 I'm a teapot\r\n"
      "Content-Length: 123\r\n"
      "Host: example.com\r\n"
      "Date: early\r\n"
      "Foo-Bar: Baz\r\n"
      "other-Header: yep\r\n"
      "\r\n");
}

KJ_TEST("HttpHeaders parse invalid") {
  auto table = HttpHeaderTable::Builder().build();
  HttpHeaders headers(*table);

  // NUL byte in request.
  {
    auto input = kj::heapString(
        "POST  \0 /some/path \t   HTTP/1.1\r\n"
        "Foo-BaR: Baz\r\n"
        "Host: example.com\r\n"
        "DATE:     early\r\n"
        "other-Header: yep\r\n"
        "\r\n");

    auto protocolError = headers.tryParseRequest(input).get<HttpHeaders::ProtocolError>();

    KJ_EXPECT(protocolError.description == "Request headers have no terminal newline.",
        protocolError.description);
    KJ_EXPECT(protocolError.rawContent.asChars() == input);
  }

  // Control character in header name.
  {
    auto input = kj::heapString(
        "POST   /some/path \t   HTTP/1.1\r\n"
        "Foo-BaR: Baz\r\n"
        "Cont\001ent-Length: 123\r\n"
        "DATE:     early\r\n"
        "other-Header: yep\r\n"
        "\r\n");

    auto protocolError = headers.tryParseRequest(input).get<HttpHeaders::ProtocolError>();

    KJ_EXPECT(protocolError.description == "The headers sent by your client are not valid.",
        protocolError.description);
    KJ_EXPECT(protocolError.rawContent.asChars() == input);
  }

  // Separator character in header name.
  {
     auto input = kj::heapString(
        "POST   /some/path \t   HTTP/1.1\r\n"
        "Foo-BaR: Baz\r\n"
        "Host: example.com\r\n"
        "DATE/:     early\r\n"
        "other-Header: yep\r\n"
        "\r\n");

    auto protocolError = headers.tryParseRequest(input).get<HttpHeaders::ProtocolError>();

    KJ_EXPECT(protocolError.description == "The headers sent by your client are not valid.",
        protocolError.description);
    KJ_EXPECT(protocolError.rawContent.asChars() == input);
  }

  // Response status code not numeric.
  {
     auto input = kj::heapString(
      "HTTP/1.1\t\t  abc\t    I'm a teapot\r\n"
      "Foo-BaR: Baz\r\n"
      "Host: example.com\r\n"
      "DATE:     early\r\n"
      "other-Header: yep\r\n"
      "\r\n");

    auto protocolError = headers.tryParseRequest(input).get<HttpHeaders::ProtocolError>();

    KJ_EXPECT(protocolError.description == "Unrecognized request method.",
        protocolError.description);
    KJ_EXPECT(protocolError.rawContent.asChars() == input);
  }
}

KJ_TEST("HttpHeaders require valid HttpHeaderTable") {
  const auto ERROR_MESSAGE =
      "HttpHeaders object was constructed from HttpHeaderTable "
      "that wasn't fully built yet at the time of construction"_kj;

  {
    // A tabula rasa is valid.
    HttpHeaderTable table;
    KJ_REQUIRE(table.isReady());

    HttpHeaders headers(table);
  }

  {
    // A future table is not valid.
    HttpHeaderTable::Builder builder;

    auto& futureTable = builder.getFutureTable();
    KJ_REQUIRE(!futureTable.isReady());

    auto makeHeadersThenBuild = [&]() {
      HttpHeaders headers(futureTable);
      auto table = builder.build();
    };
    KJ_EXPECT_THROW_MESSAGE(ERROR_MESSAGE, makeHeadersThenBuild());
  }

  {
    // A well built table is valid.
    HttpHeaderTable::Builder builder;

    auto& futureTable = builder.getFutureTable();
    KJ_REQUIRE(!futureTable.isReady());

    auto ownedTable = builder.build();
    KJ_REQUIRE(futureTable.isReady());
    KJ_REQUIRE(ownedTable->isReady());

    HttpHeaders headers(futureTable);
  }
}

KJ_TEST("HttpHeaders validation") {
  auto table = HttpHeaderTable::Builder().build();
  HttpHeaders headers(*table);

  headers.add("Valid-Name", "valid value");

  // The HTTP RFC prohibits control characters, but browsers only prohibit \0, \r, and \n. KJ goes
  // with the browsers for compatibility.
  headers.add("Valid-Name", "valid\x01value");

  // The HTTP RFC does not permit non-ASCII values.
  // KJ chooses to interpret them as UTF-8, to avoid the need for any expensive conversion.
  // Browsers apparently interpret them as LATIN-1. Applications can reinterpet these strings as
  // LATIN-1 easily enough if they really need to.
  headers.add("Valid-Name", u8"valid€value");

  KJ_EXPECT_THROW_MESSAGE("invalid header name", headers.add("Invalid Name", "value"));
  KJ_EXPECT_THROW_MESSAGE("invalid header name", headers.add("Invalid@Name", "value"));

  KJ_EXPECT_THROW_MESSAGE("invalid header value", headers.set(HttpHeaderId::HOST, "in\nvalid"));
  KJ_EXPECT_THROW_MESSAGE("invalid header value", headers.add("Valid-Name", "in\nvalid"));
}

KJ_TEST("HttpHeaders Set-Cookie handling") {
  HttpHeaderTable::Builder builder;
  auto hCookie = builder.add("Cookie");
  auto hSetCookie = builder.add("Set-Cookie");
  auto table = builder.build();

  HttpHeaders headers(*table);
  headers.set(hCookie, "Foo");
  headers.add("Cookie", "Bar");
  headers.add("Cookie", "Baz");
  headers.set(hSetCookie, "Foo");
  headers.add("Set-Cookie", "Bar");
  headers.add("Set-Cookie", "Baz");

  auto text = headers.toString();
  KJ_EXPECT(text ==
      "Cookie: Foo, Bar, Baz\r\n"
      "Set-Cookie: Foo\r\n"
      "Set-Cookie: Bar\r\n"
      "Set-Cookie: Baz\r\n"
      "\r\n", text);
}

// =======================================================================================

class ReadFragmenter final: public kj::AsyncIoStream {
public:
  ReadFragmenter(AsyncIoStream& inner, size_t limit): inner(inner), limit(limit) {}

  Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return inner.read(buffer, minBytes, kj::max(minBytes, kj::min(limit, maxBytes)));
  }
  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return inner.tryRead(buffer, minBytes, kj::max(minBytes, kj::min(limit, maxBytes)));
  }

  Maybe<uint64_t> tryGetLength() override { return inner.tryGetLength(); }

  Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
    return inner.pumpTo(output, amount);
  }

  Promise<void> write(const void* buffer, size_t size) override {
    return inner.write(buffer, size);
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return inner.write(pieces);
  }

  Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
    return inner.tryPumpFrom(input, amount);
  }

  Promise<void> whenWriteDisconnected() override {
    return inner.whenWriteDisconnected();
  }

  void shutdownWrite() override {
    return inner.shutdownWrite();
  }

  void abortRead() override { return inner.abortRead(); }

  void getsockopt(int level, int option, void* value, uint* length) override {
    return inner.getsockopt(level, option, value, length);
  }
  void setsockopt(int level, int option, const void* value, uint length) override {
    return inner.setsockopt(level, option, value, length);
  }

  void getsockname(struct sockaddr* addr, uint* length) override {
    return inner.getsockname(addr, length);
  }
  void getpeername(struct sockaddr* addr, uint* length) override {
    return inner.getsockname(addr, length);
  }

private:
  kj::AsyncIoStream& inner;
  size_t limit;
};

template <typename T>
class InitializeableArray: public Array<T> {
public:
  InitializeableArray(std::initializer_list<T> init)
      : Array<T>(kj::heapArray(init)) {}
};

enum Side { BOTH, CLIENT_ONLY, SERVER_ONLY };

struct HeaderTestCase {
  HttpHeaderId id;
  kj::StringPtr value;
};

struct HttpRequestTestCase {
  kj::StringPtr raw;

  HttpMethod method;
  kj::StringPtr path;
  InitializeableArray<HeaderTestCase> requestHeaders;
  kj::Maybe<uint64_t> requestBodySize;
  InitializeableArray<kj::StringPtr> requestBodyParts;

  Side side = BOTH;

  // TODO(cleanup): Delete this constructor if/when we move to C++14.
  HttpRequestTestCase(kj::StringPtr raw, HttpMethod method, kj::StringPtr path,
                      InitializeableArray<HeaderTestCase> requestHeaders,
                      kj::Maybe<uint64_t> requestBodySize,
                      InitializeableArray<kj::StringPtr> requestBodyParts,
                      Side side = BOTH)
      : raw(raw), method(method), path(path), requestHeaders(kj::mv(requestHeaders)),
        requestBodySize(requestBodySize), requestBodyParts(kj::mv(requestBodyParts)),
        side(side) {}
};

struct HttpResponseTestCase {
  kj::StringPtr raw;

  uint64_t statusCode;
  kj::StringPtr statusText;
  InitializeableArray<HeaderTestCase> responseHeaders;
  kj::Maybe<uint64_t> responseBodySize;
  InitializeableArray<kj::StringPtr> responseBodyParts;

  HttpMethod method = HttpMethod::GET;

  Side side = BOTH;

  // TODO(cleanup): Delete this constructor if/when we move to C++14.
  HttpResponseTestCase(kj::StringPtr raw, uint64_t statusCode, kj::StringPtr statusText,
                       InitializeableArray<HeaderTestCase> responseHeaders,
                       kj::Maybe<uint64_t> responseBodySize,
                       InitializeableArray<kj::StringPtr> responseBodyParts,
                       HttpMethod method = HttpMethod::GET,
                       Side side = BOTH)
      : raw(raw), statusCode(statusCode), statusText(statusText),
        responseHeaders(kj::mv(responseHeaders)), responseBodySize(responseBodySize),
        responseBodyParts(kj::mv(responseBodyParts)), method(method), side(side) {}
};

struct HttpTestCase {
  HttpRequestTestCase request;
  HttpResponseTestCase response;
};

kj::Promise<void> writeEach(kj::AsyncOutputStream& out, kj::ArrayPtr<const kj::StringPtr> parts) {
  if (parts.size() == 0) return kj::READY_NOW;

  return out.write(parts[0].begin(), parts[0].size())
      .then([&out,parts]() {
    return writeEach(out, parts.slice(1, parts.size()));
  });
}

kj::Promise<void> expectRead(kj::AsyncInputStream& in, kj::StringPtr expected) {
  if (expected.size() == 0) return kj::READY_NOW;

  auto buffer = kj::heapArray<char>(expected.size());

  auto promise = in.tryRead(buffer.begin(), 1, buffer.size());
  return promise.then([&in,expected,buffer=kj::mv(buffer)](size_t amount) {
    if (amount == 0) {
      KJ_FAIL_ASSERT("expected data never sent", expected);
    }

    auto actual = buffer.slice(0, amount);
    if (memcmp(actual.begin(), expected.begin(), actual.size()) != 0) {
      KJ_FAIL_ASSERT("data from stream doesn't match expected", expected, actual);
    }

    return expectRead(in, expected.slice(amount));
  });
}

kj::Promise<void> expectRead(kj::AsyncInputStream& in, kj::ArrayPtr<const byte> expected) {
  if (expected.size() == 0) return kj::READY_NOW;

  auto buffer = kj::heapArray<byte>(expected.size());

  auto promise = in.tryRead(buffer.begin(), 1, buffer.size());
  return promise.then([&in,expected,buffer=kj::mv(buffer)](size_t amount) {
    if (amount == 0) {
      KJ_FAIL_ASSERT("expected data never sent", expected);
    }

    auto actual = buffer.slice(0, amount);
    if (memcmp(actual.begin(), expected.begin(), actual.size()) != 0) {
      KJ_FAIL_ASSERT("data from stream doesn't match expected", expected, actual);
    }

    return expectRead(in, expected.slice(amount, expected.size()));
  });
}

kj::Promise<void> expectEnd(kj::AsyncInputStream& in) {
  static char buffer;

  auto promise = in.tryRead(&buffer, 1, 1);
  return promise.then([](size_t amount) {
    KJ_ASSERT(amount == 0, "expected EOF");
  });
}

void testHttpClientRequest(kj::WaitScope& waitScope, const HttpRequestTestCase& testCase,
                           kj::TwoWayPipe pipe) {

  auto serverTask = expectRead(*pipe.ends[1], testCase.raw).then([&]() {
    static const char SIMPLE_RESPONSE[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    return pipe.ends[1]->write(SIMPLE_RESPONSE, strlen(SIMPLE_RESPONSE));
  }).then([&]() -> kj::Promise<void> {
    return kj::NEVER_DONE;
  });

  HttpHeaderTable table;
  auto client = newHttpClient(table, *pipe.ends[0]);

  HttpHeaders headers(table);
  for (auto& header: testCase.requestHeaders) {
    headers.set(header.id, header.value);
  }

  auto request = client->request(testCase.method, testCase.path, headers, testCase.requestBodySize);
  if (testCase.requestBodyParts.size() > 0) {
    writeEach(*request.body, testCase.requestBodyParts).wait(waitScope);
  }
  request.body = nullptr;
  auto clientTask = request.response
      .then([&](HttpClient::Response&& response) {
    auto promise = response.body->readAllText();
    return promise.attach(kj::mv(response.body));
  }).ignoreResult();

  serverTask.exclusiveJoin(kj::mv(clientTask)).wait(waitScope);

  // Verify no more data written by client.
  client = nullptr;
  pipe.ends[0]->shutdownWrite();
  KJ_EXPECT(pipe.ends[1]->readAllText().wait(waitScope) == "");
}

void testHttpClientResponse(kj::WaitScope& waitScope, const HttpResponseTestCase& testCase,
                            size_t readFragmentSize, kj::TwoWayPipe pipe) {
  ReadFragmenter fragmenter(*pipe.ends[0], readFragmentSize);

  auto expectedReqText = testCase.method == HttpMethod::GET || testCase.method == HttpMethod::HEAD
      ? kj::str(testCase.method, " / HTTP/1.1\r\n\r\n")
      : kj::str(testCase.method, " / HTTP/1.1\r\nContent-Length: 0\r\n");

  auto serverTask = expectRead(*pipe.ends[1], expectedReqText).then([&]() {
    return pipe.ends[1]->write(testCase.raw.begin(), testCase.raw.size());
  }).then([&]() -> kj::Promise<void> {
    pipe.ends[1]->shutdownWrite();
    return kj::NEVER_DONE;
  });

  HttpHeaderTable table;
  auto client = newHttpClient(table, fragmenter);

  HttpHeaders headers(table);
  auto request = client->request(testCase.method, "/", headers, uint64_t(0));
  request.body = nullptr;
  auto clientTask = request.response
      .then([&](HttpClient::Response&& response) {
    KJ_EXPECT(response.statusCode == testCase.statusCode);
    KJ_EXPECT(response.statusText == testCase.statusText);

    for (auto& header: testCase.responseHeaders) {
      KJ_EXPECT(KJ_ASSERT_NONNULL(response.headers->get(header.id)) == header.value);
    }
    auto promise = response.body->readAllText();
    return promise.attach(kj::mv(response.body));
  }).then([&](kj::String body) {
    KJ_EXPECT(body == kj::strArray(testCase.responseBodyParts, ""), body);
  });

  serverTask.exclusiveJoin(kj::mv(clientTask)).wait(waitScope);

  // Verify no more data written by client.
  client = nullptr;
  pipe.ends[0]->shutdownWrite();
  KJ_EXPECT(pipe.ends[1]->readAllText().wait(waitScope) == "");
}

void testHttpClient(kj::WaitScope& waitScope, HttpHeaderTable& table,
                    HttpClient& client, const HttpTestCase& testCase) {
  KJ_CONTEXT(testCase.request.raw, testCase.response.raw);

  HttpHeaders headers(table);
  for (auto& header: testCase.request.requestHeaders) {
    headers.set(header.id, header.value);
  }

  auto request = client.request(
      testCase.request.method, testCase.request.path, headers, testCase.request.requestBodySize);
  for (auto& part: testCase.request.requestBodyParts) {
    request.body->write(part.begin(), part.size()).wait(waitScope);
  }
  request.body = nullptr;

  auto response = request.response.wait(waitScope);

  KJ_EXPECT(response.statusCode == testCase.response.statusCode);
  auto body = response.body->readAllText().wait(waitScope);
  if (testCase.request.method == HttpMethod::HEAD) {
    KJ_EXPECT(body == "");
  } else {
    KJ_EXPECT(body == kj::strArray(testCase.response.responseBodyParts, ""), body);
  }
}

class TestHttpService final: public HttpService {
public:
  TestHttpService(const HttpRequestTestCase& expectedRequest,
                  const HttpResponseTestCase& response,
                  HttpHeaderTable& table)
      : singleExpectedRequest(&expectedRequest),
        singleResponse(&response),
        responseHeaders(table) {}
  TestHttpService(kj::ArrayPtr<const HttpTestCase> testCases,
                  HttpHeaderTable& table)
      : singleExpectedRequest(nullptr),
        singleResponse(nullptr),
        testCases(testCases),
        responseHeaders(table) {}

  uint getRequestCount() { return requestCount; }

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& responseSender) override {
    auto& expectedRequest = testCases == nullptr ? *singleExpectedRequest :
        testCases[requestCount % testCases.size()].request;
    auto& response = testCases == nullptr ? *singleResponse :
        testCases[requestCount % testCases.size()].response;

    ++requestCount;

    KJ_EXPECT(method == expectedRequest.method, method);
    KJ_EXPECT(url == expectedRequest.path, url);

    for (auto& header: expectedRequest.requestHeaders) {
      KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(header.id)) == header.value);
    }

    auto size = requestBody.tryGetLength();
    KJ_IF_MAYBE(expectedSize, expectedRequest.requestBodySize) {
      KJ_IF_MAYBE(s, size) {
        KJ_EXPECT(*s == *expectedSize, *s);
      } else {
        KJ_FAIL_EXPECT("tryGetLength() returned nullptr; expected known size");
      }
    } else {
      KJ_EXPECT(size == nullptr);
    }

    return requestBody.readAllText()
        .then([this,&expectedRequest,&response,&responseSender](kj::String text) {
      KJ_EXPECT(text == kj::strArray(expectedRequest.requestBodyParts, ""), text);

      responseHeaders.clear();
      for (auto& header: response.responseHeaders) {
        responseHeaders.set(header.id, header.value);
      }

      auto stream = responseSender.send(response.statusCode, response.statusText,
                                        responseHeaders, response.responseBodySize);
      auto promise = writeEach(*stream, response.responseBodyParts);
      return promise.attach(kj::mv(stream));
    });
  }

private:
  const HttpRequestTestCase* singleExpectedRequest;
  const HttpResponseTestCase* singleResponse;
  kj::ArrayPtr<const HttpTestCase> testCases;
  HttpHeaders responseHeaders;
  uint requestCount = 0;
};

void testHttpServerRequest(kj::WaitScope& waitScope, kj::Timer& timer,
                           const HttpRequestTestCase& requestCase,
                           const HttpResponseTestCase& responseCase,
                           kj::TwoWayPipe pipe) {
  HttpHeaderTable table;
  TestHttpService service(requestCase, responseCase, table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  pipe.ends[1]->write(requestCase.raw.begin(), requestCase.raw.size()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1], responseCase.raw).wait(waitScope);

  listenTask.wait(waitScope);

  KJ_EXPECT(service.getRequestCount() == 1);
}

kj::ArrayPtr<const HttpRequestTestCase> requestTestCases() {
  static const auto HUGE_STRING = kj::strArray(kj::repeat("abcdefgh", 4096), "");
  static const auto HUGE_REQUEST = kj::str(
      "GET / HTTP/1.1\r\n"
      "Host: ", HUGE_STRING, "\r\n"
      "\r\n");

  static const HttpRequestTestCase REQUEST_TEST_CASES[] {
    {
      "GET /foo/bar HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "\r\n",

      HttpMethod::GET,
      "/foo/bar",
      {{HttpHeaderId::HOST, "example.com"}},
      uint64_t(0), {},
    },

    {
      "HEAD /foo/bar HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "\r\n",

      HttpMethod::HEAD,
      "/foo/bar",
      {{HttpHeaderId::HOST, "example.com"}},
      uint64_t(0), {},
    },

    {
      "POST / HTTP/1.1\r\n"
      "Content-Length: 9\r\n"
      "Host: example.com\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "foobarbaz",

      HttpMethod::POST,
      "/",
      {
        {HttpHeaderId::HOST, "example.com"},
        {HttpHeaderId::CONTENT_TYPE, "text/plain"},
      },
      9, { "foo", "bar", "baz" },
    },

    {
      "POST / HTTP/1.1\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Host: example.com\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "3\r\n"
      "foo\r\n"
      "6\r\n"
      "barbaz\r\n"
      "0\r\n"
      "\r\n",

      HttpMethod::POST,
      "/",
      {
        {HttpHeaderId::HOST, "example.com"},
        {HttpHeaderId::CONTENT_TYPE, "text/plain"},
      },
      nullptr, { "foo", "barbaz" },
    },

    {
      "POST / HTTP/1.1\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Host: example.com\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "1d\r\n"
      "0123456789abcdef0123456789abc\r\n"
      "0\r\n"
      "\r\n",

      HttpMethod::POST,
      "/",
      {
        {HttpHeaderId::HOST, "example.com"},
        {HttpHeaderId::CONTENT_TYPE, "text/plain"},
      },
      nullptr, { "0123456789abcdef0123456789abc" },
    },

    {
      HUGE_REQUEST,

      HttpMethod::GET,
      "/",
      {{HttpHeaderId::HOST, HUGE_STRING}},
      uint64_t(0), {}
    },

    {
      "GET /foo/bar HTTP/1.1\r\n"
      "Content-Length: 6\r\n"
      "Host: example.com\r\n"
      "\r\n"
      "foobar",

      HttpMethod::GET,
      "/foo/bar",
      {{HttpHeaderId::HOST, "example.com"}},
      uint64_t(6), { "foobar" },
    },

    {
      "GET /foo/bar HTTP/1.1\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Host: example.com\r\n"
      "\r\n"
      "3\r\n"
      "foo\r\n"
      "3\r\n"
      "bar\r\n"
      "0\r\n"
      "\r\n",

      HttpMethod::GET,
      "/foo/bar",
      {{HttpHeaderId::HOST, "example.com"},
       {HttpHeaderId::TRANSFER_ENCODING, "chunked"}},
      nullptr, { "foo", "bar" },
    }
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents REQUEST_TEST_CASES from implicitly
  //   casting to our return type.
  return kj::arrayPtr(REQUEST_TEST_CASES, kj::size(REQUEST_TEST_CASES));
}

kj::ArrayPtr<const HttpResponseTestCase> responseTestCases() {
  static const HttpResponseTestCase RESPONSE_TEST_CASES[] {
    {
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n"
      "\r\n"
      "baz qux",

      200, "OK",
      {{HttpHeaderId::CONTENT_TYPE, "text/plain"}},
      nullptr, {"baz qux"},

      HttpMethod::GET,
      CLIENT_ONLY,   // Server never sends connection: close
    },

    {
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Transfer-Encoding: identity\r\n"
      "Content-Length: foobar\r\n"  // intentionally wrong
      "\r\n"
      "baz qux",

      200, "OK",
      {{HttpHeaderId::CONTENT_TYPE, "text/plain"}},
      nullptr, {"baz qux"},

      HttpMethod::GET,
      CLIENT_ONLY,   // Server never sends transfer-encoding: identity
    },

    {
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "baz qux",

      200, "OK",
      {{HttpHeaderId::CONTENT_TYPE, "text/plain"}},
      nullptr, {"baz qux"},

      HttpMethod::GET,
      CLIENT_ONLY,   // Server never sends non-delimited message
    },

    {
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 123\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n",

      200, "OK",
      {{HttpHeaderId::CONTENT_TYPE, "text/plain"}},
      123, {},

      HttpMethod::HEAD,
    },

    {
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: foobar\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n",

      200, "OK",
      {{HttpHeaderId::CONTENT_TYPE, "text/plain"},
       {HttpHeaderId::CONTENT_LENGTH, "foobar"}},
      123, {},

      HttpMethod::HEAD,
    },

    // Zero-length expected size response to HEAD request has no Content-Length header.
    {
      "HTTP/1.1 200 OK\r\n"
      "\r\n",

      200, "OK",
      {},
      uint64_t(0), {},

      HttpMethod::HEAD,
    },

    {
      "HTTP/1.1 204 No Content\r\n"
      "\r\n",

      204, "No Content",
      {},
      uint64_t(0), {},
    },

    {
      "HTTP/1.1 205 Reset Content\r\n"
      "Content-Length: 0\r\n"
      "\r\n",

      205, "Reset Content",
      {},
      uint64_t(0), {},
    },

    {
      "HTTP/1.1 304 Not Modified\r\n"
      "\r\n",

      304, "Not Modified",
      {},
      uint64_t(0), {},
    },

    {
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 8\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "quxcorge",

      200, "OK",
      {{HttpHeaderId::CONTENT_TYPE, "text/plain"}},
      8, { "qux", "corge" }
    },

    {
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "3\r\n"
      "qux\r\n"
      "5\r\n"
      "corge\r\n"
      "0\r\n"
      "\r\n",

      200, "OK",
      {{HttpHeaderId::CONTENT_TYPE, "text/plain"}},
      nullptr, { "qux", "corge" }
    },
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents RESPONSE_TEST_CASES from implicitly
  //   casting to our return type.
  return kj::arrayPtr(RESPONSE_TEST_CASES, kj::size(RESPONSE_TEST_CASES));
}

KJ_TEST("HttpClient requests") {
  KJ_HTTP_TEST_SETUP_IO;

  for (auto& testCase: requestTestCases()) {
    if (testCase.side == SERVER_ONLY) continue;
    KJ_CONTEXT(testCase.raw);
    testHttpClientRequest(waitScope, testCase, KJ_HTTP_TEST_CREATE_2PIPE);
  }
}

KJ_TEST("HttpClient responses") {
  KJ_HTTP_TEST_SETUP_IO;
  size_t FRAGMENT_SIZES[] = { 1, 2, 3, 4, 5, 6, 7, 8, 16, 31, kj::maxValue };

  for (auto& testCase: responseTestCases()) {
    if (testCase.side == SERVER_ONLY) continue;
    for (size_t fragmentSize: FRAGMENT_SIZES) {
      KJ_CONTEXT(testCase.raw, fragmentSize);
      testHttpClientResponse(waitScope, testCase, fragmentSize, KJ_HTTP_TEST_CREATE_2PIPE);
    }
  }
}

KJ_TEST("HttpClient canceled write") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto serverPromise = pipe.ends[1]->readAllText();

  {
    HttpHeaderTable table;
    auto client = newHttpClient(table, *pipe.ends[0]);

    auto body = kj::heapArray<byte>(4096);
    memset(body.begin(), 0xcf, body.size());

    auto req = client->request(HttpMethod::POST, "/", HttpHeaders(table), uint64_t(4096));

    // Note: This poll() forces the server to receive what was written so far. Otherwise,
    //   cancelling the write below may in fact cancel the previous write as well.
    KJ_EXPECT(!serverPromise.poll(waitScope));

    // Start a write and immediately cancel it.
    {
      auto ignore KJ_UNUSED = req.body->write(body.begin(), body.size());
    }

    KJ_EXPECT_THROW_MESSAGE("overwrote", req.body->write("foo", 3).wait(waitScope));
    req.body = nullptr;

    KJ_EXPECT(!serverPromise.poll(waitScope));

    KJ_EXPECT_THROW_MESSAGE("can't start new request until previous request body",
        client->request(HttpMethod::GET, "/", HttpHeaders(table)).response.wait(waitScope));
  }

  pipe.ends[0]->shutdownWrite();
  auto text = serverPromise.wait(waitScope);
  KJ_EXPECT(text == "POST / HTTP/1.1\r\nContent-Length: 4096\r\n\r\n", text);
}

KJ_TEST("HttpClient chunked body gather-write") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto serverPromise = pipe.ends[1]->readAllText();

  {
    HttpHeaderTable table;
    auto client = newHttpClient(table, *pipe.ends[0]);

    auto req = client->request(HttpMethod::POST, "/", HttpHeaders(table));

    kj::ArrayPtr<const byte> bodyParts[] = {
      "foo"_kj.asBytes(), " "_kj.asBytes(),
      "bar"_kj.asBytes(), " "_kj.asBytes(),
      "baz"_kj.asBytes()
    };

    req.body->write(kj::arrayPtr(bodyParts, kj::size(bodyParts))).wait(waitScope);
    req.body = nullptr;

    // Wait for a response so the client has a chance to end the request body with a 0-chunk.
    kj::StringPtr responseText = "HTTP/1.1 204 No Content\r\n\r\n";
    pipe.ends[1]->write(responseText.begin(), responseText.size()).wait(waitScope);
    auto response = req.response.wait(waitScope);
  }

  pipe.ends[0]->shutdownWrite();

  auto text = serverPromise.wait(waitScope);
  KJ_EXPECT(text == "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
                    "b\r\nfoo bar baz\r\n0\r\n\r\n", text);
}

KJ_TEST("HttpClient chunked body pump from fixed length stream") {
  class FixedBodyStream final: public kj::AsyncInputStream {
    Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
      auto n = kj::min(body.size(), maxBytes);
      n = kj::max(n, minBytes);
      n = kj::min(n, body.size());
      memcpy(buffer, body.begin(), n);
      body = body.slice(n);
      return n;
    }

    Maybe<uint64_t> tryGetLength() override { return body.size(); }

    kj::StringPtr body = "foo bar baz";
  };

  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto serverPromise = pipe.ends[1]->readAllText();

  {
    HttpHeaderTable table;
    auto client = newHttpClient(table, *pipe.ends[0]);

    auto req = client->request(HttpMethod::POST, "/", HttpHeaders(table));

    FixedBodyStream bodyStream;
    bodyStream.pumpTo(*req.body).wait(waitScope);
    req.body = nullptr;

    // Wait for a response so the client has a chance to end the request body with a 0-chunk.
    kj::StringPtr responseText = "HTTP/1.1 204 No Content\r\n\r\n";
    pipe.ends[1]->write(responseText.begin(), responseText.size()).wait(waitScope);
    auto response = req.response.wait(waitScope);
  }

  pipe.ends[0]->shutdownWrite();

  auto text = serverPromise.wait(waitScope);
  KJ_EXPECT(text == "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
                    "b\r\nfoo bar baz\r\n0\r\n\r\n", text);
}

KJ_TEST("HttpServer requests") {
  HttpResponseTestCase RESPONSE = {
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 3\r\n"
    "\r\n"
    "foo",

    200, "OK",
    {},
    3, {"foo"}
  };

  HttpResponseTestCase HEAD_RESPONSE = {
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 3\r\n"
    "\r\n",

    200, "OK",
    {},
    3, {"foo"}
  };

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  for (auto& testCase: requestTestCases()) {
    if (testCase.side == CLIENT_ONLY) continue;
    KJ_CONTEXT(testCase.raw);
    testHttpServerRequest(waitScope, timer, testCase,
        testCase.method == HttpMethod::HEAD ? HEAD_RESPONSE : RESPONSE,
        KJ_HTTP_TEST_CREATE_2PIPE);
  }
}

KJ_TEST("HttpServer responses") {
  HttpRequestTestCase REQUEST = {
    "GET / HTTP/1.1\r\n"
    "\r\n",

    HttpMethod::GET,
    "/",
    {},
    uint64_t(0), {},
  };

  HttpRequestTestCase HEAD_REQUEST = {
    "HEAD / HTTP/1.1\r\n"
    "\r\n",

    HttpMethod::HEAD,
    "/",
    {},
    uint64_t(0), {},
  };

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  for (auto& testCase: responseTestCases()) {
    if (testCase.side == CLIENT_ONLY) continue;
    KJ_CONTEXT(testCase.raw);
    testHttpServerRequest(waitScope, timer,
        testCase.method == HttpMethod::HEAD ? HEAD_REQUEST : REQUEST, testCase,
        KJ_HTTP_TEST_CREATE_2PIPE);
  }
}

// -----------------------------------------------------------------------------

kj::ArrayPtr<const HttpTestCase> pipelineTestCases() {
  static const HttpTestCase PIPELINE_TESTS[] = {
    {
      {
        "GET / HTTP/1.1\r\n"
        "\r\n",

        HttpMethod::GET, "/", {}, uint64_t(0), {},
      },
      {
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 7\r\n"
        "\r\n"
        "foo bar",

        200, "OK", {}, 7, { "foo bar" }
      },
    },

    {
      {
        "POST /foo HTTP/1.1\r\n"
        "Content-Length: 6\r\n"
        "\r\n"
        "grault",

        HttpMethod::POST, "/foo", {}, 6, { "grault" },
      },
      {
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "baz qux corge",

        404, "Not Found", {}, 13, { "baz qux corge" }
      },
    },

    // Throw a zero-size request/response into the pipeline to check for a bug that existed with
    // them previously.
    {
      {
        "POST /foo HTTP/1.1\r\n"
        "Content-Length: 0\r\n"
        "\r\n",

        HttpMethod::POST, "/foo", {}, uint64_t(0), {},
      },
      {
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n",

        200, "OK", {}, uint64_t(0), {}
      },
    },

    // Also a zero-size chunked request/response.
    {
      {
        "POST /foo HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "0\r\n"
        "\r\n",

        HttpMethod::POST, "/foo", {}, nullptr, {},
      },
      {
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "0\r\n"
        "\r\n",

        200, "OK", {}, nullptr, {}
      },
    },

    {
      {
        "POST /bar HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "6\r\n"
        "garply\r\n"
        "5\r\n"
        "waldo\r\n"
        "0\r\n"
        "\r\n",

        HttpMethod::POST, "/bar", {}, nullptr, { "garply", "waldo" },
      },
      {
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "4\r\n"
        "fred\r\n"
        "5\r\n"
        "plugh\r\n"
        "0\r\n"
        "\r\n",

        200, "OK", {}, nullptr, { "fred", "plugh" }
      },
    },

    {
      {
        "HEAD / HTTP/1.1\r\n"
        "\r\n",

        HttpMethod::HEAD, "/", {}, uint64_t(0), {},
      },
      {
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 7\r\n"
        "\r\n",

        200, "OK", {}, 7, { "foo bar" }
      },
    },

    // Zero-length expected size response to HEAD request has no Content-Length header.
    {
      {
        "HEAD / HTTP/1.1\r\n"
        "\r\n",

        HttpMethod::HEAD, "/", {}, uint64_t(0), {},
      },
      {
        "HTTP/1.1 200 OK\r\n"
        "\r\n",

        200, "OK", {}, uint64_t(0), {}, HttpMethod::HEAD,
      },
    },
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents RESPONSE_TEST_CASES from implicitly
  //   casting to our return type.
  return kj::arrayPtr(PIPELINE_TESTS, kj::size(PIPELINE_TESTS));
}

KJ_TEST("HttpClient pipeline") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::Promise<void> writeResponsesPromise = kj::READY_NOW;
  for (auto& testCase: PIPELINE_TESTS) {
    writeResponsesPromise = writeResponsesPromise
        .then([&]() {
      return expectRead(*pipe.ends[1], testCase.request.raw);
    }).then([&]() {
      return pipe.ends[1]->write(testCase.response.raw.begin(), testCase.response.raw.size());
    });
  }

  HttpHeaderTable table;
  auto client = newHttpClient(table, *pipe.ends[0]);

  for (auto& testCase: PIPELINE_TESTS) {
    testHttpClient(waitScope, table, *client, testCase);
  }

  client = nullptr;
  pipe.ends[0]->shutdownWrite();

  writeResponsesPromise.wait(waitScope);
}

KJ_TEST("HttpClient parallel pipeline") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::Promise<void> readRequestsPromise = kj::READY_NOW;
  kj::Promise<void> writeResponsesPromise = kj::READY_NOW;
  for (auto& testCase: PIPELINE_TESTS) {
    auto forked = readRequestsPromise
        .then([&]() {
      return expectRead(*pipe.ends[1], testCase.request.raw);
    }).fork();
    readRequestsPromise = forked.addBranch();

    // Don't write each response until the corresponding request is received.
    auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);
    promises.add(forked.addBranch());
    promises.add(kj::mv(writeResponsesPromise));
    writeResponsesPromise = kj::joinPromises(promises.finish()).then([&]() {
      return pipe.ends[1]->write(testCase.response.raw.begin(), testCase.response.raw.size());
    });
  }

  HttpHeaderTable table;
  auto client = newHttpClient(table, *pipe.ends[0]);

  auto responsePromises = KJ_MAP(testCase, PIPELINE_TESTS) {
    KJ_CONTEXT(testCase.request.raw, testCase.response.raw);

    HttpHeaders headers(table);
    for (auto& header: testCase.request.requestHeaders) {
      headers.set(header.id, header.value);
    }

    auto request = client->request(
        testCase.request.method, testCase.request.path, headers, testCase.request.requestBodySize);
    for (auto& part: testCase.request.requestBodyParts) {
      request.body->write(part.begin(), part.size()).wait(waitScope);
    }

    return kj::mv(request.response);
  };

  for (auto i: kj::indices(PIPELINE_TESTS)) {
    auto& testCase = PIPELINE_TESTS[i];
    auto response = responsePromises[i].wait(waitScope);

    KJ_EXPECT(response.statusCode == testCase.response.statusCode);
    auto body = response.body->readAllText().wait(waitScope);
    if (testCase.request.method == HttpMethod::HEAD) {
      KJ_EXPECT(body == "");
    } else {
      KJ_EXPECT(body == kj::strArray(testCase.response.responseBodyParts, ""), body);
    }
  }

  client = nullptr;
  pipe.ends[0]->shutdownWrite();

  writeResponsesPromise.wait(waitScope);
}

KJ_TEST("HttpServer pipeline") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  for (auto& testCase: PIPELINE_TESTS) {
    KJ_CONTEXT(testCase.request.raw, testCase.response.raw);

    pipe.ends[1]->write(testCase.request.raw.begin(), testCase.request.raw.size())
        .wait(waitScope);

    expectRead(*pipe.ends[1], testCase.response.raw).wait(waitScope);
  }

  pipe.ends[1]->shutdownWrite();
  listenTask.wait(waitScope);

  KJ_EXPECT(service.getRequestCount() == kj::size(PIPELINE_TESTS));
}

KJ_TEST("HttpServer parallel pipeline") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto allRequestText =
      kj::strArray(KJ_MAP(testCase, PIPELINE_TESTS) { return testCase.request.raw; }, "");
  auto allResponseText =
      kj::strArray(KJ_MAP(testCase, PIPELINE_TESTS) { return testCase.response.raw; }, "");

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  pipe.ends[1]->write(allRequestText.begin(), allRequestText.size()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  auto rawResponse = pipe.ends[1]->readAllText().wait(waitScope);
  KJ_EXPECT(rawResponse == allResponseText, rawResponse);

  listenTask.wait(waitScope);

  KJ_EXPECT(service.getRequestCount() == kj::size(PIPELINE_TESTS));
}

KJ_TEST("HttpClient <-> HttpServer") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[1]));
  auto client = newHttpClient(table, *pipe.ends[0]);

  for (auto& testCase: PIPELINE_TESTS) {
    testHttpClient(waitScope, table, *client, testCase);
  }

  client = nullptr;
  pipe.ends[0]->shutdownWrite();
  listenTask.wait(waitScope);
  KJ_EXPECT(service.getRequestCount() == kj::size(PIPELINE_TESTS));
}

// -----------------------------------------------------------------------------

KJ_TEST("HttpInputStream requests") {
  KJ_HTTP_TEST_SETUP_IO;

  kj::HttpHeaderTable table;

  auto pipe = kj::newOneWayPipe();
  auto input = newHttpInputStream(*pipe.in, table);

  kj::Promise<void> writeQueue = kj::READY_NOW;

  for (auto& testCase: requestTestCases()) {
    writeQueue = writeQueue.then([&]() {
      return pipe.out->write(testCase.raw.begin(), testCase.raw.size());
    });
  }
  writeQueue = writeQueue.then([&]() {
    pipe.out = nullptr;
  });

  for (auto& testCase: requestTestCases()) {
    KJ_CONTEXT(testCase.raw);

    KJ_ASSERT(input->awaitNextMessage().wait(waitScope));

    auto req = input->readRequest().wait(waitScope);
    KJ_EXPECT(req.method == testCase.method);
    KJ_EXPECT(req.url == testCase.path);
    for (auto& header: testCase.requestHeaders) {
      KJ_EXPECT(KJ_ASSERT_NONNULL(req.headers.get(header.id)) == header.value);
    }
    auto body = req.body->readAllText().wait(waitScope);
    KJ_EXPECT(body == kj::strArray(testCase.requestBodyParts, ""));
  }

  writeQueue.wait(waitScope);
  KJ_EXPECT(!input->awaitNextMessage().wait(waitScope));
}

KJ_TEST("HttpInputStream responses") {
  KJ_HTTP_TEST_SETUP_IO;

  kj::HttpHeaderTable table;

  auto pipe = kj::newOneWayPipe();
  auto input = newHttpInputStream(*pipe.in, table);

  kj::Promise<void> writeQueue = kj::READY_NOW;

  for (auto& testCase: responseTestCases()) {
    if (testCase.side == CLIENT_ONLY) continue;  // skip Connection: close case.
    writeQueue = writeQueue.then([&]() {
      return pipe.out->write(testCase.raw.begin(), testCase.raw.size());
    });
  }
  writeQueue = writeQueue.then([&]() {
    pipe.out = nullptr;
  });

  for (auto& testCase: responseTestCases()) {
    if (testCase.side == CLIENT_ONLY) continue;  // skip Connection: close case.
    KJ_CONTEXT(testCase.raw);

    KJ_ASSERT(input->awaitNextMessage().wait(waitScope));

    auto resp = input->readResponse(testCase.method).wait(waitScope);
    KJ_EXPECT(resp.statusCode == testCase.statusCode);
    KJ_EXPECT(resp.statusText == testCase.statusText);
    for (auto& header: testCase.responseHeaders) {
      KJ_EXPECT(KJ_ASSERT_NONNULL(resp.headers.get(header.id)) == header.value);
    }
    auto body = resp.body->readAllText().wait(waitScope);
    KJ_EXPECT(body == kj::strArray(testCase.responseBodyParts, ""));
  }

  writeQueue.wait(waitScope);
  KJ_EXPECT(!input->awaitNextMessage().wait(waitScope));
}

KJ_TEST("HttpInputStream bare messages") {
  KJ_HTTP_TEST_SETUP_IO;

  kj::HttpHeaderTable table;

  auto pipe = kj::newOneWayPipe();
  auto input = newHttpInputStream(*pipe.in, table);

  kj::StringPtr messages =
      "Content-Length: 6\r\n"
      "\r\n"
      "foobar"
      "Content-Length: 11\r\n"
      "Content-Type: some/type\r\n"
      "\r\n"
      "bazquxcorge"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "6\r\n"
      "grault\r\n"
      "b\r\n"
      "garplywaldo\r\n"
      "0\r\n"
      "\r\n"_kj;

  kj::Promise<void> writeTask = pipe.out->write(messages.begin(), messages.size())
      .then([&]() { pipe.out = nullptr; });

  {
    KJ_ASSERT(input->awaitNextMessage().wait(waitScope));
    auto message = input->readMessage().wait(waitScope);
    KJ_EXPECT(KJ_ASSERT_NONNULL(message.headers.get(HttpHeaderId::CONTENT_LENGTH)) == "6");
    KJ_EXPECT(message.body->readAllText().wait(waitScope) == "foobar");
  }
  {
    KJ_ASSERT(input->awaitNextMessage().wait(waitScope));
    auto message = input->readMessage().wait(waitScope);
    KJ_EXPECT(KJ_ASSERT_NONNULL(message.headers.get(HttpHeaderId::CONTENT_LENGTH)) == "11");
    KJ_EXPECT(KJ_ASSERT_NONNULL(message.headers.get(HttpHeaderId::CONTENT_TYPE)) == "some/type");
    KJ_EXPECT(message.body->readAllText().wait(waitScope) == "bazquxcorge");
  }
  {
    KJ_ASSERT(input->awaitNextMessage().wait(waitScope));
    auto message = input->readMessage().wait(waitScope);
    KJ_EXPECT(KJ_ASSERT_NONNULL(message.headers.get(HttpHeaderId::TRANSFER_ENCODING)) == "chunked");
    KJ_EXPECT(message.body->readAllText().wait(waitScope) == "graultgarplywaldo");
  }

  writeTask.wait(waitScope);
  KJ_EXPECT(!input->awaitNextMessage().wait(waitScope));
}

// -----------------------------------------------------------------------------

KJ_TEST("WebSocket core protocol") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto client = newWebSocket(kj::mv(pipe.ends[0]), nullptr);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr);

  auto mediumString = kj::strArray(kj::repeat(kj::StringPtr("123456789"), 30), "");
  auto bigString = kj::strArray(kj::repeat(kj::StringPtr("123456789"), 10000), "");

  auto clientTask = client->send(kj::StringPtr("hello"))
      .then([&]() { return client->send(mediumString); })
      .then([&]() { return client->send(bigString); })
      .then([&]() { return client->send(kj::StringPtr("world").asBytes()); })
      .then([&]() { return client->close(1234, "bored"); })
      .then([&]() { KJ_EXPECT(client->sentByteCount() == 90307)});

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "hello");
  }

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == mediumString);
  }

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == bigString);
  }

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::Array<byte>>());
    KJ_EXPECT(kj::str(message.get<kj::Array<byte>>().asChars()) == "world");
  }

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<WebSocket::Close>());
    KJ_EXPECT(message.get<WebSocket::Close>().code == 1234);
    KJ_EXPECT(message.get<WebSocket::Close>().reason == "bored");
    KJ_EXPECT(server->receivedByteCount() == 90307);
  }

  auto serverTask = server->close(4321, "whatever");

  {
    auto message = client->receive().wait(waitScope);
    KJ_ASSERT(message.is<WebSocket::Close>());
    KJ_EXPECT(message.get<WebSocket::Close>().code == 4321);
    KJ_EXPECT(message.get<WebSocket::Close>().reason == "whatever");
    KJ_EXPECT(client->receivedByteCount() == 12);
  }

  clientTask.wait(waitScope);
  serverTask.wait(waitScope);
}

KJ_TEST("WebSocket fragmented") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr);

  byte DATA[] = {
    0x01, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',

    0x00, 0x03, 'w', 'o', 'r',

    0x80, 0x02, 'l', 'd',
  };

  auto clientTask = client->write(DATA, sizeof(DATA));

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "hello world");
  }

  clientTask.wait(waitScope);
}

#if KJ_HAS_ZLIB
KJ_TEST("WebSocket compressed fragment") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr, CompressionParameters{
      .outboundNoContextTakeover = false,
      .inboundNoContextTakeover = false,
      .outboundMaxWindowBits=15,
      .inboundMaxWindowBits=15,
  });

  // The message is "Hello", sent in two fragments, see the fragmented example at the bottom of:
  // https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.3.1
  byte COMPRESSED_DATA[] = {
    0x41, 0x03, 0xf2, 0x48, 0xcd,

    0x80, 0x04, 0xc9, 0xc9, 0x07, 0x00
  };

  auto clientTask = client->write(COMPRESSED_DATA, sizeof(COMPRESSED_DATA));

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "Hello");
  }

  clientTask.wait(waitScope);
}
#endif // KJ_HAS_ZLIB

class FakeEntropySource final: public EntropySource {
public:
  void generate(kj::ArrayPtr<byte> buffer) override {
    static constexpr byte DUMMY[4] = { 12, 34, 56, 78 };

    for (auto i: kj::indices(buffer)) {
      buffer[i] = DUMMY[i % sizeof(DUMMY)];
    }
  }
};

KJ_TEST("WebSocket masked") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;
  FakeEntropySource maskGenerator;

  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), maskGenerator);

  byte DATA[] = {
    0x81, 0x86, 12, 34, 56, 78, 'h' ^ 12, 'e' ^ 34, 'l' ^ 56, 'l' ^ 78, 'o' ^ 12, ' ' ^ 34,
  };

  auto clientTask = client->write(DATA, sizeof(DATA));
  auto serverTask = server->send(kj::StringPtr("hello "));

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "hello ");
  }

  expectRead(*client, DATA).wait(waitScope);

  clientTask.wait(waitScope);
  serverTask.wait(waitScope);
}

class WebSocketErrorCatcher : public WebSocketErrorHandler {
public:
  kj::Vector<kj::WebSocket::ProtocolError> errors;

  kj::Exception handleWebSocketProtocolError(kj::WebSocket::ProtocolError protocolError) {
    errors.add(kj::mv(protocolError));
    return KJ_EXCEPTION(FAILED, protocolError.description);
  }
};

KJ_TEST("WebSocket unexpected RSV bits") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr, nullptr, errorCatcher);

  byte DATA[] = {
    0x01, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',

    0xF0, 0x05, 'w', 'o', 'r', 'l', 'd'  // all RSV bits set, plus FIN
  };

  auto clientTask = client->write(DATA, sizeof(DATA));

  {
    bool gotException = false;
    auto serverTask = server->receive().then([](auto&& m) {}, [&gotException](kj::Exception&& ex) { gotException = true; });
    serverTask.wait(waitScope);
    KJ_ASSERT(gotException);
    KJ_ASSERT(errorCatcher.errors.size() == 1);
    KJ_ASSERT(errorCatcher.errors[0].statusCode == 1002);
  }

  clientTask.wait(waitScope);
}

KJ_TEST("WebSocket unexpected continuation frame") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr, nullptr, errorCatcher);

  byte DATA[] = {
    0x80, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',  // Continuation frame with no start frame, plus FIN
  };

  auto clientTask = client->write(DATA, sizeof(DATA));

  {
    bool gotException = false;
    auto serverTask = server->receive().then([](auto&& m) {}, [&gotException](kj::Exception&& ex) { gotException = true; });
    serverTask.wait(waitScope);
    KJ_ASSERT(gotException);
    KJ_ASSERT(errorCatcher.errors.size() == 1);
    KJ_ASSERT(errorCatcher.errors[0].statusCode == 1002);
  }

  clientTask.wait(waitScope);
}

KJ_TEST("WebSocket missing continuation frame") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr, nullptr, errorCatcher);

  byte DATA[] = {
    0x01, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',  // Start frame
    0x01, 0x06, 'w', 'o', 'r', 'l', 'd', '!',  // Another start frame
  };

  auto clientTask = client->write(DATA, sizeof(DATA));

  {
    bool gotException = false;
    auto serverTask = server->receive().then([](auto&& m) {}, [&gotException](kj::Exception&& ex) { gotException = true; });
    serverTask.wait(waitScope);
    KJ_ASSERT(gotException);
    KJ_ASSERT(errorCatcher.errors.size() == 1);
  }

  clientTask.wait(waitScope);
}

KJ_TEST("WebSocket fragmented control frame") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr, nullptr, errorCatcher);

  byte DATA[] = {
    0x09, 0x04, 'd', 'a', 't', 'a'  // Fragmented ping frame
  };

  auto clientTask = client->write(DATA, sizeof(DATA));

  {
    bool gotException = false;
    auto serverTask = server->receive().then([](auto&& m) {}, [&gotException](kj::Exception&& ex) { gotException = true; });
    serverTask.wait(waitScope);
    KJ_ASSERT(gotException);
    KJ_ASSERT(errorCatcher.errors.size() == 1);
    KJ_ASSERT(errorCatcher.errors[0].statusCode == 1002);
  }

  clientTask.wait(waitScope);
}

KJ_TEST("WebSocket unknown opcode") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr, nullptr, errorCatcher);

  byte DATA[] = {
    0x85, 0x04, 'd', 'a', 't', 'a'  // 5 is a reserved opcode
  };

  auto clientTask = client->write(DATA, sizeof(DATA));

  {
    bool gotException = false;
    auto serverTask = server->receive().then([](auto&& m) {}, [&gotException](kj::Exception&& ex) { gotException = true; });
    serverTask.wait(waitScope);
    KJ_ASSERT(gotException);
    KJ_ASSERT(errorCatcher.errors.size() == 1);
    KJ_ASSERT(errorCatcher.errors[0].statusCode == 1002);
  }

  clientTask.wait(waitScope);
}

KJ_TEST("WebSocket unsolicited pong") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr);

  byte DATA[] = {
    0x01, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',

    0x8A, 0x03, 'f', 'o', 'o',

    0x80, 0x05, 'w', 'o', 'r', 'l', 'd',
  };

  auto clientTask = client->write(DATA, sizeof(DATA));

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "hello world");
  }

  clientTask.wait(waitScope);
}

KJ_TEST("WebSocket ping") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr);

  // Be extra-annoying by having the ping arrive between fragments.
  byte DATA[] = {
    0x01, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',

    0x89, 0x03, 'f', 'o', 'o',

    0x80, 0x05, 'w', 'o', 'r', 'l', 'd',
  };

  auto clientTask = client->write(DATA, sizeof(DATA));

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "hello world");
  }

  auto serverTask = server->send(kj::StringPtr("bar"));

  byte EXPECTED[] = {
    0x8A, 0x03, 'f', 'o', 'o',  // pong
    0x81, 0x03, 'b', 'a', 'r',  // message
  };

  expectRead(*client, EXPECTED).wait(waitScope);

  clientTask.wait(waitScope);
  serverTask.wait(waitScope);
}

KJ_TEST("WebSocket ping mid-send") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr);

  auto bigString = kj::strArray(kj::repeat(kj::StringPtr("12345678"), 65536), "");
  auto serverTask = server->send(bigString).eagerlyEvaluate(nullptr);

  byte DATA[] = {
    0x89, 0x03, 'f', 'o', 'o',  // ping
    0x81, 0x03, 'b', 'a', 'r',  // some other message
  };

  auto clientTask = client->write(DATA, sizeof(DATA));

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "bar");
  }

  byte EXPECTED1[] = { 0x81, 0x7f, 0, 0, 0, 0, 0, 8, 0, 0 };
  expectRead(*client, EXPECTED1).wait(waitScope);
  expectRead(*client, bigString).wait(waitScope);

  byte EXPECTED2[] = { 0x8A, 0x03, 'f', 'o', 'o' };
  expectRead(*client, EXPECTED2).wait(waitScope);

  clientTask.wait(waitScope);
  serverTask.wait(waitScope);
}

class InputOutputPair final: public kj::AsyncIoStream {
  // Creates an AsyncIoStream out of an AsyncInputStream and an AsyncOutputStream.

public:
  InputOutputPair(kj::Own<kj::AsyncInputStream> in, kj::Own<kj::AsyncOutputStream> out)
      : in(kj::mv(in)), out(kj::mv(out)) {}

  kj::Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return in->read(buffer, minBytes, maxBytes);
  }
  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return in->tryRead(buffer, minBytes, maxBytes);
  }

  Maybe<uint64_t> tryGetLength() override {
    return in->tryGetLength();
  }

  Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount = kj::maxValue) override {
    return in->pumpTo(output, amount);
  }

  kj::Promise<void> write(const void* buffer, size_t size) override {
    return out->write(buffer, size);
  }

  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    return out->write(pieces);
  }

  kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(
      kj::AsyncInputStream& input, uint64_t amount = kj::maxValue) override {
    return out->tryPumpFrom(input, amount);
  }

  Promise<void> whenWriteDisconnected() override {
    return out->whenWriteDisconnected();
  }

  void shutdownWrite() override {
    out = nullptr;
  }

private:
  kj::Own<kj::AsyncInputStream> in;
  kj::Own<kj::AsyncOutputStream> out;
};

KJ_TEST("WebSocket double-ping mid-send") {
  KJ_HTTP_TEST_SETUP_IO;

  auto upPipe = newOneWayPipe();
  auto downPipe = newOneWayPipe();
  InputOutputPair client(kj::mv(downPipe.in), kj::mv(upPipe.out));
  auto server = newWebSocket(kj::heap<InputOutputPair>(kj::mv(upPipe.in), kj::mv(downPipe.out)),
                             nullptr);

  auto bigString = kj::strArray(kj::repeat(kj::StringPtr("12345678"), 65536), "");
  auto serverTask = server->send(bigString).eagerlyEvaluate(nullptr);

  byte DATA[] = {
    0x89, 0x03, 'f', 'o', 'o',  // ping
    0x89, 0x03, 'q', 'u', 'x',  // ping2
    0x81, 0x03, 'b', 'a', 'r',  // some other message
  };

  auto clientTask = client.write(DATA, sizeof(DATA));

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "bar");
  }

  byte EXPECTED1[] = { 0x81, 0x7f, 0, 0, 0, 0, 0, 8, 0, 0 };
  expectRead(client, EXPECTED1).wait(waitScope);
  expectRead(client, bigString).wait(waitScope);

  byte EXPECTED2[] = { 0x8A, 0x03, 'q', 'u', 'x' };
  expectRead(client, EXPECTED2).wait(waitScope);

  clientTask.wait(waitScope);
  serverTask.wait(waitScope);
}

KJ_TEST("WebSocket ping received during pong send") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr);

  // Send a very large ping so that sending the pong takes a while. Then send a second ping
  // immediately after.
  byte PREFIX[] = { 0x89, 0x7f, 0, 0, 0, 0, 0, 8, 0, 0 };
  auto bigString = kj::strArray(kj::repeat(kj::StringPtr("12345678"), 65536), "");
  byte POSTFIX[] = {
    0x89, 0x03, 'f', 'o', 'o',
    0x81, 0x03, 'b', 'a', 'r',
  };

  kj::ArrayPtr<const byte> parts[] = {PREFIX, bigString.asBytes(), POSTFIX};
  auto clientTask = client->write(parts);

  {
    auto message = server->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "bar");
  }

  byte EXPECTED1[] = { 0x8A, 0x7f, 0, 0, 0, 0, 0, 8, 0, 0 };
  expectRead(*client, EXPECTED1).wait(waitScope);
  expectRead(*client, bigString).wait(waitScope);

  byte EXPECTED2[] = { 0x8A, 0x03, 'f', 'o', 'o' };
  expectRead(*client, EXPECTED2).wait(waitScope);

  clientTask.wait(waitScope);
}

KJ_TEST("WebSocket pump byte counting") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe1 = KJ_HTTP_TEST_CREATE_2PIPE;
  auto pipe2 = KJ_HTTP_TEST_CREATE_2PIPE;

  FakeEntropySource maskGenerator;
  auto server1 = newWebSocket(kj::mv(pipe1.ends[1]), nullptr);
  auto client2 = newWebSocket(kj::mv(pipe2.ends[0]), maskGenerator);
  auto server2 = newWebSocket(kj::mv(pipe2.ends[1]), nullptr);

  auto pumpTask = server1->pumpTo(*client2);
  auto receiveTask = server2->receive();

  // Client sends three bytes of a valid message then disconnects.
  const char DATA[] = {0x01, 0x06, 'h'};
  pipe1.ends[0]->write(DATA, 3).wait(waitScope);
  pipe1.ends[0] = nullptr;

  // The pump completes successfully, forwarding the disconnect.
  pumpTask.wait(waitScope);

  // The eventual receiver gets a disconnect exception.
  // (Note: We don't use KJ_EXPECT_THROW here because under -fno-exceptions it forks and we lose
  // state.)
  receiveTask.then([](auto) {
    KJ_FAIL_EXPECT("expected exception");
  }, [](kj::Exception&& e) {
    KJ_EXPECT(e.getType() == kj::Exception::Type::DISCONNECTED);
  }).wait(waitScope);

  KJ_EXPECT(server1->receivedByteCount() == 3);
#if KJ_NO_RTTI
  // Optimized socket pump will be disabled, so only whole messages are counted by client2/server2.
  KJ_EXPECT(client2->sentByteCount() == 0);
  KJ_EXPECT(server2->receivedByteCount() == 0);
#else
  KJ_EXPECT(client2->sentByteCount() == 3);
  KJ_EXPECT(server2->receivedByteCount() == 3);
#endif
}

KJ_TEST("WebSocket pump disconnect on send") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe1 = KJ_HTTP_TEST_CREATE_2PIPE;
  auto pipe2 = KJ_HTTP_TEST_CREATE_2PIPE;

  FakeEntropySource maskGenerator;
  auto client1 = newWebSocket(kj::mv(pipe1.ends[0]), maskGenerator);
  auto server1 = newWebSocket(kj::mv(pipe1.ends[1]), nullptr);
  auto client2 = newWebSocket(kj::mv(pipe2.ends[0]), maskGenerator);

  auto pumpTask = server1->pumpTo(*client2);
  auto sendTask = client1->send("hello"_kj);

  // Endpoint reads three bytes and then disconnects.
  char buffer[3];
  pipe2.ends[1]->read(buffer, 3).wait(waitScope);
  pipe2.ends[1] = nullptr;

  // Pump throws disconnected.
  KJ_EXPECT_THROW_RECOVERABLE(DISCONNECTED, pumpTask.wait(waitScope));

  // client1 may or may not have been able to send its whole message depending on buffering.
  sendTask.then([]() {}, [](kj::Exception&& e) {
    KJ_EXPECT(e.getType() == kj::Exception::Type::DISCONNECTED);
  }).wait(waitScope);
}

KJ_TEST("WebSocket pump disconnect on receive") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe1 = KJ_HTTP_TEST_CREATE_2PIPE;
  auto pipe2 = KJ_HTTP_TEST_CREATE_2PIPE;

  FakeEntropySource maskGenerator;
  auto server1 = newWebSocket(kj::mv(pipe1.ends[1]), nullptr);
  auto client2 = newWebSocket(kj::mv(pipe2.ends[0]), maskGenerator);
  auto server2 = newWebSocket(kj::mv(pipe2.ends[1]), nullptr);

  auto pumpTask = server1->pumpTo(*client2);
  auto receiveTask = server2->receive();

  // Client sends three bytes of a valid message then disconnects.
  const char DATA[] = {0x01, 0x06, 'h'};
  pipe1.ends[0]->write(DATA, 3).wait(waitScope);
  pipe1.ends[0] = nullptr;

  // The pump completes successfully, forwarding the disconnect.
  pumpTask.wait(waitScope);

  // The eventual receiver gets a disconnect exception.
  KJ_EXPECT_THROW(DISCONNECTED, receiveTask.wait(waitScope));
}

KJ_TEST("WebSocket abort propagates through pipe") {
  // Pumping one end of a WebSocket pipe into another WebSocket which later becomes aborted will
  // cancel the pump promise with a DISCONNECTED exception.

  KJ_HTTP_TEST_SETUP_IO;
  auto pipe1 = KJ_HTTP_TEST_CREATE_2PIPE;

  auto server = newWebSocket(kj::mv(pipe1.ends[1]), nullptr);
  auto client = newWebSocket(kj::mv(pipe1.ends[0]), nullptr);

  auto wsPipe = newWebSocketPipe();

  auto downstreamPump = wsPipe.ends[0]->pumpTo(*server);
  KJ_EXPECT(!downstreamPump.poll(waitScope));

  client->abort();

  KJ_EXPECT(downstreamPump.poll(waitScope));
  KJ_EXPECT_THROW_RECOVERABLE(DISCONNECTED, downstreamPump.wait(waitScope));
}

KJ_TEST("WebSocket maximum message size") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe =KJ_HTTP_TEST_CREATE_2PIPE;

  WebSocketErrorCatcher errorCatcher;
  FakeEntropySource maskGenerator;
  auto client = newWebSocket(kj::mv(pipe.ends[0]), maskGenerator);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr, nullptr, errorCatcher);

  size_t maxSize = 100;
  auto biggestAllowedString = kj::strArray(kj::repeat(kj::StringPtr("A"), maxSize), "");
  auto tooBigString = kj::strArray(kj::repeat(kj::StringPtr("B"), maxSize + 1), "");

  auto clientTask = client->send(biggestAllowedString)
      .then([&]() { return client->send(tooBigString); })
      .then([&]() { return client->close(1234, "done"); });

  {
    auto message = server->receive(maxSize).wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>().size() == maxSize);
  }

  {
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("too large",
        server->receive(maxSize).ignoreResult().wait(waitScope));
    KJ_ASSERT(errorCatcher.errors.size() == 1);
    KJ_ASSERT(errorCatcher.errors[0].statusCode == 1009);
  }
}

class TestWebSocketService final: public HttpService, private kj::TaskSet::ErrorHandler {
public:
  TestWebSocketService(HttpHeaderTable& headerTable, HttpHeaderId hMyHeader)
      : headerTable(headerTable), hMyHeader(hMyHeader), tasks(*this) {}

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    KJ_ASSERT(headers.isWebSocket());

    HttpHeaders responseHeaders(headerTable);
    KJ_IF_MAYBE(h, headers.get(hMyHeader)) {
      responseHeaders.set(hMyHeader, kj::str("respond-", *h));
    }

    if (url == "/return-error") {
      response.send(404, "Not Found", responseHeaders, uint64_t(0));
      return kj::READY_NOW;
    } else if (url == "/websocket") {
      auto ws = response.acceptWebSocket(responseHeaders);
      return doWebSocket(*ws, "start-inline").attach(kj::mv(ws));
    } else {
      KJ_FAIL_ASSERT("unexpected path", url);
    }
  }

private:
  HttpHeaderTable& headerTable;
  HttpHeaderId hMyHeader;
  kj::TaskSet tasks;

  void taskFailed(kj::Exception&& exception) override {
    KJ_LOG(ERROR, exception);
  }

  static kj::Promise<void> doWebSocket(WebSocket& ws, kj::StringPtr message) {
    auto copy = kj::str(message);
    return ws.send(copy).attach(kj::mv(copy))
        .then([&ws]() {
      return ws.receive();
    }).then([&ws](WebSocket::Message&& message) {
      KJ_SWITCH_ONEOF(message) {
        KJ_CASE_ONEOF(str, kj::String) {
          return doWebSocket(ws, kj::str("reply:", str));
        }
        KJ_CASE_ONEOF(data, kj::Array<byte>) {
          return doWebSocket(ws, kj::str("reply:", data));
        }
        KJ_CASE_ONEOF(close, WebSocket::Close) {
          auto reason = kj::str("close-reply:", close.reason);
          return ws.close(close.code + 1, reason).attach(kj::mv(reason));
        }
      }
      KJ_UNREACHABLE;
    });
  }
};

const char WEBSOCKET_REQUEST_HANDSHAKE[] =
    " HTTP/1.1\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Key: DCI4TgwiOE4MIjhODCI4Tg==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "My-Header: foo\r\n"
    "\r\n";
const char WEBSOCKET_RESPONSE_HANDSHAKE[] =
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Accept: pShtIFKT0s8RYZvnWY/CrjQD8CM=\r\n"
    "My-Header: respond-foo\r\n"
    "\r\n";
#if KJ_HAS_ZLIB
const char WEBSOCKET_COMPRESSION_HANDSHAKE[] =
    " HTTP/1.1\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Key: DCI4TgwiOE4MIjhODCI4Tg==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "Sec-WebSocket-Extensions: permessage-deflate; server_no_context_takeover\r\n"
    "\r\n";
const char WEBSOCKET_COMPRESSION_RESPONSE_HANDSHAKE[] =
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Accept: pShtIFKT0s8RYZvnWY/CrjQD8CM=\r\n"
    "Sec-WebSocket-Extensions: permessage-deflate; server_no_context_takeover\r\n"
    "\r\n";
const char WEBSOCKET_COMPRESSION_CLIENT_DISCARDS_CTX_HANDSHAKE[] =
    " HTTP/1.1\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Key: DCI4TgwiOE4MIjhODCI4Tg==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "Sec-WebSocket-Extensions: permessage-deflate; client_no_context_takeover; "
        "server_no_context_takeover\r\n"
    "\r\n";
const char WEBSOCKET_COMPRESSION_CLIENT_DISCARDS_CTX_RESPONSE_HANDSHAKE[] =
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Accept: pShtIFKT0s8RYZvnWY/CrjQD8CM=\r\n"
    "Sec-WebSocket-Extensions: permessage-deflate; client_no_context_takeover; "
        "server_no_context_takeover\r\n"
    "\r\n";
#endif // KJ_HAS_ZLIB
const char WEBSOCKET_RESPONSE_HANDSHAKE_ERROR[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: 0\r\n"
    "My-Header: respond-foo\r\n"
    "\r\n";
const byte WEBSOCKET_FIRST_MESSAGE_INLINE[] =
    { 0x81, 0x0c, 's','t','a','r','t','-','i','n','l','i','n','e' };
const byte WEBSOCKET_SEND_MESSAGE[] =
    { 0x81, 0x83, 12, 34, 56, 78, 'b'^12, 'a'^34, 'r'^56 };
const byte WEBSOCKET_REPLY_MESSAGE[] =
    { 0x81, 0x09, 'r','e','p','l','y',':','b','a','r' };
const byte WEBSOCKET_SEND_CLOSE[] =
    { 0x88, 0x85, 12, 34, 56, 78, 0x12^12, 0x34^34, 'q'^56, 'u'^78, 'x'^12 };
const byte WEBSOCKET_REPLY_CLOSE[] =
    { 0x88, 0x11, 0x12, 0x35, 'c','l','o','s','e','-','r','e','p','l','y',':','q','u','x' };

#if KJ_HAS_ZLIB
const byte WEBSOCKET_FIRST_COMPRESSED_MESSAGE[] =
    { 0xc1, 0x07, 0xf2, 0x48, 0xcd, 0xc9, 0xc9, 0x07, 0x00 };
// See this example: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.3.2
const byte WEBSOCKET_SEND_COMPRESSED_MESSAGE[] =
    { 0xc1, 0x87, 12, 34, 56, 78, 0xf2^12, 0x48^34, 0xcd^56, 0xc9^78, 0xc9^12, 0x07^34, 0x00^56 };
const byte WEBSOCKET_SEND_COMPRESSED_MESSAGE_REUSE_CTX[] =
    { 0xc1, 0x85, 12, 34, 56, 78, 0xf2^12, 0x00^34, 0x11^56, 0x00^78, 0x00^12};
// See same compression example, but where `client_no_context_takeover` is used (saves 2 bytes).
const byte WEBSOCKET_DEFLATE_NO_COMPRESSION_MESSAGE[] =
    { 0xc1, 0x0b, 0x00, 0x05, 0x00, 0xfa, 0xff, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x00 };
// See this example: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.3.3
// This uses a DEFLATE block with no compression.
const byte WEBSOCKET_BFINAL_SET_MESSAGE[] =
    { 0xc1, 0x08, 0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0x07, 0x00, 0x00 };
// See this example: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.3.4
// This uses a DEFLATE block with BFINAL set to 1.
const byte WEBSOCKET_TWO_DEFLATE_BLOCKS_MESSAGE[] =
    {  0xc1, 0x0d, 0xf2, 0x48, 0x05, 0x00, 0x00, 0x00, 0xff, 0xff, 0xca, 0xc9, 0xc9, 0x07, 0x00 };
// See this example: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.3.5
// This uses two DEFLATE blocks in a single message.
const byte WEBSOCKET_EMPTY_COMPRESSED_MESSAGE[] =
    { 0xc1, 0x01, 0x00 };
const byte WEBSOCKET_EMPTY_SEND_COMPRESSED_MESSAGE[] =
    { 0xc1, 0x81, 12, 34, 56, 78, 0x00^12 };
#endif // KJ_HAS_ZLIB

template <size_t s>
kj::ArrayPtr<const byte> asBytes(const char (&chars)[s]) {
  return kj::ArrayPtr<const char>(chars, s - 1).asBytes();
}

void testWebSocketClient(kj::WaitScope& waitScope, HttpHeaderTable& headerTable,
                         kj::HttpHeaderId hMyHeader, HttpClient& client) {
  kj::HttpHeaders headers(headerTable);
  headers.set(hMyHeader, "foo");
  auto response = client.openWebSocket("/websocket", headers).wait(waitScope);

  KJ_EXPECT(response.statusCode == 101);
  KJ_EXPECT(response.statusText == "Switching Protocols", response.statusText);
  KJ_EXPECT(KJ_ASSERT_NONNULL(response.headers->get(hMyHeader)) == "respond-foo");
  KJ_ASSERT(response.webSocketOrBody.is<kj::Own<WebSocket>>());
  auto ws = kj::mv(response.webSocketOrBody.get<kj::Own<WebSocket>>());

  {
    auto message = ws->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "start-inline");
  }

  ws->send(kj::StringPtr("bar")).wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "reply:bar");
  }

  ws->close(0x1234, "qux").wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    KJ_ASSERT(message.is<WebSocket::Close>());
    KJ_EXPECT(message.get<WebSocket::Close>().code == 0x1235);
    KJ_EXPECT(message.get<WebSocket::Close>().reason == "close-reply:qux");
  }
}

#if KJ_HAS_ZLIB
void testWebSocketTwoMessageCompression(kj::WaitScope& waitScope, HttpHeaderTable& headerTable,
                                        kj::HttpHeaderId extHeader, kj::StringPtr extensions,
                                        HttpClient& client) {
  // In this test, the server will always use `server_no_context_takeover` (since we can just reuse
  // the message). However, we will modify the client's compressor in different ways to see how the
  // compressed message changes.

  kj::HttpHeaders headers(headerTable);
  headers.set(extHeader, extensions);
  auto response = client.openWebSocket("/websocket", headers).wait(waitScope);

  KJ_EXPECT(response.statusCode == 101);
  KJ_EXPECT(response.statusText == "Switching Protocols", response.statusText);
  KJ_EXPECT(KJ_ASSERT_NONNULL(response.headers->get(extHeader)).startsWith("permessage-deflate"));
  KJ_ASSERT(response.webSocketOrBody.is<kj::Own<WebSocket>>());
  auto ws = kj::mv(response.webSocketOrBody.get<kj::Own<WebSocket>>());

  {
    auto message = ws->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "Hello");
  }
  ws->send(kj::StringPtr("Hello")).wait(waitScope);

  {
    auto message = ws->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "Hello");
  }
  ws->send(kj::StringPtr("Hello")).wait(waitScope);

  ws->close(0x1234, "qux").wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    KJ_ASSERT(message.is<WebSocket::Close>());
    KJ_EXPECT(message.get<WebSocket::Close>().code == 0x1235);
    KJ_EXPECT(message.get<WebSocket::Close>().reason == "close-reply:qux");
  }
}
#endif // KJ_HAS_ZLIB

#if KJ_HAS_ZLIB
void testWebSocketEmptyMessageCompression(kj::WaitScope& waitScope, HttpHeaderTable& headerTable,
                                        kj::HttpHeaderId extHeader, kj::StringPtr extensions,
                                        HttpClient& client) {
  // Confirm that we can send empty messages when compression is enabled.

  kj::HttpHeaders headers(headerTable);
  headers.set(extHeader, extensions);
  auto response = client.openWebSocket("/websocket", headers).wait(waitScope);

  KJ_EXPECT(response.statusCode == 101);
  KJ_EXPECT(response.statusText == "Switching Protocols", response.statusText);
  KJ_EXPECT(KJ_ASSERT_NONNULL(response.headers->get(extHeader)).startsWith("permessage-deflate"));
  KJ_ASSERT(response.webSocketOrBody.is<kj::Own<WebSocket>>());
  auto ws = kj::mv(response.webSocketOrBody.get<kj::Own<WebSocket>>());

  {
    auto message = ws->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "Hello");
  }
  ws->send(kj::StringPtr("Hello")).wait(waitScope);

  {
    auto message = ws->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "");
  }
  ws->send(kj::StringPtr("")).wait(waitScope);

  {
    auto message = ws->receive().wait(waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "Hello");
  }
  ws->send(kj::StringPtr("Hello")).wait(waitScope);

  ws->close(0x1234, "qux").wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    KJ_ASSERT(message.is<WebSocket::Close>());
    KJ_EXPECT(message.get<WebSocket::Close>().code == 0x1235);
    KJ_EXPECT(message.get<WebSocket::Close>().reason == "close-reply:qux");
  }
}
#endif // KJ_HAS_ZLIB

#if KJ_HAS_ZLIB
void testWebSocketOptimizePumpProxy(kj::WaitScope& waitScope, HttpHeaderTable& headerTable,
                                    kj::HttpHeaderId extHeader, kj::StringPtr extensions,
                                    HttpClient& client) {
  // Suppose we are proxying a websocket conversation between a client and a server.
  // This looks something like: CLIENT <--> (proxyServer <==PUMP==> proxyClient) <--> SERVER
  //
  // We want to enable optimizedPumping from the proxy's server (which communicates with the client),
  // to the proxy's client (which communicates with the origin server).
  //
  // For this to work, proxyServer's inbound settings must map to proxyClient's outbound settings
  // (and vice versa). In this case, `ws` is `proxyClient`, so we want to take `ws`'s compression
  // configuration and pass it to `proxyServer` in a way that would allow for optimizedPumping.

  kj::HttpHeaders headers(headerTable);
  headers.set(extHeader, extensions);
  auto response = client.openWebSocket("/websocket", headers).wait(waitScope);

  KJ_EXPECT(response.statusCode == 101);
  KJ_EXPECT(response.statusText == "Switching Protocols", response.statusText);
  KJ_EXPECT(KJ_ASSERT_NONNULL(response.headers->get(extHeader)).startsWith("permessage-deflate"));
  KJ_ASSERT(response.webSocketOrBody.is<kj::Own<WebSocket>>());
  auto ws = kj::mv(response.webSocketOrBody.get<kj::Own<WebSocket>>());

  auto maybeExt = ws->getPreferredExtensions(kj::WebSocket::ExtensionsContext::REQUEST);
  // Should be nullptr since we are asking `ws` (a client) to give us extensions that we can give to
  // another client. Since clients cannot `optimizedPumpTo` each other, we must get null.
  KJ_ASSERT(maybeExt == nullptr);

  maybeExt = ws->getPreferredExtensions(kj::WebSocket::ExtensionsContext::RESPONSE);
  kj::StringPtr extStr = KJ_ASSERT_NONNULL(maybeExt);
  KJ_ASSERT(extStr == "permessage-deflate; server_no_context_takeover");
  // We got back the string the client sent!
  // We could then pass this string as a header to `acceptWebSocket` and ensure the `proxyServer`s
  // inbound settings match the `proxyClient`s outbound settings.

  ws->close(0x1234, "qux").wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    KJ_ASSERT(message.is<WebSocket::Close>());
    KJ_EXPECT(message.get<WebSocket::Close>().code == 0x1235);
    KJ_EXPECT(message.get<WebSocket::Close>().reason == "close-reply:qux");
  }
}
#endif // KJ_HAS_ZLIB
#if KJ_HAS_ZLIB
void testWebSocketFourMessageCompression(kj::WaitScope& waitScope, HttpHeaderTable& headerTable,
                                          kj::HttpHeaderId extHeader, kj::StringPtr extensions,
                                          HttpClient& client) {
  // In this test, the server will always use `server_no_context_takeover` (since we can just reuse
  // the message). We will receive three messages.

  kj::HttpHeaders headers(headerTable);
  headers.set(extHeader, extensions);
  auto response = client.openWebSocket("/websocket", headers).wait(waitScope);

  KJ_EXPECT(response.statusCode == 101);
  KJ_EXPECT(response.statusText == "Switching Protocols", response.statusText);
  KJ_EXPECT(KJ_ASSERT_NONNULL(response.headers->get(extHeader)).startsWith("permessage-deflate"));
  KJ_ASSERT(response.webSocketOrBody.is<kj::Own<WebSocket>>());
  auto ws = kj::mv(response.webSocketOrBody.get<kj::Own<WebSocket>>());

  for (size_t i = 0; i < 4; i++) {
    {
      auto message = ws->receive().wait(waitScope);
      KJ_ASSERT(message.is<kj::String>());
      KJ_EXPECT(message.get<kj::String>() == "Hello");
    }
  }

  ws->close(0x1234, "qux").wait(waitScope);
  {
    auto message = ws->receive().wait(waitScope);
    KJ_ASSERT(message.is<WebSocket::Close>());
    KJ_EXPECT(message.get<WebSocket::Close>().code == 0x1235);
    KJ_EXPECT(message.get<WebSocket::Close>().reason == "close-reply:qux");
  }
}
#endif // KJ_HAS_ZLIB

inline kj::Promise<void> writeA(kj::AsyncOutputStream& out, kj::ArrayPtr<const byte> data) {
  return out.write(data.begin(), data.size());
}

KJ_TEST("HttpClient WebSocket handshake") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto request = kj::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);

  auto serverTask = expectRead(*pipe.ends[1], request)
      .then([&]() { return writeA(*pipe.ends[1], asBytes(WEBSOCKET_RESPONSE_HANDSHAKE)); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_MESSAGE_INLINE); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_MESSAGE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_MESSAGE); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  testWebSocketClient(waitScope, *headerTable, hMyHeader, *client);

  serverTask.wait(waitScope);
}

KJ_TEST("WebSocket Compression String Parsing (splitNext)") {
  // Test `splitNext()`.
  // We want to assert that:
  // If a delimiter is found:
  // - `input` is updated to point to the rest of the string after the delimiter.
  // - The text before the delimiter is returned.
  // If no delimiter is found:
  // - `input` is updated to an empty string.
  // - The text that had been in `input` is returned.

  const auto s =  "permessage-deflate;   client_max_window_bits=10;server_no_context_takeover"_kj;

  const auto expectedPartOne = "permessage-deflate"_kj;
  const auto expectedRemainingOne = "client_max_window_bits=10;server_no_context_takeover"_kj;

  auto cursor = s.asArray();
  auto actual = _::splitNext(cursor, ';');
  KJ_ASSERT(actual == expectedPartOne);

  _::stripLeadingAndTrailingSpace(cursor);
  KJ_ASSERT(cursor == expectedRemainingOne.asArray());

  const auto expectedPartTwo = "client_max_window_bits=10"_kj;
  const auto expectedRemainingTwo = "server_no_context_takeover"_kj;

  actual = _::splitNext(cursor, ';');
  KJ_ASSERT(actual == expectedPartTwo);
  KJ_ASSERT(cursor == expectedRemainingTwo);

  const auto expectedPartThree = "server_no_context_takeover"_kj;
  const auto expectedRemainingThree = ""_kj;
  actual = _::splitNext(cursor, ';');
  KJ_ASSERT(actual == expectedPartThree);
  KJ_ASSERT(cursor == expectedRemainingThree);
}

KJ_TEST("WebSocket Compression String Parsing (splitParts)") {
  // Test `splitParts()`.
  // We want to assert that we:
  //  1. Correctly split by the delimiter.
  //  2. Strip whitespace before/after the extracted part.
  const auto permitted = "permessage-deflate"_kj;

  const auto s =  "permessage-deflate; client_max_window_bits=10;server_no_context_takeover,    "
                  "    permessage-deflate;  ;   ,"  // strips leading whitespace
                  "permessage-deflate"_kj;

  // These are the expected values.
  const auto extOne = "permessage-deflate; client_max_window_bits=10;server_no_context_takeover"_kj;
  const auto extTwo = "permessage-deflate;  ;"_kj;
  const auto extThree = "permessage-deflate"_kj;

  auto actualExtensions = kj::_::splitParts(s, ',');
  KJ_ASSERT(actualExtensions.size() == 3);
  KJ_ASSERT(actualExtensions[0] == extOne);
  KJ_ASSERT(actualExtensions[1] == extTwo);
  KJ_ASSERT(actualExtensions[2] == extThree);
  // Splitting by ',' was fine, now let's try splitting the parameters (split by ';').

  const auto paramOne = "client_max_window_bits=10"_kj;
  const auto paramTwo = "server_no_context_takeover"_kj;

  auto actualParamsFirstExt = kj::_::splitParts(actualExtensions[0], ';');
  KJ_ASSERT(actualParamsFirstExt.size() == 3);
  KJ_ASSERT(actualParamsFirstExt[0] == permitted);
  KJ_ASSERT(actualParamsFirstExt[1] == paramOne);
  KJ_ASSERT(actualParamsFirstExt[2] == paramTwo);

  auto actualParamsSecondExt = kj::_::splitParts(actualExtensions[1], ';');
  KJ_ASSERT(actualParamsSecondExt.size() == 2);
  KJ_ASSERT(actualParamsSecondExt[0] == permitted);
  KJ_ASSERT(actualParamsSecondExt[1] == ""_kj); // Note that the whitespace was stripped.

  auto actualParamsThirdExt = kj::_::splitParts(actualExtensions[2], ';');
  // No parameters supplied in the third offer. We expect to only see the extension name.
  KJ_ASSERT(actualParamsThirdExt.size() == 1);
  KJ_ASSERT(actualParamsThirdExt[0] == permitted);
}

KJ_TEST("WebSocket Compression String Parsing (toKeysAndVals)") {
  // If an "=" is found, everything before the "=" goes into the `Key` and everything after goes
  // into the `Value`. Otherwise, everything goes into the `Key` and the `Value` remains null.
  const auto cleanParameters =  "client_no_context_takeover; client_max_window_bits; "
                                "server_max_window_bits=10"_kj;
  auto parts = _::splitParts(cleanParameters, ';');
  auto keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  KJ_ASSERT(keysMaybeValues.size() == 3);

  auto firstKey = "client_no_context_takeover"_kj;
  KJ_ASSERT(keysMaybeValues[0].key == firstKey.asArray());
  KJ_ASSERT(keysMaybeValues[0].val == nullptr);

  auto secondKey = "client_max_window_bits"_kj;
  KJ_ASSERT(keysMaybeValues[1].key == secondKey.asArray());
  KJ_ASSERT(keysMaybeValues[1].val == nullptr);

  auto thirdKey = "server_max_window_bits"_kj;
  auto thirdVal = "10"_kj;
  KJ_ASSERT(keysMaybeValues[2].key == thirdKey.asArray());
  KJ_ASSERT(keysMaybeValues[2].val == thirdVal.asArray());

  const auto weirdParameters =  "= 14 ; client_max_window_bits= ; server_max_window_bits =hello"_kj;
  // This is weird because:
  //  1. Parameter 1 has no key.
  //  2. Parameter 2 has an "=" but no subsequent value.
  //  3. Parameter 3 has an "=" with an invalid value.
  // That said, we don't mind if the parameters are weird when calling this function. The point
  // is to create KeyMaybeVal pairs and process them later.

  parts = _::splitParts(weirdParameters, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  KJ_ASSERT(keysMaybeValues.size() == 3);

  firstKey = ""_kj;
  auto firstVal = "14"_kj;
  KJ_ASSERT(keysMaybeValues[0].key == firstKey.asArray());
  KJ_ASSERT(keysMaybeValues[0].val == firstVal.asArray());

  secondKey = "client_max_window_bits"_kj;
  auto secondVal = ""_kj;
  KJ_ASSERT(keysMaybeValues[1].key == secondKey.asArray());
  KJ_ASSERT(keysMaybeValues[1].val == secondVal.asArray());

  thirdKey = "server_max_window_bits"_kj;
  thirdVal = "hello"_kj;
  KJ_ASSERT(keysMaybeValues[2].key == thirdKey.asArray());
  KJ_ASSERT(keysMaybeValues[2].val == thirdVal.asArray());
}

KJ_TEST("WebSocket Compression String Parsing (populateUnverifiedConfig)") {
  // First we'll cover cases where the `UnverifiedConfig` is successfully constructed,
  // which indicates the offer was structured in a parseable way. Next, we'll cover cases where the
  // offer is structured incorrectly.
  const auto cleanParameters =  "client_no_context_takeover; client_max_window_bits; "
                                "server_max_window_bits=10"_kj;
  auto parts = _::splitParts(cleanParameters, ';');
  auto keysMaybeValues = _::toKeysAndVals(parts.asPtr());

  auto unverified = _::populateUnverifiedConfig(keysMaybeValues);
  auto config = KJ_ASSERT_NONNULL(unverified);
  KJ_ASSERT(config.clientNoContextTakeover == true);
  KJ_ASSERT(config.serverNoContextTakeover == false);

  auto clientBits = KJ_ASSERT_NONNULL(config.clientMaxWindowBits);
  KJ_ASSERT(clientBits == ""_kj);
  auto serverBits = KJ_ASSERT_NONNULL(config.serverMaxWindowBits);
  KJ_ASSERT(serverBits == "10"_kj);
  // Valid config can be populated succesfully.

  const auto weirdButValidParameters =  "client_no_context_takeover; client_max_window_bits; "
                                        "server_max_window_bits=this_should_be_a_number"_kj;
  parts = _::splitParts(weirdButValidParameters, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());

  unverified = _::populateUnverifiedConfig(keysMaybeValues);
  config = KJ_ASSERT_NONNULL(unverified);
  KJ_ASSERT(config.clientNoContextTakeover == true);
  KJ_ASSERT(config.serverNoContextTakeover == false);

  clientBits = KJ_ASSERT_NONNULL(config.clientMaxWindowBits);
  KJ_ASSERT(clientBits == ""_kj);
  serverBits = KJ_ASSERT_NONNULL(config.serverMaxWindowBits);
  KJ_ASSERT(serverBits == "this_should_be_a_number"_kj);
  // Note that while the value associated with `server_max_window_bits` is not a number,
  // `populateUnverifiedConfig` succeeds because the parameter[=value] is generally structured
  // correctly.

  // --- HANDLE INCORRECTLY STRUCTURED OFFERS ---
  auto invalidKey = "somethingKey; client_max_window_bits;"_kj;
  parts = _::splitParts(invalidKey, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  KJ_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == nullptr);
  // Fail to populate due to invalid key name

  auto invalidKeyTwo = "client_max_window_bitsJUNK; server_no_context_takeover"_kj;
  parts = _::splitParts(invalidKeyTwo, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  KJ_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == nullptr);
  // Fail to populate due to invalid key name (invalid characters after valid parameter name).

  auto repeatedKey = "client_no_context_takeover; client_no_context_takeover"_kj;
  parts = _::splitParts(repeatedKey, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  KJ_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == nullptr);
  // Fail to populate due to repeated key name.

  auto unexpectedValue = "client_no_context_takeover="_kj;
  parts = _::splitParts(unexpectedValue, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  KJ_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == nullptr);
  // Fail to populate due to value in `x_no_context_takeover` parameter (unexpected value).

  auto unexpectedValueTwo = "client_no_context_takeover=   "_kj;
  parts = _::splitParts(unexpectedValueTwo, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  KJ_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == nullptr);
  // Fail to populate due to value in `x_no_context_takeover` parameter.

  auto emptyValue = "client_max_window_bits="_kj;
  parts = _::splitParts(emptyValue, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  KJ_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == nullptr);
  // Fail to populate due to empty value in `x_max_window_bits` parameter.
  // "Empty" in this case means an "=" was provided, but no subsequent value was provided.

  auto emptyValueTwo = "client_max_window_bits=   "_kj;
  parts = _::splitParts(emptyValueTwo, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  KJ_ASSERT(_::populateUnverifiedConfig(keysMaybeValues) == nullptr);
  // Fail to populate due to empty value in `x_max_window_bits` parameter.
  // "Empty" in this case means an "=" was provided, but no subsequent value was provided.
}

KJ_TEST("WebSocket Compression String Parsing (validateCompressionConfig)") {
  // We've tested `toKeysAndVals()` and `populateUnverifiedConfig()`, so we only need to test
  // correctly structured offers/agreements here.
  const auto cleanParameters =  "client_no_context_takeover; client_max_window_bits; "
                                "server_max_window_bits=10"_kj;
  auto parts = _::splitParts(cleanParameters, ';');
  auto keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  auto maybeUnverified = _::populateUnverifiedConfig(keysMaybeValues);
  auto unverified = KJ_ASSERT_NONNULL(maybeUnverified);
  auto maybeValid = _::validateCompressionConfig(kj::mv(unverified), false); // Validate as Server.
  auto valid = KJ_ASSERT_NONNULL(maybeValid);
  KJ_ASSERT(valid.inboundNoContextTakeover == true);
  KJ_ASSERT(valid.outboundNoContextTakeover == false);
  auto inboundBits = KJ_ASSERT_NONNULL(valid.inboundMaxWindowBits);
  KJ_ASSERT(inboundBits == 15); // `client_max_window_bits` can be empty in an offer.
  auto outboundBits = KJ_ASSERT_NONNULL(valid.outboundMaxWindowBits);
  KJ_ASSERT(outboundBits == 10);
  // Valid config successfully constructed.

  const auto correctStructureButInvalid = "client_no_context_takeover; client_max_window_bits; "
                                          "server_max_window_bits=this_should_be_a_number"_kj;
  parts = _::splitParts(correctStructureButInvalid, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());

  maybeUnverified = _::populateUnverifiedConfig(keysMaybeValues);
  unverified = KJ_ASSERT_NONNULL(maybeUnverified);
  maybeValid = _::validateCompressionConfig(kj::mv(unverified), false); // Validate as Server.
  KJ_ASSERT(maybeValid == nullptr);
  // The config "looks" correct, but the `server_max_window_bits` parameter has an invalid value.

  const auto invalidRange = "client_max_window_bits; server_max_window_bits=18;"_kj;
  // `server_max_window_bits` is out of range, decline.
  parts = _::splitParts(invalidRange, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  maybeUnverified = _::populateUnverifiedConfig(keysMaybeValues);
  maybeValid = _::validateCompressionConfig(kj::mv(KJ_REQUIRE_NONNULL(maybeUnverified)), false);
  KJ_ASSERT(maybeValid == nullptr);

  const auto invalidRangeTwo = "client_max_window_bits=4"_kj;
  // `server_max_window_bits` is out of range, decline.
  parts = _::splitParts(invalidRangeTwo, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  maybeUnverified = _::populateUnverifiedConfig(keysMaybeValues);
  maybeValid = _::validateCompressionConfig(kj::mv(KJ_REQUIRE_NONNULL(maybeUnverified)), false);
  KJ_ASSERT(maybeValid == nullptr);

  const auto invalidRequest = "server_max_window_bits"_kj;
  // `sever_max_window_bits` must have a value in a request AND a response.
  parts = _::splitParts(invalidRequest, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  maybeUnverified = _::populateUnverifiedConfig(keysMaybeValues);
  maybeValid = _::validateCompressionConfig(kj::mv(KJ_REQUIRE_NONNULL(maybeUnverified)), false);
  KJ_ASSERT(maybeValid == nullptr);

  const auto invalidResponse = "client_max_window_bits"_kj;
  // `client_max_window_bits` must have a value in a response.
  parts = _::splitParts(invalidResponse, ';');
  keysMaybeValues = _::toKeysAndVals(parts.asPtr());
  maybeUnverified = _::populateUnverifiedConfig(keysMaybeValues);
  maybeValid = _::validateCompressionConfig(kj::mv(KJ_REQUIRE_NONNULL(maybeUnverified)), true);
  KJ_ASSERT(maybeValid == nullptr);
}

KJ_TEST("WebSocket Compression String Parsing (findValidExtensionOffers)") {
  // Test that we can extract only the valid extensions from a string of offers.
  constexpr auto extensions = "permessage-deflate; " // Valid offer.
                                "client_no_context_takeover; "
                                "client_max_window_bits; "
                                "server_max_window_bits=10, "
                              "permessage-deflate; " // Another valid offer.
                                "client_no_context_takeover; "
                                "client_max_window_bits, "
                              "permessage-invalid; " // Invalid ext name.
                                "client_no_context_takeover, "
                              "permessage-deflate; " // Invalid parmeter.
                                "invalid_parameter; "
                                "client_max_window_bits; "
                                "server_max_window_bits=10, "
                              "permessage-deflate; " // Invalid parmeter value.
                                "server_max_window_bits=should_be_a_number, "
                              "permessage-deflate; " // Unexpected parmeter value.
                                "client_max_window_bits=true, "
                              "permessage-deflate; " // Missing expected parmeter value.
                                "server_max_window_bits, "
                              "permessage-deflate; " // Invalid parameter value (too high).
                                "client_max_window_bits=99, "
                              "permessage-deflate; " // Invalid parameter value (too low).
                                "client_max_window_bits=4, "
                              "permessage-deflate; " // Invalid parameter (repeated).
                                "client_max_window_bits; "
                                "client_max_window_bits, "
                              "permessage-deflate"_kj; // Valid offer (no parameters).

  auto validOffers = _::findValidExtensionOffers(extensions);
  KJ_ASSERT(validOffers.size() == 3);
  KJ_ASSERT(validOffers[0].outboundNoContextTakeover == true);
  KJ_ASSERT(validOffers[0].inboundNoContextTakeover == false);
  KJ_ASSERT(validOffers[0].outboundMaxWindowBits == 15);
  KJ_ASSERT(validOffers[0].inboundMaxWindowBits == 10);

  KJ_ASSERT(validOffers[1].outboundNoContextTakeover == true);
  KJ_ASSERT(validOffers[1].inboundNoContextTakeover == false);
  KJ_ASSERT(validOffers[1].outboundMaxWindowBits == 15);
  KJ_ASSERT(validOffers[1].inboundMaxWindowBits == nullptr);

  KJ_ASSERT(validOffers[2].outboundNoContextTakeover == false);
  KJ_ASSERT(validOffers[2].inboundNoContextTakeover == false);
  KJ_ASSERT(validOffers[2].outboundMaxWindowBits == nullptr);
  KJ_ASSERT(validOffers[2].inboundMaxWindowBits == nullptr);
}

KJ_TEST("WebSocket Compression String Parsing (generateExtensionRequest)") {
  // Test that we can extract only the valid extensions from a string of offers.
  constexpr auto extensions = "permessage-deflate; "
                                "client_no_context_takeover; "
                                "server_max_window_bits=10; "
                                "client_max_window_bits, "
                              "permessage-deflate; "
                                "client_no_context_takeover; "
                                "client_max_window_bits, "
                              "permessage-deflate"_kj;
  constexpr auto EXPECTED = "permessage-deflate; "
                              "client_no_context_takeover; "
                              "client_max_window_bits=15; "
                              "server_max_window_bits=10, "
                            "permessage-deflate; "
                              "client_no_context_takeover; "
                              "client_max_window_bits=15, "
                            "permessage-deflate"_kj;
  auto validOffers = _::findValidExtensionOffers(extensions);
  auto extensionRequest = _::generateExtensionRequest(validOffers);
  KJ_ASSERT(extensionRequest == EXPECTED);
}

KJ_TEST("WebSocket Compression String Parsing (tryParseExtensionOffers)") {
  // Test that we can accept a valid offer from string of offers.
  constexpr auto extensions = "permessage-invalid; " // Invalid ext name.
                                "client_no_context_takeover, "
                              "permessage-deflate; " // Invalid parmeter.
                                "invalid_parameter; "
                                "client_max_window_bits; "
                                "server_max_window_bits=10, "
                              "permessage-deflate; " // Invalid parmeter value.
                                "server_max_window_bits=should_be_a_number, "
                              "permessage-deflate; " // Unexpected parmeter value.
                                "client_max_window_bits=true, "
                              "permessage-deflate; " // Missing expected parmeter value.
                                "server_max_window_bits, "
                              "permessage-deflate; " // Invalid parameter value (too high).
                                "client_max_window_bits=99, "
                              "permessage-deflate; " // Invalid parameter value (too low).
                                "client_max_window_bits=4, "
                              "permessage-deflate; " // Invalid parameter (repeated).
                                "client_max_window_bits; "
                                "client_max_window_bits, "
                              "permessage-deflate; " // Valid offer.
                                "client_no_context_takeover; "
                                "client_max_window_bits; "
                                "server_max_window_bits=10, "
                              "permessage-deflate; " // Another valid offer.
                                "client_no_context_takeover; "
                                "client_max_window_bits, "
                              "permessage-deflate"_kj; // Valid offer (no parameters).

  auto maybeAccepted = _::tryParseExtensionOffers(extensions);
  auto accepted = KJ_ASSERT_NONNULL(maybeAccepted);
  KJ_ASSERT(accepted.outboundNoContextTakeover == false);
  KJ_ASSERT(accepted.inboundNoContextTakeover == true);
  KJ_ASSERT(accepted.outboundMaxWindowBits == 10);
  KJ_ASSERT(accepted.inboundMaxWindowBits == 15);

  // Try the second valid offer from the big list above.
  auto offerTwo = "permessage-deflate; client_no_context_takeover; client_max_window_bits"_kj;
  maybeAccepted = _::tryParseExtensionOffers(offerTwo);
  accepted = KJ_ASSERT_NONNULL(maybeAccepted);
  KJ_ASSERT(accepted.outboundNoContextTakeover == false);
  KJ_ASSERT(accepted.inboundNoContextTakeover == true);
  KJ_ASSERT(accepted.outboundMaxWindowBits == nullptr);
  KJ_ASSERT(accepted.inboundMaxWindowBits == 15);

  auto offerThree = "permessage-deflate"_kj; // The third valid offer.
  maybeAccepted = _::tryParseExtensionOffers(offerThree);
  accepted = KJ_ASSERT_NONNULL(maybeAccepted);
  KJ_ASSERT(accepted.outboundNoContextTakeover == false);
  KJ_ASSERT(accepted.inboundNoContextTakeover == false);
  KJ_ASSERT(accepted.outboundMaxWindowBits == nullptr);
  KJ_ASSERT(accepted.inboundMaxWindowBits == nullptr);

  auto invalid = "invalid"_kj; // Any of the invalid offers we saw above would return NULL.
  maybeAccepted = _::tryParseExtensionOffers(invalid);
  KJ_ASSERT(maybeAccepted == nullptr);
}

KJ_TEST("WebSocket Compression String Parsing (tryParseAllExtensionOffers)") {
  // We want to test the following:
  //  1. We reject all if we don't find an offer we can accept.
  //  2. We accept one after iterating over offers that we have to reject.
  //  3. We accept an offer with a `server_max_window_bits` parameter if the manual config allows
  //     it, and choose the smaller "number of bits" (from clients request).
  //  4. We accept an offer with a `server_no_context_takeover` parameter if the manual config
  //     allows it, and choose the smaller "number of bits" (from manual config) from
  //     `server_max_window_bits`.
  constexpr auto serverOnly = "permessage-deflate; "
                                "client_no_context_takeover; "
                                "server_max_window_bits = 14; "
                                "server_no_context_takeover, "
                              "permessage-deflate; "
                                "client_no_context_takeover; "
                                "server_no_context_takeover, "
                              "permessage-deflate; "
                                "client_no_context_takeover; "
                                "server_max_window_bits = 14"_kj;

  constexpr auto acceptLast = "permessage-deflate; "
                                "client_no_context_takeover; "
                                "server_max_window_bits = 14; "
                                "server_no_context_takeover, "
                              "permessage-deflate; "
                                "client_no_context_takeover; "
                                "server_no_context_takeover, "
                              "permessage-deflate; "
                                "client_no_context_takeover; "
                                "server_max_window_bits = 14, "
                              "permessage-deflate; " // accept this
                                "client_no_context_takeover"_kj;

  const auto defaultConfig = CompressionParameters();
  // Our default config is equivalent to `permessage-deflate` with no parameters.

  auto maybeAccepted = _::tryParseAllExtensionOffers(serverOnly, defaultConfig);
  KJ_ASSERT(maybeAccepted == nullptr);
  // Asserts that we rejected all the offers with `server_x` parameters.

  maybeAccepted = _::tryParseAllExtensionOffers(acceptLast, defaultConfig);
  auto accepted = KJ_ASSERT_NONNULL(maybeAccepted);
  KJ_ASSERT(accepted.outboundNoContextTakeover == false);
  KJ_ASSERT(accepted.inboundNoContextTakeover == false);
  KJ_ASSERT(accepted.outboundMaxWindowBits == nullptr);
  KJ_ASSERT(accepted.inboundMaxWindowBits == nullptr);
  // Asserts that we accepted the only offer that did not have a `server_x` parameter.

  const auto allowServerBits = CompressionParameters {
      false,
      false,
      15, // server_max_window_bits = 15
      nullptr
  };
  maybeAccepted = _::tryParseAllExtensionOffers(serverOnly, allowServerBits);
  accepted = KJ_ASSERT_NONNULL(maybeAccepted);
  KJ_ASSERT(accepted.outboundNoContextTakeover == false);
  KJ_ASSERT(accepted.inboundNoContextTakeover == false);
  KJ_ASSERT(accepted.outboundMaxWindowBits == 14); // Note that we chose the lower of (14, 15).
  KJ_ASSERT(accepted.inboundMaxWindowBits == nullptr);
  // Asserts that we accepted an offer that allowed for `server_max_window_bits` AND we chose the
  // lower number of bits (in this case, the clients offer of 14).

  const auto allowServerTakeoverAndBits = CompressionParameters {
      true, // server_no_context_takeover = true
      false,
      13, // server_max_window_bits = 13
      nullptr
  };

  maybeAccepted = _::tryParseAllExtensionOffers(serverOnly, allowServerTakeoverAndBits);
  accepted = KJ_ASSERT_NONNULL(maybeAccepted);
  KJ_ASSERT(accepted.outboundNoContextTakeover == true);
  KJ_ASSERT(accepted.inboundNoContextTakeover == false);
  KJ_ASSERT(accepted.outboundMaxWindowBits == 13); // Note that we chose the lower of (14, 15).
  KJ_ASSERT(accepted.inboundMaxWindowBits == nullptr);
  // Asserts that we accepted an offer that allowed for `server_no_context_takeover` AND we chose
  // the lower number of bits (in this case, the manual config's choice of 13).
}

KJ_TEST("WebSocket Compression String Parsing (generateExtensionResponse)") {
  // Test that we can extract only the valid extensions from a string of offers.
  constexpr auto extensions = "permessage-deflate; "
                                "client_no_context_takeover; "
                                "server_max_window_bits=10; "
                                "client_max_window_bits, "
                              "permessage-deflate; "
                                "client_no_context_takeover; "
                                "client_max_window_bits, "
                              "permessage-deflate"_kj;
  constexpr auto EXPECTED = "permessage-deflate; "
                              "client_no_context_takeover; "
                              "client_max_window_bits=15; "
                              "server_max_window_bits=10"_kj;
  auto accepted = _::tryParseExtensionOffers(extensions);
  auto extensionResponse = _::generateExtensionResponse(KJ_ASSERT_NONNULL(accepted));
  KJ_ASSERT(extensionResponse == EXPECTED);
}

KJ_TEST("WebSocket Compression String Parsing (tryParseExtensionAgreement)") {
  constexpr auto didNotOffer = "Server failed WebSocket handshake: "
      "added Sec-WebSocket-Extensions when client did not offer any."_kj;
  constexpr auto tooMany = "Server failed WebSocket handshake: "
      "expected exactly one extension (permessage-deflate) but received more than one."_kj;
  constexpr auto badExt = "Server failed WebSocket handshake: "
      "response included a Sec-WebSocket-Extensions value that was not permessage-deflate."_kj;
  constexpr auto badVal = "Server failed WebSocket handshake: "
      "the Sec-WebSocket-Extensions header in the Response included an invalid value."_kj;

  constexpr auto tooManyExtensions = "permessage-deflate; client_no_context_takeover; "
      "client_max_window_bits; server_max_window_bits=10, "
      "permessage-deflate; client_no_context_takeover; client_max_window_bits;"_kj;

  auto maybeAccepted = _::tryParseExtensionAgreement(nullptr, tooManyExtensions);
  KJ_ASSERT(
      KJ_ASSERT_NONNULL(maybeAccepted.tryGet<kj::Exception>()).getDescription() == didNotOffer);

  Maybe<CompressionParameters> defaultConfig = CompressionParameters{};
  maybeAccepted = _::tryParseExtensionAgreement(defaultConfig, tooManyExtensions);
  KJ_ASSERT(KJ_ASSERT_NONNULL(maybeAccepted.tryGet<kj::Exception>()).getDescription() == tooMany);

  constexpr auto invalidExt = "permessage-invalid; "
                                "client_no_context_takeover; "
                                "client_max_window_bits; "
                                "server_max_window_bits=10;";
  maybeAccepted = _::tryParseExtensionAgreement(defaultConfig, invalidExt);
  KJ_ASSERT(KJ_ASSERT_NONNULL(maybeAccepted.tryGet<kj::Exception>()).getDescription() == badExt);

  constexpr auto invalidVal = "permessage-deflate; "
                                "client_no_context_takeover; "
                                "client_max_window_bits; "
                                "server_max_window_bits=100;";
  maybeAccepted = _::tryParseExtensionAgreement(defaultConfig, invalidVal);
  KJ_ASSERT(KJ_ASSERT_NONNULL(maybeAccepted.tryGet<kj::Exception>()).getDescription() == badVal);

  constexpr auto missingVal = "permessage-deflate; "
                                "client_no_context_takeover; "
                                "client_max_window_bits; " // This must have a value in a Response!
                                "server_max_window_bits=10;";
  maybeAccepted = _::tryParseExtensionAgreement(defaultConfig, missingVal);
  KJ_ASSERT(KJ_ASSERT_NONNULL(maybeAccepted.tryGet<kj::Exception>()).getDescription() == badVal);

  constexpr auto valid = "permessage-deflate; client_no_context_takeover; "
      "client_max_window_bits=15; server_max_window_bits=10"_kj;
  maybeAccepted = _::tryParseExtensionAgreement(defaultConfig, valid);
  auto config = KJ_ASSERT_NONNULL(maybeAccepted.tryGet<CompressionParameters>());
  KJ_ASSERT(config.outboundNoContextTakeover == true);
  KJ_ASSERT(config.inboundNoContextTakeover == false);
  KJ_ASSERT(config.outboundMaxWindowBits == 15);
  KJ_ASSERT(config.inboundMaxWindowBits == 10);

  auto client = CompressionParameters{ true, false, 15, 10 };
  // If the server ignores our `client_no_context_takeover` parameter, we (the client) still use it.
  constexpr auto serverIgnores = "permessage-deflate; client_max_window_bits=15; "
    "server_max_window_bits=10"_kj;
  maybeAccepted = _::tryParseExtensionAgreement(client, serverIgnores);
  config = KJ_ASSERT_NONNULL(maybeAccepted.tryGet<CompressionParameters>());
  KJ_ASSERT(config.outboundNoContextTakeover == true); // Note that this is missing in the response.
  KJ_ASSERT(config.inboundNoContextTakeover == false);
  KJ_ASSERT(config.outboundMaxWindowBits == 15);
  KJ_ASSERT(config.inboundMaxWindowBits == 10);
}

#if KJ_HAS_ZLIB
KJ_TEST("HttpClient WebSocket Empty Message Compression") {
  // We'll try to send and receive "Hello", then "", followed by "Hello" again.
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto request = kj::str("GET /websocket", WEBSOCKET_COMPRESSION_HANDSHAKE);

  auto serverTask = expectRead(*pipe.ends[1], request)
      .then([&]() { return writeA(*pipe.ends[1], asBytes(WEBSOCKET_COMPRESSION_RESPONSE_HANDSHAKE)); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_EMPTY_COMPRESSED_MESSAGE); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_EMPTY_SEND_COMPRESSED_MESSAGE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE_REUSE_CTX); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId extHeader = tableBuilder.add("Sec-WebSocket-Extensions");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.webSocketCompressionMode = HttpClientSettings::MANUAL_COMPRESSION;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  constexpr auto extensions = "permessage-deflate; server_no_context_takeover"_kj;
  testWebSocketEmptyMessageCompression(waitScope, *headerTable, extHeader, extensions, *client);

  serverTask.wait(waitScope);
}
#endif // KJ_HAS_ZLIB

#if KJ_HAS_ZLIB
KJ_TEST("HttpClient WebSocket Default Compression") {
  // We'll try to send and receive "Hello" twice. The second time we receive "Hello", the compressed
  // message will be smaller as a result of the client reusing the lookback window.
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto request = kj::str("GET /websocket", WEBSOCKET_COMPRESSION_HANDSHAKE);

  auto serverTask = expectRead(*pipe.ends[1], request)
      .then([&]() { return writeA(*pipe.ends[1], asBytes(WEBSOCKET_COMPRESSION_RESPONSE_HANDSHAKE)); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE_REUSE_CTX); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId extHeader = tableBuilder.add("Sec-WebSocket-Extensions");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.webSocketCompressionMode = HttpClientSettings::MANUAL_COMPRESSION;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  constexpr auto extensions = "permessage-deflate; server_no_context_takeover"_kj;
  testWebSocketTwoMessageCompression(waitScope, *headerTable, extHeader, extensions, *client);

  serverTask.wait(waitScope);
}
#endif // KJ_HAS_ZLIB

#if KJ_HAS_ZLIB
KJ_TEST("HttpClient WebSocket Extract Extensions") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto request = kj::str("GET /websocket", WEBSOCKET_COMPRESSION_HANDSHAKE);

  auto serverTask = expectRead(*pipe.ends[1], request)
      .then([&]() { return writeA(*pipe.ends[1], asBytes(WEBSOCKET_COMPRESSION_RESPONSE_HANDSHAKE)); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId extHeader = tableBuilder.add("Sec-WebSocket-Extensions");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.webSocketCompressionMode = HttpClientSettings::MANUAL_COMPRESSION;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  constexpr auto extensions = "permessage-deflate; server_no_context_takeover"_kj;
  testWebSocketOptimizePumpProxy(waitScope, *headerTable, extHeader, extensions, *client);

  serverTask.wait(waitScope);
}
#endif // KJ_HAS_ZLIB

#if KJ_HAS_ZLIB
KJ_TEST("HttpClient WebSocket Compression (Client Discards Compression Context)") {
  // We'll try to send and receive "Hello" twice. The second time we receive "Hello", the compressed
  // message will be the same size as the first time, since the client discards the lookback window.
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto request = kj::str("GET /websocket", WEBSOCKET_COMPRESSION_CLIENT_DISCARDS_CTX_HANDSHAKE);

  auto serverTask = expectRead(*pipe.ends[1], request)
      .then([&]() { return writeA(*pipe.ends[1],
          asBytes(WEBSOCKET_COMPRESSION_CLIENT_DISCARDS_CTX_RESPONSE_HANDSHAKE)); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_FIRST_COMPRESSED_MESSAGE); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId extHeader = tableBuilder.add("Sec-WebSocket-Extensions");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.webSocketCompressionMode = HttpClientSettings::MANUAL_COMPRESSION;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  constexpr auto extensions =
      "permessage-deflate; client_no_context_takeover; server_no_context_takeover"_kj;
  testWebSocketTwoMessageCompression(waitScope, *headerTable, extHeader, extensions, *client);

  serverTask.wait(waitScope);
}
#endif // KJ_HAS_ZLIB

#if KJ_HAS_ZLIB
KJ_TEST("HttpClient WebSocket Compression (Different DEFLATE blocks)") {
  // In this test, we'll try to use the following DEFLATE blocks:
  //  - Two DEFLATE blocks in 1 message.
  //  - A block with no compression.
  //  - A block with BFINAL set to 1.
  // Then, we'll try to send a normal compressed message following the BFINAL message to ensure we
  // can still process messages after receiving BFINAL.
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto request = kj::str("GET /websocket", WEBSOCKET_COMPRESSION_CLIENT_DISCARDS_CTX_HANDSHAKE);

  auto serverTask = expectRead(*pipe.ends[1], request)
      .then([&]() { return writeA(*pipe.ends[1],
          asBytes(WEBSOCKET_COMPRESSION_CLIENT_DISCARDS_CTX_RESPONSE_HANDSHAKE)); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_TWO_DEFLATE_BLOCKS_MESSAGE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_DEFLATE_NO_COMPRESSION_MESSAGE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_BFINAL_SET_MESSAGE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_SEND_COMPRESSED_MESSAGE); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
      .then([&]() { return writeA(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId extHeader = tableBuilder.add("Sec-WebSocket-Extensions");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.webSocketCompressionMode = HttpClientSettings::MANUAL_COMPRESSION;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  constexpr auto extensions =
      "permessage-deflate; client_no_context_takeover; server_no_context_takeover"_kj;
  testWebSocketFourMessageCompression(waitScope, *headerTable, extHeader, extensions, *client);

  serverTask.wait(waitScope);
}
#endif // KJ_HAS_ZLIB

KJ_TEST("HttpClient WebSocket error") {
  KJ_HTTP_TEST_SETUP_IO;
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto request = kj::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);

  auto serverTask = expectRead(*pipe.ends[1], request)
      .then([&]() { return writeA(*pipe.ends[1], asBytes(WEBSOCKET_RESPONSE_HANDSHAKE_ERROR)); })
      .then([&]() { return expectRead(*pipe.ends[1], request); })
      .then([&]() { return writeA(*pipe.ends[1], asBytes(WEBSOCKET_RESPONSE_HANDSHAKE_ERROR)); })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], clientSettings);

  kj::HttpHeaders headers(*headerTable);
  headers.set(hMyHeader, "foo");

  {
    auto response = client->openWebSocket("/websocket", headers).wait(waitScope);

    KJ_EXPECT(response.statusCode == 404);
    KJ_EXPECT(response.statusText == "Not Found", response.statusText);
    KJ_EXPECT(KJ_ASSERT_NONNULL(response.headers->get(hMyHeader)) == "respond-foo");
    KJ_ASSERT(response.webSocketOrBody.is<kj::Own<AsyncInputStream>>());
  }

  {
    auto response = client->openWebSocket("/websocket", headers).wait(waitScope);

    KJ_EXPECT(response.statusCode == 404);
    KJ_EXPECT(response.statusText == "Not Found", response.statusText);
    KJ_EXPECT(KJ_ASSERT_NONNULL(response.headers->get(hMyHeader)) == "respond-foo");
    KJ_ASSERT(response.webSocketOrBody.is<kj::Own<AsyncInputStream>>());
  }

  serverTask.wait(waitScope);
}

KJ_TEST("HttpServer WebSocket handshake") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();
  TestWebSocketService service(*headerTable, hMyHeader);
  HttpServer server(timer, *headerTable, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto request = kj::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);
  writeA(*pipe.ends[1], request.asBytes()).wait(waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_RESPONSE_HANDSHAKE).wait(waitScope);

  expectRead(*pipe.ends[1], WEBSOCKET_FIRST_MESSAGE_INLINE).wait(waitScope);
  writeA(*pipe.ends[1], WEBSOCKET_SEND_MESSAGE).wait(waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_REPLY_MESSAGE).wait(waitScope);
  writeA(*pipe.ends[1], WEBSOCKET_SEND_CLOSE).wait(waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE).wait(waitScope);

  listenTask.wait(waitScope);
}

KJ_TEST("HttpServer WebSocket handshake error") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();
  TestWebSocketService service(*headerTable, hMyHeader);
  HttpServer server(timer, *headerTable, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto request = kj::str("GET /return-error", WEBSOCKET_REQUEST_HANDSHAKE);
  writeA(*pipe.ends[1], request.asBytes()).wait(waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_RESPONSE_HANDSHAKE_ERROR).wait(waitScope);

  // Can send more requests!
  writeA(*pipe.ends[1], request.asBytes()).wait(waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_RESPONSE_HANDSHAKE_ERROR).wait(waitScope);

  pipe.ends[1]->shutdownWrite();

  listenTask.wait(waitScope);
}

void testBadWebSocketHandshake(
    WaitScope& waitScope, Timer& timer, StringPtr request, StringPtr response, TwoWayPipe pipe) {
  // Write an invalid WebSocket GET request, and expect a particular error response.

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();
  TestWebSocketService service(*headerTable, hMyHeader);

  class ErrorHandler final: public HttpServerErrorHandler {
    Promise<void> handleApplicationError(
        Exception exception, Maybe<HttpService::Response&> response) override {
      // When I first wrote this, I expected this function to be called, because
      // `TestWebSocketService::request()` definitely throws. However, the exception it throws comes
      // from `HttpService::Response::acceptWebSocket()`, which stores the fact which it threw a
      // WebSocket error. This prevents the HttpServer's listen loop from propagating the exception
      // to our HttpServerErrorHandler (i.e., this function), because it assumes the exception is
      // related to the WebSocket error response. See `HttpServer::Connection::startLoop()` for
      // details.
      bool responseWasSent = response == nullptr;
      KJ_FAIL_EXPECT("Unexpected application error", responseWasSent, exception);
      return READY_NOW;
    }
  };

  ErrorHandler errorHandler;

  HttpServerSettings serverSettings;
  serverSettings.errorHandler = errorHandler;

  HttpServer server(timer, *headerTable, service, serverSettings);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  pipe.ends[1]->write(request.begin(), request.size()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1], response).wait(waitScope);

  listenTask.wait(waitScope);
}

KJ_TEST("HttpServer WebSocket handshake with unsupported Sec-WebSocket-Version") {
  static constexpr auto REQUEST =
    "GET /websocket HTTP/1.1\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Key: DCI4TgwiOE4MIjhODCI4Tg==\r\n"
    "Sec-WebSocket-Version: 1\r\n"
    "My-Header: foo\r\n"
    "\r\n"_kj;

  static constexpr auto RESPONSE =
    "HTTP/1.1 400 Bad Request\r\n"
    "Connection: close\r\n"
    "Content-Length: 56\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "ERROR: The requested WebSocket version is not supported."_kj;

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  testBadWebSocketHandshake(waitScope, timer, REQUEST, RESPONSE, KJ_HTTP_TEST_CREATE_2PIPE);
}

KJ_TEST("HttpServer WebSocket handshake with missing Sec-WebSocket-Key") {
  static constexpr auto REQUEST =
    "GET /websocket HTTP/1.1\r\n"
    "Connection: Upgrade\r\n"
    "Upgrade: websocket\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "My-Header: foo\r\n"
    "\r\n"_kj;

  static constexpr auto RESPONSE =
    "HTTP/1.1 400 Bad Request\r\n"
    "Connection: close\r\n"
    "Content-Length: 32\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "ERROR: Missing Sec-WebSocket-Key"_kj;

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  testBadWebSocketHandshake(waitScope, timer, REQUEST, RESPONSE, KJ_HTTP_TEST_CREATE_2PIPE);
}

KJ_TEST("HttpServer WebSocket with application error after accept") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  class WebSocketApplicationErrorService: public HttpService, public HttpServerErrorHandler {
    // Accepts a WebSocket, receives a message, and throws an exception (application error).

  public:
    Promise<void> request(
        HttpMethod method, kj::StringPtr, const HttpHeaders&,
        AsyncInputStream&, Response& response) override {
      KJ_ASSERT(method == HttpMethod::GET);
      HttpHeaderTable headerTable;
      HttpHeaders responseHeaders(headerTable);
      auto webSocket = response.acceptWebSocket(responseHeaders);
      return webSocket->receive().then([](WebSocket::Message) {
        throwRecoverableException(KJ_EXCEPTION(FAILED, "test exception"));
      }).attach(kj::mv(webSocket));
    }

    Promise<void> handleApplicationError(Exception exception, Maybe<Response&> response) override {
      // We accepted the WebSocket, so the response was already sent. At one time, we _did_ expose a
      // useless Response reference here, so this is a regression test.
      bool responseWasSent = response == nullptr;
      KJ_EXPECT(responseWasSent);
      KJ_EXPECT(exception.getDescription() == "test exception"_kj);
      return READY_NOW;
    }
  };

  // Set up the HTTP service.

  WebSocketApplicationErrorService service;

  HttpServerSettings serverSettings;
  serverSettings.errorHandler = service;

  HttpHeaderTable headerTable;
  HttpServer server(timer, headerTable, service, serverSettings);

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Make a client and open a WebSocket to the service.

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  auto client = newHttpClient(
      headerTable, *pipe.ends[1], clientSettings);

  HttpHeaders headers(headerTable);
  auto webSocketResponse = client->openWebSocket("/websocket"_kj, headers)
      .wait(waitScope);

  KJ_ASSERT(webSocketResponse.statusCode == 101);
  auto webSocket = kj::mv(KJ_ASSERT_NONNULL(webSocketResponse.webSocketOrBody.tryGet<Own<WebSocket>>()));

  webSocket->send("ignored"_kj).wait(waitScope);

  listenTask.wait(waitScope);
}

// -----------------------------------------------------------------------------

KJ_TEST("HttpServer request timeout") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServerSettings settings;
  settings.headerTimeout = 1 * kj::MILLISECONDS;
  HttpServer server(timer, table, service, settings);

  // Shouldn't hang! Should time out.
  auto promise = server.listenHttp(kj::mv(pipe.ends[0]));
  KJ_EXPECT(!promise.poll(waitScope));
  timer.advanceTo(timer.now() + settings.headerTimeout / 2);
  KJ_EXPECT(!promise.poll(waitScope));
  timer.advanceTo(timer.now() + settings.headerTimeout);
  promise.wait(waitScope);

  // Closes the connection without sending anything.
  KJ_EXPECT(pipe.ends[1]->readAllText().wait(waitScope) == "");
}

KJ_TEST("HttpServer pipeline timeout") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServerSettings settings;
  settings.pipelineTimeout = 1 * kj::MILLISECONDS;
  HttpServer server(timer, table, service, settings);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(waitScope);
  expectRead(*pipe.ends[1], PIPELINE_TESTS[0].response.raw).wait(waitScope);

  // Listen task should time out even though we didn't shutdown the socket.
  KJ_EXPECT(!listenTask.poll(waitScope));
  timer.advanceTo(timer.now() + settings.pipelineTimeout / 2);
  KJ_EXPECT(!listenTask.poll(waitScope));
  timer.advanceTo(timer.now() + settings.pipelineTimeout);
  listenTask.wait(waitScope);

  // In this case, no data is sent back.
  KJ_EXPECT(pipe.ends[1]->readAllText().wait(waitScope) == "");
}

class BrokenHttpService final: public HttpService {
  // HttpService that doesn't send a response.
public:
  BrokenHttpService() = default;
  explicit BrokenHttpService(kj::Exception&& exception): exception(kj::mv(exception)) {}

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& responseSender) override {
    return requestBody.readAllBytes().then([this](kj::Array<byte>&&) -> kj::Promise<void> {
      KJ_IF_MAYBE(e, exception) {
        return kj::cp(*e);
      } else {
        return kj::READY_NOW;
      }
    });
  }

private:
  kj::Maybe<kj::Exception> exception;
};

KJ_TEST("HttpServer no response") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  BrokenHttpService service;
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  KJ_EXPECT(text ==
      "HTTP/1.1 500 Internal Server Error\r\n"
      "Connection: close\r\n"
      "Content-Length: 51\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "ERROR: The HttpService did not generate a response.", text);
}

KJ_TEST("HttpServer disconnected") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  BrokenHttpService service(KJ_EXCEPTION(DISCONNECTED, "disconnected"));
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  KJ_EXPECT(text == "", text);
}

KJ_TEST("HttpServer overloaded") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  BrokenHttpService service(KJ_EXCEPTION(OVERLOADED, "overloaded"));
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  KJ_EXPECT(text.startsWith("HTTP/1.1 503 Service Unavailable"), text);
}

KJ_TEST("HttpServer unimplemented") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  BrokenHttpService service(KJ_EXCEPTION(UNIMPLEMENTED, "unimplemented"));
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  KJ_EXPECT(text.startsWith("HTTP/1.1 501 Not Implemented"), text);
}

KJ_TEST("HttpServer threw exception") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  BrokenHttpService service(KJ_EXCEPTION(FAILED, "failed"));
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  KJ_EXPECT(text.startsWith("HTTP/1.1 500 Internal Server Error"), text);
}

KJ_TEST("HttpServer bad request") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  BrokenHttpService service;
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  static constexpr auto request = "GET / HTTP/1.1\r\nbad request\r\n\r\n"_kj;
  auto writePromise = pipe.ends[1]->write(request.begin(), request.size());
  auto response = pipe.ends[1]->readAllText().wait(waitScope);
  KJ_EXPECT(writePromise.poll(waitScope));
  writePromise.wait(waitScope);

  static constexpr auto expectedResponse =
      "HTTP/1.1 400 Bad Request\r\n"
      "Connection: close\r\n"
      "Content-Length: 53\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "ERROR: The headers sent by your client are not valid."_kj;

  KJ_EXPECT(expectedResponse == response, expectedResponse, response);
}

KJ_TEST("HttpServer invalid method") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  BrokenHttpService service;
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  static constexpr auto request = "bad request\r\n\r\n"_kj;
  auto writePromise = pipe.ends[1]->write(request.begin(), request.size());
  auto response = pipe.ends[1]->readAllText().wait(waitScope);
  KJ_EXPECT(writePromise.poll(waitScope));
  writePromise.wait(waitScope);

  static constexpr auto expectedResponse =
      "HTTP/1.1 501 Not Implemented\r\n"
      "Connection: close\r\n"
      "Content-Length: 35\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "ERROR: Unrecognized request method."_kj;

  KJ_EXPECT(expectedResponse == response, expectedResponse, response);
}

// Ensure that HttpServerSettings can continue to be constexpr.
KJ_UNUSED static constexpr HttpServerSettings STATIC_CONSTEXPR_SETTINGS {};

class TestErrorHandler: public HttpServerErrorHandler {
public:
  kj::Promise<void> handleClientProtocolError(
      HttpHeaders::ProtocolError protocolError, kj::HttpService::Response& response) override {
    // In a real error handler, you should redact `protocolError.rawContent`.
    auto message = kj::str("Saw protocol error: ", protocolError.description, "; rawContent = ",
        encodeCEscape(protocolError.rawContent));
    return sendError(400, "Bad Request", kj::mv(message), response);
  }

  kj::Promise<void> handleApplicationError(
      kj::Exception exception, kj::Maybe<kj::HttpService::Response&> response) override {
    return sendError(500, "Internal Server Error",
        kj::str("Saw application error: ", exception.getDescription()), response);
  }

  kj::Promise<void> handleNoResponse(kj::HttpService::Response& response) override {
    return sendError(500, "Internal Server Error", kj::str("Saw no response."), response);
  }

  static TestErrorHandler instance;

private:
  kj::Promise<void> sendError(uint statusCode, kj::StringPtr statusText, String message,
      Maybe<HttpService::Response&> response) {
    KJ_IF_MAYBE(r, response) {
      HttpHeaderTable headerTable;
      HttpHeaders headers(headerTable);
      auto body = r->send(statusCode, statusText, headers, message.size());
      return body->write(message.begin(), message.size()).attach(kj::mv(body), kj::mv(message));
    } else {
      KJ_LOG(ERROR, "Saw an error but too late to report to client.");
      return kj::READY_NOW;
    }
  }
};

TestErrorHandler TestErrorHandler::instance {};

KJ_TEST("HttpServer no response, custom error handler") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpServerSettings settings {};
  settings.errorHandler = TestErrorHandler::instance;

  HttpHeaderTable table;
  BrokenHttpService service;
  HttpServer server(timer, table, service, settings);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  KJ_EXPECT(text ==
      "HTTP/1.1 500 Internal Server Error\r\n"
      "Connection: close\r\n"
      "Content-Length: 16\r\n"
      "\r\n"
      "Saw no response.", text);
}

KJ_TEST("HttpServer threw exception, custom error handler") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpServerSettings settings {};
  settings.errorHandler = TestErrorHandler::instance;

  HttpHeaderTable table;
  BrokenHttpService service(KJ_EXCEPTION(FAILED, "failed"));
  HttpServer server(timer, table, service, settings);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  KJ_EXPECT(text ==
      "HTTP/1.1 500 Internal Server Error\r\n"
      "Connection: close\r\n"
      "Content-Length: 29\r\n"
      "\r\n"
      "Saw application error: failed", text);
}

KJ_TEST("HttpServer bad request, custom error handler") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpServerSettings settings {};
  settings.errorHandler = TestErrorHandler::instance;

  HttpHeaderTable table;
  BrokenHttpService service;
  HttpServer server(timer, table, service, settings);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  static constexpr auto request = "bad request\r\n\r\n"_kj;
  auto writePromise = pipe.ends[1]->write(request.begin(), request.size());
  auto response = pipe.ends[1]->readAllText().wait(waitScope);
  KJ_EXPECT(writePromise.poll(waitScope));
  writePromise.wait(waitScope);

  static constexpr auto expectedResponse =
      "HTTP/1.1 400 Bad Request\r\n"
      "Connection: close\r\n"
      "Content-Length: 80\r\n"
      "\r\n"
      "Saw protocol error: Unrecognized request method.; "
      "rawContent = bad request\\000\\n"_kj;

  KJ_EXPECT(expectedResponse == response, expectedResponse, response);
}

class PartialResponseService final: public HttpService {
  // HttpService that sends a partial response then throws.
public:
  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    return requestBody.readAllBytes()
        .then([this,&response](kj::Array<byte>&&) -> kj::Promise<void> {
      HttpHeaders headers(table);
      auto body = response.send(200, "OK", headers, 32);
      auto promise = body->write("foo", 3);
      return promise.attach(kj::mv(body)).then([]() -> kj::Promise<void> {
        return KJ_EXCEPTION(FAILED, "failed");
      });
    });
  }

private:
  kj::Maybe<kj::Exception> exception;
  HttpHeaderTable table;
};

KJ_TEST("HttpServer threw exception after starting response") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  PartialResponseService service;
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  KJ_EXPECT_LOG(ERROR, "HttpService threw exception after generating a partial response");

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  KJ_EXPECT(text ==
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 32\r\n"
      "\r\n"
      "foo", text);
}

class PartialResponseNoThrowService final: public HttpService {
  // HttpService that sends a partial response then returns without throwing.
public:
  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    return requestBody.readAllBytes()
        .then([this,&response](kj::Array<byte>&&) -> kj::Promise<void> {
      HttpHeaders headers(table);
      auto body = response.send(200, "OK", headers, 32);
      auto promise = body->write("foo", 3);
      return promise.attach(kj::mv(body));
    });
  }

private:
  kj::Maybe<kj::Exception> exception;
  HttpHeaderTable table;
};

KJ_TEST("HttpServer failed to write complete response but didn't throw") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  PartialResponseNoThrowService service;
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(waitScope);
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  KJ_EXPECT(text ==
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 32\r\n"
      "\r\n"
      "foo", text);
}

class SimpleInputStream final: public kj::AsyncInputStream {
  // An InputStream that returns bytes out of a static string.

public:
  SimpleInputStream(kj::StringPtr text)
      : unread(text.asBytes()) {}

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    size_t amount = kj::min(maxBytes, unread.size());
    memcpy(buffer, unread.begin(), amount);
    unread = unread.slice(amount, unread.size());
    return amount;
  }

private:
  kj::ArrayPtr<const byte> unread;
};

class PumpResponseService final: public HttpService {
  // HttpService that uses pumpTo() to write a response, without carefully specifying how much to
  // pump, but the stream happens to be the right size.
public:
  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    return requestBody.readAllBytes()
        .then([this,&response](kj::Array<byte>&&) -> kj::Promise<void> {
      HttpHeaders headers(table);
      kj::StringPtr text = "Hello, World!";
      auto body = response.send(200, "OK", headers, text.size());

      auto stream = kj::heap<SimpleInputStream>(text);
      auto promise = stream->pumpTo(*body);
      return promise.attach(kj::mv(body), kj::mv(stream))
          .then([text](uint64_t amount) {
        KJ_EXPECT(amount == text.size());
      });
    });
  }

private:
  kj::Maybe<kj::Exception> exception;
  HttpHeaderTable table;
};

KJ_TEST("HttpFixedLengthEntityWriter correctly implements tryPumpFrom") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  PumpResponseService service;
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(waitScope);
  pipe.ends[1]->shutdownWrite();
  auto text = pipe.ends[1]->readAllText().wait(waitScope);

  KJ_EXPECT(text ==
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 13\r\n"
      "\r\n"
      "Hello, World!", text);
}

class HangingHttpService final: public HttpService {
  // HttpService that hangs forever.
public:
  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& responseSender) override {
    kj::Promise<void> result = kj::NEVER_DONE;
    ++inFlight;
    return result.attach(kj::defer([this]() {
      if (--inFlight == 0) {
        KJ_IF_MAYBE(f, onCancelFulfiller) {
          f->get()->fulfill();
        }
      }
    }));
  }

  kj::Promise<void> onCancel() {
    auto paf = kj::newPromiseAndFulfiller<void>();
    onCancelFulfiller = kj::mv(paf.fulfiller);
    return kj::mv(paf.promise);
  }

  uint inFlight = 0;

private:
  kj::Maybe<kj::Exception> exception;
  kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> onCancelFulfiller;
};

KJ_TEST("HttpServer cancels request when client disconnects") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  HangingHttpService service;
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  KJ_EXPECT(service.inFlight == 0);

  static constexpr auto request = "GET / HTTP/1.1\r\n\r\n"_kj;
  pipe.ends[1]->write(request.begin(), request.size()).wait(waitScope);

  auto cancelPromise = service.onCancel();
  KJ_EXPECT(!cancelPromise.poll(waitScope));
  KJ_EXPECT(service.inFlight == 1);

  // Disconnect client and verify server cancels.
  pipe.ends[1] = nullptr;
  KJ_ASSERT(cancelPromise.poll(waitScope));
  KJ_EXPECT(service.inFlight == 0);
  cancelPromise.wait(waitScope);
}

class SuspendAfter: private HttpService {
  // A SuspendableHttpServiceFactory which responds to the first `n` requests with 200 OK, then
  // suspends all subsequent requests until its counter is reset.

public:
  void suspendAfter(uint countdownParam) { countdown = countdownParam; }

  kj::Maybe<kj::Own<HttpService>> operator()(HttpServer::SuspendableRequest& sr) {
    if (countdown == 0) {
      suspendedRequest = sr.suspend();
      return nullptr;
    }
    --countdown;
    return kj::Own<HttpService>(static_cast<HttpService*>(this), kj::NullDisposer::instance);
  }

  kj::Maybe<HttpServer::SuspendedRequest> getSuspended() {
    KJ_DEFER(suspendedRequest = nullptr);
    return kj::mv(suspendedRequest);
  }

private:
  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    HttpHeaders responseHeaders(table);
    response.send(200, "OK", responseHeaders);
    return requestBody.readAllBytes().ignoreResult();
  }

  HttpHeaderTable table;

  uint countdown = kj::maxValue;
  kj::Maybe<HttpServer::SuspendedRequest> suspendedRequest;
};

KJ_TEST("HttpServer can suspend a request") {
  // This test sends a single request to an HttpServer three times. First it writes the request to
  // its pipe and arranges for the HttpServer to suspend the request. Then it resumes the suspended
  // request and arranges for this resumption to be suspended as well. Then it resumes once more and
  // arranges for the request to be completed.

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  // This HttpService will not actually be used, because we're passing a factory in to
  // listenHttpCleanDrain().
  HangingHttpService service;
  HttpServer server(timer, table, service);

  kj::Maybe<HttpServer::SuspendedRequest> suspendedRequest;

  SuspendAfter factory;

  {
    // Observe the HttpServer suspend.

    factory.suspendAfter(0);
    auto listenPromise = server.listenHttpCleanDrain(*pipe.ends[0], factory);

    static constexpr kj::StringPtr REQUEST =
        "POST / HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "6\r\n"
        "foobar\r\n"
        "0\r\n"
        "\r\n"_kj;
    pipe.ends[1]->write(REQUEST.begin(), REQUEST.size()).wait(waitScope);

    // The listen promise is fulfilled with false.
    KJ_EXPECT(listenPromise.poll(waitScope));
    KJ_EXPECT(!listenPromise.wait(waitScope));

    // And we have a SuspendedRequest.
    suspendedRequest = factory.getSuspended();
    KJ_EXPECT(suspendedRequest != nullptr);
  }

  {
    // Observe the HttpServer suspend again without reading from the connection.

    factory.suspendAfter(0);
    auto listenPromise = server.listenHttpCleanDrain(
        *pipe.ends[0], factory, kj::mv(suspendedRequest));

    // The listen promise is again fulfilled with false.
    KJ_EXPECT(listenPromise.poll(waitScope));
    KJ_EXPECT(!listenPromise.wait(waitScope));

    // We again have a suspendedRequest.
    suspendedRequest = factory.getSuspended();
    KJ_EXPECT(suspendedRequest != nullptr);
  }

  {
    // The SuspendedRequest is completed.

    factory.suspendAfter(1);
    auto listenPromise = server.listenHttpCleanDrain(
        *pipe.ends[0], factory, kj::mv(suspendedRequest));

    auto drainPromise = kj::evalLast([&]() {
      return server.drain();
    });

    // We need to read the response for the HttpServer to drain.
    auto readPromise = pipe.ends[1]->readAllText();

    // This time, the server drained cleanly.
    KJ_EXPECT(listenPromise.poll(waitScope));
    KJ_EXPECT(listenPromise.wait(waitScope));

    drainPromise.wait(waitScope);

    // Close the server side of the pipe so our read promise completes.
    pipe.ends[0] = nullptr;

    auto response = readPromise.wait(waitScope);
    static constexpr kj::StringPtr RESPONSE =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "0\r\n"
        "\r\n"_kj;
    KJ_EXPECT(RESPONSE == response);
  }
}

KJ_TEST("HttpServer can suspend and resume pipelined requests") {
  // This test sends multiple requests with both Content-Length and Transfer-Encoding: chunked
  // bodies, and verifies that suspending both kinds does not corrupt the stream.

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  // This HttpService will not actually be used, because we're passing a factory in to
  // listenHttpCleanDrain().
  HangingHttpService service;
  HttpServer server(timer, table, service);

  // We'll suspend the second request.
  kj::Maybe<HttpServer::SuspendedRequest> suspendedRequest;
  SuspendAfter factory;

  static constexpr kj::StringPtr LENGTHFUL_REQUEST =
      "POST / HTTP/1.1\r\n"
      "Content-Length: 6\r\n"
      "\r\n"
      "foobar"_kj;
  static constexpr kj::StringPtr CHUNKED_REQUEST =
      "POST / HTTP/1.1\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "6\r\n"
      "foobar\r\n"
      "0\r\n"
      "\r\n"_kj;

  // Set up several requests; we'll suspend and transfer the second and third one.
  auto writePromise = pipe.ends[1]->write(LENGTHFUL_REQUEST.begin(), LENGTHFUL_REQUEST.size())
      .then([&]() {
    return pipe.ends[1]->write(CHUNKED_REQUEST.begin(), CHUNKED_REQUEST.size());
  }).then([&]() {
    return pipe.ends[1]->write(LENGTHFUL_REQUEST.begin(), LENGTHFUL_REQUEST.size());
  }).then([&]() {
    return pipe.ends[1]->write(CHUNKED_REQUEST.begin(), CHUNKED_REQUEST.size());
  });

  auto readPromise = pipe.ends[1]->readAllText();

  {
    // Observe the HttpServer suspend the second request.

    factory.suspendAfter(1);
    auto listenPromise = server.listenHttpCleanDrain(*pipe.ends[0], factory);

    KJ_EXPECT(listenPromise.poll(waitScope));
    KJ_EXPECT(!listenPromise.wait(waitScope));
    suspendedRequest = factory.getSuspended();
    KJ_EXPECT(suspendedRequest != nullptr);
  }

  {
    // Let's resume one request and suspend the next pipelined request.

    factory.suspendAfter(1);
    auto listenPromise = server.listenHttpCleanDrain(
        *pipe.ends[0], factory, kj::mv(suspendedRequest));

    KJ_EXPECT(listenPromise.poll(waitScope));
    KJ_EXPECT(!listenPromise.wait(waitScope));
    suspendedRequest = factory.getSuspended();
    KJ_EXPECT(suspendedRequest != nullptr);
  }

  {
    // Resume again and run to completion.

    factory.suspendAfter(kj::maxValue);
    auto listenPromise = server.listenHttpCleanDrain(
        *pipe.ends[0], factory, kj::mv(suspendedRequest));

    auto drainPromise = kj::evalLast([&]() {
      return server.drain();
    });

    // This time, the server drained cleanly.
    KJ_EXPECT(listenPromise.poll(waitScope));
    KJ_EXPECT(listenPromise.wait(waitScope));
    // No suspended request this time.
    suspendedRequest = factory.getSuspended();
    KJ_EXPECT(suspendedRequest == nullptr);

    drainPromise.wait(waitScope);
  }

  writePromise.wait(waitScope);

  // Close the server side of the pipe so our read promise completes.
  pipe.ends[0] = nullptr;

  auto responses = readPromise.wait(waitScope);
  static constexpr kj::StringPtr RESPONSE =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "0\r\n"
      "\r\n"_kj;
  KJ_EXPECT(kj::str(kj::delimited(kj::repeat(RESPONSE, 4), "")) == responses);
}

KJ_TEST("HttpServer can suspend a request with no leftover") {
  // This test verifies that if the request loop's read perfectly ends at the end of message
  // headers, leaving no leftover section, we can still successfully suspend and resume.

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  // This HttpService will not actually be used, because we're passing a factory in to
  // listenHttpCleanDrain().
  HangingHttpService service;
  HttpServer server(timer, table, service);

  kj::Maybe<HttpServer::SuspendedRequest> suspendedRequest;

  SuspendAfter factory;

  {
    factory.suspendAfter(0);
    auto listenPromise = server.listenHttpCleanDrain(*pipe.ends[0], factory);

    static constexpr kj::StringPtr REQUEST_HEADERS =
        "POST / HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"_kj;
    pipe.ends[1]->write(REQUEST_HEADERS.begin(), REQUEST_HEADERS.size()).wait(waitScope);

    // The listen promise is fulfilled with false.
    KJ_EXPECT(listenPromise.poll(waitScope));
    KJ_EXPECT(!listenPromise.wait(waitScope));

    // And we have a SuspendedRequest. We know that it has no leftover, because we only wrote
    // headers, no body yet.
    suspendedRequest = factory.getSuspended();
    KJ_EXPECT(suspendedRequest != nullptr);
  }

  {
    factory.suspendAfter(1);
    auto listenPromise = server.listenHttpCleanDrain(
        *pipe.ends[0], factory, kj::mv(suspendedRequest));

    auto drainPromise = kj::evalLast([&]() {
      return server.drain();
    });

    // We need to read the response for the HttpServer to drain.
    auto readPromise = pipe.ends[1]->readAllText();

    static constexpr kj::StringPtr REQUEST_BODY =
        "6\r\n"
        "foobar\r\n"
        "0\r\n"
        "\r\n"_kj;
    pipe.ends[1]->write(REQUEST_BODY.begin(), REQUEST_BODY.size()).wait(waitScope);

    // Clean drain.
    KJ_EXPECT(listenPromise.poll(waitScope));
    KJ_EXPECT(listenPromise.wait(waitScope));

    drainPromise.wait(waitScope);

    // No SuspendedRequest.
    suspendedRequest = factory.getSuspended();
    KJ_EXPECT(suspendedRequest == nullptr);

    // Close the server side of the pipe so our read promise completes.
    pipe.ends[0] = nullptr;

    auto response = readPromise.wait(waitScope);
    static constexpr kj::StringPtr RESPONSE =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "0\r\n"
        "\r\n"_kj;
    KJ_EXPECT(RESPONSE == response);
  }
}

KJ_TEST("HttpServer::listenHttpCleanDrain() factory-created services outlive requests") {
  // Test that the lifetimes of factory-created Own<HttpService> objects are handled correctly.

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  // This HttpService will not actually be used, because we're passing a factory in to
  // listenHttpCleanDrain().
  HangingHttpService service;
  HttpServer server(timer, table, service);

  uint serviceCount = 0;

  // A factory which returns a service whose request() function responds asynchronously.
  auto factory = [&](HttpServer::SuspendableRequest&) -> kj::Own<HttpService> {
    class ServiceImpl final: public HttpService {
    public:
      explicit ServiceImpl(uint& serviceCount): serviceCount(++serviceCount) {}
      ~ServiceImpl() noexcept(false) { --serviceCount; }
      KJ_DISALLOW_COPY_AND_MOVE(ServiceImpl);

      kj::Promise<void> request(
          HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
          kj::AsyncInputStream& requestBody, Response& response) override {
        return evalLater([&serviceCount = serviceCount, &table = table, &requestBody, &response]() {
          // This KJ_EXPECT here is the entire point of this test.
          KJ_EXPECT(serviceCount == 1)
          HttpHeaders responseHeaders(table);
          response.send(200, "OK", responseHeaders);
          return requestBody.readAllBytes().ignoreResult();
        });
      }

    private:
      HttpHeaderTable table;

      uint& serviceCount;
    };

    return kj::heap<ServiceImpl>(serviceCount);
  };

  auto listenPromise = server.listenHttpCleanDrain(*pipe.ends[0], factory);

  static constexpr kj::StringPtr REQUEST =
      "POST / HTTP/1.1\r\n"
      "Content-Length: 6\r\n"
      "\r\n"
      "foobar"_kj;
  pipe.ends[1]->write(REQUEST.begin(), REQUEST.size()).wait(waitScope);

  // We need to read the response for the HttpServer to drain.
  auto readPromise = pipe.ends[1]->readAllText();

  // http-socketpair-test quirk: we must drive the request loop past the point of receiving request
  // headers so that our call to server.drain() doesn't prematurely cancel the request.
  KJ_EXPECT(!listenPromise.poll(waitScope));

  auto drainPromise = kj::evalLast([&]() {
    return server.drain();
  });

  // Clean drain.
  KJ_EXPECT(listenPromise.poll(waitScope));
  KJ_EXPECT(listenPromise.wait(waitScope));

  drainPromise.wait(waitScope);

  // Close the server side of the pipe so our read promise completes.
  pipe.ends[0] = nullptr;
  auto response = readPromise.wait(waitScope);

  static constexpr kj::StringPtr RESPONSE =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "0\r\n"
      "\r\n"_kj;
  KJ_EXPECT(RESPONSE == response);
}

// -----------------------------------------------------------------------------

KJ_TEST("newHttpService from HttpClient") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto frontPipe = KJ_HTTP_TEST_CREATE_2PIPE;
  auto backPipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::Promise<void> writeResponsesPromise = kj::READY_NOW;
  for (auto& testCase: PIPELINE_TESTS) {
    writeResponsesPromise = writeResponsesPromise
        .then([&]() {
      return expectRead(*backPipe.ends[1], testCase.request.raw);
    }).then([&]() {
      return backPipe.ends[1]->write(testCase.response.raw.begin(), testCase.response.raw.size());
    });
  }

  {
    HttpHeaderTable table;
    auto backClient = newHttpClient(table, *backPipe.ends[0]);
    auto frontService = newHttpService(*backClient);
    HttpServer frontServer(timer, table, *frontService);
    auto listenTask = frontServer.listenHttp(kj::mv(frontPipe.ends[1]));

    for (auto& testCase: PIPELINE_TESTS) {
      KJ_CONTEXT(testCase.request.raw, testCase.response.raw);

      frontPipe.ends[0]->write(testCase.request.raw.begin(), testCase.request.raw.size())
               .wait(waitScope);

      expectRead(*frontPipe.ends[0], testCase.response.raw).wait(waitScope);
    }

    frontPipe.ends[0]->shutdownWrite();
    listenTask.wait(waitScope);
  }

  backPipe.ends[0]->shutdownWrite();
  writeResponsesPromise.wait(waitScope);
}

KJ_TEST("newHttpService from HttpClient WebSockets") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto frontPipe = KJ_HTTP_TEST_CREATE_2PIPE;
  auto backPipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto request = kj::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);
  auto writeResponsesPromise = expectRead(*backPipe.ends[1], request)
      .then([&]() { return writeA(*backPipe.ends[1], asBytes(WEBSOCKET_RESPONSE_HANDSHAKE)); })
      .then([&]() { return writeA(*backPipe.ends[1], WEBSOCKET_FIRST_MESSAGE_INLINE); })
      .then([&]() { return expectRead(*backPipe.ends[1], WEBSOCKET_SEND_MESSAGE); })
      .then([&]() { return writeA(*backPipe.ends[1], WEBSOCKET_REPLY_MESSAGE); })
      .then([&]() { return expectRead(*backPipe.ends[1], WEBSOCKET_SEND_CLOSE); })
      .then([&]() { return writeA(*backPipe.ends[1], WEBSOCKET_REPLY_CLOSE); })
      .then([&]() { return expectEnd(*backPipe.ends[1]); })
      .then([&]() { backPipe.ends[1]->shutdownWrite(); })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  {
    HttpHeaderTable table;
    FakeEntropySource entropySource;
    HttpClientSettings clientSettings;
    clientSettings.entropySource = entropySource;
    auto backClientStream = kj::mv(backPipe.ends[0]);
    auto backClient = newHttpClient(table, *backClientStream, clientSettings);
    auto frontService = newHttpService(*backClient);
    HttpServer frontServer(timer, table, *frontService);
    auto listenTask = frontServer.listenHttp(kj::mv(frontPipe.ends[1]));

    writeA(*frontPipe.ends[0], request.asBytes()).wait(waitScope);
    expectRead(*frontPipe.ends[0], WEBSOCKET_RESPONSE_HANDSHAKE).wait(waitScope);

    expectRead(*frontPipe.ends[0], WEBSOCKET_FIRST_MESSAGE_INLINE).wait(waitScope);
    writeA(*frontPipe.ends[0], WEBSOCKET_SEND_MESSAGE).wait(waitScope);
    expectRead(*frontPipe.ends[0], WEBSOCKET_REPLY_MESSAGE).wait(waitScope);
    writeA(*frontPipe.ends[0], WEBSOCKET_SEND_CLOSE).wait(waitScope);
    expectRead(*frontPipe.ends[0], WEBSOCKET_REPLY_CLOSE).wait(waitScope);

    frontPipe.ends[0]->shutdownWrite();
    listenTask.wait(waitScope);
  }

  writeResponsesPromise.wait(waitScope);
}

KJ_TEST("newHttpService from HttpClient WebSockets disconnect") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto frontPipe = KJ_HTTP_TEST_CREATE_2PIPE;
  auto backPipe = KJ_HTTP_TEST_CREATE_2PIPE;

  auto request = kj::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);
  auto writeResponsesPromise = expectRead(*backPipe.ends[1], request)
      .then([&]() { return writeA(*backPipe.ends[1], asBytes(WEBSOCKET_RESPONSE_HANDSHAKE)); })
      .then([&]() { return writeA(*backPipe.ends[1], WEBSOCKET_FIRST_MESSAGE_INLINE); })
      .then([&]() { return expectRead(*backPipe.ends[1], WEBSOCKET_SEND_MESSAGE); })
      .then([&]() { backPipe.ends[1]->shutdownWrite(); })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  {
    HttpHeaderTable table;
    FakeEntropySource entropySource;
    HttpClientSettings clientSettings;
    clientSettings.entropySource = entropySource;
    auto backClient = newHttpClient(table, *backPipe.ends[0], clientSettings);
    auto frontService = newHttpService(*backClient);
    HttpServer frontServer(timer, table, *frontService);
    auto listenTask = frontServer.listenHttp(kj::mv(frontPipe.ends[1]));

    writeA(*frontPipe.ends[0], request.asBytes()).wait(waitScope);
    expectRead(*frontPipe.ends[0], WEBSOCKET_RESPONSE_HANDSHAKE).wait(waitScope);

    expectRead(*frontPipe.ends[0], WEBSOCKET_FIRST_MESSAGE_INLINE).wait(waitScope);
    writeA(*frontPipe.ends[0], WEBSOCKET_SEND_MESSAGE).wait(waitScope);

    KJ_EXPECT(frontPipe.ends[0]->readAllText().wait(waitScope) == "");

    frontPipe.ends[0]->shutdownWrite();
    listenTask.wait(waitScope);
  }

  writeResponsesPromise.wait(waitScope);
}

// -----------------------------------------------------------------------------

KJ_TEST("newHttpClient from HttpService") {
  auto PIPELINE_TESTS = pipelineTestCases();

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  auto client = newHttpClient(service);

  for (auto& testCase: PIPELINE_TESTS) {
    testHttpClient(waitScope, table, *client, testCase);
  }
}

KJ_TEST("newHttpClient from HttpService WebSockets") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();
  TestWebSocketService service(*headerTable, hMyHeader);
  auto client = newHttpClient(service);

  testWebSocketClient(waitScope, *headerTable, hMyHeader, *client);
}

KJ_TEST("adapted client/server propagates request exceptions like non-adapted client") {
  KJ_HTTP_TEST_SETUP_IO;

  HttpHeaderTable table;
  HttpHeaders headers(table);

  class FailingHttpClient final: public HttpClient {
  public:
    Request request(
        HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
        kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
      KJ_FAIL_ASSERT("request_fail");
    }

    kj::Promise<WebSocketResponse> openWebSocket(
        kj::StringPtr url, const HttpHeaders& headers) override {
      KJ_FAIL_ASSERT("websocket_fail");
    }
  };

  auto rawClient = kj::heap<FailingHttpClient>();

  auto innerClient = kj::heap<FailingHttpClient>();
  auto adaptedService = kj::newHttpService(*innerClient).attach(kj::mv(innerClient));
  auto adaptedClient = kj::newHttpClient(*adaptedService).attach(kj::mv(adaptedService));

  KJ_EXPECT_THROW_MESSAGE("request_fail", rawClient->request(HttpMethod::POST, "/"_kj, headers));
  KJ_EXPECT_THROW_MESSAGE("request_fail", adaptedClient->request(HttpMethod::POST, "/"_kj, headers));

  KJ_EXPECT_THROW_MESSAGE("websocket_fail", rawClient->openWebSocket("/"_kj, headers));
  KJ_EXPECT_THROW_MESSAGE("websocket_fail", adaptedClient->openWebSocket("/"_kj, headers));
}

class DelayedCompletionHttpService final: public HttpService {
public:
  DelayedCompletionHttpService(HttpHeaderTable& table, kj::Maybe<uint64_t> expectedLength)
      : table(table), expectedLength(expectedLength) {}

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    auto stream = response.send(200, "OK", HttpHeaders(table), expectedLength);
    auto promise = stream->write("foo", 3);
    return promise.attach(kj::mv(stream)).then([this]() {
      return kj::mv(paf.promise);
    });
  }

  kj::PromiseFulfiller<void>& getFulfiller() { return *paf.fulfiller; }

private:
  HttpHeaderTable& table;
  kj::Maybe<uint64_t> expectedLength;
  kj::PromiseFulfillerPair<void> paf = kj::newPromiseAndFulfiller<void>();
};

void doDelayedCompletionTest(bool exception, kj::Maybe<uint64_t> expectedLength) noexcept {
  KJ_HTTP_TEST_SETUP_IO;

  HttpHeaderTable table;

  DelayedCompletionHttpService service(table, expectedLength);
  auto client = newHttpClient(service);

  auto resp = client->request(HttpMethod::GET, "/", HttpHeaders(table), uint64_t(0))
      .response.wait(waitScope);
  KJ_EXPECT(resp.statusCode == 200);

  // Read "foo" from the response body: works
  char buffer[16];
  KJ_ASSERT(resp.body->tryRead(buffer, 1, sizeof(buffer)).wait(waitScope) == 3);
  buffer[3] = '\0';
  KJ_EXPECT(buffer == "foo"_kj);

  // But reading any more hangs.
  auto promise = resp.body->tryRead(buffer, 1, sizeof(buffer));

  KJ_EXPECT(!promise.poll(waitScope));

  // Until we cause the service to return.
  if (exception) {
    service.getFulfiller().reject(KJ_EXCEPTION(FAILED, "service-side failure"));
  } else {
    service.getFulfiller().fulfill();
  }

  KJ_ASSERT(promise.poll(waitScope));

  if (exception) {
    KJ_EXPECT_THROW_MESSAGE("service-side failure", promise.wait(waitScope));
  } else {
    promise.wait(waitScope);
  }
};

KJ_TEST("adapted client waits for service to complete before returning EOF on response stream") {
  doDelayedCompletionTest(false, uint64_t(3));
}

KJ_TEST("adapted client waits for service to complete before returning EOF on chunked response") {
  doDelayedCompletionTest(false, nullptr);
}

KJ_TEST("adapted client propagates throw from service after complete response body sent") {
  doDelayedCompletionTest(true, uint64_t(3));
}

KJ_TEST("adapted client propagates throw from service after incomplete response body sent") {
  doDelayedCompletionTest(true, uint64_t(6));
}

KJ_TEST("adapted client propagates throw from service after chunked response body sent") {
  doDelayedCompletionTest(true, nullptr);
}

class DelayedCompletionWebSocketHttpService final: public HttpService {
public:
  DelayedCompletionWebSocketHttpService(HttpHeaderTable& table, bool closeUpstreamFirst)
      : table(table), closeUpstreamFirst(closeUpstreamFirst) {}

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    KJ_ASSERT(headers.isWebSocket());

    auto ws = response.acceptWebSocket(HttpHeaders(table));
    kj::Promise<void> promise = kj::READY_NOW;
    if (closeUpstreamFirst) {
      // Wait for a close message from the client before starting.
      promise = promise.then([&ws = *ws]() { return ws.receive(); }).ignoreResult();
    }
    promise = promise
        .then([&ws = *ws]() { return ws.send("foo"_kj); })
        .then([&ws = *ws]() { return ws.close(1234, "closed"_kj); });
    if (!closeUpstreamFirst) {
      // Wait for a close message from the client at the end.
      promise = promise.then([&ws = *ws]() { return ws.receive(); }).ignoreResult();
    }
    return promise.attach(kj::mv(ws)).then([this]() {
      return kj::mv(paf.promise);
    });
  }

  kj::PromiseFulfiller<void>& getFulfiller() { return *paf.fulfiller; }

private:
  HttpHeaderTable& table;
  bool closeUpstreamFirst;
  kj::PromiseFulfillerPair<void> paf = kj::newPromiseAndFulfiller<void>();
};

void doDelayedCompletionWebSocketTest(bool exception, bool closeUpstreamFirst) noexcept {
  KJ_HTTP_TEST_SETUP_IO;

  HttpHeaderTable table;

  DelayedCompletionWebSocketHttpService service(table, closeUpstreamFirst);
  auto client = newHttpClient(service);

  auto resp = client->openWebSocket("/", HttpHeaders(table)).wait(waitScope);
  auto ws = kj::mv(KJ_ASSERT_NONNULL(resp.webSocketOrBody.tryGet<kj::Own<WebSocket>>()));

  if (closeUpstreamFirst) {
    // Send "close" immediately.
    ws->close(1234, "whatever"_kj).wait(waitScope);
  }

  // Read "foo" from the WebSocket: works
  {
    auto msg = ws->receive().wait(waitScope);
    KJ_ASSERT(msg.is<kj::String>());
    KJ_ASSERT(msg.get<kj::String>() == "foo");
  }

  kj::Promise<void> promise = nullptr;
  if (closeUpstreamFirst) {
    // Receiving the close hangs.
    promise = ws->receive()
        .then([](WebSocket::Message&& msg) { KJ_EXPECT(msg.is<WebSocket::Close>()); });
  } else {
    auto msg = ws->receive().wait(waitScope);
    KJ_ASSERT(msg.is<WebSocket::Close>());

    // Sending a close hangs.
    promise = ws->close(1234, "whatever"_kj);
  }
  KJ_EXPECT(!promise.poll(waitScope));

  // Until we cause the service to return.
  if (exception) {
    service.getFulfiller().reject(KJ_EXCEPTION(FAILED, "service-side failure"));
  } else {
    service.getFulfiller().fulfill();
  }

  KJ_ASSERT(promise.poll(waitScope));

  if (exception) {
    KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("service-side failure", promise.wait(waitScope));
  } else {
    promise.wait(waitScope);
  }
};

KJ_TEST("adapted client waits for service to complete before completing upstream close on WebSocket") {
  doDelayedCompletionWebSocketTest(false, false);
}

KJ_TEST("adapted client waits for service to complete before returning downstream close on WebSocket") {
  doDelayedCompletionWebSocketTest(false, true);
}

KJ_TEST("adapted client propagates throw from service after WebSocket upstream close sent") {
  doDelayedCompletionWebSocketTest(true, false);
}

KJ_TEST("adapted client propagates throw from service after WebSocket downstream close sent") {
  doDelayedCompletionWebSocketTest(true, true);
}

// -----------------------------------------------------------------------------

class CountingIoStream final: public kj::AsyncIoStream {
  // An AsyncIoStream wrapper which decrements a counter when destroyed (allowing us to count how
  // many connections are open).

public:
  CountingIoStream(kj::Own<kj::AsyncIoStream> inner, uint& count)
      : inner(kj::mv(inner)), count(count) {}
  ~CountingIoStream() noexcept(false) {
    --count;
  }

  kj::Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return inner->read(buffer, minBytes, maxBytes);
  }
  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return inner->tryRead(buffer, minBytes, maxBytes);
  }
  kj::Maybe<uint64_t> tryGetLength() override {
    return inner->tryGetLength();;
  }
  kj::Promise<uint64_t> pumpTo(kj::AsyncOutputStream& output, uint64_t amount) override {
    return inner->pumpTo(output, amount);
  }
  kj::Promise<void> write(const void* buffer, size_t size) override {
    return inner->write(buffer, size);
  }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    return inner->write(pieces);
  }
  kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(
      kj::AsyncInputStream& input, uint64_t amount = kj::maxValue) override {
    return inner->tryPumpFrom(input, amount);
  }
  Promise<void> whenWriteDisconnected() override {
    return inner->whenWriteDisconnected();
  }
  void shutdownWrite() override {
    return inner->shutdownWrite();
  }
  void abortRead() override {
    return inner->abortRead();
  }

public:
  kj::Own<AsyncIoStream> inner;
  uint& count;
};

class CountingNetworkAddress final: public kj::NetworkAddress {
public:
  CountingNetworkAddress(kj::NetworkAddress& inner, uint& count, uint& cumulative)
      : inner(inner), count(count), addrCount(ownAddrCount), cumulative(cumulative) {}
  CountingNetworkAddress(kj::Own<kj::NetworkAddress> inner, uint& count, uint& addrCount)
      : inner(*inner), ownInner(kj::mv(inner)), count(count), addrCount(addrCount),
        cumulative(ownCumulative) {}
  ~CountingNetworkAddress() noexcept(false) {
    --addrCount;
  }

  kj::Promise<kj::Own<kj::AsyncIoStream>> connect() override {
    ++count;
    ++cumulative;
    return inner.connect()
        .then([this](kj::Own<kj::AsyncIoStream> stream) -> kj::Own<kj::AsyncIoStream> {
      return kj::heap<CountingIoStream>(kj::mv(stream), count);
    });
  }

  kj::Own<kj::ConnectionReceiver> listen() override { KJ_UNIMPLEMENTED("test"); }
  kj::Own<kj::NetworkAddress> clone() override { KJ_UNIMPLEMENTED("test"); }
  kj::String toString() override { KJ_UNIMPLEMENTED("test"); }

private:
  kj::NetworkAddress& inner;
  kj::Own<kj::NetworkAddress> ownInner;
  uint& count;
  uint ownAddrCount = 1;
  uint& addrCount;
  uint ownCumulative = 0;
  uint& cumulative;
};

class ConnectionCountingNetwork final: public kj::Network {
public:
  ConnectionCountingNetwork(kj::Network& inner, uint& count, uint& addrCount)
      : inner(inner), count(count), addrCount(addrCount) {}

  Promise<Own<NetworkAddress>> parseAddress(StringPtr addr, uint portHint = 0) override {
    ++addrCount;
    return inner.parseAddress(addr, portHint)
        .then([this](Own<NetworkAddress>&& addr) -> Own<NetworkAddress> {
      return kj::heap<CountingNetworkAddress>(kj::mv(addr), count, addrCount);
    });
  }
  Own<NetworkAddress> getSockaddr(const void* sockaddr, uint len) override {
    KJ_UNIMPLEMENTED("test");
  }
  Own<Network> restrictPeers(
      kj::ArrayPtr<const kj::StringPtr> allow,
      kj::ArrayPtr<const kj::StringPtr> deny = nullptr) override {
    KJ_UNIMPLEMENTED("test");
  }

private:
  kj::Network& inner;
  uint& count;
  uint& addrCount;
};

class DummyService final: public HttpService {
public:
  DummyService(HttpHeaderTable& headerTable): headerTable(headerTable) {}

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    if (!headers.isWebSocket()) {
      if (url == "/throw") {
        return KJ_EXCEPTION(FAILED, "client requested failure");
      }

      auto body = kj::str(headers.get(HttpHeaderId::HOST).orDefault("null"), ":", url);
      auto stream = response.send(200, "OK", HttpHeaders(headerTable), body.size());
      auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);
      promises.add(stream->write(body.begin(), body.size()));
      promises.add(requestBody.readAllBytes().ignoreResult());
      return kj::joinPromises(promises.finish()).attach(kj::mv(stream), kj::mv(body));
    } else {
      auto ws = response.acceptWebSocket(HttpHeaders(headerTable));
      auto body = kj::str(headers.get(HttpHeaderId::HOST).orDefault("null"), ":", url);
      auto sendPromise = ws->send(body);

      auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);
      promises.add(sendPromise.attach(kj::mv(body)));
      promises.add(ws->receive().ignoreResult());
      return kj::joinPromises(promises.finish()).attach(kj::mv(ws));
    }
  }

private:
  HttpHeaderTable& headerTable;
};

KJ_TEST("HttpClient connection management") {
  KJ_HTTP_TEST_SETUP_IO;
  KJ_HTTP_TEST_SETUP_LOOPBACK_LISTENER_AND_ADDR;

  kj::TimerImpl serverTimer(kj::origin<kj::TimePoint>());
  kj::TimerImpl clientTimer(kj::origin<kj::TimePoint>());
  HttpHeaderTable headerTable;

  DummyService service(headerTable);
  HttpServerSettings serverSettings;
  HttpServer server(serverTimer, headerTable, service, serverSettings);
  auto listenTask = server.listenHttp(*listener);

  uint count = 0;
  uint cumulative = 0;
  CountingNetworkAddress countingAddr(*addr, count, cumulative);

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  auto client = newHttpClient(clientTimer, headerTable, countingAddr, clientSettings);

  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 0);

  uint i = 0;
  auto doRequest = [&]() {
    uint n = i++;
    return client->request(HttpMethod::GET, kj::str("/", n), HttpHeaders(headerTable)).response
        .then([](HttpClient::Response&& response) {
      auto promise = response.body->readAllText();
      return promise.attach(kj::mv(response.body));
    }).then([n](kj::String body) {
      KJ_EXPECT(body == kj::str("null:/", n));
    });
  };

  // We can do several requests in a row and only have one connection.
  doRequest().wait(waitScope);
  doRequest().wait(waitScope);
  doRequest().wait(waitScope);
  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 1);

  // But if we do two in parallel, we'll end up with two connections.
  auto req1 = doRequest();
  auto req2 = doRequest();
  req1.wait(waitScope);
  req2.wait(waitScope);
  KJ_EXPECT(count == 2);
  KJ_EXPECT(cumulative == 2);

  // We can reuse after a POST, provided we write the whole POST body properly.
  {
    auto req = client->request(
        HttpMethod::POST, kj::str("/foo"), HttpHeaders(headerTable), size_t(6));
    req.body->write("foobar", 6).wait(waitScope);
    req.response.wait(waitScope).body->readAllBytes().wait(waitScope);
  }
  KJ_EXPECT(count == 2);
  KJ_EXPECT(cumulative == 2);
  doRequest().wait(waitScope);
  KJ_EXPECT(count == 2);
  KJ_EXPECT(cumulative == 2);

  // Advance time for half the timeout, then exercise one of the connections.
  clientTimer.advanceTo(clientTimer.now() + clientSettings.idleTimeout / 2);
  doRequest().wait(waitScope);
  doRequest().wait(waitScope);
  waitScope.poll();
  KJ_EXPECT(count == 2);
  KJ_EXPECT(cumulative == 2);

  // Advance time past when the other connection should time out. It should be dropped.
  clientTimer.advanceTo(clientTimer.now() + clientSettings.idleTimeout * 3 / 4);
  waitScope.poll();
  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 2);

  // Wait for the other to drop.
  clientTimer.advanceTo(clientTimer.now() + clientSettings.idleTimeout / 2);
  waitScope.poll();
  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 2);

  // New request creates a new connection again.
  doRequest().wait(waitScope);
  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 3);

  // WebSocket connections are not reused.
  client->openWebSocket(kj::str("/websocket"), HttpHeaders(headerTable))
      .wait(waitScope);
  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 3);

  // Errored connections are not reused.
  doRequest().wait(waitScope);
  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 4);
  client->request(HttpMethod::GET, kj::str("/throw"), HttpHeaders(headerTable)).response
      .wait(waitScope).body->readAllBytes().wait(waitScope);
  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 4);

  // Connections where we failed to read the full response body are not reused.
  doRequest().wait(waitScope);
  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 5);
  client->request(HttpMethod::GET, kj::str("/foo"), HttpHeaders(headerTable)).response
      .wait(waitScope);
  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 5);

  // Connections where we didn't even wait for the response headers are not reused.
  doRequest().wait(waitScope);
  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 6);
  client->request(HttpMethod::GET, kj::str("/foo"), HttpHeaders(headerTable));
  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 6);

  // Connections where we failed to write the full request body are not reused.
  doRequest().wait(waitScope);
  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 7);
  client->request(HttpMethod::POST, kj::str("/foo"), HttpHeaders(headerTable), size_t(6)).response
      .wait(waitScope).body->readAllBytes().wait(waitScope);
  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 7);

  // If the server times out the connection, we figure it out on the client.
  doRequest().wait(waitScope);

  // TODO(someday): Figure out why the following poll is necessary for the test to pass on Windows
  //   and Mac.  Without it, it seems that the request's connection never starts, so the
  //   subsequent advanceTo() does not actually time out the connection.
  waitScope.poll();

  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 8);
  serverTimer.advanceTo(serverTimer.now() + serverSettings.pipelineTimeout * 2);
  waitScope.poll();
  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 8);

  // Can still make requests.
  doRequest().wait(waitScope);
  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 9);
}

KJ_TEST("HttpClient disable connection reuse") {
  KJ_HTTP_TEST_SETUP_IO;
  KJ_HTTP_TEST_SETUP_LOOPBACK_LISTENER_AND_ADDR;

  kj::TimerImpl serverTimer(kj::origin<kj::TimePoint>());
  kj::TimerImpl clientTimer(kj::origin<kj::TimePoint>());
  HttpHeaderTable headerTable;

  DummyService service(headerTable);
  HttpServerSettings serverSettings;
  HttpServer server(serverTimer, headerTable, service, serverSettings);
  auto listenTask = server.listenHttp(*listener);

  uint count = 0;
  uint cumulative = 0;
  CountingNetworkAddress countingAddr(*addr, count, cumulative);

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.idleTimeout = 0 * kj::SECONDS;
  auto client = newHttpClient(clientTimer, headerTable, countingAddr, clientSettings);

  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 0);

  uint i = 0;
  auto doRequest = [&]() {
    uint n = i++;
    return client->request(HttpMethod::GET, kj::str("/", n), HttpHeaders(headerTable)).response
        .then([](HttpClient::Response&& response) {
      auto promise = response.body->readAllText();
      return promise.attach(kj::mv(response.body));
    }).then([n](kj::String body) {
      KJ_EXPECT(body == kj::str("null:/", n));
    });
  };

  // Each serial request gets its own connection.
  doRequest().wait(waitScope);
  doRequest().wait(waitScope);
  doRequest().wait(waitScope);
  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 3);

  // Each parallel request gets its own connection.
  auto req1 = doRequest();
  auto req2 = doRequest();
  req1.wait(waitScope);
  req2.wait(waitScope);
  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 5);
}

KJ_TEST("HttpClient concurrency limiting") {
#if KJ_HTTP_TEST_USE_OS_PIPE && !__linux__
  // On Windows and Mac, OS event delivery is not always immediate, and that seems to make this
  // test flakey. On Linux, events are always immediately delivered. For now, we compile the test
  // but we don't run it outside of Linux. We do run the in-memory-pipes version on all OSs since
  // that mode shouldn't depend on kernel behavior at all.
  return;
#endif

  KJ_HTTP_TEST_SETUP_IO;
  KJ_HTTP_TEST_SETUP_LOOPBACK_LISTENER_AND_ADDR;

  kj::TimerImpl serverTimer(kj::origin<kj::TimePoint>());
  kj::TimerImpl clientTimer(kj::origin<kj::TimePoint>());
  HttpHeaderTable headerTable;

  DummyService service(headerTable);
  HttpServerSettings serverSettings;
  HttpServer server(serverTimer, headerTable, service, serverSettings);
  auto listenTask = server.listenHttp(*listener);

  uint count = 0;
  uint cumulative = 0;
  CountingNetworkAddress countingAddr(*addr, count, cumulative);

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  clientSettings.idleTimeout = 0 * kj::SECONDS;
  auto innerClient = newHttpClient(clientTimer, headerTable, countingAddr, clientSettings);

  struct CallbackEvent {
    uint runningCount;
    uint pendingCount;

    bool operator==(const CallbackEvent& other) const {
      return runningCount == other.runningCount && pendingCount == other.pendingCount;
    }
    bool operator!=(const CallbackEvent& other) const { return !(*this == other); }
    // TODO(someday): Can use default spaceship operator in C++20:
    //auto operator<=>(const CallbackEvent&) const = default;
  };

  kj::Vector<CallbackEvent> callbackEvents;
  auto callback = [&](uint runningCount, uint pendingCount) {
    callbackEvents.add(CallbackEvent{runningCount, pendingCount});
  };
  auto client = newConcurrencyLimitingHttpClient(*innerClient, 1, kj::mv(callback));

  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 0);

  uint i = 0;
  auto doRequest = [&]() {
    uint n = i++;
    return client->request(HttpMethod::GET, kj::str("/", n), HttpHeaders(headerTable)).response
        .then([](HttpClient::Response&& response) {
      auto promise = response.body->readAllText();
      return promise.attach(kj::mv(response.body));
    }).then([n](kj::String body) {
      KJ_EXPECT(body == kj::str("null:/", n));
    });
  };

  // Second connection blocked by first.
  auto req1 = doRequest();

  KJ_EXPECT(callbackEvents == kj::ArrayPtr<const CallbackEvent>({ {1, 0} }));
  callbackEvents.clear();

  auto req2 = doRequest();

  // TODO(someday): Figure out why this poll() is necessary on Windows and macOS.
  waitScope.poll();

  KJ_EXPECT(req1.poll(waitScope));
  KJ_EXPECT(!req2.poll(waitScope));
  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 1);
  KJ_EXPECT(callbackEvents == kj::ArrayPtr<const CallbackEvent>({ {1, 1} }));
  callbackEvents.clear();

  // Releasing first connection allows second to start.
  req1.wait(waitScope);
  KJ_EXPECT(req2.poll(waitScope));
  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 2);
  KJ_EXPECT(callbackEvents == kj::ArrayPtr<const CallbackEvent>({ {1, 0} }));
  callbackEvents.clear();

  req2.wait(waitScope);
  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 2);
  KJ_EXPECT(callbackEvents == kj::ArrayPtr<const CallbackEvent>({ {0, 0} }));
  callbackEvents.clear();

  // Using body stream after releasing blocked response promise throws no exception
  auto req3 = doRequest();
  {
    kj::Own<kj::AsyncOutputStream> req4Body;
    {
      auto req4 = client->request(HttpMethod::GET, kj::str("/", ++i), HttpHeaders(headerTable));
      waitScope.poll();
      req4Body = kj::mv(req4.body);
    }
    auto writePromise = req4Body->write("a", 1);
    KJ_EXPECT(!writePromise.poll(waitScope));
  }
  req3.wait(waitScope);
  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 3);

  // Similar connection limiting for web sockets
  // TODO(someday): Figure out why the sequencing of websockets events does
  // not work correctly on Windows (and maybe macOS?).  The solution is not as
  // simple as inserting poll()s as above, since doing so puts the websocket in
  // a state that trips a "previous HTTP message body incomplete" assertion,
  // while trying to write 500 network response.
  callbackEvents.clear();
  auto ws1 = kj::heap(client->openWebSocket(kj::str("/websocket"), HttpHeaders(headerTable)));
  KJ_EXPECT(callbackEvents == kj::ArrayPtr<const CallbackEvent>({ {1, 0} }));
  callbackEvents.clear();
  auto ws2 = kj::heap(client->openWebSocket(kj::str("/websocket"), HttpHeaders(headerTable)));
  KJ_EXPECT(ws1->poll(waitScope));
  KJ_EXPECT(!ws2->poll(waitScope));
  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 4);
  KJ_EXPECT(callbackEvents == kj::ArrayPtr<const CallbackEvent>({ {1, 1} }));
  callbackEvents.clear();

  {
    auto response1 = ws1->wait(waitScope);
    KJ_EXPECT(!ws2->poll(waitScope));
    KJ_EXPECT(callbackEvents == kj::ArrayPtr<const CallbackEvent>({}));
  }
  KJ_EXPECT(ws2->poll(waitScope));
  KJ_EXPECT(count == 1);
  KJ_EXPECT(cumulative == 5);
  KJ_EXPECT(callbackEvents == kj::ArrayPtr<const CallbackEvent>({ {1, 0} }));
  callbackEvents.clear();
  {
    auto response2 = ws2->wait(waitScope);
    KJ_EXPECT(callbackEvents == kj::ArrayPtr<const CallbackEvent>({}));
  }
  KJ_EXPECT(count == 0);
  KJ_EXPECT(cumulative == 5);
  KJ_EXPECT(callbackEvents == kj::ArrayPtr<const CallbackEvent>({ {0, 0} }));
}

#if KJ_HTTP_TEST_USE_OS_PIPE
// This test relies on access to the network.
KJ_TEST("NetworkHttpClient connect impl") {
  KJ_HTTP_TEST_SETUP_IO;
  auto listener1 = io.provider->getNetwork().parseAddress("localhost", 0)
      .wait(io.waitScope)->listen();

  auto ignored KJ_UNUSED = listener1->accept().then([](Own<kj::AsyncIoStream> stream) {
    auto buffer = kj::str("test");
    return stream->write(buffer.cStr(), buffer.size()).attach(kj::mv(stream), kj::mv(buffer));
  }).eagerlyEvaluate(nullptr);

  HttpClientSettings clientSettings;
  kj::TimerImpl clientTimer(kj::origin<kj::TimePoint>());
  HttpHeaderTable headerTable;
  auto client = newHttpClient(clientTimer, headerTable,
      io.provider->getNetwork(), nullptr, clientSettings);
  auto request = client->connect(
      kj::str("localhost:", listener1->getPort()), HttpHeaders(headerTable), {});

  auto buf = kj::heapArray<char>(4);
  return request.connection->tryRead(buf.begin(), 1, buf.size())
      .then([buf = kj::mv(buf)](size_t count) {
    KJ_ASSERT(count == 4);
    KJ_ASSERT(kj::str(buf.asChars()) == "test");
  }).attach(kj::mv(request.connection)).wait(io.waitScope);
}
#endif

#if KJ_HTTP_TEST_USE_OS_PIPE
// TODO(someday): Implement mock kj::Network for userspace version of this test?
KJ_TEST("HttpClient multi host") {
  auto io = kj::setupAsyncIo();

  kj::TimerImpl serverTimer(kj::origin<kj::TimePoint>());
  kj::TimerImpl clientTimer(kj::origin<kj::TimePoint>());
  HttpHeaderTable headerTable;

  auto listener1 = io.provider->getNetwork().parseAddress("localhost", 0)
      .wait(io.waitScope)->listen();
  auto listener2 = io.provider->getNetwork().parseAddress("localhost", 0)
      .wait(io.waitScope)->listen();
  DummyService service(headerTable);
  HttpServer server(serverTimer, headerTable, service);
  auto listenTask1 = server.listenHttp(*listener1);
  auto listenTask2 = server.listenHttp(*listener2);

  uint count = 0, addrCount = 0;
  uint tlsCount = 0, tlsAddrCount = 0;
  ConnectionCountingNetwork countingNetwork(io.provider->getNetwork(), count, addrCount);
  ConnectionCountingNetwork countingTlsNetwork(io.provider->getNetwork(), tlsCount, tlsAddrCount);

  HttpClientSettings clientSettings;
  auto client = newHttpClient(clientTimer, headerTable,
      countingNetwork, countingTlsNetwork, clientSettings);

  KJ_EXPECT(count == 0);

  uint i = 0;
  auto doRequest = [&](bool tls, uint port) {
    uint n = i++;
    // We stick a double-slash in the URL to test that it doesn't get coalesced into one slash,
    // which was a bug in the past.
    return client->request(HttpMethod::GET,
        kj::str((tls ? "https://localhost:" : "http://localhost:"), port, "//", n),
                HttpHeaders(headerTable)).response
        .then([](HttpClient::Response&& response) {
      auto promise = response.body->readAllText();
      return promise.attach(kj::mv(response.body));
    }).then([n, port](kj::String body) {
      KJ_EXPECT(body == kj::str("localhost:", port, "://", n), body, port, n);
    });
  };

  uint port1 = listener1->getPort();
  uint port2 = listener2->getPort();

  // We can do several requests in a row to the same host and only have one connection.
  doRequest(false, port1).wait(io.waitScope);
  doRequest(false, port1).wait(io.waitScope);
  doRequest(false, port1).wait(io.waitScope);
  KJ_EXPECT(count == 1);
  KJ_EXPECT(tlsCount == 0);
  KJ_EXPECT(addrCount == 1);
  KJ_EXPECT(tlsAddrCount == 0);

  // Request a different host, and now we have two connections.
  doRequest(false, port2).wait(io.waitScope);
  KJ_EXPECT(count == 2);
  KJ_EXPECT(tlsCount == 0);
  KJ_EXPECT(addrCount == 2);
  KJ_EXPECT(tlsAddrCount == 0);

  // Try TLS.
  doRequest(true, port1).wait(io.waitScope);
  KJ_EXPECT(count == 2);
  KJ_EXPECT(tlsCount == 1);
  KJ_EXPECT(addrCount == 2);
  KJ_EXPECT(tlsAddrCount == 1);

  // Try first host again, no change in connection count.
  doRequest(false, port1).wait(io.waitScope);
  KJ_EXPECT(count == 2);
  KJ_EXPECT(tlsCount == 1);
  KJ_EXPECT(addrCount == 2);
  KJ_EXPECT(tlsAddrCount == 1);

  // Multiple requests in parallel forces more connections to that host.
  auto promise1 = doRequest(false, port1);
  auto promise2 = doRequest(false, port1);
  promise1.wait(io.waitScope);
  promise2.wait(io.waitScope);
  KJ_EXPECT(count == 3);
  KJ_EXPECT(tlsCount == 1);
  KJ_EXPECT(addrCount == 2);
  KJ_EXPECT(tlsAddrCount == 1);

  // Let everything expire.
  clientTimer.advanceTo(clientTimer.now() + clientSettings.idleTimeout * 2);
  io.waitScope.poll();
  KJ_EXPECT(count == 0);
  KJ_EXPECT(tlsCount == 0);
  KJ_EXPECT(addrCount == 0);
  KJ_EXPECT(tlsAddrCount == 0);

  // We can still request those hosts again.
  doRequest(false, port1).wait(io.waitScope);
  KJ_EXPECT(count == 1);
  KJ_EXPECT(tlsCount == 0);
  KJ_EXPECT(addrCount == 1);
  KJ_EXPECT(tlsAddrCount == 0);
}
#endif

// -----------------------------------------------------------------------------

#if KJ_HTTP_TEST_USE_OS_PIPE
// This test only makes sense using the real network.
KJ_TEST("HttpClient to capnproto.org") {
  auto io = kj::setupAsyncIo();

  auto maybeConn = io.provider->getNetwork().parseAddress("capnproto.org", 80)
      .then([](kj::Own<kj::NetworkAddress> addr) {
    auto promise = addr->connect();
    return promise.attach(kj::mv(addr));
  }).then([](kj::Own<kj::AsyncIoStream>&& connection) -> kj::Maybe<kj::Own<kj::AsyncIoStream>> {
    return kj::mv(connection);
  }, [](kj::Exception&& e) -> kj::Maybe<kj::Own<kj::AsyncIoStream>> {
    KJ_LOG(WARNING, "skipping test because couldn't connect to capnproto.org");
    return nullptr;
  }).wait(io.waitScope);

  KJ_IF_MAYBE(conn, maybeConn) {
    // Successfully connected to capnproto.org. Try doing GET /. We expect to get a redirect to
    // HTTPS, because what kind of horrible web site would serve in plaintext, really?

    HttpHeaderTable table;
    auto client = newHttpClient(table, **conn);

    HttpHeaders headers(table);
    headers.set(HttpHeaderId::HOST, "capnproto.org");

    auto response = client->request(HttpMethod::GET, "/", headers).response.wait(io.waitScope);
    KJ_EXPECT(response.statusCode / 100 == 3);
    auto location = KJ_ASSERT_NONNULL(response.headers->get(HttpHeaderId::LOCATION));
    KJ_EXPECT(location == "https://capnproto.org/");

    auto body = response.body->readAllText().wait(io.waitScope);
  }
}
#endif

// =======================================================================================
// Misc bugfix tests

class ReadCancelHttpService final: public HttpService {
  // HttpService that tries to read all request data but cancels after 1ms and sends a response.
public:
  ReadCancelHttpService(kj::Timer& timer, HttpHeaderTable& headerTable)
      : timer(timer), headerTable(headerTable) {}

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& responseSender) override {
    if (method == HttpMethod::POST) {
      // Try to read all content, but cancel after 1ms.

      // Actually, we can't literally cancel mid-read, because this leaves the stream in an
      // unknown state which requires closing the connection. Instead, we know that the sender
      // will send 5 bytes, so we read that, then pause.
      static char junk[5];
      return requestBody.read(junk, 5)
          .then([]() -> kj::Promise<void> { return kj::NEVER_DONE; })
          .exclusiveJoin(timer.afterDelay(1 * kj::MILLISECONDS))
          .then([this, &responseSender]() {
        responseSender.send(408, "Request Timeout", kj::HttpHeaders(headerTable), uint64_t(0));
      });
    } else {
      responseSender.send(200, "OK", kj::HttpHeaders(headerTable), uint64_t(0));
      return kj::READY_NOW;
    }
  }

private:
  kj::Timer& timer;
  HttpHeaderTable& headerTable;
};

KJ_TEST("canceling a length stream mid-read correctly discards rest of request") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  ReadCancelHttpService service(timer, table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  {
    static constexpr kj::StringPtr REQUEST =
        "POST / HTTP/1.1\r\n"
        "Content-Length: 6\r\n"
        "\r\n"
        "fooba"_kj;  // incomplete
    pipe.ends[1]->write(REQUEST.begin(), REQUEST.size()).wait(waitScope);

    auto promise = expectRead(*pipe.ends[1],
        "HTTP/1.1 408 Request Timeout\r\n"
        "Content-Length: 0\r\n"
        "\r\n"_kj);

    KJ_EXPECT(!promise.poll(waitScope));

    // Trigger timeout, then response should be sent.
    timer.advanceTo(timer.now() + 1 * kj::MILLISECONDS);
    KJ_ASSERT(promise.poll(waitScope));
    promise.wait(waitScope);
  }

  // We left our request stream hanging. The server will try to read and discard the request body.
  // Let's give it the rest of the data, followed by a second request.
  {
    static constexpr kj::StringPtr REQUEST =
        "r"
        "GET / HTTP/1.1\r\n"
        "\r\n"_kj;
    pipe.ends[1]->write(REQUEST.begin(), REQUEST.size()).wait(waitScope);

    auto promise = expectRead(*pipe.ends[1],
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n"_kj);
    KJ_ASSERT(promise.poll(waitScope));
    promise.wait(waitScope);
  }
}

KJ_TEST("canceling a chunked stream mid-read correctly discards rest of request") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  ReadCancelHttpService service(timer, table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  {
    static constexpr kj::StringPtr REQUEST =
        "POST / HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "6\r\n"
        "fooba"_kj;  // incomplete chunk
    pipe.ends[1]->write(REQUEST.begin(), REQUEST.size()).wait(waitScope);

    auto promise = expectRead(*pipe.ends[1],
        "HTTP/1.1 408 Request Timeout\r\n"
        "Content-Length: 0\r\n"
        "\r\n"_kj);

    KJ_EXPECT(!promise.poll(waitScope));

    // Trigger timeout, then response should be sent.
    timer.advanceTo(timer.now() + 1 * kj::MILLISECONDS);
    KJ_ASSERT(promise.poll(waitScope));
    promise.wait(waitScope);
  }

  // We left our request stream hanging. The server will try to read and discard the request body.
  // Let's give it the rest of the data, followed by a second request.
  {
    static constexpr kj::StringPtr REQUEST =
        "r\r\n"
        "4a\r\n"
        "this is some text that is the body of a chunk and not a valid chunk header\r\n"
        "0\r\n"
        "\r\n"
        "GET / HTTP/1.1\r\n"
        "\r\n"_kj;
    pipe.ends[1]->write(REQUEST.begin(), REQUEST.size()).wait(waitScope);

    auto promise = expectRead(*pipe.ends[1],
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 0\r\n"
        "\r\n"_kj);
    KJ_ASSERT(promise.poll(waitScope));
    promise.wait(waitScope);
  }
}

KJ_TEST("drain() doesn't lose bytes when called at the wrong moment") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  DummyService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttpCleanDrain(*pipe.ends[0]);

  // Do a regular request.
  static constexpr kj::StringPtr REQUEST =
      "GET / HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "\r\n"_kj;
  pipe.ends[1]->write(REQUEST.begin(), REQUEST.size()).wait(waitScope);
  expectRead(*pipe.ends[1],
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 13\r\n"
      "\r\n"
      "example.com:/"_kj).wait(waitScope);

  // Make sure the server is blocked on the next read from the socket.
  kj::Promise<void>(kj::NEVER_DONE).poll(waitScope);

  // Now simultaneously deliver a new request AND drain the socket.
  auto drainPromise = server.drain();
  static constexpr kj::StringPtr REQUEST2 =
      "GET /foo HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "\r\n"_kj;
  pipe.ends[1]->write(REQUEST2.begin(), REQUEST2.size()).wait(waitScope);

#if KJ_HTTP_TEST_USE_OS_PIPE
  // In the case of an OS pipe, the drain will complete before any data is read from the socket.
  drainPromise.wait(waitScope);

  // The HTTP server should indicate the connection was released but still valid.
  KJ_ASSERT(listenTask.wait(waitScope));

  // The request will not have been read off the socket. We can read it now.
  pipe.ends[1]->shutdownWrite();
  KJ_EXPECT(pipe.ends[0]->readAllText().wait(waitScope) == REQUEST2);

#else
  // In the case of an in-memory pipe, the write() will have delivered bytes directly to the
  // destination buffer synchronously, which means that the server must handle the request
  // before draining.
  KJ_EXPECT(!drainPromise.poll(waitScope));

  // The HTTP request should get a response.
  expectRead(*pipe.ends[1],
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 16\r\n"
      "\r\n"
      "example.com:/foo"_kj).wait(waitScope);

  // Now the drain completes.
  drainPromise.wait(waitScope);

  // The HTTP server should indicate the connection was released but still valid.
  KJ_ASSERT(listenTask.wait(waitScope));
#endif
}

KJ_TEST("drain() does not cancel the first request on a new connection") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  DummyService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttpCleanDrain(*pipe.ends[0]);

  // Request a drain(). It won't complete, because the newly-connected socket is considered to have
  // an in-flight request.
  auto drainPromise = server.drain();
  KJ_EXPECT(!drainPromise.poll(waitScope));

  // Deliver the request.
  static constexpr kj::StringPtr REQUEST2 =
      "GET /foo HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "\r\n"_kj;
  pipe.ends[1]->write(REQUEST2.begin(), REQUEST2.size()).wait(waitScope);

  // It should get a response.
  expectRead(*pipe.ends[1],
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 16\r\n"
      "\r\n"
      "example.com:/foo"_kj).wait(waitScope);

  // Now the drain completes.
  drainPromise.wait(waitScope);

  // The HTTP server should indicate the connection was released but still valid.
  KJ_ASSERT(listenTask.wait(waitScope));
}

KJ_TEST("drain() when NOT using listenHttpCleanDrain() sends Connection: close header") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  HttpHeaderTable table;
  DummyService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Request a drain(). It won't complete, because the newly-connected socket is considered to have
  // an in-flight request.
  auto drainPromise = server.drain();
  KJ_EXPECT(!drainPromise.poll(waitScope));

  // Deliver the request.
  static constexpr kj::StringPtr REQUEST2 =
      "GET /foo HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "\r\n"_kj;
  pipe.ends[1]->write(REQUEST2.begin(), REQUEST2.size()).wait(waitScope);

  // It should get a response.
  expectRead(*pipe.ends[1],
      "HTTP/1.1 200 OK\r\n"
      "Connection: close\r\n"
      "Content-Length: 16\r\n"
      "\r\n"
      "example.com:/foo"_kj).wait(waitScope);

  // And then EOF.
  auto rest = pipe.ends[1]->readAllText();
  KJ_ASSERT(rest.poll(waitScope));
  KJ_EXPECT(rest.wait(waitScope) == nullptr);

  // The drain task and listen task are done.
  drainPromise.wait(waitScope);
  listenTask.wait(waitScope);
}

class BrokenConnectionListener final: public kj::ConnectionReceiver {
public:
  void fulfillOne(kj::Own<kj::AsyncIoStream> stream) {
    fulfiller->fulfill(kj::mv(stream));
  }

  kj::Promise<kj::Own<kj::AsyncIoStream>> accept() override {
    auto paf = kj::newPromiseAndFulfiller<kj::Own<kj::AsyncIoStream>>();
    fulfiller = kj::mv(paf.fulfiller);
    return kj::mv(paf.promise);
  }

  uint getPort() override {
    KJ_UNIMPLEMENTED("not used");
  }

private:
  kj::Own<kj::PromiseFulfiller<kj::Own<kj::AsyncIoStream>>> fulfiller;
};

class BrokenConnection final: public kj::AsyncIoStream {
public:
  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return KJ_EXCEPTION(FAILED, "broken");
  }
  Promise<void> write(const void* buffer, size_t size) override {
    return KJ_EXCEPTION(FAILED, "broken");
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return KJ_EXCEPTION(FAILED, "broken");
  }
  Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }

  void shutdownWrite() override {}
};

KJ_TEST("HttpServer.listenHttp() doesn't prematurely terminate if an accepted connection is broken") {
  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  DummyService service(table);
  HttpServer server(timer, table, service);

  BrokenConnectionListener listener;
  auto promise = server.listenHttp(listener).eagerlyEvaluate(nullptr);

  // Loop is waiting for a connection.
  KJ_ASSERT(!promise.poll(waitScope));

  KJ_EXPECT_LOG(ERROR, "failed: broken");
  listener.fulfillOne(kj::heap<BrokenConnection>());

  // The loop should not have stopped, even though the connection was broken.
  KJ_ASSERT(!promise.poll(waitScope));
}

KJ_TEST("HttpServer handles disconnected exception for clients disconnecting after headers") {
  // This test case reproduces a race condition where a client could disconnect after the server
  // sent response headers but before it sent the response body, resulting in a broken pipe
  // "disconnected" exception when writing the body.  The default handler for application errors
  // tells the server to ignore "disconnected" exceptions and close the connection, but code
  // after the handler exercised the broken connection, causing the server loop to instead fail
  // with a "failed" exception.

  KJ_HTTP_TEST_SETUP_IO;
  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  class SendErrorHttpService final: public HttpService {
    // HttpService that serves an error page via sendError().
  public:
    SendErrorHttpService(HttpHeaderTable& headerTable): headerTable(headerTable) {}
    kj::Promise<void> request(
        HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
        kj::AsyncInputStream& requestBody, Response& responseSender) override {
      return responseSender.sendError(404, "Not Found", headerTable);
    }

  private:
    HttpHeaderTable& headerTable;
  };

  class DisconnectingAsyncIoStream final: public kj::AsyncIoStream {
  public:
    DisconnectingAsyncIoStream(AsyncIoStream& inner): inner(inner) {}

    Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
      return inner.read(buffer, minBytes, maxBytes);
    }
    Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
      return inner.tryRead(buffer, minBytes, maxBytes);
    }

    Maybe<uint64_t> tryGetLength() override { return inner.tryGetLength(); }

    Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
      return inner.pumpTo(output, amount);
    }

    Promise<void> write(const void* buffer, size_t size) override {
      int writeId = writeCount++;
      if (writeId == 0) {
        // Allow first write (headers) to succeed.
        auto promise = inner.write(buffer, size);
        inner.shutdownWrite();
        return promise;
      } else if (writeId == 1) {
        // Fail subsequent write (body) with a disconnected exception.
        return KJ_EXCEPTION(DISCONNECTED, "a_disconnected_exception");
      } else {
        KJ_FAIL_ASSERT("Unexpected write");
      }
    }
    Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
      return inner.write(pieces);
    }

    Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
      return inner.tryPumpFrom(input, amount);
    }

    Promise<void> whenWriteDisconnected() override {
      return inner.whenWriteDisconnected();
    }

    void shutdownWrite() override {
      return inner.shutdownWrite();
    }

    void abortRead() override {
      return inner.abortRead();
    }

    void getsockopt(int level, int option, void* value, uint* length) override {
      return inner.getsockopt(level, option, value, length);
    }
    void setsockopt(int level, int option, const void* value, uint length) override {
      return inner.setsockopt(level, option, value, length);
    }

    void getsockname(struct sockaddr* addr, uint* length) override {
      return inner.getsockname(addr, length);
    }
    void getpeername(struct sockaddr* addr, uint* length) override {
      return inner.getsockname(addr, length);
    }

    int writeCount = 0;

  private:
    kj::AsyncIoStream& inner;
  };

  class TestErrorHandler: public HttpServerErrorHandler {
  public:
    kj::Promise<void> handleApplicationError(
        kj::Exception exception, kj::Maybe<kj::HttpService::Response&> response) override {
      applicationErrorCount++;
      if (exception.getType() == kj::Exception::Type::DISCONNECTED) {
        // Tell HttpServer to ignore disconnected exceptions (the default behavior).
        return kj::READY_NOW;
      }
      KJ_FAIL_ASSERT("Unexpected application error type", exception.getType());
    }

    int applicationErrorCount = 0;
  };

  TestErrorHandler testErrorHandler;
  HttpServerSettings settings {};
  settings.errorHandler = testErrorHandler;

  HttpHeaderTable table;
  SendErrorHttpService service(table);
  HttpServer server(timer, table, service, settings);

  auto stream = kj::heap<DisconnectingAsyncIoStream>(*pipe.ends[0]);
  auto listenPromise = server.listenHttpCleanDrain(*stream);

  static constexpr auto request = "GET / HTTP/1.1\r\n\r\n"_kj;
  pipe.ends[1]->write(request.begin(), request.size()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  // Client races to read headers but not body, then disconnects.  (Note that the following code
  // doesn't reliably reproduce the race condition by itself -- DisconnectingAsyncIoStream is
  // needed to ensure the disconnected exception throws on the correct write promise.)
  expectRead(*pipe.ends[1],
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: 9\r\n"
    "\r\n"_kj).wait(waitScope);
  pipe.ends[1] = nullptr;

  // The race condition failure would manifest as a "previous HTTP message body incomplete"
  // "FAILED" exception here:
  bool canReuse = listenPromise.wait(waitScope);

  KJ_ASSERT(!canReuse);
  KJ_ASSERT(stream->writeCount == 2);
  KJ_ASSERT(testErrorHandler.applicationErrorCount == 1);
}

// =======================================================================================
// CONNECT tests

class ConnectEchoService final: public HttpService {
    // A simple CONNECT echo. It will always accept, and whatever data it
    // receives will be echoed back.
public:
  ConnectEchoService(HttpHeaderTable& headerTable, uint statusCodeToSend = 200)
      : headerTable(headerTable),
        statusCodeToSend(statusCodeToSend) {
    KJ_ASSERT(statusCodeToSend >= 200 && statusCodeToSend < 300);
  }

  uint connectCount = 0;

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    KJ_UNIMPLEMENTED("Regular HTTP requests are not implemented here.");
  }

  kj::Promise<void> connect(kj::StringPtr host,
                            const HttpHeaders& headers,
                            kj::AsyncIoStream& connection,
                            ConnectResponse& response,
                            kj::HttpConnectSettings settings) override {
    connectCount++;
    response.accept(statusCodeToSend, "OK", HttpHeaders(headerTable));
    return connection.pumpTo(connection).ignoreResult();
  }

private:
  HttpHeaderTable& headerTable;
  uint statusCodeToSend;
};

class ConnectRejectService final: public HttpService {
  // A simple CONNECT implementation that always rejects.
public:
  ConnectRejectService(HttpHeaderTable& headerTable, uint statusCodeToSend = 400)
      : headerTable(headerTable),
        statusCodeToSend(statusCodeToSend) {
    KJ_ASSERT(statusCodeToSend >= 300);
  }

  uint connectCount = 0;

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    KJ_UNIMPLEMENTED("Regular HTTP requests are not implemented here.");
  }

  kj::Promise<void> connect(kj::StringPtr host,
                            const HttpHeaders& headers,
                            kj::AsyncIoStream& connection,
                            ConnectResponse& response,
                            kj::HttpConnectSettings settings) override {
    connectCount++;
    auto out = response.reject(statusCodeToSend, "Failed"_kj, HttpHeaders(headerTable), 4);
    return out->write("boom", 4).attach(kj::mv(out));
  }

private:
  HttpHeaderTable& headerTable;
  uint statusCodeToSend;
};

class ConnectCancelReadService final: public HttpService {
    // A simple CONNECT server that will accept a connection then immediately
    // cancel reading from it to test handling of abrupt termination.
public:
  ConnectCancelReadService(HttpHeaderTable& headerTable)
      : headerTable(headerTable) {}

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    KJ_UNIMPLEMENTED("Regular HTTP requests are not implemented here.");
  }

  kj::Promise<void> connect(kj::StringPtr host,
                            const HttpHeaders& headers,
                            kj::AsyncIoStream& connection,
                            ConnectResponse& response,
                            kj::HttpConnectSettings settings) override {
    response.accept(200, "OK", HttpHeaders(headerTable));
    // Return an immediately resolved promise and drop the connection
    return kj::READY_NOW;
  }

private:
  HttpHeaderTable& headerTable;
};

class ConnectCancelWriteService final: public HttpService {
    // A simple CONNECT server that will accept a connection then immediately
    // cancel writing to it to test handling of abrupt termination.
public:
  ConnectCancelWriteService(HttpHeaderTable& headerTable)
      : headerTable(headerTable) {}

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    KJ_UNIMPLEMENTED("Regular HTTP requests are not implemented here.");
  }

  kj::Promise<void> connect(kj::StringPtr host,
                            const HttpHeaders& headers,
                            kj::AsyncIoStream& connection,
                            ConnectResponse& response,
                            kj::HttpConnectSettings settings) override {
    response.accept(200, "OK", HttpHeaders(headerTable));

    auto msg = "hello"_kj;
    auto promise KJ_UNUSED = connection.write(msg.begin(), 5);

    // Return an immediately resolved promise and drop the io
    return kj::READY_NOW;
  }

private:
  HttpHeaderTable& headerTable;
};

class ConnectHttpService final: public HttpService {
  // A CONNECT service that tunnels HTTP requests just to verify that, yes, the CONNECT
  // impl can actually tunnel actual protocols.
public:
  ConnectHttpService(HttpHeaderTable& table)
      : timer(kj::origin<kj::TimePoint>()),
        tunneledService(table),
        server(timer, table, tunneledService) {}
private:

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    KJ_UNIMPLEMENTED("Regular HTTP requests are not implemented here.");
  }

  kj::Promise<void> connect(kj::StringPtr host,
                            const HttpHeaders& headers,
                            kj::AsyncIoStream& connection,
                            ConnectResponse& response,
                            kj::HttpConnectSettings settings) override {
    response.accept(200, "OK", HttpHeaders(tunneledService.table));
    return server.listenHttp(kj::Own<kj::AsyncIoStream>(&connection, kj::NullDisposer::instance));
  }

  class SimpleHttpService final: public HttpService {
  public:
    SimpleHttpService(HttpHeaderTable& table) : table(table) {}
    kj::Promise<void> request(
        HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
        kj::AsyncInputStream& requestBody, Response& response) override {
      auto out = response.send(200, "OK"_kj, HttpHeaders(table));
      auto msg = "hello there"_kj;
      return out->write(msg.begin(), 11).attach(kj::mv(out));
    }

    HttpHeaderTable& table;
  };

  kj::TimerImpl timer;
  SimpleHttpService tunneledService;
  HttpServer server;
};

class ConnectCloseService final: public HttpService {
  // A simple CONNECT server that will accept a connection then immediately
  // shutdown the write side of the AsyncIoStream to simulate socket disconnection.
public:
  ConnectCloseService(HttpHeaderTable& headerTable)
      : headerTable(headerTable) {}

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    KJ_UNIMPLEMENTED("Regular HTTP requests are not implemented here.");
  }

  kj::Promise<void> connect(kj::StringPtr host,
                            const HttpHeaders& headers,
                            kj::AsyncIoStream& connection,
                            ConnectResponse& response,
                            kj::HttpConnectSettings settings) override {
    response.accept(200, "OK", HttpHeaders(headerTable));
    connection.shutdownWrite();
    return kj::READY_NOW;
  }

private:
  HttpHeaderTable& headerTable;
};

KJ_TEST("Simple CONNECT Server works") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  ConnectEchoService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto msg = "CONNECT https://example.org HTTP/1.1\r\n"
             "\r\n"
             "hello"_kj;

  pipe.ends[1]->write(msg.begin(), msg.size()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 200 OK\r\n"
             "\r\n"
             "hello"_kj).wait(waitScope);

  expectEnd(*pipe.ends[1]);

  listenTask.wait(waitScope);

  KJ_ASSERT(service.connectCount == 1);
}

KJ_TEST("Simple CONNECT Client/Server works") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  ConnectEchoService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);

  HttpHeaderTable clientHeaders;
  // Initiates a CONNECT with the echo server. Once established, sends a bit of data
  // and waits for it to be echoed back.
  auto request = client->connect(
      "https://example.org"_kj, HttpHeaders(clientHeaders), {});

  request.status.then([io=kj::mv(request.connection)](auto status) mutable {
    KJ_ASSERT(status.statusCode == 200);
    KJ_ASSERT(status.statusText == "OK"_kj);

    auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);
    promises.add(io->write("hello", 5));
    promises.add(expectRead(*io, "hello"_kj));
    return kj::joinPromises(promises.finish())
        .then([io=kj::mv(io)]() mutable {
      io->shutdownWrite();
      return expectEnd(*io).attach(kj::mv(io));
    });
  }).wait(waitScope);

  listenTask.wait(waitScope);

  KJ_ASSERT(service.connectCount == 1);
}

KJ_TEST("CONNECT Server (201 status)") {
  KJ_HTTP_TEST_SETUP_IO;

  // Test that CONNECT works with 2xx status codes that typically do
  // not carry a response payload.

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  ConnectEchoService service(table, 201);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto msg = "CONNECT https://example.org HTTP/1.1\r\n"
             "\r\n"
             "hello"_kj;

  pipe.ends[1]->write(msg.begin(), msg.size()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 201 OK\r\n"
             "\r\n"
             "hello"_kj).wait(waitScope);

  expectEnd(*pipe.ends[1]);

  listenTask.wait(waitScope);

  KJ_ASSERT(service.connectCount == 1);
}

KJ_TEST("CONNECT Client (204 status)") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  // Test that CONNECT works with 2xx status codes that typically do
  // not carry a response payload.

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  ConnectEchoService service(table, 204);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);

  HttpHeaderTable clientHeaders;
  // Initiates a CONNECT with the echo server. Once established, sends a bit of data
  // and waits for it to be echoed back.
  auto request = client->connect(
      "https://example.org"_kj, HttpHeaders(clientHeaders), {});

  request.status.then([io=kj::mv(request.connection)](auto status) mutable {
    KJ_ASSERT(status.statusCode == 204);
    KJ_ASSERT(status.statusText == "OK"_kj);

    auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);
    promises.add(io->write("hello", 5));
    promises.add(expectRead(*io, "hello"_kj));

    return kj::joinPromises(promises.finish())
        .then([io=kj::mv(io)]() mutable {
      io->shutdownWrite();
      return expectEnd(*io).attach(kj::mv(io));
    });
  }).wait(waitScope);

  listenTask.wait(waitScope);

  KJ_ASSERT(service.connectCount == 1);
}

KJ_TEST("CONNECT Server rejected") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  ConnectRejectService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto msg = "CONNECT https://example.org HTTP/1.1\r\n"
             "\r\n"
             "hello"_kj;

  pipe.ends[1]->write(msg.begin(), msg.size()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 400 Failed\r\n"
             "Connection: close\r\n"
             "Content-Length: 4\r\n"
             "\r\n"
             "boom"_kj).wait(waitScope);

  expectEnd(*pipe.ends[1]);

  listenTask.wait(waitScope);

  KJ_ASSERT(service.connectCount == 1);
}

#ifndef KJ_HTTP_TEST_USE_OS_PIPE
KJ_TEST("CONNECT Client rejected") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  ConnectRejectService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);

  HttpHeaderTable clientHeaders;
  auto request = client->connect(
      "https://example.org"_kj, HttpHeaders(clientHeaders), {});

  request.status.then([](auto status) mutable {
    KJ_ASSERT(status.statusCode == 400);
    KJ_ASSERT(status.statusText == "Failed"_kj);

    auto& errorBody = KJ_ASSERT_NONNULL(status.errorBody);

    return expectRead(*errorBody, "boom"_kj).then([&errorBody=*errorBody]() {
      return expectEnd(errorBody);
    }).attach(kj::mv(errorBody));
  }).wait(waitScope);

  listenTask.wait(waitScope);

  KJ_ASSERT(service.connectCount == 1);
}
#endif

KJ_TEST("CONNECT Server cancels read") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  ConnectCancelReadService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto msg = "CONNECT https://example.org HTTP/1.1\r\n"
             "\r\n"
             "hello"_kj;

  pipe.ends[1]->write(msg.begin(), msg.size()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 200 OK\r\n"
             "\r\n"_kj).wait(waitScope);

  expectEnd(*pipe.ends[1]);

  listenTask.wait(waitScope);
}

#ifndef KJ_HTTP_TEST_USE_OS_PIPE
KJ_TEST("CONNECT Server cancels read w/client") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  ConnectCancelReadService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);
  bool failed = false;

  HttpHeaderTable clientHeaders;
  auto request = client->connect(
      "https://example.org"_kj, HttpHeaders(clientHeaders), {});

  request.status.then([&failed, io=kj::mv(request.connection)](auto status) mutable {
    KJ_ASSERT(status.statusCode == 200);
    KJ_ASSERT(status.statusText == "OK"_kj);

    return io->write("hello", 5).catch_([&](kj::Exception&& ex) {
      KJ_ASSERT(ex.getType() == kj::Exception::Type::DISCONNECTED);
      failed = true;
    }).attach(kj::mv(io));
  }).wait(waitScope);

  KJ_ASSERT(failed, "the write promise should have failed");

  listenTask.wait(waitScope);
}
#endif

KJ_TEST("CONNECT Server cancels write") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  ConnectCancelWriteService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto msg = "CONNECT https://example.org HTTP/1.1\r\n"
             "\r\n"
             "hello"_kj;

  pipe.ends[1]->write(msg.begin(), msg.size()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 200 OK\r\n"
             "\r\n"_kj).wait(waitScope);

  expectEnd(*pipe.ends[1]);

  listenTask.wait(waitScope);
}

#ifndef KJ_HTTP_TEST_USE_OS_PIPE
KJ_TEST("CONNECT Server cancels write w/client") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  ConnectCancelWriteService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);

  HttpHeaderTable clientHeaders;
  bool failed = false;
  auto request = client->connect(
      "https://example.org"_kj, HttpHeaders(clientHeaders), {});

  request.status.then([&failed, io=kj::mv(request.connection)](auto status) mutable {
    KJ_ASSERT(status.statusCode == 200);
    KJ_ASSERT(status.statusText == "OK"_kj);

    return io->write("hello", 5).catch_([&failed](kj::Exception&& ex) mutable {
      KJ_ASSERT(ex.getType() == kj::Exception::Type::DISCONNECTED);
      failed = true;
    }).attach(kj::mv(io));
  }).wait(waitScope);

  KJ_ASSERT(failed, "the write promise should have failed");

  listenTask.wait(waitScope);
}
#endif

KJ_TEST("CONNECT rejects Transfer-Encoding") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  ConnectEchoService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto msg = "CONNECT https://example.org HTTP/1.1\r\n"
             "Transfer-Encoding: chunked\r\n"
             "\r\n"
             "5\r\n"
             "hello"
             "0\r\n"_kj;

  pipe.ends[1]->write(msg.begin(), msg.size()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 400 Bad Request\r\n"
             "Connection: close\r\n"
             "Content-Length: 18\r\n"
             "Content-Type: text/plain\r\n"
             "\r\n"
             "ERROR: Bad Request"_kj).wait(waitScope);

  expectEnd(*pipe.ends[1]);

  listenTask.wait(waitScope);
}

KJ_TEST("CONNECT rejects Content-Length") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());

  HttpHeaderTable table;
  ConnectEchoService service(table);
  HttpServer server(timer, table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto msg = "CONNECT https://example.org HTTP/1.1\r\n"
             "Content-Length: 5\r\n"
             "\r\n"
             "hello"_kj;

  pipe.ends[1]->write(msg.begin(), msg.size()).wait(waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1],
             "HTTP/1.1 400 Bad Request\r\n"
             "Connection: close\r\n"
             "Content-Length: 18\r\n"
             "Content-Type: text/plain\r\n"
             "\r\n"
             "ERROR: Bad Request"_kj).wait(waitScope);

  expectEnd(*pipe.ends[1]);

  listenTask.wait(waitScope);
}

KJ_TEST("CONNECT HTTP-tunneled-over-CONNECT") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  HttpHeaderTable table;
  ConnectHttpService service(table);
  HttpServer server(timer, table, service);

  auto listenTask KJ_UNUSED = server.listenHttp(kj::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);

  HttpHeaderTable connectHeaderTable;
  HttpHeaderTable tunneledHeaderTable;
  HttpClientSettings settings;

  auto request = client->connect(
      "https://example.org"_kj, HttpHeaders(connectHeaderTable), {});

  auto text = request.status.then([
      &tunneledHeaderTable,
      &settings,
      io=kj::mv(request.connection)](auto status) mutable {
    KJ_ASSERT(status.statusCode == 200);
    KJ_ASSERT(status.statusText == "OK"_kj);
    auto client = newHttpClient(tunneledHeaderTable, *io, settings)
        .attach(kj::mv(io));

    return client->request(HttpMethod::GET, "http://example.org"_kj,
                           HttpHeaders(tunneledHeaderTable))
        .response.then([](HttpClient::Response&& response) {
      return response.body->readAllText().attach(kj::mv(response));
    }).attach(kj::mv(client));
  }).wait(waitScope);

  KJ_ASSERT(text == "hello there");
}

KJ_TEST("CONNECT HTTP-tunneled-over-pipelined-CONNECT") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  HttpHeaderTable table;
  ConnectHttpService service(table);
  HttpServer server(timer, table, service);

  auto listenTask KJ_UNUSED = server.listenHttp(kj::mv(pipe.ends[0]));

  auto client = newHttpClient(table, *pipe.ends[1]);

  HttpHeaderTable connectHeaderTable;
  HttpHeaderTable tunneledHeaderTable;
  HttpClientSettings settings;

  auto request = client->connect(
      "https://exmaple.org"_kj, HttpHeaders(connectHeaderTable), {});
  auto conn = kj::mv(request.connection);
  auto proxyClient = newHttpClient(tunneledHeaderTable, *conn, settings).attach(kj::mv(conn));

  auto get = proxyClient->request(HttpMethod::GET,
                                  "http://example.org"_kj,
                                  HttpHeaders(tunneledHeaderTable));
  auto text = get.response.then([](HttpClient::Response&& response) mutable {
    return response.body->readAllText().attach(kj::mv(response));
  }).attach(kj::mv(proxyClient)).wait(waitScope);

  KJ_ASSERT(text == "hello there");
}

KJ_TEST("CONNECT pipelined via an adapter") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  HttpHeaderTable table;
  ConnectHttpService service(table);
  HttpServer server(timer, table, service);

  auto listenTask KJ_UNUSED = server.listenHttp(kj::mv(pipe.ends[0]));

  bool acceptCalled = false;

  auto client = newHttpClient(table, *pipe.ends[1]);
  auto adaptedService = kj::newHttpService(*client).attach(kj::mv(client));

  // adaptedService is an HttpService that wraps an HttpClient that sends
  // a request to server.

  auto clientPipe = newTwoWayPipe();

  struct ResponseImpl final: public HttpService::ConnectResponse {
    bool& acceptCalled;
    ResponseImpl(bool& acceptCalled) : acceptCalled(acceptCalled) {}
    void accept(uint statusCode, kj::StringPtr statusText, const HttpHeaders& headers) override {
      acceptCalled = true;
    }

    kj::Own<kj::AsyncOutputStream> reject(
        uint statusCode,
        kj::StringPtr statusText,
        const HttpHeaders& headers,
        kj::Maybe<uint64_t> expectedBodySize) override {
      KJ_UNREACHABLE;
    }
  };

  ResponseImpl response(acceptCalled);

  HttpHeaderTable connectHeaderTable;
  HttpHeaderTable tunneledHeaderTable;
  HttpClientSettings settings;

  auto promise = adaptedService->connect("https://example.org"_kj,
                                         HttpHeaders(connectHeaderTable),
                                         *clientPipe.ends[0],
                                         response,
                                         {}).attach(kj::mv(clientPipe.ends[0]));

  auto proxyClient = newHttpClient(tunneledHeaderTable, *clientPipe.ends[1], settings)
      .attach(kj::mv(clientPipe.ends[1]));

  auto text = proxyClient->request(HttpMethod::GET,
                          "http://example.org"_kj,
                          HttpHeaders(tunneledHeaderTable))
      .response.then([](HttpClient::Response&& response) mutable {
    return response.body->readAllText().attach(kj::mv(response));
  }).wait(waitScope);

  KJ_ASSERT(acceptCalled);
  KJ_ASSERT(text == "hello there");
}

KJ_TEST("CONNECT pipelined via an adapter (reject)") {
  KJ_HTTP_TEST_SETUP_IO;

  auto pipe = KJ_HTTP_TEST_CREATE_2PIPE;

  kj::TimerImpl timer(kj::origin<kj::TimePoint>());
  HttpHeaderTable table;
  ConnectRejectService service(table);
  HttpServer server(timer, table, service);

  auto listenTask KJ_UNUSED = server.listenHttp(kj::mv(pipe.ends[0]));

  bool rejectCalled = false;
  bool failedAsExpected = false;

  auto client = newHttpClient(table, *pipe.ends[1]);
  auto adaptedService = kj::newHttpService(*client).attach(kj::mv(client));

  // adaptedService is an HttpService that wraps an HttpClient that sends
  // a request to server.

  auto clientPipe = newTwoWayPipe();

  struct ResponseImpl final: public HttpService::ConnectResponse {
    bool& rejectCalled;
    kj::OneWayPipe pipe;
    ResponseImpl(bool& rejectCalled)
        : rejectCalled(rejectCalled),
          pipe(kj::newOneWayPipe()) {}
    void accept(uint statusCode, kj::StringPtr statusText, const HttpHeaders& headers) override {
      KJ_UNREACHABLE;
    }

    kj::Own<kj::AsyncOutputStream> reject(
        uint statusCode,
        kj::StringPtr statusText,
        const HttpHeaders& headers,
        kj::Maybe<uint64_t> expectedBodySize) override {
      rejectCalled = true;
      return kj::mv(pipe.out);
    }

    kj::Own<kj::AsyncInputStream> getRejectStream() {
      return kj::mv(pipe.in);
    }
  };

  ResponseImpl response(rejectCalled);

  HttpHeaderTable connectHeaderTable;
  HttpHeaderTable tunneledHeaderTable;
  HttpClientSettings settings;

  auto promise = adaptedService->connect("https://example.org"_kj,
                                         HttpHeaders(connectHeaderTable),
                                         *clientPipe.ends[0],
                                         response,
                                         {}).attach(kj::mv(clientPipe.ends[0]));

  auto proxyClient = newHttpClient(tunneledHeaderTable, *clientPipe.ends[1], settings)
      .attach(kj::mv(clientPipe.ends[1]));

  auto text = proxyClient->request(HttpMethod::GET,
                       "http://example.org"_kj,
                       HttpHeaders(tunneledHeaderTable))
      .response.then([](HttpClient::Response&& response) mutable {
    return response.body->readAllText().attach(kj::mv(response));
  }, [&](kj::Exception&& ex) -> kj::Promise<kj::String> {
    // We fully expect the stream to fail here.
    if (ex.getDescription() == "stream disconnected prematurely") {
      failedAsExpected = true;
    }
    return kj::str("ok");
  }).wait(waitScope);

  auto rejectStream = response.getRejectStream();

#ifndef KJ_HTTP_TEST_USE_OS_PIPE
  expectRead(*rejectStream, "boom"_kj).wait(waitScope);
#endif

  KJ_ASSERT(rejectCalled);
  KJ_ASSERT(failedAsExpected);
  KJ_ASSERT(text == "ok");
}

}  // namespace
}  // namespace kj
