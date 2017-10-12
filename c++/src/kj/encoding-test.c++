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

#include "encoding.h"
#include <kj/test.h>
#include <stdint.h>

namespace kj {
namespace {

CappedArray<char, sizeof(char    ) * 2 + 1> hex(byte     i) { return kj::hex((uint8_t )i); }
CappedArray<char, sizeof(char    ) * 2 + 1> hex(char     i) { return kj::hex((uint8_t )i); }
CappedArray<char, sizeof(char16_t) * 2 + 1> hex(char16_t i) { return kj::hex((uint16_t)i); }
CappedArray<char, sizeof(char32_t) * 2 + 1> hex(char32_t i) { return kj::hex((uint32_t)i); }
// Hexify chars correctly.
//
// TODO(cleanup): Should this go into string.h with the other definitions of hex()?

template <typename T, typename U>
void expectResImpl(EncodingResult<T> result,
                   ArrayPtr<const U> expected,
                   bool errors = false) {
  if (errors) {
    KJ_EXPECT(result.hadErrors);
  } else {
    KJ_EXPECT(!result.hadErrors);
  }

  KJ_EXPECT(result.size() == expected.size(), result.size(), expected.size());
  for (auto i: kj::zeroTo(kj::min(result.size(), expected.size()))) {
    KJ_EXPECT(result[i] == expected[i], i, hex(result[i]), hex(expected[i]));
  }
}

template <typename T, typename U, size_t s>
void expectRes(EncodingResult<T> result,
               const U (&expected)[s],
               bool errors = false) {
  expectResImpl(kj::mv(result), arrayPtr(expected, s - 1), errors);
}

template <typename T, size_t s>
void expectRes(EncodingResult<T> result,
               byte (&expected)[s],
               bool errors = false) {
  expectResImpl(kj::mv(result), arrayPtr<const byte>(expected, s), errors);
}

KJ_TEST("encode UTF-8 to UTF-16") {
  expectRes(encodeUtf16(u8"foo"), u"foo");
  expectRes(encodeUtf16(u8"Здравствуйте"), u"Здравствуйте");
  expectRes(encodeUtf16(u8"中国网络"), u"中国网络");
  expectRes(encodeUtf16(u8"😺☁☄🐵"), u"😺☁☄🐵");
}

KJ_TEST("invalid UTF-8 to UTF-16") {
  // Disembodied continuation byte.
  expectRes(encodeUtf16("\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("f\xbfo"), u"f\ufffdo", true);
  expectRes(encodeUtf16("f\xbf\x80\xb0o"), u"f\ufffdo", true);

  // Missing continuation bytes.
  expectRes(encodeUtf16("\xc2x"), u"\ufffdx", true);
  expectRes(encodeUtf16("\xe0x"), u"\ufffdx", true);
  expectRes(encodeUtf16("\xe0\xa0x"), u"\ufffdx", true);
  expectRes(encodeUtf16("\xf0x"), u"\ufffdx", true);
  expectRes(encodeUtf16("\xf0\x90x"), u"\ufffdx", true);
  expectRes(encodeUtf16("\xf0\x90\x80x"), u"\ufffdx", true);

  // Overlong sequences.
  expectRes(encodeUtf16("\xc0\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xc1\xbf"), u"\ufffd", true);
  expectRes(encodeUtf16("\xc2\x80"), u"\u0080", false);
  expectRes(encodeUtf16("\xdf\xbf"), u"\u07ff", false);

  expectRes(encodeUtf16("\xe0\x80\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xe0\x9f\xbf"), u"\ufffd", true);
  expectRes(encodeUtf16("\xe0\xa0\x80"), u"\u0800", false);
  expectRes(encodeUtf16("\xef\xbf\xbe"), u"\ufffe", false);

  // Due to a classic off-by-one error, GCC 4.x rather hilariously encodes '\uffff' as the
  // "surrogate pair" 0xd7ff, 0xdfff: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=41698
  if (kj::size(u"\uffff") == 2) {
    expectRes(encodeUtf16("\xef\xbf\xbf"), u"\uffff", false);
  }

  expectRes(encodeUtf16("\xf0\x80\x80\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xf0\x8f\xbf\xbf"), u"\ufffd", true);
  expectRes(encodeUtf16("\xf0\x90\x80\x80"), u"\U00010000", false);
  expectRes(encodeUtf16("\xf4\x8f\xbf\xbf"), u"\U0010ffff", false);

  // Out of Unicode range.
  expectRes(encodeUtf16("\xf5\x80\x80\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xf8\xbf\x80\x80\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xfc\xbf\x80\x80\x80\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xfe\xbf\x80\x80\x80\x80\x80"), u"\ufffd", true);
  expectRes(encodeUtf16("\xff\xbf\x80\x80\x80\x80\x80\x80"), u"\ufffd", true);
}

KJ_TEST("encode UTF-8 to UTF-32") {
  expectRes(encodeUtf32(u8"foo"), U"foo");
  expectRes(encodeUtf32(u8"Здравствуйте"), U"Здравствуйте");
  expectRes(encodeUtf32(u8"中国网络"), U"中国网络");
  expectRes(encodeUtf32(u8"😺☁☄🐵"), U"😺☁☄🐵");
}

KJ_TEST("invalid UTF-8 to UTF-32") {
  // Disembodied continuation byte.
  expectRes(encodeUtf32("\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("f\xbfo"), U"f\ufffdo", true);
  expectRes(encodeUtf32("f\xbf\x80\xb0o"), U"f\ufffdo", true);

  // Missing continuation bytes.
  expectRes(encodeUtf32("\xc2x"), U"\ufffdx", true);
  expectRes(encodeUtf32("\xe0x"), U"\ufffdx", true);
  expectRes(encodeUtf32("\xe0\xa0x"), U"\ufffdx", true);
  expectRes(encodeUtf32("\xf0x"), U"\ufffdx", true);
  expectRes(encodeUtf32("\xf0\x90x"), U"\ufffdx", true);
  expectRes(encodeUtf32("\xf0\x90\x80x"), U"\ufffdx", true);

  // Overlong sequences.
  expectRes(encodeUtf32("\xc0\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xc1\xbf"), U"\ufffd", true);
  expectRes(encodeUtf32("\xc2\x80"), U"\u0080", false);
  expectRes(encodeUtf32("\xdf\xbf"), U"\u07ff", false);

  expectRes(encodeUtf32("\xe0\x80\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xe0\x9f\xbf"), U"\ufffd", true);
  expectRes(encodeUtf32("\xe0\xa0\x80"), U"\u0800", false);
  expectRes(encodeUtf32("\xef\xbf\xbf"), U"\uffff", false);

  expectRes(encodeUtf32("\xf0\x80\x80\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xf0\x8f\xbf\xbf"), U"\ufffd", true);
  expectRes(encodeUtf32("\xf0\x90\x80\x80"), U"\U00010000", false);
  expectRes(encodeUtf32("\xf4\x8f\xbf\xbf"), U"\U0010ffff", false);

  // Out of Unicode range.
  expectRes(encodeUtf32("\xf5\x80\x80\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xf8\xbf\x80\x80\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xfc\xbf\x80\x80\x80\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xfe\xbf\x80\x80\x80\x80\x80"), U"\ufffd", true);
  expectRes(encodeUtf32("\xff\xbf\x80\x80\x80\x80\x80\x80"), U"\ufffd", true);
}

KJ_TEST("decode UTF-16 to UTF-8") {
  expectRes(decodeUtf16(u"foo"), u8"foo");
  expectRes(decodeUtf16(u"Здравствуйте"), u8"Здравствуйте");
  expectRes(decodeUtf16(u"中国网络"), u8"中国网络");
  expectRes(decodeUtf16(u"😺☁☄🐵"), u8"😺☁☄🐵");
}

KJ_TEST("invalid UTF-16 to UTF-8") {
  // Surrogates in wrong order.
  expectRes(decodeUtf16(u"\xd7ff\xdc00\xdfff\xe000"), u8"\ud7ff\ufffd\ufffd\ue000", true);

  // Missing second surrogate.
  expectRes(decodeUtf16(u"f\xd800"), u8"f\ufffd", true);
  expectRes(decodeUtf16(u"f\xd800x"), u8"f\ufffdx", true);
  expectRes(decodeUtf16(u"f\xd800\xd800x"), u8"f\ufffd\ufffdx", true);
}

KJ_TEST("decode UTF-32 to UTF-8") {
  expectRes(decodeUtf32(U"foo"), u8"foo");
  expectRes(decodeUtf32(U"Здравствуйте"), u8"Здравствуйте");
  expectRes(decodeUtf32(U"中国网络"), u8"中国网络");
  expectRes(decodeUtf32(U"😺☁☄🐵"), u8"😺☁☄🐵");
}

KJ_TEST("invalid UTF-32 to UTF-8") {
  // Surrogates rejected.
  expectRes(decodeUtf32(U"\xd7ff\xdc00\xdfff\xe000"), u8"\ud7ff\ufffd\ufffd\ue000", true);

  // Even if it would be a valid surrogate pair in UTF-16.
  expectRes(decodeUtf32(U"\xd7ff\xd800\xdfff\xe000"), u8"\ud7ff\ufffd\ufffd\ue000", true);
}

KJ_TEST("EncodingResult as a Maybe") {
  KJ_IF_MAYBE(result, encodeUtf16("\x80")) {
    KJ_FAIL_EXPECT("expected failure");
  }

  KJ_IF_MAYBE(result, encodeUtf16("foo")) {
    // good
  } else {
    KJ_FAIL_EXPECT("expected success");
  }

  KJ_EXPECT(KJ_ASSERT_NONNULL(decodeUtf16(u"foo")) == "foo");
}

// =======================================================================================

KJ_TEST("hex encoding/decoding") {
  byte bytes[] = {0x12, 0x34, 0xab, 0xf2};

  KJ_EXPECT(encodeHex(bytes) == "1234abf2");

  expectRes(decodeHex("1234abf2"), bytes);

  expectRes(decodeHex("1234abf21"), bytes, true);

  bytes[2] = 0xa0;
  expectRes(decodeHex("1234axf2"), bytes, true);

  bytes[2] = 0x0b;
  expectRes(decodeHex("1234xbf2"), bytes, true);
}

KJ_TEST("URI encoding/decoding") {
  KJ_EXPECT(encodeUriComponent("foo") == "foo");
  KJ_EXPECT(encodeUriComponent("foo bar") == "foo%20bar");
  KJ_EXPECT(encodeUriComponent("\xab\xba") == "%AB%BA");
  KJ_EXPECT(encodeUriComponent(StringPtr("foo\0bar", 7)) == "foo%00bar");

  expectRes(decodeUriComponent("foo%20bar"), "foo bar");
  expectRes(decodeUriComponent("%ab%BA"), "\xab\xba");

  expectRes(decodeUriComponent("foo%1xxx"), "foo\1xxx", true);
  expectRes(decodeUriComponent("foo%1"), "foo\1", true);
  expectRes(decodeUriComponent("foo%xxx"), "fooxxx", true);
  expectRes(decodeUriComponent("foo%"), "foo", true);

  byte bytes[] = {12, 34, 56};
  KJ_EXPECT(decodeBinaryUriComponent(encodeUriComponent(bytes)).asPtr() == bytes);
}

KJ_TEST("C escape encoding/decoding") {
  KJ_EXPECT(encodeCEscape("fooo\a\b\f\n\r\t\v\'\"\\bar") ==
      "fooo\\a\\b\\f\\n\\r\\t\\v\\\'\\\"\\\\bar");
  KJ_EXPECT(encodeCEscape("foo\x01\x7fxxx") ==
      "foo\\001\\177xxx");

  expectRes(decodeCEscape("fooo\\a\\b\\f\\n\\r\\t\\v\\\'\\\"\\\\bar"),
      "fooo\a\b\f\n\r\t\v\'\"\\bar");
  expectRes(decodeCEscape("foo\\x01\\x7fxxx"), "foo\x01\x7fxxx");
  expectRes(decodeCEscape("foo\\001\\177234"), "foo\001\177234");
  expectRes(decodeCEscape("foo\\x1"), "foo\x1");
  expectRes(decodeCEscape("foo\\1"), "foo\1");

  expectRes(decodeCEscape("foo\\u1234bar"), u8"foo\u1234bar");
  expectRes(decodeCEscape("foo\\U00045678bar"), u8"foo\U00045678bar");

  // Error cases.
  expectRes(decodeCEscape("foo\\"), "foo", true);
  expectRes(decodeCEscape("foo\\x123x"), u8"foo\x23x", true);
  expectRes(decodeCEscape("foo\\u12"), u8"foo\u0012", true);
  expectRes(decodeCEscape("foo\\u12xxx"), u8"foo\u0012xxx", true);
  expectRes(decodeCEscape("foo\\U12"), u8"foo\u0012", true);
  expectRes(decodeCEscape("foo\\U12xxxxxxxx"), u8"foo\u0012xxxxxxxx", true);
}

KJ_TEST("base64 encoding/decoding") {
  {
    auto encoded = encodeBase64(StringPtr("foo").asBytes(), false);
    KJ_EXPECT(encoded == "Zm9v", encoded, encoded.size());
    KJ_EXPECT(heapString(decodeBase64(encoded.asArray()).asChars()) == "foo");
  }

  {
    auto encoded = encodeBase64(StringPtr("corge").asBytes(), false);
    KJ_EXPECT(encoded == "Y29yZ2U=", encoded);
    KJ_EXPECT(heapString(decodeBase64(encoded.asArray()).asChars()) == "corge");
  }

  KJ_EXPECT(heapString(decodeBase64("Y29yZ2U").asChars()) == "corge");
  KJ_EXPECT(heapString(decodeBase64("Y\n29y Z@2U=\n").asChars()) == "corge");

  {
    auto encoded = encodeBase64(StringPtr("corge").asBytes(), true);
    KJ_EXPECT(encoded == "Y29yZ2U=\n", encoded);
  }

  StringPtr fullLine = "012345678901234567890123456789012345678901234567890123";
  {
    auto encoded = encodeBase64(fullLine.asBytes(), false);
    KJ_EXPECT(
        encoded == "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIz",
        encoded);
  }
  {
    auto encoded = encodeBase64(fullLine.asBytes(), true);
    KJ_EXPECT(
        encoded == "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIz\n",
        encoded);
  }

  String multiLine = str(fullLine, "456");
  {
    auto encoded = encodeBase64(multiLine.asBytes(), false);
    KJ_EXPECT(
        encoded == "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2",
        encoded);
  }
  {
    auto encoded = encodeBase64(multiLine.asBytes(), true);
    KJ_EXPECT(
        encoded == "MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0NTY3ODkwMTIz\n"
                   "NDU2\n",
        encoded);
  }
}

}  // namespace
}  // namespace kj
