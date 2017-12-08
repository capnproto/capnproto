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
#include "url.h"
#include <kj/debug.h>
#include <kj/parse/char.h>
#include <unordered_map>
#include <stdlib.h>
#include <kj/encoding.h>
#include <deque>
#include <map>

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
// TODO(cleanup): Move this to a shared hashing library. Maybe. Or maybe don't, becaues no one
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
  for (char c: value) {
    // While the HTTP spec suggests that only printable ASCII characters are allowed in header
    // values, reality has a different opinion. See: https://github.com/httpwg/http11bis/issues/19
    // We follow the browsers' lead.
    KJ_REQUIRE(c != '\0' && c != '\r' && c != '\n', "invalid header value",
        kj::encodeCEscape(value));
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

kj::Maybe<HttpHeaderId> HttpHeaderTable::stringToId(kj::StringPtr name) const {
  auto iter = idsByName->map.find(name);
  if (iter == idsByName->map.end()) {
    return nullptr;
  } else {
    return HttpHeaderId(this, iter->second);
  }
}

// =======================================================================================

HttpHeaders::HttpHeaders(const HttpHeaderTable& table)
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

  bool canReuse() {
    return !broken;
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
        "application did not finish reading previous HTTP response body",
        "can't read next pipelined request/response"));
    onMessageDone = nullptr;
    broken = true;
  }

  // ---------------------------------------------------------------------------

  kj::Promise<bool> awaitNextMessage() {
    // Waits until more data is available, but doesn't consume it. Returns false on EOF.
    //
    // Used on the server after a request is handled, to check for pipelined requests.
    //
    // Used on the client to detect when idle connections are closed from the server end. (In this
    // case, the promise always returns false or is canceled.)

    if (onMessageDone != nullptr) {
      // We're still working on reading the previous body.
      auto fork = messageReadQueue.fork();
      messageReadQueue = fork.addBranch();
      return fork.addBranch().then([this]() {
        return awaitNextMessage();
      });
    }

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
          KJ_FAIL_REQUIRE("invalid HTTP chunk size", text, text.asBytes()) { break; }
          return value;
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

  struct ReleasedBuffer {
    kj::Array<byte> buffer;
    kj::ArrayPtr<byte> leftover;
  };

  ReleasedBuffer releaseBuffer() {
    return { headerBuffer.releaseAsBytes(), leftover.asBytes() };
  }

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

  bool broken = false;
  // Becomes true if the caller failed to read the whole entity-body before closing the stream.

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
  // Stream for an entity-body which is not present. Always returns EOF on read, but tryGetLength()
  // may indicate non-zero in the special case of a response to a HEAD request.

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

  bool canReuse() {
    return !inBody && !broken;
  }

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
    broken = true;

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
  bool broken = false;

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

class WebSocketImpl final: public WebSocket {
public:
  WebSocketImpl(kj::Own<kj::AsyncIoStream> stream,
                kj::Maybe<EntropySource&> maskKeyGenerator,
                kj::Array<byte> buffer = kj::heapArray<byte>(4096),
                kj::ArrayPtr<byte> leftover = nullptr,
                kj::Maybe<kj::Promise<void>> waitBeforeSend = nullptr)
      : stream(kj::mv(stream)), maskKeyGenerator(maskKeyGenerator),
        sendingPong(kj::mv(waitBeforeSend)),
        recvBuffer(kj::mv(buffer)), recvData(leftover) {}

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
    if (!sendClosed) {
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

      sendClosed = true;
    }

    stream->shutdownWrite();
    return kj::READY_NOW;
  }

  kj::Promise<Message> receive() override {
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
          .then([this](size_t actual) -> kj::Promise<Message> {
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
        return receive();
      });
    }

    auto& recvHeader = *reinterpret_cast<Header*>(recvData.begin());

    recvData = recvData.slice(headerSize, recvData.size());

    size_t payloadLen = recvHeader.getPayloadLen();

    auto opcode = recvHeader.getOpcode();
    bool isData = opcode < OPCODE_FIRST_CONTROL;
    if (opcode == OPCODE_CONTINUATION) {
      KJ_REQUIRE(!fragments.empty(), "unexpected continuation frame in WebSocket");

      opcode = fragmentOpcode;
    } else if (isData) {
      KJ_REQUIRE(fragments.empty(), "expected continuation frame in WebSocket");
    }

    bool isFin = recvHeader.isFin();

    kj::Array<byte> message;           // space to allocate
    byte* payloadTarget;               // location into which to read payload (size is payloadLen)
    if (isFin) {
      // Add space for NUL terminator when allocating text message.
      size_t amountToAllocate = payloadLen + (opcode == OPCODE_TEXT && isFin);

      if (isData && !fragments.empty()) {
        // Final frame of a fragmented message. Gather the fragments.
        size_t offset = 0;
        for (auto& fragment: fragments) offset += fragment.size();
        message = kj::heapArray<byte>(offset + amountToAllocate);

        offset = 0;
        for (auto& fragment: fragments) {
          memcpy(message.begin() + offset, fragment.begin(), fragment.size());
          offset += fragment.size();
        }
        payloadTarget = message.begin() + offset;

        fragments.clear();
        fragmentOpcode = 0;
      } else {
        // Single-frame message.
        message = kj::heapArray<byte>(amountToAllocate);
        payloadTarget = message.begin();
      }
    } else {
      // Fragmented message, and this isn't the final fragment.
      KJ_REQUIRE(isData, "WebSocket control frame cannot be fragmented");

      message = kj::heapArray<byte>(payloadLen);
      payloadTarget = message.begin();
      if (fragments.empty()) {
        // This is the first fragment, so set the opcode.
        fragmentOpcode = opcode;
      }
    }

    Mask mask = recvHeader.getMask();

    auto handleMessage = kj::mvCapture(message,
        [this,opcode,payloadTarget,payloadLen,mask,isFin]
        (kj::Array<byte>&& message) -> kj::Promise<Message> {
      if (!mask.isZero()) {
        mask.apply(kj::arrayPtr(payloadTarget, payloadLen));
      }

      if (!isFin) {
        // Add fragment to the list and loop.
        fragments.add(kj::mv(message));
        return receive();
      }

      switch (opcode) {
        case OPCODE_CONTINUATION:
          // Shouldn't get here; handled above.
          KJ_UNREACHABLE;
        case OPCODE_TEXT:
          message.back() = '\0';
          return Message(kj::String(message.releaseAsChars()));
        case OPCODE_BINARY:
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
          return receive();
        case OPCODE_PONG:
          // Unsolicited pong. Ignore.
          return receive();
        default:
          KJ_FAIL_REQUIRE("unknown WebSocket opcode", opcode);
      }
    });

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
          .then([remaining](size_t amount) {
        if (amount < remaining) {
          kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "WebSocket EOF in message"));
        }
      });
      recvData = nullptr;
      return promise.then(kj::mv(handleMessage));
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
    kj::ArrayPtr<const byte> compose(bool fin, byte opcode, uint64_t payloadLen, Mask mask) {
      bytes[0] = (fin ? FIN_MASK : 0) | opcode;
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

    bool hasRsv() const {
      return bytes[0] & RSV_MASK;
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
    static constexpr byte RSV_MASK = 0x70;
    static constexpr byte OPCODE_MASK = 0x0f;

    static constexpr byte USE_MASK_MASK = 0x80;
    static constexpr byte PAYLOAD_LEN_MASK = 0x7f;
  };

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

  bool sendClosed = false;
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
  kj::Vector<kj::Array<byte>> fragments;
  // If `fragments` is non-empty, we've already received some fragments of a message.
  // `fragmentOpcode` is the original opcode.

  kj::Array<byte> recvBuffer;
  kj::ArrayPtr<byte> recvData;

  kj::Promise<void> sendImpl(byte opcode, kj::ArrayPtr<const byte> message) {
    KJ_REQUIRE(!sendClosed, "WebSocket already closed");
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

    sendClosed = opcode == OPCODE_CLOSE;

    Mask mask(maskKeyGenerator);

    kj::Array<byte> ownMessage;
    if (!mask.isZero()) {
      // Sadness, we have to make a copy to apply the mask.
      ownMessage = kj::heapArray(message);
      mask.apply(ownMessage);
      message = ownMessage;
    }

    sendParts[0] = sendHeader.compose(true, opcode, message.size(), mask);
    sendParts[1] = message;

    auto promise = stream->write(sendParts);
    if (!mask.isZero()) {
      promise = promise.attach(kj::mv(ownMessage));
    }
    return promise.then([this]() {
      currentlySending = false;

      // Send queued pong if needed.
      KJ_IF_MAYBE(q, queuedPong) {
        kj::Array<byte> payload = kj::mv(*q);
        queuedPong = nullptr;
        queuePong(kj::mv(payload));
      }
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
      sendingPong = promise->then(kj::mvCapture(payload, [this](kj::Array<byte> payload) mutable {
        return sendPong(kj::mv(payload));
      }));
    } else {
      // We're not sending any pong currently.
      sendingPong = sendPong(kj::mv(payload));
    }
  }

  kj::Promise<void> sendPong(kj::Array<byte> payload) {
    if (sendClosed) {
      return kj::READY_NOW;
    }

    sendParts[0] = sendHeader.compose(true, OPCODE_PONG, payload.size(), Mask(maskKeyGenerator));
    sendParts[1] = payload;
    return stream->write(sendParts).attach(kj::mv(payload));
  }
};

kj::Own<WebSocket> upgradeToWebSocket(
    kj::Own<kj::AsyncIoStream> stream, HttpInputStream& httpInput, HttpOutputStream& httpOutput,
    kj::Maybe<EntropySource&> maskKeyGenerator) {
  // Create a WebSocket upgraded from an HTTP stream.
  auto releasedBuffer = httpInput.releaseBuffer();
  return kj::heap<WebSocketImpl>(kj::mv(stream), maskKeyGenerator,
                                 kj::mv(releasedBuffer.buffer), releasedBuffer.leftover,
                                 httpOutput.flush());
}

}  // namespace

kj::Own<WebSocket> newWebSocket(kj::Own<kj::AsyncIoStream> stream,
                                kj::Maybe<EntropySource&> maskKeyGenerator) {
  return kj::heap<WebSocketImpl>(kj::mv(stream), maskKeyGenerator);
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
        return to.close(close.code, close.reason)
            .attach(kj::mv(close))
            .then([&from,&to]() { return pumpWebSocketLoop(from, to); });
      }
    }
    KJ_UNREACHABLE;
  });
}

kj::Promise<void> WebSocket::pumpTo(WebSocket& other) {
  return kj::evalNow([&]() {
    return pumpWebSocketLoop(*this, other);
  }).catch_([&other](kj::Exception&& e) -> kj::Promise<void> {
    if (e.getType() == kj::Exception::Type::DISCONNECTED) {
      return other.disconnect();
    } else {
      return other.close(1002, e.getDescription());
    }
  });
}


// =======================================================================================

namespace {

class HttpClientImpl final: public HttpClient {
public:
  HttpClientImpl(HttpHeaderTable& responseHeaderTable, kj::Own<kj::AsyncIoStream> rawStream,
                 HttpClientSettings settings)
      : httpInput(*rawStream, responseHeaderTable),
        httpOutput(*rawStream),
        ownStream(kj::mv(rawStream)),
        settings(kj::mv(settings)) {}

  bool canReuse() {
    // Returns true if we can reuse this HttpClient for another request.

    return !upgraded && !closed && httpInput.canReuse() && httpOutput.canReuse();
  }

  Request request(HttpMethod method, kj::StringPtr url, const HttpHeaders& headers,
                  kj::Maybe<uint64_t> expectedBodySize = nullptr) override {
    KJ_REQUIRE(!upgraded,
        "can't make further requests on this HttpClient because it has been or is in the process "
        "of being upgraded");
    KJ_REQUIRE(!closed,
        "this HttpClient's connection has been closed by the server or due to an error");
    closeWatcherTask = nullptr;

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
        HttpClient::Response result {
          r->statusCode,
          r->statusText,
          &httpInput.getHeaders(),
          httpInput.getEntityBody(HttpInputStream::RESPONSE, method, r->statusCode,
                                  r->connectionHeaders)
        };
        if (fastCaseCmp<'c', 'l', 'o', 's', 'e'>(r->connectionHeaders.connection.cStr())) {
          closed = true;
        } else {
          watchForClose();
        }
        return result;
      } else {
        closed = true;
        KJ_FAIL_REQUIRE("received invalid HTTP response") { break; }
        return HttpClient::Response();
      }
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

    HttpHeaders::ConnectionHeaders connectionHeaders;
    connectionHeaders.connection = "Upgrade";
    connectionHeaders.upgrade = "websocket";
    connectionHeaders.websocketVersion = "13";
    connectionHeaders.websocketKey = keyBase64;

    httpOutput.writeHeaders(headers.serializeRequest(HttpMethod::GET, url, connectionHeaders));

    // No entity-body.
    httpOutput.finishBody();

    return httpInput.readResponseHeaders()
        .then(kj::mvCapture(keyBase64,
            [this](kj::StringPtr keyBase64, kj::Maybe<HttpHeaders::Response>&& response)
            -> HttpClient::WebSocketResponse {
      KJ_IF_MAYBE(r, response) {
        if (r->statusCode == 101) {
          if (!fastCaseCmp<'w', 'e', 'b', 's', 'o', 'c', 'k', 'e', 't'>(
                  r->connectionHeaders.upgrade.cStr())) {
            KJ_FAIL_REQUIRE("server returned incorrect Upgrade header; should be 'websocket'",
                r->connectionHeaders.upgrade) { break; }
            return HttpClient::WebSocketResponse();
          }

          auto expectedAccept = generateWebSocketAccept(keyBase64);
          if (r->connectionHeaders.websocketAccept != expectedAccept) {
            KJ_FAIL_REQUIRE("server returned incorrect Sec-WebSocket-Accept header",
                r->connectionHeaders.websocketAccept, expectedAccept) { break; }
            return HttpClient::WebSocketResponse();
          }

          return {
            r->statusCode,
            r->statusText,
            &httpInput.getHeaders(),
            upgradeToWebSocket(kj::mv(ownStream), httpInput, httpOutput, settings.entropySource),
          };
        } else {
          upgraded = false;
          HttpClient::WebSocketResponse result {
            r->statusCode,
            r->statusText,
            &httpInput.getHeaders(),
            httpInput.getEntityBody(HttpInputStream::RESPONSE, HttpMethod::GET, r->statusCode,
                                    r->connectionHeaders)
          };
          if (fastCaseCmp<'c', 'l', 'o', 's', 'e'>(r->connectionHeaders.connection.cStr())) {
            closed = true;
          } else {
            watchForClose();
          }
          return result;
        }
      } else {
        KJ_FAIL_REQUIRE("received invalid HTTP response") { break; }
        return HttpClient::WebSocketResponse();
      }
    }));
  }

private:
  HttpInputStream httpInput;
  HttpOutputStream httpOutput;
  kj::Own<AsyncIoStream> ownStream;
  HttpClientSettings settings;
  kj::Maybe<kj::Promise<void>> closeWatcherTask;
  bool upgraded = false;
  bool closed = false;

  void watchForClose() {
    closeWatcherTask = httpInput.awaitNextMessage().then([this](bool hasData) {
      if (hasData) {
        // Uhh... The server sent some data before we asked for anything. Perhaps due to properties
        // of this application, the server somehow already knows what the next request will be, and
        // it is trying to optimize. Or maybe this is some sort of test and the server is just
        // replaying a script. In any case, we will humor it -- leave the data in the buffer and
        // let it become the response to the next request.
      } else {
        // EOF -- server disconnected.

        // Proactively free up the socket.
        ownStream = nullptr;

        closed = true;
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

kj::Promise<kj::Own<kj::AsyncIoStream>> HttpClient::connect(kj::StringPtr host) {
  KJ_UNIMPLEMENTED("CONNECT is not implemented by this HttpClient");
}

kj::Own<HttpClient> newHttpClient(
    HttpHeaderTable& responseHeaderTable, kj::AsyncIoStream& stream,
    HttpClientSettings settings) {
  return kj::heap<HttpClientImpl>(responseHeaderTable,
      kj::Own<kj::AsyncIoStream>(&stream, kj::NullDisposer::instance),
      kj::mv(settings));
}

// =======================================================================================

namespace {

class PromiseIoStream final: public kj::AsyncIoStream, private kj::TaskSet::ErrorHandler {
  // An AsyncIoStream which waits for a promise to resolve then forwards all calls to the promised
  // stream.
  //
  // TODO(cleanup): Make this more broadly available.

public:
  PromiseIoStream(kj::Promise<kj::Own<AsyncIoStream>> promise)
      : promise(promise.then([this](kj::Own<AsyncIoStream> result) {
          stream = kj::mv(result);
        }).fork()),
        tasks(*this) {}

  kj::Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->read(buffer, minBytes, maxBytes);
    } else {
      return promise.addBranch().then([this,buffer,minBytes,maxBytes]() {
        return KJ_ASSERT_NONNULL(stream)->read(buffer, minBytes, maxBytes);
      });
    }
  }
  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->tryRead(buffer, minBytes, maxBytes);
    } else {
      return promise.addBranch().then([this,buffer,minBytes,maxBytes]() {
        return KJ_ASSERT_NONNULL(stream)->tryRead(buffer, minBytes, maxBytes);
      });
    }
  }

  kj::Maybe<uint64_t> tryGetLength() override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->tryGetLength();
    } else {
      return nullptr;
    }
  }

  kj::Promise<uint64_t> pumpTo(kj::AsyncOutputStream& output, uint64_t amount) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->pumpTo(output, amount);
    } else {
      return promise.addBranch().then([this,&output,amount]() {
        return KJ_ASSERT_NONNULL(stream)->pumpTo(output, amount);
      });
    }
  }

  kj::Promise<void> write(const void* buffer, size_t size) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->write(buffer, size);
    } else {
      return promise.addBranch().then([this,buffer,size]() {
        return KJ_ASSERT_NONNULL(stream)->write(buffer, size);
      });
    }
  }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->write(pieces);
    } else {
      return promise.addBranch().then([this,pieces]() {
        return KJ_ASSERT_NONNULL(stream)->write(pieces);
      });
    }
  }

  kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(
      kj::AsyncInputStream& input, uint64_t amount = kj::maxValue) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->tryPumpFrom(input, amount);
    } else {
      return promise.addBranch().then([this,&input,amount]() {
        // Call input.pumpTo() on the resolved stream instead.
        return input.pumpTo(*KJ_ASSERT_NONNULL(stream), amount);
      });
    }
  }

  void shutdownWrite() override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->shutdownWrite();
    } else {
      tasks.add(promise.addBranch().then([this]() {
        return KJ_ASSERT_NONNULL(stream)->shutdownWrite();
      }));
    }
  }

  void abortRead() override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->abortRead();
    } else {
      tasks.add(promise.addBranch().then([this]() {
        return KJ_ASSERT_NONNULL(stream)->abortRead();
      }));
    }
  }

public:
  kj::ForkedPromise<void> promise;
  kj::Maybe<kj::Own<AsyncIoStream>> stream;
  kj::TaskSet tasks;

  void taskFailed(kj::Exception&& exception) override {
    KJ_LOG(ERROR, exception);
  }
};

class PromiseOutputStream final: public kj::AsyncOutputStream {
  // An AsyncOutputStream which waits for a promise to resolve then forwards all calls to the
  // promised stream.
  //
  // TODO(cleanup): Make this more broadly available.
  // TODO(cleanup): Can this share implementation with PromiseIoStream? Seems hard.

public:
  PromiseOutputStream(kj::Promise<kj::Own<AsyncOutputStream>> promise)
      : promise(promise.then([this](kj::Own<AsyncOutputStream> result) {
          stream = kj::mv(result);
        }).fork()) {}

  kj::Promise<void> write(const void* buffer, size_t size) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->write(buffer, size);
    } else {
      return promise.addBranch().then([this,buffer,size]() {
        return KJ_ASSERT_NONNULL(stream)->write(buffer, size);
      });
    }
  }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->write(pieces);
    } else {
      return promise.addBranch().then([this,pieces]() {
        return KJ_ASSERT_NONNULL(stream)->write(pieces);
      });
    }
  }

  kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(
      kj::AsyncInputStream& input, uint64_t amount = kj::maxValue) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->tryPumpFrom(input, amount);
    } else {
      return promise.addBranch().then([this,&input,amount]() {
        // Call input.pumpTo() on the resolved stream instead.
        return input.pumpTo(*KJ_ASSERT_NONNULL(stream), amount);
      });
    }
  }

public:
  kj::ForkedPromise<void> promise;
  kj::Maybe<kj::Own<AsyncOutputStream>> stream;
};

class NetworkAddressHttpClient final: public HttpClient {
public:
  NetworkAddressHttpClient(kj::Timer& timer, HttpHeaderTable& responseHeaderTable,
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
    result.response = result.response.then(kj::mvCapture(refcounted,
        [](kj::Own<RefcountedClient>&& refcounted, Response&& response) {
      response.body = response.body.attach(kj::mv(refcounted));
      return kj::mv(response);
    }));
    return result;
  }

  kj::Promise<WebSocketResponse> openWebSocket(
      kj::StringPtr url, const HttpHeaders& headers) override {
    auto refcounted = getClient();
    auto result = refcounted->client->openWebSocket(url, headers);
    return result.then(kj::mvCapture(refcounted,
        [](kj::Own<RefcountedClient>&& refcounted, WebSocketResponse&& response) {
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
    }));
  }

private:
  kj::Timer& timer;
  HttpHeaderTable& responseHeaderTable;
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
        auto stream = kj::heap<PromiseIoStream>(address->connect());
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
    // Only return the connection to the pool if it is reusable.
    if (client->canReuse()) {
      availableClients.push_back(AvailableClient {
        kj::mv(client), timer.now() + settings.idleTimout
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
      auto combined = promise.addBranch().then(kj::mvCapture(urlCopy, kj::mvCapture(headersCopy,
          [this,method,expectedBodySize](HttpHeaders&& headers, kj::String&& url)
          -> kj::Tuple<kj::Own<kj::AsyncOutputStream>, kj::Promise<Response>> {
        auto req = KJ_ASSERT_NONNULL(client)->request(method, url, headers, expectedBodySize);
        return kj::tuple(kj::mv(req.body), kj::mv(req.response));
      })));

      auto split = combined.split();
      return {
        kj::heap<PromiseOutputStream>(kj::mv(kj::get<0>(split))),
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
      return promise.addBranch().then(kj::mvCapture(urlCopy, kj::mvCapture(headersCopy,
          [this](HttpHeaders&& headers, kj::String&& url) {
        return KJ_ASSERT_NONNULL(client)->openWebSocket(url, headers);
      })));
    }
  }

private:
  kj::ForkedPromise<void> promise;
  kj::Maybe<kj::Own<NetworkAddressHttpClient>> client;
  bool failed = false;
};

class NetworkHttpClient final: public HttpClient, private kj::TaskSet::ErrorHandler {
public:
  NetworkHttpClient(kj::Timer& timer, HttpHeaderTable& responseHeaderTable,
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
    auto parsed = Url::parse(url, Url::HTTP_PROXY_REQUEST);
    auto path = parsed.toString(Url::HTTP_REQUEST);
    auto headersCopy = headers.clone();
    headersCopy.set(HttpHeaderId::HOST, parsed.host);
    return getClient(parsed).request(method, path, headersCopy, expectedBodySize);
  }

  kj::Promise<WebSocketResponse> openWebSocket(
      kj::StringPtr url, const HttpHeaders& headers) override {
    auto parsed = Url::parse(url, Url::HTTP_PROXY_REQUEST);
    auto path = parsed.toString(Url::HTTP_REQUEST);
    auto headersCopy = headers.clone();
    headersCopy.set(HttpHeaderId::HOST, parsed.host);
    return getClient(parsed).openWebSocket(path, headersCopy);
  }

private:
  kj::Timer& timer;
  HttpHeaderTable& responseHeaderTable;
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

kj::Own<HttpClient> newHttpClient(kj::Timer& timer, HttpHeaderTable& responseHeaderTable,
                                  kj::NetworkAddress& addr, HttpClientSettings settings) {
  return kj::heap<NetworkAddressHttpClient>(timer, responseHeaderTable,
      kj::Own<kj::NetworkAddress>(&addr, kj::NullDisposer::instance), kj::mv(settings));
}

kj::Own<HttpClient> newHttpClient(kj::Timer& timer, HttpHeaderTable& responseHeaderTable,
                                  kj::Network& network, kj::Maybe<kj::Network&> tlsNetwork,
                                  HttpClientSettings settings) {
  return kj::heap<NetworkHttpClient>(
      timer, responseHeaderTable, network, tlsNetwork, kj::mv(settings));
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

  kj::Promise<void> openWebSocket(
      kj::StringPtr url, const HttpHeaders& headers, WebSocketResponse& response) override {
    return client.openWebSocket(url, headers)
        .then([&response](HttpClient::WebSocketResponse&& innerResponse) -> kj::Promise<void> {
      KJ_SWITCH_ONEOF(innerResponse.webSocketOrBody) {
        KJ_CASE_ONEOF(ws, kj::Own<WebSocket>) {
          auto ws2 = response.acceptWebSocket(*innerResponse.headers);
          auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);
          promises.add(ws->pumpTo(*ws2));
          promises.add(ws2->pumpTo(*ws));
          return kj::joinPromises(promises.finish()).attach(kj::mv(ws), kj::mv(ws2));
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

class HttpServer::Connection final: private HttpService::WebSocketResponse {
public:
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
      auto timeoutPromise = server.timer.afterDelay(server.settings.pipelineTimeout)
          .exclusiveJoin(server.onDrain.addBranch())
          .then([this]() -> bool {
        timedOut = true;
        return false;
      });
      firstByte = firstByte.exclusiveJoin(kj::mv(timeoutPromise));
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
      auto timeoutPromise = server.timer.afterDelay(server.settings.headerTimeout)
          .exclusiveJoin(server.onDrain.addBranch())
          .then([this]() -> kj::Maybe<HttpHeaders::Request> {
        timedOut = true;
        return nullptr;
      });
      receivedHeaders = receivedHeaders.exclusiveJoin(kj::mv(timeoutPromise));
    }

    return receivedHeaders
        .then([this](kj::Maybe<HttpHeaders::Request>&& request) -> kj::Promise<void> {
      if (closed) {
        // Client closed connection. Close our end too.
        return httpOutput.flush();
      }
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

        return httpOutput.flush();
      }

      KJ_IF_MAYBE(req, request) {
        kj::Promise<void> promise = nullptr;

        if (fastCaseCmp<'w', 'e', 'b', 's', 'o', 'c', 'k', 'e', 't'>(
                req->connectionHeaders.upgrade.cStr())) {
          if (req->method != HttpMethod::GET) {
            return sendError(400, "Bad Request", kj::str(
                "ERROR: WebSocket must be initiated with a GET request."));
          }

          if (req->connectionHeaders.websocketVersion != "13") {
            return sendError(400, "Bad Request", kj::str(
                "ERROR: The requested WebSocket version is not supported."));
          }

          if (req->connectionHeaders.websocketKey == nullptr) {
            return sendError(400, "Bad Request", kj::str("ERROR: Missing Sec-WebSocket-Key"));
          }

          currentMethod = HttpMethod::GET;
          websocketKey = kj::str(req->connectionHeaders.websocketKey);
          promise = server.service.openWebSocket(req->url, httpInput.getHeaders(), *this);
        } else {
          currentMethod = req->method;
          websocketKey = nullptr;
          auto body = httpInput.getEntityBody(
              HttpInputStream::REQUEST, req->method, 0, req->connectionHeaders);

          // TODO(perf): If the client disconnects, should we cancel the response? Probably, to
          //   prevent permanent deadlock. It's slightly weird in that arguably the client should
          //   be able to shutdown the upstream but still wait on the downstream, but I believe many
          //   other HTTP servers do similar things.

          promise = server.service.request(
              req->method, req->url, httpInput.getHeaders(), *body, *this);
          promise = promise.attach(kj::mv(body));
        }

        return promise
            .then([this]() -> kj::Promise<void> {
          // Response done. Await next request.

          if (upgraded) {
            // We've upgraded to WebSocket so we can exit this listen loop. In fact, we no longer
            // own the stream.
            //
            // Note that the WebSocket itself also flush()es the httpOutput before writing any
            // WebSocket content, but we should also make sure that we don't let the listen loop
            // exit until that flush is done, since we can't destroy the HttpOutputStream in the
            // meantime.
            return httpOutput.flush();
          }

          if (currentMethod != nullptr) {
            return sendError(500, "Internal Server Error", kj::str(
                "ERROR: The HttpService did not generate a response."));
          }

          if (server.draining) {
            // Never mind, drain time.
            return httpOutput.flush();
          }

          return httpOutput.flush().then([this]() { return loop(false); });
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
  kj::Maybe<kj::String> websocketKey;
  bool timedOut = false;
  bool closed = false;
  bool upgraded = false;

  kj::Own<kj::AsyncOutputStream> send(
      uint statusCode, kj::StringPtr statusText, const HttpHeaders& headers,
      kj::Maybe<uint64_t> expectedBodySize) override {
    auto method = KJ_REQUIRE_NONNULL(currentMethod, "already called startResponse()");
    currentMethod = nullptr;

    if (websocketKey != nullptr) {
      // This was a WebSocket request but the upgrade wasn't accepted.
      websocketKey = nullptr;
      httpInput.finishRead();
    }

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

  kj::Own<WebSocket> acceptWebSocket(const HttpHeaders& headers) override {
    auto key = KJ_REQUIRE_NONNULL(kj::mv(websocketKey), "not a WebSocket request");
    currentMethod = nullptr;
    websocketKey = nullptr;
    upgraded = true;

    auto websocketAccept = generateWebSocketAccept(key);

    HttpHeaders::ConnectionHeaders connectionHeaders;
    connectionHeaders.websocketAccept = websocketAccept;
    connectionHeaders.upgrade = "websocket";
    connectionHeaders.connection = "Upgrade";

    httpOutput.writeHeaders(headers.serializeResponse(
        101, "Switching Protocols", connectionHeaders));

    return upgradeToWebSocket(kj::mv(ownStream), httpInput, httpOutput, nullptr);
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
