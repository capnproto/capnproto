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
#include <kj/parse/char.h>
#include <unordered_map>
#include <stdlib.h>

namespace kj {

static const char* METHOD_NAMES[] = {
#define METHOD_NAME(id) #id,
KJ_HTTP_FOR_EACH_METHOD(METHOD_NAME)
#undef METHOD_NAME
};

kj::StringPtr KJ_STRINGIFY(HttpMethod method) {
  return METHOD_NAMES[static_cast<uint>(method)];
}

static kj::Maybe<HttpMethod> consumeHttpMethod(char*& ptr) {
  char* p = ptr;

#define EXPECT_REST(prefix, suffix) \
  if (strncmp(p, #suffix, sizeof(#suffix)-1) == 0) { \
    ptr = p + (sizeof(#suffix)-1); \
    return HttpMethod::prefix##suffix; \
  } else { \
    return nullptr; \
  }

  switch (*p++) {
    case 'C':
      switch (*p++) {
        case 'H': EXPECT_REST(CH,ECKOUT)
        case 'O': EXPECT_REST(CO,PY)
        default: return nullptr;
      }
    case 'D': EXPECT_REST(D,ELETE)
    case 'G': EXPECT_REST(G,ET)
    case 'H': EXPECT_REST(H,EAD)
    case 'L': EXPECT_REST(L,OCK)
    case 'M':
      switch (*p++) {
        case 'E': EXPECT_REST(ME,RGE)
        case 'K':
          switch (*p++) {
            case 'A': EXPECT_REST(MKA,CTIVITY)
            case 'C': EXPECT_REST(MKC,OL)
            default: return nullptr;
          }
        case 'O': EXPECT_REST(MO,VE)
        case 'S': EXPECT_REST(MS,EARCH)
        default: return nullptr;
      }
    case 'N': EXPECT_REST(N,OTIFY)
    case 'O': EXPECT_REST(O,PTIONS)
    case 'P':
      switch (*p++) {
        case 'A': EXPECT_REST(PA,TCH)
        case 'O': EXPECT_REST(PO,ST)
        case 'R':
          if (*p++ != 'O' || *p++ != 'P') return nullptr;
          switch (*p++) {
            case 'F': EXPECT_REST(PROPF,IND)
            case 'P': EXPECT_REST(PROPP,ATCH)
            default: return nullptr;
          }
        case 'U':
          switch (*p++) {
            case 'R': EXPECT_REST(PUR,GE)
            case 'T': EXPECT_REST(PUT,)
            default: return nullptr;
          }
        default: return nullptr;
      }
    case 'R': EXPECT_REST(R,EPORT)
    case 'S':
      switch (*p++) {
        case 'E': EXPECT_REST(SE,ARCH)
        case 'U': EXPECT_REST(SU,BSCRIBE)
        default: return nullptr;
      }
    case 'T': EXPECT_REST(T,RACE)
    case 'U':
      if (*p++ != 'N') return nullptr;
      switch (*p++) {
        case 'L': EXPECT_REST(UNL,OCK)
        case 'S': EXPECT_REST(UNS,UBSCRIBE)
        default: return nullptr;
      }
    default: return nullptr;
  }
#undef EXPECT_REST
}

kj::Maybe<HttpMethod> tryParseHttpMethod(kj::StringPtr name) {
  // const_cast OK because we don't actually access it. consumeHttpMethod() is also called by some
  // code later than explicitly needs to use a non-const pointer.
  char* ptr = const_cast<char*>(name.begin());
  auto result = consumeHttpMethod(ptr);
  if (*ptr == '\0') {
    return result;
  } else {
    return nullptr;
  }
}

// =======================================================================================

namespace {

constexpr auto HTTP_SEPARATOR_CHARS = kj::parse::anyOfChars("()<>@,;:\\\"/[]?={} \t");
// RFC2616 section 2.2: https://www.w3.org/Protocols/rfc2616/rfc2616-sec2.html#sec2.2

constexpr auto HTTP_TOKEN_CHARS =
    kj::parse::controlChar.orChar('\x7f')
    .orGroup(kj::parse::whitespaceChar)
    .orGroup(HTTP_SEPARATOR_CHARS)
    .invert();
// RFC2616 section 2.2: https://www.w3.org/Protocols/rfc2616/rfc2616-sec2.html#sec2.2

constexpr auto HTTP_HEADER_NAME_CHARS = HTTP_TOKEN_CHARS;
// RFC2616 section 4.2: https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2

static void requireValidHeaderName(kj::StringPtr name) {
  for (char c: name) {
    KJ_REQUIRE(HTTP_HEADER_NAME_CHARS.contains(c), "invalid header name", name);
  }
}

static void requireValidHeaderValue(kj::StringPtr value) {
  for (char c: value) {
    KJ_REQUIRE(c >= 0x20, "invalid header value", value);
  }
}

static const char* BUILTIN_HEADER_NAMES[] = {
  // Indexed by header ID, which includes connection headers, so we include those names too.
#define HEADER_NAME(id, name) name,
  KJ_HTTP_FOR_EACH_BUILTIN_HEADER(HEADER_NAME)
#undef HEADER_NAME
};

enum class BuiltinHeaderIndices {
#define HEADER_ID(id, name) id,
  KJ_HTTP_FOR_EACH_BUILTIN_HEADER(HEADER_ID)
#undef HEADER_ID
};

static constexpr size_t CONNECTION_HEADER_COUNT KJ_UNUSED = 0
#define COUNT_HEADER(id, name) + 1
  KJ_HTTP_FOR_EACH_CONNECTION_HEADER(COUNT_HEADER)
#undef COUNT_HEADER
  ;

enum class ConnectionHeaderIndices {
#define HEADER_ID(id, name) id,
  KJ_HTTP_FOR_EACH_CONNECTION_HEADER(HEADER_ID)
#undef HEADER_ID
};

static constexpr uint CONNECTION_HEADER_XOR = kj::maxValue;
static constexpr uint CONNECTION_HEADER_THRESHOLD = CONNECTION_HEADER_XOR >> 1;

}  // namespace

#define DEFINE_HEADER(id, name) \
const HttpHeaderId HttpHeaderId::id(nullptr, static_cast<uint>(BuiltinHeaderIndices::id));
KJ_HTTP_FOR_EACH_BUILTIN_HEADER(DEFINE_HEADER)
#undef DEFINE_HEADER

kj::StringPtr HttpHeaderId::toString() const {
  if (table == nullptr) {
    KJ_ASSERT(id < kj::size(BUILTIN_HEADER_NAMES));
    return BUILTIN_HEADER_NAMES[id];
  } else {
    return table->idToString(*this);
  }
}

namespace {

struct HeaderNameHash {
  size_t operator()(kj::StringPtr s) const {
    size_t result = 5381;
    for (byte b: s.asBytes()) {
      // Masking bit 0x20 makes our hash case-insensitive while conveniently avoiding any
      // collisions that would matter for header names.
      result = ((result << 5) + result) ^ (b & ~0x20);
    }
    return result;
  }

  bool operator()(kj::StringPtr a, kj::StringPtr b) const {
    // TODO(perf): I wonder if we can beat strcasecmp() by masking bit 0x20 from each byte. We'd
    //   need to prohibit one of the technically-legal characters '^' or '~' from header names
    //   since they'd otherwise be ambiguous, but otherwise there is no ambiguity.
#if _MSC_VER
    return _stricmp(a.cStr(), b.cStr()) == 0;
#else
    return strcasecmp(a.cStr(), b.cStr()) == 0;
#endif
  }
};

}  // namespace

struct HttpHeaderTable::IdsByNameMap {
  // TODO(perf): If we were cool we could maybe use a perfect hash here, since our hashtable is
  //   static once built.

  std::unordered_map<kj::StringPtr, uint, HeaderNameHash, HeaderNameHash> map;
};

HttpHeaderTable::Builder::Builder()
    : table(kj::heap<HttpHeaderTable>()) {}

HttpHeaderId HttpHeaderTable::Builder::add(kj::StringPtr name) {
  requireValidHeaderName(name);

  auto insertResult = table->idsByName->map.insert(std::make_pair(name, table->namesById.size()));
  if (insertResult.second) {
    table->namesById.add(name);
  }
  return HttpHeaderId(table, insertResult.first->second);
}

HttpHeaderTable::HttpHeaderTable()
    : idsByName(kj::heap<IdsByNameMap>()) {
#define ADD_HEADER(id, name) \
  idsByName->map.insert(std::make_pair(name, \
      static_cast<uint>(ConnectionHeaderIndices::id) ^ CONNECTION_HEADER_XOR));
  KJ_HTTP_FOR_EACH_CONNECTION_HEADER(ADD_HEADER);
#undef ADD_HEADER

#define ADD_HEADER(id, name) \
  namesById.add(name); \
  idsByName->map.insert(std::make_pair(name, static_cast<uint>(BuiltinHeaderIndices::id)));
  KJ_HTTP_FOR_EACH_BUILTIN_HEADER(ADD_HEADER);
#undef ADD_HEADER
}
HttpHeaderTable::~HttpHeaderTable() noexcept(false) {}

kj::Maybe<HttpHeaderId> HttpHeaderTable::stringToId(kj::StringPtr name) {
  auto iter = idsByName->map.find(name);
  if (iter == idsByName->map.end()) {
    return nullptr;
  } else {
    return HttpHeaderId(this, iter->second);
  }
}

// =======================================================================================

HttpHeaders::HttpHeaders(HttpHeaderTable& table)
    : table(&table),
      indexedHeaders(kj::heapArray<kj::StringPtr>(table.idCount())) {}

void HttpHeaders::clear() {
  for (auto& header: indexedHeaders) {
    header = nullptr;
  }

  unindexedHeaders.clear();
}

HttpHeaders HttpHeaders::clone() const {
  HttpHeaders result(*table);

  for (auto i: kj::indices(indexedHeaders)) {
    if (indexedHeaders[i] != nullptr) {
      result.indexedHeaders[i] = result.cloneToOwn(indexedHeaders[i]);
    }
  }

  result.unindexedHeaders.resize(unindexedHeaders.size());
  for (auto i: kj::indices(unindexedHeaders)) {
    result.unindexedHeaders[i].name = result.cloneToOwn(unindexedHeaders[i].name);
    result.unindexedHeaders[i].value = result.cloneToOwn(unindexedHeaders[i].value);
  }

  return result;
}

HttpHeaders HttpHeaders::cloneShallow() const {
  HttpHeaders result(*table);

  for (auto i: kj::indices(indexedHeaders)) {
    if (indexedHeaders[i] != nullptr) {
      result.indexedHeaders[i] = indexedHeaders[i];
    }
  }

  result.unindexedHeaders.resize(unindexedHeaders.size());
  for (auto i: kj::indices(unindexedHeaders)) {
    result.unindexedHeaders[i] = unindexedHeaders[i];
  }

  return result;
}

kj::StringPtr HttpHeaders::cloneToOwn(kj::StringPtr str) {
  auto copy = kj::heapString(str);
  kj::StringPtr result = copy;
  ownedStrings.add(copy.releaseArray());
  return result;
}

void HttpHeaders::set(HttpHeaderId id, kj::StringPtr value) {
  id.requireFrom(*table);
  requireValidHeaderValue(value);

  indexedHeaders[id.id] = value;
}

void HttpHeaders::set(HttpHeaderId id, kj::String&& value) {
  set(id, kj::StringPtr(value));
  takeOwnership(kj::mv(value));
}

void HttpHeaders::add(kj::StringPtr name, kj::StringPtr value) {
  requireValidHeaderName(name);
  requireValidHeaderValue(value);

  KJ_REQUIRE(addNoCheck(name, value) == nullptr,
      "can't set connection-level headers on HttpHeaders", name, value) { break; }
}

void HttpHeaders::add(kj::StringPtr name, kj::String&& value) {
  add(name, kj::StringPtr(value));
  takeOwnership(kj::mv(value));
}

void HttpHeaders::add(kj::String&& name, kj::String&& value) {
  add(kj::StringPtr(name), kj::StringPtr(value));
  takeOwnership(kj::mv(name));
  takeOwnership(kj::mv(value));
}

kj::Maybe<uint> HttpHeaders::addNoCheck(kj::StringPtr name, kj::StringPtr value) {
  KJ_IF_MAYBE(id, table->stringToId(name)) {
    if (id->id > CONNECTION_HEADER_THRESHOLD) {
      return id->id ^ CONNECTION_HEADER_XOR;
    }

    if (indexedHeaders[id->id] == nullptr) {
      indexedHeaders[id->id] = value;
    } else {
      // Duplicate HTTP headers are equivalent to the values being separated by a comma.
      auto concat = kj::str(indexedHeaders[id->id], ", ", value);
      indexedHeaders[id->id] = concat;
      ownedStrings.add(concat.releaseArray());
    }
  } else {
    unindexedHeaders.add(Header {name, value});
  }

  return nullptr;
}

void HttpHeaders::takeOwnership(kj::String&& string) {
  ownedStrings.add(string.releaseArray());
}
void HttpHeaders::takeOwnership(kj::Array<char>&& chars) {
  ownedStrings.add(kj::mv(chars));
}
void HttpHeaders::takeOwnership(HttpHeaders&& otherHeaders) {
  for (auto& str: otherHeaders.ownedStrings) {
    ownedStrings.add(kj::mv(str));
  }
  otherHeaders.ownedStrings.clear();
}

// -----------------------------------------------------------------------------

static inline char* skipSpace(char* p) {
  for (;;) {
    switch (*p) {
      case '\t':
      case ' ':
        ++p;
        break;
      default:
        return p;
    }
  }
}

static kj::Maybe<kj::StringPtr> consumeWord(char*& ptr) {
  char* start = skipSpace(ptr);
  char* p = start;

  for (;;) {
    switch (*p) {
      case '\0':
        ptr = p;
        return kj::StringPtr(start, p);

      case '\t':
      case ' ': {
        char* end = p++;
        ptr = p;
        *end = '\0';
        return kj::StringPtr(start, end);
      }

      case '\n':
      case '\r':
        // Not expecting EOL!
        return nullptr;

      default:
        ++p;
        break;
    }
  }
}

static kj::Maybe<uint> consumeNumber(char*& ptr) {
  char* start = skipSpace(ptr);
  char* p = start;

  uint result = 0;

  for (;;) {
    char c = *p;
    if ('0' <= c && c <= '9') {
      result = result * 10 + (c - '0');
      ++p;
    } else {
      if (p == start) return nullptr;
      ptr = p;
      return result;
    }
  }
}

static kj::StringPtr consumeLine(char*& ptr) {
  char* start = skipSpace(ptr);
  char* p = start;

  for (;;) {
    switch (*p) {
      case '\0':
        ptr = p;
        return kj::StringPtr(start, p);

      case '\r': {
        char* end = p++;
        if (*p == '\n') ++p;

        if (*p == ' ' || *p == '\t') {
          // Whoa, continuation line. These are deprecated, but historically a line starting with
          // a space was treated as a continuation of the previous line. The behavior should be
          // the same as if the \r\n were replaced with spaces, so let's do that here to prevent
          // confusion later.
          *end = ' ';
          p[-1] = ' ';
          break;
        }

        ptr = p;
        *end = '\0';
        return kj::StringPtr(start, end);
      }

      case '\n': {
        char* end = p++;

        if (*p == ' ' || *p == '\t') {
          // Whoa, continuation line. These are deprecated, but historically a line starting with
          // a space was treated as a continuation of the previous line. The behavior should be
          // the same as if the \n were replaced with spaces, so let's do that here to prevent
          // confusion later.
          *end = ' ';
          break;
        }

        ptr = p;
        *end = '\0';
        return kj::StringPtr(start, end);
      }

      default:
        ++p;
        break;
    }
  }
}

static kj::Maybe<kj::StringPtr> consumeHeaderName(char*& ptr) {
  // Do NOT skip spaces before the header name. Leading spaces indicate a continuation line; they
  // should have been handled in consumeLine().
  char* p = ptr;

  char* start = p;
  while (HTTP_HEADER_NAME_CHARS.contains(*p)) ++p;
  char* end = p;

  p = skipSpace(p);

  if (end == start || *p != ':') return nullptr;
  ++p;

  p = skipSpace(p);

  *end = '\0';
  ptr = p;
  return kj::StringPtr(start, end);
}

static char* trimHeaderEnding(kj::ArrayPtr<char> content) {
  // Trim off the trailing \r\n from a header blob.

  if (content.size() < 2) return nullptr;

  // Remove trailing \r\n\r\n and replace with \0 sentinel char.
  char* end = content.end();

  if (end[-1] != '\n') return nullptr;
  --end;
  if (end[-1] == '\r') --end;
  *end = '\0';

  return end;
}

kj::Maybe<HttpHeaders::Request> HttpHeaders::tryParseRequest(kj::ArrayPtr<char> content) {
  char* end = trimHeaderEnding(content);
  if (end == nullptr) return nullptr;

  char* ptr = content.begin();

  HttpHeaders::Request request;

  KJ_IF_MAYBE(method, consumeHttpMethod(ptr)) {
    request.method = *method;
    if (*ptr != ' ' && *ptr != '\t') {
      return nullptr;
    }
    ++ptr;
  } else {
    return nullptr;
  }

  KJ_IF_MAYBE(path, consumeWord(ptr)) {
    request.url = *path;
  } else {
    return nullptr;
  }

  // Ignore rest of line. Don't care about "HTTP/1.1" or whatever.
  consumeLine(ptr);

  if (!parseHeaders(ptr, end, request.connectionHeaders)) return nullptr;

  return request;
}

kj::Maybe<HttpHeaders::Response> HttpHeaders::tryParseResponse(kj::ArrayPtr<char> content) {
  char* end = trimHeaderEnding(content);
  if (end == nullptr) return nullptr;

  char* ptr = content.begin();

  HttpHeaders::Response response;

  KJ_IF_MAYBE(version, consumeWord(ptr)) {
    if (!version->startsWith("HTTP/")) return nullptr;
  } else {
    return nullptr;
  }

  KJ_IF_MAYBE(code, consumeNumber(ptr)) {
    response.statusCode = *code;
  } else {
    return nullptr;
  }

  response.statusText = consumeLine(ptr);

  if (!parseHeaders(ptr, end, response.connectionHeaders)) return nullptr;

  return response;
}

bool HttpHeaders::parseHeaders(char* ptr, char* end, ConnectionHeaders& connectionHeaders) {
  while (*ptr != '\0') {
    KJ_IF_MAYBE(name, consumeHeaderName(ptr)) {
      kj::StringPtr line = consumeLine(ptr);
      KJ_IF_MAYBE(connectionHeaderId, addNoCheck(*name, line)) {
        // Parsed a connection header.
        switch (*connectionHeaderId) {
#define HANDLE_HEADER(id, name) \
          case static_cast<uint>(ConnectionHeaderIndices::id): \
            connectionHeaders.id = line; \
            break;
          KJ_HTTP_FOR_EACH_CONNECTION_HEADER(HANDLE_HEADER)
#undef HANDLE_HEADER
          default:
            KJ_UNREACHABLE;
        }
      }
    } else {
      return false;
    }
  }

  return ptr == end;
}

// -----------------------------------------------------------------------------

kj::String HttpHeaders::serializeRequest(HttpMethod method, kj::StringPtr url,
                                         const ConnectionHeaders& connectionHeaders) const {
  return serialize(kj::toCharSequence(method), url, kj::StringPtr("HTTP/1.1"), connectionHeaders);
}

kj::String HttpHeaders::serializeResponse(uint statusCode, kj::StringPtr statusText,
                                          const ConnectionHeaders& connectionHeaders) const {
  auto statusCodeStr = kj::toCharSequence(statusCode);

  return serialize(kj::StringPtr("HTTP/1.1"), statusCodeStr, statusText, connectionHeaders);
}

kj::String HttpHeaders::serialize(kj::ArrayPtr<const char> word1,
                                  kj::ArrayPtr<const char> word2,
                                  kj::ArrayPtr<const char> word3,
                                  const ConnectionHeaders& connectionHeaders) const {
  const kj::StringPtr space = " ";
  const kj::StringPtr newline = "\r\n";
  const kj::StringPtr colon = ": ";

  size_t size = 2;  // final \r\n
  if (word1 != nullptr) {
    size += word1.size() + word2.size() + word3.size() + 4;
  }
#define HANDLE_HEADER(id, name) \
  if (connectionHeaders.id != nullptr) { \
    size += connectionHeaders.id.size() + (sizeof(name) + 3); \
  }
  KJ_HTTP_FOR_EACH_CONNECTION_HEADER(HANDLE_HEADER)
#undef HANDLE_HEADER
  for (auto i: kj::indices(indexedHeaders)) {
    if (indexedHeaders[i] != nullptr) {
      size += table->idToString(HttpHeaderId(table, i)).size() + indexedHeaders[i].size() + 4;
    }
  }
  for (auto& header: unindexedHeaders) {
    size += header.name.size() + header.value.size() + 4;
  }

  String result = heapString(size);
  char* ptr = result.begin();

  if (word1 != nullptr) {
    ptr = kj::_::fill(ptr, word1, space, word2, space, word3, newline);
  }
#define HANDLE_HEADER(id, name) \
  if (connectionHeaders.id != nullptr) { \
    ptr = kj::_::fill(ptr, kj::StringPtr(name), colon, connectionHeaders.id, newline); \
  }
  KJ_HTTP_FOR_EACH_CONNECTION_HEADER(HANDLE_HEADER)
#undef HANDLE_HEADER
  for (auto i: kj::indices(indexedHeaders)) {
    if (indexedHeaders[i] != nullptr) {
      ptr = kj::_::fill(ptr, table->idToString(HttpHeaderId(table, i)), colon,
                        indexedHeaders[i], newline);
    }
  }
  for (auto& header: unindexedHeaders) {
    ptr = kj::_::fill(ptr, header.name, colon, header.value, newline);
  }
  ptr = kj::_::fill(ptr, newline);

  KJ_ASSERT(ptr == result.end());
  return result;
}

kj::String HttpHeaders::toString() const {
  return serialize(nullptr, nullptr, nullptr, ConnectionHeaders());
}

// =======================================================================================

namespace {

static constexpr size_t MIN_BUFFER = 4096;
static constexpr size_t MAX_BUFFER = 65536;
static constexpr size_t MAX_CHUNK_HEADER_SIZE = 32;

class HttpInputStream {
public:
  explicit HttpInputStream(AsyncIoStream& inner, HttpHeaderTable& table)
      : inner(inner), headerBuffer(kj::heapArray<char>(MIN_BUFFER)), headers(table) {
  }

  // ---------------------------------------------------------------------------
  // Stream locking: While an entity-body is being read, the body stream "locks" the underlying
  // HTTP stream. Once the entity-body is complete, we can read the next pipelined message.

  void finishRead() {
    // Called when entire request has been read.

    KJ_REQUIRE_NONNULL(onMessageDone)->fulfill();
    onMessageDone = nullptr;
  }

  void abortRead() {
    // Called when a body input stream was destroyed without reading to the end.

    KJ_REQUIRE_NONNULL(onMessageDone)->reject(KJ_EXCEPTION(FAILED,
        "client did not finish reading previous HTTP response body",
        "can't read next pipelined response"));
    onMessageDone = nullptr;
  }

  // ---------------------------------------------------------------------------

  kj::Promise<bool> awaitNextMessage() {
    // Waits until more data is available, but doesn't consume it. Only meant for server-side use,
    // after a request is handled, to check for pipelined requests. Returns false on EOF.

    // Slightly-crappy code to snarf the expected line break. This will actually eat the leading
    // regex /\r*\n?/.
    while (lineBreakBeforeNextHeader && leftover.size() > 0) {
      if (leftover[0] == '\r') {
        leftover = leftover.slice(1, leftover.size());
      } else if (leftover[0] == '\n') {
        leftover = leftover.slice(1, leftover.size());
        lineBreakBeforeNextHeader = false;
      } else {
        // Err, missing line break, whatever.
        lineBreakBeforeNextHeader = false;
      }
    }

    if (!lineBreakBeforeNextHeader && leftover != nullptr) {
      return true;
    }

    return inner.tryRead(headerBuffer.begin(), 1, headerBuffer.size())
        .then([this](size_t amount) -> kj::Promise<bool> {
      if (amount > 0) {
        leftover = headerBuffer.slice(0, amount);
        return awaitNextMessage();
      } else {
        return false;
      }
    });
  }

  kj::Promise<kj::ArrayPtr<char>> readMessageHeaders() {
    auto paf = kj::newPromiseAndFulfiller<void>();

    auto promise = messageReadQueue
        .then(kj::mvCapture(paf.fulfiller, [this](kj::Own<kj::PromiseFulfiller<void>> fulfiller) {
      onMessageDone = kj::mv(fulfiller);
      return readHeader(HeaderType::MESSAGE, 0, 0);
    }));

    messageReadQueue = kj::mv(paf.promise);

    return promise;
  }

  kj::Promise<uint64_t> readChunkHeader() {
    KJ_REQUIRE(onMessageDone != nullptr);

    // We use the portion of the header after the end of message headers.
    return readHeader(HeaderType::CHUNK, messageHeaderEnd, messageHeaderEnd)
        .then([](kj::ArrayPtr<char> text) -> uint64_t {
      KJ_REQUIRE(text.size() > 0) { break; }

      uint64_t value = 0;
      for (char c: text) {
        if ('0' <= c && c <= '9') {
          value = value * 16 + (c - '0');
        } else if ('a' <= c && c <= 'f') {
          value = value * 16 + (c - 'a' + 10);
        } else if ('A' <= c && c <= 'F') {
          value = value * 16 + (c - 'A' + 10);
        } else {
          KJ_FAIL_REQUIRE("invalid HTTP chunk size", text, text.asBytes()) {
            return value;
          }
        }
      }

      return value;
    });
  }

  inline kj::Promise<kj::Maybe<HttpHeaders::Request>> readRequestHeaders() {
    headers.clear();
    return readMessageHeaders().then([this](kj::ArrayPtr<char> text) {
      return headers.tryParseRequest(text);
    });
  }

  inline kj::Promise<kj::Maybe<HttpHeaders::Response>> readResponseHeaders() {
    headers.clear();
    return readMessageHeaders().then([this](kj::ArrayPtr<char> text) {
      return headers.tryParseResponse(text);
    });
  }

  inline const HttpHeaders& getHeaders() const { return headers; }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) {
    // Read message body data.

    KJ_REQUIRE(onMessageDone != nullptr);

    if (leftover == nullptr) {
      // No leftovers. Forward directly to inner stream.
      return inner.tryRead(buffer, minBytes, maxBytes);
    } else if (leftover.size() >= maxBytes) {
      // Didn't even read the entire leftover buffer.
      memcpy(buffer, leftover.begin(), maxBytes);
      leftover = leftover.slice(maxBytes, leftover.size());
      return maxBytes;
    } else {
      // Read the entire leftover buffer, plus some.
      memcpy(buffer, leftover.begin(), leftover.size());
      size_t copied = leftover.size();
      leftover = nullptr;
      if (copied >= minBytes) {
        // Got enough to stop here.
        return copied;
      } else {
        // Read the rest from the underlying stream.
        return inner.tryRead(reinterpret_cast<byte*>(buffer) + copied,
                             minBytes - copied, maxBytes - copied)
            .then([copied](size_t n) { return n + copied; });
      }
    }
  }

  enum RequestOrResponse {
    REQUEST,
    RESPONSE
  };

  kj::Own<kj::AsyncInputStream> getEntityBody(
      RequestOrResponse type, HttpMethod method, uint statusCode,
      HttpHeaders::ConnectionHeaders& connectionHeaders);

private:
  AsyncIoStream& inner;
  kj::Array<char> headerBuffer;

  size_t messageHeaderEnd = 0;
  // Position in headerBuffer where the message headers end -- further buffer space can
  // be used for chunk headers.

  kj::ArrayPtr<char> leftover;
  // Data in headerBuffer that comes immediately after the header content, if any.

  HttpHeaders headers;
  // Parsed headers, after a call to parseAwaited*().

  bool lineBreakBeforeNextHeader = false;
  // If true, the next await should expect to start with a spurrious '\n' or '\r\n'. This happens
  // as a side-effect of HTTP chunked encoding, where such a newline is added to the end of each
  // chunk, for no good reason.

  kj::Promise<void> messageReadQueue = kj::READY_NOW;

  kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> onMessageDone;
  // Fulfill once the current message has been completely read. Unblocks reading of the next
  // message headers.

  enum class HeaderType {
    MESSAGE,
    CHUNK
  };

  kj::Promise<kj::ArrayPtr<char>> readHeader(
      HeaderType type, size_t bufferStart, size_t bufferEnd) {
    // Reads the HTTP message header or a chunk header (as in transfer-encoding chunked) and
    // returns the buffer slice containing it.
    //
    // The main source of complication here is that we want to end up with one continuous buffer
    // containing the result, and that the input is delimited by newlines rather than by an upfront
    // length.

    kj::Promise<size_t> readPromise = nullptr;

    // Figure out where we're reading from.
    if (leftover != nullptr) {
      // Some data is still left over from the previous message, so start with that.

      // This can only happen if this is the initial call to readHeader() (not recursive).
      KJ_ASSERT(bufferStart == bufferEnd);

      // OK, set bufferStart and bufferEnd to both point to the start of the leftover, and then
      // fake a read promise as if we read the bytes from the leftover.
      bufferStart = leftover.begin() - headerBuffer.begin();
      bufferEnd = bufferStart;
      readPromise = leftover.size();
      leftover = nullptr;
    } else {
      // Need to read more data from the unfderlying stream.

      if (bufferEnd == headerBuffer.size()) {
        // Out of buffer space.

        // Maybe we can move bufferStart backwards to make more space at the end?
        size_t minStart = type == HeaderType::MESSAGE ? 0 : messageHeaderEnd;

        if (bufferStart > minStart) {
          // Move to make space.
          memmove(headerBuffer.begin() + minStart, headerBuffer.begin() + bufferStart,
                  bufferEnd - bufferStart);
          bufferEnd = bufferEnd - bufferStart + minStart;
          bufferStart = minStart;
        } else {
          // Really out of buffer space. Grow the buffer.
          if (type != HeaderType::MESSAGE) {
            // Can't grow because we'd invalidate the HTTP headers.
            return KJ_EXCEPTION(FAILED, "invalid HTTP chunk size");
          }
          KJ_REQUIRE(headerBuffer.size() < MAX_BUFFER, "request headers too large");
          auto newBuffer = kj::heapArray<char>(headerBuffer.size() * 2);
          memcpy(newBuffer.begin(), headerBuffer.begin(), headerBuffer.size());
          headerBuffer = kj::mv(newBuffer);
        }
      }

      // How many bytes will we read?
      size_t maxBytes = headerBuffer.size() - bufferEnd;

      if (type == HeaderType::CHUNK) {
        // Roughly limit the amount of data we read to MAX_CHUNK_HEADER_SIZE.
        // TODO(perf): This is mainly to avoid copying a lot of body data into our buffer just to
        //   copy it again when it is read. But maybe the copy would be cheaper than overhead of
        //   extra event loop turns?
        KJ_REQUIRE(bufferEnd - bufferStart <= MAX_CHUNK_HEADER_SIZE, "invalid HTTP chunk size");
        maxBytes = kj::min(maxBytes, MAX_CHUNK_HEADER_SIZE);
      }

      readPromise = inner.read(headerBuffer.begin() + bufferEnd, 1, maxBytes);
    }

    return readPromise.then([this,type,bufferStart,bufferEnd](size_t amount) mutable
                            -> kj::Promise<kj::ArrayPtr<char>> {
      if (lineBreakBeforeNextHeader) {
        // Hackily deal with expected leading line break.
        if (bufferEnd == bufferStart && headerBuffer[bufferEnd] == '\r') {
          ++bufferEnd;
          --amount;
        }

        if (amount > 0 && headerBuffer[bufferEnd] == '\n') {
          lineBreakBeforeNextHeader = false;
          ++bufferEnd;
          --amount;

          // Cut the leading line break out of the buffer entirely.
          bufferStart = bufferEnd;
        }

        if (amount == 0) {
          return readHeader(type, bufferStart, bufferEnd);
        }
      }

      size_t pos = bufferEnd;
      size_t newEnd = pos + amount;

      for (;;) {
        // Search for next newline.
        char* nl = reinterpret_cast<char*>(
            memchr(headerBuffer.begin() + pos, '\n', newEnd - pos));
        if (nl == nullptr) {
          // No newline found. Wait for more data.
          return readHeader(type, bufferStart, newEnd);
        }

        // Is this newline which we found the last of the header? For a chunk header, always. For
        // a message header, we search for two newlines in a row. We accept either "\r\n" or just
        // "\n" as a newline sequence (though the standard requires "\r\n").
        if (type == HeaderType::CHUNK ||
            (nl - headerBuffer.begin() >= 4 &&
             ((nl[-1] == '\r' && nl[-2] == '\n') || (nl[-1] == '\n')))) {
          // OK, we've got all the data!

          size_t endIndex = nl + 1 - headerBuffer.begin();
          size_t leftoverStart = endIndex;

          // Strip off the last newline from end.
          endIndex -= 1 + (nl[-1] == '\r');

          if (type == HeaderType::MESSAGE) {
            if (headerBuffer.size() - newEnd < MAX_CHUNK_HEADER_SIZE) {
              // Ugh, there's not enough space for the secondary await buffer. Grow once more.
              auto newBuffer = kj::heapArray<char>(headerBuffer.size() * 2);
              memcpy(newBuffer.begin(), headerBuffer.begin(), headerBuffer.size());
              headerBuffer = kj::mv(newBuffer);
            }
            messageHeaderEnd = endIndex;
          } else {
            // For some reason, HTTP specifies that there will be a line break after each chunk.
            lineBreakBeforeNextHeader = true;
          }

          auto result = headerBuffer.slice(bufferStart, endIndex);
          leftover = headerBuffer.slice(leftoverStart, newEnd);
          return result;
        } else {
          pos = nl - headerBuffer.begin() + 1;
        }
      }
    });
  }
};

// -----------------------------------------------------------------------------

class HttpEntityBodyReader: public kj::AsyncInputStream {
public:
  HttpEntityBodyReader(HttpInputStream& inner): inner(inner) {}
  ~HttpEntityBodyReader() noexcept(false) {
    if (!finished) {
      inner.abortRead();
    }
  }

protected:
  HttpInputStream& inner;

  void doneReading() {
    KJ_REQUIRE(!finished);
    finished = true;
    inner.finishRead();
  }

  inline bool alreadyDone() { return finished; }

private:
  bool finished = false;
};

class HttpNullEntityReader final: public HttpEntityBodyReader {
  // Stream which reads until EOF.

public:
  HttpNullEntityReader(HttpInputStream& inner, kj::Maybe<uint64_t> length)
      : HttpEntityBodyReader(inner), length(length) {
    // `length` is what to return from tryGetLength(). For a response to a HEAD request, this may
    // be non-zero.
    doneReading();
  }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return size_t(0);
  }

  Maybe<uint64_t> tryGetLength() override {
    return length;
  }

private:
  kj::Maybe<uint64_t> length;
};

class HttpConnectionCloseEntityReader final: public HttpEntityBodyReader {
  // Stream which reads until EOF.

public:
  HttpConnectionCloseEntityReader(HttpInputStream& inner)
      : HttpEntityBodyReader(inner) {}

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    if (alreadyDone()) return size_t(0);

    return inner.tryRead(buffer, minBytes, maxBytes)
        .then([=](size_t amount) {
      if (amount < minBytes) {
        doneReading();
      }
      return amount;
    });
  }
};

class HttpFixedLengthEntityReader final: public HttpEntityBodyReader {
  // Stream which reads only up to a fixed length from the underlying stream, then emulates EOF.

public:
  HttpFixedLengthEntityReader(HttpInputStream& inner, size_t length)
      : HttpEntityBodyReader(inner), length(length) {
    if (length == 0) doneReading();
  }

  Maybe<uint64_t> tryGetLength() override {
    return length;
  }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    if (length == 0) return size_t(0);

    return inner.tryRead(buffer, kj::min(minBytes, length), kj::min(maxBytes, length))
        .then([=](size_t amount) {
      length -= amount;
      if (length > 0 && amount < minBytes) {
        kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED,
            "premature EOF in HTTP entity body; did not reach Content-Length"));
      } else if (length == 0) {
        doneReading();
      }
      return amount;
    });
  }

private:
  size_t length;
};

class HttpChunkedEntityReader final: public HttpEntityBodyReader {
  // Stream which reads a Transfer-Encoding: Chunked stream.

public:
  HttpChunkedEntityReader(HttpInputStream& inner)
      : HttpEntityBodyReader(inner) {}

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return tryReadInternal(buffer, minBytes, maxBytes, 0);
  }

private:
  size_t chunkSize = 0;

  Promise<size_t> tryReadInternal(void* buffer, size_t minBytes, size_t maxBytes,
                                  size_t alreadyRead) {
    if (alreadyDone()) {
      return alreadyRead;
    } else if (chunkSize == 0) {
      // Read next chunk header.
      return inner.readChunkHeader().then([=](uint64_t nextChunkSize) {
        if (nextChunkSize == 0) {
          doneReading();
        }

        chunkSize = nextChunkSize;
        return tryReadInternal(buffer, minBytes, maxBytes, alreadyRead);
      });
    } else if (chunkSize < minBytes) {
      // Read entire current chunk and continue to next chunk.
      return inner.tryRead(buffer, chunkSize, chunkSize)
          .then([=](size_t amount) -> kj::Promise<size_t> {
        chunkSize -= amount;
        if (chunkSize > 0) {
          return KJ_EXCEPTION(DISCONNECTED, "premature EOF in HTTP chunk");
        }

        return tryReadInternal(reinterpret_cast<byte*>(buffer) + amount,
                               minBytes - amount, maxBytes - amount, alreadyRead + amount);
      });
    } else {
      // Read only part of the current chunk.
      return inner.tryRead(buffer, minBytes, kj::min(maxBytes, chunkSize))
          .then([=](size_t amount) -> size_t {
        chunkSize -= amount;
        return alreadyRead + amount;
      });
    }
  }
};

template <char...>
struct FastCaseCmp;

template <char first, char... rest>
struct FastCaseCmp<first, rest...> {
  static constexpr bool apply(const char* actual) {
    return
      'a' <= first && first <= 'z'
        ? (*actual | 0x20) == first && FastCaseCmp<rest...>::apply(actual + 1)
      : 'A' <= first && first <= 'Z'
        ? (*actual & ~0x20) == first && FastCaseCmp<rest...>::apply(actual + 1)
        : *actual == first && FastCaseCmp<rest...>::apply(actual + 1);
  }
};

template <>
struct FastCaseCmp<> {
  static constexpr bool apply(const char* actual) {
    return *actual == '\0';
  }
};

template <char... chars>
constexpr bool fastCaseCmp(const char* actual) {
  return FastCaseCmp<chars...>::apply(actual);
}

// Tests
static_assert(fastCaseCmp<'f','O','o','B','1'>("FooB1"), "");
static_assert(!fastCaseCmp<'f','O','o','B','2'>("FooB1"), "");
static_assert(!fastCaseCmp<'n','O','o','B','1'>("FooB1"), "");
static_assert(!fastCaseCmp<'f','O','o','B'>("FooB1"), "");
static_assert(!fastCaseCmp<'f','O','o','B','1','a'>("FooB1"), "");

kj::Own<kj::AsyncInputStream> HttpInputStream::getEntityBody(
    RequestOrResponse type, HttpMethod method, uint statusCode,
    HttpHeaders::ConnectionHeaders& connectionHeaders) {
  if (type == RESPONSE) {
    if (method == HttpMethod::HEAD) {
      // Body elided.
      kj::Maybe<uint64_t> length;
      if (connectionHeaders.contentLength != nullptr) {
        length = strtoull(connectionHeaders.contentLength.cStr(), nullptr, 10);
      }
      return kj::heap<HttpNullEntityReader>(*this, length);
    } else if (statusCode == 204 || statusCode == 205 || statusCode == 304) {
      // No body.
      return kj::heap<HttpNullEntityReader>(*this, uint64_t(0));
    }
  }

  if (connectionHeaders.transferEncoding != nullptr) {
    // TODO(someday): Support plugable transfer encodings? Or at least gzip?
    // TODO(soon): Support stacked transfer encodings, e.g. "gzip, chunked".
    if (fastCaseCmp<'c','h','u','n','k','e','d'>(connectionHeaders.transferEncoding.cStr())) {
      return kj::heap<HttpChunkedEntityReader>(*this);
    } else {
      KJ_FAIL_REQUIRE("unknown transfer encoding") { break; }
    }
  }

  if (connectionHeaders.contentLength != nullptr) {
    return kj::heap<HttpFixedLengthEntityReader>(*this,
        strtoull(connectionHeaders.contentLength.cStr(), nullptr, 10));
  }

  if (type == REQUEST) {
    // Lack of a Content-Length or Transfer-Encoding means no body for requests.
    return kj::heap<HttpNullEntityReader>(*this, uint64_t(0));
  }

  if (connectionHeaders.connection != nullptr) {
    // TODO(soon): Connection header can actually have multiple tokens... but no one ever uses
    //   that feature?
    if (fastCaseCmp<'c','l','o','s','e'>(connectionHeaders.connection.cStr())) {
      return kj::heap<HttpConnectionCloseEntityReader>(*this);
    }
  }

  KJ_FAIL_REQUIRE("don't know how HTTP body is delimited", headers);
  return kj::heap<HttpNullEntityReader>(*this, uint64_t(0));
}

// =======================================================================================

class HttpOutputStream {
public:
  HttpOutputStream(AsyncOutputStream& inner): inner(inner) {}

  void writeHeaders(String content) {
    // Writes some header content and begins a new entity body.

    KJ_REQUIRE(!inBody, "previous HTTP message body incomplete; can't write more messages");
    inBody = true;

    queueWrite(kj::mv(content));
  }

  void writeBodyData(kj::String content) {
    KJ_REQUIRE(inBody) { return; }

    queueWrite(kj::mv(content));
  }

  kj::Promise<void> writeBodyData(const void* buffer, size_t size) {
    KJ_REQUIRE(inBody) { return kj::READY_NOW; }

    auto fork = writeQueue.then([this,buffer,size]() {
      return inner.write(buffer, size);
    }).fork();

    writeQueue = fork.addBranch();
    return fork.addBranch();
  }

  kj::Promise<void> writeBodyData(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) {
    KJ_REQUIRE(inBody) { return kj::READY_NOW; }

    auto fork = writeQueue.then([this,pieces]() {
      return inner.write(pieces);
    }).fork();

    writeQueue = fork.addBranch();
    return fork.addBranch();
  }

  Promise<uint64_t> pumpBodyFrom(AsyncInputStream& input, uint64_t amount) {
    KJ_REQUIRE(inBody) { return uint64_t(0); }

    auto fork = writeQueue.then([this,&input,amount]() {
      return input.pumpTo(inner, amount);
    }).fork();

    writeQueue = fork.addBranch().ignoreResult();
    return fork.addBranch();
  }

  void finishBody() {
    // Called when entire body was written.

    KJ_REQUIRE(inBody) { return; }
    inBody = false;
  }

  void abortBody() {
    // Called if the application failed to write all expected body bytes.
    KJ_REQUIRE(inBody) { return; }
    inBody = false;

    writeQueue = writeQueue.then([]() -> kj::Promise<void> {
      return KJ_EXCEPTION(FAILED,
          "previous HTTP message body incomplete; can't write more messages");
    });
  }

  kj::Promise<void> flush() {
    auto fork = writeQueue.fork();
    writeQueue = fork.addBranch();
    return fork.addBranch();
  }

private:
  AsyncOutputStream& inner;
  kj::Promise<void> writeQueue = kj::READY_NOW;
  bool inBody = false;

  void queueWrite(kj::String content) {
    writeQueue = writeQueue.then(kj::mvCapture(content, [this](kj::String&& content) {
      auto promise = inner.write(content.begin(), content.size());
      return promise.attach(kj::mv(content));
    }));
  }
};

class HttpNullEntityWriter final: public kj::AsyncOutputStream {
public:
  Promise<void> write(const void* buffer, size_t size) override {
    return KJ_EXCEPTION(FAILED, "HTTP message has no entity-body; can't write()");
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return KJ_EXCEPTION(FAILED, "HTTP message has no entity-body; can't write()");
  }
};

class HttpDiscardingEntityWriter final: public kj::AsyncOutputStream {
public:
  Promise<void> write(const void* buffer, size_t size) override {
    return kj::READY_NOW;
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return kj::READY_NOW;
  }
};

class HttpFixedLengthEntityWriter final: public kj::AsyncOutputStream {
public:
  HttpFixedLengthEntityWriter(HttpOutputStream& inner, uint64_t length)
      : inner(inner), length(length) {
    if (length == 0) inner.finishBody();
  }
  ~HttpFixedLengthEntityWriter() noexcept(false) {
    if (length > 0) inner.abortBody();
  }

  Promise<void> write(const void* buffer, size_t size) override {
    if (size == 0) return kj::READY_NOW;
    KJ_REQUIRE(size <= length, "overwrote Content-Length");
    length -= size;

    return maybeFinishAfter(inner.writeBodyData(buffer, size));
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    uint64_t size = 0;
    for (auto& piece: pieces) size += piece.size();

    if (size == 0) return kj::READY_NOW;
    KJ_REQUIRE(size <= length, "overwrote Content-Length");
    length -= size;

    return maybeFinishAfter(inner.writeBodyData(pieces));
  }

  Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
    if (amount == 0) return Promise<uint64_t>(uint64_t(0));

    bool overshot = amount > length;
    if (overshot) {
      // Hmm, the requested amount was too large, but it's common to specify kj::max as the amount
      // to pump, in which case we pump to EOF. Let's try to verify whether EOF is where we
      // expect it to be.
      KJ_IF_MAYBE(available, input.tryGetLength()) {
        // Great, the stream knows how large it is. If it's indeed larger than the space available
        // then let's abort.
        KJ_REQUIRE(*available <= length, "overwrote Content-Length");
      } else {
        // OK, we have no idea how large the input is, so we'll have to check later.
      }
    }

    amount = kj::min(amount, length);
    length -= amount;

    auto promise = amount == 0
        ? kj::Promise<uint64_t>(amount)
        : inner.pumpBodyFrom(input, amount).then([this,amount](uint64_t actual) {
      // Adjust for bytes not written.
      length += amount - actual;
      if (length == 0) inner.finishBody();
      return actual;
    });

    if (overshot) {
      promise = promise.then([this,amount,&input](uint64_t actual) -> kj::Promise<uint64_t> {
        if (actual == amount) {
          // We read exactly the amount expected. In order to detect an overshoot, we have to
          // try reading one more byte. Ugh.
          static byte junk;
          return input.tryRead(&junk, 1, 1).then([actual](size_t extra) {
            KJ_REQUIRE(extra == 0, "overwrote Content-Length");
            return actual;
          });
        } else {
          // We actually read less data than requested so we couldn't have overshot. In fact, we
          // undershot.
          return actual;
        }
      });
    }

    return kj::mv(promise);
  }

private:
  HttpOutputStream& inner;
  uint64_t length;

  kj::Promise<void> maybeFinishAfter(kj::Promise<void> promise) {
    if (length == 0) {
      return promise.then([this]() { inner.finishBody(); });
    } else {
      return kj::mv(promise);
    }
  }
};

class HttpChunkedEntityWriter final: public kj::AsyncOutputStream {
public:
  HttpChunkedEntityWriter(HttpOutputStream& inner)
      : inner(inner) {}
  ~HttpChunkedEntityWriter() noexcept(false) {
    inner.writeBodyData(kj::str("0\r\n\r\n"));
    inner.finishBody();
  }

  Promise<void> write(const void* buffer, size_t size) override {
    if (size == 0) return kj::READY_NOW;  // can't encode zero-size chunk since it indicates EOF.

    auto header = kj::str(kj::hex(size), "\r\n");
    auto parts = kj::heapArray<ArrayPtr<const byte>>(3);
    parts[0] = header.asBytes();
    parts[1] = kj::arrayPtr(reinterpret_cast<const byte*>(buffer), size);
    parts[2] = kj::StringPtr("\r\n").asBytes();

    auto promise = inner.writeBodyData(parts.asPtr());
    return promise.attach(kj::mv(header), kj::mv(parts));
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    uint64_t size = 0;
    for (auto& piece: pieces) size += piece.size();

    if (size == 0) return kj::READY_NOW;  // can't encode zero-size chunk since it indicates EOF.

    auto header = kj::str(size, "\r\n");
    auto partsBuilder = kj::heapArrayBuilder<ArrayPtr<const byte>>(pieces.size());
    partsBuilder.add(header.asBytes());
    for (auto& piece: pieces) {
      partsBuilder.add(piece);
    }
    partsBuilder.add(kj::StringPtr("\r\n").asBytes());

    auto parts = partsBuilder.finish();
    auto promise = inner.writeBodyData(parts.asPtr());
    return promise.attach(kj::mv(header), kj::mv(parts));
  }

  Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
    KJ_IF_MAYBE(l, input.tryGetLength()) {
      // Hey, we know exactly how large the input is, so we can write just one chunk.

      uint64_t length = kj::min(amount, *l);
      inner.writeBodyData(kj::str(length, "\r\n"));
      return inner.pumpBodyFrom(input, length)
          .then([this,length](uint64_t actual) {
        if (actual < length) {
          inner.abortBody();
          KJ_FAIL_REQUIRE(
              "value returned by input.tryGetLength() was greater than actual bytes transferred") {
            break;
          }
        }

        inner.writeBodyData(kj::str("\r\n"));
        return actual;
      });
    } else {
      // Need to use naive read/write loop.
      return nullptr;
    }
  }

private:
  HttpOutputStream& inner;
};

// =======================================================================================

class HttpClientImpl final: public HttpClient {
public:
  HttpClientImpl(HttpHeaderTable& responseHeaderTable, kj::AsyncIoStream& rawStream)
      : httpInput(rawStream, responseHeaderTable),
        httpOutput(rawStream) {}

  Request request(HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
                  kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
    HttpHeaders::ConnectionHeaders connectionHeaders;
    kj::String lengthStr;

    if (method == HttpMethod::GET || method == HttpMethod::HEAD) {
      // No entity-body.
    } else KJ_IF_MAYBE(s, expectedBodySize) {
      lengthStr = kj::str(*s);
      connectionHeaders.contentLength = lengthStr;
    } else {
      connectionHeaders.transferEncoding = "chunked";
    }

    httpOutput.writeHeaders(headers.serializeRequest(method, url, connectionHeaders));

    kj::Own<kj::AsyncOutputStream> bodyStream;
    if (method == HttpMethod::GET || method == HttpMethod::HEAD) {
      // No entity-body.
      httpOutput.finishBody();
      bodyStream = heap<HttpNullEntityWriter>();
    } else KJ_IF_MAYBE(s, expectedBodySize) {
      bodyStream = heap<HttpFixedLengthEntityWriter>(httpOutput, *s);
    } else {
      bodyStream = heap<HttpChunkedEntityWriter>(httpOutput);
    }

    auto responsePromise = httpInput.readResponseHeaders()
        .then([this,method](kj::Maybe<HttpHeaders::Response>&& response) -> HttpClient::Response {
      KJ_IF_MAYBE(r, response) {
        return {
          r->statusCode,
          r->statusText,
          &httpInput.getHeaders(),
          httpInput.getEntityBody(HttpInputStream::RESPONSE, method, r->statusCode,
                                  r->connectionHeaders)
        };
      } else {
        KJ_FAIL_REQUIRE("received invalid HTTP response") { break; }
        return HttpClient::Response();
      }
    });

    return { kj::mv(bodyStream), kj::mv(responsePromise) };
  }

private:
  HttpInputStream httpInput;
  HttpOutputStream httpOutput;
};

}  // namespace

kj::Promise<HttpClient::WebSocketResponse> HttpClient::openWebSocket(
    kj::StringPtr url, const HttpHeaders& headers, kj::Own<WebSocket> downstream) {
  return request(HttpMethod::GET, url, headers, nullptr)
      .response.then([](HttpClient::Response&& response) -> WebSocketResponse {
    kj::OneOf<kj::Own<kj::AsyncInputStream>, kj::Own<WebSocket>> body;
    body.init<kj::Own<kj::AsyncInputStream>>(kj::mv(response.body));

    return {
      response.statusCode,
      response.statusText,
      response.headers,
      kj::mv(body)
    };
  });
}

kj::Promise<kj::Own<kj::AsyncIoStream>> HttpClient::connect(kj::StringPtr host) {
  KJ_UNIMPLEMENTED("CONNECT is not implemented by this HttpClient");
}

kj::Own<HttpClient> newHttpClient(
    HttpHeaderTable& responseHeaderTable, kj::AsyncIoStream& stream) {
  return kj::heap<HttpClientImpl>(responseHeaderTable, stream);
}

// =======================================================================================

namespace {

class HttpServiceAdapter final: public HttpService {
public:
  HttpServiceAdapter(HttpClient& client): client(client) {}

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    auto innerReq = client.request(method, url, headers, requestBody.tryGetLength());

    auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);
    promises.add(requestBody.pumpTo(*innerReq.body).ignoreResult()
        .attach(kj::mv(innerReq.body)).eagerlyEvaluate(nullptr));

    promises.add(innerReq.response
        .then([&response](HttpClient::Response&& innerResponse) {
      auto out = response.send(
          innerResponse.statusCode, innerResponse.statusText, *innerResponse.headers,
          innerResponse.body->tryGetLength());
      auto promise = innerResponse.body->pumpTo(*out);
      return promise.ignoreResult().attach(kj::mv(out), kj::mv(innerResponse.body));
    }));

    return kj::joinPromises(promises.finish());
  }

  kj::Promise<kj::Own<kj::AsyncIoStream>> connect(kj::StringPtr host) override {
    return client.connect(kj::mv(host));
  }

  // TODO(soon): other methods

private:
  HttpClient& client;
};

}  // namespace

kj::Own<HttpService> newHttpService(HttpClient& client) {
  return kj::heap<HttpServiceAdapter>(client);
}

// =======================================================================================

kj::Promise<void> HttpService::openWebSocket(
    kj::StringPtr url, const HttpHeaders& headers, WebSocketResponse& response) {
  class EmptyStream final: public kj::AsyncInputStream {
  public:
    Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
      return size_t(0);
    }
  };

  auto requestBody = heap<EmptyStream>();
  auto promise = request(HttpMethod::GET, url, headers, *requestBody, response);
  return promise.attach(kj::mv(requestBody));
}

kj::Promise<kj::Own<kj::AsyncIoStream>> HttpService::connect(kj::StringPtr host) {
  KJ_UNIMPLEMENTED("CONNECT is not implemented by this HttpService");
}

class HttpServer::Connection final: private HttpService::Response {
public:
  Connection(HttpServer& server, kj::AsyncIoStream& stream)
      : server(server),
        httpInput(stream, server.requestHeaderTable),
        httpOutput(stream) {
    ++server.connectionCount;
  }
  Connection(HttpServer& server, kj::Own<kj::AsyncIoStream>&& stream)
      : server(server),
        httpInput(*stream, server.requestHeaderTable),
        httpOutput(*stream),
        ownStream(kj::mv(stream)) {
    ++server.connectionCount;
  }
  ~Connection() noexcept(false) {
    if (--server.connectionCount == 0) {
      KJ_IF_MAYBE(f, server.zeroConnectionsFulfiller) {
        f->get()->fulfill();
      }
    }
  }

  kj::Promise<void> loop(bool firstRequest) {
    auto firstByte = httpInput.awaitNextMessage();

    if (!firstRequest) {
      // For requests after the first, require that the first byte arrive before the pipeline
      // timeout, otherwise treat it like the connection was simply closed.
      firstByte = firstByte.exclusiveJoin(
          server.timer.afterDelay(server.settings.pipelineTimeout)
          .then([this]() -> bool {
        timedOut = true;
        return false;
      }));
    }

    auto receivedHeaders = firstByte
        .then([this,firstRequest](bool hasData)-> kj::Promise<kj::Maybe<HttpHeaders::Request>> {
      if (hasData) {
        auto readHeaders = httpInput.readRequestHeaders();
        if (!firstRequest) {
          // On requests other than the first, the header timeout starts ticking when we receive
          // the first byte of a pipeline response.
          readHeaders = readHeaders.exclusiveJoin(
              server.timer.afterDelay(server.settings.headerTimeout)
              .then([this]() -> kj::Maybe<HttpHeaders::Request> {
            timedOut = true;
            return nullptr;
          }));
        }
        return kj::mv(readHeaders);
      } else {
        // Client closed connection or pipeline timed out with no bytes received. This is not an
        // error, so don't report one.
        this->closed = true;
        return kj::Maybe<HttpHeaders::Request>(nullptr);
      }
    });

    if (firstRequest) {
      // On the first request, the header timeout starts ticking immediately upon request opening.
      receivedHeaders = receivedHeaders.exclusiveJoin(
          server.timer.afterDelay(server.settings.headerTimeout)
          .then([this]() -> kj::Maybe<HttpHeaders::Request> {
        timedOut = true;
        return nullptr;
      }));
    }

    return receivedHeaders
        .then([this,firstRequest](kj::Maybe<HttpHeaders::Request>&& request) -> kj::Promise<void> {
      if (closed) {
        // Client closed connection. Close our end too.
        return httpOutput.flush();
      }
      if (timedOut) {
        if (firstRequest) {
          return sendError(408, "Request Timeout", kj::str(
              "ERROR: Your client took too long to send HTTP headers."));
        } else {
          // After the first request, we just close the connection if the client takes too long,
          // on the assumption that the client is intentionally trying to keep the connection
          // around for reuse (not necessarily an error).
          return httpOutput.flush();
        }
      }

      KJ_IF_MAYBE(req, request) {
        currentMethod = req->method;
        auto body = httpInput.getEntityBody(
            HttpInputStream::REQUEST, req->method, 0, req->connectionHeaders);

        // TODO(perf): If the client disconnects, should we cancel the response? Probably, to
        //   prevent permanent deadlock. It's slightly weird in that arguably the client should
        //   be able to shutdown the upstream but still wait on the downstream, but I believe many
        //   other HTTP servers do similar things.

        auto promise = server.service.request(
            req->method, req->url, httpInput.getHeaders(), *body, *this);
        return promise.attach(kj::mv(body))
            .then([this]() { return httpOutput.flush(); })
            .then([this]() -> kj::Promise<void> {
          // Response done. Await next request.

          if (currentMethod != nullptr) {
            return sendError(500, "Internal Server Error", kj::str(
                "ERROR: The HttpService did not generate a response."));
          }

          if (server.draining) {
            // Never mind, drain time.
            return httpOutput.flush();
          }

          return loop(false);
        });
      } else {
        // Bad request.

        return sendError(400, "Bad Request", kj::str(
            "ERROR: The headers sent by your client were not valid."));
      }
    }).catch_([this](kj::Exception&& e) -> kj::Promise<void> {
      // Exception; report 500.

      if (currentMethod == nullptr) {
        // Dang, already sent a partial response. Can't do anything else.
        //
        // If it's a DISCONNECTED exception, it's probably that the client disconnected, which is
        // not really worth logging.
        if (e.getType() != kj::Exception::Type::DISCONNECTED) {
          KJ_LOG(ERROR, "HttpService threw exception after generating a partial response",
                        "too late to report error to client", e);
        }
        return kj::READY_NOW;
      }

      if (e.getType() == kj::Exception::Type::OVERLOADED) {
        return sendError(503, "Service Unavailable", kj::str(
            "ERROR: The server is temporarily unable to handle your request. Details:\n\n", e));
      } else if (e.getType() == kj::Exception::Type::UNIMPLEMENTED) {
        return sendError(501, "Not Implemented", kj::str(
            "ERROR: The server does not implement this operation. Details:\n\n", e));
      } else if (e.getType() == kj::Exception::Type::DISCONNECTED) {
        // How do we tell an HTTP client that there was a transient network error, and it should
        // try again immediately? There's no HTTP status code for this (503 is meant for "try
        // again later, not now"). Here's an idea: Don't send any response; just close the
        // connection, so that it looks like the connection between the HTTP client and server
        // was dropped. A good client should treat this exactly the way we want.
        return kj::READY_NOW;
      } else {
        return sendError(500, "Internal Server Error", kj::str(
            "ERROR: The server threw an exception. Details:\n\n", e));
      }
    });
  }

private:
  HttpServer& server;
  HttpInputStream httpInput;
  HttpOutputStream httpOutput;
  kj::Own<kj::AsyncIoStream> ownStream;
  kj::Maybe<HttpMethod> currentMethod;
  bool timedOut = false;
  bool closed = false;

  kj::Own<kj::AsyncOutputStream> send(
      uint statusCode, kj::StringPtr statusText, const HttpHeaders& headers,
      kj::Maybe<uint64_t> expectedBodySize) override {
    auto method = KJ_REQUIRE_NONNULL(currentMethod, "already called startResponse()");
    currentMethod = nullptr;

    HttpHeaders::ConnectionHeaders connectionHeaders;
    kj::String lengthStr;

    if (statusCode == 204 || statusCode == 205 || statusCode == 304) {
      // No entity-body.
    } else KJ_IF_MAYBE(s, expectedBodySize) {
      lengthStr = kj::str(*s);
      connectionHeaders.contentLength = lengthStr;
    } else {
      connectionHeaders.transferEncoding = "chunked";
    }

    httpOutput.writeHeaders(headers.serializeResponse(statusCode, statusText, connectionHeaders));

    kj::Own<kj::AsyncOutputStream> bodyStream;
    if (method == HttpMethod::HEAD) {
      // Ignore entity-body.
      httpOutput.finishBody();
      return heap<HttpDiscardingEntityWriter>();
    } else if (statusCode == 204 || statusCode == 205 || statusCode == 304) {
      // No entity-body.
      httpOutput.finishBody();
      return heap<HttpNullEntityWriter>();
    } else KJ_IF_MAYBE(s, expectedBodySize) {
      return heap<HttpFixedLengthEntityWriter>(httpOutput, *s);
    } else {
      return heap<HttpChunkedEntityWriter>(httpOutput);
    }
  }

  kj::Promise<void> sendError(uint statusCode, kj::StringPtr statusText, kj::String body) {
    auto bodySize = kj::str(body.size());

    HttpHeaders failed(server.requestHeaderTable);
    HttpHeaders::ConnectionHeaders connHeaders;
    connHeaders.connection = "close";
    connHeaders.contentLength = bodySize;

    failed.set(HttpHeaderId::CONTENT_TYPE, "text/plain");

    httpOutput.writeHeaders(failed.serializeResponse(statusCode, statusText, connHeaders));
    httpOutput.writeBodyData(kj::mv(body));
    httpOutput.finishBody();
    return httpOutput.flush();  // loop ends after flush
  }
};

HttpServer::HttpServer(kj::Timer& timer, HttpHeaderTable& requestHeaderTable, HttpService& service,
                       Settings settings)
    : HttpServer(timer, requestHeaderTable, service, settings,
                 kj::newPromiseAndFulfiller<void>()) {}

HttpServer::HttpServer(kj::Timer& timer, HttpHeaderTable& requestHeaderTable, HttpService& service,
                       Settings settings, kj::PromiseFulfillerPair<void> paf)
    : timer(timer), requestHeaderTable(requestHeaderTable), service(service), settings(settings),
      onDrain(paf.promise.fork()), drainFulfiller(kj::mv(paf.fulfiller)), tasks(*this) {}

kj::Promise<void> HttpServer::drain() {
  KJ_REQUIRE(!draining, "you can only call drain() once");

  draining = true;
  drainFulfiller->fulfill();

  if (connectionCount == 0) {
    return kj::READY_NOW;
  } else {
    auto paf = kj::newPromiseAndFulfiller<void>();
    zeroConnectionsFulfiller = kj::mv(paf.fulfiller);
    return kj::mv(paf.promise);
  }
}

kj::Promise<void> HttpServer::listenHttp(kj::ConnectionReceiver& port) {
  return listenLoop(port).exclusiveJoin(onDrain.addBranch());
}

kj::Promise<void> HttpServer::listenLoop(kj::ConnectionReceiver& port) {
  return port.accept()
      .then([this,&port](kj::Own<kj::AsyncIoStream>&& connection) -> kj::Promise<void> {
    if (draining) {
      // Can get here if we *just* started draining.
      return kj::READY_NOW;
    }

    tasks.add(listenHttp(kj::mv(connection)));
    return listenLoop(port);
  });
}

kj::Promise<void> HttpServer::listenHttp(kj::Own<kj::AsyncIoStream> connection) {
  auto obj = heap<Connection>(*this, kj::mv(connection));
  auto promise = obj->loop(true);

  // Eagerly evaluate so that we drop the connection when the promise resolves, even if the caller
  // doesn't eagerly evaluate.
  return promise.attach(kj::mv(obj)).eagerlyEvaluate(nullptr);
}

void HttpServer::taskFailed(kj::Exception&& exception) {
  KJ_LOG(ERROR, "unhandled exception in HTTP server", exception);
}

} // namespace kj
