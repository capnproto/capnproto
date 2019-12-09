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
#include <queue>
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
    case 'A': EXPECT_REST(A,CL)
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

HttpHeaders::HttpHeaders(const HttpHeaderTable& table)
    : table(&table),
      indexedHeaders(kj::heapArray<kj::StringPtr>(table.idCount())) {}

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
  char* end = trimHeaderEnding(content);
  if (end == nullptr) {
    return ProtocolError { 400, "Bad Request",
        "ERROR: Request headers have no terminal newline.", content };
  }

  char* ptr = content.begin();

  HttpHeaders::Request request;

  KJ_IF_MAYBE(method, consumeHttpMethod(ptr)) {
    request.method = *method;
    if (*ptr != ' ' && *ptr != '\t') {
      return ProtocolError { 501, "Not Implemented",
          "ERROR: Unrecognized request method.", content };
    }
    ++ptr;
  } else {
    return ProtocolError { 501, "Not Implemented",
        "ERROR: Unrecognized request method.", content };
  }

  KJ_IF_MAYBE(path, consumeWord(ptr)) {
    request.url = *path;
  } else {
    return ProtocolError { 400, "Bad Request",
        "ERROR: Invalid request line.", content };
  }

  // Ignore rest of line. Don't care about "HTTP/1.1" or whatever.
  consumeLine(ptr);

  if (!parseHeaders(ptr, end)) {
    return ProtocolError { 400, "Bad Request",
        "ERROR: The headers sent by your client are not valid.", content };
  }

  return request;
}

HttpHeaders::ResponseOrProtocolError HttpHeaders::tryParseResponse(kj::ArrayPtr<char> content) {
  char* end = trimHeaderEnding(content);
  if (end == nullptr) {
    return ProtocolError { 502, "Bad Gateway",
        "ERROR: Response headers have no terminal newline.", content };
  }

  char* ptr = content.begin();

  HttpHeaders::Response response;

  KJ_IF_MAYBE(version, consumeWord(ptr)) {
    if (!version->startsWith("HTTP/")) {
      return ProtocolError { 502, "Bad Gateway",
          "ERROR: Invalid response status line (invalid protocol).", content };
    }
  } else {
    return ProtocolError { 502, "Bad Gateway",
        "ERROR: Invalid response status line (no spaces).", content };
  }

  KJ_IF_MAYBE(code, consumeNumber(ptr)) {
    response.statusCode = *code;
  } else {
    return ProtocolError { 502, "Bad Gateway",
        "ERROR: Invalid response status line (invalid status code).", content };
  }

  response.statusText = consumeLine(ptr);

  if (!parseHeaders(ptr, end)) {
    return ProtocolError { 502, "Bad Gateway",
        "ERROR: The headers sent by the server are not valid.", content };
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

static constexpr size_t MIN_BUFFER = 4096;
static constexpr size_t MAX_BUFFER = 65536;
static constexpr size_t MAX_CHUNK_HEADER_SIZE = 32;

class HttpInputStreamImpl final: public HttpInputStream {
public:
  explicit HttpInputStreamImpl(AsyncInputStream& inner, HttpHeaderTable& table)
      : inner(inner), headerBuffer(kj::heapArray<char>(MIN_BUFFER)), headers(table) {
  }

  bool canReuse() {
    return !broken && pendingMessageCount == 0;
  }

  // ---------------------------------------------------------------------------
  // public interface

  kj::Promise<Request> readRequest() override {
    return readRequestHeaders()
        .then([this](HttpHeaders::RequestOrProtocolError&& requestOrProtocolError)
            -> HttpInputStream::Request {
      auto request = KJ_REQUIRE_NONNULL(
          requestOrProtocolError.tryGet<HttpHeaders::Request>(), "bad request");
      auto body = getEntityBody(HttpInputStreamImpl::REQUEST, request.method, 0, headers);

      return { request.method, request.url, headers, kj::mv(body) };
    });
  }

  kj::Promise<Response> readResponse(HttpMethod requestMethod) override {
    return readResponseHeaders()
        .then([this,requestMethod](HttpHeaders::ResponseOrProtocolError&& responseOrProtocolError)
              -> HttpInputStream::Response {
      auto response = KJ_REQUIRE_NONNULL(
          responseOrProtocolError.tryGet<HttpHeaders::Response>(), "bad response");
      auto body = getEntityBody(HttpInputStreamImpl::RESPONSE, requestMethod, 0, headers);

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

  inline kj::Promise<HttpHeaders::RequestOrProtocolError> readRequestHeaders() {
    return readMessageHeaders().then([this](kj::ArrayPtr<char> text) {
      headers.clear();
      return headers.tryParseRequest(text);
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
      RequestOrResponse type, HttpMethod method, uint statusCode,
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

  bool lineBreakBeforeNextHeader = false;
  // If true, the next await should expect to start with a spurrious '\n' or '\r\n'. This happens
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
  HttpEntityBodyReader(HttpInputStreamImpl& inner): inner(inner) {}
  ~HttpEntityBodyReader() noexcept(false) {
    if (!finished) {
      inner.abortRead();
    }
  }

protected:
  HttpInputStreamImpl& inner;

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
  HttpNullEntityReader(HttpInputStreamImpl& inner, kj::Maybe<uint64_t> length)
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
  HttpConnectionCloseEntityReader(HttpInputStreamImpl& inner)
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
  HttpFixedLengthEntityReader(HttpInputStreamImpl& inner, size_t length)
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
  HttpChunkedEntityReader(HttpInputStreamImpl& inner)
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
        if (amount < minBytes) {
          kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "premature EOF in HTTP chunk"));
        }
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

kj::Own<kj::AsyncInputStream> HttpInputStreamImpl::getEntityBody(
    RequestOrResponse type, HttpMethod method, uint statusCode,
    const kj::HttpHeaders& headers) {
  // Rules to determine how HTTP entity-body is delimited:
  //   https://tools.ietf.org/html/rfc7230#section-3.3.3

  // #1
  if (type == RESPONSE) {
    if (method == HttpMethod::HEAD) {
      // Body elided.
      kj::Maybe<uint64_t> length;
      KJ_IF_MAYBE(cl, headers.get(HttpHeaderId::CONTENT_LENGTH)) {
        length = strtoull(cl->cStr(), nullptr, 10);
      } else if (headers.get(HttpHeaderId::TRANSFER_ENCODING) == nullptr) {
        // HACK: Neither Content-Length nor Transfer-Encoding header in response to HEAD request.
        //   Propagate this fact with a 0 expected body length.
        length = uint64_t(0);
      }
      return kj::heap<HttpNullEntityReader>(*this, length);
    } else if (statusCode == 204 || statusCode == 205 || statusCode == 304) {
      // No body.
      return kj::heap<HttpNullEntityReader>(*this, uint64_t(0));
    }
  }

  // #2 deals with the CONNECT method which is handled separately.

  // #3
  KJ_IF_MAYBE(te, headers.get(HttpHeaderId::TRANSFER_ENCODING)) {
    // TODO(someday): Support plugable transfer encodings? Or at least gzip?
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
    } else {
      KJ_FAIL_REQUIRE("unknown transfer encoding", *te) { break; }
    }
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

kj::Own<HttpInputStream> newHttpInputStream(kj::AsyncInputStream& input, HttpHeaderTable& table) {
  return kj::heap<HttpInputStreamImpl>(input, table);
}

// =======================================================================================

namespace {

class HttpOutputStream {
public:
  HttpOutputStream(AsyncOutputStream& inner): inner(inner) {}

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

    // TODO(soon): Queueing writes this way is flawed, because it means the caller cannot cancel
    //   the write.
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
  Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
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
  Promise<void> whenWriteDisconnected() override {
    return kj::NEVER_DONE;
  }
};

class HttpFixedLengthEntityWriter final: public kj::AsyncOutputStream {
public:
  HttpFixedLengthEntityWriter(HttpOutputStream& inner, uint64_t length)
      : inner(inner), length(length) {
    if (length == 0) inner.finishBody();
  }
  ~HttpFixedLengthEntityWriter() noexcept(false) {
    if (length > 0 || inner.isWriteInProgress()) {
      inner.abortBody();
    }
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

  Promise<void> whenWriteDisconnected() override {
    return inner.whenWriteDisconnected();
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
    if (inner.canWriteBodyData()) {
      inner.writeBodyData(kj::str("0\r\n\r\n"));
      inner.finishBody();
    } else {
      inner.abortBody();
    }
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

    auto header = kj::str(kj::hex(size), "\r\n");
    auto partsBuilder = kj::heapArrayBuilder<ArrayPtr<const byte>>(pieces.size() + 2);
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
      inner.writeBodyData(kj::str(kj::hex(length), "\r\n"));
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

  Promise<void> whenWriteDisconnected() override {
    return inner.whenWriteDisconnected();
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
  kj::Vector<kj::Array<byte>> fragments;
  // If `fragments` is non-empty, we've already received some fragments of a message.
  // `fragmentOpcode` is the original opcode.

  kj::Array<byte> recvBuffer;
  kj::ArrayPtr<byte> recvData;

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
    if (hasSentClose || disconnected) {
      return kj::READY_NOW;
    }

    sendParts[0] = sendHeader.compose(true, OPCODE_PONG, payload.size(), Mask(maskKeyGenerator));
    sendParts[1] = payload;
    return stream->write(sendParts).attach(kj::mv(payload));
  }
};

kj::Own<WebSocket> upgradeToWebSocket(
    kj::Own<kj::AsyncIoStream> stream, HttpInputStreamImpl& httpInput, HttpOutputStream& httpOutput,
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
      return s->send(message);
    } else {
      return newAdaptedPromise<void, BlockedSend>(*this, MessagePtr(message));
    }
  }
  kj::Promise<void> send(kj::ArrayPtr<const char> message) override {
    KJ_IF_MAYBE(s, state) {
      return s->send(message);
    } else {
      return newAdaptedPromise<void, BlockedSend>(*this, MessagePtr(message));
    }
  }
  kj::Promise<void> close(uint16_t code, kj::StringPtr reason) override {
    KJ_IF_MAYBE(s, state) {
      return s->close(code, reason);
    } else {
      return newAdaptedPromise<void, BlockedSend>(*this, MessagePtr(ClosePtr { code, reason }));
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

  kj::Promise<Message> receive() override {
    KJ_IF_MAYBE(s, state) {
      return s->receive();
    } else {
      return newAdaptedPromise<Message, BlockedReceive>(*this);
    }
  }
  kj::Promise<void> pumpTo(WebSocket& other) override {
    KJ_IF_MAYBE(s, state) {
      return s->pumpTo(other);
    } else {
      return newAdaptedPromise<void, BlockedPumpTo>(*this, other);
    }
  }

private:
  kj::Maybe<WebSocket&> state;
  // Object-oriented state! If any method call is blocked waiting on activity from the other end,
  // then `state` is non-null and method calls should be forwarded to it. If no calls are
  // outstanding, `state` is null.

  kj::Own<WebSocket> ownState;

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

    kj::Promise<Message> receive() override {
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

    kj::Promise<Message> receive() override {
      KJ_REQUIRE(canceler.isEmpty(), "another message receive is already in progress");
      return canceler.wrap(input.receive()
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

  private:
    kj::PromiseFulfiller<void>& fulfiller;
    WebSocketPipeImpl& pipe;
    WebSocket& input;
    Canceler canceler;
  };

  class BlockedReceive final: public WebSocket {
  public:
    BlockedReceive(kj::PromiseFulfiller<Message>& fulfiller, WebSocketPipeImpl& pipe)
        : fulfiller(fulfiller), pipe(pipe) {
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
      return canceler.wrap(other.receive().then([this,&other](Message message) {
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

    kj::Promise<Message> receive() override {
      KJ_FAIL_ASSERT("another message receive is already in progress");
    }
    kj::Promise<void> pumpTo(WebSocket& other) override {
      KJ_FAIL_ASSERT("another message receive is already in progress");
    }

  private:
    kj::PromiseFulfiller<Message>& fulfiller;
    WebSocketPipeImpl& pipe;
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
      }));
    }
    kj::Promise<void> disconnect() override {
      KJ_REQUIRE(canceler.isEmpty(), "another message send is already in progress");
      return canceler.wrap(output.disconnect().then([this]() {
        canceler.release();
        pipe.endState(*this);
        fulfiller.fulfill();
        return pipe.disconnect();
      }));
    }
    kj::Maybe<kj::Promise<void>> tryPumpFrom(WebSocket& other) override {
      KJ_REQUIRE(canceler.isEmpty(), "another message send is already in progress");
      return canceler.wrap(other.pumpTo(output).then([this]() {
        canceler.release();
        pipe.endState(*this);
        fulfiller.fulfill();
      }));
    }

    kj::Promise<Message> receive() override {
      KJ_FAIL_ASSERT("another message receive is already in progress");
    }
    kj::Promise<void> pumpTo(WebSocket& other) override {
      KJ_FAIL_ASSERT("another message receive is already in progress");
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

    kj::Promise<Message> receive() override {
      return KJ_EXCEPTION(DISCONNECTED, "WebSocket disconnected");
    }
    kj::Promise<void> pumpTo(WebSocket& other) override {
      return kj::READY_NOW;
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

    kj::Promise<Message> receive() override {
      return KJ_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
    }
    kj::Promise<void> pumpTo(WebSocket& other) override {
      return KJ_EXCEPTION(DISCONNECTED, "other end of WebSocketPipe was destroyed");
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

  kj::Promise<Message> receive() override {
    return in->receive();
  }
  kj::Promise<void> pumpTo(WebSocket& other) override {
    return in->pumpTo(other);
  }

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
          // TODO(someday): Do something with ProtocolError::rawContent. Exceptions feel like the
          //   most idiomatic way to report errors when using HttpClient::request(), but we don't
          //   have a good way of attaching the raw content to the exception.
          KJ_FAIL_REQUIRE(protocolError.description) { break; }
          return HttpClient::Response();
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

    httpOutput.writeHeaders(headers.serializeRequest(HttpMethod::GET, url, connectionHeaders));

    // No entity-body.
    httpOutput.finishBody();

    auto id = ++counter;

    return httpInput.readResponseHeaders()
        .then([this,id,keyBase64 = kj::mv(keyBase64)](
            HttpHeaders::ResponseOrProtocolError&& responseOrProtocolError)
            -> HttpClient::WebSocketResponse {
      KJ_SWITCH_ONEOF(responseOrProtocolError) {
        KJ_CASE_ONEOF(response, HttpHeaders::Response) {
          auto& responseHeaders = httpInput.getHeaders();
          if (response.statusCode == 101) {
            if (!fastCaseCmp<'w', 'e', 'b', 's', 'o', 'c', 'k', 'e', 't'>(
                    responseHeaders.get(HttpHeaderId::UPGRADE).orDefault(nullptr).cStr())) {
              KJ_FAIL_REQUIRE("server returned incorrect Upgrade header; should be 'websocket'",
                              responseHeaders.get(HttpHeaderId::UPGRADE).orDefault("(null)")) {
                break;
              }
              return HttpClient::WebSocketResponse();
            }

            auto expectedAccept = generateWebSocketAccept(keyBase64);
            if (responseHeaders.get(HttpHeaderId::SEC_WEBSOCKET_ACCEPT).orDefault(nullptr)
                  != expectedAccept) {
              KJ_FAIL_REQUIRE("server returned incorrect Sec-WebSocket-Accept header",
                  responseHeaders.get(HttpHeaderId::SEC_WEBSOCKET_ACCEPT).orDefault("(null)"),
                  expectedAccept) { break; }
              return HttpClient::WebSocketResponse();
            }

            return {
              response.statusCode,
              response.statusText,
              &httpInput.getHeaders(),
              upgradeToWebSocket(kj::mv(ownStream), httpInput, httpOutput, settings.entropySource),
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
          // TODO(someday): Do something with ProtocolError::rawContent. Exceptions feel like the
          //   most idiomatic way to report errors when using HttpClient::request(), but we don't
          //   have a good way of attaching the raw content to the exception.
          KJ_FAIL_REQUIRE(protocolError.description) { break; }
          return HttpClient::WebSocketResponse();
        }
      }

      KJ_UNREACHABLE;
    });
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
    if (client->canReuse() && settings.idleTimout > 0 * kj::SECONDS) {
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

class ConcurrencyLimitingHttpClient final: public HttpClient {
public:
  ConcurrencyLimitingHttpClient(
      kj::HttpClient& inner, uint maxConcurrentRequests,
      kj::Function<void(uint runningCount, uint pendingCount)> countChangedCallback)
      : inner(inner),
        maxConcurrentRequests(maxConcurrentRequests),
        countChangedCallback(kj::mv(countChangedCallback)) {}

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
    if (concurrentRequests >= maxConcurrentRequests) { return; }
    if (pendingRequests.empty()) { return; }

    auto fulfiller = kj::mv(pendingRequests.front());
    pendingRequests.pop();
    fulfiller->fulfill(ConnectionCounter(*this));
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

class NullInputStream final: public kj::AsyncInputStream {
public:
  NullInputStream(kj::Maybe<size_t> expectedLength = size_t(0))
      : expectedLength(expectedLength) {}

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return size_t(0);
  }

  kj::Maybe<uint64_t> tryGetLength() override {
    return expectedLength;
  }

  kj::Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
    return uint64_t(0);
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

  kj::Promise<kj::Own<kj::AsyncIoStream>> connect(kj::StringPtr host) override {
    return service.connect(kj::mv(host));
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
      return inner->tryRead(buffer, minBytes, maxBytes)
          .then([this, minBytes](size_t amount) -> kj::Promise<size_t> {
        if (amount < minBytes) {
          // Must have reached EOF.
          KJ_IF_MAYBE(t, completionTask) {
            // Delay until completion.
            auto result = t->then([amount]() { return amount; });
            completionTask = nullptr;
            return result;
          } else {
            // Must have called tryRead() again after we already signaled EOF. Fine.
            return amount;
          }
        } else {
          return amount;
        }
      });
    }

    kj::Maybe<uint64_t> tryGetLength() override {
      return inner->tryGetLength();
    }

    kj::Promise<uint64_t> pumpTo(kj::AsyncOutputStream& output, uint64_t amount) override {
      return inner->pumpTo(output, amount)
          .then([this,amount](uint64_t actual) -> kj::Promise<uint64_t> {
        if (actual < amount) {
          // Must have reached EOF.
          KJ_IF_MAYBE(t, completionTask) {
            // Delay until completion.
            auto result = t->then([amount]() { return amount; });
            completionTask = nullptr;
            return result;
          } else {
            // Must have called tryRead() again after we already signaled EOF. Fine.
            return amount;
          }
        } else {
          return amount;
        }
      });
    }

  private:
    kj::Own<kj::AsyncInputStream> inner;
    kj::Maybe<kj::Promise<void>> completionTask;
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
          KJ_LOG(ERROR, "HttpService threw an exception after having already started responding",
                        exception);
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
    kj::Promise<Message> receive() override {
      return inner->receive().then([this](Message&& message) -> kj::Promise<Message> {
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
          KJ_LOG(ERROR, "HttpService threw an exception after having already started responding",
                        exception);
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

      return kj::joinPromises(promises.finish());
    } else {
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
  }

  kj::Promise<kj::Own<kj::AsyncIoStream>> connect(kj::StringPtr host) override {
    return client.connect(kj::mv(host));
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

kj::Promise<kj::Own<kj::AsyncIoStream>> HttpService::connect(kj::StringPtr host) {
  KJ_UNIMPLEMENTED("CONNECT is not implemented by this HttpService");
}

class HttpServer::Connection final: private HttpService::Response,
                                    private HttpServerErrorHandler {
public:
  Connection(HttpServer& server, kj::AsyncIoStream& stream,
             HttpService& service)
      : server(server),
        stream(stream),
        service(service),
        httpInput(stream, server.requestHeaderTable),
        httpOutput(stream) {
    ++server.connectionCount;
  }
  ~Connection() noexcept(false) {
    if (--server.connectionCount == 0) {
      KJ_IF_MAYBE(f, server.zeroConnectionsFulfiller) {
        f->get()->fulfill();
      }
    }
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
        timeoutPromise = timeoutPromise.exclusiveJoin(server.onDrain.addBranch());
      }

      firstByte = firstByte.exclusiveJoin(timeoutPromise.then([this]() -> bool {
        timedOut = true;
        return false;
      }));
    }

    auto receivedHeaders = firstByte
        .then([this,firstRequest](bool hasData)
            -> kj::Promise<HttpHeaders::RequestOrProtocolError> {
      if (hasData) {
        auto readHeaders = httpInput.readRequestHeaders();
        if (!firstRequest) {
          // On requests other than the first, the header timeout starts ticking when we receive
          // the first byte of a pipeline response.
          readHeaders = readHeaders.exclusiveJoin(
              server.timer.afterDelay(server.settings.headerTimeout)
              .then([this]() -> HttpHeaders::RequestOrProtocolError {
            timedOut = true;
            return HttpHeaders::ProtocolError {
              408, "Request Timeout",
              "ERROR: Timed out waiting for next request headers.", nullptr
            };
          }));
        }
        return kj::mv(readHeaders);
      } else {
        // Client closed connection or pipeline timed out with no bytes received. This is not an
        // error, so don't report one.
        this->closed = true;
        return HttpHeaders::RequestOrProtocolError(HttpHeaders::ProtocolError {
          408, "Request Timeout",
          "ERROR: Client closed connection or connection timeout "
          "while waiting for request headers.", nullptr
        });
      }
    });

    if (firstRequest) {
      // On the first request, the header timeout starts ticking immediately upon request opening.
      auto timeoutPromise = server.timer.afterDelay(server.settings.headerTimeout)
          .exclusiveJoin(server.onDrain.addBranch())
          .then([this]() -> HttpHeaders::RequestOrProtocolError {
        timedOut = true;
        return HttpHeaders::ProtocolError {
          408, "Request Timeout",
          "ERROR: Timed out waiting for initial request headers.", nullptr
        };
      });
      receivedHeaders = receivedHeaders.exclusiveJoin(kj::mv(timeoutPromise));
    }

    return receivedHeaders
        .then([this](HttpHeaders::RequestOrProtocolError&& requestOrProtocolError)
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
        KJ_CASE_ONEOF(request, HttpHeaders::Request) {
          auto& headers = httpInput.getHeaders();

          currentMethod = request.method;
          auto body = httpInput.getEntityBody(
              HttpInputStreamImpl::REQUEST, request.method, 0, headers);

          // TODO(perf): If the client disconnects, should we cancel the response? Probably, to
          //   prevent permanent deadlock. It's slightly weird in that arguably the client should
          //   be able to shutdown the upstream but still wait on the downstream, but I believe many
          //   other HTTP servers do similar things.

          auto promise = service.request(
              request.method, request.url, headers, *body, *this);
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
              if (!webSocketClosed) {
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

                // Note that we don't have to handle server.draining here because we'll take care of
                // it the next time around the loop.
                return loop(false);
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
                // There's no way we can get out of this perfectly cleanly. HTTP just isn't good
                // enough at connection management. The best we can do is give the client some grace
                // period and then abort the connection.

                auto dummy = kj::heap<HttpDiscardingEntityWriter>();
                auto lengthGrace = body->pumpTo(*dummy, server.settings.canceledUploadGraceBytes)
                    .then([this](size_t amount) {
                  if (httpInput.canReuse()) {
                    // Success, we can continue.
                    return true;
                  } else {
                    // Still more data. Give up.
                    return false;
                  }
                });
                lengthGrace = lengthGrace.attach(kj::mv(dummy), kj::mv(body));

                auto timeGrace = server.timer.afterDelay(server.settings.canceledUploadGacePeriod)
                    .then([]() { return false; });

                return lengthGrace.exclusiveJoin(kj::mv(timeGrace))
                    .then([this](bool clean) -> kj::Promise<bool> {
                  if (clean) {
                    // We recovered. Continue loop.
                    return loop(false);
                  } else {
                    // Client still not done. Return broken.
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
    }).catch_([this](kj::Exception&& e) -> kj::Promise<bool> {
      // Exception; report 5xx.

      KJ_IF_MAYBE(p, webSocketError) {
        // sendWebSocketError() was called. Finish sending and close the connection. Don't log
        // the exception because it's probably a side-effect of this.
        auto promise = kj::mv(*p);
        webSocketError = nullptr;
        return kj::mv(promise);
      }

      return sendError(kj::mv(e));
    });
  }

private:
  HttpServer& server;
  kj::AsyncIoStream& stream;
  HttpService& service;
  HttpInputStreamImpl httpInput;
  HttpOutputStream httpOutput;
  kj::Maybe<HttpMethod> currentMethod;
  bool timedOut = false;
  bool closed = false;
  bool upgraded = false;
  bool webSocketClosed = false;
  bool closeAfterSend = false;  // True if send() should set Connection: close.
  kj::Maybe<kj::Promise<bool>> webSocketError;

  kj::Own<kj::AsyncOutputStream> send(
      uint statusCode, kj::StringPtr statusText, const HttpHeaders& headers,
      kj::Maybe<uint64_t> expectedBodySize) override {
    auto method = KJ_REQUIRE_NONNULL(currentMethod, "already called send()");
    currentMethod = nullptr;

    kj::StringPtr connectionHeaders[HttpHeaders::CONNECTION_HEADERS_COUNT];
    kj::String lengthStr;

    if (closeAfterSend) {
      connectionHeaders[HttpHeaders::BuiltinIndices::CONNECTION] = "close";
    }

    if (statusCode == 204 || statusCode == 205 || statusCode == 304) {
      // No entity-body.
    } else KJ_IF_MAYBE(s, expectedBodySize) {
      // HACK: We interpret a zero-length expected body length on responses to HEAD requests to mean
      //   "don't set a Content-Length header at all." This provides a way to omit a body header on
      //   HEAD responses with non-null-body status codes. This is a hack that *only* makes sense
      //   for HEAD responses.
      if (method != HttpMethod::HEAD || *s > 0) {
        lengthStr = kj::str(*s);
        connectionHeaders[HttpHeaders::BuiltinIndices::CONTENT_LENGTH] = lengthStr;
      }
    } else {
      connectionHeaders[HttpHeaders::BuiltinIndices::TRANSFER_ENCODING] = "chunked";
    }

    // For HEAD requests, if the application specified a Content-Length or Transfer-Encoding
    // header, use that instead of whatever we decided above.
    kj::ArrayPtr<kj::StringPtr> connectionHeadersArray = connectionHeaders;
    if (method == HttpMethod::HEAD) {
      if (headers.get(HttpHeaderId::CONTENT_LENGTH) != nullptr ||
          headers.get(HttpHeaderId::TRANSFER_ENCODING) != nullptr) {
        connectionHeadersArray = connectionHeadersArray
            .slice(0, HttpHeaders::HEAD_RESPONSE_CONNECTION_HEADERS_COUNT);
      }
    }

    httpOutput.writeHeaders(headers.serializeResponse(
        statusCode, statusText, connectionHeadersArray));

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
    auto& requestHeaders = httpInput.getHeaders();
    KJ_REQUIRE(requestHeaders.isWebSocket(),
        "can't call acceptWebSocket() if the request headers didn't have Upgrade: WebSocket");

    auto method = KJ_REQUIRE_NONNULL(currentMethod, "already called send()");
    // Unlike send(), we neither need nor want to null out currentMethod. The error cases below
    // depend on it being non-null to allow error responses to be sent, and the happy path expects
    // it to be GET.

    if (method != HttpMethod::GET) {
      return sendWebSocketError("ERROR: WebSocket must be initiated with a GET request.");
    }

    if (requestHeaders.get(HttpHeaderId::SEC_WEBSOCKET_VERSION).orDefault(nullptr) != "13") {
      return sendWebSocketError("ERROR: The requested WebSocket version is not supported.");
    }

    kj::String key;
    KJ_IF_MAYBE(k, requestHeaders.get(HttpHeaderId::SEC_WEBSOCKET_KEY)) {
      key = kj::str(*k);
    } else {
      return sendWebSocketError("ERROR: Missing Sec-WebSocket-Key");
    }

    auto websocketAccept = generateWebSocketAccept(key);

    kj::StringPtr connectionHeaders[HttpHeaders::WEBSOCKET_CONNECTION_HEADERS_COUNT];
    connectionHeaders[HttpHeaders::BuiltinIndices::SEC_WEBSOCKET_ACCEPT] = websocketAccept;
    connectionHeaders[HttpHeaders::BuiltinIndices::UPGRADE] = "websocket";
    connectionHeaders[HttpHeaders::BuiltinIndices::CONNECTION] = "Upgrade";

    httpOutput.writeHeaders(headers.serializeResponse(
        101, "Switching Protocols", connectionHeaders));

    upgraded = true;
    // We need to give the WebSocket an Own<AsyncIoStream>, but we only have a reference. This is
    // safe because the application is expected to drop the WebSocket object before returning
    // from the request handler. For some extra safety, we check that webSocketClosed has been
    // set true when the handler returns.
    auto deferNoteClosed = kj::defer([this]() { webSocketClosed = true; });
    kj::Own<kj::AsyncIoStream> ownStream(&stream, kj::NullDisposer::instance);
    return upgradeToWebSocket(ownStream.attach(kj::mv(deferNoteClosed)),
                              httpInput, httpOutput, nullptr);
  }

  kj::Promise<bool> sendError(HttpHeaders::ProtocolError protocolError) {
    closeAfterSend = true;

    // Client protocol errors always happen on request headers parsing, before we call into the
    // HttpService, meaning no response has been sent and we can provide a Response object.
    auto promise = server.settings.errorHandler.orDefault(*this).handleClientProtocolError(
        kj::mv(protocolError), *this);

    return promise.then([this]() { return httpOutput.flush(); })
                  .then([]() { return false; });  // loop ends after flush
  }

  kj::Promise<bool> sendError(kj::Exception&& exception) {
    closeAfterSend = true;

    // We only provide the Response object if we know we haven't already sent a response.
    auto promise = server.settings.errorHandler.orDefault(*this).handleApplicationError(
        kj::mv(exception), currentMethod.map([this](auto&&) -> Response& { return *this; }));

    return promise.then([this]() { return httpOutput.flush(); })
                  .then([]() { return false; });  // loop ends after flush
  }

  kj::Promise<bool> sendError() {
    closeAfterSend = true;

    // We can provide a Response object, since none has already been sent.
    auto promise = server.settings.errorHandler.orDefault(*this).handleNoResponse(*this);

    return promise.then([this]() { return httpOutput.flush(); })
                  .then([]() { return false; });  // loop ends after flush
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
      kj::Promise<Message> receive() override {
        return kj::cp(exception);
      }

    private:
      kj::Exception exception;
    };

    return kj::heap<BrokenWebSocket>(KJ_EXCEPTION(FAILED,
        "received bad WebSocket handshake", errorMessage));
  }
};

HttpServer::HttpServer(kj::Timer& timer, HttpHeaderTable& requestHeaderTable, HttpService& service,
                       Settings settings)
    : HttpServer(timer, requestHeaderTable, &service, settings,
                 kj::newPromiseAndFulfiller<void>()) {}

HttpServer::HttpServer(kj::Timer& timer, HttpHeaderTable& requestHeaderTable,
                       HttpServiceFactory serviceFactory, Settings settings)
    : HttpServer(timer, requestHeaderTable, kj::mv(serviceFactory), settings,
                 kj::newPromiseAndFulfiller<void>()) {}

HttpServer::HttpServer(kj::Timer& timer, HttpHeaderTable& requestHeaderTable,
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
    if (draining) {
      // Can get here if we *just* started draining.
      return kj::READY_NOW;
    }

    tasks.add(listenHttp(kj::mv(connection)));
    return listenLoop(port);
  });
}

kj::Promise<void> HttpServer::listenHttp(kj::Own<kj::AsyncIoStream> connection) {
  auto promise = listenHttpCleanDrain(*connection).ignoreResult();

  // eagerlyEvaluate() to maintain historical guarantee that this method eagerly closes the
  // connection when done.
  return promise.attach(kj::mv(connection)).eagerlyEvaluate(nullptr);
}

kj::Promise<bool> HttpServer::listenHttpCleanDrain(kj::AsyncIoStream& connection) {
  kj::Own<Connection> obj;

  KJ_SWITCH_ONEOF(service) {
    KJ_CASE_ONEOF(ptr, HttpService*) {
      obj = heap<Connection>(*this, connection, *ptr);
    }
    KJ_CASE_ONEOF(func, HttpServiceFactory) {
      auto srv = func(connection);
      obj = heap<Connection>(*this, connection, *srv);
      obj = obj.attach(kj::mv(srv));
    }
  }

  // Start reading requests and responding to them, but immediately cancel processing if the client
  // disconnects.
  auto promise = obj->loop(true)
      .exclusiveJoin(connection.whenWriteDisconnected().then([]() {return false;}));

  // Eagerly evaluate so that we drop the connection when the promise resolves, even if the caller
  // doesn't eagerly evaluate.
  return promise.attach(kj::mv(obj)).eagerlyEvaluate(nullptr);
}

void HttpServer::taskFailed(kj::Exception&& exception) {
  KJ_LOG(ERROR, "unhandled exception in HTTP server", exception);
}

kj::Promise<void> HttpServerErrorHandler::handleClientProtocolError(
    HttpHeaders::ProtocolError protocolError, kj::HttpService::Response& response) {
  // Default error handler implementation.

  HttpHeaderTable headerTable {};
  HttpHeaders headers(headerTable);
  headers.set(HttpHeaderId::CONTENT_TYPE, "text/plain");

  auto errorMessage = kj::str(protocolError.description);
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

kj::Promise<void> HttpServerErrorHandler::handleNoResponse(kj::HttpService::Response& response) {
  HttpHeaderTable headerTable {};
  HttpHeaders headers(headerTable);
  headers.set(HttpHeaderId::CONTENT_TYPE, "text/plain");

  constexpr auto errorMessage = "ERROR: The HttpService did not generate a response."_kj;
  auto body = response.send(500, "Internal Server Error", headers, errorMessage.size());

  return body->write(errorMessage.begin(), errorMessage.size()).attach(kj::mv(body));
}

} // namespace kj
