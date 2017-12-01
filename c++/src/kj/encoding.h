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
struct EncodingResult: public ResultType {
  // Equivalent to ResultType (a String or wide-char array) for all intents and purposes, except
  // that the bool `hadErrors` can be inspected to see if any errors were encountered in the input.
  // Each encoding/decoding function that returns this type will "work around" errors in some way,
  // so an application doesn't strictly have to check for errors. E.g. the Unicode functions
  // replace errors with U+FFFD in the output.
  //
  // Through magic, KJ_IF_MAYBE() and KJ_{REQUIRE,ASSERT}_NONNULL() work on EncodingResult<T>
  // exactly if it were a Maybe<T> that is null in case of errors.

  inline EncodingResult(ResultType&& result, bool hadErrors)
      : ResultType(kj::mv(result)), hadErrors(hadErrors) {}

  const bool hadErrors;
};

EncodingResult<Array<char16_t>> encodeUtf16(ArrayPtr<const char> text, bool nulTerminate = false);
EncodingResult<Array<char32_t>> encodeUtf32(ArrayPtr<const char> text, bool nulTerminate = false);
// Convert UTF-8 text (which KJ strings use) to UTF-16 or UTF-32.
//
// If `nulTerminate` is true, an extra NUL character will be added to the end of the output.
//
// The `try` versions return null if the input is invalid; the non-`try` versions return data
// containing the Unicode replacement character (U+FFFD).
//
// The returned arrays are in platform-native endianness (otherwise they wouldn't really be
// char16_t / char32_t).

EncodingResult<String> decodeUtf16(ArrayPtr<const char16_t> utf16);
EncodingResult<String> decodeUtf32(ArrayPtr<const char32_t> utf32);
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
EncodingResult<Array<byte>> decodeHex(ArrayPtr<const char> text);
// Encode/decode bytes as hex strings.

String encodeUriComponent(ArrayPtr<const byte> bytes);
String encodeUriComponent(ArrayPtr<const char> bytes);
EncodingResult<Array<byte>> decodeBinaryUriComponent(
    ArrayPtr<const char> text, bool nulTerminate = false);
EncodingResult<String> decodeUriComponent(ArrayPtr<const char> text);
// Encode/decode URI components using % escapes. See Javascript's encodeURIComponent().

String encodeCEscape(ArrayPtr<const byte> bytes);
String encodeCEscape(ArrayPtr<const char> bytes);
EncodingResult<Array<byte>> decodeBinaryCEscape(
    ArrayPtr<const char> text, bool nulTerminate = false);
EncodingResult<String> decodeCEscape(ArrayPtr<const char> text);

String encodeBase64(ArrayPtr<const byte> bytes, bool breakLines = false);
// Encode the given bytes as base64 text. If `breakLines` is true, line breaks will be inserted
// into the output every 72 characters (e.g. for encoding e-mail bodies).

EncodingResult<Array<byte>> decodeBase64(ArrayPtr<const char> text);
// Decode base64 text. This function reports errors required by the WHATWG HTML/Infra specs: see
// https://html.spec.whatwg.org/multipage/webappapis.html#atob for details.

// =======================================================================================
// inline implementation details

namespace _ {  // private

template <typename T>
NullableValue<T> readMaybe(EncodingResult<T>&& value) {
  if (value.hadErrors) {
    return nullptr;
  } else {
    return kj::mv(value);
  }
}

template <typename T>
T* readMaybe(EncodingResult<T>& value) {
  if (value.hadErrors) {
    return nullptr;
  } else {
    return &value;
  }
}

template <typename T>
const T* readMaybe(const EncodingResult<T>& value) {
  if (value.hadErrors) {
    return nullptr;
  } else {
    return &value;
  }
}

}  // namespace _ (private)

inline String encodeUriComponent(ArrayPtr<const char> text) {
  return encodeUriComponent(text.asBytes());
}
inline EncodingResult<String> decodeUriComponent(ArrayPtr<const char> text) {
  auto result = decodeBinaryUriComponent(text, true);
  return { String(result.releaseAsChars()), result.hadErrors };
}

inline String encodeCEscape(ArrayPtr<const char> text) {
  return encodeCEscape(text.asBytes());
}
inline EncodingResult<String> decodeCEscape(ArrayPtr<const char> text) {
  auto result = decodeBinaryCEscape(text, true);
  return { String(result.releaseAsChars()), result.hadErrors };
}

// If you pass a string literal to a function taking ArrayPtr<const char>, it'll include the NUL
// termintator, which is surprising. Let's add overloads that avoid that. In practice this probably
// only even matters for encoding-test.c++.

template <size_t s>
inline EncodingResult<Array<char16_t>> encodeUtf16(const char (&text)[s], bool nulTerminate=false) {
  return encodeUtf16(arrayPtr(text, s - 1), nulTerminate);
}
template <size_t s>
inline EncodingResult<Array<char32_t>> encodeUtf32(const char (&text)[s], bool nulTerminate=false) {
  return encodeUtf32(arrayPtr(text, s - 1), nulTerminate);
}
template <size_t s>
inline EncodingResult<String> decodeUtf16(const char16_t (&utf16)[s]) {
  return decodeUtf16(arrayPtr(utf16, s - 1));
}
template <size_t s>
inline EncodingResult<String> decodeUtf32(const char32_t (&utf32)[s]) {
  return decodeUtf32(arrayPtr(utf32, s - 1));
}
template <size_t s>
inline EncodingResult<Array<byte>> decodeHex(const char (&text)[s]) {
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
inline EncodingResult<String> decodeUriComponent(const char (&text)[s]) {
  return decodeUriComponent(arrayPtr(text, s-1));
}
template <size_t s>
inline String encodeCEscape(const char (&text)[s]) {
  return encodeCEscape(arrayPtr(text, s - 1));
}
template <size_t s>
inline EncodingResult<Array<byte>> decodeBinaryCEscape(const char (&text)[s]) {
  return decodeBinaryCEscape(arrayPtr(text, s - 1));
}
template <size_t s>
inline EncodingResult<String> decodeCEscape(const char (&text)[s]) {
  return decodeCEscape(arrayPtr(text, s-1));
}
template <size_t s>
EncodingResult<Array<byte>> decodeBase64(const char (&text)[s]) {
  return decodeBase64(arrayPtr(text, s - 1));
}

} // namespace kj

#endif // KJ_ENCODING_H_
