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

#ifndef KJ_ENCODING_H_
#define KJ_ENCODING_H_
// Functions for encoding/decoding bytes and text in common formats, including:
// - UTF-{8,16,32}
// - Hex
// - URI encoding
// - Base64

#if defined(__GNUC__) && !KJ_HEADER_WARNINGS
#pragma GCC system_header
#endif

#include "string.h"

namespace kj {

template <typename ResultType>
struct UtfResult: public ResultType {
  // Equivalent to ResultType (a String or wide-char array) for all intents and purposes, except
  // that the bool `hadErrors` can be inspected to see if any errors were encountered in the input,
  // resulting in instances of the replacement character (U+FFFD) in the output.

  inline UtfResult(ResultType&& result, bool hadErrors)
      : ResultType(kj::mv(result)), hadErrors(hadErrors) {}

  const bool hadErrors;
  // If true, then invalid sequences were detected in the input and were replaced with the Unicode
  // replacement character (U+FFFD) in the output. Many applications will chose to ignore this
  // boolean and continue on with the damaged data.
};

UtfResult<Array<char16_t>> encodeUtf16(ArrayPtr<const char> text, bool nulTerminate = false);
UtfResult<Array<char32_t>> encodeUtf32(ArrayPtr<const char> text, bool nulTerminate = false);
Maybe<Array<char16_t>> tryEncodeUtf16(ArrayPtr<const char> text, bool nulTerminate = false);
Maybe<Array<char32_t>> tryEncodeUtf32(ArrayPtr<const char> text, bool nulTerminate = false);
// Convert UTF-8 text (which KJ strings use) to UTF-16 or UTF-32.
//
// If `nulTerminate` is true, an extra NUL character will be added to the end of the output.
//
// The `try` versions return null if the input is invalid; the non-`try` versions return data
// containing the Unicode replacement character (U+FFFD).
//
// The returned arrays are in platform-native endianness (otherwise they wouldn't really be
// char16_t / char32_t).

UtfResult<String> decodeUtf16(ArrayPtr<const char16_t> utf16);
UtfResult<String> decodeUtf32(ArrayPtr<const char32_t> utf32);
Maybe<String> tryDecodeUtf16(ArrayPtr<const char16_t> utf16);
Maybe<String> tryDecodeUtf32(ArrayPtr<const char32_t> utf32);
// Convert UTF-16 or UTF-32 to UTF-8 (which KJ strings use).
//
// The input should NOT include a NUL terminator; any NUL characters in the input array will be
// preserved in the output.
//
// The `try` versions return null if the input is invalid; the non-`try` versions return data
// containing the Unicode replacement character (U+FFFD).
//
// The input must be in platform-native endianness. BOMs are NOT recognized by these functions.

String encodeHex(ArrayPtr<const byte> bytes);
Array<byte> decodeHex(ArrayPtr<const char> text);
// Encode/decode bytes as hex strings.

String encodeUriComponent(ArrayPtr<const byte> bytes);
String encodeUriComponent(ArrayPtr<const char> bytes);
Array<byte> decodeBinaryUriComponent(ArrayPtr<const char> text, bool nulTerminate = false);
String decodeUriComponent(ArrayPtr<const char> text);
// Encode/decode URI components using % escapes. See Javascript's encodeURIComponent().

String encodeCEscape(ArrayPtr<const byte> bytes);
String encodeCEscape(ArrayPtr<const char> bytes);
UtfResult<Array<byte>> decodeBinaryCEscape(ArrayPtr<const char> text, bool nulTerminate = false);
UtfResult<String> decodeCEscape(ArrayPtr<const char> text);

String encodeBase64(ArrayPtr<const byte> bytes, bool breakLines = false);
// Encode the given bytes as base64 text. If `breakLines` is true, line breaks will be inserted
// into the output every 72 characters (e.g. for encoding e-mail bodies).

Array<byte> decodeBase64(ArrayPtr<const char> text);
// Decode base64 text. Non-base64 characters are ignored.

// =======================================================================================
// inline implementation details

inline String encodeUriComponent(ArrayPtr<const char> text) {
  return encodeUriComponent(text.asBytes());
}
inline String decodeUriComponent(ArrayPtr<const char> text) {
  return String(decodeBinaryUriComponent(text, true).releaseAsChars());
}

inline String encodeCEscape(ArrayPtr<const char> text) {
  return encodeCEscape(text.asBytes());
}
inline UtfResult<String> decodeCEscape(ArrayPtr<const char> text) {
  auto result = decodeBinaryCEscape(text, true);
  return { String(result.releaseAsChars()), result.hadErrors };
}

// If you pass a string literal to a function taking ArrayPtr<const char>, it'll include the NUL
// termintator, which is surprising. Let's add overloads that avoid that. In practice this probably
// only even matters for encoding-test.c++.

template <size_t s>
inline UtfResult<Array<char16_t>> encodeUtf16(const char (&text)[s], bool nulTerminate = false) {
  return encodeUtf16(arrayPtr(text, s - 1), nulTerminate);
}
template <size_t s>
inline UtfResult<Array<char32_t>> encodeUtf32(const char (&text)[s], bool nulTerminate = false) {
  return encodeUtf32(arrayPtr(text, s - 1), nulTerminate);
}
template <size_t s>
inline Maybe<Array<char16_t>> tryEncodeUtf16(const char (&text)[s], bool nulTerminate = false) {
  return tryEncodeUtf16(arrayPtr(text, s - 1), nulTerminate);
}
template <size_t s>
inline Maybe<Array<char32_t>> tryEncodeUtf32(const char (&text)[s], bool nulTerminate = false) {
  return tryEncodeUtf32(arrayPtr(text, s - 1), nulTerminate);
}
template <size_t s>
inline UtfResult<String> decodeUtf16(const char16_t (&utf16)[s]) {
  return decodeUtf16(arrayPtr(utf16, s - 1));
}
template <size_t s>
inline UtfResult<String> decodeUtf32(const char32_t (&utf32)[s]) {
  return decodeUtf32(arrayPtr(utf32, s - 1));
}
template <size_t s>
inline Maybe<String> tryDecodeUtf16(const char16_t (&utf16)[s]) {
  return tryDecodeUtf16(arrayPtr(utf16, s - 1));
}
template <size_t s>
inline Maybe<String> tryDecodeUtf32(const char32_t (&utf32)[s]) {
  return tryDecodeUtf32(arrayPtr(utf32, s - 1));
}
template <size_t s>
inline Array<byte> decodeHex(const char (&text)[s]) {
  return decodeHex(arrayPtr(text, s - 1));
}
template <size_t s>
inline String encodeUriComponent(const char (&text)[s]) {
  return encodeUriComponent(arrayPtr(text, s - 1));
}
template <size_t s>
inline Array<byte> decodeBinaryUriComponent(const char (&text)[s]) {
  return decodeBinaryUriComponent(arrayPtr(text, s - 1));
}
template <size_t s>
inline String decodeUriComponent(const char (&text)[s]) {
  return String(decodeBinaryUriComponent(arrayPtr(text, s - 1), true).releaseAsChars());
}
template <size_t s>
inline String encodeCEscape(const char (&text)[s]) {
  return encodeCEscape(arrayPtr(text, s - 1));
}
template <size_t s>
inline UtfResult<Array<byte>> decodeBinaryCEscape(const char (&text)[s]) {
  return decodeBinaryCEscape(arrayPtr(text, s - 1));
}
template <size_t s>
inline UtfResult<String> decodeCEscape(const char (&text)[s]) {
  auto result = decodeBinaryCEscape(arrayPtr(text, s - 1), true);
  return { String(result.releaseAsChars()), result.hadErrors };
}
template <size_t s>
Array<byte> decodeBase64(const char (&text)[s]) {
  return decodeBase64(arrayPtr(text, s - 1));
}

} // namespace kj

#endif // KJ_ENCODING_H_
