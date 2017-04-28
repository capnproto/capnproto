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
      nullptr, {},
    },

    {
      "HEAD /foo/bar HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "\r\n",

      HttpMethod::HEAD,
      "/foo/bar",
      {{HttpHeaderId::HOST, "example.com"}},
      nullptr, {},
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
      nullptr, {}
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
    nullptr, {},
  };

  HttpRequestTestCase HEAD_REQUEST = {
    "HEAD / HTTP/1.1\r\n"
    "\r\n",

    HttpMethod::HEAD,
    "/",
    {},
    nullptr, {},
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

        HttpMethod::GET, "/", {}, nullptr, {},
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

        HttpMethod::HEAD, "/", {}, nullptr, {},
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

  // Sends back 408 Request Timeout.
  KJ_EXPECT(pipe.ends[1]->readAllText().wait(io.waitScope)
      .startsWith("HTTP/1.1 408 Request Timeout"));
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
