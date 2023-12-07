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
#include "kj/exception.h"
#include "url.h"
#include <kj/debug.h>
#include <kj/parse/char.h>
#include <kj/string.h>
#include <unordered_map>
#include <stdlib.h>
#include <kj/encoding.h>
#include <deque>
#include <queue>
#include <map>
#if KJ_HAS_ZLIB
#include <zlib.h>
#endif // KJ_HAS_ZLIB

namespace kj {

// =======================================================================================
// SHA-1 implementation from https://github.com/clibs/sha1
//
// The WebSocket standard depends on SHA-1. ARRRGGGHHHHH.
//
// Any old checksum would have served the purpose, or hell, even just returning the header
// verbatim. But NO, they decided to throw a whole complicated hash algorithm in there, AND
// THEY CHOSE A BROKEN ONE THAT WE OTHERWISE WOULDN'T NEED ANYMORE.
//
// TODO(cleanup): Move this to a shared hashing library. Maybe. Or maybe don't, because no one
//   should be using SHA-1 anymore.
//
// THIS USAGE IS NOT SECURITY SENSITIVE. IF YOU REPORT A SECURITY ISSUE BECAUSE YOU SAW SHA1 IN THE
// SOURCE CODE I WILL MAKE FUN OF YOU.

/*
SHA-1 in C
By Steve Reid <steve@edmweb.com>
100% Public Domain
Test Vectors (from FIPS PUB 180-1)
"abc"
  A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D
"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
  84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1
A million repetitions of "a"
  34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F
*/

/* #define LITTLE_ENDIAN * This should be #define'd already, if true. */
/* #define SHA1HANDSOFF * Copies data before messing with it. */

#define SHA1HANDSOFF

typedef struct
{
    uint32_t state[5];
    uint32_t count[2];
    unsigned char buffer[64];
} SHA1_CTX;

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* blk0() and blk() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */
#if BYTE_ORDER == LITTLE_ENDIAN
#define blk0(i) (block->l[i] = (rol(block->l[i],24)&0xFF00FF00) \
    |(rol(block->l[i],8)&0x00FF00FF))
#elif BYTE_ORDER == BIG_ENDIAN
#define blk0(i) block->l[i]
#else
#error "Endianness not defined!"
#endif
#define blk(i) (block->l[i&15] = rol(block->l[(i+13)&15]^block->l[(i+8)&15] \
    ^block->l[(i+2)&15]^block->l[i&15],1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);


/* Hash a single 512-bit block. This is the core of the algorithm. */

void SHA1Transform(
    uint32_t state[5],
    const unsigned char buffer[64]
)
{
    uint32_t a, b, c, d, e;

    typedef union
    {
        unsigned char c[64];
        uint32_t l[16];
    } CHAR64LONG16;

#ifdef SHA1HANDSOFF
    CHAR64LONG16 block[1];      /* use array to appear as a pointer */

    memcpy(block, buffer, 64);
#else
    /* The following had better never be used because it causes the
     * pointer-to-const buffer to be cast into a pointer to non-const.
     * And the result is written through.  I threw a "const" in, hoping
     * this will cause a diagnostic.
     */
    CHAR64LONG16 *block = (const CHAR64LONG16 *) buffer;
#endif
    /* Copy context->state[] to working vars */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    /* 4 rounds of 20 operations each. Loop unrolled. */
    R0(a, b, c, d, e, 0);
    R0(e, a, b, c, d, 1);
    R0(d, e, a, b, c, 2);
    R0(c, d, e, a, b, 3);
    R0(b, c, d, e, a, 4);
    R0(a, b, c, d, e, 5);
    R0(e, a, b, c, d, 6);
    R0(d, e, a, b, c, 7);
    R0(c, d, e, a, b, 8);
    R0(b, c, d, e, a, 9);
    R0(a, b, c, d, e, 10);
    R0(e, a, b, c, d, 11);
    R0(d, e, a, b, c, 12);
    R0(c, d, e, a, b, 13);
    R0(b, c, d, e, a, 14);
    R0(a, b, c, d, e, 15);
    R1(e, a, b, c, d, 16);
    R1(d, e, a, b, c, 17);
    R1(c, d, e, a, b, 18);
    R1(b, c, d, e, a, 19);
    R2(a, b, c, d, e, 20);
    R2(e, a, b, c, d, 21);
    R2(d, e, a, b, c, 22);
    R2(c, d, e, a, b, 23);
    R2(b, c, d, e, a, 24);
    R2(a, b, c, d, e, 25);
    R2(e, a, b, c, d, 26);
    R2(d, e, a, b, c, 27);
    R2(c, d, e, a, b, 28);
    R2(b, c, d, e, a, 29);
    R2(a, b, c, d, e, 30);
    R2(e, a, b, c, d, 31);
    R2(d, e, a, b, c, 32);
    R2(c, d, e, a, b, 33);
    R2(b, c, d, e, a, 34);
    R2(a, b, c, d, e, 35);
    R2(e, a, b, c, d, 36);
    R2(d, e, a, b, c, 37);
    R2(c, d, e, a, b, 38);
    R2(b, c, d, e, a, 39);
    R3(a, b, c, d, e, 40);
    R3(e, a, b, c, d, 41);
    R3(d, e, a, b, c, 42);
    R3(c, d, e, a, b, 43);
    R3(b, c, d, e, a, 44);
    R3(a, b, c, d, e, 45);
    R3(e, a, b, c, d, 46);
    R3(d, e, a, b, c, 47);
    R3(c, d, e, a, b, 48);
    R3(b, c, d, e, a, 49);
    R3(a, b, c, d, e, 50);
    R3(e, a, b, c, d, 51);
    R3(d, e, a, b, c, 52);
    R3(c, d, e, a, b, 53);
    R3(b, c, d, e, a, 54);
    R3(a, b, c, d, e, 55);
    R3(e, a, b, c, d, 56);
    R3(d, e, a, b, c, 57);
    R3(c, d, e, a, b, 58);
    R3(b, c, d, e, a, 59);
    R4(a, b, c, d, e, 60);
    R4(e, a, b, c, d, 61);
    R4(d, e, a, b, c, 62);
    R4(c, d, e, a, b, 63);
    R4(b, c, d, e, a, 64);
    R4(a, b, c, d, e, 65);
    R4(e, a, b, c, d, 66);
    R4(d, e, a, b, c, 67);
    R4(c, d, e, a, b, 68);
    R4(b, c, d, e, a, 69);
    R4(a, b, c, d, e, 70);
    R4(e, a, b, c, d, 71);
    R4(d, e, a, b, c, 72);
    R4(c, d, e, a, b, 73);
    R4(b, c, d, e, a, 74);
    R4(a, b, c, d, e, 75);
    R4(e, a, b, c, d, 76);
    R4(d, e, a, b, c, 77);
    R4(c, d, e, a, b, 78);
    R4(b, c, d, e, a, 79);
    /* Add the working vars back into context.state[] */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    /* Wipe variables */
    a = b = c = d = e = 0;
#ifdef SHA1HANDSOFF
    memset(block, '\0', sizeof(block));
#endif
}


/* SHA1Init - Initialize new context */

void SHA1Init(
    SHA1_CTX * context
)
{
    /* SHA1 initialization constants */
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->count[0] = context->count[1] = 0;
}


/* Run your data through this. */

void SHA1Update(
    SHA1_CTX * context,
    const unsigned char *data,
    uint32_t len
)
{
    uint32_t i;

    uint32_t j;

    j = context->count[0];
    if ((context->count[0] += len << 3) < j)
        context->count[1]++;
    context->count[1] += (len >> 29);
    j = (j >> 3) & 63;
    if ((j + len) > 63)
    {
        memcpy(&context->buffer[j], data, (i = 64 - j));
        SHA1Transform(context->state, context->buffer);
        for (; i + 63 < len; i += 64)
        {
            SHA1Transform(context->state, &data[i]);
        }
        j = 0;
    }
    else
        i = 0;
    memcpy(&context->buffer[j], &data[i], len - i);
}


/* Add padding and return the message digest. */

void SHA1Final(
    unsigned char digest[20],
    SHA1_CTX * context
)
{
    unsigned i;

    unsigned char finalcount[8];

    unsigned char c;

#if 0    /* untested "improvement" by DHR */
    /* Convert context->count to a sequence of bytes
     * in finalcount.  Second element first, but
     * big-endian order within element.
     * But we do it all backwards.
     */
    unsigned char *fcp = &finalcount[8];
    for (i = 0; i < 2; i++)
    {
        uint32_t t = context->count[i];
        int j;
        for (j = 0; j < 4; t >>= 8, j++)
            *--fcp = (unsigned char) t}
#else
    for (i = 0; i < 8; i++)
    {
        finalcount[i] = (unsigned char) ((context->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);      /* Endian independent */
    }
#endif
    c = 0200;
    SHA1Update(context, &c, 1);
    while ((context->count[0] & 504) != 448)
    {
        c = 0000;
        SHA1Update(context, &c, 1);
    }
    SHA1Update(context, finalcount, 8); /* Should cause a SHA1Transform() */
    for (i = 0; i < 20; i++)
    {
        digest[i] = (unsigned char)
            ((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
    /* Wipe variables */
    memset(context, '\0', sizeof(*context));
    memset(&finalcount, '\0', sizeof(finalcount));
}

// End SHA-1 implementation.
// =======================================================================================

static const char* METHOD_NAMES[] = {
#define METHOD_NAME(id) #id,
KJ_HTTP_FOR_EACH_METHOD(METHOD_NAME)
#undef METHOD_NAME
};

kj::StringPtr KJ_STRINGIFY(HttpMethod method) {
  return METHOD_NAMES[static_cast<uint>(method)];
}

kj::StringPtr KJ_STRINGIFY(HttpConnectMethod method) {
  return "CONNECT"_kj;
}

static kj::Maybe<kj::OneOf<HttpMethod, HttpConnectMethod>> consumeHttpMethod(char*& ptr) {
  char* p = ptr;

#define EXPECT_REST(prefix, suffix) \
  if (strncmp(p, #suffix, sizeof(#suffix)-1) == 0) { \
    ptr = p + (sizeof(#suffix)-1); \
    return kj::Maybe<kj::OneOf<HttpMethod, HttpConnectMethod>>(HttpMethod::prefix##suffix); \
  } else { \
    return nullptr; \
  }

  switch (*p++) {
    case 'A': EXPECT_REST(A,CL)
    case 'C':
      switch (*p++) {
        case 'H': EXPECT_REST(CH,ECKOUT)
        case 'O':
          switch (*p++) {
            case 'P': EXPECT_REST(COP,Y)
            case 'N':
              if (strncmp(p, "NECT", 4) == 0) {
                ptr = p + 4;
                return kj::Maybe<kj::OneOf<HttpMethod, HttpConnectMethod>>(HttpConnectMethod());
              } else {
                return nullptr;
              }
            default: return nullptr;
          }
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
  KJ_IF_MAYBE(method, tryParseHttpMethodAllowingConnect(name)) {
    KJ_SWITCH_ONEOF(*method) {
      KJ_CASE_ONEOF(m, HttpMethod) { return m; }
      KJ_CASE_ONEOF(m, HttpConnectMethod) { return nullptr; }
    }
    KJ_UNREACHABLE;
  } else {
    return nullptr;
  }
}

kj::Maybe<kj::OneOf<HttpMethod, HttpConnectMethod>> tryParseHttpMethodAllowingConnect(
    kj::StringPtr name) {
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

constexpr char WEBSOCKET_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
// From RFC6455.

static kj::String generateWebSocketAccept(kj::StringPtr key) {
  // WebSocket demands we do a SHA-1 here. ARRGHH WHY SHA-1 WHYYYYYY?
  SHA1_CTX ctx;
  byte digest[20];
  SHA1Init(&ctx);
  SHA1Update(&ctx, key.asBytes().begin(), key.size());
  SHA1Update(&ctx, reinterpret_cast<const byte*>(WEBSOCKET_GUID), strlen(WEBSOCKET_GUID));
  SHA1Final(digest, &ctx);
  return kj::encodeBase64(digest);
}

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
  KJ_REQUIRE(HttpHeaders::isValidHeaderValue(value), "invalid header value",
      kj::encodeCEscape(value));
}

static const char* BUILTIN_HEADER_NAMES[] = {
  // Indexed by header ID, which includes connection headers, so we include those names too.
#define HEADER_NAME(id, name) name,
  KJ_HTTP_FOR_EACH_BUILTIN_HEADER(HEADER_NAME)
#undef HEADER_NAME
};

}  // namespace

#define HEADER_ID(id, name) constexpr uint HttpHeaders::BuiltinIndices::id;
  KJ_HTTP_FOR_EACH_BUILTIN_HEADER(HEADER_ID)
#undef HEADER_ID

#define DEFINE_HEADER(id, name) \
const HttpHeaderId HttpHeaderId::id(nullptr, HttpHeaders::BuiltinIndices::id);
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
    : table(kj::heap<HttpHeaderTable>()) {
  table->buildStatus = BuildStatus::BUILDING;
}

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
  namesById.add(name); \
  idsByName->map.insert(std::make_pair(name, HttpHeaders::BuiltinIndices::id));
  KJ_HTTP_FOR_EACH_BUILTIN_HEADER(ADD_HEADER);
#undef ADD_HEADER
}
HttpHeaderTable::~HttpHeaderTable() noexcept(false) {}

kj::Maybe<HttpHeaderId> HttpHeaderTable::stringToId(kj::StringPtr name) const {
  auto iter = idsByName->map.find(name);
  if (iter == idsByName->map.end()) {
    return nullptr;
  } else {
    return HttpHeaderId(this, iter->second);
  }
}

// =======================================================================================

bool HttpHeaders::isValidHeaderValue(kj::StringPtr value) {
  for (char c: value) {
    // While the HTTP spec suggests that only printable ASCII characters are allowed in header
    // values, reality has a different opinion. See: https://github.com/httpwg/http11bis/issues/19
    // We follow the browsers' lead.
    if (c == '\0' || c == '\r' || c == '\n') {
      return false;
    }
  }

  return true;
}

HttpHeaders::HttpHeaders(const HttpHeaderTable& table)
    : table(&table),
      indexedHeaders(kj::heapArray<kj::StringPtr>(table.idCount())) {
  KJ_ASSERT(
      table.isReady(), "HttpHeaders object was constructed from "
      "HttpHeaderTable that wasn't fully built yet at the time of construction");
}

void HttpHeaders::clear() {
  for (auto& header: indexedHeaders) {
    header = nullptr;
  }

  unindexedHeaders.clear();
}

size_t HttpHeaders::size() const {
  size_t result = unindexedHeaders.size();
  for (auto i: kj::indices(indexedHeaders)) {
    if (indexedHeaders[i] != nullptr) {
      ++result;
    }
  }
  return result;
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


namespace {

template <char... chars>
constexpr bool fastCaseCmp(const char* actual);

}  // namespace

bool HttpHeaders::isWebSocket() const {
  return fastCaseCmp<'w', 'e', 'b', 's', 'o', 'c', 'k', 'e', 't'>(
      get(HttpHeaderId::UPGRADE).orDefault(nullptr).cStr());
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

  addNoCheck(name, value);
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

void HttpHeaders::addNoCheck(kj::StringPtr name, kj::StringPtr value) {
  KJ_IF_MAYBE(id, table->stringToId(name)) {
    if (indexedHeaders[id->id] == nullptr) {
      indexedHeaders[id->id] = value;
    } else {
      // Duplicate HTTP headers are equivalent to the values being separated by a comma.

#if _MSC_VER
      if (_stricmp(name.cStr(), "set-cookie") == 0) {
#else
      if (strcasecmp(name.cStr(), "set-cookie") == 0) {
#endif
        // Uh-oh, Set-Cookie will be corrupted if we try to concatenate it. We'll make it an
        // unindexed header, which is weird, but the alternative is guaranteed corruption, so...
        // TODO(cleanup): Maybe HttpHeaders should just special-case set-cookie in general?
        unindexedHeaders.add(Header {name, value});
      } else {
        auto concat = kj::str(indexedHeaders[id->id], ", ", value);
        indexedHeaders[id->id] = concat;
        ownedStrings.add(concat.releaseArray());
      }
    }
  } else {
    unindexedHeaders.add(Header {name, value});
  }
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

HttpHeaders::RequestOrProtocolError HttpHeaders::tryParseRequest(kj::ArrayPtr<char> content) {
  KJ_SWITCH_ONEOF(tryParseRequestOrConnect(content)) {
    KJ_CASE_ONEOF(request, Request) {
      return kj::mv(request);
    }
    KJ_CASE_ONEOF(error, ProtocolError) {
      return kj::mv(error);
    }
    KJ_CASE_ONEOF(connect, ConnectRequest) {
      return ProtocolError { 501, "Not Implemented",
          "Unrecognized request method.", content };
    }
  }
  KJ_UNREACHABLE;
}

HttpHeaders::RequestConnectOrProtocolError HttpHeaders::tryParseRequestOrConnect(
    kj::ArrayPtr<char> content) {
  char* end = trimHeaderEnding(content);
  if (end == nullptr) {
    return ProtocolError { 400, "Bad Request",
        "Request headers have no terminal newline.", content };
  }

  char* ptr = content.begin();

  HttpHeaders::RequestConnectOrProtocolError result;

  KJ_IF_MAYBE(method, consumeHttpMethod(ptr)) {
    if (*ptr != ' ' && *ptr != '\t') {
      return ProtocolError { 501, "Not Implemented",
          "Unrecognized request method.", content };
    }
    ++ptr;

    kj::Maybe<StringPtr> path;
    KJ_IF_MAYBE(p, consumeWord(ptr)) {
      path = *p;
    } else {
      return ProtocolError { 400, "Bad Request",
          "Invalid request line.", content };
    }

    KJ_SWITCH_ONEOF(*method) {
      KJ_CASE_ONEOF(m, HttpMethod) {
        result = HttpHeaders::Request { m, KJ_ASSERT_NONNULL(path) };
      }
      KJ_CASE_ONEOF(m, HttpConnectMethod) {
        result = HttpHeaders::ConnectRequest { KJ_ASSERT_NONNULL(path) };
      }
    }
  } else {
    return ProtocolError { 501, "Not Implemented",
        "Unrecognized request method.", content };
  }

  // Ignore rest of line. Don't care about "HTTP/1.1" or whatever.
  consumeLine(ptr);

  if (!parseHeaders(ptr, end)) {
    return ProtocolError { 400, "Bad Request",
        "The headers sent by your client are not valid.", content };
  }

  return result;
}

HttpHeaders::ResponseOrProtocolError HttpHeaders::tryParseResponse(kj::ArrayPtr<char> content) {
  char* end = trimHeaderEnding(content);
  if (end == nullptr) {
    return ProtocolError { 502, "Bad Gateway",
        "Response headers have no terminal newline.", content };
  }

  char* ptr = content.begin();

  HttpHeaders::Response response;

  KJ_IF_MAYBE(version, consumeWord(ptr)) {
    if (!version->startsWith("HTTP/")) {
      return ProtocolError { 502, "Bad Gateway",
          "Invalid response status line (invalid protocol).", content };
    }
  } else {
    return ProtocolError { 502, "Bad Gateway",
        "Invalid response status line (no spaces).", content };
  }

  KJ_IF_MAYBE(code, consumeNumber(ptr)) {
    response.statusCode = *code;
  } else {
    return ProtocolError { 502, "Bad Gateway",
        "Invalid response status line (invalid status code).", content };
  }

  response.statusText = consumeLine(ptr);

  if (!parseHeaders(ptr, end)) {
    return ProtocolError { 502, "Bad Gateway",
        "The headers sent by the server are not valid.", content };
  }

  return response;
}

bool HttpHeaders::tryParse(kj::ArrayPtr<char> content) {
  char* end = trimHeaderEnding(content);
  if (end == nullptr) return false;

  char* ptr = content.begin();
  return parseHeaders(ptr, end);
}

bool HttpHeaders::parseHeaders(char* ptr, char* end) {
  while (*ptr != '\0') {
    KJ_IF_MAYBE(name, consumeHeaderName(ptr)) {
      kj::StringPtr line = consumeLine(ptr);
      addNoCheck(*name, line);
    } else {
      return false;
    }
  }

  return ptr == end;
}

// -----------------------------------------------------------------------------

kj::String HttpHeaders::serializeRequest(
    HttpMethod method, kj::StringPtr url,
    kj::ArrayPtr<const kj::StringPtr> connectionHeaders) const {
  return serialize(kj::toCharSequence(method), url, kj::StringPtr("HTTP/1.1"), connectionHeaders);
}

kj::String HttpHeaders::serializeConnectRequest(
    kj::StringPtr authority,
    kj::ArrayPtr<const kj::StringPtr> connectionHeaders) const {
  return serialize("CONNECT"_kj, authority, kj::StringPtr("HTTP/1.1"), connectionHeaders);
}

kj::String HttpHeaders::serializeResponse(
    uint statusCode, kj::StringPtr statusText,
    kj::ArrayPtr<const kj::StringPtr> connectionHeaders) const {
  auto statusCodeStr = kj::toCharSequence(statusCode);

  return serialize(kj::StringPtr("HTTP/1.1"), statusCodeStr, statusText, connectionHeaders);
}

kj::String HttpHeaders::serialize(kj::ArrayPtr<const char> word1,
                                  kj::ArrayPtr<const char> word2,
                                  kj::ArrayPtr<const char> word3,
                                  kj::ArrayPtr<const kj::StringPtr> connectionHeaders) const {
  const kj::StringPtr space = " ";
  const kj::StringPtr newline = "\r\n";
  const kj::StringPtr colon = ": ";

  size_t size = 2;  // final \r\n
  if (word1 != nullptr) {
    size += word1.size() + word2.size() + word3.size() + 4;
  }
  KJ_ASSERT(connectionHeaders.size() <= indexedHeaders.size());
  for (auto i: kj::indices(indexedHeaders)) {
    kj::StringPtr value = i < connectionHeaders.size() ? connectionHeaders[i] : indexedHeaders[i];
    if (value != nullptr) {
      size += table->idToString(HttpHeaderId(table, i)).size() + value.size() + 4;
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
  for (auto i: kj::indices(indexedHeaders)) {
    kj::StringPtr value = i < connectionHeaders.size() ? connectionHeaders[i] : indexedHeaders[i];
    if (value != nullptr) {
      ptr = kj::_::fill(ptr, table->idToString(HttpHeaderId(table, i)), colon, value, newline);
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
  return serialize(nullptr, nullptr, nullptr, nullptr);
}

// =======================================================================================

namespace {

template <typename Subclass>
class WrappableStreamMixin {
  // Both HttpInputStreamImpl and HttpOutputStream are commonly wrapped by a class that implements
  // a particular type of body stream, such as a chunked body or a fixed-length body. That wrapper
  // stream is passed back to the application to represent the specific request/response body, but
  // the inner stream is associated with the connection and can be reused several times.
  //
  // It's easy for applications to screw up and hold on to a body stream beyond the lifetime of the
  // underlying connection stream. This used to lead to UAF. This mixin class implements behavior
  // that detached the wrapper if it outlives the wrapped stream, so that we log errors and

public:
  WrappableStreamMixin() = default;
  WrappableStreamMixin(WrappableStreamMixin&& other) {
    // This constructor is only needed by HttpServer::Connection::makeHttpInput() which constructs
    // a new stream and returns it. Technically the constructor will always be elided anyway.
    KJ_REQUIRE(other.currentWrapper == nullptr, "can't move a wrappable object that has wrappers!");
  }
  KJ_DISALLOW_COPY(WrappableStreamMixin);

  ~WrappableStreamMixin() noexcept(false) {
    KJ_IF_MAYBE(w, currentWrapper) {
      KJ_LOG(ERROR, "HTTP connection destroyed while HTTP body streams still exist",
          kj::getStackTrace());
      *w = nullptr;
    }
  }

  void setCurrentWrapper(kj::Maybe<Subclass&>& weakRef) {
    // Tracks the current `HttpEntityBodyReader` instance which is wrapping this stream. There can
    // be only one wrapper at a time, and the wrapper must be destroyed before the underlying HTTP
    // connection is torn down. The purpose of tracking the wrapper here is to detect when these
    // rules are violated by apps, and log an error instead of going UB.
    //
    // `weakRef` is the wrapper's pointer to this object. If the underlying stream is destroyed
    // before the wrapper, then `weakRef` will be nulled out.

    // The API should prevent an app from obtaining multiple wrappers with the same backing stream.
    KJ_ASSERT(currentWrapper == nullptr,
        "bug in KJ HTTP: only one HTTP stream wrapper can exist at a time");

    currentWrapper = weakRef;
    weakRef = static_cast<Subclass&>(*this);
  }

  void unsetCurrentWrapper(kj::Maybe<Subclass&>& weakRef) {
    auto& current = KJ_ASSERT_NONNULL(currentWrapper);
    KJ_ASSERT(&current == &weakRef,
        "bug in KJ HTTP: unsetCurrentWrapper() passed the wrong wrapper");
    weakRef = nullptr;
    currentWrapper = nullptr;
  }

private:
  kj::Maybe<kj::Maybe<Subclass&>&> currentWrapper;
};

// =======================================================================================

static constexpr size_t MIN_BUFFER = 4096;
static constexpr size_t MAX_BUFFER = 128 * 1024;
static constexpr size_t MAX_CHUNK_HEADER_SIZE = 32;

class HttpInputStreamImpl final: public HttpInputStream,
                                 public WrappableStreamMixin<HttpInputStreamImpl> {
private:
  static kj::OneOf<HttpHeaders::Request, HttpHeaders::ConnectRequest> getResumingRequest(
      kj::OneOf<HttpMethod, HttpConnectMethod> method,
      kj::StringPtr url) {
    KJ_SWITCH_ONEOF(method) {
      KJ_CASE_ONEOF(m, HttpMethod) {
        return HttpHeaders::Request { m, url };
      }
      KJ_CASE_ONEOF(m, HttpConnectMethod) {
        return HttpHeaders::ConnectRequest { url };
      }
    }
    KJ_UNREACHABLE;
  }
public:
  explicit HttpInputStreamImpl(AsyncInputStream& inner, const HttpHeaderTable& table)
      : inner(inner), headerBuffer(kj::heapArray<char>(MIN_BUFFER)), headers(table) {
  }

  explicit HttpInputStreamImpl(AsyncInputStream& inner,
      kj::Array<char> headerBufferParam,
      kj::ArrayPtr<char> leftoverParam,
      kj::OneOf<HttpMethod, HttpConnectMethod> method,
      kj::StringPtr url,
      HttpHeaders headers)
      : inner(inner),
        headerBuffer(kj::mv(headerBufferParam)),
        // Initialize `messageHeaderEnd` to a safe value, we'll adjust it below.
        messageHeaderEnd(leftoverParam.begin() - headerBuffer.begin()),
        leftover(leftoverParam),
        headers(kj::mv(headers)),
        resumingRequest(getResumingRequest(method, url)) {
    // Constructor used for resuming a SuspendedRequest.

    // We expect headerBuffer to look like this:
    //   <method> <url> <headers> [CR] LF <leftover>
    // We initialized `messageHeaderEnd` to the beginning of `leftover`, but we want to point it at
    // the CR (or LF if there's no CR).
    KJ_REQUIRE(messageHeaderEnd >= 2 && leftover.end() <= headerBuffer.end(),
        "invalid SuspendedRequest - leftover buffer not where it should be");
    KJ_REQUIRE(leftover.begin()[-1] == '\n', "invalid SuspendedRequest - missing LF");
    messageHeaderEnd -= 1 + (leftover.begin()[-2] == '\r');

    // We're in the middle of a message, so set up our state as such. Note that the only way to
    // resume a SuspendedRequest is via an HttpServer, but HttpServers never call
    // `awaitNextMessage()` before fully reading request bodies, meaning we expect that
    // `messageReadQueue` will never be used.
    ++pendingMessageCount;
    auto paf = kj::newPromiseAndFulfiller<void>();
    onMessageDone = kj::mv(paf.fulfiller);
    messageReadQueue = kj::mv(paf.promise);
  }

  bool canReuse() {
    return !broken && pendingMessageCount == 0;
  }

  bool canSuspend() {
    // We are at a suspendable point if we've parsed the headers, but haven't consumed anything
    // beyond that.
    //
    // TODO(cleanup): This is a silly check; we need a more defined way to track the state of the
    //   stream.
    bool messageHeaderEndLooksRight =
        (leftover.begin() - (headerBuffer.begin() + messageHeaderEnd) == 2 &&
            leftover.begin()[-1] == '\n' && leftover.begin()[-2] == '\r')
        || (leftover.begin() - (headerBuffer.begin() + messageHeaderEnd) == 1 &&
            leftover.begin()[-1] == '\n');

    return !broken && headerBuffer.size() > 0 && messageHeaderEndLooksRight;
  }

  // ---------------------------------------------------------------------------
  // public interface

  kj::Promise<Request> readRequest() override {
    return readRequestHeaders()
        .then([this](HttpHeaders::RequestConnectOrProtocolError&& requestOrProtocolError)
            -> HttpInputStream::Request {
      auto request = KJ_REQUIRE_NONNULL(
          requestOrProtocolError.tryGet<HttpHeaders::Request>(), "bad request");
      auto body = getEntityBody(HttpInputStreamImpl::REQUEST, request.method, 0, headers);

      return { request.method, request.url, headers, kj::mv(body) };
    });
  }

  kj::Promise<kj::OneOf<Request, Connect>> readRequestAllowingConnect() override {
    return readRequestHeaders()
        .then([this](HttpHeaders::RequestConnectOrProtocolError&& requestOrProtocolError)
            -> kj::OneOf<HttpInputStream::Request, HttpInputStream::Connect> {
      KJ_SWITCH_ONEOF(requestOrProtocolError) {
        KJ_CASE_ONEOF(request, HttpHeaders::Request) {
          auto body = getEntityBody(HttpInputStreamImpl::REQUEST, request.method, 0, headers);
          return HttpInputStream::Request { request.method, request.url, headers, kj::mv(body) };
        }
        KJ_CASE_ONEOF(request, HttpHeaders::ConnectRequest) {
          auto body = getEntityBody(HttpInputStreamImpl::REQUEST, HttpConnectMethod(), 0, headers);
          return HttpInputStream::Connect { request.authority, headers, kj::mv(body) };
        }
        KJ_CASE_ONEOF(error, HttpHeaders::ProtocolError) {
          KJ_FAIL_REQUIRE("bad request");
        }
      }
      KJ_UNREACHABLE;
    });
  }

  kj::Promise<Response> readResponse(HttpMethod requestMethod) override {
    return readResponseHeaders()
        .then([this,requestMethod](HttpHeaders::ResponseOrProtocolError&& responseOrProtocolError)
              -> HttpInputStream::Response {
      auto response = KJ_REQUIRE_NONNULL(
          responseOrProtocolError.tryGet<HttpHeaders::Response>(), "bad response");
      auto body = getEntityBody(HttpInputStreamImpl::RESPONSE, requestMethod,
                                response.statusCode, headers);

      return { response.statusCode, response.statusText, headers, kj::mv(body) };
    });
  }

  kj::Promise<Message> readMessage() override {
    return readMessageHeaders()
        .then([this](kj::ArrayPtr<char> text) -> HttpInputStream::Message {
      headers.clear();
      KJ_REQUIRE(headers.tryParse(text), "bad message");
      auto body = getEntityBody(HttpInputStreamImpl::RESPONSE, HttpMethod::GET, 0, headers);

      return { headers, kj::mv(body) };
    });
  }

  // ---------------------------------------------------------------------------
  // Stream locking: While an entity-body is being read, the body stream "locks" the underlying
  // HTTP stream. Once the entity-body is complete, we can read the next pipelined message.

  void finishRead() {
    // Called when entire request has been read.

    KJ_REQUIRE_NONNULL(onMessageDone)->fulfill();
    onMessageDone = nullptr;
    --pendingMessageCount;
  }

  void abortRead() {
    // Called when a body input stream was destroyed without reading to the end.

    KJ_REQUIRE_NONNULL(onMessageDone)->reject(KJ_EXCEPTION(FAILED,
        "application did not finish reading previous HTTP response body",
        "can't read next pipelined request/response"));
    onMessageDone = nullptr;
    broken = true;
  }

  // ---------------------------------------------------------------------------

  kj::Promise<bool> awaitNextMessage() override {
    // Waits until more data is available, but doesn't consume it. Returns false on EOF.
    //
    // Used on the server after a request is handled, to check for pipelined requests.
    //
    // Used on the client to detect when idle connections are closed from the server end. (In this
    // case, the promise always returns false or is canceled.)

    if (resumingRequest != nullptr) {
      // We're resuming a request, so report that we have a message.
      return true;
    }

    if (onMessageDone != nullptr) {
      // We're still working on reading the previous body.
      auto fork = messageReadQueue.fork();
      messageReadQueue = fork.addBranch();
      return fork.addBranch().then([this]() {
        return awaitNextMessage();
      });
    }

    snarfBufferedLineBreak();

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

  bool isCleanDrain() {
    // Returns whether we can cleanly drain the stream at this point.
    if (onMessageDone != nullptr) return false;
    snarfBufferedLineBreak();
    return !lineBreakBeforeNextHeader && leftover == nullptr;
  }

  kj::Promise<kj::ArrayPtr<char>> readMessageHeaders() {
    ++pendingMessageCount;
    auto paf = kj::newPromiseAndFulfiller<void>();

    auto promise = messageReadQueue
        .then([this,fulfiller=kj::mv(paf.fulfiller)]() mutable {
      onMessageDone = kj::mv(fulfiller);
      return readHeader(HeaderType::MESSAGE, 0, 0);
    });

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
          KJ_FAIL_REQUIRE("invalid HTTP chunk size", text, text.asBytes()) { break; }
          return value;
        }
      }

      return value;
    });
  }

  inline kj::Promise<HttpHeaders::RequestConnectOrProtocolError> readRequestHeaders() {
    KJ_IF_MAYBE(resuming, resumingRequest) {
      KJ_DEFER(resumingRequest = nullptr);
      return HttpHeaders::RequestConnectOrProtocolError(*resuming);
    }

    return readMessageHeaders().then([this](kj::ArrayPtr<char> text) {
      headers.clear();
      return headers.tryParseRequestOrConnect(text);
    });
  }

  inline kj::Promise<HttpHeaders::ResponseOrProtocolError> readResponseHeaders() {
    // Note: readResponseHeaders() could be called multiple times concurrently when pipelining
    //   requests. readMessageHeaders() will serialize these, but it's important not to mess with
    //   state (like calling headers.clear()) before said serialization has taken place.
    return readMessageHeaders().then([this](kj::ArrayPtr<char> text) {
      headers.clear();
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
      RequestOrResponse type,
      kj::OneOf<HttpMethod, HttpConnectMethod> method,
      uint statusCode,
      const kj::HttpHeaders& headers);

  struct ReleasedBuffer {
    kj::Array<byte> buffer;
    kj::ArrayPtr<byte> leftover;
  };

  ReleasedBuffer releaseBuffer() {
    return { headerBuffer.releaseAsBytes(), leftover.asBytes() };
  }

private:
  AsyncInputStream& inner;
  kj::Array<char> headerBuffer;

  size_t messageHeaderEnd = 0;
  // Position in headerBuffer where the message headers end -- further buffer space can
  // be used for chunk headers.

  kj::ArrayPtr<char> leftover;
  // Data in headerBuffer that comes immediately after the header content, if any.

  HttpHeaders headers;
  // Parsed headers, after a call to parseAwaited*().

  kj::Maybe<kj::OneOf<HttpHeaders::Request, HttpHeaders::ConnectRequest>> resumingRequest;
  // Non-null if we're resuming a SuspendedRequest.

  bool lineBreakBeforeNextHeader = false;
  // If true, the next await should expect to start with a spurious '\n' or '\r\n'. This happens
  // as a side-effect of HTTP chunked encoding, where such a newline is added to the end of each
  // chunk, for no good reason.

  bool broken = false;
  // Becomes true if the caller failed to read the whole entity-body before closing the stream.

  uint pendingMessageCount = 0;
  // Number of reads we have queued up.

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
      // Need to read more data from the underlying stream.

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

  void snarfBufferedLineBreak() {
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
  }
};

// -----------------------------------------------------------------------------

class HttpEntityBodyReader: public kj::AsyncInputStream {
public:
  HttpEntityBodyReader(HttpInputStreamImpl& inner) {
    inner.setCurrentWrapper(weakInner);
  }
  ~HttpEntityBodyReader() noexcept(false) {
    if (!finished) {
      KJ_IF_MAYBE(inner, weakInner) {
        inner->unsetCurrentWrapper(weakInner);
        inner->abortRead();
      } else {
        // Since we're in a destructor, log an error instead of throwing.
        KJ_LOG(ERROR, "HTTP body input stream outlived underlying connection", kj::getStackTrace());
      }
    }
  }

protected:
  HttpInputStreamImpl& getInner() {
    KJ_IF_MAYBE(i, weakInner) {
      return *i;
    } else if (finished) {
      // This is a bug in the implementations in this file, not the app.
      KJ_FAIL_ASSERT("bug in KJ HTTP: tried to access inner stream after it had been released");
    } else {
      KJ_FAIL_REQUIRE("HTTP body input stream outlived underlying connection");
    }
  }

  void doneReading() {
    auto& inner = getInner();
    inner.unsetCurrentWrapper(weakInner);
    finished = true;
    inner.finishRead();
  }

  inline bool alreadyDone() { return weakInner == nullptr; }

private:
  kj::Maybe<HttpInputStreamImpl&> weakInner;
  bool finished = false;
};

class HttpNullEntityReader final: public HttpEntityBodyReader {
  // Stream for an entity-body which is not present. Always returns EOF on read, but tryGetLength()
  // may indicate non-zero in the special case of a response to a HEAD request.

public:
  HttpNullEntityReader(HttpInputStreamImpl& inner, kj::Maybe<uint64_t> length)
      : HttpEntityBodyReader(inner), length(length) {
    // `length` is what to return from tryGetLength(). For a response to a HEAD request, this may
    // be non-zero.
    doneReading();
  }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return constPromise<size_t, 0>();
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
  HttpConnectionCloseEntityReader(HttpInputStreamImpl& inner)
      : HttpEntityBodyReader(inner) {}

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    if (alreadyDone()) return constPromise<size_t, 0>();

    return getInner().tryRead(buffer, minBytes, maxBytes)
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
  HttpFixedLengthEntityReader(HttpInputStreamImpl& inner, size_t length)
      : HttpEntityBodyReader(inner), length(length) {
    if (length == 0) doneReading();
  }

  Maybe<uint64_t> tryGetLength() override {
    return length;
  }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    KJ_REQUIRE(clean, "can't read more data after a previous read didn't complete");
    clean = false;
    return tryReadInternal(buffer, minBytes, maxBytes, 0);
  }

private:
  size_t length;
  bool clean = true;

  Promise<size_t> tryReadInternal(void* buffer, size_t minBytes, size_t maxBytes,
                                  size_t alreadyRead) {
    if (length == 0) {
      clean = true;
      return constPromise<size_t, 0>();
    }

    // We have to set minBytes to 1 here so that if we read any data at all, we update our
    // counter immediately, so that we still know where we are in case of cancellation.
    return getInner().tryRead(buffer, 1, kj::min(maxBytes, length))
        .then([=](size_t amount) -> kj::Promise<size_t> {
      length -= amount;
      if (length > 0) {
        // We haven't reached the end of the entity body yet.
        if (amount == 0) {
          kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED,
              "premature EOF in HTTP entity body; did not reach Content-Length"));
        } else if (amount < minBytes) {
          // We requested a minimum 1 byte above, but our own caller actually set a larger minimum
          // which has not yet been reached. Keep trying until we reach it.
          return tryReadInternal(reinterpret_cast<byte*>(buffer) + amount,
                                 minBytes - amount, maxBytes - amount, alreadyRead + amount);
        }
      } else if (length == 0) {
        doneReading();
      }
      clean = true;
      return amount + alreadyRead;
    });
  }
};

class HttpChunkedEntityReader final: public HttpEntityBodyReader {
  // Stream which reads a Transfer-Encoding: Chunked stream.

public:
  HttpChunkedEntityReader(HttpInputStreamImpl& inner)
      : HttpEntityBodyReader(inner) {}

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    KJ_REQUIRE(clean, "can't read more data after a previous read didn't complete");
    clean = false;
    return tryReadInternal(buffer, minBytes, maxBytes, 0);
  }

private:
  size_t chunkSize = 0;
  bool clean = true;

  Promise<size_t> tryReadInternal(void* buffer, size_t minBytes, size_t maxBytes,
                                  size_t alreadyRead) {
    if (alreadyDone()) {
      clean = true;
      return alreadyRead;
    } else if (chunkSize == 0) {
      // Read next chunk header.
      return getInner().readChunkHeader().then([=](uint64_t nextChunkSize) {
        if (nextChunkSize == 0) {
          doneReading();
        }

        chunkSize = nextChunkSize;
        return tryReadInternal(buffer, minBytes, maxBytes, alreadyRead);
      });
    } else {
      // Read current chunk.
      // We have to set minBytes to 1 here so that if we read any data at all, we update our
      // counter immediately, so that we still know where we are in case of cancellation.
      return getInner().tryRead(buffer, 1, kj::min(maxBytes, chunkSize))
          .then([=](size_t amount) -> kj::Promise<size_t> {
        chunkSize -= amount;
        if (amount == 0) {
          kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "premature EOF in HTTP chunk"));
        } else if (amount < minBytes) {
          // We requested a minimum 1 byte above, but our own caller actually set a larger minimum
          // which has not yet been reached. Keep trying until we reach it.
          return tryReadInternal(reinterpret_cast<byte*>(buffer) + amount,
                                 minBytes - amount, maxBytes - amount, alreadyRead + amount);
        }
        clean = true;
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
      ('a' <= first && first <= 'z') || ('A' <= first && first <= 'Z')
        ? (*actual | 0x20) == (first | 0x20) && FastCaseCmp<rest...>::apply(actual + 1)
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

kj::Own<kj::AsyncInputStream> HttpInputStreamImpl::getEntityBody(
    RequestOrResponse type,
    kj::OneOf<HttpMethod, HttpConnectMethod> method,
    uint statusCode,
    const kj::HttpHeaders& headers) {
  KJ_REQUIRE(headerBuffer.size() > 0, "Cannot get entity body after header buffer release.");

  auto isHeadRequest = method.tryGet<HttpMethod>().map([](auto& m) {
    return m == HttpMethod::HEAD;
  }).orDefault(false);

  auto isConnectRequest = method.is<HttpConnectMethod>();

  // Rules to determine how HTTP entity-body is delimited:
  //   https://tools.ietf.org/html/rfc7230#section-3.3.3
  // #1
  if (type == RESPONSE) {
    if (isHeadRequest) {
      // Body elided.
      kj::Maybe<uint64_t> length;
      KJ_IF_MAYBE(cl, headers.get(HttpHeaderId::CONTENT_LENGTH)) {
        length = strtoull(cl->cStr(), nullptr, 10);
      } else if (headers.get(HttpHeaderId::TRANSFER_ENCODING) == nullptr) {
        // HACK: Neither Content-Length nor Transfer-Encoding header in response to HEAD
        //   request. Propagate this fact with a 0 expected body length.
        length = uint64_t(0);
      }
      return kj::heap<HttpNullEntityReader>(*this, length);
    } else if (isConnectRequest && statusCode >= 200 && statusCode < 300) {
      KJ_FAIL_ASSERT("a CONNECT response with a 2xx status does not have an entity body to get");
    } else if (statusCode == 204 || statusCode == 304) {
      // No body.
      return kj::heap<HttpNullEntityReader>(*this, uint64_t(0));
    }
  }

  // For CONNECT requests messages, we let the rest of the logic play out.
  // We already check before here to ensure that Transfer-Encoding and
  // Content-Length headers are not present in which case the code below
  // does the right thing.

  // #3
  KJ_IF_MAYBE(te, headers.get(HttpHeaderId::TRANSFER_ENCODING)) {
    // TODO(someday): Support pluggable transfer encodings? Or at least gzip?
    // TODO(someday): Support stacked transfer encodings, e.g. "gzip, chunked".

    // NOTE: #3¶3 is ambiguous about what should happen if Transfer-Encoding and Content-Length are
    //   both present. It says that Transfer-Encoding takes precedence, but also that the request
    //   "ought to be handled as an error", and that proxies "MUST" drop the Content-Length before
    //   forwarding. We ignore the vague "ought to" part and implement the other two. (The
    //   dropping of Content-Length will happen naturally if/when the message is sent back out to
    //   the network.)
    if (fastCaseCmp<'c','h','u','n','k','e','d'>(te->cStr())) {
      // #3¶1
      return kj::heap<HttpChunkedEntityReader>(*this);
    } else if (fastCaseCmp<'i','d','e','n','t','i','t','y'>(te->cStr())) {
      // #3¶2
      KJ_REQUIRE(type != REQUEST, "request body cannot have Transfer-Encoding other than chunked");
      return kj::heap<HttpConnectionCloseEntityReader>(*this);
    }

    KJ_FAIL_REQUIRE("unknown transfer encoding", *te) { break; };
  }

  // #4 and #5
  KJ_IF_MAYBE(cl, headers.get(HttpHeaderId::CONTENT_LENGTH)) {
    // NOTE: By spec, multiple Content-Length values are allowed as long as they are the same, e.g.
    //   "Content-Length: 5, 5, 5". Hopefully no one actually does that...
    char* end;
    uint64_t length = strtoull(cl->cStr(), &end, 10);
    if (end > cl->begin() && *end == '\0') {
      // #5
      return kj::heap<HttpFixedLengthEntityReader>(*this, length);
    } else {
      // #4 (bad content-length)
      KJ_FAIL_REQUIRE("invalid Content-Length header value", *cl);
    }
  }

  // #6
  if (type == REQUEST) {
    // Lack of a Content-Length or Transfer-Encoding means no body for requests.
    return kj::heap<HttpNullEntityReader>(*this, uint64_t(0));
  }

  // RFC 2616 permitted "multipart/byteranges" responses to be self-delimiting, but this was
  // mercifully removed in RFC 7230, and new exceptions of this type are disallowed:
  //   https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.4
  //   https://tools.ietf.org/html/rfc7230#page-81
  // To be extra-safe, we'll reject a multipart/byteranges response that lacks transfer-encoding
  // and content-length.
  KJ_IF_MAYBE(type, headers.get(HttpHeaderId::CONTENT_TYPE)) {
    if (type->startsWith("multipart/byteranges")) {
      KJ_FAIL_REQUIRE(
          "refusing to handle multipart/byteranges response without transfer-encoding nor "
          "content-length due to ambiguity between RFC 2616 vs RFC 7230.");
    }
  }

  // #7
  return kj::heap<HttpConnectionCloseEntityReader>(*this);
}

}  // namespace

kj::Own<HttpInputStream> newHttpInputStream(
    kj::AsyncInputStream& input, const HttpHeaderTable& table) {
  return kj::heap<HttpInputStreamImpl>(input, table);
}

// =======================================================================================

namespace {

class HttpOutputStream: public WrappableStreamMixin<HttpOutputStream> {
public:
  HttpOutputStream(AsyncOutputStream& inner): inner(inner) {}

  bool isInBody() {
    return inBody;
  }

  bool canReuse() {
    return !inBody && !broken && !writeInProgress;
  }

  bool canWriteBodyData() {
    return !writeInProgress && inBody;
  }

  bool isBroken() {
    return broken;
  }

  void writeHeaders(String content) {
    // Writes some header content and begins a new entity body.

    KJ_REQUIRE(!writeInProgress, "concurrent write()s not allowed") { return; }
    KJ_REQUIRE(!inBody, "previous HTTP message body incomplete; can't write more messages");
    inBody = true;

    queueWrite(kj::mv(content));
  }

  void writeBodyData(kj::String content) {
    KJ_REQUIRE(!writeInProgress, "concurrent write()s not allowed") { return; }
    KJ_REQUIRE(inBody) { return; }

    queueWrite(kj::mv(content));
  }

  kj::Promise<void> writeBodyData(const void* buffer, size_t size) {
    KJ_REQUIRE(!writeInProgress, "concurrent write()s not allowed") { return kj::READY_NOW; }
    KJ_REQUIRE(inBody) { return kj::READY_NOW; }

    writeInProgress = true;
    auto fork = writeQueue.fork();
    writeQueue = fork.addBranch();

    return fork.addBranch().then([this,buffer,size]() {
      return inner.write(buffer, size);
    }).then([this]() {
      writeInProgress = false;
    });
  }

  kj::Promise<void> writeBodyData(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) {
    KJ_REQUIRE(!writeInProgress, "concurrent write()s not allowed") { return kj::READY_NOW; }
    KJ_REQUIRE(inBody) { return kj::READY_NOW; }

    writeInProgress = true;
    auto fork = writeQueue.fork();
    writeQueue = fork.addBranch();

    return fork.addBranch().then([this,pieces]() {
      return inner.write(pieces);
    }).then([this]() {
      writeInProgress = false;
    });
  }

  Promise<uint64_t> pumpBodyFrom(AsyncInputStream& input, uint64_t amount) {
    KJ_REQUIRE(!writeInProgress, "concurrent write()s not allowed") { return uint64_t(0); }
    KJ_REQUIRE(inBody) { return uint64_t(0); }

    writeInProgress = true;
    auto fork = writeQueue.fork();
    writeQueue = fork.addBranch();

    return fork.addBranch().then([this,&input,amount]() {
      return input.pumpTo(inner, amount);
    }).then([this](uint64_t actual) {
      writeInProgress = false;
      return actual;
    });
  }

  void finishBody() {
    // Called when entire body was written.

    KJ_REQUIRE(inBody) { return; }
    inBody = false;

    if (writeInProgress) {
      // It looks like the last write never completed -- possibly because it was canceled or threw
      // an exception. We must treat this equivalent to abortBody().
      broken = true;

      // Cancel any writes that are still queued.
      writeQueue = KJ_EXCEPTION(FAILED,
          "previous HTTP message body incomplete; can't write more messages");
    }
  }

  void abortBody() {
    // Called if the application failed to write all expected body bytes.
    KJ_REQUIRE(inBody) { return; }
    inBody = false;
    broken = true;

    // Cancel any writes that are still queued.
    writeQueue = KJ_EXCEPTION(FAILED,
        "previous HTTP message body incomplete; can't write more messages");
  }

  kj::Promise<void> flush() {
    auto fork = writeQueue.fork();
    writeQueue = fork.addBranch();
    return fork.addBranch();
  }

  Promise<void> whenWriteDisconnected() {
    return inner.whenWriteDisconnected();
  }

  bool isWriteInProgress() { return writeInProgress; }

private:
  AsyncOutputStream& inner;
  kj::Promise<void> writeQueue = kj::READY_NOW;
  bool inBody = false;
  bool broken = false;

  bool writeInProgress = false;
  // True if a write method has been called and has not completed successfully. In the case that
  // a write throws an exception or is canceled, this remains true forever. In these cases, the
  // underlying stream is in an inconsistent state and cannot be reused.

  void queueWrite(kj::String content) {
    // We only use queueWrite() in cases where we can take ownership of the write buffer, and where
    // it is convenient if we can return `void` rather than a promise.  In particular, this is used
    // to write headers and chunk boundaries. Writes of application data do not go into
    // `writeQueue` because this would prevent cancellation. Instead, they wait until `writeQueue`
    // is empty, then they make the write directly, using `writeInProgress` to detect and block
    // concurrent writes.

    writeQueue = writeQueue.then([this,content=kj::mv(content)]() mutable {
      auto promise = inner.write(content.begin(), content.size());
      return promise.attach(kj::mv(content));
    });
  }
};

class HttpEntityBodyWriter: public kj::AsyncOutputStream {
public:
  HttpEntityBodyWriter(HttpOutputStream& inner) {
    inner.setCurrentWrapper(weakInner);
  }
  ~HttpEntityBodyWriter() noexcept(false) {
    if (!finished) {
      KJ_IF_MAYBE(inner, weakInner) {
        inner->unsetCurrentWrapper(weakInner);
        inner->abortBody();
      } else {
        // Since we're in a destructor, log an error instead of throwing.
        KJ_LOG(ERROR, "HTTP body output stream outlived underlying connection",
            kj::getStackTrace());
      }
    }
  }

protected:
  HttpOutputStream& getInner() {
    KJ_IF_MAYBE(i, weakInner) {
      return *i;
    } else if (finished) {
      // This is a bug in the implementations in this file, not the app.
      KJ_FAIL_ASSERT("bug in KJ HTTP: tried to access inner stream after it had been released");
    } else {
      KJ_FAIL_REQUIRE("HTTP body output stream outlived underlying connection");
    }
  }

  void doneWriting() {
    auto& inner = getInner();
    inner.unsetCurrentWrapper(weakInner);
    finished = true;
    inner.finishBody();
  }

  inline bool alreadyDone() { return weakInner == nullptr; }

private:
  kj::Maybe<HttpOutputStream&> weakInner;
  bool finished = false;
};

class HttpNullEntityWriter final: public kj::AsyncOutputStream {
  // Does not inherit HttpEntityBodyWriter because it doesn't actually write anything.
public:
  Promise<void> write(const void* buffer, size_t size) override {
    return KJ_EXCEPTION(FAILED, "HTTP message has no entity-body; can't write()");
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return KJ_EXCEPTION(FAILED, "HTTP message has no entity-body; can't write()");
  }
  Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }
};

class HttpDiscardingEntityWriter final: public kj::AsyncOutputStream {
  // Does not inherit HttpEntityBodyWriter because it doesn't actually write anything.
public:
  Promise<void> write(const void* buffer, size_t size) override {
    return kj::READY_NOW;
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return kj::READY_NOW;
  }
  Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }
};

class HttpFixedLengthEntityWriter final: public HttpEntityBodyWriter {
public:
  HttpFixedLengthEntityWriter(HttpOutputStream& inner, uint64_t length)
      : HttpEntityBodyWriter(inner), length(length) {
    if (length == 0) doneWriting();
  }

  Promise<void> write(const void* buffer, size_t size) override {
    if (size == 0) return kj::READY_NOW;
    KJ_REQUIRE(size <= length, "overwrote Content-Length");
    length -= size;

    return maybeFinishAfter(getInner().writeBodyData(buffer, size));
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    uint64_t size = 0;
    for (auto& piece: pieces) size += piece.size();

    if (size == 0) return kj::READY_NOW;
    KJ_REQUIRE(size <= length, "overwrote Content-Length");
    length -= size;

    return maybeFinishAfter(getInner().writeBodyData(pieces));
  }

  Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
    if (amount == 0) return constPromise<uint64_t, 0>();

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
        : getInner().pumpBodyFrom(input, amount).then([this,amount](uint64_t actual) {
      // Adjust for bytes not written.
      length += amount - actual;
      if (length == 0) doneWriting();
      return actual;
    });

    if (overshot) {
      promise = promise.then([amount,&input](uint64_t actual) -> kj::Promise<uint64_t> {
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

  Promise<void> whenWriteDisconnected() override {
    return getInner().whenWriteDisconnected();
  }

private:
  uint64_t length;

  kj::Promise<void> maybeFinishAfter(kj::Promise<void> promise) {
    if (length == 0) {
      return promise.then([this]() { doneWriting(); });
    } else {
      return kj::mv(promise);
    }
  }
};

class HttpChunkedEntityWriter final: public HttpEntityBodyWriter {
public:
  HttpChunkedEntityWriter(HttpOutputStream& inner)
      : HttpEntityBodyWriter(inner) {}
  ~HttpChunkedEntityWriter() noexcept(false) {
    if (!alreadyDone()) {
      auto& inner = getInner();
      if (inner.canWriteBodyData()) {
        inner.writeBodyData(kj::str("0\r\n\r\n"));
        doneWriting();
      }
    }
  }

  Promise<void> write(const void* buffer, size_t size) override {
    if (size == 0) return kj::READY_NOW;  // can't encode zero-size chunk since it indicates EOF.

    auto header = kj::str(kj::hex(size), "\r\n");
    auto parts = kj::heapArray<ArrayPtr<const byte>>(3);
    parts[0] = header.asBytes();
    parts[1] = kj::arrayPtr(reinterpret_cast<const byte*>(buffer), size);
    parts[2] = kj::StringPtr("\r\n").asBytes();

    auto promise = getInner().writeBodyData(parts.asPtr());
    return promise.attach(kj::mv(header), kj::mv(parts));
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    uint64_t size = 0;
    for (auto& piece: pieces) size += piece.size();

    if (size == 0) return kj::READY_NOW;  // can't encode zero-size chunk since it indicates EOF.

    auto header = kj::str(kj::hex(size), "\r\n");
    auto partsBuilder = kj::heapArrayBuilder<ArrayPtr<const byte>>(pieces.size() + 2);
    partsBuilder.add(header.asBytes());
    for (auto& piece: pieces) {
      partsBuilder.add(piece);
    }
    partsBuilder.add(kj::StringPtr("\r\n").asBytes());

    auto parts = partsBuilder.finish();
    auto promise = getInner().writeBodyData(parts.asPtr());
    return promise.attach(kj::mv(header), kj::mv(parts));
  }

  Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
    KJ_IF_MAYBE(l, input.tryGetLength()) {
      // Hey, we know exactly how large the input is, so we can write just one chunk.

      uint64_t length = kj::min(amount, *l);
      auto& inner = getInner();
      inner.writeBodyData(kj::str(kj::hex(length), "\r\n"));
      return inner.pumpBodyFrom(input, length)
          .then([this,length](uint64_t actual) {
        auto& inner = getInner();
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

  Promise<void> whenWriteDisconnected() override {
    return getInner().whenWriteDisconnected();
  }
};

// =======================================================================================

class WebSocketImpl final: public WebSocket, private WebSocketErrorHandler {
public:
  WebSocketImpl(kj::Own<kj::AsyncIoStream> stream,
                kj::Maybe<EntropySource&> maskKeyGenerator,
                kj::Maybe<CompressionParameters> compressionConfigParam = nullptr,
                kj::Maybe<WebSocketErrorHandler&> errorHandler = nullptr,
                kj::Array<byte> buffer = kj::heapArray<byte>(4096),
                kj::ArrayPtr<byte> leftover = nullptr,
                kj::Maybe<kj::Promise<void>> waitBeforeSend = nullptr)
      : stream(kj::mv(stream)), maskKeyGenerator(maskKeyGenerator),
        compressionConfig(kj::mv(compressionConfigParam)),
        errorHandler(errorHandler.orDefault(*this)),
        sendingPong(kj::mv(waitBeforeSend)),
        recvBuffer(kj::mv(buffer)), recvData(leftover) {
#if KJ_HAS_ZLIB
    KJ_IF_MAYBE(config, compressionConfig) {
      compressionContext.emplace(ZlibContext::Mode::COMPRESS, *config);
      decompressionContext.emplace(ZlibContext::Mode::DECOMPRESS, *config);
    }
#else
    KJ_REQUIRE(compressionConfig == nullptr,
        "WebSocket compression is only supported if KJ is compiled with Zlib.");
#endif // KJ_HAS_ZLIB
  }

  kj::Promise<void> send(kj::ArrayPtr<const byte> message) override {
    return sendImpl(OPCODE_BINARY, message);
  }

  kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
    return sendImpl(OPCODE_TEXT, message.asBytes());
  }

  kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
    kj::Array<byte> payload;
    if (code == 1005) {
      KJ_REQUIRE(reason.size() == 0, "WebSocket close code 1005 cannot have a reason");

      // code 1005 -- leave payload empty
    } else {
      payload = heapArray<byte>(reason.size() + 2);
      payload[0] = code >> 8;
      payload[1] = code;
      memcpy(payload.begin() + 2, reason.begin(), reason.size());
    }

    auto promise = sendImpl(OPCODE_CLOSE, payload);
    return promise.attach(kj::mv(payload));
  }

  kj::Promise<void> disconnect() override {
    KJ_REQUIRE(!currentlySending, "another message send is already in progress");

    KJ_IF_MAYBE(p, sendingPong) {
      // We recently sent a pong, make sure it's finished before proceeding.
      currentlySending = true;
      auto promise = p->then([this]() {
        currentlySending = false;
        return disconnect();
      });
      sendingPong = nullptr;
      return promise;
    }

    disconnected = true;

    stream->shutdownWrite();
    return kj::READY_NOW;
  }

  void abort() override {
    queuedPong = nullptr;
    sendingPong = nullptr;
    disconnected = true;
    stream->abortRead();
    stream->shutdownWrite();
  }

  kj::Promise<void> whenAborted() override {
    return stream->whenWriteDisconnected();
  }

  kj::Promise<Message> receive(size_t maxSize) override {
    size_t headerSize = Header::headerSize(recvData.begin(), recvData.size());

    if (headerSize > recvData.size()) {
      if (recvData.begin() != recvBuffer.begin()) {
        // Move existing data to front of buffer.
        if (recvData.size() > 0) {
          memmove(recvBuffer.begin(), recvData.begin(), recvData.size());
        }
        recvData = recvBuffer.slice(0, recvData.size());
      }

      return stream->tryRead(recvData.end(), 1, recvBuffer.end() - recvData.end())
          .then([this,maxSize](size_t actual) -> kj::Promise<Message> {
        receivedBytes += actual;
        if (actual == 0) {
          if (recvData.size() > 0) {
            return KJ_EXCEPTION(DISCONNECTED, "WebSocket EOF in frame header");
          } else {
            // It's incorrect for the WebSocket to disconnect without sending `Close`.
            return KJ_EXCEPTION(DISCONNECTED,
                "WebSocket disconnected between frames without sending `Close`.");
          }
        }

        recvData = recvBuffer.slice(0, recvData.size() + actual);
        return receive(maxSize);
      });
    }

    auto& recvHeader = *reinterpret_cast<Header*>(recvData.begin());
    if (recvHeader.hasRsv2or3()) {
      return errorHandler.handleWebSocketProtocolError({
        1002, "Received frame had RSV bits 2 or 3 set",
      });
    }

    recvData = recvData.slice(headerSize, recvData.size());

    size_t payloadLen = recvHeader.getPayloadLen();
    if (payloadLen > maxSize) {
      return errorHandler.handleWebSocketProtocolError({
        1009, kj::str("Message is too large: ", payloadLen, " > ", maxSize)
      });
    }

    auto opcode = recvHeader.getOpcode();
    bool isData = opcode < OPCODE_FIRST_CONTROL;
    if (opcode == OPCODE_CONTINUATION) {
      if (fragments.empty()) {
        return errorHandler.handleWebSocketProtocolError({
          1002, "Unexpected continuation frame"
        });
      }

      opcode = fragmentOpcode;
    } else if (isData) {
      if (!fragments.empty()) {
        return errorHandler.handleWebSocketProtocolError({
          1002, "Missing continuation frame"
        });
      }
    }

    bool isFin = recvHeader.isFin();
    bool isCompressed = false;

    kj::Array<byte> message;           // space to allocate
    byte* payloadTarget;               // location into which to read payload (size is payloadLen)
    kj::Maybe<size_t> originalMaxSize; // maxSize from first `receive()` call
    if (isFin) {
      size_t amountToAllocate;
      if (recvHeader.isCompressed() || fragmentCompressed) {
        // Add 4 since we append 0x00 0x00 0xFF 0xFF to the tail of the payload.
        // See: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.2
        amountToAllocate = payloadLen + 4;
        isCompressed = true;
      } else {
        // Add space for NUL terminator when allocating text message.
        amountToAllocate = payloadLen + (opcode == OPCODE_TEXT && isFin);
      }

      if (isData && !fragments.empty()) {
        // Final frame of a fragmented message. Gather the fragments.
        size_t offset = 0;
        for (auto& fragment: fragments) offset += fragment.size();
        message = kj::heapArray<byte>(offset + amountToAllocate);
        originalMaxSize = offset + maxSize; // gives us back the original maximum message size.

        offset = 0;
        for (auto& fragment: fragments) {
          memcpy(message.begin() + offset, fragment.begin(), fragment.size());
          offset += fragment.size();
        }
        payloadTarget = message.begin() + offset;

        fragments.clear();
        fragmentOpcode = 0;
        fragmentCompressed = false;
      } else {
        // Single-frame message.
        message = kj::heapArray<byte>(amountToAllocate);
        originalMaxSize = maxSize; // gives us back the original maximum message size.
        payloadTarget = message.begin();
      }
    } else {
      // Fragmented message, and this isn't the final fragment.
      if (!isData) {
        return errorHandler.handleWebSocketProtocolError({
          1002, "Received fragmented control frame"
        });
      }

      message = kj::heapArray<byte>(payloadLen);
      payloadTarget = message.begin();
      if (fragments.empty()) {
        // This is the first fragment, so set the opcode.
        fragmentOpcode = opcode;
        fragmentCompressed = recvHeader.isCompressed();
      }
    }

    Mask mask = recvHeader.getMask();

    auto handleMessage =
        [this,opcode,payloadTarget,payloadLen,mask,isFin,maxSize,originalMaxSize,
         isCompressed,message=kj::mv(message)]() mutable
        -> kj::Promise<Message> {
      if (!mask.isZero()) {
        mask.apply(kj::arrayPtr(payloadTarget, payloadLen));
      }

      if (!isFin) {
        // Add fragment to the list and loop.
        auto newMax = maxSize - message.size();
        fragments.add(kj::mv(message));
        return receive(newMax);
      }

      // Provide a reasonable error if a compressed frame is received without compression enabled.
      if (isCompressed && compressionConfig == nullptr) {
        return errorHandler.handleWebSocketProtocolError({
          1002, kj::str(
              "Received a WebSocket frame whose compression bit was set, but the compression "
              "extension was not negotiated for this connection.")
        });
      }

      switch (opcode) {
        case OPCODE_CONTINUATION:
          // Shouldn't get here; handled above.
          KJ_UNREACHABLE;
        case OPCODE_TEXT:
#if KJ_HAS_ZLIB
          if (isCompressed) {
            auto& config = KJ_ASSERT_NONNULL(compressionConfig);
            auto& decompressor = KJ_ASSERT_NONNULL(decompressionContext);
            KJ_ASSERT(message.size() >= 4);
            auto tail = message.slice(message.size() - 4, message.size());
            // Note that we added an additional 4 bytes to `message`s capacity to account for these
            // extra bytes. See `amountToAllocate` in the if(recvHeader.isCompressed()) block above.
            const byte tailBytes[] = {0x00, 0x00, 0xFF, 0xFF};
            memcpy(tail.begin(), tailBytes, sizeof(tailBytes));
            // We have to append 0x00 0x00 0xFF 0xFF to the message before inflating.
            // See: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.2
            if (config.inboundNoContextTakeover) {
              // We must reset context on each message.
              decompressor.reset();
            }
            bool addNullTerminator = true;
            // We want to add the null terminator when receiving a TEXT message.
            auto decompressed = decompressor.processMessage(message, originalMaxSize,
                addNullTerminator);
            return Message(kj::String(decompressed.releaseAsChars()));
          }
#endif // KJ_HAS_ZLIB
          message.back() = '\0';
          return Message(kj::String(message.releaseAsChars()));
        case OPCODE_BINARY:
#if KJ_HAS_ZLIB
          if (isCompressed) {
            auto& config = KJ_ASSERT_NONNULL(compressionConfig);
            auto& decompressor = KJ_ASSERT_NONNULL(decompressionContext);
            KJ_ASSERT(message.size() >= 4);
            auto tail = message.slice(message.size() - 4, message.size());
            // Note that we added an additional 4 bytes to `message`s capacity to account for these
            // extra bytes. See `amountToAllocate` in the if(recvHeader.isCompressed()) block above.
            const byte tailBytes[] = {0x00, 0x00, 0xFF, 0xFF};
            memcpy(tail.begin(), tailBytes, sizeof(tailBytes));
            // We have to append 0x00 0x00 0xFF 0xFF to the message before inflating.
            // See: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.2
            if (config.inboundNoContextTakeover) {
              // We must reset context on each message.
              decompressor.reset();
            }
            auto decompressed = decompressor.processMessage(message, originalMaxSize);
            return Message(decompressed.releaseAsBytes());
          }
#endif // KJ_HAS_ZLIB
          return Message(message.releaseAsBytes());
        case OPCODE_CLOSE:
          if (message.size() < 2) {
            return Message(Close { 1005, nullptr });
          } else {
            uint16_t status = (static_cast<uint16_t>(message[0]) << 8)
                            | (static_cast<uint16_t>(message[1])     );
            return Message(Close {
              status, kj::heapString(message.slice(2, message.size()).asChars())
            });
          }
        case OPCODE_PING:
          // Send back a pong.
          queuePong(kj::mv(message));
          return receive(maxSize);
        case OPCODE_PONG:
          // Unsolicited pong. Ignore.
          return receive(maxSize);
        default:
          return errorHandler.handleWebSocketProtocolError({
            1002, kj::str("Unknown opcode ", opcode)
          });
      }
    };

    if (payloadLen <= recvData.size()) {
      // All data already received.
      memcpy(payloadTarget, recvData.begin(), payloadLen);
      recvData = recvData.slice(payloadLen, recvData.size());
      return handleMessage();
    } else {
      // Need to read more data.
      memcpy(payloadTarget, recvData.begin(), recvData.size());
      size_t remaining = payloadLen - recvData.size();
      auto promise = stream->tryRead(payloadTarget + recvData.size(), remaining, remaining)
          .then([this, remaining](size_t amount) {
        receivedBytes += amount;
        if (amount < remaining) {
          kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "WebSocket EOF in message"));
        }
      });
      recvData = nullptr;
      return promise.then(kj::mv(handleMessage));
    }
  }

  kj::Maybe<kj::Promise<void>> tryPumpFrom(WebSocket& other) override {
    KJ_IF_MAYBE(optOther, kj::dynamicDowncastIfAvailable<WebSocketImpl>(other)) {
      // Both WebSockets are raw WebSockets, so we can pump the streams directly rather than read
      // whole messages.

      if ((maskKeyGenerator == nullptr) == (optOther->maskKeyGenerator == nullptr)) {
        // Oops, it appears that we either believe we are the client side of both sockets, or we
        // are the server side of both sockets. Since clients must "mask" their outgoing frames but
        // servers must *not* do so, we can't direct-pump. Sad.
        return nullptr;
      }

      KJ_IF_MAYBE(config, compressionConfig) {
        KJ_IF_MAYBE(otherConfig, optOther->compressionConfig) {
          if (config->outboundMaxWindowBits != otherConfig->inboundMaxWindowBits ||
              config->inboundMaxWindowBits != otherConfig->outboundMaxWindowBits ||
              config->inboundNoContextTakeover!= otherConfig->outboundNoContextTakeover ||
              config->outboundNoContextTakeover!= otherConfig->inboundNoContextTakeover) {
            // Compression configurations differ.
            return nullptr;
          }
        } else {
          // Only one websocket uses compression.
          return nullptr;
        }
      } else {
        if (optOther->compressionConfig != nullptr) {
          // Only one websocket uses compression.
          return nullptr;
        }
      }
      // Both websockets use compatible compression configurations so we can pump directly.

      // Check same error conditions as with sendImpl().
      KJ_REQUIRE(!disconnected, "WebSocket can't send after disconnect()");
      KJ_REQUIRE(!currentlySending, "another message send is already in progress");
      currentlySending = true;

      // If the application chooses to pump messages out, but receives incoming messages normally
      // with `receive()`, then we will receive pings and attempt to send pongs. But we can't
      // safely insert a pong in the middle of a pumped stream. We kind of don't have a choice
      // except to drop them on the floor, which is what will happen if we set `hasSentClose` true.
      // Hopefully most apps that set up a pump do so in both directions at once, and so pings will
      // flow through and pongs will flow back.
      hasSentClose = true;

      return optOther->optimizedPumpTo(*this);
    }

    return nullptr;
  }

  uint64_t sentByteCount() override { return sentBytes; }

  uint64_t receivedByteCount() override { return receivedBytes; }

  kj::Maybe<kj::String> getPreferredExtensions(ExtensionsContext ctx) override {
    if (maskKeyGenerator == nullptr) {
      // `this` is the server side of a websocket.
      if (ctx == ExtensionsContext::REQUEST) {
        // The other WebSocket is (going to be) the client side of a WebSocket, i.e. this is a
        // proxying pass-through scenario. Optimization is possible. Confusingly, we have to use
        // generateExtensionResponse() (even though we're generating headers to be passed in a
        // request) because this is the function that correctly maps our config's inbound/outbound
        // to client/server.
        KJ_IF_MAYBE(c, compressionConfig) {
          return _::generateExtensionResponse(*c);
        } else {
          return kj::String(nullptr);  // recommend no compression
        }
      } else {
        // We're apparently arranging to pump from the server side of one WebSocket to the server
        // side of another; i.e., we are a server, we have two clients, and we're trying to pump
        // between them. We cannot optimize this case, because the masking requirements are
        // different for client->server vs. server->client messages. Since we have to parse out
        // the messages anyway there's no point in trying to match extensions, so return null.
        return nullptr;
      }
    } else {
      // `this` is the client side of a websocket.
      if (ctx == ExtensionsContext::RESPONSE) {
        // The other WebSocket is (going to be) the server side of a WebSocket, i.e. this is a
        // proxying pass-through scenario. Optimization is possible. Confusingly, we have to use
        // generateExtensionRequest() (even though we're generating headers to be passed in a
        // response) because this is the function that correctly maps our config's inbound/outbound
        // to server/client.
        KJ_IF_MAYBE(c, compressionConfig) {
          CompressionParameters arr[1]{*c};
          return _::generateExtensionRequest(arr);
        } else {
          return kj::String(nullptr);  // recommend no compression
        }
      } else {
        // We're apparently arranging to pump from the client side of one WebSocket to the client
        // side of another; i.e., we are a client, we are connected to two servers, and we're
        // trying to pump between them. We cannot optimize this case, because the masking
        // requirements are different for client->server vs. server->client messages. Since we have
        // to parse out the messages anyway there's no point in trying to match extensions, so
        // return null.
        return nullptr;
      }
    }
  }

private:
  class Mask {
  public:
    Mask(): maskBytes { 0, 0, 0, 0 } {}
    Mask(const byte* ptr) { memcpy(maskBytes, ptr, 4); }

    Mask(kj::Maybe<EntropySource&> generator) {
      KJ_IF_MAYBE(g, generator) {
        g->generate(maskBytes);
      } else {
        memset(maskBytes, 0, 4);
      }
    }

    void apply(kj::ArrayPtr<byte> bytes) const {
      apply(bytes.begin(), bytes.size());
    }

    void copyTo(byte* output) const {
      memcpy(output, maskBytes, 4);
    }

    bool isZero() const {
      return (maskBytes[0] | maskBytes[1] | maskBytes[2] | maskBytes[3]) == 0;
    }

  private:
    byte maskBytes[4];

    void apply(byte* __restrict__ bytes, size_t size) const {
      for (size_t i = 0; i < size; i++) {
        bytes[i] ^= maskBytes[i % 4];
      }
    }
  };

  class Header {
  public:
    kj::ArrayPtr<const byte> compose(bool fin, bool compressed, byte opcode, uint64_t payloadLen,
        Mask mask) {
      bytes[0] = (fin ? FIN_MASK : 0) | (compressed ? RSV1_MASK : 0) | opcode;
      // Note that we can only set the compressed bit on DATA frames.
      bool hasMask = !mask.isZero();

      size_t fill;

      if (payloadLen < 126) {
        bytes[1] = (hasMask ? USE_MASK_MASK : 0) | payloadLen;
        if (hasMask) {
          mask.copyTo(bytes + 2);
          fill = 6;
        } else {
          fill = 2;
        }
      } else if (payloadLen < 65536) {
        bytes[1] = (hasMask ? USE_MASK_MASK : 0) | 126;
        bytes[2] = static_cast<byte>(payloadLen >> 8);
        bytes[3] = static_cast<byte>(payloadLen     );
        if (hasMask) {
          mask.copyTo(bytes + 4);
          fill = 8;
        } else {
          fill = 4;
        }
      } else {
        bytes[1] = (hasMask ? USE_MASK_MASK : 0) | 127;
        bytes[2] = static_cast<byte>(payloadLen >> 56);
        bytes[3] = static_cast<byte>(payloadLen >> 48);
        bytes[4] = static_cast<byte>(payloadLen >> 40);
        bytes[5] = static_cast<byte>(payloadLen >> 42);
        bytes[6] = static_cast<byte>(payloadLen >> 24);
        bytes[7] = static_cast<byte>(payloadLen >> 16);
        bytes[8] = static_cast<byte>(payloadLen >>  8);
        bytes[9] = static_cast<byte>(payloadLen      );
        if (hasMask) {
          mask.copyTo(bytes + 10);
          fill = 14;
        } else {
          fill = 10;
        }
      }

      return arrayPtr(bytes, fill);
    }

    bool isFin() const {
      return bytes[0] & FIN_MASK;
    }

    bool isCompressed() const {
      return bytes[0] & RSV1_MASK;
    }

    bool hasRsv2or3() const {
      return bytes[0] & RSV2_3_MASK;
    }

    byte getOpcode() const {
      return bytes[0] & OPCODE_MASK;
    }

    uint64_t getPayloadLen() const {
      byte payloadLen = bytes[1] & PAYLOAD_LEN_MASK;
      if (payloadLen == 127) {
        return (static_cast<uint64_t>(bytes[2]) << 56)
             | (static_cast<uint64_t>(bytes[3]) << 48)
             | (static_cast<uint64_t>(bytes[4]) << 40)
             | (static_cast<uint64_t>(bytes[5]) << 32)
             | (static_cast<uint64_t>(bytes[6]) << 24)
             | (static_cast<uint64_t>(bytes[7]) << 16)
             | (static_cast<uint64_t>(bytes[8]) <<  8)
             | (static_cast<uint64_t>(bytes[9])      );
      } else if (payloadLen == 126) {
        return (static_cast<uint64_t>(bytes[2]) <<  8)
             | (static_cast<uint64_t>(bytes[3])      );
      } else {
        return payloadLen;
      }
    }

    Mask getMask() const {
      if (bytes[1] & USE_MASK_MASK) {
        byte payloadLen = bytes[1] & PAYLOAD_LEN_MASK;
        if (payloadLen == 127) {
          return Mask(bytes + 10);
        } else if (payloadLen == 126) {
          return Mask(bytes + 4);
        } else {
          return Mask(bytes + 2);
        }
      } else {
        return Mask();
      }
    }

    static size_t headerSize(byte const* bytes, size_t sizeSoFar) {
      if (sizeSoFar < 2) return 2;

      size_t required = 2;

      if (bytes[1] & USE_MASK_MASK) {
        required += 4;
      }

      byte payloadLen = bytes[1] & PAYLOAD_LEN_MASK;
      if (payloadLen == 127) {
        required += 8;
      } else if (payloadLen == 126) {
        required += 2;
      }

      return required;
    }

  private:
    byte bytes[14];

    static constexpr byte FIN_MASK = 0x80;
    static constexpr byte RSV2_3_MASK = 0x30;
    static constexpr byte RSV1_MASK = 0x40;
    static constexpr byte OPCODE_MASK = 0x0f;

    static constexpr byte USE_MASK_MASK = 0x80;
    static constexpr byte PAYLOAD_LEN_MASK = 0x7f;
  };

#if KJ_HAS_ZLIB
  class ZlibContext {
    // `ZlibContext` is the WebSocket's interface to Zlib's compression/decompression functions.
    // Depending on the `mode`, `ZlibContext` will act as a compressor or a decompressor.
  public:
    enum class Mode {
      COMPRESS,
      DECOMPRESS,
    };

    struct Result {
      int processResult = 0;
      kj::Array<const byte> buffer;
      size_t size = 0; // Number of bytes used; size <= buffer.size().
    };

    ZlibContext(Mode mode, const CompressionParameters& config) : mode(mode) {
      switch (mode) {
        case Mode::COMPRESS: {
          int windowBits = -config.outboundMaxWindowBits.orDefault(15);
          // We use negative values because we want to use raw deflate.
          if(windowBits == -8) {
            // Zlib cannot accept `windowBits` of 8 for the deflater. However, due to an
            // implementation quirk, `windowBits` of 8 and 9 would both use 250 bytes.
            // Therefore, a decompressor using `windowBits` of 8 could safely inflate a message
            // that a zlib client compressed using `windowBits` = 9.
            // https://bugs.chromium.org/p/chromium/issues/detail?id=691074
            windowBits = -9;
          }
          int result = deflateInit2(
              &ctx,
              Z_DEFAULT_COMPRESSION,
              Z_DEFLATED,
              windowBits,
              8,  // memLevel = 8 is the default
              Z_DEFAULT_STRATEGY);
          KJ_REQUIRE(result == Z_OK, "Failed to initialize compression context (deflate).");
          break;
        }
        case Mode::DECOMPRESS: {
          int windowBits = -config.inboundMaxWindowBits.orDefault(15);
          // We use negative values because we want to use raw inflate.
          int result = inflateInit2(&ctx, windowBits);
          KJ_REQUIRE(result == Z_OK, "Failed to initialize decompression context (inflate).");
          break;
        }
      }
    }

    ~ZlibContext() noexcept(false) {
      switch (mode) {
        case Mode::COMPRESS:
          deflateEnd(&ctx);
          break;
        case Mode::DECOMPRESS:
          inflateEnd(&ctx);
          break;
      }
    }

    KJ_DISALLOW_COPY_AND_MOVE(ZlibContext);

    kj::Array<kj::byte> processMessage(kj::ArrayPtr<const byte> message,
        kj::Maybe<size_t> maxSize = nullptr,
        bool addNullTerminator = false) {
      // If `this` is the compressor, calling `processMessage()` will compress the `message`.
      // Likewise, if `this` is the decompressor, `processMessage()` will decompress the `message`.
      //
      // `maxSize` is only passed in when decompressing, since we want to ensure the decompressed
      // message is smaller than the `maxSize` passed to `receive()`.
      //
      // If (de)compression is successful, the result is returned as a Vector, otherwise,
      // an Exception is thrown.

      ctx.next_in = const_cast<byte*>(reinterpret_cast<const byte*>(message.begin()));
      ctx.avail_in = message.size();

      kj::Vector<Result> parts(processLoop(maxSize));

      size_t amountToAllocate = 0;
      for (const auto& part : parts) {
        amountToAllocate += part.size;
      }

      if (addNullTerminator) {
        // Add space for the null-terminator.
        amountToAllocate += 1;
      }

      kj::Array<kj::byte> processedMessage = kj::heapArray<kj::byte>(amountToAllocate);
      size_t currentIndex = 0; // Current index into processedMessage.
      for (const auto& part : parts) {
        memcpy(&processedMessage[currentIndex], part.buffer.begin(), part.size);
        // We need to use `part.size` to determine the number of useful bytes, since data after
        // `part.size` is unused (and probably junk).
        currentIndex += part.size;
      }

      if (addNullTerminator) {
        processedMessage[currentIndex++] = '\0';
      }

      KJ_ASSERT(currentIndex == processedMessage.size());

      return kj::mv(processedMessage);
    }

    void reset() {
      // Resets the (de)compression context. This should only be called when the (de)compressor uses
      // client/server_no_context_takeover.
      switch (mode) {
        case Mode::COMPRESS: {
          KJ_ASSERT(deflateReset(&ctx) == Z_OK, "deflateReset() failed.");
          break;
        }
        case Mode::DECOMPRESS: {
          KJ_ASSERT(inflateReset(&ctx) == Z_OK, "inflateReset failed.");
          break;
        }
      }

    }

  private:
    Result pumpOnce() {
      // Prepares Zlib's internal state for a call to deflate/inflate, then calls the relevant
      // function to process the input buffer. It is assumed that the caller has already set up
      // Zlib's input buffer.
      //
      // Since calls to deflate/inflate will process data until the input is empty, or until the
      // output is full, multiple calls to `pumpOnce()` may be required to process the entire
      // message. We're done processing once either `result` is `Z_STREAM_END`, or we get
      // `Z_BUF_ERROR` and did not write any more output.
      size_t bufSize = 4096;
      Array<kj::byte> buffer = kj::heapArray<kj::byte>(bufSize);
      ctx.next_out = buffer.begin();
      ctx.avail_out = bufSize;

      int result = Z_OK;

      switch (mode) {
        case Mode::COMPRESS:
          result = deflate(&ctx, Z_SYNC_FLUSH);
          KJ_REQUIRE(result == Z_OK || result == Z_BUF_ERROR || result == Z_STREAM_END,
                      "Compression failed", result);
          break;
        case Mode::DECOMPRESS:
          result = inflate(&ctx, Z_SYNC_FLUSH);
          KJ_REQUIRE(result == Z_OK || result == Z_BUF_ERROR || result == Z_STREAM_END,
                      "Decompression failed", result, " with reason", ctx.msg);
          break;
      }

      return Result {
        result,
        kj::mv(buffer),
        bufSize - ctx.avail_out,
      };
    }

    kj::Vector<Result> processLoop(kj::Maybe<size_t> maxSize) {
      // Since Zlib buffers the writes, we want to continue processing until there's nothing left.
      kj::Vector<Result> output;
      size_t totalBytesProcessed = 0;
      for (;;) {
        Result result = pumpOnce();

        auto status = result.processResult;
        auto bytesProcessed = result.size;
        if (bytesProcessed > 0) {
          output.add(kj::mv(result));
          totalBytesProcessed += bytesProcessed;
          KJ_IF_MAYBE(m, maxSize) {
            // This is only non-null for `receive` calls, so we must be decompressing. We don't want
            // the decompressed message to OOM us, so let's make sure it's not too big.
            KJ_REQUIRE(totalBytesProcessed < *m,
                "Decompressed WebSocket message is too large");
          }
        }

        if ((ctx.avail_in == 0 && ctx.avail_out != 0) || status == Z_STREAM_END) {
          // If we're out of input to consume, and we have space in the output buffer, then we must
          // have flushed the remaining message, so we're done pumping. Alternatively, if we found a
          // BFINAL deflate block, then we know the stream is completely finished.
          if (status == Z_STREAM_END) {
            reset();
          }
          return kj::mv(output);
        }
      }
    }

    Mode mode;
    z_stream ctx = {};
  };
#endif // KJ_HAS_ZLIB

  static constexpr byte OPCODE_CONTINUATION = 0;
  static constexpr byte OPCODE_TEXT         = 1;
  static constexpr byte OPCODE_BINARY       = 2;
  static constexpr byte OPCODE_CLOSE        = 8;
  static constexpr byte OPCODE_PING         = 9;
  static constexpr byte OPCODE_PONG         = 10;

  static constexpr byte OPCODE_FIRST_CONTROL = 8;

  // ---------------------------------------------------------------------------

  kj::Own<kj::AsyncIoStream> stream;
  kj::Maybe<EntropySource&> maskKeyGenerator;
  kj::Maybe<CompressionParameters> compressionConfig;
  WebSocketErrorHandler& errorHandler;
#if KJ_HAS_ZLIB
  kj::Maybe<ZlibContext> compressionContext;
  kj::Maybe<ZlibContext> decompressionContext;
#endif // KJ_HAS_ZLIB

  bool hasSentClose = false;
  bool disconnected = false;
  bool currentlySending = false;
  Header sendHeader;
  kj::ArrayPtr<const byte> sendParts[2];

  kj::Maybe<kj::Array<byte>> queuedPong;
  // If a Ping is received while currentlySending is true, then queuedPong is set to the body of
  // a pong message that should be sent once the current send is complete.

  kj::Maybe<kj::Promise<void>> sendingPong;
  // If a Pong is being sent asynchronously in response to a Ping, this is a promise for the
  // completion of that send.
  //
  // Additionally, this member is used if we need to block our first send on WebSocket startup,
  // e.g. because we need to wait for HTTP handshake writes to flush before we can start sending
  // WebSocket data. `sendingPong` was overloaded for this use case because the logic is the same.
  // Perhaps it should be renamed to `blockSend` or `writeQueue`.

  uint fragmentOpcode = 0;
  bool fragmentCompressed = false;
  // For fragmented messages, was the first frame compressed?
  // Note that subsequent frames of a compressed message will not set the RSV1 bit.
  kj::Vector<kj::Array<byte>> fragments;
  // If `fragments` is non-empty, we've already received some fragments of a message.
  // `fragmentOpcode` is the original opcode.

  kj::Array<byte> recvBuffer;
  kj::ArrayPtr<byte> recvData;

  uint64_t sentBytes = 0;
  uint64_t receivedBytes = 0;

  kj::Promise<void> sendImpl(byte opcode, kj::ArrayPtr<const byte> message) {
    KJ_REQUIRE(!disconnected, "WebSocket can't send after disconnect()");
    KJ_REQUIRE(!currentlySending, "another message send is already in progress");

    currentlySending = true;

    KJ_IF_MAYBE(p, sendingPong) {
      // We recently sent a pong, make sure it's finished before proceeding.
      auto promise = p->then([this, opcode, message]() {
        currentlySending = false;
        return sendImpl(opcode, message);
      });
      sendingPong = nullptr;
      return promise;
    }

    // We don't stop the application from sending further messages after close() -- this is the
    // application's error to make. But, we do want to make sure we don't send any PONGs after a
    // close, since that would be our error. So we stack whether we closed for that reason.
    hasSentClose = hasSentClose || opcode == OPCODE_CLOSE;

    Mask mask(maskKeyGenerator);

    bool useCompression = false;
    kj::Maybe<kj::Array<byte>> compressedMessage;
    if (opcode == OPCODE_BINARY || opcode == OPCODE_TEXT) {
      // We can only compress data frames.
#if KJ_HAS_ZLIB
      KJ_IF_MAYBE(config, compressionConfig) {
        useCompression = true;
        // Compress `message` according to `compressionConfig`s outbound parameters.
        auto& compressor = KJ_ASSERT_NONNULL(compressionContext);
        if (config->outboundNoContextTakeover) {
          // We must reset context on each message.
          compressor.reset();
        }
        auto& innerMessage = compressedMessage.emplace(compressor.processMessage(message));
        if (message.size() > 0) {
          KJ_ASSERT(innerMessage.asPtr().endsWith({0x00, 0x00, 0xFF, 0xFF}));
          message = innerMessage.slice(0, innerMessage.size() - 4);
          // Strip 0x00 0x00 0xFF 0xFF off the tail.
          // See: https://datatracker.ietf.org/doc/html/rfc7692#section-7.2.1
        } else {
          // RFC 7692 (7.2.3.6) specifies that an empty uncompressed DEFLATE block (0x00) should be
          // built if the compression library doesn't generate data when the input is empty.
          message = compressedMessage.emplace(kj::heapArray<byte>({0x00}));
        }
      }
#endif // KJ_HAS_ZLIB
    }

    kj::Array<byte> ownMessage;
    if (!mask.isZero()) {
      // Sadness, we have to make a copy to apply the mask.
      ownMessage = kj::heapArray(message);
      mask.apply(ownMessage);
      message = ownMessage;
    }

    sendParts[0] = sendHeader.compose(true, useCompression, opcode, message.size(), mask);
    sendParts[1] = message;
    KJ_ASSERT(!sendHeader.hasRsv2or3(), "RSV bits 2 and 3 must be 0, as we do not currently "
        "support an extension that would set these bits");

    auto promise = stream->write(sendParts).attach(kj::mv(compressedMessage));
    if (!mask.isZero()) {
      promise = promise.attach(kj::mv(ownMessage));
    }
    return promise.then([this, size = sendParts[0].size() + sendParts[1].size()]() {
      currentlySending = false;

      // Send queued pong if needed.
      KJ_IF_MAYBE(q, queuedPong) {
        kj::Array<byte> payload = kj::mv(*q);
        queuedPong = nullptr;
        queuePong(kj::mv(payload));
      }
      sentBytes += size;
    });
  }

  void queuePong(kj::Array<byte> payload) {
    if (currentlySending) {
      // There is a message-send in progress, so we cannot write to the stream now.
      //
      // Note: According to spec, if the server receives a second ping before responding to the
      //   previous one, it can opt to respond only to the last ping. So we don't have to check if
      //   queuedPong is already non-null.
      queuedPong = kj::mv(payload);
    } else KJ_IF_MAYBE(promise, sendingPong) {
      // We're still sending a previous pong. Wait for it to finish before sending ours.
      sendingPong = promise->then([this,payload=kj::mv(payload)]() mutable {
        return sendPong(kj::mv(payload));
      });
    } else {
      // We're not sending any pong currently.
      sendingPong = sendPong(kj::mv(payload));
    }
  }

  kj::Promise<void> sendPong(kj::Array<byte> payload) {
    if (hasSentClose || disconnected) {
      return kj::READY_NOW;
    }

    sendParts[0] = sendHeader.compose(true, false, OPCODE_PONG,
                                      payload.size(), Mask(maskKeyGenerator));
    sendParts[1] = payload;
    return stream->write(sendParts).attach(kj::mv(payload));
  }

  kj::Promise<void> optimizedPumpTo(WebSocketImpl& other) {
    KJ_IF_MAYBE(p, other.sendingPong) {
      // We recently sent a pong, make sure it's finished before proceeding.
      auto promise = p->then([this, &other]() {
        return optimizedPumpTo(other);
      });
      other.sendingPong = nullptr;
      return promise;
    }

    if (recvData.size() > 0) {
      // We have some data buffered. Write it first.
      return other.stream->write(recvData.begin(), recvData.size())
          .then([this, &other, size = recvData.size()]() {
        recvData = nullptr;
        other.sentBytes += size;
        return optimizedPumpTo(other);
      });
    }

    auto cancelPromise = other.stream->whenWriteDisconnected()
        .then([this]() -> kj::Promise<void> {
      this->abort();
      return KJ_EXCEPTION(DISCONNECTED,
          "destination of WebSocket pump disconnected prematurely");
    });

    // There's no buffered incoming data, so start pumping stream now.
    return stream->pumpTo(*other.stream).then([this, &other](size_t s) -> kj::Promise<void> {
      // WebSocket pumps are expected to include end-of-stream.
      other.disconnected = true;
      other.stream->shutdownWrite();
      receivedBytes += s;
      other.sentBytes += s;
      return kj::READY_NOW;
    }, [&other](kj::Exception&& e) -> kj::Promise<void> {
      // We don't know if it was a read or a write that threw. If it was a read that threw, we need
      // to send a disconnect on the destination. If it was the destination that threw, it
      // shouldn't hurt to disconnect() it again, but we'll catch and squelch any exceptions.
      other.disconnected = true;
      kj::runCatchingExceptions([&other]() { other.stream->shutdownWrite(); });
      return kj::mv(e);
    }).exclusiveJoin(kj::mv(cancelPromise));
  }
};

kj::Own<WebSocket> upgradeToWebSocket(
    kj::Own<kj::AsyncIoStream> stream, HttpInputStreamImpl& httpInput, HttpOutputStream& httpOutput,
    kj::Maybe<EntropySource&> maskKeyGenerator,
    kj::Maybe<CompressionParameters> compressionConfig = nullptr,
    kj::Maybe<WebSocketErrorHandler&> errorHandler = nullptr) {
  // Create a WebSocket upgraded from an HTTP stream.
  auto releasedBuffer = httpInput.releaseBuffer();
  return kj::heap<WebSocketImpl>(kj::mv(stream), maskKeyGenerator,
                                 kj::mv(compressionConfig), errorHandler,
                                 kj::mv(releasedBuffer.buffer),
                                 releasedBuffer.leftover, httpOutput.flush());
}

}  // namespace

kj::Own<WebSocket> newWebSocket(kj::Own<kj::AsyncIoStream> stream,
                                kj::Maybe<EntropySource&> maskKeyGenerator,
                                kj::Maybe<CompressionParameters> compressionConfig,
                                kj::Maybe<WebSocketErrorHandler&> errorHandler) {
  return kj::heap<WebSocketImpl>(kj::mv(stream), maskKeyGenerator, kj::mv(compressionConfig), errorHandler);
}

static kj::Promise<void> pumpWebSocketLoop(WebSocket& from, WebSocket& to) {
  return from.receive().then([&from,&to](WebSocket::Message&& message) {
    KJ_SWITCH_ONEOF(message) {
      KJ_CASE_ONEOF(text, kj::String) {
        return to.send(text)
            .attach(kj::mv(text))
            .then([&from,&to]() { return pumpWebSocketLoop(from, to); });
      }
      KJ_CASE_ONEOF(data, kj::Array<byte>) {
        return to.send(data)
            .attach(kj::mv(data))
            .then([&from,&to]() { return pumpWebSocketLoop(from, to); });
      }
      KJ_CASE_ONEOF(close, WebSocket::Close) {
        // Once a close has passed through, the pump is complete.
        return to.close(close.code, close.reason)
            .attach(kj::mv(close));
      }
    }
    KJ_UNREACHABLE;
  }, [&to](kj::Exception&& e) {
    if (e.getType() == kj::Exception::Type::DISCONNECTED) {
      return to.disconnect();
    } else {
      return to.close(1002, e.getDescription());
    }
  });
}

kj::Promise<void> WebSocket::pumpTo(WebSocket& other) {
  KJ_IF_MAYBE(p, other.tryPumpFrom(*this)) {
    // Yay, optimized pump!
    return kj::mv(*p);
  } else {
    // Fall back to default implementation.
    return kj::evalNow([&]() {
      auto cancelPromise = other.whenAborted().then([this]() -> kj::Promise<void> {
        this->abort();
        return KJ_EXCEPTION(DISCONNECTED,
            "destination of WebSocket pump disconnected prematurely");
      });
      return pumpWebSocketLoop(*this, other).exclusiveJoin(kj::mv(cancelPromise));
    });
  }
}

kj::Maybe<kj::Promise<void>> WebSocket::tryPumpFrom(WebSocket& other) {
  return nullptr;
}

namespace {

class WebSocketPipeImpl final: public WebSocket, public kj::Refcounted {
  // Represents one direction of a WebSocket pipe.
  //
  // This class behaves as a "loopback" WebSocket: a message sent using send() is received using
  // receive(), on the same object. This is *not* how WebSocket implementations usually behave.
  // But, this object is actually used to implement only one direction of a bidirectional pipe. At
  // another layer above this, the pipe is actually composed of two WebSocketPipeEnd instances,
  // which layer on top of two WebSocketPipeImpl instances representing the two directions. So,
  // send() calls on a WebSocketPipeImpl instance always come from one of the two WebSocketPipeEnds
  // while receive() calls come from the other end.

public:
  ~WebSocketPipeImpl() noexcept(false) {
    KJ_REQUIRE(state == nullptr || ownState.get() != nullptr,
        "destroying WebSocketPipe with operation still in-progress; probably going to segfault") {
      // Don't std::terminate().
      break;
    }
  }

  void abort() override {
    KJ_IF_MAYBE(s, state) {
      s->abort();
    } else {
      ownState = heap<Aborted>();
      state = *ownState;

      aborted = true;
      KJ_IF_MAYBE(f, abortedFulfiller) {
        f->get()->fulfill();
        abortedFulfiller = nullptr;
      }
    }
  }

  kj::Promise<void> send(kj::ArrayPtr<const byte> message) override {
    KJ_IF_MAYBE(s, state) {
      return s->send(message).then([&, size = message.size()]() { transferredBytes += size; });
    } else {
      return newAdaptedPromise<void, BlockedSend>(*this, MessagePtr(message))
          .then([&, size = message.size()]() { transferredBytes += size; });
    }
  }
  kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
    KJ_IF_MAYBE(s, state) {
      return s->send(message).then([&, size = message.size()]() { transferredBytes += size; });
    } else {
      return newAdaptedPromise<void, BlockedSend>(*this, MessagePtr(message))
          .then([&, size = message.size()]() { transferredBytes += size; });
    }
  }
  kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
    KJ_IF_MAYBE(s, state) {
      return s->close(code, reason)
          .then([&, size = reason.size()]() { transferredBytes += (2 +size); });
    } else {
      return newAdaptedPromise<void, BlockedSend>(*this, MessagePtr(ClosePtr { code, reason }))
          .then([&, size = reason.size()]() { transferredBytes += (2 +size); });
    }
  }
  kj::Promise<void> disconnect() override {
    KJ_IF_MAYBE(s, state) {
      return s->disconnect();
    } else {
      ownState = heap<Disconnected>();
      state = *ownState;
      return kj::READY_NOW;
    }
  }
  kj::Promise<void> whenAborted() override {
    if (aborted) {
      return kj::READY_NOW;
    } else KJ_IF_MAYBE(p, abortedPromise) {
      return p->addBranch();
    } else {
      auto paf = newPromiseAndFulfiller<void>();
      abortedFulfiller = kj::mv(paf.fulfiller);
      auto fork = paf.promise.fork();
      auto result = fork.addBranch();
      abortedPromise = kj::mv(fork);
      return result;
    }
  }
  kj::Maybe<kj::Promise<void>> tryPumpFrom(WebSocket& other) override {
    KJ_IF_MAYBE(s, state) {
      return s->tryPumpFrom(other);
    } else {
      return newAdaptedPromise<void, BlockedPumpFrom>(*this, other);
    }
  }

  kj::Promise<Message> receive(size_t maxSize) override {
    KJ_IF_MAYBE(s, state) {
      return s->receive(maxSize);
    } else {
      return newAdaptedPromise<Message, BlockedReceive>(*this, maxSize);
    }
  }
  kj::Promise<void> pumpTo(WebSocket& other) override {
    auto onAbort = other.whenAborted()
        .then([]() -> kj::Promise<void> {
      return KJ_EXCEPTION(DISCONNECTED, "WebSocket was aborted");
    });

    KJ_IF_MAYBE(s, state) {
      auto before = other.receivedByteCount();
      return s->pumpTo(other).attach(kj::defer([this, &other, before]() {
        transferredBytes += other.receivedByteCount() - before;
      })).exclusiveJoin(kj::mv(onAbort));
    } else {
      return newAdaptedPromise<void, BlockedPumpTo>(*this, other).exclusiveJoin(kj::mv(onAbort));
    }
  }

  uint64_t sentByteCount() override {
    return transferredBytes;
  }
  uint64_t receivedByteCount() override {
    return transferredBytes;
  }

private:
  kj::Maybe<WebSocket&> state;
  // Object-oriented state! If any method call is blocked waiting on activity from the other end,
  // then `state` is non-null and method calls should be forwarded to it. If no calls are
  // outstanding, `state` is null.

  kj::Own<WebSocket> ownState;

  uint64_t transferredBytes = 0;

  bool aborted = false;
  Maybe<Own<PromiseFulfiller<void>>> abortedFulfiller = nullptr;
  Maybe<ForkedPromise<void>> abortedPromise = nullptr;

  void endState(WebSocket& obj) {
    KJ_IF_MAYBE(s, state) {
      if (s == &obj) {
        state = nullptr;
      }
    }
  }

  struct ClosePtr {
    uint16_t code;
    kj::StringPtr reason;
  };
  typedef kj::OneOf<kj::ArrayPtr<const char>, kj::ArrayPtr<const byte>, ClosePtr> MessagePtr;

  class BlockedSend final: public WebSocket {
  public:
    BlockedSend(kj::PromiseFulfiller<void>& fulfiller, WebSocketPipeImpl& pipe, MessagePtr message)
        : fulfiller(fulfiller), pipe(pipe), message(kj::mv(message)) {
      KJ_REQUIRE(pipe.state == nullptr);
      pipe.state = *this;
    }
    ~BlockedSend() noexcept(false) {
      pipe.endState(*this);
    }

    void abort() override {
      canceler.cancel("other end of WebSocketPipe was destroyed");
      fulfiller.reject(KJ_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed"));
      pipe.endState(*this);
      pipe.abort();
    }
    kj::Promise<void> whenAborted() override {
      KJ_FAIL_ASSERT("can't get here -- implemented by WebSocketPipeImpl");
    }

    kj::Promise<void> send(kj::ArrayPtr<const byte> message) override {
      KJ_FAIL_ASSERT("another message send is already in progress");
    }
    kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
      KJ_FAIL_ASSERT("another message send is already in progress");
    }
    kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
      KJ_FAIL_ASSERT("another message send is already in progress");
    }
    kj::Promise<void> disconnect() override {
      KJ_FAIL_ASSERT("another message send is already in progress");
    }
    kj::Maybe<kj::Promise<void>> tryPumpFrom(WebSocket& other) override {
      KJ_FAIL_ASSERT("another message send is already in progress");
    }

    kj::Promise<Message> receive(size_t maxSize) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");
      fulfiller.fulfill();
      pipe.endState(*this);
      KJ_SWITCH_ONEOF(message) {
        KJ_CASE_ONEOF(arr, kj::ArrayPtr<const char>) {
          return Message(kj::str(arr));
        }
        KJ_CASE_ONEOF(arr, kj::ArrayPtr<const byte>) {
          auto copy = kj::heapArray<byte>(arr.size());
          memcpy(copy.begin(), arr.begin(), arr.size());
          return Message(kj::mv(copy));
        }
        KJ_CASE_ONEOF(close, ClosePtr) {
          return Message(Close { close.code, kj::str(close.reason) });
        }
      }
      KJ_UNREACHABLE;
    }
    kj::Promise<void> pumpTo(WebSocket& other) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");
      kj::Promise<void> promise = nullptr;
      KJ_SWITCH_ONEOF(message) {
        KJ_CASE_ONEOF(arr, kj::ArrayPtr<const char>) {
          promise = other.send(arr);
        }
        KJ_CASE_ONEOF(arr, kj::ArrayPtr<const byte>) {
          promise = other.send(arr);
        }
        KJ_CASE_ONEOF(close, ClosePtr) {
          promise = other.close(close.code, close.reason);
        }
      }
      return canceler.wrap(promise.then([this,&other]() {
        canceler.release();
        fulfiller.fulfill();
        pipe.endState(*this);
        return pipe.pumpTo(other);
      }, [this](kj::Exception&& e) -> kj::Promise<void> {
        canceler.release();
        fulfiller.reject(kj::cp(e));
        pipe.endState(*this);
        return kj::mv(e);
      }));
    }

  uint64_t sentByteCount() override {
    KJ_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
  }
  uint64_t receivedByteCount() override {
    KJ_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
   }

  private:
    kj::PromiseFulfiller<void>& fulfiller;
    WebSocketPipeImpl& pipe;
    MessagePtr message;
    Canceler canceler;
  };

  class BlockedPumpFrom final: public WebSocket {
  public:
    BlockedPumpFrom(kj::PromiseFulfiller<void>& fulfiller, WebSocketPipeImpl& pipe,
                    WebSocket& input)
        : fulfiller(fulfiller), pipe(pipe), input(input) {
      KJ_REQUIRE(pipe.state == nullptr);
      pipe.state = *this;
    }
    ~BlockedPumpFrom() noexcept(false) {
      pipe.endState(*this);
    }

    void abort() override {
      canceler.cancel("other end of WebSocketPipe was destroyed");
      fulfiller.reject(KJ_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed"));
      pipe.endState(*this);
      pipe.abort();
    }
    kj::Promise<void> whenAborted() override {
      KJ_FAIL_ASSERT("can't get here -- implemented by WebSocketPipeImpl");
    }

    kj::Promise<void> send(kj::ArrayPtr<const byte> message) override {
      KJ_FAIL_ASSERT("another message send is already in progress");
    }
    kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
      KJ_FAIL_ASSERT("another message send is already in progress");
    }
    kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
      KJ_FAIL_ASSERT("another message send is already in progress");
    }
    kj::Promise<void> disconnect() override {
      KJ_FAIL_ASSERT("another message send is already in progress");
    }
    kj::Maybe<kj::Promise<void>> tryPumpFrom(WebSocket& other) override {
      KJ_FAIL_ASSERT("another message send is already in progress");
    }

    kj::Promise<Message> receive(size_t maxSize) override {
      KJ_REQUIRE(canceler.isEmpty(), "another message receive is already in progress");
      return canceler.wrap(input.receive(maxSize)
          .then([this](Message message) {
        if (message.is<Close>()) {
          canceler.release();
          fulfiller.fulfill();
          pipe.endState(*this);
        }
        return kj::mv(message);
      }, [this](kj::Exception&& e) -> Message {
        canceler.release();
        fulfiller.reject(kj::cp(e));
        pipe.endState(*this);
        kj::throwRecoverableException(kj::mv(e));
        return Message(kj::String());
      }));
    }
    kj::Promise<void> pumpTo(WebSocket& other) override {
      KJ_REQUIRE(canceler.isEmpty(), "another message receive is already in progress");
      return canceler.wrap(input.pumpTo(other)
          .then([this]() {
        canceler.release();
        fulfiller.fulfill();
        pipe.endState(*this);
      }, [this](kj::Exception&& e) {
        canceler.release();
        fulfiller.reject(kj::cp(e));
        pipe.endState(*this);
        kj::throwRecoverableException(kj::mv(e));
      }));
    }

    uint64_t sentByteCount() override {
      KJ_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
    uint64_t receivedByteCount() override {
      KJ_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }

  private:
    kj::PromiseFulfiller<void>& fulfiller;
    WebSocketPipeImpl& pipe;
    WebSocket& input;
    Canceler canceler;
  };

  class BlockedReceive final: public WebSocket {
  public:
    BlockedReceive(kj::PromiseFulfiller<Message>& fulfiller, WebSocketPipeImpl& pipe,
                   size_t maxSize)
        : fulfiller(fulfiller), pipe(pipe), maxSize(maxSize) {
      KJ_REQUIRE(pipe.state == nullptr);
      pipe.state = *this;
    }
    ~BlockedReceive() noexcept(false) {
      pipe.endState(*this);
    }

    void abort() override {
      canceler.cancel("other end of WebSocketPipe was destroyed");
      fulfiller.reject(KJ_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed"));
      pipe.endState(*this);
      pipe.abort();
    }
    kj::Promise<void> whenAborted() override {
      KJ_FAIL_ASSERT("can't get here -- implemented by WebSocketPipeImpl");
    }

    kj::Promise<void> send(kj::ArrayPtr<const byte> message) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");
      auto copy = kj::heapArray<byte>(message.size());
      memcpy(copy.begin(), message.begin(), message.size());
      fulfiller.fulfill(Message(kj::mv(copy)));
      pipe.endState(*this);
      return kj::READY_NOW;
    }
    kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");
      fulfiller.fulfill(Message(kj::str(message)));
      pipe.endState(*this);
      return kj::READY_NOW;
    }
    kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");
      fulfiller.fulfill(Message(Close { code, kj::str(reason) }));
      pipe.endState(*this);
      return kj::READY_NOW;
    }
    kj::Promise<void> disconnect() override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");
      fulfiller.reject(KJ_EXCEPTION(DISCONNECTED, "WebSocket disconnected"));
      pipe.endState(*this);
      return pipe.disconnect();
    }
    kj::Maybe<kj::Promise<void>> tryPumpFrom(WebSocket& other) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");
      return canceler.wrap(other.receive(maxSize).then([this,&other](Message message) {
        canceler.release();
        fulfiller.fulfill(kj::mv(message));
        pipe.endState(*this);
        return other.pumpTo(pipe);
      }, [this](kj::Exception&& e) -> kj::Promise<void> {
        canceler.release();
        fulfiller.reject(kj::cp(e));
        pipe.endState(*this);
        return kj::mv(e);
      }));
    }

    kj::Promise<Message> receive(size_t maxSize) override {
      KJ_FAIL_ASSERT("another message receive is already in progress");
    }
    kj::Promise<void> pumpTo(WebSocket& other) override {
      KJ_FAIL_ASSERT("another message receive is already in progress");
    }

    uint64_t sentByteCount() override {
      KJ_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
    uint64_t receivedByteCount() override {
      KJ_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }

  private:
    kj::PromiseFulfiller<Message>& fulfiller;
    WebSocketPipeImpl& pipe;
    size_t maxSize;
    Canceler canceler;
  };

  class BlockedPumpTo final: public WebSocket {
  public:
    BlockedPumpTo(kj::PromiseFulfiller<void>& fulfiller, WebSocketPipeImpl& pipe, WebSocket& output)
        : fulfiller(fulfiller), pipe(pipe), output(output) {
      KJ_REQUIRE(pipe.state == nullptr);
      pipe.state = *this;
    }
    ~BlockedPumpTo() noexcept(false) {
      pipe.endState(*this);
    }

    void abort() override {
      canceler.cancel("other end of WebSocketPipe was destroyed");

      // abort() is called when the pipe end is dropped. This should be treated as disconnecting,
      // so pumpTo() should complete normally.
      fulfiller.fulfill();

      pipe.endState(*this);
      pipe.abort();
    }
    kj::Promise<void> whenAborted() override {
      KJ_FAIL_ASSERT("can't get here -- implemented by WebSocketPipeImpl");
    }

    kj::Promise<void> send(kj::ArrayPtr<const byte> message) override {
      KJ_REQUIRE(canceler.isEmpty(), "another message send is already in progress");
      return canceler.wrap(output.send(message));
    }
    kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
      KJ_REQUIRE(canceler.isEmpty(), "another message send is already in progress");
      return canceler.wrap(output.send(message));
    }
    kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
      KJ_REQUIRE(canceler.isEmpty(), "another message send is already in progress");
      return canceler.wrap(output.close(code, reason).then([this]() {
        // A pump is expected to end upon seeing a Close message.
        canceler.release();
        pipe.endState(*this);
        fulfiller.fulfill();
      }, [this](kj::Exception&& e) {
        canceler.release();
        pipe.endState(*this);
        fulfiller.reject(kj::cp(e));
        kj::throwRecoverableException(kj::mv(e));
      }));
    }
    kj::Promise<void> disconnect() override {
      KJ_REQUIRE(canceler.isEmpty(), "another message send is already in progress");
      return canceler.wrap(output.disconnect().then([this]() {
        canceler.release();
        pipe.endState(*this);
        fulfiller.fulfill();
        return pipe.disconnect();
      }, [this](kj::Exception&& e) {
        canceler.release();
        pipe.endState(*this);
        fulfiller.reject(kj::cp(e));
        kj::throwRecoverableException(kj::mv(e));
      }));
    }
    kj::Maybe<kj::Promise<void>> tryPumpFrom(WebSocket& other) override {
      KJ_REQUIRE(canceler.isEmpty(), "another message send is already in progress");
      return canceler.wrap(other.pumpTo(output).then([this]() {
        canceler.release();
        pipe.endState(*this);
        fulfiller.fulfill();
      }, [this](kj::Exception&& e) {
        canceler.release();
        pipe.endState(*this);
        fulfiller.reject(kj::cp(e));
        kj::throwRecoverableException(kj::mv(e));
      }));
    }

    kj::Promise<Message> receive(size_t maxSize) override {
      KJ_FAIL_ASSERT("another message receive is already in progress");
    }
    kj::Promise<void> pumpTo(WebSocket& other) override {
      KJ_FAIL_ASSERT("another message receive is already in progress");
    }

    uint64_t sentByteCount() override {
      KJ_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
    uint64_t receivedByteCount() override {
      KJ_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }

  private:
    kj::PromiseFulfiller<void>& fulfiller;
    WebSocketPipeImpl& pipe;
    WebSocket& output;
    Canceler canceler;
  };

  class Disconnected final: public WebSocket {
  public:
    void abort() override {
      // can ignore
    }
    kj::Promise<void> whenAborted() override {
      KJ_FAIL_ASSERT("can't get here -- implemented by WebSocketPipeImpl");
    }

    kj::Promise<void> send(kj::ArrayPtr<const byte> message) override {
      KJ_FAIL_REQUIRE("can't send() after disconnect()");
    }
    kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
      KJ_FAIL_REQUIRE("can't send() after disconnect()");
    }
    kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
      KJ_FAIL_REQUIRE("can't close() after disconnect()");
    }
    kj::Promise<void> disconnect() override {
      return kj::READY_NOW;
    }
    kj::Maybe<kj::Promise<void>> tryPumpFrom(WebSocket& other) override {
      KJ_FAIL_REQUIRE("can't tryPumpFrom() after disconnect()");
    }

    kj::Promise<Message> receive(size_t maxSize) override {
      return KJ_EXCEPTION(DISCONNECTED, "WebSocket disconnected");
    }
    kj::Promise<void> pumpTo(WebSocket& other) override {
      return kj::READY_NOW;
    }

    uint64_t sentByteCount() override {
      KJ_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
    uint64_t receivedByteCount() override {
      KJ_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }

  };

  class Aborted final: public WebSocket {
  public:
    void abort() override {
      // can ignore
    }
    kj::Promise<void> whenAborted() override {
      KJ_FAIL_ASSERT("can't get here -- implemented by WebSocketPipeImpl");
    }

    kj::Promise<void> send(kj::ArrayPtr<const byte> message) override {
      return KJ_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
    }
    kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
      return KJ_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
    }
    kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
      return KJ_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
    }
    kj::Promise<void> disconnect() override {
      return KJ_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
    }
    kj::Maybe<kj::Promise<void>> tryPumpFrom(WebSocket& other) override {
      return kj::Promise<void>(KJ_EXCEPTION(DISCONNECTED,
          "other end of WebSocketPipe was destroyed"));
    }

    kj::Promise<Message> receive(size_t maxSize) override {
      return KJ_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
    }
    kj::Promise<void> pumpTo(WebSocket& other) override {
      return KJ_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
    }

    uint64_t sentByteCount() override {
      KJ_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
    uint64_t receivedByteCount() override {
      KJ_FAIL_ASSERT("Bytes are not counted for the individual states of WebSocketPipeImpl.");
    }
  };
};

class WebSocketPipeEnd final: public WebSocket {
public:
  WebSocketPipeEnd(kj::Own<WebSocketPipeImpl> in, kj::Own<WebSocketPipeImpl> out)
      : in(kj::mv(in)), out(kj::mv(out)) {}
  ~WebSocketPipeEnd() noexcept(false) {
    in->abort();
    out->abort();
  }

  kj::Promise<void> send(kj::ArrayPtr<const byte> message) override {
    return out->send(message);
  }
  kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
    return out->send(message);
  }
  kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
    return out->close(code, reason);
  }
  kj::Promise<void> disconnect() override {
    return out->disconnect();
  }
  void abort() override {
    in->abort();
    out->abort();
  }
  kj::Promise<void> whenAborted() override {
    return out->whenAborted();
  }
  kj::Maybe<kj::Promise<void>> tryPumpFrom(WebSocket& other) override {
    return out->tryPumpFrom(other);
  }

  kj::Promise<Message> receive(size_t maxSize) override {
    return in->receive(maxSize);
  }
  kj::Promise<void> pumpTo(WebSocket& other) override {
    return in->pumpTo(other);
  }

  uint64_t sentByteCount() override { return out->sentByteCount(); }
  uint64_t receivedByteCount() override { return in->sentByteCount(); }

private:
  kj::Own<WebSocketPipeImpl> in;
  kj::Own<WebSocketPipeImpl> out;
};

}  // namespace

WebSocketPipe newWebSocketPipe() {
  auto pipe1 = kj::refcounted<WebSocketPipeImpl>();
  auto pipe2 = kj::refcounted<WebSocketPipeImpl>();

  auto end1 = kj::heap<WebSocketPipeEnd>(kj::addRef(*pipe1), kj::addRef(*pipe2));
  auto end2 = kj::heap<WebSocketPipeEnd>(kj::mv(pipe2), kj::mv(pipe1));

  return { { kj::mv(end1), kj::mv(end2) } };
}

// =======================================================================================
class AsyncIoStreamWithInitialBuffer final: public kj::AsyncIoStream {
  // An AsyncIoStream implementation that accepts an initial buffer of data
  // to be read out first, and is optionally capable of deferring writes
  // until a given waitBeforeSend promise is fulfilled.
  //
  // Instances are created with a leftoverBackingBuffer (a kj::Array<byte>)
  // and a leftover kj::ArrayPtr<byte> that provides a view into the backing
  // buffer representing the queued data that is pending to be read. Calling
  // tryRead will consume the data from the leftover first. Once leftover has
  // been fully consumed, reads will defer to the underlying stream.
public:
  AsyncIoStreamWithInitialBuffer(kj::Own<kj::AsyncIoStream> stream,
                                 kj::Array<byte> leftoverBackingBuffer,
                                 kj::ArrayPtr<byte> leftover)
                                 : stream(kj::mv(stream)),
                                   leftoverBackingBuffer(kj::mv(leftoverBackingBuffer)),
                                   leftover(leftover) {}

  void shutdownWrite() override {
    stream->shutdownWrite();
  }

  // AsyncInputStream
  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    KJ_REQUIRE(maxBytes >= minBytes);
    auto destination = static_cast<byte*>(buffer);

    // If there are at least minBytes available in the leftover buffer...
    if (leftover.size() >= minBytes) {
      // We are going to immediately read up to maxBytes from the leftover buffer...
      auto bytesToCopy = kj::min(maxBytes, leftover.size());
      memcpy(destination, leftover.begin(), bytesToCopy);
      leftover = leftover.slice(bytesToCopy, leftover.size());

      // If we've consumed all of the data in the leftover buffer, go ahead and free it.
      if (leftover.size() == 0) {
        leftoverBackingBuffer = nullptr;
      }

      return bytesToCopy;
    } else {
      // We know here that leftover.size() is less than minBytes, but it might not
      // be zero. Copy everything from leftover into the destination buffer then read
      // the rest from the underlying stream.
      auto bytesToCopy = leftover.size();
      KJ_DASSERT(bytesToCopy < minBytes);

      if (bytesToCopy > 0) {
        memcpy(destination, leftover.begin(), bytesToCopy);
        leftoverBackingBuffer = nullptr;
        minBytes -= bytesToCopy;
        maxBytes -= bytesToCopy;
        KJ_DASSERT(minBytes >= 1);
        KJ_DASSERT(maxBytes >= minBytes);
      }

      return stream->tryRead(destination + bytesToCopy, minBytes, maxBytes)
          .then([bytesToCopy](size_t amount) { return amount + bytesToCopy; });
    }
  }

  Maybe<uint64_t> tryGetLength() override {
    // For a CONNECT pipe, we have no idea how much data there is going to be.
    return nullptr;
  }

  kj::Promise<uint64_t> pumpTo(AsyncOutputStream& output,
                               uint64_t amount = kj::maxValue) override {
    return pumpLoop(output, amount, 0);
  }

  kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input,
                                               uint64_t amount = kj::maxValue) override {
    return input.pumpTo(*stream, amount);
  }

  // AsyncOutputStream
  Promise<void> write(const void* buffer, size_t size) override {
    return stream->write(buffer, size);
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return stream->write(pieces);
  }

  Promise<void> whenWriteDisconnected() override {
    return stream->whenWriteDisconnected();
  }

private:

  kj::Promise<uint64_t> pumpLoop(
      kj::AsyncOutputStream& output,
      uint64_t remaining,
      uint64_t total) {
    // If there is any data remaining in the leftover queue, we'll write it out first to output.
    if (leftover.size() > 0) {
      auto bytesToWrite = kj::min(leftover.size(), remaining);
      return output.write(leftover.begin(), bytesToWrite).then(
          [this, &output, remaining, total, bytesToWrite]() mutable -> kj::Promise<uint64_t> {
        leftover = leftover.slice(bytesToWrite, leftover.size());
        // If the leftover buffer has been fully consumed, go ahead and free it now.
        if (leftover.size() == 0) {
          leftoverBackingBuffer = nullptr;
        }
        remaining -= bytesToWrite;
        total += bytesToWrite;

        if (remaining == 0) {
          return total;
        }
        return pumpLoop(output, remaining, total);
      });
    } else {
      // Otherwise, we are just going to defer to stream's pumpTo, making sure to
      // account for the total amount we've already written from the leftover queue.
      return stream->pumpTo(output, remaining).then([total](auto read) {
        return total + read;
      });
    }
  };

  kj::Own<kj::AsyncIoStream> stream;
  kj::Array<byte> leftoverBackingBuffer;
  kj::ArrayPtr<byte> leftover;
};

class AsyncIoStreamWithGuards final: public kj::AsyncIoStream,
                                     private kj::TaskSet::ErrorHandler {
  // This AsyncIoStream adds separate kj::Promise guards to both the input and output,
  // delaying reads and writes until each relevant guard is resolved.
  //
  // When the read guard promise resolves, it may provide a released buffer that will
  // be read out first.
  // The primary use case for this impl is to support pipelined CONNECT calls which
  // optimistically allow outbound writes to happen while establishing the CONNECT
  // tunnel has not yet been completed. If the guard promise rejects, the stream
  // is permanently errored and existing pending calls (reads and writes) are canceled.
public:
  AsyncIoStreamWithGuards(
      kj::Own<kj::AsyncIoStream> inner,
      kj::Promise<kj::Maybe<HttpInputStreamImpl::ReleasedBuffer>> readGuard,
      kj::Promise<void> writeGuard)
      : inner(kj::mv(inner)),
        readGuard(handleReadGuard(kj::mv(readGuard))),
        writeGuard(handleWriteGuard(kj::mv(writeGuard))),
        tasks(*this) {}

  // AsyncInputStream
  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    if (readGuardReleased) {
      return inner->tryRead(buffer, minBytes, maxBytes);
    }
    return readGuard.addBranch().then([this, buffer, minBytes, maxBytes] {
      return inner->tryRead(buffer, minBytes, maxBytes);
    });
  }

  Maybe<uint64_t> tryGetLength() override {
    return nullptr;
  }

  kj::Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount = kj::maxValue) override {
    if (readGuardReleased) {
      return inner->pumpTo(output, amount);
    }
    return readGuard.addBranch().then([this, &output, amount] {
      return inner->pumpTo(output, amount);
    });
  }

  // AsyncOutputStream

  void shutdownWrite() override {
    if (writeGuardReleased) {
      inner->shutdownWrite();
    } else {
      tasks.add(writeGuard.addBranch().then([this]() { inner->shutdownWrite(); }));
    }
  }

  kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input,
                                               uint64_t amount = kj::maxValue) override {
    if (writeGuardReleased) {
      return input.pumpTo(*inner, amount);
    } else {
      return writeGuard.addBranch().then([this,&input,amount]() {
        return input.pumpTo(*inner, amount);
      });
    }
  }

  Promise<void> write(const void* buffer, size_t size) override {
    if (writeGuardReleased) {
      return inner->write(buffer, size);
    } else {
      return writeGuard.addBranch().then([this,buffer,size]() {
        return inner->write(buffer, size);
      });
    }
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    if (writeGuardReleased) {
      return inner->write(pieces);
    } else {
      return writeGuard.addBranch().then([this, pieces]() {
        return inner->write(pieces);
      });
    }
  }

  Promise<void> whenWriteDisconnected() override {
    if (writeGuardReleased) {
      return inner->whenWriteDisconnected();
    } else {
      return writeGuard.addBranch().then([this]() {
        return inner->whenWriteDisconnected();
      }, [](kj::Exception&& e) mutable -> kj::Promise<void> {
        if (e.getType() == kj::Exception::Type::DISCONNECTED) {
          return kj::READY_NOW;
        } else {
          return kj::mv(e);
        }
      });
    }
  }

private:
  kj::Own<kj::AsyncIoStream> inner;
  kj::ForkedPromise<void> readGuard;
  kj::ForkedPromise<void> writeGuard;
  bool readGuardReleased = false;
  bool writeGuardReleased = false;
  kj::TaskSet tasks;
  // Set of tasks used to call `shutdownWrite` after write guard is released.

  void taskFailed(kj::Exception&& exception) override {
    // This `taskFailed` callback is only used when `shutdownWrite` is being called. Because we
    // don't care about DISCONNECTED exceptions when `shutdownWrite` is called we ignore this
    // class of exceptions here.
    if (exception.getType() != kj::Exception::Type::DISCONNECTED) {
      KJ_LOG(ERROR, exception);
    }
  }

  kj::ForkedPromise<void> handleWriteGuard(kj::Promise<void> guard) {
    return guard.then([this]() {
      writeGuardReleased = true;
    }).fork();
  }

  kj::ForkedPromise<void> handleReadGuard(
      kj::Promise<kj::Maybe<HttpInputStreamImpl::ReleasedBuffer>> guard) {
    return guard.then([this](kj::Maybe<HttpInputStreamImpl::ReleasedBuffer> buffer) mutable {
      readGuardReleased = true;
      KJ_IF_MAYBE(b, buffer) {
        if (b->leftover.size() > 0) {
          // We only need to replace the inner stream if a non-empty buffer is provided.
          inner = heap<AsyncIoStreamWithInitialBuffer>(
              kj::mv(inner),
              kj::mv(b->buffer), b->leftover);
        }
      }
    }).fork();
  }
};

// =======================================================================================

namespace _ { // private implementation details

kj::ArrayPtr<const char> splitNext(kj::ArrayPtr<const char>& cursor, char delimiter) {
  // Consumes and returns the next item in a delimited list.
  //
  // If a delimiter is found:
  //  - `cursor` is updated to point to the rest of the string after the delimiter.
  //  - The text before the delimiter is returned.
  // If no delimiter is found:
  //  - `cursor` is updated to an empty string.
  //  - The text that had been in `cursor` is returned.
  //
  // (It's up to the caller to stop the loop once `cursor` is empty.)
  KJ_IF_MAYBE(index, cursor.findFirst(delimiter)) {
    auto part = cursor.slice(0, *index);
    cursor = cursor.slice(*index + 1, cursor.size());
    return part;
  }
  kj::ArrayPtr<const char> result(kj::mv(cursor));
  cursor = nullptr;

  return result;
}

void stripLeadingAndTrailingSpace(ArrayPtr<const char>& str) {
  // Remove any leading/trailing spaces from `str`, modifying it in-place.
  while (str.size() > 0 && (str[0] == ' ' || str[0] == '\t')) {
    str = str.slice(1, str.size());
  }
  while (str.size() > 0 && (str.back() == ' ' || str.back() == '\t')) {
    str = str.slice(0, str.size() - 1);
  }
}

kj::Vector<kj::ArrayPtr<const char>> splitParts(kj::ArrayPtr<const char> input, char delim) {
  // Given a string `input` and a delimiter `delim`, split the string into a vector of substrings,
  // separated by the delimiter. Note that leading/trailing whitespace is stripped from each element.
  kj::Vector<kj::ArrayPtr<const char>> parts;

  while (input.size() != 0) {
    auto part = splitNext(input, delim);
    stripLeadingAndTrailingSpace(part);
    parts.add(kj::mv(part));
  }

  return parts;
}

kj::Array<KeyMaybeVal> toKeysAndVals(const kj::ArrayPtr<kj::ArrayPtr<const char>>& params) {
  // Given a collection of parameters (a single offer), parse the parameters into <key, MaybeValue>
  // pairs. If the parameter contains an `=`, we set the `key` to everything before, and the `value`
  // to everything after. Otherwise, we set the `key` to be the entire parameter.
  // Either way, both the key and value (if it exists) are stripped of leading & trailing whitespace.
  auto result = kj::heapArray<KeyMaybeVal>(params.size());
  size_t count = 0;
  for (const auto& param : params) {
    kj::ArrayPtr<const char> key;
    kj::Maybe<kj::ArrayPtr<const char>> value;

    KJ_IF_MAYBE(index, param.findFirst('=')) {
      // Found '=' so we have a value.
      key = param.slice(0, *index);
      stripLeadingAndTrailingSpace(key);
      value = param.slice(*index + 1, param.size());
      KJ_IF_MAYBE(v, value) {
        stripLeadingAndTrailingSpace(*v);
      }
    } else {
      key = kj::mv(param);
    }

    result[count].key = kj::mv(key);
    result[count].val = kj::mv(value);
    ++count;
  }
  return kj::mv(result);
}

struct ParamType {
  enum { CLIENT, SERVER } side;
  enum { NO_CONTEXT_TAKEOVER, MAX_WINDOW_BITS } property;
};

inline kj::Maybe<ParamType> parseKeyName(kj::ArrayPtr<const char>& key) {
  // Returns a `ParamType` struct if the `key` is valid and nullptr if invalid.

  if (key == "client_no_context_takeover"_kj) {
    return ParamType { ParamType::CLIENT, ParamType::NO_CONTEXT_TAKEOVER };
  } else if (key == "server_no_context_takeover"_kj) {
    return ParamType { ParamType::SERVER, ParamType::NO_CONTEXT_TAKEOVER };
  } else if (key == "client_max_window_bits"_kj) {
    return ParamType { ParamType::CLIENT, ParamType::MAX_WINDOW_BITS };
  } else if (key == "server_max_window_bits"_kj) {
    return ParamType { ParamType::SERVER, ParamType::MAX_WINDOW_BITS };
  }
  return nullptr;
}

kj::Maybe<UnverifiedConfig> populateUnverifiedConfig(kj::Array<KeyMaybeVal>& params) {
  // Given a collection of <key, MaybeValue> pairs, attempt to populate an `UnverifiedConfig` struct.
  // If the struct cannot be populated, we return null.
  //
  // This function populates the struct with what it finds, it does not perform bounds checking or
  // concern itself with valid `Value`s (so long as the `Value` is non-empty).
  //
  // The following issues would prevent a struct from being populated:
  //  Key issues:
  //    - `Key` is invalid (see `parseKeyName()`).
  //    - `Key` is repeated.
  //  Value issues:
  //    - Got a `Value` when none was expected (only the `max_window_bits` parameters expect values).
  //    - Got an empty `Value` (0 characters, or all whitespace characters).

  if (params.size() > 4) {
    // We expect 4 `Key`s at most, having more implies repeats/invalid keys are present.
    return nullptr;
  }

  UnverifiedConfig config;

  for (auto& param : params) {
    KJ_IF_MAYBE(paramType, parseKeyName(param.key)) {
      // `Key` is valid, but we still want to check for repeats.
      const auto& side = paramType->side;
      const auto& property = paramType->property;

      if (property == ParamType::NO_CONTEXT_TAKEOVER) {
        auto& takeOverSetting = (side == ParamType::CLIENT) ?
            config.clientNoContextTakeover : config.serverNoContextTakeover;

        if (takeOverSetting == true) {
          // This `Key` is a repeat; invalid config.
          return nullptr;
        }

        if (param.val != nullptr) {
          // The `x_no_context_takeover` parameter shouldn't have a value; invalid config.
          return nullptr;
        }

        takeOverSetting = true;
      } else if (property == ParamType::MAX_WINDOW_BITS) {
        auto& maxBitsSetting =
            (side == ParamType::CLIENT) ? config.clientMaxWindowBits : config.serverMaxWindowBits;

        if (maxBitsSetting != nullptr) {
          // This `Key` is a repeat; invalid config.
          return nullptr;
        }

        KJ_IF_MAYBE(value, param.val) {
          if (value->size() == 0) {
            // This is equivalent to `x_max_window_bits=`, since we got an "=" we expected a token
            // to follow.
            return nullptr;
          }
          maxBitsSetting = param.val;
        } else {
          // We know we got this `max_window_bits` parameter in a Request/Response, and we also know
          // that it didn't include an "=" (otherwise the value wouldn't be null).
          // It's important to retain the information that the parameter was received *without* a
          // corresponding value, as this may determine whether the offer is valid or not.
          //
          // To retain this information, we'll set `maxBitsSetting` to be an empty ArrayPtr so this
          // can be dealt with properly later.
          maxBitsSetting = ArrayPtr<const char>();
        }
      }
    } else {
      // Invalid parameter.
      return nullptr;
    }
  }
  return kj::mv(config);
}

kj::Maybe<CompressionParameters> validateCompressionConfig(UnverifiedConfig&& config,
    bool isAgreement) {
  // Verifies that the `config` is valid depending on whether we're validating a Request (offer) or
  // a Response (agreement). This essentially consumes the `UnverifiedConfig` and converts it into a
  // `CompressionParameters` struct.
  CompressionParameters result;

  KJ_IF_MAYBE(serverBits, config.serverMaxWindowBits) {
    if (serverBits->size() == 0) {
      // This means `server_max_window_bits` was passed without a value. Since a value is required,
      // this config is invalid.
      return nullptr;
    } else {
      KJ_IF_MAYBE(bits, kj::str(*serverBits).tryParseAs<size_t>()) {
        if (*bits < 8 || 15 < *bits) {
          // Out of range -- invalid.
          return nullptr;
        }
        if (isAgreement) {
          result.inboundMaxWindowBits = *bits;
        } else {
          result.outboundMaxWindowBits = *bits;
        }
      } else {
        // Invalid ABNF, expected 1*DIGIT.
        return nullptr;
      }
    }
  }

  KJ_IF_MAYBE(clientBits, config.clientMaxWindowBits) {
    if (clientBits->size() == 0) {
      if (!isAgreement) {
        // `client_max_window_bits` does not need to have a value in an offer, let's set it to 15
        // to get the best level of compression.
        result.inboundMaxWindowBits = 15;
      } else {
        // `client_max_window_bits` must have a value in a Response.
        return nullptr;
      }
    } else {
      KJ_IF_MAYBE(bits, kj::str(*clientBits).tryParseAs<size_t>()) {
        if (*bits < 8 || 15 < *bits) {
          // Out of range -- invalid.
          return nullptr;
        }
        if (isAgreement) {
          result.outboundMaxWindowBits = *bits;
        } else {
          result.inboundMaxWindowBits = *bits;
        }
      } else {
        // Invalid ABNF, expected 1*DIGIT.
        return nullptr;
      }
    }
  }

  if (isAgreement) {
    result.outboundNoContextTakeover = config.clientNoContextTakeover;
    result.inboundNoContextTakeover = config.serverNoContextTakeover;
  } else {
    result.inboundNoContextTakeover = config.clientNoContextTakeover;
    result.outboundNoContextTakeover = config.serverNoContextTakeover;
  }
  return kj::mv(result);
}

inline kj::Maybe<CompressionParameters> tryExtractParameters(
    kj::Vector<kj::ArrayPtr<const char>>& configuration,
    bool isAgreement) {
  // If the `configuration` is structured correctly and has no invalid parameters/values, we will
  // return a populated `CompressionParameters` struct.
  if (configuration.size() == 1) {
    // Plain `permessage-deflate`.
    return CompressionParameters{};
  }
  auto params = configuration.slice(1, configuration.size());
  auto keyMaybeValuePairs = toKeysAndVals(params);
  // Parse parameter strings into parameter[=value] pairs.
  auto maybeUnverified = populateUnverifiedConfig(keyMaybeValuePairs);
  KJ_IF_MAYBE(unverified, maybeUnverified) {
    // Parsing succeeded, i.e. the parameter (`key`) names are valid and we don't have
    // values for `x_no_context_takeover` parameters (the configuration is structured correctly).
    // All that's left is to check the `x_max_window_bits` values (if any are present).
    KJ_IF_MAYBE(validConfig, validateCompressionConfig(kj::mv(*unverified), isAgreement)) {
      return kj::mv(*validConfig);
    }
  }
  return nullptr;
}

kj::Vector<CompressionParameters> findValidExtensionOffers(StringPtr offers) {
  // A function to be called by the client that wants to offer extensions through
  // `Sec-WebSocket-Extensions`. This function takes the value of the header (a string) and
  // populates a Vector of all the valid offers.
  kj::Vector<CompressionParameters> result;

  auto extensions = splitParts(offers, ',');

  for (const auto& offer : extensions) {
    auto splitOffer = splitParts(offer, ';');
    if (splitOffer.front() != "permessage-deflate"_kj) {
      continue;
    }
    KJ_IF_MAYBE(validated, tryExtractParameters(splitOffer, false)) {
      // We need to swap the inbound/outbound properties since `tryExtractParameters` thinks we're
      // parsing as the server (`isAgreement` is false).
      auto tempCtx = validated->inboundNoContextTakeover;
      validated->inboundNoContextTakeover = validated->outboundNoContextTakeover;
      validated->outboundNoContextTakeover = tempCtx;
      auto tempWindow = validated->inboundMaxWindowBits;
      validated->inboundMaxWindowBits = validated->outboundMaxWindowBits;
      validated->outboundMaxWindowBits = tempWindow;
      result.add(kj::mv(*validated));
    }
  }

  return kj::mv(result);
}

kj::String generateExtensionRequest(const ArrayPtr<CompressionParameters>& extensions) {
  // Build the `Sec-WebSocket-Extensions` request from the validated parameters.
  constexpr auto EXT = "permessage-deflate"_kj;
  auto offers = kj::heapArray<String>(extensions.size());
  size_t i = 0;
  for (const auto& offer : extensions) {
    offers[i] = kj::str(EXT);
    if (offer.outboundNoContextTakeover) {
      offers[i] = kj::str(offers[i], "; client_no_context_takeover");
    }
    if (offer.inboundNoContextTakeover) {
      offers[i] = kj::str(offers[i], "; server_no_context_takeover");
    }
    if (offer.outboundMaxWindowBits != nullptr) {
      auto w = KJ_ASSERT_NONNULL(offer.outboundMaxWindowBits);
      offers[i] = kj::str(offers[i], "; client_max_window_bits=", w);
    }
    if (offer.inboundMaxWindowBits != nullptr) {
      auto w = KJ_ASSERT_NONNULL(offer.inboundMaxWindowBits);
      offers[i] = kj::str(offers[i], "; server_max_window_bits=", w);
    }
    ++i;
  }
  return kj::strArray(offers, ", ");
}

kj::Maybe<CompressionParameters> tryParseExtensionOffers(StringPtr offers) {
  // Given a string of offers, accept the first valid offer by returning a `CompressionParameters`
  // struct. If there are no valid offers, return `nullptr`.
  auto splitOffers = splitParts(offers, ',');

  for (const auto& offer : splitOffers) {
    auto splitOffer = splitParts(offer, ';');

    if (splitOffer.front() != "permessage-deflate"_kj) {
      // Extension token was invalid.
      continue;
    }
    KJ_IF_MAYBE(config, tryExtractParameters(splitOffer, false)) {
      return kj::mv(*config);
    }
  }
  return nullptr;
}

kj::Maybe<CompressionParameters> tryParseAllExtensionOffers(StringPtr offers,
    CompressionParameters manualConfig) {
  // Similar to `tryParseExtensionOffers()`, however, this function is called when parsing in
  // `MANUAL_COMPRESSION` mode. In some cases, the server's configuration might not support the
  // `server_no_context_takeover` or `server_max_window_bits` parameters. Essentially, this function
  // will look at all the client's offers, and accept the first one that it can support.
  //
  // We differentiate these functions because in `AUTOMATIC_COMPRESSION` mode, KJ can support these
  // server restricting compression parameters.
  auto splitOffers = splitParts(offers, ',');

  for (const auto& offer : splitOffers) {
    auto splitOffer = splitParts(offer, ';');

    if (splitOffer.front() != "permessage-deflate"_kj) {
      // Extension token was invalid.
      continue;
    }
    KJ_IF_MAYBE(config, tryExtractParameters(splitOffer, false)) {
      KJ_IF_MAYBE(finalConfig, compareClientAndServerConfigs(*config, manualConfig)) {
        // Found a compatible configuration between the server's config and client's offer.
        return kj::mv(*finalConfig);
      }
    }
  }
  return nullptr;
}

kj::Maybe<CompressionParameters> compareClientAndServerConfigs(CompressionParameters requestConfig,
    CompressionParameters manualConfig) {
  // We start from the `manualConfig` and go through a series of filters to get a compression
  // configuration that both the client and the server can agree upon. If no agreement can be made,
  // we return null.

  CompressionParameters acceptedParameters = manualConfig;

  // We only need to modify `client_no_context_takeover` and `server_no_context_takeover` when
  // `manualConfig` doesn't include them.
  if (manualConfig.inboundNoContextTakeover == false) {
    acceptedParameters.inboundNoContextTakeover = false;
  }

  if (manualConfig.outboundNoContextTakeover == false) {
    acceptedParameters.outboundNoContextTakeover = false;
    if (requestConfig.outboundNoContextTakeover == true) {
      // The client has told the server to not use context takeover. This is not a "hint",
      // rather it is a restriction on the server's configuration. If the server does not support
      // the configuration, it must reject the offer.
      return nullptr;
    }
  }

  // client_max_window_bits
  if (requestConfig.inboundMaxWindowBits != nullptr &&
      manualConfig.inboundMaxWindowBits != nullptr)  {
    // We want `min(requestConfig, manualConfig)` in this case.
    auto reqBits = KJ_ASSERT_NONNULL(requestConfig.inboundMaxWindowBits);
    auto manualBits = KJ_ASSERT_NONNULL(manualConfig.inboundMaxWindowBits);
    if (reqBits < manualBits) {
      acceptedParameters.inboundMaxWindowBits = reqBits;
    }
  } else {
    // We will not reply with `client_max_window_bits`.
    acceptedParameters.inboundMaxWindowBits = nullptr;
  }

  // server_max_window_bits
  if (manualConfig.outboundMaxWindowBits != nullptr) {
    auto manualBits = KJ_ASSERT_NONNULL(manualConfig.outboundMaxWindowBits);
    if (requestConfig.outboundMaxWindowBits != nullptr) {
      // We want `min(requestConfig, manualConfig)` in this case.
      auto reqBits = KJ_ASSERT_NONNULL(requestConfig.outboundMaxWindowBits);
      if (reqBits < manualBits) {
        acceptedParameters.outboundMaxWindowBits = reqBits;
      }
    }
  } else {
    acceptedParameters.outboundMaxWindowBits = nullptr;
    if (requestConfig.outboundMaxWindowBits != nullptr) {
      // The client has told the server to use `server_max_window_bits`. This is not a "hint",
      // rather it is a restriction on the server's configuration. If the server does not support
      // the configuration, it must reject the offer.
      return nullptr;
    }
  }
  return acceptedParameters;
}

kj::String generateExtensionResponse(const CompressionParameters& parameters) {
  // Build the `Sec-WebSocket-Extensions` response from the agreed parameters.
  kj::String response = kj::str("permessage-deflate");
  if (parameters.inboundNoContextTakeover) {
    response = kj::str(response, "; client_no_context_takeover");
  }
  if (parameters.outboundNoContextTakeover) {
    response = kj::str(response, "; server_no_context_takeover");
  }
  if (parameters.inboundMaxWindowBits != nullptr) {
    auto w = KJ_REQUIRE_NONNULL(parameters.inboundMaxWindowBits);
    response = kj::str(response, "; client_max_window_bits=", w);
  }
  if (parameters.outboundMaxWindowBits != nullptr) {
    auto w = KJ_REQUIRE_NONNULL(parameters.outboundMaxWindowBits);
    response = kj::str(response, "; server_max_window_bits=", w);
  }
  return kj::mv(response);
}

kj::OneOf<CompressionParameters, kj::Exception> tryParseExtensionAgreement(
    const Maybe<CompressionParameters>& clientOffer,
    StringPtr agreedParameters) {
  // Like `tryParseExtensionOffers`, but called by the client when parsing the server's Response.
  // If the client must decline the agreement, we want to provide some details about what went wrong
  // (since the client has to fail the connection).
  constexpr auto FAILURE = "Server failed WebSocket handshake: "_kj;
  auto e = KJ_EXCEPTION(FAILED);

  if (clientOffer == nullptr) {
    // We've received extensions when we did not send any in the first place.
    e.setDescription(
        kj::str(FAILURE, "added Sec-WebSocket-Extensions when client did not offer any."));
    return kj::mv(e);
  }

  auto offers = splitParts(agreedParameters, ',');
  if (offers.size() != 1) {
    constexpr auto EXPECT = "expected exactly one extension (permessage-deflate) but received "
                            "more than one."_kj;
    e.setDescription(kj::str(FAILURE, EXPECT));
    return kj::mv(e);
  }
  auto splitOffer = splitParts(offers.front(), ';');

  if (splitOffer.front() != "permessage-deflate"_kj) {
    e.setDescription(kj::str(FAILURE, "response included a Sec-WebSocket-Extensions value that was "
                                      "not permessage-deflate."));
    return kj::mv(e);
  }

  // Verify the parameters of our single extension, and compare it with the clients original offer.
  KJ_IF_MAYBE(config, tryExtractParameters(splitOffer, true)) {
    const auto& client = KJ_ASSERT_NONNULL(clientOffer);
    // The server might have ignored the client's hints regarding its compressor's configuration.
    // That's fine, but as the client, we still want to use those outbound compression parameters.
    if (config->outboundMaxWindowBits == nullptr) {
      config->outboundMaxWindowBits = client.outboundMaxWindowBits;
    } else KJ_IF_MAYBE(value, client.outboundMaxWindowBits) {
      if (*value < KJ_ASSERT_NONNULL(config->outboundMaxWindowBits)) {
        // If the client asked for a value smaller than what the server responded with, use the
        // value that the client originally specified.
        config->outboundMaxWindowBits = *value;
      }
    }
    if (config->outboundNoContextTakeover == false) {
      config->outboundNoContextTakeover = client.outboundNoContextTakeover;
    }
    return kj::mv(*config);
  }

  // There was a problem parsing the server's `Sec-WebSocket-Extensions` response.
  e.setDescription(kj::str(FAILURE, "the Sec-WebSocket-Extensions header in the Response included "
      "an invalid value."));
  return kj::mv(e);
}
} // namespace _ (private)
namespace {
class NullInputStream final: public kj::AsyncInputStream {
public:
  NullInputStream(kj::Maybe<size_t> expectedLength = size_t(0))
      : expectedLength(expectedLength) {}

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return constPromise<size_t, 0>();
  }

  kj::Maybe<uint64_t> tryGetLength() override {
    return expectedLength;
  }

  kj::Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
    return constPromise<uint64_t, 0>();
  }

private:
  kj::Maybe<size_t> expectedLength;
};

class NullOutputStream final: public kj::AsyncOutputStream {
public:
  Promise<void> write(const void* buffer, size_t size) override {
    return kj::READY_NOW;
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return kj::READY_NOW;
  }
  Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }

  // We can't really optimize tryPumpFrom() unless AsyncInputStream grows a skip() method.
};

class NullIoStream final: public kj::AsyncIoStream {
public:
  void shutdownWrite() override {}

  Promise<void> write(const void* buffer, size_t size) override {
    return kj::READY_NOW;
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return kj::READY_NOW;
  }
  Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return constPromise<size_t, 0>();
  }

  kj::Maybe<uint64_t> tryGetLength() override {
    return kj::Maybe<uint64_t>((uint64_t)0);
  }

  kj::Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
    return constPromise<uint64_t, 0>();
  }
};

class HttpClientImpl final: public HttpClient,
                            private HttpClientErrorHandler {
public:
  HttpClientImpl(const HttpHeaderTable& responseHeaderTable, kj::Own<kj::AsyncIoStream> rawStream,
                 HttpClientSettings settings)
      : httpInput(*rawStream, responseHeaderTable),
        httpOutput(*rawStream),
        ownStream(kj::mv(rawStream)),
        settings(kj::mv(settings)) {}

  bool canReuse() {
    // Returns true if we can immediately reuse this HttpClient for another message (so all
    // previous messages have been fully read).

    return !upgraded && !closed && httpInput.canReuse() && httpOutput.canReuse();
  }

  Request request(HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
                  kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
    KJ_REQUIRE(!upgraded,
        "can't make further requests on this HttpClient because it has been or is in the process "
        "of being upgraded");
    KJ_REQUIRE(!closed,
        "this HttpClient's connection has been closed by the server or due to an error");
    KJ_REQUIRE(httpOutput.canReuse(),
        "can't start new request until previous request body has been fully written");
    closeWatcherTask = nullptr;

    kj::StringPtr connectionHeaders[HttpHeaders::CONNECTION_HEADERS_COUNT];
    kj::String lengthStr;

    bool isGet = method == HttpMethod::GET || method == HttpMethod::HEAD;
    bool hasBody;

    KJ_IF_MAYBE(s, expectedBodySize) {
      if (isGet && *s == 0) {
        // GET with empty body; don't send any Content-Length.
        hasBody = false;
      } else {
        lengthStr = kj::str(*s);
        connectionHeaders[HttpHeaders::BuiltinIndices::CONTENT_LENGTH] = lengthStr;
        hasBody = true;
      }
    } else {
      if (isGet && headers.get(HttpHeaderId::TRANSFER_ENCODING) == nullptr) {
        // GET with empty body; don't send any Transfer-Encoding.
        hasBody = false;
      } else {
        // HACK: Normally GET requests shouldn't have bodies. But, if the caller set a
        //   Transfer-Encoding header on a GET, we use this as a special signal that it might
        //   actually want to send a body. This allows pass-through of a GET request with a chunked
        //   body to "just work". We strongly discourage writing any new code that sends
        //   full-bodied GETs.
        connectionHeaders[HttpHeaders::BuiltinIndices::TRANSFER_ENCODING] = "chunked";
        hasBody = true;
      }
    }

    httpOutput.writeHeaders(headers.serializeRequest(method, url, connectionHeaders));

    kj::Own<kj::AsyncOutputStream> bodyStream;
    if (!hasBody) {
      // No entity-body.
      httpOutput.finishBody();
      bodyStream = heap<HttpNullEntityWriter>();
    } else KJ_IF_MAYBE(s, expectedBodySize) {
      bodyStream = heap<HttpFixedLengthEntityWriter>(httpOutput, *s);
    } else {
      bodyStream = heap<HttpChunkedEntityWriter>(httpOutput);
    }

    auto id = ++counter;

    auto responsePromise = httpInput.readResponseHeaders().then(
        [this,method,id](HttpHeaders::ResponseOrProtocolError&& responseOrProtocolError)
            -> HttpClient::Response {
      KJ_SWITCH_ONEOF(responseOrProtocolError) {
        KJ_CASE_ONEOF(response, HttpHeaders::Response) {
          auto& responseHeaders = httpInput.getHeaders();
          HttpClient::Response result {
            response.statusCode,
            response.statusText,
            &responseHeaders,
            httpInput.getEntityBody(
                HttpInputStreamImpl::RESPONSE, method, response.statusCode, responseHeaders)
          };

          if (fastCaseCmp<'c', 'l', 'o', 's', 'e'>(
              responseHeaders.get(HttpHeaderId::CONNECTION).orDefault(nullptr).cStr())) {
            closed = true;
          } else if (counter == id) {
            watchForClose();
          } else {
            // Another request was already queued after this one, so we don't want to watch for
            // stream closure because we're fully expecting another response.
          }
          return result;
        }
        KJ_CASE_ONEOF(protocolError, HttpHeaders::ProtocolError) {
          closed = true;
          return settings.errorHandler.orDefault(*this).handleProtocolError(
              kj::mv(protocolError));
        }
      }

      KJ_UNREACHABLE;
    });

    return { kj::mv(bodyStream), kj::mv(responsePromise) };
  }

  kj::Promise<WebSocketResponse> openWebSocket(
      kj::StringPtr url, const HttpHeaders& headers) override {
    KJ_REQUIRE(!upgraded,
        "can't make further requests on this HttpClient because it has been or is in the process "
        "of being upgraded");
    KJ_REQUIRE(!closed,
        "this HttpClient's connection has been closed by the server or due to an error");
    closeWatcherTask = nullptr;

    // Mark upgraded for now, even though the upgrade could fail, because we can't allow pipelined
    // requests in the meantime.
    upgraded = true;

    byte keyBytes[16];
    KJ_ASSERT_NONNULL(settings.entropySource,
        "can't use openWebSocket() because no EntropySource was provided when creating the "
        "HttpClient").generate(keyBytes);
    auto keyBase64 = kj::encodeBase64(keyBytes);

    kj::StringPtr connectionHeaders[HttpHeaders::WEBSOCKET_CONNECTION_HEADERS_COUNT];
    connectionHeaders[HttpHeaders::BuiltinIndices::CONNECTION] = "Upgrade";
    connectionHeaders[HttpHeaders::BuiltinIndices::UPGRADE] = "websocket";
    connectionHeaders[HttpHeaders::BuiltinIndices::SEC_WEBSOCKET_VERSION] = "13";
    connectionHeaders[HttpHeaders::BuiltinIndices::SEC_WEBSOCKET_KEY] = keyBase64;

    kj::Maybe<kj::String> offeredExtensions;
    kj::Maybe<CompressionParameters> clientOffer;
    kj::Vector<CompressionParameters> extensions;
    auto compressionMode = settings.webSocketCompressionMode;

    if (compressionMode == HttpClientSettings::MANUAL_COMPRESSION) {
      KJ_IF_MAYBE(value, headers.get(HttpHeaderId::SEC_WEBSOCKET_EXTENSIONS)) {
        // Strip all `Sec-WebSocket-Extensions` except for `permessage-deflate`.
        extensions = _::findValidExtensionOffers(*value);
      }
    } else if (compressionMode == HttpClientSettings::AUTOMATIC_COMPRESSION) {
      // If AUTOMATIC_COMPRESSION is enabled, we send `Sec-WebSocket-Extensions: permessage-deflate`
      // to the server and ignore the `headers` provided by the caller.
      extensions.add(CompressionParameters());
    }

    if (extensions.size() > 0) {
      clientOffer = extensions.front();
      // We hold on to a copy of the client's most preferred offer so even if the server
      // ignores `client_no_context_takeover` or `client_max_window_bits`, we can still refer to
      // the original offer made by the client (thereby allowing the client to use these parameters).
      //
      // It's safe to ignore the remaining offers because:
      //  1. Offers are ordered by preference.
      //  2. `client_x` parameters are hints to the server and do not result in rejections, so the
      //     client is likely to put them in every offer anyways.
      connectionHeaders[HttpHeaders::BuiltinIndices::SEC_WEBSOCKET_EXTENSIONS] =
          offeredExtensions.emplace(_::generateExtensionRequest(extensions.asPtr()));
    }

    httpOutput.writeHeaders(headers.serializeRequest(HttpMethod::GET, url, connectionHeaders));

    // No entity-body.
    httpOutput.finishBody();

    auto id = ++counter;

    return httpInput.readResponseHeaders()
        .then([this,id,keyBase64 = kj::mv(keyBase64),clientOffer = kj::mv(clientOffer)](
            HttpHeaders::ResponseOrProtocolError&& responseOrProtocolError)
            -> HttpClient::WebSocketResponse {
      KJ_SWITCH_ONEOF(responseOrProtocolError) {
        KJ_CASE_ONEOF(response, HttpHeaders::Response) {
          auto& responseHeaders = httpInput.getHeaders();
          if (response.statusCode == 101) {
            if (!fastCaseCmp<'w', 'e', 'b', 's', 'o', 'c', 'k', 'e', 't'>(
                    responseHeaders.get(HttpHeaderId::UPGRADE).orDefault(nullptr).cStr())) {
              kj::String ownMessage;
              kj::StringPtr message;
              KJ_IF_MAYBE(actual, responseHeaders.get(HttpHeaderId::UPGRADE)) {
                ownMessage = kj::str(
                    "Server failed WebSocket handshake: incorrect Upgrade header: "
                    "expected 'websocket', got '", *actual, "'.");
                message = ownMessage;
              } else {
                message = "Server failed WebSocket handshake: missing Upgrade header.";
              }
              return settings.errorHandler.orDefault(*this).handleWebSocketProtocolError({
                502, "Bad Gateway", message, nullptr
              });
            }

            auto expectedAccept = generateWebSocketAccept(keyBase64);
            if (responseHeaders.get(HttpHeaderId::SEC_WEBSOCKET_ACCEPT).orDefault(nullptr)
                  != expectedAccept) {
              kj::String ownMessage;
              kj::StringPtr message;
              KJ_IF_MAYBE(actual, responseHeaders.get(HttpHeaderId::SEC_WEBSOCKET_ACCEPT)) {
                ownMessage = kj::str(
                    "Server failed WebSocket handshake: incorrect Sec-WebSocket-Accept header: "
                    "expected '", expectedAccept, "', got '", *actual, "'.");
                message = ownMessage;
              } else {
                message = "Server failed WebSocket handshake: missing Upgrade header.";
              }
              return settings.errorHandler.orDefault(*this).handleWebSocketProtocolError({
                502, "Bad Gateway", message, nullptr
              });
            }

            kj::Maybe<CompressionParameters> compressionParameters;
            if (settings.webSocketCompressionMode != HttpClientSettings::NO_COMPRESSION) {
              KJ_IF_MAYBE(agreedParameters, responseHeaders.get(
                  HttpHeaderId::SEC_WEBSOCKET_EXTENSIONS)) {

                auto parseResult = _::tryParseExtensionAgreement(clientOffer,
                    *agreedParameters);
                if (parseResult.is<kj::Exception>()) {
                  return settings.errorHandler.orDefault(*this).handleWebSocketProtocolError({
                    502, "Bad Gateway", parseResult.get<kj::Exception>().getDescription(), nullptr});
                }
                compressionParameters.emplace(kj::mv(parseResult.get<CompressionParameters>()));
              }
            }

            return {
              response.statusCode,
              response.statusText,
              &httpInput.getHeaders(),
              upgradeToWebSocket(kj::mv(ownStream), httpInput, httpOutput, settings.entropySource,
                  kj::mv(compressionParameters)),
            };
          } else {
            upgraded = false;
            HttpClient::WebSocketResponse result {
              response.statusCode,
              response.statusText,
              &responseHeaders,
              httpInput.getEntityBody(HttpInputStreamImpl::RESPONSE, HttpMethod::GET,
                                      response.statusCode, responseHeaders)
            };
            if (fastCaseCmp<'c', 'l', 'o', 's', 'e'>(
                responseHeaders.get(HttpHeaderId::CONNECTION).orDefault(nullptr).cStr())) {
              closed = true;
            } else if (counter == id) {
              watchForClose();
            } else {
              // Another request was already queued after this one, so we don't want to watch for
              // stream closure because we're fully expecting another response.
            }
            return result;
          }
        }
        KJ_CASE_ONEOF(protocolError, HttpHeaders::ProtocolError) {
          return settings.errorHandler.orDefault(*this).handleWebSocketProtocolError(
              kj::mv(protocolError));
        }
      }

      KJ_UNREACHABLE;
    });
  }

  ConnectRequest connect(
      kj::StringPtr host, const HttpHeaders& headers, HttpConnectSettings settings) override {
    KJ_REQUIRE(!upgraded,
        "can't make further requests on this HttpClient because it has been or is in the process "
        "of being upgraded");
    KJ_REQUIRE(!closed,
        "this HttpClient's connection has been closed by the server or due to an error");
    KJ_REQUIRE(httpOutput.canReuse(),
        "can't start new request until previous request body has been fully written");

    if (settings.useTls) {
      KJ_UNIMPLEMENTED("This HttpClient does not support TLS.");
    }

    closeWatcherTask = nullptr;

    // Mark upgraded for now even though the tunnel could fail, because we can't allow pipelined
    // requests in the meantime.
    upgraded = true;

    kj::StringPtr connectionHeaders[HttpHeaders::CONNECTION_HEADERS_COUNT];

    httpOutput.writeHeaders(headers.serializeConnectRequest(host, connectionHeaders));

    auto id = ++counter;

    auto split = httpInput.readResponseHeaders().then(
        [this, id](HttpHeaders::ResponseOrProtocolError&& responseOrProtocolError) mutable
            -> kj::Tuple<kj::Promise<ConnectRequest::Status>,
                         kj::Promise<kj::Maybe<HttpInputStreamImpl::ReleasedBuffer>>> {
      KJ_SWITCH_ONEOF(responseOrProtocolError) {
        KJ_CASE_ONEOF(response, HttpHeaders::Response) {
          auto& responseHeaders = httpInput.getHeaders();
          if (response.statusCode < 200 || response.statusCode >= 300) {
            // Any statusCode that is not in the 2xx range in interpreted
            // as an HTTP response. Any status code in the 2xx range is
            // interpreted as a successful CONNECT response.
            closed = true;
            return kj::tuple(ConnectRequest::Status(
              response.statusCode,
              kj::str(response.statusText),
              kj::heap(responseHeaders.clone()),
              httpInput.getEntityBody(
                  HttpInputStreamImpl::RESPONSE,
                  HttpConnectMethod(),
                  response.statusCode,
                  responseHeaders)),
              KJ_EXCEPTION(DISCONNECTED, "the connect request was rejected"));
          }
          KJ_ASSERT(counter == id);
          return kj::tuple(ConnectRequest::Status(
            response.statusCode,
            kj::str(response.statusText),
            kj::heap(responseHeaders.clone())
          ), kj::Maybe<HttpInputStreamImpl::ReleasedBuffer>(httpInput.releaseBuffer()));
        }
        KJ_CASE_ONEOF(protocolError, HttpHeaders::ProtocolError) {
          closed = true;
          auto response = handleProtocolError(protocolError);
          return kj::tuple(ConnectRequest::Status(
            response.statusCode,
            kj::str(response.statusText),
            kj::heap(response.headers->clone()),
            kj::mv(response.body)
          ), KJ_EXCEPTION(DISCONNECTED, "the connect request errored"));
        }
      }
      KJ_UNREACHABLE;
    }).split();

    return ConnectRequest {
      kj::mv(kj::get<0>(split)),  // Promise for the result
      heap<AsyncIoStreamWithGuards>(
          kj::mv(ownStream),
          kj::mv(kj::get<1>(split)) /* read guard (Promise for the ReleasedBuffer) */,
          httpOutput.flush() /* write guard (void Promise) */)
    };
  }

private:
  HttpInputStreamImpl httpInput;
  HttpOutputStream httpOutput;
  kj::Own<AsyncIoStream> ownStream;
  HttpClientSettings settings;
  kj::Maybe<kj::Promise<void>> closeWatcherTask;
  bool upgraded = false;
  bool closed = false;

  uint counter = 0;
  // Counts requests for the sole purpose of detecting if more requests have been made after some
  // point in history.

  void watchForClose() {
    closeWatcherTask = httpInput.awaitNextMessage()
        .then([this](bool hasData) -> kj::Promise<void> {
      if (hasData) {
        // Uhh... The server sent some data before we asked for anything. Perhaps due to properties
        // of this application, the server somehow already knows what the next request will be, and
        // it is trying to optimize. Or maybe this is some sort of test and the server is just
        // replaying a script. In any case, we will humor it -- leave the data in the buffer and
        // let it become the response to the next request.
        return kj::READY_NOW;
      } else {
        // EOF -- server disconnected.
        closed = true;
        if (httpOutput.isInBody()) {
          // Huh, the application is still sending a request. We should let it finish. We do not
          // need to proactively free the socket in this case because we know that we're not
          // sitting in a reusable connection pool, because we know the application is still
          // actively using the connection.
          return kj::READY_NOW;
        } else {
          return httpOutput.flush().then([this]() {
            // We might be sitting in NetworkAddressHttpClient's `availableClients` pool. We don't
            // have a way to notify it to remove this client from the pool; instead, when it tries
            // to pull this client from the pool later, it will notice the client is dead and will
            // discard it then. But, we would like to avoid holding on to a socket forever. So,
            // destroy the socket now.
            // TODO(cleanup): Maybe we should arrange to proactively remove ourselves? Seems
            //   like the code will be awkward.
            ownStream = nullptr;
          });
        }
      }
    }).eagerlyEvaluate(nullptr);
  }
};

}  // namespace

kj::Promise<HttpClient::WebSocketResponse> HttpClient::openWebSocket(
    kj::StringPtr url, const HttpHeaders& headers) {
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

HttpClient::ConnectRequest HttpClient::connect(
    kj::StringPtr host, const HttpHeaders& headers, HttpConnectSettings settings) {
  KJ_UNIMPLEMENTED("CONNECT is not implemented by this HttpClient");
}

kj::Own<HttpClient> newHttpClient(
    const HttpHeaderTable& responseHeaderTable, kj::AsyncIoStream& stream,
    HttpClientSettings settings) {
  return kj::heap<HttpClientImpl>(responseHeaderTable,
      kj::Own<kj::AsyncIoStream>(&stream, kj::NullDisposer::instance),
      kj::mv(settings));
}

HttpClient::Response HttpClientErrorHandler::handleProtocolError(
      HttpHeaders::ProtocolError protocolError) {
  KJ_FAIL_REQUIRE(protocolError.description) { break; }
  return HttpClient::Response();
}

HttpClient::WebSocketResponse HttpClientErrorHandler::handleWebSocketProtocolError(
      HttpHeaders::ProtocolError protocolError) {
  auto response = handleProtocolError(protocolError);
  return HttpClient::WebSocketResponse {
    response.statusCode, response.statusText, response.headers, kj::mv(response.body)
  };
}

kj::Exception WebSocketErrorHandler::handleWebSocketProtocolError(
      WebSocket::ProtocolError protocolError) {
  return KJ_EXCEPTION(FAILED, "WebSocket protocol error", protocolError.statusCode, protocolError.description);
}

class PausableReadAsyncIoStream::PausableRead {
public:
  PausableRead(
      kj::PromiseFulfiller<size_t>& fulfiller, PausableReadAsyncIoStream& parent,
      void* buffer, size_t minBytes, size_t maxBytes)
      : fulfiller(fulfiller), parent(parent),
        operationBuffer(buffer), operationMinBytes(minBytes), operationMaxBytes(maxBytes),
        innerRead(parent.tryReadImpl(operationBuffer, operationMinBytes, operationMaxBytes).then(
            [&fulfiller](size_t size) mutable -> kj::Promise<void> {
          fulfiller.fulfill(kj::mv(size));
          return kj::READY_NOW;
        }, [&fulfiller](kj::Exception&& err) {
          fulfiller.reject(kj::mv(err));
        })) {
    KJ_ASSERT(parent.maybePausableRead == nullptr);
    parent.maybePausableRead = *this;
  }

  ~PausableRead() noexcept(false) {
    parent.maybePausableRead = nullptr;
  }

  void pause() {
    innerRead = nullptr;
  }

  void unpause() {
    innerRead = parent.tryReadImpl(operationBuffer, operationMinBytes, operationMaxBytes).then(
        [this](size_t size) -> kj::Promise<void> {
      fulfiller.fulfill(kj::mv(size));
      return kj::READY_NOW;
    }, [this](kj::Exception&& err) {
      fulfiller.reject(kj::mv(err));
    });
  }

  void reject(kj::Exception&& exc) {
    fulfiller.reject(kj::mv(exc));
  }
private:
  kj::PromiseFulfiller<size_t>& fulfiller;
  PausableReadAsyncIoStream& parent;

  void* operationBuffer;
  size_t operationMinBytes;
  size_t operationMaxBytes;
  // The parameters of the current tryRead call. Used to unpause a paused read.

  kj::Promise<void> innerRead;
  // The current pending read.
};

_::Deferred<kj::Function<void()>> PausableReadAsyncIoStream::trackRead() {
  KJ_REQUIRE(!currentlyReading, "only one read is allowed at any one time");
  currentlyReading = true;
  return kj::defer<kj::Function<void()>>([this]() { currentlyReading = false; });
}

_::Deferred<kj::Function<void()>> PausableReadAsyncIoStream::trackWrite() {
  KJ_REQUIRE(!currentlyWriting, "only one write is allowed at any one time");
  currentlyWriting = true;
  return kj::defer<kj::Function<void()>>([this]() { currentlyWriting = false; });
}

kj::Promise<size_t> PausableReadAsyncIoStream::tryRead(
    void* buffer, size_t minBytes, size_t maxBytes) {
  return kj::newAdaptedPromise<size_t, PausableRead>(*this, buffer, minBytes, maxBytes);
}

kj::Promise<size_t> PausableReadAsyncIoStream::tryReadImpl(
    void* buffer, size_t minBytes, size_t maxBytes) {
  // Hack: evalNow used here because `newAdaptedPromise` has a bug. We may need to change
  // `PromiseDisposer::alloc` to not be `noexcept` but in order to do so we'll need to benchmark
  // its performance.
  return kj::evalNow([&]() -> kj::Promise<size_t> {
    return inner->tryRead(buffer, minBytes, maxBytes).attach(trackRead());
  });
}

kj::Maybe<uint64_t> PausableReadAsyncIoStream::tryGetLength() {
  return inner->tryGetLength();
}

kj::Promise<uint64_t> PausableReadAsyncIoStream::pumpTo(
    kj::AsyncOutputStream& output, uint64_t amount) {
  return kj::unoptimizedPumpTo(*this, output, amount);
}

kj::Promise<void> PausableReadAsyncIoStream::write(const void* buffer, size_t size) {
  return inner->write(buffer, size).attach(trackWrite());
}

kj::Promise<void> PausableReadAsyncIoStream::write(
    kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) {
  return inner->write(pieces).attach(trackWrite());
}

kj::Maybe<kj::Promise<uint64_t>> PausableReadAsyncIoStream::tryPumpFrom(
    kj::AsyncInputStream& input, uint64_t amount) {
  auto result = inner->tryPumpFrom(input, amount);
  KJ_IF_MAYBE(r, result) {
    return r->attach(trackWrite());
  } else {
    return nullptr;
  }
}

kj::Promise<void> PausableReadAsyncIoStream::whenWriteDisconnected() {
  return inner->whenWriteDisconnected();
}

void PausableReadAsyncIoStream::shutdownWrite() {
  inner->shutdownWrite();
}

void PausableReadAsyncIoStream::abortRead() {
  inner->abortRead();
}

kj::Maybe<int> PausableReadAsyncIoStream::getFd() const {
  return inner->getFd();
}

void PausableReadAsyncIoStream::pause() {
  KJ_IF_MAYBE(pausable, maybePausableRead) {
    pausable->pause();
  }
}

void PausableReadAsyncIoStream::unpause() {
  KJ_IF_MAYBE(pausable, maybePausableRead) {
    pausable->unpause();
  }
}

bool PausableReadAsyncIoStream::getCurrentlyReading() {
  return currentlyReading;
}

bool PausableReadAsyncIoStream::getCurrentlyWriting() {
  return currentlyWriting;
}

kj::Own<kj::AsyncIoStream> PausableReadAsyncIoStream::takeStream() {
  return kj::mv(inner);
}

void PausableReadAsyncIoStream::replaceStream(kj::Own<kj::AsyncIoStream> stream) {
  inner = kj::mv(stream);
}

void PausableReadAsyncIoStream::reject(kj::Exception&& exc) {
  KJ_IF_MAYBE(pausable, maybePausableRead) {
    pausable->reject(kj::mv(exc));
  }
}

// =======================================================================================

namespace {

class NetworkAddressHttpClient final: public HttpClient {
public:
  NetworkAddressHttpClient(kj::Timer& timer, const HttpHeaderTable& responseHeaderTable,
                           kj::Own<kj::NetworkAddress> address, HttpClientSettings settings)
      : timer(timer),
        responseHeaderTable(responseHeaderTable),
        address(kj::mv(address)),
        settings(kj::mv(settings)) {}

  bool isDrained() {
    // Returns true if there are no open connections.
    return activeConnectionCount == 0 && availableClients.empty();
  }

  kj::Promise<void> onDrained() {
    // Returns a promise which resolves the next time isDrained() transitions from false to true.
    auto paf = kj::newPromiseAndFulfiller<void>();
    drainedFulfiller = kj::mv(paf.fulfiller);
    return kj::mv(paf.promise);
  }

  Request request(HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
                  kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
    auto refcounted = getClient();
    auto result = refcounted->client->request(method, url, headers, expectedBodySize);
    result.body = result.body.attach(kj::addRef(*refcounted));
    result.response = result.response.then(
        [refcounted=kj::mv(refcounted)](Response&& response) mutable {
      response.body = response.body.attach(kj::mv(refcounted));
      return kj::mv(response);
    });
    return result;
  }

  kj::Promise<WebSocketResponse> openWebSocket(
      kj::StringPtr url, const HttpHeaders& headers) override {
    auto refcounted = getClient();
    auto result = refcounted->client->openWebSocket(url, headers);
    return result.then(
        [refcounted=kj::mv(refcounted)](WebSocketResponse&& response) mutable {
      KJ_SWITCH_ONEOF(response.webSocketOrBody) {
        KJ_CASE_ONEOF(body, kj::Own<kj::AsyncInputStream>) {
          response.webSocketOrBody = body.attach(kj::mv(refcounted));
        }
        KJ_CASE_ONEOF(ws, kj::Own<WebSocket>) {
          // The only reason we need to attach the client to the WebSocket is because otherwise
          // the response headers will be deleted prematurely. Otherwise, the WebSocket has taken
          // ownership of the connection.
          //
          // TODO(perf): Maybe we could transfer ownership of the response headers specifically?
          response.webSocketOrBody = ws.attach(kj::mv(refcounted));
        }
      }
      return kj::mv(response);
    });
  }

  ConnectRequest connect(
      kj::StringPtr host, const HttpHeaders& headers, HttpConnectSettings settings) override {
    auto refcounted = getClient();
    auto request = refcounted->client->connect(host, headers, settings);
    return ConnectRequest {
      request.status.attach(kj::addRef(*refcounted)),
      request.connection.attach(kj::mv(refcounted))
    };
  }

private:
  kj::Timer& timer;
  const HttpHeaderTable& responseHeaderTable;
  kj::Own<kj::NetworkAddress> address;
  HttpClientSettings settings;

  kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> drainedFulfiller;
  uint activeConnectionCount = 0;

  bool timeoutsScheduled = false;
  kj::Promise<void> timeoutTask = nullptr;

  struct AvailableClient {
    kj::Own<HttpClientImpl> client;
    kj::TimePoint expires;
  };

  std::deque<AvailableClient> availableClients;

  struct RefcountedClient final: public kj::Refcounted {
    RefcountedClient(NetworkAddressHttpClient& parent, kj::Own<HttpClientImpl> client)
        : parent(parent), client(kj::mv(client)) {
      ++parent.activeConnectionCount;
    }
    ~RefcountedClient() noexcept(false) {
      --parent.activeConnectionCount;
      KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
        parent.returnClientToAvailable(kj::mv(client));
      })) {
        KJ_LOG(ERROR, *exception);
      }
    }

    NetworkAddressHttpClient& parent;
    kj::Own<HttpClientImpl> client;
  };

  kj::Own<RefcountedClient> getClient() {
    for (;;) {
      if (availableClients.empty()) {
        auto stream = newPromisedStream(address->connect());
        return kj::refcounted<RefcountedClient>(*this,
          kj::heap<HttpClientImpl>(responseHeaderTable, kj::mv(stream), settings));
      } else {
        auto client = kj::mv(availableClients.back().client);
        availableClients.pop_back();
        if (client->canReuse()) {
          return kj::refcounted<RefcountedClient>(*this, kj::mv(client));
        }
        // Whoops, this client's connection was closed by the server at some point. Discard.
      }
    }
  }

  void returnClientToAvailable(kj::Own<HttpClientImpl> client) {
    // Only return the connection to the pool if it is reusable and if our settings indicate we
    // should reuse connections.
    if (client->canReuse() && settings.idleTimeout > 0 * kj::SECONDS) {
      availableClients.push_back(AvailableClient {
        kj::mv(client), timer.now() + settings.idleTimeout
      });
    }

    // Call this either way because it also signals onDrained().
    if (!timeoutsScheduled) {
      timeoutsScheduled = true;
      timeoutTask = applyTimeouts();
    }
  }

  kj::Promise<void> applyTimeouts() {
    if (availableClients.empty()) {
      timeoutsScheduled = false;
      if (activeConnectionCount == 0) {
        KJ_IF_MAYBE(f, drainedFulfiller) {
          f->get()->fulfill();
          drainedFulfiller = nullptr;
        }
      }
      return kj::READY_NOW;
    } else {
      auto time = availableClients.front().expires;
      return timer.atTime(time).then([this,time]() {
        while (!availableClients.empty() && availableClients.front().expires <= time) {
          availableClients.pop_front();
        }
        return applyTimeouts();
      });
    }
  }
};

class TransitionaryAsyncIoStream final: public kj::AsyncIoStream {
  // This specialised AsyncIoStream is used by NetworkHttpClient to support startTls.
public:
  TransitionaryAsyncIoStream(kj::Own<kj::AsyncIoStream> unencryptedStream)
      : inner(kj::heap<kj::PausableReadAsyncIoStream>(kj::mv(unencryptedStream))) {}

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return inner->tryRead(buffer, minBytes, maxBytes);
  }

  kj::Maybe<uint64_t> tryGetLength() override {
    return inner->tryGetLength();
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

  kj::Promise<void> whenWriteDisconnected() override {
    return inner->whenWriteDisconnected();
  }

  void shutdownWrite() override {
    inner->shutdownWrite();
  }

  void abortRead() override {
    inner->abortRead();
  }

  kj::Maybe<int> getFd() const override {
    return inner->getFd();
  }

  void startTls(
      kj::SecureNetworkWrapper* wrapper, kj::StringPtr expectedServerHostname) {
    // Pause any potential pending reads.
    inner->pause();

    KJ_ON_SCOPE_FAILURE({
      inner->reject(KJ_EXCEPTION(FAILED, "StartTls failed."));
    });

    KJ_ASSERT(!inner->getCurrentlyReading() && !inner->getCurrentlyWriting(),
        "Cannot call startTls while reads/writes are outstanding");
    kj::Promise<kj::Own<kj::AsyncIoStream>> secureStream =
        wrapper->wrapClient(inner->takeStream(), expectedServerHostname);
    inner->replaceStream(kj::newPromisedStream(kj::mv(secureStream)));
    // Resume any previous pending reads.
    inner->unpause();
  }

private:
  kj::Own<kj::PausableReadAsyncIoStream> inner;
};

class PromiseNetworkAddressHttpClient final: public HttpClient {
  // An HttpClient which waits for a promise to resolve then forwards all calls to the promised
  // client.

public:
  PromiseNetworkAddressHttpClient(kj::Promise<kj::Own<NetworkAddressHttpClient>> promise)
      : promise(promise.then([this](kj::Own<NetworkAddressHttpClient>&& client) {
          this->client = kj::mv(client);
        }).fork()) {}

  bool isDrained() {
    KJ_IF_MAYBE(c, client) {
      return c->get()->isDrained();
    } else {
      return failed;
    }
  }

  kj::Promise<void> onDrained() {
    KJ_IF_MAYBE(c, client) {
      return c->get()->onDrained();
    } else {
      return promise.addBranch().then([this]() {
        return KJ_ASSERT_NONNULL(client)->onDrained();
      }, [this](kj::Exception&& e) {
        // Connecting failed. Treat as immediately drained.
        failed = true;
        return kj::READY_NOW;
      });
    }
  }

  Request request(HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
                  kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
    KJ_IF_MAYBE(c, client) {
      return c->get()->request(method, url, headers, expectedBodySize);
    } else {
      // This gets complicated since request() returns a pair of a stream and a promise.
      auto urlCopy = kj::str(url);
      auto headersCopy = headers.clone();
      auto combined = promise.addBranch().then(
          [this,method,expectedBodySize,url=kj::mv(urlCopy), headers=kj::mv(headersCopy)]()
          -> kj::Tuple<kj::Own<kj::AsyncOutputStream>, kj::Promise<Response>> {
        auto req = KJ_ASSERT_NONNULL(client)->request(method, url, headers, expectedBodySize);
        return kj::tuple(kj::mv(req.body), kj::mv(req.response));
      });

      auto split = combined.split();
      return {
        newPromisedStream(kj::mv(kj::get<0>(split))),
        kj::mv(kj::get<1>(split))
      };
    }
  }

  kj::Promise<WebSocketResponse> openWebSocket(
      kj::StringPtr url, const HttpHeaders& headers) override {
    KJ_IF_MAYBE(c, client) {
      return c->get()->openWebSocket(url, headers);
    } else {
      auto urlCopy = kj::str(url);
      auto headersCopy = headers.clone();
      return promise.addBranch().then(
          [this,url=kj::mv(urlCopy),headers=kj::mv(headersCopy)]() {
        return KJ_ASSERT_NONNULL(client)->openWebSocket(url, headers);
      });
    }
  }

  ConnectRequest connect(
      kj::StringPtr host, const HttpHeaders& headers, HttpConnectSettings settings) override {
    KJ_IF_MAYBE(c, client) {
      return c->get()->connect(host, headers, settings);
    } else {
      auto split = promise.addBranch().then(
          [this, host=kj::str(host), headers=headers.clone(), settings]() mutable
          -> kj::Tuple<kj::Promise<ConnectRequest::Status>,
                       kj::Promise<kj::Own<kj::AsyncIoStream>>> {
        auto request = KJ_ASSERT_NONNULL(client)->connect(host, headers, kj::mv(settings));
        return kj::tuple(kj::mv(request.status), kj::mv(request.connection));
      }).split();

      return ConnectRequest {
        kj::mv(kj::get<0>(split)),
        kj::newPromisedStream(kj::mv(kj::get<1>(split)))
      };
    }
  }

private:
  kj::ForkedPromise<void> promise;
  kj::Maybe<kj::Own<NetworkAddressHttpClient>> client;
  bool failed = false;
};

class NetworkHttpClient final: public HttpClient, private kj::TaskSet::ErrorHandler {
public:
  NetworkHttpClient(kj::Timer& timer, const HttpHeaderTable& responseHeaderTable,
                    kj::Network& network, kj::Maybe<kj::Network&> tlsNetwork,
                    HttpClientSettings settings)
      : timer(timer),
        responseHeaderTable(responseHeaderTable),
        network(network),
        tlsNetwork(tlsNetwork),
        settings(kj::mv(settings)),
        tasks(*this) {}

  Request request(HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
                  kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
    // We need to parse the proxy-style URL to convert it to host-style.
    // Use URL parsing options that avoid unnecessary rewrites.
    Url::Options urlOptions;
    urlOptions.allowEmpty = true;
    urlOptions.percentDecode = false;

    auto parsed = Url::parse(url, Url::HTTP_PROXY_REQUEST, urlOptions);
    auto path = parsed.toString(Url::HTTP_REQUEST);
    auto headersCopy = headers.clone();
    headersCopy.set(HttpHeaderId::HOST, parsed.host);
    return getClient(parsed).request(method, path, headersCopy, expectedBodySize);
  }

  kj::Promise<WebSocketResponse> openWebSocket(
      kj::StringPtr url, const HttpHeaders& headers) override {
    // We need to parse the proxy-style URL to convert it to host-style.
    // Use URL parsing options that avoid unnecessary rewrites.
    Url::Options urlOptions;
    urlOptions.allowEmpty = true;
    urlOptions.percentDecode = false;

    auto parsed = Url::parse(url, Url::HTTP_PROXY_REQUEST, urlOptions);
    auto path = parsed.toString(Url::HTTP_REQUEST);
    auto headersCopy = headers.clone();
    headersCopy.set(HttpHeaderId::HOST, parsed.host);
    return getClient(parsed).openWebSocket(path, headersCopy);
  }

  ConnectRequest connect(
      kj::StringPtr host, const HttpHeaders& headers,
      HttpConnectSettings connectSettings) override {
    // We want to connect directly instead of going through a proxy here.
    // https://github.com/capnproto/capnproto/pull/1454#discussion_r900414879
    kj::Maybe<kj::Promise<kj::Own<kj::NetworkAddress>>> addr;
    if (connectSettings.useTls) {
      kj::Network& tlsNet = KJ_REQUIRE_NONNULL(tlsNetwork, "this HttpClient doesn't support TLS");
      addr = tlsNet.parseAddress(host);
    } else {
      addr = network.parseAddress(host);
    }

    auto split = KJ_ASSERT_NONNULL(addr).then([this](auto address) {
      return address->connect().then([this](auto connection)
          -> kj::Tuple<kj::Promise<ConnectRequest::Status>,
                       kj::Promise<kj::Own<kj::AsyncIoStream>>> {
        return kj::tuple(
            ConnectRequest::Status(
              200,
              kj::str("OK"),
              kj::heap<kj::HttpHeaders>(responseHeaderTable) // Empty headers
            ),
            kj::mv(connection));
      }).attach(kj::mv(address));
    }).split();

    auto connection = kj::newPromisedStream(kj::mv(kj::get<1>(split)));

    if (!connectSettings.useTls) {
      KJ_IF_MAYBE(wrapper, settings.tlsContext) {
        KJ_IF_MAYBE(tlsStarter, connectSettings.tlsStarter) {
          auto transitConnectionRef = kj::refcountedWrapper(
              kj::heap<TransitionaryAsyncIoStream>(kj::mv(connection)));
          Function<kj::Promise<void>(kj::StringPtr)> cb =
              [wrapper, ref1 = transitConnectionRef->addWrappedRef()](
              kj::StringPtr expectedServerHostname) mutable {
            ref1->startTls(wrapper, expectedServerHostname);
            return kj::READY_NOW;
          };
          connection = transitConnectionRef->addWrappedRef();
          *tlsStarter = kj::mv(cb);
        }
      }
    }

    return ConnectRequest {
      kj::mv(kj::get<0>(split)),
      kj::mv(connection)
    };
  }

private:
  kj::Timer& timer;
  const HttpHeaderTable& responseHeaderTable;
  kj::Network& network;
  kj::Maybe<kj::Network&> tlsNetwork;
  HttpClientSettings settings;

  struct Host {
    kj::String name;  // including port, if non-default
    kj::Own<PromiseNetworkAddressHttpClient> client;
  };

  std::map<kj::StringPtr, Host> httpHosts;
  std::map<kj::StringPtr, Host> httpsHosts;

  struct RequestInfo {
    HttpMethod method;
    kj::String hostname;
    kj::String path;
    HttpHeaders headers;
    kj::Maybe<uint64_t> expectedBodySize;
  };

  kj::TaskSet tasks;

  HttpClient& getClient(kj::Url& parsed) {
    bool isHttps = parsed.scheme == "https";
    bool isHttp = parsed.scheme == "http";
    KJ_REQUIRE(isHttp || isHttps);

    auto& hosts = isHttps ? httpsHosts : httpHosts;

    // Look for a cached client for this host.
    // TODO(perf): It would be nice to recognize when different hosts have the same address and
    //   reuse the same connection pool, but:
    //   - We'd need a reliable way to compare NetworkAddresses, e.g. .equals() and .hashCode().
    //     It's very Java... ick.
    //   - Correctly handling TLS would be tricky: we'd need to verify that the new hostname is
    //     on the certificate. When SNI is in use we might have to request an additional
    //     certificate (is that possible?).
    auto iter = hosts.find(parsed.host);

    if (iter == hosts.end()) {
      // Need to open a new connection.
      kj::Network* networkToUse = &network;
      if (isHttps) {
        networkToUse = &KJ_REQUIRE_NONNULL(tlsNetwork, "this HttpClient doesn't support HTTPS");
      }

      auto promise = networkToUse->parseAddress(parsed.host, isHttps ? 443 : 80)
          .then([this](kj::Own<kj::NetworkAddress> addr) {
        return kj::heap<NetworkAddressHttpClient>(
            timer, responseHeaderTable, kj::mv(addr), settings);
      });

      Host host {
        kj::mv(parsed.host),
        kj::heap<PromiseNetworkAddressHttpClient>(kj::mv(promise))
      };
      kj::StringPtr nameRef = host.name;

      auto insertResult = hosts.insert(std::make_pair(nameRef, kj::mv(host)));
      KJ_ASSERT(insertResult.second);
      iter = insertResult.first;

      tasks.add(handleCleanup(hosts, iter));
    }

    return *iter->second.client;
  }

  kj::Promise<void> handleCleanup(std::map<kj::StringPtr, Host>& hosts,
                                  std::map<kj::StringPtr, Host>::iterator iter) {
    return iter->second.client->onDrained()
        .then([this,&hosts,iter]() -> kj::Promise<void> {
      // Double-check that it's really drained to avoid race conditions.
      if (iter->second.client->isDrained()) {
        hosts.erase(iter);
        return kj::READY_NOW;
      } else {
        return handleCleanup(hosts, iter);
      }
    });
  }

  void taskFailed(kj::Exception&& exception) override {
    KJ_LOG(ERROR, exception);
  }
};

}  // namespace

kj::Own<HttpClient> newHttpClient(kj::Timer& timer, const HttpHeaderTable& responseHeaderTable,
                                  kj::NetworkAddress& addr, HttpClientSettings settings) {
  return kj::heap<NetworkAddressHttpClient>(timer, responseHeaderTable,
      kj::Own<kj::NetworkAddress>(&addr, kj::NullDisposer::instance), kj::mv(settings));
}

kj::Own<HttpClient> newHttpClient(kj::Timer& timer, const HttpHeaderTable& responseHeaderTable,
                                  kj::Network& network, kj::Maybe<kj::Network&> tlsNetwork,
                                  HttpClientSettings settings) {
  return kj::heap<NetworkHttpClient>(
      timer, responseHeaderTable, network, tlsNetwork, kj::mv(settings));
}

// =======================================================================================

namespace {

class ConcurrencyLimitingHttpClient final: public HttpClient {
public:
  KJ_DISALLOW_COPY_AND_MOVE(ConcurrencyLimitingHttpClient);
  ConcurrencyLimitingHttpClient(
      kj::HttpClient& inner, uint maxConcurrentRequests,
      kj::Function<void(uint runningCount, uint pendingCount)> countChangedCallback)
      : inner(inner),
        maxConcurrentRequests(maxConcurrentRequests),
        countChangedCallback(kj::mv(countChangedCallback)) {}

  ~ConcurrencyLimitingHttpClient() noexcept(false) {
    if (concurrentRequests > 0) {
      static bool logOnce KJ_UNUSED = ([&] {
        KJ_LOG(ERROR, "ConcurrencyLimitingHttpClient getting destroyed when concurrent requests "
            "are still active", concurrentRequests);
        return true;
      })();
    }
  }

  Request request(HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
                  kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
    if (concurrentRequests < maxConcurrentRequests) {
      auto counter = ConnectionCounter(*this);
      auto request = inner.request(method, url, headers, expectedBodySize);
      fireCountChanged();
      auto promise = attachCounter(kj::mv(request.response), kj::mv(counter));
      return { kj::mv(request.body), kj::mv(promise) };
    }

    auto paf = kj::newPromiseAndFulfiller<ConnectionCounter>();
    auto urlCopy = kj::str(url);
    auto headersCopy = headers.clone();

    auto combined = paf.promise
        .then([this,
               method,
               urlCopy = kj::mv(urlCopy),
               headersCopy = kj::mv(headersCopy),
               expectedBodySize](ConnectionCounter&& counter) mutable {
      auto req = inner.request(method, urlCopy, headersCopy, expectedBodySize);
      return kj::tuple(kj::mv(req.body), attachCounter(kj::mv(req.response), kj::mv(counter)));
    });
    auto split = combined.split();
    pendingRequests.push(kj::mv(paf.fulfiller));
    fireCountChanged();
    return { newPromisedStream(kj::mv(kj::get<0>(split))), kj::mv(kj::get<1>(split)) };
  }

  kj::Promise<WebSocketResponse> openWebSocket(
      kj::StringPtr url, const kj::HttpHeaders& headers) override {
    if (concurrentRequests < maxConcurrentRequests) {
      auto counter = ConnectionCounter(*this);
      auto response = inner.openWebSocket(url, headers);
      fireCountChanged();
      return attachCounter(kj::mv(response), kj::mv(counter));
    }

    auto paf = kj::newPromiseAndFulfiller<ConnectionCounter>();
    auto urlCopy = kj::str(url);
    auto headersCopy = headers.clone();

    auto promise = paf.promise
        .then([this,
               urlCopy = kj::mv(urlCopy),
               headersCopy = kj::mv(headersCopy)](ConnectionCounter&& counter) mutable {
      return attachCounter(inner.openWebSocket(urlCopy, headersCopy), kj::mv(counter));
    });

    pendingRequests.push(kj::mv(paf.fulfiller));
    fireCountChanged();
    return kj::mv(promise);
  }

  ConnectRequest connect(
      kj::StringPtr host, const kj::HttpHeaders& headers, HttpConnectSettings settings) override {
    if (concurrentRequests < maxConcurrentRequests) {
      auto counter = ConnectionCounter(*this);
      auto response = inner.connect(host, headers, settings);
      fireCountChanged();
      return attachCounter(kj::mv(response), kj::mv(counter));
    }

    auto paf = kj::newPromiseAndFulfiller<ConnectionCounter>();

    auto split = paf.promise
        .then([this, host=kj::str(host), headers=headers.clone(), settings]
              (ConnectionCounter&& counter) mutable
                  -> kj::Tuple<kj::Promise<ConnectRequest::Status>,
                               kj::Promise<kj::Own<kj::AsyncIoStream>>> {
      auto request = attachCounter(inner.connect(host, headers, settings), kj::mv(counter));
      return kj::tuple(kj::mv(request.status), kj::mv(request.connection));
    }).split();

    pendingRequests.push(kj::mv(paf.fulfiller));
    fireCountChanged();

    return ConnectRequest {
      kj::mv(kj::get<0>(split)),
      kj::newPromisedStream(kj::mv(kj::get<1>(split)))
    };
  }

private:
  struct ConnectionCounter;

  kj::HttpClient& inner;
  uint maxConcurrentRequests;
  uint concurrentRequests = 0;
  kj::Function<void(uint runningCount, uint pendingCount)> countChangedCallback;

  std::queue<kj::Own<kj::PromiseFulfiller<ConnectionCounter>>> pendingRequests;
  // TODO(someday): want maximum cap on queue size?

  struct ConnectionCounter final {
    ConnectionCounter(ConcurrencyLimitingHttpClient& client) : parent(&client) {
      ++parent->concurrentRequests;
    }
    KJ_DISALLOW_COPY(ConnectionCounter);
    ~ConnectionCounter() noexcept(false) {
      if (parent != nullptr) {
        --parent->concurrentRequests;
        parent->serviceQueue();
        parent->fireCountChanged();
      }
    }
    ConnectionCounter(ConnectionCounter&& other) : parent(other.parent) {
      other.parent = nullptr;
    }
    ConnectionCounter& operator=(ConnectionCounter&& other) {
      if (this != &other) {
        this->parent = other.parent;
        other.parent = nullptr;
      }
      return *this;
    }

    ConcurrencyLimitingHttpClient* parent;
  };

  void serviceQueue() {
    while (concurrentRequests < maxConcurrentRequests && !pendingRequests.empty()) {
      auto fulfiller = kj::mv(pendingRequests.front());
      pendingRequests.pop();
      // ConnectionCounter's destructor calls this function, so we can avoid unnecessary recursion
      // if we only create a ConnectionCounter when we find a waiting fulfiller.
      if (fulfiller->isWaiting()) {
        fulfiller->fulfill(ConnectionCounter(*this));
      }
    }
  }

  void fireCountChanged() {
    countChangedCallback(concurrentRequests, pendingRequests.size());
  }

  using WebSocketOrBody = kj::OneOf<kj::Own<kj::AsyncInputStream>, kj::Own<WebSocket>>;
  static WebSocketOrBody attachCounter(WebSocketOrBody&& webSocketOrBody,
                                       ConnectionCounter&& counter) {
    KJ_SWITCH_ONEOF(webSocketOrBody) {
      KJ_CASE_ONEOF(ws, kj::Own<WebSocket>) {
        return ws.attach(kj::mv(counter));
      }
      KJ_CASE_ONEOF(body, kj::Own<kj::AsyncInputStream>) {
        return body.attach(kj::mv(counter));
      }
    }
    KJ_UNREACHABLE;
  }

  static kj::Promise<WebSocketResponse> attachCounter(kj::Promise<WebSocketResponse>&& promise,
                                                      ConnectionCounter&& counter) {
    return promise.then([counter = kj::mv(counter)](WebSocketResponse&& response) mutable {
      return WebSocketResponse {
        response.statusCode,
        response.statusText,
        response.headers,
        attachCounter(kj::mv(response.webSocketOrBody), kj::mv(counter))
      };
    });
  }

  static kj::Promise<Response> attachCounter(kj::Promise<Response>&& promise,
                                             ConnectionCounter&& counter) {
    return promise.then([counter = kj::mv(counter)](Response&& response) mutable {
      return Response {
        response.statusCode,
        response.statusText,
        response.headers,
        response.body.attach(kj::mv(counter))
      };
    });
  }

  static ConnectRequest attachCounter(
      ConnectRequest&& request,
      ConnectionCounter&& counter) {
    // Notice here that we are only attaching the counter to the connection stream. In the case
    // where the connect tunnel request is rejected and the status promise resolves with an
    // errorBody, there is a possibility that the consuming code might drop the connection stream
    // and the counter while the error body stream is still be consumed. Technically speaking that
    // means we could potentially exceed our concurrency limit temporarily but we consider that
    // acceptable here since the error body is an exception path (plus not requiring that we
    // attach to the errorBody keeps ConnectionCounter from having to be a refcounted heap
    // allocation).
    request.connection = request.connection.attach(kj::mv(counter));
    return kj::mv(request);
  }
};

}

kj::Own<HttpClient> newConcurrencyLimitingHttpClient(
    HttpClient& inner, uint maxConcurrentRequests,
    kj::Function<void(uint runningCount, uint pendingCount)> countChangedCallback) {
  return kj::heap<ConcurrencyLimitingHttpClient>(inner, maxConcurrentRequests,
      kj::mv(countChangedCallback));
}

// =======================================================================================

namespace {

class HttpClientAdapter final: public HttpClient {
public:
  HttpClientAdapter(HttpService& service): service(service) {}

  Request request(HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
                  kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
    // We have to clone the URL and headers because HttpService implementation are allowed to
    // assume that they remain valid until the service handler completes whereas HttpClient callers
    // are allowed to destroy them immediately after the call.
    auto urlCopy = kj::str(url);
    auto headersCopy = kj::heap(headers.clone());

    auto pipe = newOneWayPipe(expectedBodySize);

    // TODO(cleanup): The ownership relationships here are a mess. Can we do something better
    //   involving a PromiseAdapter, maybe?
    auto paf = kj::newPromiseAndFulfiller<Response>();
    auto responder = kj::refcounted<ResponseImpl>(method, kj::mv(paf.fulfiller));

    auto requestPaf = kj::newPromiseAndFulfiller<kj::Promise<void>>();
    responder->setPromise(kj::mv(requestPaf.promise));

    auto promise = service.request(method, urlCopy, *headersCopy, *pipe.in, *responder)
        .attach(kj::mv(pipe.in), kj::mv(urlCopy), kj::mv(headersCopy));
    requestPaf.fulfiller->fulfill(kj::mv(promise));

    return {
      kj::mv(pipe.out),
      paf.promise.attach(kj::mv(responder))
    };
  }

  kj::Promise<WebSocketResponse> openWebSocket(
      kj::StringPtr url, const HttpHeaders& headers) override {
    // We have to clone the URL and headers because HttpService implementation are allowed to
    // assume that they remain valid until the service handler completes whereas HttpClient callers
    // are allowed to destroy them immediately after the call. Also we need to add
    // `Upgrade: websocket` so that headers.isWebSocket() returns true on the service side.
    auto urlCopy = kj::str(url);
    auto headersCopy = kj::heap(headers.clone());
    headersCopy->set(HttpHeaderId::UPGRADE, "websocket");
    KJ_DASSERT(headersCopy->isWebSocket());

    auto paf = kj::newPromiseAndFulfiller<WebSocketResponse>();
    auto responder = kj::refcounted<WebSocketResponseImpl>(kj::mv(paf.fulfiller));

    auto requestPaf = kj::newPromiseAndFulfiller<kj::Promise<void>>();
    responder->setPromise(kj::mv(requestPaf.promise));

    auto in = kj::heap<NullInputStream>();
    auto promise = service.request(HttpMethod::GET, urlCopy, *headersCopy, *in, *responder)
        .attach(kj::mv(in), kj::mv(urlCopy), kj::mv(headersCopy));
    requestPaf.fulfiller->fulfill(kj::mv(promise));

    return paf.promise.attach(kj::mv(responder));
  }

  ConnectRequest connect(
      kj::StringPtr host, const HttpHeaders& headers, HttpConnectSettings settings) override {
    // We have to clone the host and the headers because HttpServer implementation are allowed to
    // assusme that they remain valid until the service handler completes whereas HttpClient callers
    // are allowed to destroy them immediately after the call.
    auto hostCopy = kj::str(host);
    auto headersCopy = kj::heap(headers.clone());

    // 1. Create a new TwoWayPipe, one will be returned with the ConnectRequest,
    //    the other will be held by the ConnectResponseImpl.
    auto pipe = kj::newTwoWayPipe();

    // 2. Create a promise/fulfiller pair for the status. The promise will be
    //    returned with the ConnectResponse, the fulfiller will be held by the
    //    ConnectResponseImpl.
    auto paf = kj::newPromiseAndFulfiller<ConnectRequest::Status>();

    // 3. Create the ConnectResponseImpl
    auto response = kj::refcounted<ConnectResponseImpl>(kj::mv(paf.fulfiller),
                                                        kj::mv(pipe.ends[0]));

    // 5. Call service.connect, passing in the tunnel.
    //    The call to tunnel->getConnectStream() returns a guarded stream that will buffer
    //    writes until the status is indicated by calling accept/reject.
    auto connectStream = response->getConnectStream();
    auto promise = service.connect(hostCopy, *headersCopy, *connectStream, *response, settings)
        .eagerlyEvaluate([response=kj::mv(response),
                          host=kj::mv(hostCopy),
                          headers=kj::mv(headersCopy),
                          connectStream=kj::mv(connectStream)](kj::Exception&& ex) mutable {
      // A few things need to happen here.
      //   1. We'll log the exception.
      //   2. We'll break the pipe.
      //   3. We'll reject the status promise if it is still pending.
      //
      // We'll do all of this within the ConnectResponseImpl, however, since it
      // maintains the state necessary here.
      response->handleException(kj::mv(ex), kj::mv(connectStream));
    });

    // TODO(bug): There's a challenge with attaching the service.connect promise to the
    // connection stream below in that the client will likely drop the connection as soon
    // as it reads EOF, but the promise representing the service connect() call may still
    // be running and want to do some cleanup after it has sent EOF. That cleanup will be
    // canceled. For regular HTTP calls, DelayedEofInputStream was created to address this
    // exact issue but with connect() being bidirectional it's rather more difficult. We
    // want a delay similar to what DelayedEofInputStream adds but only when both directions
    // have been closed. That currently is not possible until we have an alternative to
    // shutdownWrite() that returns a Promise (e.g. Promise<void> end()). For now, we can
    // live with the current limitation.
    return ConnectRequest {
      kj::mv(paf.promise),
      pipe.ends[1].attach(kj::mv(promise)),
    };
  }

private:
  HttpService& service;

  class DelayedEofInputStream final: public kj::AsyncInputStream {
    // An AsyncInputStream wrapper that, when it reaches EOF, delays the final read until some
    // promise completes.

  public:
    DelayedEofInputStream(kj::Own<kj::AsyncInputStream> inner, kj::Promise<void> completionTask)
        : inner(kj::mv(inner)), completionTask(kj::mv(completionTask)) {}

    kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
      return wrap(minBytes, inner->tryRead(buffer, minBytes, maxBytes));
    }

    kj::Maybe<uint64_t> tryGetLength() override {
      return inner->tryGetLength();
    }

    kj::Promise<uint64_t> pumpTo(kj::AsyncOutputStream& output, uint64_t amount) override {
      return wrap(amount, inner->pumpTo(output, amount));
    }

  private:
    kj::Own<kj::AsyncInputStream> inner;
    kj::Maybe<kj::Promise<void>> completionTask;

    template <typename T>
    kj::Promise<T> wrap(T requested, kj::Promise<T> innerPromise) {
      return innerPromise.then([this,requested](T actual) -> kj::Promise<T> {
        if (actual < requested) {
          // Must have reached EOF.
          KJ_IF_MAYBE(t, completionTask) {
            // Delay until completion.
            auto result = t->then([actual]() { return actual; });
            completionTask = nullptr;
            return result;
          } else {
            // Must have called tryRead() again after we already signaled EOF. Fine.
            return actual;
          }
        } else {
          return actual;
        }
      }, [this](kj::Exception&& e) -> kj::Promise<T> {
        // The stream threw an exception, but this exception is almost certainly just complaining
        // that the other end of the stream was dropped. In all likelihood, the HttpService
        // request() call itself will throw a much more interesting error -- we'd rather propagate
        // that one, if so.
        KJ_IF_MAYBE(t, completionTask) {
          auto result = t->then([e = kj::mv(e)]() mutable -> kj::Promise<T> {
            // Looks like the service didn't throw. I guess we should propagate the stream error
            // after all.
            return kj::mv(e);
          });
          completionTask = nullptr;
          return result;
        } else {
          // Must have called tryRead() again after we already signaled EOF or threw. Fine.
          return kj::mv(e);
        }
      });
    }
  };

  class ResponseImpl final: public HttpService::Response, public kj::Refcounted {
  public:
    ResponseImpl(kj::HttpMethod method,
                 kj::Own<kj::PromiseFulfiller<HttpClient::Response>> fulfiller)
        : method(method), fulfiller(kj::mv(fulfiller)) {}

    void setPromise(kj::Promise<void> promise) {
      task = promise.eagerlyEvaluate([this](kj::Exception&& exception) {
        if (fulfiller->isWaiting()) {
          fulfiller->reject(kj::mv(exception));
        } else {
          // We need to cause the response stream's read() to throw this, so we should propagate it.
          kj::throwRecoverableException(kj::mv(exception));
        }
      });
    }

    kj::Own<kj::AsyncOutputStream> send(
        uint statusCode, kj::StringPtr statusText, const HttpHeaders& headers,
        kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
      // The caller of HttpClient is allowed to assume that the statusText and headers remain
      // valid until the body stream is dropped, but the HttpService implementation is allowed to
      // send values that are only valid until send() returns, so we have to copy.
      auto statusTextCopy = kj::str(statusText);
      auto headersCopy = kj::heap(headers.clone());

      if (method == kj::HttpMethod::HEAD || expectedBodySize.orDefault(1) == 0) {
        // We're not expecting any body. We need to delay reporting completion to the client until
        // the server side has actually returned from the service method, otherwise we may
        // prematurely cancel it.

        task = task.then([this,statusCode,statusTextCopy=kj::mv(statusTextCopy),
                          headersCopy=kj::mv(headersCopy),expectedBodySize]() mutable {
          fulfiller->fulfill({
            statusCode, statusTextCopy, headersCopy.get(),
            kj::heap<NullInputStream>(expectedBodySize)
                .attach(kj::mv(statusTextCopy), kj::mv(headersCopy))
          });
        }).eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });
        return kj::heap<NullOutputStream>();
      } else {
        auto pipe = newOneWayPipe(expectedBodySize);

        // Wrap the stream in a wrapper that delays the last read (the one that signals EOF) until
        // the service's request promise has finished.
        auto wrapper = kj::heap<DelayedEofInputStream>(
            kj::mv(pipe.in), task.attach(kj::addRef(*this)));

        fulfiller->fulfill({
          statusCode, statusTextCopy, headersCopy.get(),
          wrapper.attach(kj::mv(statusTextCopy), kj::mv(headersCopy))
        });
        return kj::mv(pipe.out);
      }
    }

    kj::Own<WebSocket> acceptWebSocket(const HttpHeaders& headers) override {
      KJ_FAIL_REQUIRE("a WebSocket was not requested");
    }

  private:
    kj::HttpMethod method;
    kj::Own<kj::PromiseFulfiller<HttpClient::Response>> fulfiller;
    kj::Promise<void> task = nullptr;
  };

  class DelayedCloseWebSocket final: public WebSocket {
    // A WebSocket wrapper that, when it reaches Close (in both directions), delays the final close
    // operation until some promise completes.

  public:
    DelayedCloseWebSocket(kj::Own<kj::WebSocket> inner, kj::Promise<void> completionTask)
        : inner(kj::mv(inner)), completionTask(kj::mv(completionTask)) {}

    kj::Promise<void> send(kj::ArrayPtr<const byte> message) override {
      return inner->send(message);
    }
    kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
      return inner->send(message);
    }
    kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
      return inner->close(code, reason)
          .then([this]() {
        return afterSendClosed();
      });
    }
    kj::Promise<void> disconnect() override {
      return inner->disconnect();
    }
    void abort() override {
      // Don't need to worry about completion task in this case -- cancelling it is reasonable.
      inner->abort();
    }
    kj::Promise<void> whenAborted() override {
      return inner->whenAborted();
    }
    kj::Promise<Message> receive(size_t maxSize) override {
      return inner->receive(maxSize).then([this](Message&& message) -> kj::Promise<Message> {
        if (message.is<WebSocket::Close>()) {
          return afterReceiveClosed()
              .then([message = kj::mv(message)]() mutable { return kj::mv(message); });
        }
        return kj::mv(message);
      });
    }
    kj::Promise<void> pumpTo(WebSocket& other) override {
      return inner->pumpTo(other).then([this]() {
        return afterReceiveClosed();
      });
    }
    kj::Maybe<kj::Promise<void>> tryPumpFrom(WebSocket& other) override {
      return other.pumpTo(*inner).then([this]() {
        return afterSendClosed();
      });
    }

    uint64_t sentByteCount() override { return inner->sentByteCount(); }
    uint64_t receivedByteCount() override { return inner->receivedByteCount(); }

  private:
    kj::Own<kj::WebSocket> inner;
    kj::Maybe<kj::Promise<void>> completionTask;

    bool sentClose = false;
    bool receivedClose = false;

    kj::Promise<void> afterSendClosed() {
      sentClose = true;
      if (receivedClose) {
        KJ_IF_MAYBE(t, completionTask) {
          auto result = kj::mv(*t);
          completionTask = nullptr;
          return result;
        }
      }
      return kj::READY_NOW;
    }

    kj::Promise<void> afterReceiveClosed() {
      receivedClose = true;
      if (sentClose) {
        KJ_IF_MAYBE(t, completionTask) {
          auto result = kj::mv(*t);
          completionTask = nullptr;
          return result;
        }
      }
      return kj::READY_NOW;
    }
  };

  class WebSocketResponseImpl final: public HttpService::Response, public kj::Refcounted {
  public:
    WebSocketResponseImpl(kj::Own<kj::PromiseFulfiller<HttpClient::WebSocketResponse>> fulfiller)
        : fulfiller(kj::mv(fulfiller)) {}

    void setPromise(kj::Promise<void> promise) {
      task = promise.eagerlyEvaluate([this](kj::Exception&& exception) {
        if (fulfiller->isWaiting()) {
          fulfiller->reject(kj::mv(exception));
        } else {
          // We need to cause the client-side WebSocket to throw on close, so propagate the
          // exception.
          kj::throwRecoverableException(kj::mv(exception));
        }
      });
    }

    kj::Own<kj::AsyncOutputStream> send(
        uint statusCode, kj::StringPtr statusText, const HttpHeaders& headers,
        kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
      // The caller of HttpClient is allowed to assume that the statusText and headers remain
      // valid until the body stream is dropped, but the HttpService implementation is allowed to
      // send values that are only valid until send() returns, so we have to copy.
      auto statusTextCopy = kj::str(statusText);
      auto headersCopy = kj::heap(headers.clone());

      if (expectedBodySize.orDefault(1) == 0) {
        // We're not expecting any body. We need to delay reporting completion to the client until
        // the server side has actually returned from the service method, otherwise we may
        // prematurely cancel it.

        task = task.then([this,statusCode,statusTextCopy=kj::mv(statusTextCopy),
                          headersCopy=kj::mv(headersCopy),expectedBodySize]() mutable {
          fulfiller->fulfill({
            statusCode, statusTextCopy, headersCopy.get(),
            kj::Own<AsyncInputStream>(kj::heap<NullInputStream>(expectedBodySize)
                .attach(kj::mv(statusTextCopy), kj::mv(headersCopy)))
          });
        }).eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });
        return kj::heap<NullOutputStream>();
      } else {
        auto pipe = newOneWayPipe(expectedBodySize);

        // Wrap the stream in a wrapper that delays the last read (the one that signals EOF) until
        // the service's request promise has finished.
        kj::Own<AsyncInputStream> wrapper =
            kj::heap<DelayedEofInputStream>(kj::mv(pipe.in), task.attach(kj::addRef(*this)));

        fulfiller->fulfill({
          statusCode, statusTextCopy, headersCopy.get(),
          wrapper.attach(kj::mv(statusTextCopy), kj::mv(headersCopy))
        });
        return kj::mv(pipe.out);
      }
    }

    kj::Own<WebSocket> acceptWebSocket(const HttpHeaders& headers) override {
      // The caller of HttpClient is allowed to assume that the headers remain valid until the body
      // stream is dropped, but the HttpService implementation is allowed to send headers that are
      // only valid until acceptWebSocket() returns, so we have to copy.
      auto headersCopy = kj::heap(headers.clone());

      auto pipe = newWebSocketPipe();

      // Wrap the client-side WebSocket in a wrapper that delays clean close of the WebSocket until
      // the service's request promise has finished.
      kj::Own<WebSocket> wrapper =
          kj::heap<DelayedCloseWebSocket>(kj::mv(pipe.ends[0]), task.attach(kj::addRef(*this)));
      fulfiller->fulfill({
        101, "Switching Protocols", headersCopy.get(),
        wrapper.attach(kj::mv(headersCopy))
      });
      return kj::mv(pipe.ends[1]);
    }

  private:
    kj::Own<kj::PromiseFulfiller<HttpClient::WebSocketResponse>> fulfiller;
    kj::Promise<void> task = nullptr;
  };

  class ConnectResponseImpl final: public HttpService::ConnectResponse, public kj::Refcounted {
  public:
    ConnectResponseImpl(
        kj::Own<kj::PromiseFulfiller<HttpClient::ConnectRequest::Status>> fulfiller,
        kj::Own<kj::AsyncIoStream> stream)
        : fulfiller(kj::mv(fulfiller)),
          streamAndFulfiller(initStreamsAndFulfiller(kj::mv(stream))) {}

    ~ConnectResponseImpl() noexcept(false) {
      if (fulfiller->isWaiting() || streamAndFulfiller.fulfiller->isWaiting()) {
        auto ex = KJ_EXCEPTION(FAILED,
            "service's connect() implementation never called accept() nor reject()");
        if (fulfiller->isWaiting()) {
          fulfiller->reject(kj::cp(ex));
        }
        if (streamAndFulfiller.fulfiller->isWaiting()) {
          streamAndFulfiller.fulfiller->reject(kj::mv(ex));
        }
      }
    }

    void accept(uint statusCode, kj::StringPtr statusText, const HttpHeaders& headers) override {
      KJ_REQUIRE(statusCode >= 200 && statusCode < 300, "the statusCode must be 2xx for accept");
      respond(statusCode, statusText, headers);
    }

    kj::Own<kj::AsyncOutputStream> reject(
        uint statusCode,
        kj::StringPtr statusText,
        const HttpHeaders& headers,
        kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
      KJ_REQUIRE(statusCode < 200 || statusCode >= 300,
          "the statusCode must not be 2xx for reject.");
      auto pipe = kj::newOneWayPipe();
      respond(statusCode, statusText, headers, kj::mv(pipe.in));
      return kj::mv(pipe.out);
    }

  private:
    struct StreamsAndFulfiller {
      // guarded is the wrapped/guarded stream that wraps a reference to
      // the underlying stream but blocks reads until the connection is accepted
      // or rejected.
      // This will be handed off when getConnectStream() is called.
      // The fulfiller is used to resolve the guard for the second stream. This will
      // be fulfilled or rejected when accept/reject is called.
      kj::Own<kj::AsyncIoStream> guarded;
      kj::Own<kj::PromiseFulfiller<void>> fulfiller;
    };

    kj::Own<kj::PromiseFulfiller<HttpClient::ConnectRequest::Status>> fulfiller;
    StreamsAndFulfiller streamAndFulfiller;
    bool connectStreamDetached = false;

    StreamsAndFulfiller initStreamsAndFulfiller(kj::Own<kj::AsyncIoStream> stream) {
      auto paf = kj::newPromiseAndFulfiller<void>();
      auto guarded = kj::heap<AsyncIoStreamWithGuards>(
          kj::mv(stream),
          kj::Maybe<HttpInputStreamImpl::ReleasedBuffer>(nullptr),
          kj::mv(paf.promise));
      return StreamsAndFulfiller {
        kj::mv(guarded),
        kj::mv(paf.fulfiller)
      };
    }

    void handleException(kj::Exception&& ex, kj::Own<kj::AsyncIoStream> connectStream) {
      // Log the exception...
      KJ_LOG(ERROR, "Error in HttpClientAdapter connect()", kj::cp(ex));
      // Reject the status promise if it is still pending...
      if (fulfiller->isWaiting()) {
        fulfiller->reject(kj::cp(ex));
      }
      if (streamAndFulfiller.fulfiller->isWaiting()) {
        // If the guard hasn't yet ben released, we can fail the pending reads by
        // rejecting the fulfiller here.
        streamAndFulfiller.fulfiller->reject(kj::mv(ex));
      } else {
        // The guard has already been released at this point.
        // TODO(connect): How to properly propagate the actual exception to the
        // connect stream? Here we "simply" shut it down.
        connectStream->abortRead();
        connectStream->shutdownWrite();
      }
    }

    kj::Own<kj::AsyncIoStream> getConnectStream() {
      KJ_ASSERT(!connectStreamDetached, "the connect stream was already detached");
      connectStreamDetached = true;
      return streamAndFulfiller.guarded.attach(kj::addRef(*this));
    }

    void respond(uint statusCode,
                 kj::StringPtr statusText,
                 const HttpHeaders& headers,
                 kj::Maybe<kj::Own<kj::AsyncInputStream>> errorBody = nullptr) {
      if (errorBody == nullptr) {
        streamAndFulfiller.fulfiller->fulfill();
      } else {
        streamAndFulfiller.fulfiller->reject(
            KJ_EXCEPTION(DISCONNECTED, "the connect request was rejected"));
      }
      fulfiller->fulfill(HttpClient::ConnectRequest::Status(
          statusCode,
          kj::str(statusText),
          kj::heap(headers.clone()),
          kj::mv(errorBody)));
    }

    friend class HttpClientAdapter;
  };

};

}  // namespace

kj::Own<HttpClient> newHttpClient(HttpService& service) {
  return kj::heap<HttpClientAdapter>(service);
}

// =======================================================================================

namespace {

class HttpServiceAdapter final: public HttpService {
public:
  HttpServiceAdapter(HttpClient& client): client(client) {}

  kj::Promise<void> request(
      HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
      kj::AsyncInputStream& requestBody, Response& response) override {
    if (!headers.isWebSocket()) {
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

      return kj::joinPromisesFailFast(promises.finish());
    } else {
      return client.openWebSocket(url, headers)
          .then([&response](HttpClient::WebSocketResponse&& innerResponse) -> kj::Promise<void> {
        KJ_SWITCH_ONEOF(innerResponse.webSocketOrBody) {
          KJ_CASE_ONEOF(ws, kj::Own<WebSocket>) {
            auto ws2 = response.acceptWebSocket(*innerResponse.headers);
            auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);
            promises.add(ws->pumpTo(*ws2));
            promises.add(ws2->pumpTo(*ws));
            return kj::joinPromisesFailFast(promises.finish()).attach(kj::mv(ws), kj::mv(ws2));
          }
          KJ_CASE_ONEOF(body, kj::Own<kj::AsyncInputStream>) {
            auto out = response.send(
                innerResponse.statusCode, innerResponse.statusText, *innerResponse.headers,
                body->tryGetLength());
            auto promise = body->pumpTo(*out);
            return promise.ignoreResult().attach(kj::mv(out), kj::mv(body));
          }
        }
        KJ_UNREACHABLE;
      });
    }
  }

  kj::Promise<void> connect(kj::StringPtr host,
                            const HttpHeaders& headers,
                            kj::AsyncIoStream& connection,
                            ConnectResponse& response,
                            HttpConnectSettings settings) override {
    KJ_REQUIRE(!headers.isWebSocket(), "WebSocket upgrade headers are not permitted in a connect.");

    auto request = client.connect(host, headers, settings);

    // This operates optimistically. In order to support pipelining, we connect the
    // input and outputs streams immediately, even if we're not yet certain that the
    // tunnel can actually be established.
    auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);

    // For the inbound pipe (from the clients stream to the passed in stream)
    // We want to guard reads pending the acceptance of the tunnel. If the
    // tunnel is not accepted, the guard will be rejected, causing pending
    // reads to fail.
    auto paf = kj::newPromiseAndFulfiller<kj::Maybe<HttpInputStreamImpl::ReleasedBuffer>>();
    auto io = kj::heap<AsyncIoStreamWithGuards>(
        kj::mv(request.connection),
        kj::mv(paf.promise) /* read guard */,
        kj::READY_NOW /* write guard */);

    // Writing from connection to io is unguarded and allowed immediately.
    promises.add(connection.pumpTo(*io).then([&io=*io](uint64_t size) {
      io.shutdownWrite();
    }));

    promises.add(io->pumpTo(connection).then([&connection](uint64_t size) {
      connection.shutdownWrite();
    }));

    auto pumpPromise = kj::joinPromisesFailFast(promises.finish());

    return request.status.then(
        [&response,&connection,fulfiller=kj::mv(paf.fulfiller),
         pumpPromise=kj::mv(pumpPromise)]
        (HttpClient::ConnectRequest::Status status) mutable -> kj::Promise<void> {
      if (status.statusCode >= 200 && status.statusCode < 300) {
        // Release the read guard!
        fulfiller->fulfill(kj::Maybe<HttpInputStreamImpl::ReleasedBuffer>(nullptr));
        response.accept(status.statusCode, status.statusText, *status.headers);
        return kj::mv(pumpPromise);
      } else {
        // If the connect request is rejected, we want to shutdown the tunnel
        // and pipeline the status.errorBody to the AsyncOutputStream returned by
        // reject if it exists.
        pumpPromise = nullptr;
        connection.shutdownWrite();
        fulfiller->reject(KJ_EXCEPTION(DISCONNECTED, "the connect request was rejected"));
        KJ_IF_MAYBE(errorBody, status.errorBody) {
          auto out = response.reject(status.statusCode, status.statusText, *status.headers,
              errorBody->get()->tryGetLength());
          return (*errorBody)->pumpTo(*out).then([](uint64_t) -> kj::Promise<void> {
            return kj::READY_NOW;
          }).attach(kj::mv(out), kj::mv(*errorBody));
        } else {
          response.reject(status.statusCode, status.statusText, *status.headers, (uint64_t)0);
          return kj::READY_NOW;
        }
      }
    }).attach(kj::mv(io));
  }

private:
  HttpClient& client;
};

}  // namespace

kj::Own<HttpService> newHttpService(HttpClient& client) {
  return kj::heap<HttpServiceAdapter>(client);
}

// =======================================================================================

kj::Promise<void> HttpService::Response::sendError(
    uint statusCode, kj::StringPtr statusText, const HttpHeaders& headers) {
  auto stream = send(statusCode, statusText, headers, statusText.size());
  auto promise = stream->write(statusText.begin(), statusText.size());
  return promise.attach(kj::mv(stream));
}

kj::Promise<void> HttpService::Response::sendError(
    uint statusCode, kj::StringPtr statusText, const HttpHeaderTable& headerTable) {
  return sendError(statusCode, statusText, HttpHeaders(headerTable));
}

kj::Promise<void> HttpService::connect(
    kj::StringPtr host,
    const HttpHeaders& headers,
    kj::AsyncIoStream& connection,
    ConnectResponse& response,
    kj::HttpConnectSettings settings) {
  KJ_UNIMPLEMENTED("CONNECT is not implemented by this HttpService");
}

class HttpServer::Connection final: private HttpService::Response,
                                    private HttpService::ConnectResponse,
                                    private HttpServerErrorHandler {
public:
  Connection(HttpServer& server, kj::AsyncIoStream& stream,
             SuspendableHttpServiceFactory factory, kj::Maybe<SuspendedRequest> suspendedRequest,
             bool wantCleanDrain)
      : server(server),
        stream(stream),
        factory(kj::mv(factory)),
        httpInput(makeHttpInput(stream, server.requestHeaderTable, kj::mv(suspendedRequest))),
        httpOutput(stream),
        wantCleanDrain(wantCleanDrain) {
    ++server.connectionCount;
  }
  ~Connection() noexcept(false) {
    if (--server.connectionCount == 0) {
      KJ_IF_MAYBE(f, server.zeroConnectionsFulfiller) {
        f->get()->fulfill();
      }
    }
  }

public:
  kj::Promise<bool> startLoop(bool firstRequest) {
    return loop(firstRequest).catch_([this](kj::Exception&& e) -> kj::Promise<bool> {
      // Exception; report 5xx.

      KJ_IF_MAYBE(p, webSocketError) {
        // sendWebSocketError() was called. Finish sending and close the connection. Don't log
        // the exception because it's probably a side-effect of this.
        auto promise = kj::mv(*p);
        webSocketError = nullptr;
        return kj::mv(promise);
      }

      KJ_IF_MAYBE(p, tunnelRejected) {
        // reject() was called to reject a CONNECT request. Finish sending and close the connection.
        // Don't log the exception because it's probably a side-effect of this.
        auto promise = kj::mv(*p);
        tunnelRejected = nullptr;
        return kj::mv(promise);
      }

      return sendError(kj::mv(e));
    });
  }

  SuspendedRequest suspend(SuspendableRequest& suspendable) {
    KJ_REQUIRE(httpInput.canSuspend(),
        "suspend() may only be called before the request body is consumed");
    KJ_DEFER(suspended = true);
    auto released = httpInput.releaseBuffer();
    return {
      kj::mv(released.buffer),
      released.leftover,
      suspendable.method,
      suspendable.url,
      suspendable.headers.cloneShallow(),
    };
  }

private:
  HttpServer& server;
  kj::AsyncIoStream& stream;

  SuspendableHttpServiceFactory factory;
  // Creates a new kj::Own<HttpService> for each request we handle on this connection.

  HttpInputStreamImpl httpInput;
  HttpOutputStream httpOutput;
  kj::Maybe<kj::OneOf<HttpMethod, HttpConnectMethod>> currentMethod;
  bool timedOut = false;
  bool closed = false;
  bool upgraded = false;
  bool webSocketOrConnectClosed = false;
  bool closeAfterSend = false;  // True if send() should set Connection: close.
  bool wantCleanDrain = false;
  bool suspended = false;
  kj::Maybe<kj::Promise<bool>> webSocketError;
  kj::Maybe<kj::Promise<bool>> tunnelRejected;
  kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> tunnelWriteGuard;

  static HttpInputStreamImpl makeHttpInput(
      kj::AsyncIoStream& stream,
      const kj::HttpHeaderTable& table,
      kj::Maybe<SuspendedRequest> suspendedRequest) {
    // Constructor helper function to create our HttpInputStreamImpl.

    KJ_IF_MAYBE(sr, suspendedRequest) {
      return HttpInputStreamImpl(stream,
          sr->buffer.releaseAsChars(),
          sr->leftover.asChars(),
          sr->method,
          sr->url,
          kj::mv(sr->headers));
    }
    return HttpInputStreamImpl(stream, table);
  }

  kj::Promise<bool> loop(bool firstRequest) {
    if (!firstRequest && server.draining && httpInput.isCleanDrain()) {
      // Don't call awaitNextMessage() in this case because that will initiate a read() which will
      // immediately be canceled, losing data.
      return true;
    }

    auto firstByte = httpInput.awaitNextMessage();

    if (!firstRequest) {
      // For requests after the first, require that the first byte arrive before the pipeline
      // timeout, otherwise treat it like the connection was simply closed.
      auto timeoutPromise = server.timer.afterDelay(server.settings.pipelineTimeout);

      if (httpInput.isCleanDrain()) {
        // If we haven't buffered any data, then we can safely drain here, so allow the wait to
        // be canceled by the onDrain promise.
        auto cleanDrainPromise = server.onDrain.addBranch()
            .then([this]() -> kj::Promise<void> {
          // This is a little tricky... drain() has been called, BUT we could have read some data
          // into the buffer in the meantime, and we don't want to lose that. If any data has
          // arrived, then we have no choice but to read the rest of the request and respond to
          // it.
          if (!httpInput.isCleanDrain()) {
            return kj::NEVER_DONE;
          }

          // OK... As far as we know, no data has arrived in the buffer. However, unfortunately,
          // we don't *really* know that, because read() is asynchronous. It may have already
          // delivered some bytes, but we just haven't received the notification yet, because it's
          // still queued on the event loop. As a horrible hack, we use evalLast(), so that any
          // such pending notifications get a chance to be delivered.
          // TODO(someday): Does this actually work on Windows, where the notification could also
          //   be queued on the IOCP?
          return kj::evalLast([this]() -> kj::Promise<void> {
            if (httpInput.isCleanDrain()) {
              return kj::READY_NOW;
            } else {
              return kj::NEVER_DONE;
            }
          });
        });
        timeoutPromise = timeoutPromise.exclusiveJoin(kj::mv(cleanDrainPromise));
      }

      firstByte = firstByte.exclusiveJoin(timeoutPromise.then([this]() -> bool {
        timedOut = true;
        return false;
      }));
    }

    auto receivedHeaders = firstByte
        .then([this,firstRequest](bool hasData)
            -> kj::Promise<HttpHeaders::RequestConnectOrProtocolError> {
      if (hasData) {
        auto readHeaders = httpInput.readRequestHeaders();
        if (!firstRequest) {
          // On requests other than the first, the header timeout starts ticking when we receive
          // the first byte of a pipeline response.
          readHeaders = readHeaders.exclusiveJoin(
              server.timer.afterDelay(server.settings.headerTimeout)
              .then([this]() -> HttpHeaders::RequestConnectOrProtocolError {
            timedOut = true;
            return HttpHeaders::ProtocolError {
              408, "Request Timeout",
              "Timed out waiting for next request headers.", nullptr
            };
          }));
        }
        return kj::mv(readHeaders);
      } else {
        // Client closed connection or pipeline timed out with no bytes received. This is not an
        // error, so don't report one.
        this->closed = true;
        return HttpHeaders::RequestConnectOrProtocolError(HttpHeaders::ProtocolError {
          408, "Request Timeout",
          "Client closed connection or connection timeout "
          "while waiting for request headers.", nullptr
        });
      }
    });

    if (firstRequest) {
      // On the first request, the header timeout starts ticking immediately upon request opening.
      // NOTE: Since we assume that the client wouldn't have formed a connection if they did not
      //   intend to send a request, we immediately treat this connection as having an active
      //   request, i.e. we do NOT cancel it if drain() is called.
      auto timeoutPromise = server.timer.afterDelay(server.settings.headerTimeout)
          .then([this]() -> HttpHeaders::RequestConnectOrProtocolError {
        timedOut = true;
        return HttpHeaders::ProtocolError {
          408, "Request Timeout",
          "Timed out waiting for initial request headers.", nullptr
        };
      });
      receivedHeaders = receivedHeaders.exclusiveJoin(kj::mv(timeoutPromise));
    }

    return receivedHeaders
        .then([this](HttpHeaders::RequestConnectOrProtocolError&& requestOrProtocolError)
            -> kj::Promise<bool> {
      if (timedOut) {
        // Client took too long to send anything, so we're going to close the connection. In
        // theory, we should send back an HTTP 408 error -- it is designed exactly for this
        // purpose. Alas, in practice, Google Chrome does not have any special handling for 408
        // errors -- it will assume the error is a response to the next request it tries to send,
        // and will happily serve the error to the user. OTOH, if we simply close the connection,
        // Chrome does the "right thing", apparently. (Though I'm not sure what happens if a
        // request is in-flight when we close... if it's a GET, the browser should retry. But if
        // it's a POST, retrying may be dangerous. This is why 408 exists -- it unambiguously
        // tells the client that it should retry.)
        //
        // Also note that if we ever decide to send 408 again, we might want to send some other
        // error in the case that the server is draining, which also sets timedOut = true; see
        // above.

        return httpOutput.flush().then([this]() {
          return server.draining && httpInput.isCleanDrain();
        });
      }

      if (closed) {
        // Client closed connection. Close our end too.
        return httpOutput.flush().then([]() { return false; });
      }

      KJ_SWITCH_ONEOF(requestOrProtocolError) {
        KJ_CASE_ONEOF(request, HttpHeaders::ConnectRequest) {
          auto& headers = httpInput.getHeaders();

          currentMethod = HttpConnectMethod();

          // The HTTP specification says that CONNECT requests have no meaningful payload
          // but stops short of saying that CONNECT *cannot* have a payload. Implementations
          // can choose to either accept payloads or reject them. We choose to reject it.
          // Specifically, if there are Content-Length or Transfer-Encoding headers in the
          // request headers, we'll automatically reject the CONNECT request.
          //
          // The key implication here is that any data that immediately follows the headers
          // block of the CONNECT request is considered to be part of the tunnel if it is
          // established.

          KJ_IF_MAYBE(cl, headers.get(HttpHeaderId::CONTENT_LENGTH)) {
            return sendError(HttpHeaders::ProtocolError {
              400,
              "Bad Request"_kj,
              "Bad Request"_kj,
              nullptr,
            });
          }
          KJ_IF_MAYBE(te, headers.get(HttpHeaderId::TRANSFER_ENCODING)) {
            return sendError(HttpHeaders::ProtocolError {
              400,
              "Bad Request"_kj,
              "Bad Request"_kj,
              nullptr,
            });
          }

          SuspendableRequest suspendable(*this, HttpConnectMethod(), request.authority, headers);
          auto maybeService = factory(suspendable);

          if (suspended) {
            return false;
          }

          auto service = KJ_ASSERT_NONNULL(kj::mv(maybeService),
              "SuspendableHttpServiceFactory did not suspend, but returned nullptr.");
          auto connectStream = getConnectStream();
          auto promise = service->connect(
              request.authority, headers, *connectStream, *this, {})
              .attach(kj::mv(service), kj::mv(connectStream));
          return promise.then([this]() mutable -> kj::Promise<bool> {
            KJ_IF_MAYBE(p, tunnelRejected) {
              // reject() was called to reject a CONNECT attempt.
              // Finish sending and close the connection.
              auto promise = kj::mv(*p);
              tunnelRejected = nullptr;
              return kj::mv(promise);
            }

            if (httpOutput.isBroken()) {
              return false;
            }

            return httpOutput.flush().then([]() mutable -> kj::Promise<bool> {
              // There is really no reasonable path to reusing a CONNECT connection.
              return false;
            });
          });
        }
        KJ_CASE_ONEOF(request, HttpHeaders::Request) {
          auto& headers = httpInput.getHeaders();

          currentMethod = request.method;

          SuspendableRequest suspendable(*this, request.method, request.url, headers);
          auto maybeService = factory(suspendable);

          if (suspended) {
            return false;
          }

          auto service = KJ_ASSERT_NONNULL(kj::mv(maybeService),
              "SuspendableHttpServiceFactory did not suspend, but returned nullptr.");

          // TODO(perf): If the client disconnects, should we cancel the response? Probably, to
          //   prevent permanent deadlock. It's slightly weird in that arguably the client should
          //   be able to shutdown the upstream but still wait on the downstream, but I believe many
          //   other HTTP servers do similar things.

          auto body = httpInput.getEntityBody(
              HttpInputStreamImpl::REQUEST, request.method, 0, headers);

          auto promise = service->request(
              request.method, request.url, headers, *body, *this).attach(kj::mv(service));
          return promise.then([this, body = kj::mv(body)]() mutable -> kj::Promise<bool> {
            // Response done. Await next request.

            KJ_IF_MAYBE(p, webSocketError) {
              // sendWebSocketError() was called. Finish sending and close the connection.
              auto promise = kj::mv(*p);
              webSocketError = nullptr;
              return kj::mv(promise);
            }

            if (upgraded) {
              // We've upgraded to WebSocket, and by now we should have closed the WebSocket.
              if (!webSocketOrConnectClosed) {
                // This is gonna segfault later so abort now instead.
                KJ_LOG(FATAL, "Accepted WebSocket object must be destroyed before HttpService "
                              "request handler completes.");
                abort();
              }

              // Once we start a WebSocket there's no going back to HTTP.
              return false;
            }

            if (currentMethod != nullptr) {
              return sendError();
            }

            if (httpOutput.isBroken()) {
              // We started a response but didn't finish it. But HttpService returns success?
              // Perhaps it decided that it doesn't want to finish this response. We'll have to
              // disconnect here. If the response body is not complete (e.g. Content-Length not
              // reached), the client should notice. We don't want to log an error because this
              // condition might be intentional on the service's part.
              return false;
            }

            return httpOutput.flush().then(
                [this, body = kj::mv(body)]() mutable -> kj::Promise<bool> {
              if (httpInput.canReuse()) {
                // Things look clean. Go ahead and accept the next request.

                if (closeAfterSend) {
                  // We sent Connection: close, so drop the connection now.
                  return false;
                } else {
                  // Note that we don't have to handle server.draining here because we'll take care
                  // of it the next time around the loop.
                  return loop(false);
                }
              } else {
                // Apparently, the application did not read the request body. Maybe this is a bug,
                // or maybe not: maybe the client tried to upload too much data and the application
                // legitimately wants to cancel the upload without reading all it it.
                //
                // We have a problem, though: We did send a response, and we didn't send
                // `Connection: close`, so the client may expect that it can send another request.
                // Perhaps the client has even finished sending the previous request's body, in
                // which case the moment it finishes receiving the response, it could be completely
                // within its rights to start a new request. If we close the socket now, we might
                // interrupt that new request.
                //
                // Or maybe we did send `Connection: close`, as indicated by `closeAfterSend` being
                // true. Even in that case, we should still try to read and ignore the request,
                // otherwise when we close the connection the client may get a "connection reset"
                // error before they get a chance to actually read the response body that we sent
                // them.
                //
                // There's no way we can get out of this perfectly cleanly. HTTP just isn't good
                // enough at connection management. The best we can do is give the client some grace
                // period and then abort the connection.

                auto dummy = kj::heap<HttpDiscardingEntityWriter>();
                auto lengthGrace = kj::evalNow([&]() {
                  return body->pumpTo(*dummy, server.settings.canceledUploadGraceBytes);
                }).catch_([](kj::Exception&& e) -> uint64_t {
                  // Reading from the input failed in some way. This may actually be the whole
                  // reason we got here in the first place so don't propagate this error, just
                  // give up on discarding the input.
                  return 0;  // This zero is ignored but `canReuse()` will return false below.
                }).then([this](uint64_t amount) {
                  if (httpInput.canReuse()) {
                    // Success, we can continue.
                    return true;
                  } else {
                    // Still more data. Give up.
                    return false;
                  }
                });
                lengthGrace = lengthGrace.attach(kj::mv(dummy), kj::mv(body));

                auto timeGrace = server.timer.afterDelay(server.settings.canceledUploadGracePeriod)
                    .then([]() { return false; });

                return lengthGrace.exclusiveJoin(kj::mv(timeGrace))
                    .then([this](bool clean) -> kj::Promise<bool> {
                  if (clean && !closeAfterSend) {
                    // We recovered. Continue loop.
                    return loop(false);
                  } else {
                    // Client still not done, or we sent Connection: close and so want to drop the
                    // connection anyway. Return broken.
                    return false;
                  }
                });
              }
            });
          });
        }
        KJ_CASE_ONEOF(protocolError, HttpHeaders::ProtocolError) {
          // Bad request.

          // sendError() uses Response::send(), which requires that we have a currentMethod, but we
          // never read one. GET seems like the correct choice here.
          currentMethod = HttpMethod::GET;
          return sendError(kj::mv(protocolError));
        }
      }

      KJ_UNREACHABLE;
    });
  }

  kj::Own<kj::AsyncOutputStream> send(
      uint statusCode, kj::StringPtr statusText, const HttpHeaders& headers,
      kj::Maybe<uint64_t> expectedBodySize) override {
    auto method = KJ_REQUIRE_NONNULL(currentMethod, "already called send()");
    currentMethod = nullptr;

    kj::StringPtr connectionHeaders[HttpHeaders::CONNECTION_HEADERS_COUNT];
    kj::String lengthStr;

    if (!closeAfterSend) {
      // Check if application wants us to close connections.
      //
      // If the application used listenHttpClientDrain() to listen, then it expects that after a
      // clean drain, the connection is still open and can receive more requests. Otherwise, after
      // receiving drain(), we will close the connection, so we should send a `Connection: close`
      // header.
      if (server.draining && !wantCleanDrain) {
        closeAfterSend = true;
      } else KJ_IF_MAYBE(c, server.settings.callbacks) {
        // The application has registered its own callback to decide whether to send
        // `Connection: close`.
        if (c->shouldClose()) {
          closeAfterSend = true;
        }
      }
    }

    if (closeAfterSend) {
      connectionHeaders[HttpHeaders::BuiltinIndices::CONNECTION] = "close";
    }

    bool isHeadRequest = method.tryGet<HttpMethod>().map([](auto& m) {
        return m == HttpMethod::HEAD;
      }).orDefault(false);

    if (statusCode == 204 || statusCode == 304) {
      // No entity-body.
    } else if (statusCode == 205) {
      // Status code 205 also has no body, but unlike 204 and 304, it must explicitly encode an
      // empty body, e.g. using content-length: 0. I'm guessing this is one of those things,
      // where some early clients expected an explicit body while others assumed an empty body,
      // and so the standard had to choose the common denominator.
      //
      // Spec: https://tools.ietf.org/html/rfc7231#section-6.3.6
      connectionHeaders[HttpHeaders::BuiltinIndices::CONTENT_LENGTH] = "0";
    } else KJ_IF_MAYBE(s, expectedBodySize) {
      // HACK: We interpret a zero-length expected body length on responses to HEAD requests to
      //   mean "don't set a Content-Length header at all." This provides a way to omit a body
      //   header on HEAD responses with non-null-body status codes. This is a hack that *only*
      //   makes sense for HEAD responses.
      if (!isHeadRequest || *s > 0) {
        lengthStr = kj::str(*s);
        connectionHeaders[HttpHeaders::BuiltinIndices::CONTENT_LENGTH] = lengthStr;
      }
    } else {
      connectionHeaders[HttpHeaders::BuiltinIndices::TRANSFER_ENCODING] = "chunked";
    }

    // For HEAD requests, if the application specified a Content-Length or Transfer-Encoding
    // header, use that instead of whatever we decided above.
    kj::ArrayPtr<kj::StringPtr> connectionHeadersArray = connectionHeaders;
    if (isHeadRequest) {
      if (headers.get(HttpHeaderId::CONTENT_LENGTH) != nullptr ||
          headers.get(HttpHeaderId::TRANSFER_ENCODING) != nullptr) {
        connectionHeadersArray = connectionHeadersArray
            .slice(0, HttpHeaders::HEAD_RESPONSE_CONNECTION_HEADERS_COUNT);
      }
    }

    httpOutput.writeHeaders(headers.serializeResponse(
        statusCode, statusText, connectionHeadersArray));

    kj::Own<kj::AsyncOutputStream> bodyStream;
    if (isHeadRequest) {
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

  kj::Own<WebSocket> acceptWebSocket(const HttpHeaders& headers) override {
    auto& requestHeaders = httpInput.getHeaders();
    KJ_REQUIRE(requestHeaders.isWebSocket(),
        "can't call acceptWebSocket() if the request headers didn't have Upgrade: WebSocket");

    auto method = KJ_REQUIRE_NONNULL(currentMethod, "already called send()");
    KJ_REQUIRE(method.tryGet<HttpMethod>().map([](auto& m) {
      return m == HttpMethod::GET;
    }).orDefault(false), "WebSocket must be initiated with a GET request.");

    if (requestHeaders.get(HttpHeaderId::SEC_WEBSOCKET_VERSION).orDefault(nullptr) != "13") {
      return sendWebSocketError("The requested WebSocket version is not supported.");
    }

    kj::String key;
    KJ_IF_MAYBE(k, requestHeaders.get(HttpHeaderId::SEC_WEBSOCKET_KEY)) {
      key = kj::str(*k);
    } else {
      return sendWebSocketError("Missing Sec-WebSocket-Key");
    }

    kj::Maybe<CompressionParameters> acceptedParameters;
    kj::String agreedParameters;
    auto compressionMode = server.settings.webSocketCompressionMode;
    if (compressionMode == HttpServerSettings::AUTOMATIC_COMPRESSION) {
      // If AUTOMATIC_COMPRESSION is enabled, we ignore the `headers` passed by the application and
      // strictly refer to the `requestHeaders` from the client.
      KJ_IF_MAYBE(value, requestHeaders.get(HttpHeaderId::SEC_WEBSOCKET_EXTENSIONS)) {
        // Perform compression parameter negotiation.
        KJ_IF_MAYBE(config, _::tryParseExtensionOffers(*value)) {
          acceptedParameters = kj::mv(*config);
        }
      }
    } else if (compressionMode == HttpServerSettings::MANUAL_COMPRESSION) {
      // If MANUAL_COMPRESSION is enabled, we use the `headers` passed in by the application, and
      // try to find a configuration that respects both the server's preferred configuration,
      // as well as the client's requested configuration.
      KJ_IF_MAYBE(value, headers.get(HttpHeaderId::SEC_WEBSOCKET_EXTENSIONS)) {
        // First, we get the manual configuration using `headers`.
        KJ_IF_MAYBE(manualConfig, _::tryParseExtensionOffers(*value)) {
          KJ_IF_MAYBE(requestOffers, requestHeaders.get(HttpHeaderId::SEC_WEBSOCKET_EXTENSIONS)) {
            // Next, we to find a configuration that both the client and server can accept.
            acceptedParameters = _::tryParseAllExtensionOffers(*requestOffers, *manualConfig);
          }
        }
      }
    }

    auto websocketAccept = generateWebSocketAccept(key);

    kj::StringPtr connectionHeaders[HttpHeaders::WEBSOCKET_CONNECTION_HEADERS_COUNT];
    connectionHeaders[HttpHeaders::BuiltinIndices::SEC_WEBSOCKET_ACCEPT] = websocketAccept;
    connectionHeaders[HttpHeaders::BuiltinIndices::UPGRADE] = "websocket";
    connectionHeaders[HttpHeaders::BuiltinIndices::CONNECTION] = "Upgrade";
    KJ_IF_MAYBE(parameters, acceptedParameters) {
      agreedParameters = _::generateExtensionResponse(*parameters);
      connectionHeaders[HttpHeaders::BuiltinIndices::SEC_WEBSOCKET_EXTENSIONS] = agreedParameters;
    }

    // Since we're about to write headers, we should nullify `currentMethod`. This tells
    // `sendError(kj::Exception)` (called from `HttpServer::Connection::startLoop()`) not to expose
    // the `HttpService::Response&` reference to the HttpServer's error `handleApplicationError()`
    // callback. This prevents the error handler from inadvertently trying to send another error on
    // the connection.
    currentMethod = nullptr;

    httpOutput.writeHeaders(headers.serializeResponse(
        101, "Switching Protocols", connectionHeaders));

    upgraded = true;
    // We need to give the WebSocket an Own<AsyncIoStream>, but we only have a reference. This is
    // safe because the application is expected to drop the WebSocket object before returning
    // from the request handler. For some extra safety, we check that webSocketOrConnectClosed has
    // been set true when the handler returns.
    auto deferNoteClosed = kj::defer([this]() { webSocketOrConnectClosed = true; });
    kj::Own<kj::AsyncIoStream> ownStream(&stream, kj::NullDisposer::instance);
    return upgradeToWebSocket(ownStream.attach(kj::mv(deferNoteClosed)),
                              httpInput, httpOutput, nullptr, kj::mv(acceptedParameters),
                             server.settings.webSocketErrorHandler);
  }

  kj::Promise<bool> sendError(HttpHeaders::ProtocolError protocolError) {
    closeAfterSend = true;

    // Client protocol errors always happen on request headers parsing, before we call into the
    // HttpService, meaning no response has been sent and we can provide a Response object.
    auto promise = server.settings.errorHandler.orDefault(*this).handleClientProtocolError(
        kj::mv(protocolError), *this);
    return finishSendingError(kj::mv(promise));
  }

  kj::Promise<bool> sendError(kj::Exception&& exception) {
    closeAfterSend = true;

    // We only provide the Response object if we know we haven't already sent a response.
    auto promise = server.settings.errorHandler.orDefault(*this).handleApplicationError(
        kj::mv(exception), currentMethod.map([this](auto&&) -> Response& { return *this; }));
    return finishSendingError(kj::mv(promise));
  }

  kj::Promise<bool> sendError() {
    closeAfterSend = true;

    // We can provide a Response object, since none has already been sent.
    auto promise = server.settings.errorHandler.orDefault(*this).handleNoResponse(*this);
    return finishSendingError(kj::mv(promise));
  }

  kj::Promise<bool> finishSendingError(kj::Promise<void> promise) {
    return promise.then([this]() -> kj::Promise<void> {
      if (httpOutput.isBroken()) {
        // Skip flush for broken streams, since it will throw an exception that may be worse than
        // the one we just handled.
        return kj::READY_NOW;
      } else {
        return httpOutput.flush();
      }
    }).then([]() { return false; });  // loop ends after flush
  }

  kj::Own<WebSocket> sendWebSocketError(StringPtr errorMessage) {
    kj::Exception exception = KJ_EXCEPTION(FAILED,
        "received bad WebSocket handshake", errorMessage);
    webSocketError = sendError(
        HttpHeaders::ProtocolError { 400, "Bad Request", errorMessage, nullptr });
    kj::throwRecoverableException(kj::mv(exception));

    // Fallback path when exceptions are disabled.
    class BrokenWebSocket final: public WebSocket {
    public:
      BrokenWebSocket(kj::Exception exception): exception(kj::mv(exception)) {}

      kj::Promise<void> send(kj::ArrayPtr<const byte> message) override {
        return kj::cp(exception);
      }
      kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
        return kj::cp(exception);
      }
      kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
        return kj::cp(exception);
      }
      kj::Promise<void> disconnect() override {
        return kj::cp(exception);
      }
      void abort() override {
        kj::throwRecoverableException(kj::cp(exception));
      }
      kj::Promise<void> whenAborted() override {
        return kj::cp(exception);
      }
      kj::Promise<Message> receive(size_t maxSize) override {
        return kj::cp(exception);
      }

      uint64_t sentByteCount() override { KJ_FAIL_ASSERT("received bad WebSocket handshake"); }
      uint64_t receivedByteCount() override { KJ_FAIL_ASSERT("received bad WebSocket handshake"); }

    private:
      kj::Exception exception;
    };

    return kj::heap<BrokenWebSocket>(KJ_EXCEPTION(FAILED,
        "received bad WebSocket handshake", errorMessage));
  }

  kj::Own<kj::AsyncIoStream> getConnectStream() {
    // Returns an AsyncIoStream over the internal stream but that waits for a Promise to be
    // resolved to allow writes after either accept or reject are called. Reads are allowed
    // immediately.
    KJ_REQUIRE(tunnelWriteGuard == nullptr, "the tunnel stream was already retrieved");
    auto paf = kj::newPromiseAndFulfiller<void>();
    tunnelWriteGuard = kj::mv(paf.fulfiller);

    kj::Own<kj::AsyncIoStream> ownStream(&stream, kj::NullDisposer::instance);
    auto releasedBuffer = httpInput.releaseBuffer();
    auto deferNoteClosed = kj::defer([this]() { webSocketOrConnectClosed = true; });
    return kj::heap<AsyncIoStreamWithGuards>(
        kj::heap<AsyncIoStreamWithInitialBuffer>(
            kj::mv(ownStream),
            kj::mv(releasedBuffer.buffer),
            releasedBuffer.leftover).attach(kj::mv(deferNoteClosed)),
        kj::Maybe<HttpInputStreamImpl::ReleasedBuffer>(nullptr),
        kj::mv(paf.promise));
  }

  void accept(uint statusCode, kj::StringPtr statusText, const HttpHeaders& headers) override {
    auto method = KJ_REQUIRE_NONNULL(currentMethod, "already called send()");
    currentMethod = nullptr;
    KJ_ASSERT(method.is<HttpConnectMethod>(), "only use accept() with CONNECT requests");
    KJ_REQUIRE(statusCode >= 200 && statusCode < 300, "the statusCode must be 2xx for accept");
    tunnelRejected = nullptr;

    auto& fulfiller = KJ_ASSERT_NONNULL(tunnelWriteGuard, "the tunnel stream was not initialized");
    httpOutput.writeHeaders(headers.serializeResponse(statusCode, statusText));
    auto promise = httpOutput.flush().then([&fulfiller]() {
      fulfiller->fulfill();
    }).eagerlyEvaluate(nullptr);
    fulfiller = fulfiller.attach(kj::mv(promise));
  }

  kj::Own<kj::AsyncOutputStream> reject(
      uint statusCode,
      kj::StringPtr statusText,
      const HttpHeaders& headers,
      kj::Maybe<uint64_t> expectedBodySize) override {
    auto method = KJ_REQUIRE_NONNULL(currentMethod, "already called send()");
    KJ_REQUIRE(method.is<HttpConnectMethod>(), "Only use reject() with CONNECT requests.");
    KJ_REQUIRE(statusCode < 200 || statusCode >= 300, "the statusCode must not be 2xx for reject.");
    tunnelRejected = Maybe<kj::Promise<bool>>(true);

    auto& fulfiller = KJ_ASSERT_NONNULL(tunnelWriteGuard, "the tunnel stream was not initialized");
    fulfiller->reject(KJ_EXCEPTION(DISCONNECTED, "the tunnel request was rejected"));
    closeAfterSend = true;
    return send(statusCode, statusText, headers, expectedBodySize);
  }
};

HttpServer::HttpServer(kj::Timer& timer, const HttpHeaderTable& requestHeaderTable,
                       HttpService& service, Settings settings)
    : HttpServer(timer, requestHeaderTable, &service, settings,
                 kj::newPromiseAndFulfiller<void>()) {}

HttpServer::HttpServer(kj::Timer& timer, const HttpHeaderTable& requestHeaderTable,
                       HttpServiceFactory serviceFactory, Settings settings)
    : HttpServer(timer, requestHeaderTable, kj::mv(serviceFactory), settings,
                 kj::newPromiseAndFulfiller<void>()) {}

HttpServer::HttpServer(kj::Timer& timer, const HttpHeaderTable& requestHeaderTable,
                       kj::OneOf<HttpService*, HttpServiceFactory> service,
                       Settings settings, kj::PromiseFulfillerPair<void> paf)
    : timer(timer), requestHeaderTable(requestHeaderTable), service(kj::mv(service)),
      settings(settings), onDrain(paf.promise.fork()), drainFulfiller(kj::mv(paf.fulfiller)),
      tasks(*this) {}

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
    tasks.add(kj::evalNow([&]() { return listenHttp(kj::mv(connection)); }));
    return listenLoop(port);
  });
}

kj::Promise<void> HttpServer::listenHttp(kj::Own<kj::AsyncIoStream> connection) {
  auto promise = listenHttpImpl(*connection, false /* wantCleanDrain */).ignoreResult();

  // eagerlyEvaluate() to maintain historical guarantee that this method eagerly closes the
  // connection when done.
  return promise.attach(kj::mv(connection)).eagerlyEvaluate(nullptr);
}

kj::Promise<bool> HttpServer::listenHttpCleanDrain(kj::AsyncIoStream& connection) {
  return listenHttpImpl(connection, true /* wantCleanDrain */);
}

kj::Promise<bool> HttpServer::listenHttpImpl(kj::AsyncIoStream& connection, bool wantCleanDrain) {
  kj::Own<HttpService> srv;

  KJ_SWITCH_ONEOF(service) {
    KJ_CASE_ONEOF(ptr, HttpService*) {
      // Fake Own okay because we can assume the HttpService outlives this HttpServer, and we can
      // assume `this` HttpServer outlives the returned `listenHttpCleanDrain()` promise, which will
      // own the fake Own.
      srv = kj::Own<HttpService>(ptr, kj::NullDisposer::instance);
    }
    KJ_CASE_ONEOF(func, HttpServiceFactory) {
      srv = func(connection);
    }
  }

  KJ_ASSERT(srv.get() != nullptr);

  return listenHttpImpl(connection, [srv = kj::mv(srv)](SuspendableRequest&) mutable {
    // This factory function will be owned by the Connection object, meaning the Connection object
    // will own the HttpService. We also know that the Connection object outlives all
    // service.request() promises (service.request() is called from a Connection member function).
    // The Owns we return from this function are attached to the service.request() promises,
    // meaning this factory function will outlive all Owns we return. So, it's safe to return a fake
    // Own.
    return kj::Own<HttpService>(srv.get(), kj::NullDisposer::instance);
  }, nullptr /* suspendedRequest */, wantCleanDrain);
}

kj::Promise<bool> HttpServer::listenHttpCleanDrain(kj::AsyncIoStream& connection,
    SuspendableHttpServiceFactory factory,
    kj::Maybe<SuspendedRequest> suspendedRequest) {
  // Don't close on drain, because a "clean drain" means we return the connection to the
  // application still-open between requests so that it can continue serving future HTTP requests
  // on it.
  return listenHttpImpl(connection, kj::mv(factory), kj::mv(suspendedRequest),
                        true /* wantCleanDrain */);
}

kj::Promise<bool> HttpServer::listenHttpImpl(kj::AsyncIoStream& connection,
    SuspendableHttpServiceFactory factory,
    kj::Maybe<SuspendedRequest> suspendedRequest,
    bool wantCleanDrain) {
  auto obj = heap<Connection>(*this, connection, kj::mv(factory), kj::mv(suspendedRequest),
                              wantCleanDrain);

  // Start reading requests and responding to them, but immediately cancel processing if the client
  // disconnects.
  auto promise = obj->startLoop(true)
      .exclusiveJoin(connection.whenWriteDisconnected().then([]() {return false;}));

  // Eagerly evaluate so that we drop the connection when the promise resolves, even if the caller
  // doesn't eagerly evaluate.
  return promise.attach(kj::mv(obj)).eagerlyEvaluate(nullptr);
}

namespace {
void defaultHandleListenLoopException(kj::Exception&& exception) {
  KJ_LOG(ERROR, "unhandled exception in HTTP server", exception);
}
}  // namespace

void HttpServer::taskFailed(kj::Exception&& exception) {
  KJ_IF_MAYBE(handler, settings.errorHandler) {
    handler->handleListenLoopException(kj::mv(exception));
  } else {
    defaultHandleListenLoopException(kj::mv(exception));
  }
}

HttpServer::SuspendedRequest::SuspendedRequest(
    kj::Array<byte> bufferParam, kj::ArrayPtr<byte> leftoverParam,
    kj::OneOf<HttpMethod, HttpConnectMethod> method,
    kj::StringPtr url, HttpHeaders headers)
    : buffer(kj::mv(bufferParam)),
      leftover(leftoverParam),
      method(method),
      url(url),
      headers(kj::mv(headers)) {
  if (leftover.size() > 0) {
    // We have a `leftover`; make sure it is a slice of `buffer`.
    KJ_ASSERT(leftover.begin() >= buffer.begin() && leftover.begin() <= buffer.end());
    KJ_ASSERT(leftover.end() >= buffer.begin() && leftover.end() <= buffer.end());
  } else {
    // We have no `leftover`, but we still expect it to point into `buffer` somewhere. This is
    // important so that `messageHeaderEnd` is initialized correctly in HttpInputStreamImpl's
    // constructor.
    KJ_ASSERT(leftover.begin() >= buffer.begin() && leftover.begin() <= buffer.end());
  }
}

HttpServer::SuspendedRequest HttpServer::SuspendableRequest::suspend() {
  return connection.suspend(*this);
}

kj::Promise<void> HttpServerErrorHandler::handleClientProtocolError(
    HttpHeaders::ProtocolError protocolError, kj::HttpService::Response& response) {
  // Default error handler implementation.

  HttpHeaderTable headerTable {};
  HttpHeaders headers(headerTable);
  headers.set(HttpHeaderId::CONTENT_TYPE, "text/plain");

  auto errorMessage = kj::str("ERROR: ", protocolError.description);
  auto body = response.send(protocolError.statusCode, protocolError.statusMessage,
                            headers, errorMessage.size());

  return body->write(errorMessage.begin(), errorMessage.size())
      .attach(kj::mv(errorMessage), kj::mv(body));
}

kj::Promise<void> HttpServerErrorHandler::handleApplicationError(
    kj::Exception exception, kj::Maybe<kj::HttpService::Response&> response) {
  // Default error handler implementation.

  if (exception.getType() == kj::Exception::Type::DISCONNECTED) {
    // How do we tell an HTTP client that there was a transient network error, and it should
    // try again immediately? There's no HTTP status code for this (503 is meant for "try
    // again later, not now"). Here's an idea: Don't send any response; just close the
    // connection, so that it looks like the connection between the HTTP client and server
    // was dropped. A good client should treat this exactly the way we want.
    //
    // We also bail here to avoid logging the disconnection, which isn't very interesting.
    return kj::READY_NOW;
  }

  KJ_IF_MAYBE(r, response) {
    KJ_LOG(INFO, "threw exception while serving HTTP response", exception);

    HttpHeaderTable headerTable {};
    HttpHeaders headers(headerTable);
    headers.set(HttpHeaderId::CONTENT_TYPE, "text/plain");

    kj::String errorMessage;
    kj::Own<AsyncOutputStream> body;

    if (exception.getType() == kj::Exception::Type::OVERLOADED) {
      errorMessage = kj::str(
          "ERROR: The server is temporarily unable to handle your request. Details:\n\n", exception);
      body = r->send(503, "Service Unavailable", headers, errorMessage.size());
    } else if (exception.getType() == kj::Exception::Type::UNIMPLEMENTED) {
      errorMessage = kj::str(
          "ERROR: The server does not implement this operation. Details:\n\n", exception);
      body = r->send(501, "Not Implemented", headers, errorMessage.size());
    } else {
      errorMessage = kj::str(
          "ERROR: The server threw an exception. Details:\n\n", exception);
      body = r->send(500, "Internal Server Error", headers, errorMessage.size());
    }

    return body->write(errorMessage.begin(), errorMessage.size())
        .attach(kj::mv(errorMessage), kj::mv(body));
  }

  KJ_LOG(ERROR, "HttpService threw exception after generating a partial response",
                "too late to report error to client", exception);
  return kj::READY_NOW;
}

void HttpServerErrorHandler::handleListenLoopException(kj::Exception&& exception) {
  defaultHandleListenLoopException(kj::mv(exception));
}

kj::Promise<void> HttpServerErrorHandler::handleNoResponse(kj::HttpService::Response& response) {
  HttpHeaderTable headerTable {};
  HttpHeaders headers(headerTable);
  headers.set(HttpHeaderId::CONTENT_TYPE, "text/plain");

  constexpr auto errorMessage = "ERROR: The HttpService did not generate a response."_kj;
  auto body = response.send(500, "Internal Server Error", headers, errorMessage.size());

  return body->write(errorMessage.begin(), errorMessage.size()).attach(kj::mv(body));
}

} // namespace kj
