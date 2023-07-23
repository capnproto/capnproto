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

#pragma once
// The KJ HTTP client/server library.
//
// This is a simple library which can be used to implement an HTTP client or server. Properties
// of this library include:
// - Uses KJ async framework.
// - Agnostic to transport layer -- you can provide your own.
// - Header parsing is zero-copy -- it results in strings that point directly into the buffer
//   received off the wire.
// - Application code which reads and writes headers refers to headers by symbolic names, not by
//   string literals, with lookups being array-index-based, not map-based. To make this possible,
//   the application announces what headers it cares about in advance, in order to assign numeric
//   values to them.
// - Methods are identified by an enum.

#include <kj/string.h>
#include <kj/vector.h>
#include <kj/memory.h>
#include <kj/one-of.h>
#include <kj/async-io.h>

namespace kj {

#define KJ_HTTP_FOR_EACH_METHOD(MACRO) \
  MACRO(GET) \
  MACRO(HEAD) \
  MACRO(POST) \
  MACRO(PUT) \
  MACRO(DELETE) \
  MACRO(PATCH) \
  MACRO(PURGE) \
  MACRO(OPTIONS) \
  MACRO(TRACE) \
  /* standard methods */ \
  /* */ \
  /* (CONNECT is intentionally omitted since it is handled specially in HttpHandler) */ \
  \
  MACRO(COPY) \
  MACRO(LOCK) \
  MACRO(MKCOL) \
  MACRO(MOVE) \
  MACRO(PROPFIND) \
  MACRO(PROPPATCH) \
  MACRO(SEARCH) \
  MACRO(UNLOCK) \
  MACRO(ACL) \
  /* WebDAV */ \
  \
  MACRO(REPORT) \
  MACRO(MKACTIVITY) \
  MACRO(CHECKOUT) \
  MACRO(MERGE) \
  /* Subversion */ \
  \
  MACRO(MSEARCH) \
  MACRO(NOTIFY) \
  MACRO(SUBSCRIBE) \
  MACRO(UNSUBSCRIBE)
  /* UPnP */

enum class HttpMethod {
  // Enum of known HTTP methods.
  //
  // We use an enum rather than a string to allow for faster parsing and switching and to reduce
  // ambiguity.

#define DECLARE_METHOD(id) id,
KJ_HTTP_FOR_EACH_METHOD(DECLARE_METHOD)
#undef DECLARE_METHOD
};

kj::StringPtr KJ_STRINGIFY(HttpMethod method);
kj::Maybe<HttpMethod> tryParseHttpMethod(kj::StringPtr name);

class HttpHeaderTable;

class HttpHeaderId {
  // Identifies an HTTP header by numeric ID that indexes into an HttpHeaderTable.
  //
  // The KJ HTTP API prefers that headers be identified by these IDs for a few reasons:
  // - Integer lookups are much more efficient than string lookups.
  // - Case-insensitivity is awkward to deal with when const strings are being passed to the lookup
  //   method.
  // - Writing out strings less often means fewer typos.
  //
  // See HttpHeaderTable for usage hints.

public:
  HttpHeaderId() = default;

  inline bool operator==(const HttpHeaderId& other) const { return id == other.id; }
  inline bool operator!=(const HttpHeaderId& other) const { return id != other.id; }
  inline bool operator< (const HttpHeaderId& other) const { return id <  other.id; }
  inline bool operator> (const HttpHeaderId& other) const { return id >  other.id; }
  inline bool operator<=(const HttpHeaderId& other) const { return id <= other.id; }
  inline bool operator>=(const HttpHeaderId& other) const { return id >= other.id; }

  inline size_t hashCode() const { return id; }
  // Returned value is guaranteed to be small and never collide with other headers on the same
  // table.

  kj::StringPtr toString() const;

  void requireFrom(const HttpHeaderTable& table) const;
  // In debug mode, throws an exception if the HttpHeaderId is not from the given table.
  //
  // In opt mode, no-op.

#define KJ_HTTP_FOR_EACH_BUILTIN_HEADER(MACRO) \
  /* Headers that are always read-only. */ \
  MACRO(CONNECTION, "Connection") \
  MACRO(KEEP_ALIVE, "Keep-Alive") \
  MACRO(TE, "TE") \
  MACRO(TRAILER, "Trailer") \
  MACRO(UPGRADE, "Upgrade") \
  \
  /* Headers that are read-only except in the case of a response to a HEAD request. */ \
  MACRO(CONTENT_LENGTH, "Content-Length") \
  MACRO(TRANSFER_ENCODING, "Transfer-Encoding") \
  \
  /* Headers that are read-only for WebSocket handshakes. */ \
  MACRO(SEC_WEBSOCKET_KEY, "Sec-WebSocket-Key") \
  MACRO(SEC_WEBSOCKET_VERSION, "Sec-WebSocket-Version") \
  MACRO(SEC_WEBSOCKET_ACCEPT, "Sec-WebSocket-Accept") \
  MACRO(SEC_WEBSOCKET_EXTENSIONS, "Sec-WebSocket-Extensions") \
  \
  /* Headers that you can write. */ \
  MACRO(HOST, "Host") \
  MACRO(DATE, "Date") \
  MACRO(LOCATION, "Location") \
  MACRO(CONTENT_TYPE, "Content-Type")
  // For convenience, these headers are valid for all HttpHeaderTables. You can refer to them like:
  //
  //     HttpHeaderId::HOST
  //
  // TODO(someday): Fill this out with more common headers.

#define DECLARE_HEADER(id, name) \
  static const HttpHeaderId id;
  // Declare a constant for each builtin header, e.g.: HttpHeaderId::CONNECTION

  KJ_HTTP_FOR_EACH_BUILTIN_HEADER(DECLARE_HEADER);
#undef DECLARE_HEADER

private:
  const HttpHeaderTable* table;
  uint id;

  inline explicit constexpr HttpHeaderId(const HttpHeaderTable* table, uint id)
      : table(table), id(id) {}
  friend class HttpHeaderTable;
  friend class HttpHeaders;
};

class HttpHeaderTable {
  // Construct an HttpHeaderTable to declare which headers you'll be interested in later on, and
  // to manufacture IDs for them.
  //
  // Example:
  //
  //     // Build a header table with the headers we are interested in.
  //     kj::HttpHeaderTable::Builder builder;
  //     const HttpHeaderId accept = builder.add("Accept");
  //     const HttpHeaderId contentType = builder.add("Content-Type");
  //     kj::HttpHeaderTable table(kj::mv(builder));
  //
  //     // Create an HTTP client.
  //     auto client = kj::newHttpClient(table, network);
  //
  //     // Get http://example.com.
  //     HttpHeaders headers(table);
  //     headers.set(accept, "text/html");
  //     auto response = client->send(kj::HttpMethod::GET, "http://example.com", headers)
  //         .wait(waitScope);
  //     auto msg = kj::str("Response content type: ", response.headers.get(contentType));

  struct IdsByNameMap;

public:
  HttpHeaderTable();
  // Constructs a table that only contains the builtin headers.

  class Builder {
  public:
    Builder();
    HttpHeaderId add(kj::StringPtr name);
    Own<HttpHeaderTable> build();

    HttpHeaderTable& getFutureTable();
    // Get the still-unbuilt header table. You cannot actually use it until build() has been
    // called.
    //
    // This method exists to help when building a shared header table -- the Builder may be passed
    // to several components, each of which will register the headers they need and get a reference
    // to the future table.

  private:
    kj::Own<HttpHeaderTable> table;
  };

  KJ_DISALLOW_COPY(HttpHeaderTable);  // Can't copy because HttpHeaderId points to the table.
  ~HttpHeaderTable() noexcept(false);

  uint idCount() const;
  // Return the number of IDs in the table.

  kj::Maybe<HttpHeaderId> stringToId(kj::StringPtr name) const;
  // Try to find an ID for the given name. The matching is case-insensitive, per the HTTP spec.
  //
  // Note: if `name` contains characters that aren't allowed in HTTP header names, this may return
  //   a bogus value rather than null, due to optimizations used in case-insensitive matching.

  kj::StringPtr idToString(HttpHeaderId id) const;
  // Get the canonical string name for the given ID.

private:
  kj::Vector<kj::StringPtr> namesById;
  kj::Own<IdsByNameMap> idsByName;
};

class HttpHeaders {
  // Represents a set of HTTP headers.
  //
  // This class guards against basic HTTP header injection attacks: Trying to set a header name or
  // value containing a newline, carriage return, or other invalid character will throw an
  // exception.

public:
  explicit HttpHeaders(const HttpHeaderTable& table);

  KJ_DISALLOW_COPY(HttpHeaders);
  HttpHeaders(HttpHeaders&&) = default;
  HttpHeaders& operator=(HttpHeaders&&) = default;

  size_t size() const;
  // Returns the number of headers that forEach() would iterate over.

  void clear();
  // Clears all contents, as if the object was freshly-allocated. However, calling this rather
  // than actually re-allocating the object may avoid re-allocation of internal objects.

  HttpHeaders clone() const;
  // Creates a deep clone of the HttpHeaders. The returned object owns all strings it references.

  HttpHeaders cloneShallow() const;
  // Creates a shallow clone of the HttpHeaders. The returned object references the same strings
  // as the original, owning none of them.

  bool isWebSocket() const;
  // Convenience method that checks for the presence of the header `Upgrade: websocket`.
  //
  // Note that this does not actually validate that the request is a complete WebSocket handshake
  // with the correct version number -- such validation will occur if and when you call
  // acceptWebSocket().

  kj::Maybe<kj::StringPtr> get(HttpHeaderId id) const;
  // Read a header.

  template <typename Func>
  void forEach(Func&& func) const;
  // Calls `func(name, value)` for each header in the set -- including headers that aren't mapped
  // to IDs in the header table. Both inputs are of type kj::StringPtr.

  template <typename Func1, typename Func2>
  void forEach(Func1&& func1, Func2&& func2) const;
  // Calls `func1(id, value)` for each header in the set that has a registered HttpHeaderId, and
  // `func2(name, value)` for each header that does not. All calls to func1() preceed all calls to
  // func2().

  void set(HttpHeaderId id, kj::StringPtr value);
  void set(HttpHeaderId id, kj::String&& value);
  // Sets a header value, overwriting the existing value.
  //
  // The String&& version is equivalent to calling the other version followed by takeOwnership().
  //
  // WARNING: It is the caller's responsibility to ensure that `value` remains valid until the
  //   HttpHeaders object is destroyed. This allows string literals to be passed without making a
  //   copy, but complicates the use of dynamic values. Hint: Consider using `takeOwnership()`.

  void add(kj::StringPtr name, kj::StringPtr value);
  void add(kj::StringPtr name, kj::String&& value);
  void add(kj::String&& name, kj::String&& value);
  // Append a header. `name` will be looked up in the header table, but if it's not mapped, the
  // header will be added to the list of unmapped headers.
  //
  // The String&& versions are equivalent to calling the other version followed by takeOwnership().
  //
  // WARNING: It is the caller's responsibility to ensure that `name` and `value` remain valid
  //   until the HttpHeaders object is destroyed. This allows string literals to be passed without
  //   making a copy, but complicates the use of dynamic values. Hint: Consider using
  //   `takeOwnership()`.

  void unset(HttpHeaderId id);
  // Removes a header.
  //
  // It's not possible to remove a header by string name because non-indexed headers would take
  // O(n) time to remove. Instead, construct a new HttpHeaders object and copy contents.

  void takeOwnership(kj::String&& string);
  void takeOwnership(kj::Array<char>&& chars);
  void takeOwnership(HttpHeaders&& otherHeaders);
  // Takes overship of a string so that it lives until the HttpHeaders object is destroyed. Useful
  // when you've passed a dynamic value to set() or add() or parse*().

  struct Request {
    HttpMethod method;
    kj::StringPtr url;
  };
  struct Response {
    uint statusCode;
    kj::StringPtr statusText;
  };

  struct ProtocolError {
    // Represents a protocol error, such as a bad request method or invalid headers. Debugging such
    // errors is difficult without a copy of the data which we tried to parse, but this data is
    // sensitive, so we can't just lump it into the error description directly. ProtocolError
    // provides this sensitive data separate from the error description.
    //
    // TODO(cleanup): Should maybe not live in HttpHeaders? HttpServerErrorHandler::ProtocolError?
    //   Or HttpProtocolError? Or maybe we need a more general way of attaching sensitive context to
    //   kj::Exceptions?

    uint statusCode;
    // Suggested HTTP status code that should be used when returning an error to the client.
    //
    // Most errors are 400. An unrecognized method will be 501.

    kj::StringPtr statusMessage;
    // HTTP status message to go with `statusCode`, e.g. "Bad Request".

    kj::StringPtr description;
    // An error description safe for all the world to see.

    kj::ArrayPtr<char> rawContent;
    // Unredacted data which led to the error condition. This may contain anything transported over
    // HTTP, to include sensitive PII, so you must take care to sanitize this before using it in any
    // error report that may leak to unprivileged eyes.
    //
    // This ArrayPtr is merely a copy of the `content` parameter passed to `tryParseRequest()` /
    // `tryParseResponse()`, thus it remains valid for as long as a successfully-parsed HttpHeaders
    // object would remain valid.
  };

  using RequestOrProtocolError = kj::OneOf<Request, ProtocolError>;
  using ResponseOrProtocolError = kj::OneOf<Response, ProtocolError>;

  RequestOrProtocolError tryParseRequest(kj::ArrayPtr<char> content);
  ResponseOrProtocolError tryParseResponse(kj::ArrayPtr<char> content);
  // Parse an HTTP header blob and add all the headers to this object.
  //
  // `content` should be all text from the start of the request to the first occurrance of two
  // newlines in a row -- including the first of these two newlines, but excluding the second.
  //
  // The parse is performed with zero copies: The callee clobbers `content` with '\0' characters
  // to split it into a bunch of shorter strings. The caller must keep `content` valid until the
  // `HttpHeaders` is destroyed, or pass it to `takeOwnership()`.

  bool tryParse(kj::ArrayPtr<char> content);
  // Like tryParseRequest()/tryParseResponse(), but don't expect any request/response line.

  kj::String serializeRequest(HttpMethod method, kj::StringPtr url,
                              kj::ArrayPtr<const kj::StringPtr> connectionHeaders = nullptr) const;
  kj::String serializeResponse(uint statusCode, kj::StringPtr statusText,
                               kj::ArrayPtr<const kj::StringPtr> connectionHeaders = nullptr) const;
  // **Most applications will not use these methods; they are called by the HTTP client and server
  // implementations.**
  //
  // Serialize the headers as a complete request or response blob. The blob uses '\r\n' newlines
  // and includes the double-newline to indicate the end of the headers.
  //
  // `connectionHeaders`, if provided, contains connection-level headers supplied by the HTTP
  // implementation, in the order specified by the KJ_HTTP_FOR_EACH_BUILTIN_HEADER macro. These
  // headers values override any corresponding header value in the HttpHeaders object. The
  // CONNECTION_HEADERS_COUNT constants below can help you construct this `connectionHeaders` array.

  enum class BuiltinIndicesEnum {
  #define HEADER_ID(id, name) id,
    KJ_HTTP_FOR_EACH_BUILTIN_HEADER(HEADER_ID)
  #undef HEADER_ID
  };

  struct BuiltinIndices {
  #define HEADER_ID(id, name) static constexpr uint id = static_cast<uint>(BuiltinIndicesEnum::id);
    KJ_HTTP_FOR_EACH_BUILTIN_HEADER(HEADER_ID)
  #undef HEADER_ID
  };

  static constexpr uint HEAD_RESPONSE_CONNECTION_HEADERS_COUNT = BuiltinIndices::CONTENT_LENGTH;
  static constexpr uint CONNECTION_HEADERS_COUNT = BuiltinIndices::SEC_WEBSOCKET_KEY;
  static constexpr uint WEBSOCKET_CONNECTION_HEADERS_COUNT = BuiltinIndices::HOST;
  // Constants for use with HttpHeaders::serialize*().

  kj::String toString() const;

private:
  const HttpHeaderTable* table;

  kj::Array<kj::StringPtr> indexedHeaders;
  // Size is always table->idCount().

  struct Header {
    kj::StringPtr name;
    kj::StringPtr value;
  };
  kj::Vector<Header> unindexedHeaders;

  kj::Vector<kj::Array<char>> ownedStrings;

  void addNoCheck(kj::StringPtr name, kj::StringPtr value);

  kj::StringPtr cloneToOwn(kj::StringPtr str);

  kj::String serialize(kj::ArrayPtr<const char> word1,
                       kj::ArrayPtr<const char> word2,
                       kj::ArrayPtr<const char> word3,
                       kj::ArrayPtr<const kj::StringPtr> connectionHeaders) const;

  bool parseHeaders(char* ptr, char* end);

  // TODO(perf): Arguably we should store a map, but header sets are never very long
  // TODO(perf): We could optimize for common headers by storing them directly as fields. We could
  //   also add direct accessors for those headers.
};

class HttpInputStream {
  // Low-level interface to receive HTTP-formatted messages (headers followed by body) from an
  // input stream, without a paired output stream.
  //
  // Most applications will not use this. Regular HTTP clients and servers don't need this. This
  // is mainly useful for apps implementing various protocols that look like HTTP but aren't
  // really.

public:
  struct Request {
    HttpMethod method;
    kj::StringPtr url;
    const HttpHeaders& headers;
    kj::Own<kj::AsyncInputStream> body;
  };
  virtual kj::Promise<Request> readRequest() = 0;
  // Reads one HTTP request from the input stream.
  //
  // The returned struct contains pointers directly into a buffer that is invalidated on the next
  // message read.

  struct Response {
    uint statusCode;
    kj::StringPtr statusText;
    const HttpHeaders& headers;
    kj::Own<kj::AsyncInputStream> body;
  };
  virtual kj::Promise<Response> readResponse(HttpMethod requestMethod) = 0;
  // Reads one HTTP response from the input stream.
  //
  // You must provide the request method because responses to HEAD requests require special
  // treatment.
  //
  // The returned struct contains pointers directly into a buffer that is invalidated on the next
  // message read.

  struct Message {
    const HttpHeaders& headers;
    kj::Own<kj::AsyncInputStream> body;
  };
  virtual kj::Promise<Message> readMessage() = 0;
  // Reads an HTTP header set followed by a body, with no request or response line. This is not
  // useful for HTTP but may be useful for other protocols that make the unfortunate choice to
  // mimic HTTP message format, such as Visual Studio Code's JSON-RPC transport.
  //
  // The returned struct contains pointers directly into a buffer that is invalidated on the next
  // message read.

  virtual kj::Promise<bool> awaitNextMessage() = 0;
  // Waits until more data is available, but doesn't consume it. Returns false on EOF.
};

class EntropySource {
  // Interface for an object that generates entropy. Typically, cryptographically-random entropy
  // is expected.
  //
  // TODO(cleanup): Put this somewhere more general.

public:
  virtual void generate(kj::ArrayPtr<byte> buffer) = 0;
};

class WebSocket {
  // Interface representincg an open WebSocket session.
  //
  // Each side can send and receive data and "close" messages.
  //
  // Ping/Pong and message fragmentation are not exposed through this interface. These features of
  // the underlying WebSocket protocol are not exposed by the browser-level Javascript API either,
  // and thus applications typically need to implement these features at the application protocol
  // level instead. The implementation is, however, expected to reply to Ping messages it receives.

public:
  virtual kj::Promise<void> send(kj::ArrayPtr<const byte> message) = 0;
  virtual kj::Promise<void> send(kj::ArrayPtr<const char> message) = 0;
  // Send a message (binary or text). The underlying buffer must remain valid, and you must not
  // call send() again, until the returned promise resolves.

  virtual kj::Promise<void> close(uint16_t code, kj::StringPtr reason) = 0;
  // Send a Close message.
  //
  // Note that the returned Promise resolves once the message has been sent -- it does NOT wait
  // for the other end to send a Close reply. The application should await a reply before dropping
  // the WebSocket object.

  virtual kj::Promise<void> disconnect() = 0;
  // Sends EOF on the underlying connection without sending a "close" message. This is NOT a clean
  // shutdown, but is sometimes useful when you want the other end to trigger whatever behavior
  // it normally triggers when a connection is dropped.

  virtual void abort() = 0;
  // Forcefully close this WebSocket, such that the remote end should get a DISCONNECTED error if
  // it continues to write. This differs from disconnect(), which only closes the sending
  // direction, but still allows receives.

  virtual kj::Promise<void> whenAborted() = 0;
  // Resolves when the remote side aborts the connection such that send() would throw DISCONNECTED,
  // if this can be detected without actually writing a message. (If not, this promise never
  // resolves, but send() or receive() will throw DISCONNECTED when appropriate. See also
  // kj::AsyncOutputStream::whenWriteDisconnected().)

  struct Close {
    uint16_t code;
    kj::String reason;
  };

  typedef kj::OneOf<kj::String, kj::Array<byte>, Close> Message;

  virtual kj::Promise<Message> receive() = 0;
  // Read one message from the WebSocket and return it. Can only call once at a time. Do not call
  // again after Close is received.

  virtual kj::Promise<void> pumpTo(WebSocket& other);
  // Continuously receives messages from this WebSocket and send them to `other`.
  //
  // On EOF, calls other.disconnect(), then resolves.
  //
  // On other read errors, calls other.close() with the error, then resolves.
  //
  // On write error, rejects with the error.

  virtual kj::Maybe<kj::Promise<void>> tryPumpFrom(WebSocket& other);
  // Either returns null, or performs the equivalent of other.pumpTo(*this). Only returns non-null
  // if this WebSocket implementation is able to perform the pump in an optimized way, better than
  // the default implementation of pumpTo(). The default implementation of pumpTo() always tries
  // calling this first, and the default implementation of tryPumpFrom() always returns null.
};

class HttpClient {
  // Interface to the client end of an HTTP connection.
  //
  // There are two kinds of clients:
  // * Host clients are used when talking to a specific host. The `url` specified in a request
  //   is actually just a path. (A `Host` header is still required in all requests.)
  // * Proxy clients are used when the target could be any arbitrary host on the internet.
  //   The `url` specified in a request is a full URL including protocol and hostname.

public:
  struct Response {
    uint statusCode;
    kj::StringPtr statusText;
    const HttpHeaders* headers;
    kj::Own<kj::AsyncInputStream> body;
    // `statusText` and `headers` remain valid until `body` is dropped or read from.
  };

  struct Request {
    kj::Own<kj::AsyncOutputStream> body;
    // Write the request entity body to this stream, then drop it when done.
    //
    // May be null for GET and HEAD requests (which have no body) and requests that have
    // Content-Length: 0.

    kj::Promise<Response> response;
    // Promise for the eventual respnose.
  };

  virtual Request request(HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
                          kj::Maybe<uint64_t> expectedBodySize = nullptr) = 0;
  // Perform an HTTP request.
  //
  // `url` may be a full URL (with protocol and host) or it may be only the path part of the URL,
  // depending on whether the client is a proxy client or a host client.
  //
  // `url` and `headers` need only remain valid until `request()` returns (they can be
  // stack-allocated).
  //
  // `expectedBodySize`, if provided, must be exactly the number of bytes that will be written to
  // the body. This will trigger use of the `Content-Length` connection header. Otherwise,
  // `Transfer-Encoding: chunked` will be used.

  struct WebSocketResponse {
    uint statusCode;
    kj::StringPtr statusText;
    const HttpHeaders* headers;
    kj::OneOf<kj::Own<kj::AsyncInputStream>, kj::Own<WebSocket>> webSocketOrBody;
    // `statusText` and `headers` remain valid until `webSocketOrBody` is dropped or read from.
  };
  virtual kj::Promise<WebSocketResponse> openWebSocket(
      kj::StringPtr url, const HttpHeaders& headers);
  // Tries to open a WebSocket. Default implementation calls send() and never returns a WebSocket.
  //
  // `url` and `headers` need only remain valid until `openWebSocket()` returns (they can be
  // stack-allocated).

  virtual kj::Promise<kj::Own<kj::AsyncIoStream>> connect(kj::StringPtr host);
  // Handles CONNECT requests. Only relevant for proxy clients. Default implementation throws
  // UNIMPLEMENTED.
};

class HttpService {
  // Interface which HTTP services should implement.
  //
  // This interface is functionally equivalent to HttpClient, but is intended for applications to
  // implement rather than call. The ergonomics and performance of the method signatures are
  // optimized for the serving end.
  //
  // As with clients, there are two kinds of services:
  // * Host services are used when talking to a specific host. The `url` specified in a request
  //   is actually just a path. (A `Host` header is still required in all requests, and the service
  //   may in fact serve multiple origins via this header.)
  // * Proxy services are used when the target could be any arbitrary host on the internet, i.e. to
  //   implement an HTTP proxy. The `url` specified in a request is a full URL including protocol
  //   and hostname.

public:
  class Response {
  public:
    virtual kj::Own<kj::AsyncOutputStream> send(
        uint statusCode, kj::StringPtr statusText, const HttpHeaders& headers,
        kj::Maybe<uint64_t> expectedBodySize = nullptr) = 0;
    // Begin the response.
    //
    // `statusText` and `headers` need only remain valid until send() returns (they can be
    // stack-allocated).

    virtual kj::Own<WebSocket> acceptWebSocket(const HttpHeaders& headers) = 0;
    // If headers.isWebSocket() is true then you can call acceptWebSocket() instead of send().

    kj::Promise<void> sendError(uint statusCode, kj::StringPtr statusText,
                                const HttpHeaders& headers);
    kj::Promise<void> sendError(uint statusCode, kj::StringPtr statusText,
                                const HttpHeaderTable& headerTable);
    // Convenience wrapper around send() which sends a basic error. A generic error page specifying
    // the error code is sent as the body.
    //
    // You must provide headers or a header table because downstream service wrappers may be
    // expecting response headers built with a particular table so that they can insert additional
    // headers.
  };

  virtual kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) = 0;
  // Perform an HTTP request.
  //
  // `url` may be a full URL (with protocol and host) or it may be only the path part of the URL,
  // depending on whether the service is a proxy service or a host service.
  //
  // `url` and `headers` are invalidated on the first read from `requestBody` or when the returned
  // promise resolves, whichever comes first.
  //
  // Request processing can be canceled by dropping the returned promise. HttpServer may do so if
  // the client disconnects prematurely.

  virtual kj::Promise<kj::Own<kj::AsyncIoStream>> connect(kj::StringPtr host);
  // Handles CONNECT requests. Only relevant for proxy services. Default implementation throws
  // UNIMPLEMENTED.
};

struct HttpClientSettings {
  kj::Duration idleTimout = 5 * kj::SECONDS;
  // For clients which automatically create new connections, any connection idle for at least this
  // long will be closed. Set this to 0 to prevent connection reuse entirely.

  kj::Maybe<EntropySource&> entropySource = nullptr;
  // Must be provided in order to use `openWebSocket`. If you don't need WebSockets, this can be
  // omitted. The WebSocket protocol uses random values to avoid triggering flaws (including
  // security flaws) in certain HTTP proxy software. Specifically, entropy is used to generate the
  // `Sec-WebSocket-Key` header and to generate frame masks. If you know that there are no broken
  // or vulnerable proxies between you and the server, you can provide a dummy entropy source that
  // doesn't generate real entropy (e.g. returning the same value every time). Otherwise, you must
  // provide a cryptographically-random entropy source.
};

kj::Own<HttpClient> newHttpClient(kj::Timer& timer, HttpHeaderTable& responseHeaderTable,
                                  kj::Network& network, kj::Maybe<kj::Network&> tlsNetwork,
                                  HttpClientSettings settings = HttpClientSettings());
// Creates a proxy HttpClient that connects to hosts over the given network. The URL must always
// be an absolute URL; the host is parsed from the URL. This implementation will automatically
// add an appropriate Host header (and convert the URL to just a path) once it has connected.
//
// Note that if you wish to route traffic through an HTTP proxy server rather than connect to
// remote hosts directly, you should use the form of newHttpClient() that takes a NetworkAddress,
// and supply the proxy's address.
//
// `responseHeaderTable` is used when parsing HTTP responses. Requests can use any header table.
//
// `tlsNetwork` is required to support HTTPS destination URLs. If null, only HTTP URLs can be
// fetched.

kj::Own<HttpClient> newHttpClient(kj::Timer& timer, HttpHeaderTable& responseHeaderTable,
                                  kj::NetworkAddress& addr,
                                  HttpClientSettings settings = HttpClientSettings());
// Creates an HttpClient that always connects to the given address no matter what URL is requested.
// The client will open and close connections as needed. It will attempt to reuse connections for
// multiple requests but will not send a new request before the previous response on the same
// connection has completed, as doing so can result in head-of-line blocking issues. The client may
// be used as a proxy client or a host client depending on whether the peer is operating as
// a proxy. (Hint: This is the best kind of client to use when routing traffic through an HTTP
// proxy. `addr` should be the address of the proxy, and the proxy itself will resolve remote hosts
// based on the URLs passed to it.)
//
// `responseHeaderTable` is used when parsing HTTP responses. Requests can use any header table.

kj::Own<HttpClient> newHttpClient(HttpHeaderTable& responseHeaderTable, kj::AsyncIoStream& stream,
                                  HttpClientSettings settings = HttpClientSettings());
// Creates an HttpClient that speaks over the given pre-established connection. The client may
// be used as a proxy client or a host client depending on whether the peer is operating as
// a proxy.
//
// Note that since this client has only one stream to work with, it will try to pipeline all
// requests on this stream. If one request or response has an I/O failure, all subsequent requests
// fail as well. If the destination server chooses to close the connection after a response,
// subsequent requests will fail. If a response takes a long time, it blocks subsequent responses.
// If a WebSocket is opened successfully, all subsequent requests fail.

kj::Own<HttpClient> newConcurrencyLimitingHttpClient(
    HttpClient& inner, uint maxConcurrentRequests,
    kj::Function<void(uint runningCount, uint pendingCount)> countChangedCallback);
// Creates an HttpClient that is limited to a maximum number of concurrent requests.  Additional
// requests are queued, to be opened only after an open request completes.  `countChangedCallback`
// is called when a new connection is opened or enqueued and when an open connection is closed,
// passing the number of open and pending connections.

kj::Own<HttpClient> newHttpClient(HttpService& service);
kj::Own<HttpService> newHttpService(HttpClient& client);
// Adapts an HttpClient to an HttpService and vice versa.

kj::Own<HttpInputStream> newHttpInputStream(
    kj::AsyncInputStream& input, HttpHeaderTable& headerTable);
// Create an HttpInputStream on top of the given stream. Normally applications would not call this
// directly, but it can be useful for implementing protocols that aren't quite HTTP but use similar
// message delimiting.
//
// The HttpInputStream implementation does read-ahead buffering on `input`. Therefore, when the
// HttpInputStream is destroyed, some data read from `input` may be lost, so it's not possible to
// continue reading from `input` in a reliable way.

kj::Own<WebSocket> newWebSocket(kj::Own<kj::AsyncIoStream> stream,
                                kj::Maybe<EntropySource&> maskEntropySource);
// Create a new WebSocket on top of the given stream. It is assumed that the HTTP -> WebSocket
// upgrade handshake has already occurred (or is not needed), and messages can immediately be
// sent and received on the stream. Normally applications would not call this directly.
//
// `maskEntropySource` is used to generate cryptographically-random frame masks. If null, outgoing
// frames will not be masked. Servers are required NOT to mask their outgoing frames, but clients
// ARE required to do so. So, on the client side, you MUST specify an entropy source. The mask
// must be crytographically random if the data being sent on the WebSocket may be malicious. The
// purpose of the mask is to prevent badly-written HTTP proxies from interpreting "things that look
// like HTTP requests" in a message as being actual HTTP requests, which could result in cache
// poisoning. See RFC6455 section 10.3.

struct WebSocketPipe {
  kj::Own<WebSocket> ends[2];
};

WebSocketPipe newWebSocketPipe();
// Create a WebSocket pipe. Messages written to one end of the pipe will be readable from the other
// end. No buffering occurs -- a message send does not complete until a corresponding receive
// accepts the message.

class HttpServerErrorHandler;

struct HttpServerSettings {
  kj::Duration headerTimeout = 15 * kj::SECONDS;
  // After initial connection open, or after receiving the first byte of a pipelined request,
  // the client must send the complete request within this time.

  kj::Duration pipelineTimeout = 5 * kj::SECONDS;
  // After one request/response completes, we'll wait up to this long for a pipelined request to
  // arrive.

  kj::Duration canceledUploadGacePeriod = 1 * kj::SECONDS;
  size_t canceledUploadGraceBytes = 65536;
  // If the HttpService sends a response and returns without having read the entire request body,
  // then we have to decide whether to close the connection or wait for the client to finish the
  // request so that it can pipeline the next one. We'll give them a grace period defined by the
  // above two values -- if they hit either one, we'll close the socket, but if the request
  // completes, we'll let the connection stay open to handle more requests.

  kj::Maybe<HttpServerErrorHandler&> errorHandler = nullptr;
  // Customize how client protocol errors and service application exceptions are handled by the
  // HttpServer. If null, HttpServerErrorHandler's default implementation will be used.
};

class HttpServerErrorHandler {
public:
  virtual kj::Promise<void> handleClientProtocolError(
      HttpHeaders::ProtocolError protocolError, kj::HttpService::Response& response);
  virtual kj::Promise<void> handleApplicationError(
      kj::Exception exception, kj::Maybe<kj::HttpService::Response&> response);
  virtual kj::Promise<void> handleNoResponse(kj::HttpService::Response& response);
  // Override these functions to customize error handling during the request/response cycle.
  //
  // Client protocol errors arise when the server receives an HTTP message that fails to parse. As
  // such, HttpService::request() will not have been called yet, and the handler is always
  // guaranteed an opportunity to send a response. The default implementation of
  // handleClientProtocolError() replies with a 400 Bad Request response.
  //
  // Application errors arise when HttpService::request() throws an exception. The default
  // implementation of handleApplicationError() maps the following exception types to HTTP statuses,
  // and generates bodies from the stringified exceptions:
  //
  //   - OVERLOADED: 503 Service Unavailable
  //   - UNIMPLEMENTED: 501 Not Implemented
  //   - DISCONNECTED: (no response)
  //   - FAILED: 500 Internal Server Error
  //
  // No-response errors occur when HttpService::request() allows its promise to settle before
  // sending a response. The default implementation of handleNoResponse() replies with a 500
  // Internal Server Error response.
  //
  // Unlike `HttpService::request()`, when calling `response.send()` in the context of one of these
  // functions, a "Connection: close" header will be added, and the connection will be closed.
  //
  // Also unlike `HttpService::request()`, it is okay to return kj::READY_NOW without calling
  // `response.send()`. In this case, no response will be sent, and the connection will be closed.
};

class HttpServer final: private kj::TaskSet::ErrorHandler {
  // Class which listens for requests on ports or connections and sends them to an HttpService.

public:
  typedef HttpServerSettings Settings;
  typedef kj::Function<kj::Own<HttpService>(kj::AsyncIoStream&)> HttpServiceFactory;

  HttpServer(kj::Timer& timer, HttpHeaderTable& requestHeaderTable, HttpService& service,
             Settings settings = Settings());
  // Set up an HttpServer that directs incoming connections to the given service. The service
  // may be a host service or a proxy service depending on whether you are intending to implement
  // an HTTP server or an HTTP proxy.

  HttpServer(kj::Timer& timer, HttpHeaderTable& requestHeaderTable,
             HttpServiceFactory serviceFactory, Settings settings = Settings());
  // Like the other constructor, but allows a new HttpService object to be used for each
  // connection, based on the connection object. This is particularly useful for capturing the
  // client's IP address and injecting it as a header.

  kj::Promise<void> drain();
  // Stop accepting new connections or new requests on existing connections. Finish any requests
  // that are already executing, then close the connections. Returns once no more requests are
  // in-flight.

  kj::Promise<void> listenHttp(kj::ConnectionReceiver& port);
  // Accepts HTTP connections on the given port and directs them to the handler.
  //
  // The returned promise never completes normally. It may throw if port.accept() throws. Dropping
  // the returned promise will cause the server to stop listening on the port, but already-open
  // connections will continue to be served. Destroy the whole HttpServer to cancel all I/O.

  kj::Promise<void> listenHttp(kj::Own<kj::AsyncIoStream> connection);
  // Reads HTTP requests from the given connection and directs them to the handler. A successful
  // completion of the promise indicates that all requests received on the connection resulted in
  // a complete response, and the client closed the connection gracefully or drain() was called.
  // The promise throws if an unparseable request is received or if some I/O error occurs. Dropping
  // the returned promise will cancel all I/O on the connection and cancel any in-flight requests.

  kj::Promise<bool> listenHttpCleanDrain(kj::AsyncIoStream& connection);
  // Like listenHttp(), but allows you to potentially drain the server without closing connections.
  // The returned promise resolves to `true` if the connection has been left in a state where a
  // new HttpServer could potentially accept further requests from it. If `false`, then the
  // connection is either in an inconsistent state or already completed a closing handshake; the
  // caller should close it without any further reads/writes. Note this only ever returns `true`
  // if you called `drain()` -- otherwise this server would keep handling the connection.

private:
  class Connection;

  kj::Timer& timer;
  HttpHeaderTable& requestHeaderTable;
  kj::OneOf<HttpService*, HttpServiceFactory> service;
  Settings settings;

  bool draining = false;
  kj::ForkedPromise<void> onDrain;
  kj::Own<kj::PromiseFulfiller<void>> drainFulfiller;

  uint connectionCount = 0;
  kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> zeroConnectionsFulfiller;

  kj::TaskSet tasks;

  HttpServer(kj::Timer& timer, HttpHeaderTable& requestHeaderTable,
             kj::OneOf<HttpService*, HttpServiceFactory> service,
             Settings settings, kj::PromiseFulfillerPair<void> paf);

  kj::Promise<void> listenLoop(kj::ConnectionReceiver& port);

  void taskFailed(kj::Exception&& exception) override;
};

// =======================================================================================
// inline implementation

inline void HttpHeaderId::requireFrom(const HttpHeaderTable& table) const {
  KJ_IREQUIRE(this->table == nullptr || this->table == &table,
      "the provided HttpHeaderId is from the wrong HttpHeaderTable");
}

inline kj::Own<HttpHeaderTable> HttpHeaderTable::Builder::build() { return kj::mv(table); }
inline HttpHeaderTable& HttpHeaderTable::Builder::getFutureTable() { return *table; }

inline uint HttpHeaderTable::idCount() const { return namesById.size(); }

inline kj::StringPtr HttpHeaderTable::idToString(HttpHeaderId id) const {
  id.requireFrom(*this);
  return namesById[id.id];
}

inline kj::Maybe<kj::StringPtr> HttpHeaders::get(HttpHeaderId id) const {
  id.requireFrom(*table);
  auto result = indexedHeaders[id.id];
  return result == nullptr ? kj::Maybe<kj::StringPtr>(nullptr) : result;
}

inline void HttpHeaders::unset(HttpHeaderId id) {
  id.requireFrom(*table);
  indexedHeaders[id.id] = nullptr;
}

template <typename Func>
inline void HttpHeaders::forEach(Func&& func) const {
  for (auto i: kj::indices(indexedHeaders)) {
    if (indexedHeaders[i] != nullptr) {
      func(table->idToString(HttpHeaderId(table, i)), indexedHeaders[i]);
    }
  }

  for (auto& header: unindexedHeaders) {
    func(header.name, header.value);
  }
}

template <typename Func1, typename Func2>
inline void HttpHeaders::forEach(Func1&& func1, Func2&& func2) const {
  for (auto i: kj::indices(indexedHeaders)) {
    if (indexedHeaders[i] != nullptr) {
      func1(HttpHeaderId(table, i), indexedHeaders[i]);
    }
  }

  for (auto& header: unindexedHeaders) {
    func2(header.name, header.value);
  }
}

}  // namespace kj
