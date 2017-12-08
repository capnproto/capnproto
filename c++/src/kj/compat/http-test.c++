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
#include <map>

namespace kj {
namespace {

KJ_TEST("HttpMethod parse / stringify") {
#define TRY(name) \
  KJ_EXPECT(kj::str(HttpMethod::name) == #name); \
  KJ_IF_MAYBE(parsed, tryParseHttpMethod(#name)) { \
    KJ_EXPECT(*parsed == HttpMethod::name); \
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
      "\r\n");
  auto result = KJ_ASSERT_NONNULL(headers.tryParseRequest(text.asArray()));

  KJ_EXPECT(result.method == HttpMethod::POST);
  KJ_EXPECT(result.url == "/some/path");
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(HttpHeaderId::HOST)) == "example.com");
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(HttpHeaderId::DATE)) == "early");
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(fooBar)) == "Baz");
  KJ_EXPECT(headers.get(bazQux) == nullptr);
  KJ_EXPECT(headers.get(HttpHeaderId::CONTENT_TYPE) == nullptr);
  KJ_EXPECT(result.connectionHeaders.contentLength == "123");
  KJ_EXPECT(result.connectionHeaders.transferEncoding == nullptr);

  std::map<kj::StringPtr, kj::StringPtr> unpackedHeaders;
  headers.forEach([&](kj::StringPtr name, kj::StringPtr value) {
    KJ_EXPECT(unpackedHeaders.insert(std::make_pair(name, value)).second);
  });
  KJ_EXPECT(unpackedHeaders.size() == 4);
  KJ_EXPECT(unpackedHeaders["Host"] == "example.com");
  KJ_EXPECT(unpackedHeaders["Date"] == "early");
  KJ_EXPECT(unpackedHeaders["Foo-Bar"] == "Baz");
  KJ_EXPECT(unpackedHeaders["other-Header"] == "yep");

  KJ_EXPECT(headers.serializeRequest(result.method, result.url, result.connectionHeaders) ==
      "POST /some/path HTTP/1.1\r\n"
      "Content-Length: 123\r\n"
      "Host: example.com\r\n"
      "Date: early\r\n"
      "Foo-Bar: Baz\r\n"
      "other-Header: yep\r\n"
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
  auto result = KJ_ASSERT_NONNULL(headers.tryParseResponse(text.asArray()));

  KJ_EXPECT(result.statusCode == 418);
  KJ_EXPECT(result.statusText == "I'm a teapot");
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(HttpHeaderId::HOST)) == "example.com");
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(HttpHeaderId::DATE)) == "early");
  KJ_EXPECT(KJ_ASSERT_NONNULL(headers.get(fooBar)) == "Baz");
  KJ_EXPECT(headers.get(bazQux) == nullptr);
  KJ_EXPECT(headers.get(HttpHeaderId::CONTENT_TYPE) == nullptr);
  KJ_EXPECT(result.connectionHeaders.contentLength == "123");
  KJ_EXPECT(result.connectionHeaders.transferEncoding == nullptr);

  std::map<kj::StringPtr, kj::StringPtr> unpackedHeaders;
  headers.forEach([&](kj::StringPtr name, kj::StringPtr value) {
    KJ_EXPECT(unpackedHeaders.insert(std::make_pair(name, value)).second);
  });
  KJ_EXPECT(unpackedHeaders.size() == 4);
  KJ_EXPECT(unpackedHeaders["Host"] == "example.com");
  KJ_EXPECT(unpackedHeaders["Date"] == "early");
  KJ_EXPECT(unpackedHeaders["Foo-Bar"] == "Baz");
  KJ_EXPECT(unpackedHeaders["other-Header"] == "yep");

  KJ_EXPECT(headers.serializeResponse(
        result.statusCode, result.statusText, result.connectionHeaders) ==
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
  KJ_EXPECT(headers.tryParseRequest(kj::heapString(
      "POST  \0 /some/path \t   HTTP/1.1\r\n"
      "Foo-BaR: Baz\r\n"
      "Host: example.com\r\n"
      "DATE:     early\r\n"
      "other-Header: yep\r\n"
      "\r\n")) == nullptr);

  // Control character in header name.
  KJ_EXPECT(headers.tryParseRequest(kj::heapString(
      "POST   /some/path \t   HTTP/1.1\r\n"
      "Foo-BaR: Baz\r\n"
      "Cont\001ent-Length: 123\r\n"
      "DATE:     early\r\n"
      "other-Header: yep\r\n"
      "\r\n")) == nullptr);

  // Separator character in header name.
  KJ_EXPECT(headers.tryParseRequest(kj::heapString(
      "POST   /some/path \t   HTTP/1.1\r\n"
      "Foo-BaR: Baz\r\n"
      "Host: example.com\r\n"
      "DATE/:     early\r\n"
      "other-Header: yep\r\n"
      "\r\n")) == nullptr);

  // Response status code not numeric.
  KJ_EXPECT(headers.tryParseResponse(kj::heapString(
      "HTTP/1.1\t\t  abc\t    I'm a teapot\r\n"
      "Foo-BaR: Baz\r\n"
      "Host: example.com\r\n"
      "DATE:     early\r\n"
      "other-Header: yep\r\n"
      "\r\n")) == nullptr);
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
  return promise.then(kj::mvCapture(buffer, [&in,expected](kj::Array<char> buffer, size_t amount) {
    if (amount == 0) {
      KJ_FAIL_ASSERT("expected data never sent", expected);
    }

    auto actual = buffer.slice(0, amount);
    if (memcmp(actual.begin(), expected.begin(), actual.size()) != 0) {
      KJ_FAIL_ASSERT("data from stream doesn't match expected", expected, actual);
    }

    return expectRead(in, expected.slice(amount));
  }));
}

kj::Promise<void> expectRead(kj::AsyncInputStream& in, kj::ArrayPtr<const byte> expected) {
  if (expected.size() == 0) return kj::READY_NOW;

  auto buffer = kj::heapArray<byte>(expected.size());

  auto promise = in.tryRead(buffer.begin(), 1, buffer.size());
  return promise.then(kj::mvCapture(buffer, [&in,expected](kj::Array<byte> buffer, size_t amount) {
    if (amount == 0) {
      KJ_FAIL_ASSERT("expected data never sent", expected);
    }

    auto actual = buffer.slice(0, amount);
    if (memcmp(actual.begin(), expected.begin(), actual.size()) != 0) {
      KJ_FAIL_ASSERT("data from stream doesn't match expected", expected, actual);
    }

    return expectRead(in, expected.slice(amount, expected.size()));
  }));
}

void testHttpClientRequest(kj::AsyncIoContext& io, const HttpRequestTestCase& testCase) {
  auto pipe = io.provider->newTwoWayPipe();

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
    writeEach(*request.body, testCase.requestBodyParts).wait(io.waitScope);
  }
  request.body = nullptr;
  auto clientTask = request.response
      .then([&](HttpClient::Response&& response) {
    auto promise = response.body->readAllText();
    return promise.attach(kj::mv(response.body));
  }).ignoreResult();

  serverTask.exclusiveJoin(kj::mv(clientTask)).wait(io.waitScope);

  // Verify no more data written by client.
  client = nullptr;
  pipe.ends[0]->shutdownWrite();
  KJ_EXPECT(pipe.ends[1]->readAllText().wait(io.waitScope) == "");
}

void testHttpClientResponse(kj::AsyncIoContext& io, const HttpResponseTestCase& testCase,
                            size_t readFragmentSize) {
  auto pipe = io.provider->newTwoWayPipe();
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

  serverTask.exclusiveJoin(kj::mv(clientTask)).wait(io.waitScope);

  // Verify no more data written by client.
  client = nullptr;
  pipe.ends[0]->shutdownWrite();
  KJ_EXPECT(pipe.ends[1]->readAllText().wait(io.waitScope) == "");
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

void testHttpServerRequest(kj::AsyncIoContext& io,
                           const HttpRequestTestCase& requestCase,
                           const HttpResponseTestCase& responseCase) {
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable table;
  TestHttpService service(requestCase, responseCase, table);
  HttpServer server(io.provider->getTimer(), table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  pipe.ends[1]->write(requestCase.raw.begin(), requestCase.raw.size()).wait(io.waitScope);
  pipe.ends[1]->shutdownWrite();

  expectRead(*pipe.ends[1], responseCase.raw).wait(io.waitScope);

  listenTask.wait(io.waitScope);

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
  auto io = kj::setupAsyncIo();

  for (auto& testCase: requestTestCases()) {
    if (testCase.side == SERVER_ONLY) continue;
    KJ_CONTEXT(testCase.raw);
    testHttpClientRequest(io, testCase);
  }
}

KJ_TEST("HttpClient responses") {
  auto io = kj::setupAsyncIo();
  size_t FRAGMENT_SIZES[] = { 1, 2, 3, 4, 5, 6, 7, 8, 16, 31, kj::maxValue };

  for (auto& testCase: responseTestCases()) {
    if (testCase.side == SERVER_ONLY) continue;
    for (size_t fragmentSize: FRAGMENT_SIZES) {
      KJ_CONTEXT(testCase.raw, fragmentSize);
      testHttpClientResponse(io, testCase, fragmentSize);
    }
  }
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

  auto io = kj::setupAsyncIo();

  for (auto& testCase: requestTestCases()) {
    if (testCase.side == CLIENT_ONLY) continue;
    KJ_CONTEXT(testCase.raw);
    testHttpServerRequest(io, testCase,
        testCase.method == HttpMethod::HEAD ? HEAD_RESPONSE : RESPONSE);
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

  auto io = kj::setupAsyncIo();

  for (auto& testCase: responseTestCases()) {
    if (testCase.side == CLIENT_ONLY) continue;
    KJ_CONTEXT(testCase.raw);
    testHttpServerRequest(io,
        testCase.method == HttpMethod::HEAD ? HEAD_REQUEST : REQUEST, testCase);
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
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents RESPONSE_TEST_CASES from implicitly
  //   casting to our return type.
  return kj::arrayPtr(PIPELINE_TESTS, kj::size(PIPELINE_TESTS));
}

KJ_TEST("HttpClient pipeline") {
  auto PIPELINE_TESTS = pipelineTestCases();

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

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
    KJ_CONTEXT(testCase.request.raw, testCase.response.raw);

    HttpHeaders headers(table);
    for (auto& header: testCase.request.requestHeaders) {
      headers.set(header.id, header.value);
    }

    auto request = client->request(
        testCase.request.method, testCase.request.path, headers, testCase.request.requestBodySize);
    for (auto& part: testCase.request.requestBodyParts) {
      request.body->write(part.begin(), part.size()).wait(io.waitScope);
    }
    request.body = nullptr;

    auto response = request.response.wait(io.waitScope);

    KJ_EXPECT(response.statusCode == testCase.response.statusCode);
    auto body = response.body->readAllText().wait(io.waitScope);
    if (testCase.request.method == HttpMethod::HEAD) {
      KJ_EXPECT(body == "");
    } else {
      KJ_EXPECT(body == kj::strArray(testCase.response.responseBodyParts, ""), body);
    }
  }

  client = nullptr;
  pipe.ends[0]->shutdownWrite();

  writeResponsesPromise.wait(io.waitScope);
}

KJ_TEST("HttpClient parallel pipeline") {
  auto PIPELINE_TESTS = pipelineTestCases();

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

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

  auto responsePromises = KJ_MAP(testCase, PIPELINE_TESTS) {
    KJ_CONTEXT(testCase.request.raw, testCase.response.raw);

    HttpHeaders headers(table);
    for (auto& header: testCase.request.requestHeaders) {
      headers.set(header.id, header.value);
    }

    auto request = client->request(
        testCase.request.method, testCase.request.path, headers, testCase.request.requestBodySize);
    for (auto& part: testCase.request.requestBodyParts) {
      request.body->write(part.begin(), part.size()).wait(io.waitScope);
    }

    return kj::mv(request.response);
  };

  for (auto i: kj::indices(PIPELINE_TESTS)) {
    auto& testCase = PIPELINE_TESTS[i];
    auto response = responsePromises[i].wait(io.waitScope);

    KJ_EXPECT(response.statusCode == testCase.response.statusCode);
    auto body = response.body->readAllText().wait(io.waitScope);
    if (testCase.request.method == HttpMethod::HEAD) {
      KJ_EXPECT(body == "");
    } else {
      KJ_EXPECT(body == kj::strArray(testCase.response.responseBodyParts, ""), body);
    }
  }

  client = nullptr;
  pipe.ends[0]->shutdownWrite();

  writeResponsesPromise.wait(io.waitScope);
}

KJ_TEST("HttpServer pipeline") {
  auto PIPELINE_TESTS = pipelineTestCases();

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServer server(io.provider->getTimer(), table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  for (auto& testCase: PIPELINE_TESTS) {
    KJ_CONTEXT(testCase.request.raw, testCase.response.raw);

    pipe.ends[1]->write(testCase.request.raw.begin(), testCase.request.raw.size())
        .wait(io.waitScope);

    expectRead(*pipe.ends[1], testCase.response.raw).wait(io.waitScope);
  }

  pipe.ends[1]->shutdownWrite();
  listenTask.wait(io.waitScope);

  KJ_EXPECT(service.getRequestCount() == kj::size(PIPELINE_TESTS));
}

KJ_TEST("HttpServer parallel pipeline") {
  auto PIPELINE_TESTS = pipelineTestCases();

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  auto allRequestText =
      kj::strArray(KJ_MAP(testCase, PIPELINE_TESTS) { return testCase.request.raw; }, "");
  auto allResponseText =
      kj::strArray(KJ_MAP(testCase, PIPELINE_TESTS) { return testCase.response.raw; }, "");

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServer server(io.provider->getTimer(), table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  pipe.ends[1]->write(allRequestText.begin(), allRequestText.size()).wait(io.waitScope);
  pipe.ends[1]->shutdownWrite();

  auto rawResponse = pipe.ends[1]->readAllText().wait(io.waitScope);
  KJ_EXPECT(rawResponse == allResponseText, rawResponse);

  listenTask.wait(io.waitScope);

  KJ_EXPECT(service.getRequestCount() == kj::size(PIPELINE_TESTS));
}

KJ_TEST("HttpClient <-> HttpServer") {
  auto PIPELINE_TESTS = pipelineTestCases();

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServer server(io.provider->getTimer(), table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[1]));
  auto client = newHttpClient(table, *pipe.ends[0]);

  for (auto& testCase: PIPELINE_TESTS) {
    KJ_CONTEXT(testCase.request.raw, testCase.response.raw);

    HttpHeaders headers(table);
    for (auto& header: testCase.request.requestHeaders) {
      headers.set(header.id, header.value);
    }

    auto request = client->request(
        testCase.request.method, testCase.request.path, headers, testCase.request.requestBodySize);
    for (auto& part: testCase.request.requestBodyParts) {
      request.body->write(part.begin(), part.size()).wait(io.waitScope);
    }
    request.body = nullptr;

    auto response = request.response.wait(io.waitScope);

    KJ_EXPECT(response.statusCode == testCase.response.statusCode);
    auto body = response.body->readAllText().wait(io.waitScope);
    if (testCase.request.method == HttpMethod::HEAD) {
      KJ_EXPECT(body == "");
    } else {
      KJ_EXPECT(body == kj::strArray(testCase.response.responseBodyParts, ""), body);
    }
  }

  client = nullptr;
  pipe.ends[0]->shutdownWrite();
  listenTask.wait(io.waitScope);
  KJ_EXPECT(service.getRequestCount() == kj::size(PIPELINE_TESTS));
}

// -----------------------------------------------------------------------------

KJ_TEST("WebSocket core protocol") {
  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  auto client = newWebSocket(kj::mv(pipe.ends[0]), nullptr);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr);

  auto mediumString = kj::strArray(kj::repeat(kj::StringPtr("123456789"), 30), "");
  auto bigString = kj::strArray(kj::repeat(kj::StringPtr("123456789"), 10000), "");

  auto clientTask = client->send(kj::StringPtr("hello"))
      .then([&]() { return client->send(mediumString); })
      .then([&]() { return client->send(bigString); })
      .then([&]() { return client->send(kj::StringPtr("world").asBytes()); })
      .then([&]() { return client->close(1234, "bored"); });

  {
    auto message = server->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "hello");
  }

  {
    auto message = server->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == mediumString);
  }

  {
    auto message = server->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == bigString);
  }

  {
    auto message = server->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::Array<byte>>());
    KJ_EXPECT(kj::str(message.get<kj::Array<byte>>().asChars()) == "world");
  }

  {
    auto message = server->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<WebSocket::Close>());
    KJ_EXPECT(message.get<WebSocket::Close>().code == 1234);
    KJ_EXPECT(message.get<WebSocket::Close>().reason == "bored");
  }

  auto serverTask = server->close(4321, "whatever");

  {
    auto message = client->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<WebSocket::Close>());
    KJ_EXPECT(message.get<WebSocket::Close>().code == 4321);
    KJ_EXPECT(message.get<WebSocket::Close>().reason == "whatever");
  }

  clientTask.wait(io.waitScope);
  serverTask.wait(io.waitScope);
}

KJ_TEST("WebSocket fragmented") {
  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr);

  byte DATA[] = {
    0x01, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',

    0x00, 0x03, 'w', 'o', 'r',

    0x80, 0x02, 'l', 'd',
  };

  auto clientTask = client->write(DATA, sizeof(DATA));

  {
    auto message = server->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "hello world");
  }

  clientTask.wait(io.waitScope);
}

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
  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();
  FakeEntropySource maskGenerator;

  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), maskGenerator);

  byte DATA[] = {
    0x81, 0x86, 12, 34, 56, 78, 'h' ^ 12, 'e' ^ 34, 'l' ^ 56, 'l' ^ 78, 'o' ^ 12, ' ' ^ 34,
  };

  auto clientTask = client->write(DATA, sizeof(DATA));
  auto serverTask = server->send(kj::StringPtr("hello "));

  {
    auto message = server->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "hello ");
  }

  expectRead(*client, DATA).wait(io.waitScope);

  clientTask.wait(io.waitScope);
  serverTask.wait(io.waitScope);
}

KJ_TEST("WebSocket unsolicited pong") {
  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  auto client = kj::mv(pipe.ends[0]);
  auto server = newWebSocket(kj::mv(pipe.ends[1]), nullptr);

  byte DATA[] = {
    0x01, 0x06, 'h', 'e', 'l', 'l', 'o', ' ',

    0x8A, 0x03, 'f', 'o', 'o',

    0x80, 0x05, 'w', 'o', 'r', 'l', 'd',
  };

  auto clientTask = client->write(DATA, sizeof(DATA));

  {
    auto message = server->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "hello world");
  }

  clientTask.wait(io.waitScope);
}

KJ_TEST("WebSocket ping") {
  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

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
    auto message = server->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "hello world");
  }

  auto serverTask = server->send(kj::StringPtr("bar"));

  byte EXPECTED[] = {
    0x8A, 0x03, 'f', 'o', 'o',  // pong
    0x81, 0x03, 'b', 'a', 'r',  // message
  };

  expectRead(*client, EXPECTED).wait(io.waitScope);

  clientTask.wait(io.waitScope);
  serverTask.wait(io.waitScope);
}

KJ_TEST("WebSocket ping mid-send") {
  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

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
    auto message = server->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "bar");
  }

  byte EXPECTED1[] = { 0x81, 0x7f, 0, 0, 0, 0, 0, 8, 0, 0 };
  expectRead(*client, EXPECTED1).wait(io.waitScope);
  expectRead(*client, bigString).wait(io.waitScope);

  byte EXPECTED2[] = { 0x8A, 0x03, 'f', 'o', 'o' };
  expectRead(*client, EXPECTED2).wait(io.waitScope);

  clientTask.wait(io.waitScope);
  serverTask.wait(io.waitScope);
}

class UnbufferedPipe final: public AsyncIoStream {
  // An in-memory one-way pipe with no internal buffer. read() blocks waiting for write()s and
  // write() blocks waiting for read()s.
  //
  // TODO(cleanup): This is probably broadly useful. Put it in a utility library somewhere.
  //   NOTE: Must implement handling of cancellation first!

public:
  kj::Promise<void> write(const void* buffer, size_t size) override {
    KJ_SWITCH_ONEOF(current) {
      KJ_CASE_ONEOF(w, CurrentWrite) {
        KJ_FAIL_REQUIRE("can only call write() once at a time");
      }
      KJ_CASE_ONEOF(r, CurrentRead) {
        if (size < r.minBytes) {
          // Write does not complete the current read.
          memcpy(r.buffer.begin(), buffer, size);
          r.minBytes -= size;
          r.alreadyRead += size;
          r.buffer = r.buffer.slice(size, r.buffer.size());
          return kj::READY_NOW;
        } else if (size <= r.buffer.size()) {
          // Write satisfies the current read, and read satisfies the write.
          memcpy(r.buffer.begin(), buffer, size);
          r.fulfiller->fulfill(r.alreadyRead + size);
          current = None();
          return kj::READY_NOW;
        } else {
          // Write satisfies the read and still has more data leftover to write.
          size_t amount = r.buffer.size();
          memcpy(r.buffer.begin(), buffer, amount);
          r.fulfiller->fulfill(amount + r.alreadyRead);
          auto paf = kj::newPromiseAndFulfiller<void>();
          current = CurrentWrite {
            kj::arrayPtr(reinterpret_cast<const byte*>(buffer) + amount, size - amount),
            kj::mv(paf.fulfiller)
          };
          return kj::mv(paf.promise);
        }
      }
      KJ_CASE_ONEOF(e, Eof) {
        KJ_FAIL_REQUIRE("write after EOF");
      }
      KJ_CASE_ONEOF(n, None) {
        auto paf = kj::newPromiseAndFulfiller<void>();
        current = CurrentWrite {
          kj::arrayPtr(reinterpret_cast<const byte*>(buffer), size),
          kj::mv(paf.fulfiller)
        };
        return kj::mv(paf.promise);
      }
    }
    KJ_UNREACHABLE;
  }

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    KJ_SWITCH_ONEOF(current) {
      KJ_CASE_ONEOF(w, CurrentWrite) {
        if (maxBytes < w.buffer.size()) {
          // Entire read satisfied by write, write is still pending.
          memcpy(buffer, w.buffer.begin(), maxBytes);
          w.buffer = w.buffer.slice(maxBytes, w.buffer.size());
          return maxBytes;
        } else if (minBytes <= w.buffer.size()) {
          // Read is satisfied by write and consumes entire write.
          size_t result = w.buffer.size();
          memcpy(buffer, w.buffer.begin(), result);
          w.fulfiller->fulfill();
          current = None();
          return result;
        } else {
          // Read consumes entire write and is not satisfied.
          size_t alreadyRead = w.buffer.size();
          memcpy(buffer, w.buffer.begin(), alreadyRead);
          w.fulfiller->fulfill();
          auto paf = kj::newPromiseAndFulfiller<size_t>();
          current = CurrentRead {
            kj::arrayPtr(reinterpret_cast<byte*>(buffer) + alreadyRead, maxBytes - alreadyRead),
            minBytes - alreadyRead,
            alreadyRead,
            kj::mv(paf.fulfiller)
          };
          return kj::mv(paf.promise);
        }
      }
      KJ_CASE_ONEOF(r, CurrentRead) {
        KJ_FAIL_REQUIRE("can only call read() once at a time");
      }
      KJ_CASE_ONEOF(e, Eof) {
        return size_t(0);
      }
      KJ_CASE_ONEOF(n, None) {
        auto paf = kj::newPromiseAndFulfiller<size_t>();
        current = CurrentRead {
          kj::arrayPtr(reinterpret_cast<byte*>(buffer), maxBytes),
          minBytes,
          0,
          kj::mv(paf.fulfiller)
        };
        return kj::mv(paf.promise);
      }
    }
    KJ_UNREACHABLE;
  }

  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    // TODO(cleanup): Should this be the defalut implementation of this method?
    if (pieces.size() == 0) return kj::READY_NOW;
    return write(pieces[0].begin(), pieces[0].size())
        .then([this, pieces]() {
      return write(pieces.slice(1, pieces.size()));
    });
  }

  void shutdownWrite() override {
    KJ_SWITCH_ONEOF(current) {
      KJ_CASE_ONEOF(w, CurrentWrite) {
        KJ_FAIL_REQUIRE("can't call shutdownWrite() during a write()");
      }
      KJ_CASE_ONEOF(r, CurrentRead) {
        r.fulfiller->fulfill(kj::mv(r.alreadyRead));
      }
      KJ_CASE_ONEOF(e, Eof) {
        // ignore
      }
      KJ_CASE_ONEOF(n, None) {
        // ignore
      }
    }

    current = Eof();
  }

private:
  struct CurrentWrite {
    kj::ArrayPtr<const byte> buffer;
    kj::Own<kj::PromiseFulfiller<void>> fulfiller;
  };
  struct CurrentRead {
    kj::ArrayPtr<byte> buffer;
    size_t minBytes;
    size_t alreadyRead;
    kj::Own<kj::PromiseFulfiller<size_t>> fulfiller;
  };
  struct Eof {};
  struct None {};

  kj::OneOf<CurrentWrite, CurrentRead, Eof, None> current = None();
};

class InputOutputPair final: public kj::AsyncIoStream {
  // Creates an AsyncIoStream out of an AsyncInputStream and an AsyncOutputStream.

public:
  InputOutputPair(kj::AsyncInputStream& in, kj::AsyncIoStream& out)
      : in(in), out(out) {}

  kj::Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return in.read(buffer, minBytes, maxBytes);
  }
  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return in.tryRead(buffer, minBytes, maxBytes);
  }

  Maybe<uint64_t> tryGetLength() override {
    return in.tryGetLength();
  }

  Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount = kj::maxValue) override {
    return in.pumpTo(output, amount);
  }

  kj::Promise<void> write(const void* buffer, size_t size) override {
    return out.write(buffer, size);
  }

  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    return out.write(pieces);
  }

  kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(
      kj::AsyncInputStream& input, uint64_t amount = kj::maxValue) override {
    return out.tryPumpFrom(input, amount);
  }

  void shutdownWrite() override {
    return out.shutdownWrite();
  }

private:
  kj::AsyncInputStream& in;
  kj::AsyncIoStream& out;
};

KJ_TEST("WebSocket double-ping mid-send") {
  auto io = kj::setupAsyncIo();

  UnbufferedPipe upPipe;
  UnbufferedPipe downPipe;
  InputOutputPair client(downPipe, upPipe);
  auto server = newWebSocket(kj::heap<InputOutputPair>(upPipe, downPipe), nullptr);

  auto bigString = kj::strArray(kj::repeat(kj::StringPtr("12345678"), 65536), "");
  auto serverTask = server->send(bigString).eagerlyEvaluate(nullptr);

  byte DATA[] = {
    0x89, 0x03, 'f', 'o', 'o',  // ping
    0x89, 0x03, 'q', 'u', 'x',  // ping2
    0x81, 0x03, 'b', 'a', 'r',  // some other message
  };

  auto clientTask = client.write(DATA, sizeof(DATA));

  {
    auto message = server->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "bar");
  }

  byte EXPECTED1[] = { 0x81, 0x7f, 0, 0, 0, 0, 0, 8, 0, 0 };
  expectRead(client, EXPECTED1).wait(io.waitScope);
  expectRead(client, bigString).wait(io.waitScope);

  byte EXPECTED2[] = { 0x8A, 0x03, 'q', 'u', 'x' };
  expectRead(client, EXPECTED2).wait(io.waitScope);

  clientTask.wait(io.waitScope);
  serverTask.wait(io.waitScope);
}

KJ_TEST("WebSocket ping received during pong send") {
  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

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
    auto message = server->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "bar");
  }

  byte EXPECTED1[] = { 0x8A, 0x7f, 0, 0, 0, 0, 0, 8, 0, 0 };
  expectRead(*client, EXPECTED1).wait(io.waitScope);
  expectRead(*client, bigString).wait(io.waitScope);

  byte EXPECTED2[] = { 0x8A, 0x03, 'f', 'o', 'o' };
  expectRead(*client, EXPECTED2).wait(io.waitScope);

  clientTask.wait(io.waitScope);
}

class TestWebSocketService final: public HttpService, private kj::TaskSet::ErrorHandler {
public:
  TestWebSocketService(HttpHeaderTable& headerTable, HttpHeaderId hMyHeader)
      : headerTable(headerTable), hMyHeader(hMyHeader), tasks(*this) {}

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    KJ_FAIL_ASSERT("can't get here");
  }

  kj::Promise<void> openWebSocket(
      kj::StringPtr url, const HttpHeaders& headers, WebSocketResponse& response) override {
    HttpHeaders responseHeaders(headerTable);
    KJ_IF_MAYBE(h, headers.get(hMyHeader)) {
      responseHeaders.set(hMyHeader, kj::str("respond-", *h));
    }

    if (url == "/return-error") {
      response.send(404, "Not Found", responseHeaders, uint64_t(0));
      return kj::READY_NOW;
    } else if (url == "/ws-inline") {
      auto ws = response.acceptWebSocket(responseHeaders);
      return doWebSocket(*ws, "start-inline").attach(kj::mv(ws));
    } else if (url == "/ws-detached") {
      auto ws = response.acceptWebSocket(responseHeaders);
      tasks.add(doWebSocket(*ws, "start-detached").attach(kj::mv(ws)));
      return kj::READY_NOW;
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
const char WEBSOCKET_RESPONSE_HANDSHAKE_ERROR[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: 0\r\n"
    "My-Header: respond-foo\r\n"
    "\r\n";
const byte WEBSOCKET_FIRST_MESSAGE_INLINE[] =
    { 0x81, 0x0c, 's','t','a','r','t','-','i','n','l','i','n','e' };
const byte WEBSOCKET_FIRST_MESSAGE_DETACHED[] =
    { 0x81, 0x0e, 's','t','a','r','t','-','d','e','t','a','c','h','e','d' };
const byte WEBSOCKET_SEND_MESSAGE[] =
    { 0x81, 0x83, 12, 34, 56, 78, 'b'^12, 'a'^34, 'r'^56 };
const byte WEBSOCKET_REPLY_MESSAGE[] =
    { 0x81, 0x09, 'r','e','p','l','y',':','b','a','r' };
const byte WEBSOCKET_SEND_CLOSE[] =
    { 0x88, 0x85, 12, 34, 56, 78, 0x12^12, 0x34^34, 'q'^56, 'u'^78, 'x'^12 };
const byte WEBSOCKET_REPLY_CLOSE[] =
    { 0x88, 0x11, 0x12, 0x35, 'c','l','o','s','e','-','r','e','p','l','y',':','q','u','x' };

template <size_t s>
kj::ArrayPtr<const byte> asBytes(const char (&chars)[s]) {
  return kj::ArrayPtr<const char>(chars, s - 1).asBytes();
}

KJ_TEST("HttpClient WebSocket handshake") {
  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  auto request = kj::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);

  auto serverTask = expectRead(*pipe.ends[1], request)
      .then([&]() { return pipe.ends[1]->write({asBytes(WEBSOCKET_RESPONSE_HANDSHAKE)}); })
      .then([&]() { return pipe.ends[1]->write({WEBSOCKET_FIRST_MESSAGE_INLINE}); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_MESSAGE); })
      .then([&]() { return pipe.ends[1]->write({WEBSOCKET_REPLY_MESSAGE}); })
      .then([&]() { return expectRead(*pipe.ends[1], WEBSOCKET_SEND_CLOSE); })
      .then([&]() { return pipe.ends[1]->write({WEBSOCKET_REPLY_CLOSE}); })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], entropySource);

  kj::HttpHeaders headers(*headerTable);
  headers.set(hMyHeader, "foo");
  auto response = client->openWebSocket("/websocket", headers).wait(io.waitScope);

  KJ_EXPECT(response.statusCode == 101);
  KJ_EXPECT(response.statusText == "Switching Protocols", response.statusText);
  KJ_EXPECT(KJ_ASSERT_NONNULL(response.headers->get(hMyHeader)) == "respond-foo");
  KJ_ASSERT(response.webSocketOrBody.is<kj::Own<WebSocket>>());
  auto ws = kj::mv(response.webSocketOrBody.get<kj::Own<WebSocket>>());

  {
    auto message = ws->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "start-inline");
  }

  ws->send(kj::StringPtr("bar")).wait(io.waitScope);
  {
    auto message = ws->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<kj::String>());
    KJ_EXPECT(message.get<kj::String>() == "reply:bar");
  }

  ws->close(0x1234, "qux").wait(io.waitScope);
  {
    auto message = ws->receive().wait(io.waitScope);
    KJ_ASSERT(message.is<WebSocket::Close>());
    KJ_EXPECT(message.get<WebSocket::Close>().code == 0x1235);
    KJ_EXPECT(message.get<WebSocket::Close>().reason == "close-reply:qux");
  }

  serverTask.wait(io.waitScope);
}

KJ_TEST("HttpClient WebSocket error") {
  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  auto request = kj::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);

  auto serverTask = expectRead(*pipe.ends[1], request)
      .then([&]() { return pipe.ends[1]->write({asBytes(WEBSOCKET_RESPONSE_HANDSHAKE_ERROR)}); })
      .then([&]() { return expectRead(*pipe.ends[1], request); })
      .then([&]() { return pipe.ends[1]->write({asBytes(WEBSOCKET_RESPONSE_HANDSHAKE_ERROR)}); })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();

  FakeEntropySource entropySource;

  auto client = newHttpClient(*headerTable, *pipe.ends[0], entropySource);

  kj::HttpHeaders headers(*headerTable);
  headers.set(hMyHeader, "foo");

  {
    auto response = client->openWebSocket("/websocket", headers).wait(io.waitScope);

    KJ_EXPECT(response.statusCode == 404);
    KJ_EXPECT(response.statusText == "Not Found", response.statusText);
    KJ_EXPECT(KJ_ASSERT_NONNULL(response.headers->get(hMyHeader)) == "respond-foo");
    KJ_ASSERT(response.webSocketOrBody.is<kj::Own<AsyncInputStream>>());
  }

  {
    auto response = client->openWebSocket("/websocket", headers).wait(io.waitScope);

    KJ_EXPECT(response.statusCode == 404);
    KJ_EXPECT(response.statusText == "Not Found", response.statusText);
    KJ_EXPECT(KJ_ASSERT_NONNULL(response.headers->get(hMyHeader)) == "respond-foo");
    KJ_ASSERT(response.webSocketOrBody.is<kj::Own<AsyncInputStream>>());
  }

  serverTask.wait(io.waitScope);
}

KJ_TEST("HttpServer WebSocket handshake") {
  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();
  TestWebSocketService service(*headerTable, hMyHeader);
  HttpServer server(io.provider->getTimer(), *headerTable, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto request = kj::str("GET /ws-inline", WEBSOCKET_REQUEST_HANDSHAKE);
  pipe.ends[1]->write({request.asBytes()}).wait(io.waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_RESPONSE_HANDSHAKE).wait(io.waitScope);

  expectRead(*pipe.ends[1], WEBSOCKET_FIRST_MESSAGE_INLINE).wait(io.waitScope);
  pipe.ends[1]->write({WEBSOCKET_SEND_MESSAGE}).wait(io.waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_REPLY_MESSAGE).wait(io.waitScope);
  pipe.ends[1]->write({WEBSOCKET_SEND_CLOSE}).wait(io.waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE).wait(io.waitScope);

  listenTask.wait(io.waitScope);
}

KJ_TEST("HttpServer WebSocket handshake detached") {
  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();
  TestWebSocketService service(*headerTable, hMyHeader);
  HttpServer server(io.provider->getTimer(), *headerTable, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto request = kj::str("GET /ws-detached", WEBSOCKET_REQUEST_HANDSHAKE);
  pipe.ends[1]->write({request.asBytes()}).wait(io.waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_RESPONSE_HANDSHAKE).wait(io.waitScope);

  listenTask.wait(io.waitScope);

  expectRead(*pipe.ends[1], WEBSOCKET_FIRST_MESSAGE_DETACHED).wait(io.waitScope);
  pipe.ends[1]->write({WEBSOCKET_SEND_MESSAGE}).wait(io.waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_REPLY_MESSAGE).wait(io.waitScope);
  pipe.ends[1]->write({WEBSOCKET_SEND_CLOSE}).wait(io.waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_REPLY_CLOSE).wait(io.waitScope);
}

KJ_TEST("HttpServer WebSocket handshake error") {
  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable::Builder tableBuilder;
  HttpHeaderId hMyHeader = tableBuilder.add("My-Header");
  auto headerTable = tableBuilder.build();
  TestWebSocketService service(*headerTable, hMyHeader);
  HttpServer server(io.provider->getTimer(), *headerTable, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  auto request = kj::str("GET /return-error", WEBSOCKET_REQUEST_HANDSHAKE);
  pipe.ends[1]->write({request.asBytes()}).wait(io.waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_RESPONSE_HANDSHAKE_ERROR).wait(io.waitScope);

  // Can send more requests!
  pipe.ends[1]->write({request.asBytes()}).wait(io.waitScope);
  expectRead(*pipe.ends[1], WEBSOCKET_RESPONSE_HANDSHAKE_ERROR).wait(io.waitScope);

  pipe.ends[1]->shutdownWrite();

  listenTask.wait(io.waitScope);
}

// -----------------------------------------------------------------------------

KJ_TEST("HttpServer request timeout") {
  auto PIPELINE_TESTS = pipelineTestCases();

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServerSettings settings;
  settings.headerTimeout = 1 * kj::MILLISECONDS;
  HttpServer server(io.provider->getTimer(), table, service, settings);

  // Shouldn't hang! Should time out.
  server.listenHttp(kj::mv(pipe.ends[0])).wait(io.waitScope);

  // Closes the connection without sending anything.
  KJ_EXPECT(pipe.ends[1]->readAllText().wait(io.waitScope) == "");
}

KJ_TEST("HttpServer pipeline timeout") {
  auto PIPELINE_TESTS = pipelineTestCases();

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable table;
  TestHttpService service(PIPELINE_TESTS, table);
  HttpServerSettings settings;
  settings.pipelineTimeout = 1 * kj::MILLISECONDS;
  HttpServer server(io.provider->getTimer(), table, service, settings);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(io.waitScope);
  expectRead(*pipe.ends[1], PIPELINE_TESTS[0].response.raw).wait(io.waitScope);

  // Listen task should time out even though we didn't shutdown the socket.
  listenTask.wait(io.waitScope);

  // In this case, no data is sent back.
  KJ_EXPECT(pipe.ends[1]->readAllText().wait(io.waitScope) == "");
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

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable table;
  BrokenHttpService service;
  HttpServer server(io.provider->getTimer(), table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(io.waitScope);
  auto text = pipe.ends[1]->readAllText().wait(io.waitScope);

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

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable table;
  BrokenHttpService service(KJ_EXCEPTION(DISCONNECTED, "disconnected"));
  HttpServer server(io.provider->getTimer(), table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(io.waitScope);
  auto text = pipe.ends[1]->readAllText().wait(io.waitScope);

  KJ_EXPECT(text == "", text);
}

KJ_TEST("HttpServer overloaded") {
  auto PIPELINE_TESTS = pipelineTestCases();

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable table;
  BrokenHttpService service(KJ_EXCEPTION(OVERLOADED, "overloaded"));
  HttpServer server(io.provider->getTimer(), table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(io.waitScope);
  auto text = pipe.ends[1]->readAllText().wait(io.waitScope);

  KJ_EXPECT(text.startsWith("HTTP/1.1 503 Service Unavailable"), text);
}

KJ_TEST("HttpServer unimplemented") {
  auto PIPELINE_TESTS = pipelineTestCases();

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable table;
  BrokenHttpService service(KJ_EXCEPTION(UNIMPLEMENTED, "unimplemented"));
  HttpServer server(io.provider->getTimer(), table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(io.waitScope);
  auto text = pipe.ends[1]->readAllText().wait(io.waitScope);

  KJ_EXPECT(text.startsWith("HTTP/1.1 501 Not Implemented"), text);
}

KJ_TEST("HttpServer threw exception") {
  auto PIPELINE_TESTS = pipelineTestCases();

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable table;
  BrokenHttpService service(KJ_EXCEPTION(FAILED, "failed"));
  HttpServer server(io.provider->getTimer(), table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(io.waitScope);
  auto text = pipe.ends[1]->readAllText().wait(io.waitScope);

  KJ_EXPECT(text.startsWith("HTTP/1.1 500 Internal Server Error"), text);
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

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable table;
  PartialResponseService service;
  HttpServer server(io.provider->getTimer(), table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  KJ_EXPECT_LOG(ERROR, "HttpService threw exception after generating a partial response");

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(io.waitScope);
  auto text = pipe.ends[1]->readAllText().wait(io.waitScope);

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

  auto io = kj::setupAsyncIo();
  auto pipe = io.provider->newTwoWayPipe();

  HttpHeaderTable table;
  PumpResponseService service;
  HttpServer server(io.provider->getTimer(), table, service);

  auto listenTask = server.listenHttp(kj::mv(pipe.ends[0]));

  // Do one request.
  pipe.ends[1]->write(PIPELINE_TESTS[0].request.raw.begin(), PIPELINE_TESTS[0].request.raw.size())
      .wait(io.waitScope);
  pipe.ends[1]->shutdownWrite();
  auto text = pipe.ends[1]->readAllText().wait(io.waitScope);

  KJ_EXPECT(text ==
      "HTTP/1.1 200 OK\r\n"
      "Content-Length: 13\r\n"
      "\r\n"
      "Hello, World!", text);
}

// -----------------------------------------------------------------------------

KJ_TEST("newHttpService from HttpClient") {
  auto PIPELINE_TESTS = pipelineTestCases();

  auto io = kj::setupAsyncIo();
  auto frontPipe = io.provider->newTwoWayPipe();
  auto backPipe = io.provider->newTwoWayPipe();

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
    HttpServer frontServer(io.provider->getTimer(), table, *frontService);
    auto listenTask = frontServer.listenHttp(kj::mv(frontPipe.ends[1]));

    for (auto& testCase: PIPELINE_TESTS) {
      KJ_CONTEXT(testCase.request.raw, testCase.response.raw);

      frontPipe.ends[0]->write(testCase.request.raw.begin(), testCase.request.raw.size())
               .wait(io.waitScope);

      expectRead(*frontPipe.ends[0], testCase.response.raw).wait(io.waitScope);
    }

    frontPipe.ends[0]->shutdownWrite();
    listenTask.wait(io.waitScope);
  }

  backPipe.ends[0]->shutdownWrite();
  writeResponsesPromise.wait(io.waitScope);
}

KJ_TEST("newHttpService from HttpClient WebSockets") {
  auto io = kj::setupAsyncIo();
  auto frontPipe = io.provider->newTwoWayPipe();
  auto backPipe = io.provider->newTwoWayPipe();

  auto request = kj::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);
  auto writeResponsesPromise = expectRead(*backPipe.ends[1], request)
      .then([&]() { return backPipe.ends[1]->write({asBytes(WEBSOCKET_RESPONSE_HANDSHAKE)}); })
      .then([&]() { return backPipe.ends[1]->write({WEBSOCKET_FIRST_MESSAGE_INLINE}); })
      .then([&]() { return expectRead(*backPipe.ends[1], WEBSOCKET_SEND_MESSAGE); })
      .then([&]() { return backPipe.ends[1]->write({WEBSOCKET_REPLY_MESSAGE}); })
      .then([&]() { return expectRead(*backPipe.ends[1], WEBSOCKET_SEND_CLOSE); })
      .then([&]() { return backPipe.ends[1]->write({WEBSOCKET_REPLY_CLOSE}); })
      // expect EOF
      .then([&]() { return backPipe.ends[1]->readAllBytes(); })
      .then([&](kj::ArrayPtr<byte> content) {
        KJ_EXPECT(content.size() == 0);
        // Send EOF.
        backPipe.ends[1]->shutdownWrite();
      })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  {
    HttpHeaderTable table;
    FakeEntropySource entropySource;
    auto backClient = newHttpClient(table, *backPipe.ends[0], entropySource);
    auto frontService = newHttpService(*backClient);
    HttpServer frontServer(io.provider->getTimer(), table, *frontService);
    auto listenTask = frontServer.listenHttp(kj::mv(frontPipe.ends[1]));

    frontPipe.ends[0]->write({request.asBytes()}).wait(io.waitScope);
    expectRead(*frontPipe.ends[0], WEBSOCKET_RESPONSE_HANDSHAKE).wait(io.waitScope);

    expectRead(*frontPipe.ends[0], WEBSOCKET_FIRST_MESSAGE_INLINE).wait(io.waitScope);
    frontPipe.ends[0]->write({WEBSOCKET_SEND_MESSAGE}).wait(io.waitScope);
    expectRead(*frontPipe.ends[0], WEBSOCKET_REPLY_MESSAGE).wait(io.waitScope);
    frontPipe.ends[0]->write({WEBSOCKET_SEND_CLOSE}).wait(io.waitScope);
    expectRead(*frontPipe.ends[0], WEBSOCKET_REPLY_CLOSE).wait(io.waitScope);

    frontPipe.ends[0]->shutdownWrite();
    listenTask.wait(io.waitScope);
  }

  writeResponsesPromise.wait(io.waitScope);
}

KJ_TEST("newHttpService from HttpClient WebSockets disconnect") {
  auto io = kj::setupAsyncIo();
  auto frontPipe = io.provider->newTwoWayPipe();
  auto backPipe = io.provider->newTwoWayPipe();

  auto request = kj::str("GET /websocket", WEBSOCKET_REQUEST_HANDSHAKE);
  auto writeResponsesPromise = expectRead(*backPipe.ends[1], request)
      .then([&]() { return backPipe.ends[1]->write({asBytes(WEBSOCKET_RESPONSE_HANDSHAKE)}); })
      .then([&]() { return backPipe.ends[1]->write({WEBSOCKET_FIRST_MESSAGE_INLINE}); })
      .then([&]() { return expectRead(*backPipe.ends[1], WEBSOCKET_SEND_MESSAGE); })
      .then([&]() { backPipe.ends[1]->shutdownWrite(); })
      .eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });

  {
    HttpHeaderTable table;
    FakeEntropySource entropySource;
    auto backClient = newHttpClient(table, *backPipe.ends[0], entropySource);
    auto frontService = newHttpService(*backClient);
    HttpServer frontServer(io.provider->getTimer(), table, *frontService);
    auto listenTask = frontServer.listenHttp(kj::mv(frontPipe.ends[1]));

    frontPipe.ends[0]->write({request.asBytes()}).wait(io.waitScope);
    expectRead(*frontPipe.ends[0], WEBSOCKET_RESPONSE_HANDSHAKE).wait(io.waitScope);

    expectRead(*frontPipe.ends[0], WEBSOCKET_FIRST_MESSAGE_INLINE).wait(io.waitScope);
    frontPipe.ends[0]->write({WEBSOCKET_SEND_MESSAGE}).wait(io.waitScope);

    KJ_EXPECT(frontPipe.ends[0]->readAllText().wait(io.waitScope) == "");

    frontPipe.ends[0]->shutdownWrite();
    listenTask.wait(io.waitScope);
  }

  writeResponsesPromise.wait(io.waitScope);
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
  CountingNetworkAddress(kj::NetworkAddress& inner, uint& count)
      : inner(inner), count(count), addrCount(ownAddrCount) {}
  CountingNetworkAddress(kj::Own<kj::NetworkAddress> inner, uint& count, uint& addrCount)
      : inner(*inner), ownInner(kj::mv(inner)), count(count), addrCount(addrCount) {}
  ~CountingNetworkAddress() noexcept(false) {
    --addrCount;
  }

  kj::Promise<kj::Own<kj::AsyncIoStream>> connect() override {
    ++count;
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
    KJ_ASSERT(url != "/throw");

    auto body = kj::str(headers.get(HttpHeaderId::HOST).orDefault("null"), ":", url);
    auto stream = response.send(200, "OK", HttpHeaders(headerTable), body.size());
    auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);
    promises.add(stream->write(body.begin(), body.size()));
    promises.add(requestBody.readAllBytes().ignoreResult());
    return kj::joinPromises(promises.finish()).attach(kj::mv(stream), kj::mv(body));
  }

  kj::Promise<void> openWebSocket(
      kj::StringPtr url, const HttpHeaders& headers, WebSocketResponse& response) override {
    auto ws = response.acceptWebSocket(HttpHeaders(headerTable));
    auto body = kj::str(headers.get(HttpHeaderId::HOST).orDefault("null"), ":", url);
    auto sendPromise = ws->send(body);

    auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);
    promises.add(sendPromise.attach(kj::mv(body)));
    promises.add(ws->receive().ignoreResult());
    return kj::joinPromises(promises.finish()).attach(kj::mv(ws));
  }

private:
  HttpHeaderTable& headerTable;
};

KJ_TEST("HttpClient connection management") {
  auto io = kj::setupAsyncIo();

  kj::TimerImpl serverTimer(kj::origin<kj::TimePoint>());
  kj::TimerImpl clientTimer(kj::origin<kj::TimePoint>());
  HttpHeaderTable headerTable;

  auto listener = io.provider->getNetwork().parseAddress("localhost", 0)
      .wait(io.waitScope)->listen();
  DummyService service(headerTable);
  HttpServerSettings serverSettings;
  HttpServer server(serverTimer, headerTable, service, serverSettings);
  auto listenTask = server.listenHttp(*listener);

  auto addr = io.provider->getNetwork().parseAddress("localhost", listener->getPort())
      .wait(io.waitScope);
  uint count = 0;
  CountingNetworkAddress countingAddr(*addr, count);

  FakeEntropySource entropySource;
  HttpClientSettings clientSettings;
  clientSettings.entropySource = entropySource;
  auto client = newHttpClient(clientTimer, headerTable, countingAddr, clientSettings);

  KJ_EXPECT(count == 0);

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
  doRequest().wait(io.waitScope);
  doRequest().wait(io.waitScope);
  doRequest().wait(io.waitScope);
  KJ_EXPECT(count == 1);

  // But if we do two in parallel, we'll end up with two connections.
  auto req1 = doRequest();
  auto req2 = doRequest();
  req1.wait(io.waitScope);
  req2.wait(io.waitScope);
  KJ_EXPECT(count == 2);

  // We can reuse after a POST, provided we write the whole POST body properly.
  {
    auto req = client->request(
        HttpMethod::POST, kj::str("/foo"), HttpHeaders(headerTable), size_t(6));
    req.body->write("foobar", 6).wait(io.waitScope);
    req.response.wait(io.waitScope).body->readAllBytes().wait(io.waitScope);
  }
  KJ_EXPECT(count == 2);
  doRequest().wait(io.waitScope);
  KJ_EXPECT(count == 2);

  // Advance time for half the timeout, then exercise one of the connections.
  clientTimer.advanceTo(clientTimer.now() + clientSettings.idleTimout / 2);
  doRequest().wait(io.waitScope);
  doRequest().wait(io.waitScope);
  io.waitScope.poll();
  KJ_EXPECT(count == 2);

  // Advance time past when the other connection should time out. It should be dropped.
  clientTimer.advanceTo(clientTimer.now() + clientSettings.idleTimout * 3 / 4);
  io.waitScope.poll();
  KJ_EXPECT(count == 1);

  // Wait for the other to drop.
  clientTimer.advanceTo(clientTimer.now() + clientSettings.idleTimout / 2);
  io.waitScope.poll();
  KJ_EXPECT(count == 0);

  // New request creates a new connection again.
  doRequest().wait(io.waitScope);
  KJ_EXPECT(count == 1);

  // WebSocket connections are not reused.
  client->openWebSocket(kj::str("/websocket"), HttpHeaders(headerTable))
      .wait(io.waitScope);
  KJ_EXPECT(count == 0);

  // Errored connections are not reused.
  doRequest().wait(io.waitScope);
  KJ_EXPECT(count == 1);
  client->request(HttpMethod::GET, kj::str("/throw"), HttpHeaders(headerTable)).response
      .wait(io.waitScope).body->readAllBytes().wait(io.waitScope);
  KJ_EXPECT(count == 0);

  // Connections where we failed to read the full response body are not reused.
  doRequest().wait(io.waitScope);
  KJ_EXPECT(count == 1);
  client->request(HttpMethod::GET, kj::str("/foo"), HttpHeaders(headerTable)).response
      .wait(io.waitScope);
  KJ_EXPECT(count == 0);

  // Connections where we failed to write the full request body are not reused.
  doRequest().wait(io.waitScope);
  KJ_EXPECT(count == 1);
  client->request(HttpMethod::POST, kj::str("/foo"), HttpHeaders(headerTable), size_t(6)).response
      .wait(io.waitScope).body->readAllBytes().wait(io.waitScope);
  KJ_EXPECT(count == 0);

#if __linux__
  // TODO(soon): Figure out why this doesn't work on Windows and is flakey on Mac. My guess is that
  //   the closing of the TCP connection propagates synchronously on Linux so that by the time we
  //   poll() the EventPort it reports the client end of the connection has reached EOF, whereas on
  //   Mac and Windows this propagation probably involves some concurrent process which may or may
  //   not complete before we poll(). A solution in this case would be to use a dummy in-memory
  //   ConnectionReceiver that returns in-memory pipes (see UnbufferedPipe earlier in this file),
  //   so that we don't rely on any non-local behavior. Another solution would be to pause for
  //   a short time, maybe.

  // If the server times out the connection, we figure it out on the client.
  doRequest().wait(io.waitScope);
  KJ_EXPECT(count == 1);
  serverTimer.advanceTo(serverTimer.now() + serverSettings.pipelineTimeout * 2);
  io.waitScope.poll();
  KJ_EXPECT(count == 0);
#endif

  // Can still make requests.
  doRequest().wait(io.waitScope);
  KJ_EXPECT(count == 1);
}

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
    return client->request(HttpMethod::GET,
        kj::str((tls ? "https://localhost:" : "http://localhost:"), port, '/', n),
                HttpHeaders(headerTable)).response
        .then([](HttpClient::Response&& response) {
      auto promise = response.body->readAllText();
      return promise.attach(kj::mv(response.body));
    }).then([n, port](kj::String body) {
      KJ_EXPECT(body == kj::str("localhost:", port, ":/", n), body, port, n);
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
  clientTimer.advanceTo(clientTimer.now() + clientSettings.idleTimout * 2);
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

// -----------------------------------------------------------------------------

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

}  // namespace
}  // namespace kj
