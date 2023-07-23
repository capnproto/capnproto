// Copyright (c) 2017 Cloudflare, Inc.; Sandstorm Development Group, Inc.; and contributors
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
#include "vector.h"
#include "debug.h"

namespace kj {

namespace {

#define GOTO_ERROR_IF(cond) if (KJ_UNLIKELY(cond)) goto error

inline void addChar32(Vector<char16_t>& vec, char32_t u) {
  // Encode as surrogate pair.
  u -= 0x10000;
  vec.add(0xd800 | (u >> 10));
  vec.add(0xdc00 | (u & 0x03ff));
}

inline void addChar32(Vector<char32_t>& vec, char32_t u) {
  vec.add(u);
}

template <typename T>
EncodingResult<Array<T>> encodeUtf(ArrayPtr<const char> text, bool nulTerminate) {
  Vector<T> result(text.size() + nulTerminate);
  bool hadErrors = false;

  size_t i = 0;
  while (i < text.size()) {
    byte c = text[i++];
    if (c < 0x80) {
      // 0xxxxxxx -- ASCII
      result.add(c);
      continue;
    } else if (KJ_UNLIKELY(c < 0xc0)) {
      // 10xxxxxx -- malformed continuation byte
      goto error;
    } else if (c < 0xe0) {
      // 110xxxxx -- 2-byte
      byte c2;
      GOTO_ERROR_IF(i == text.size() || ((c2 = text[i]) & 0xc0) != 0x80); ++i;
      char16_t u = (static_cast<char16_t>(c  & 0x1f) <<  6)
                 | (static_cast<char16_t>(c2 & 0x3f)      );

      // Disallow overlong sequence.
      GOTO_ERROR_IF(u < 0x80);

      result.add(u);
      continue;
    } else if (c < 0xf0) {
      // 1110xxxx -- 3-byte
      byte c2, c3;
      GOTO_ERROR_IF(i == text.size() || ((c2 = text[i]) & 0xc0) != 0x80); ++i;
      GOTO_ERROR_IF(i == text.size() || ((c3 = text[i]) & 0xc0) != 0x80); ++i;
      char16_t u = (static_cast<char16_t>(c  & 0x0f) << 12)
                 | (static_cast<char16_t>(c2 & 0x3f) <<  6)
                 | (static_cast<char16_t>(c3 & 0x3f)      );

      // Disallow overlong sequence.
      GOTO_ERROR_IF(u < 0x0800);

      // Flag surrogate pair code points as errors, but allow them through.
      if (KJ_UNLIKELY((u & 0xf800) == 0xd800)) {
        if (result.size() > 0 &&
            (u & 0xfc00) == 0xdc00 &&
            (result.back() & 0xfc00) == 0xd800) {
          // Whoops, the *previous* character was also an invalid surrogate, and if we add this
          // one too, they'll form a valid surrogate pair. If we allowed this, then it would mean
          // invalid UTF-8 round-tripped to UTF-16 and back could actually change meaning entirely.
          // OTOH, the reason we allow dangling surrogates is to allow invalid UTF-16 to round-trip
          // to UTF-8 without loss, but if the original UTF-16 had a valid surrogate pair, it would
          // have been encoded as a valid single UTF-8 codepoint, not as separate UTF-8 codepoints
          // for each surrogate.
          goto error;
        }

        hadErrors = true;
      }

      result.add(u);
      continue;
    } else if (c < 0xf8) {
      // 11110xxx -- 4-byte
      byte c2, c3, c4;
      GOTO_ERROR_IF(i == text.size() || ((c2 = text[i]) & 0xc0) != 0x80); ++i;
      GOTO_ERROR_IF(i == text.size() || ((c3 = text[i]) & 0xc0) != 0x80); ++i;
      GOTO_ERROR_IF(i == text.size() || ((c4 = text[i]) & 0xc0) != 0x80); ++i;
      char32_t u = (static_cast<char32_t>(c  & 0x07) << 18)
                 | (static_cast<char32_t>(c2 & 0x3f) << 12)
                 | (static_cast<char32_t>(c3 & 0x3f) <<  6)
                 | (static_cast<char32_t>(c4 & 0x3f)      );

      // Disallow overlong sequence.
      GOTO_ERROR_IF(u < 0x10000);

      // Unicode ends at U+10FFFF
      GOTO_ERROR_IF(u >= 0x110000);

      addChar32(result, u);
      continue;
    } else {
      // 5-byte and 6-byte sequences are not legal as they'd result in codepoints outside the
      // range of Unicode.
      goto error;
    }

  error:
    result.add(0xfffd);
    hadErrors = true;
    // Ignore all continuation bytes.
    while (i < text.size() && (text[i] & 0xc0) == 0x80) {
      ++i;
    }
  }

  if (nulTerminate) result.add(0);

  return { result.releaseAsArray(), hadErrors };
}

}  // namespace

EncodingResult<Array<char16_t>> encodeUtf16(ArrayPtr<const char> text, bool nulTerminate) {
  return encodeUtf<char16_t>(text, nulTerminate);
}

EncodingResult<Array<char32_t>> encodeUtf32(ArrayPtr<const char> text, bool nulTerminate) {
  return encodeUtf<char32_t>(text, nulTerminate);
}

EncodingResult<String> decodeUtf16(ArrayPtr<const char16_t> utf16) {
  Vector<char> result(utf16.size() + 1);
  bool hadErrors = false;

  size_t i = 0;
  while (i < utf16.size()) {
    char16_t u = utf16[i++];

    if (u < 0x80) {
      result.add(u);
      continue;
    } else if (u < 0x0800) {
      result.addAll<std::initializer_list<char>>({
        static_cast<char>(((u >>  6)       ) | 0xc0),
        static_cast<char>(((u      ) & 0x3f) | 0x80)
      });
      continue;
    } else if ((u & 0xf800) == 0xd800) {
      // surrogate pair
      char16_t u2;
      if (KJ_UNLIKELY(i == utf16.size()                         // missing second half
                   || (u & 0x0400) != 0                         // first half in wrong range
                   || ((u2 = utf16[i]) & 0xfc00) != 0xdc00)) {  // second half in wrong range
        hadErrors = true;
        goto threeByte;
      }
      ++i;

      char32_t u32 = (((u & 0x03ff) << 10) | (u2 & 0x03ff)) + 0x10000;
      result.addAll<std::initializer_list<char>>({
        static_cast<char>(((u32 >> 18)       ) | 0xf0),
        static_cast<char>(((u32 >> 12) & 0x3f) | 0x80),
        static_cast<char>(((u32 >>  6) & 0x3f) | 0x80),
        static_cast<char>(((u32      ) & 0x3f) | 0x80)
      });
      continue;
    } else {
    threeByte:
      result.addAll<std::initializer_list<char>>({
        static_cast<char>(((u >> 12)       ) | 0xe0),
        static_cast<char>(((u >>  6) & 0x3f) | 0x80),
        static_cast<char>(((u      ) & 0x3f) | 0x80)
      });
      continue;
    }
  }

  result.add(0);
  return { String(result.releaseAsArray()), hadErrors };
}

EncodingResult<String> decodeUtf32(ArrayPtr<const char32_t> utf16) {
  Vector<char> result(utf16.size() + 1);
  bool hadErrors = false;

  size_t i = 0;
  while (i < utf16.size()) {
    char32_t u = utf16[i++];

    if (u < 0x80) {
      result.add(u);
      continue;
    } else if (u < 0x0800) {
      result.addAll<std::initializer_list<char>>({
        static_cast<char>(((u >>  6)       ) | 0xc0),
        static_cast<char>(((u      ) & 0x3f) | 0x80)
      });
      continue;
    } else if (u < 0x10000) {
      if (KJ_UNLIKELY((u & 0xfffff800) == 0xd800)) {
        // no surrogates allowed in utf-32
        hadErrors = true;
      }
      result.addAll<std::initializer_list<char>>({
        static_cast<char>(((u >> 12)       ) | 0xe0),
        static_cast<char>(((u >>  6) & 0x3f) | 0x80),
        static_cast<char>(((u      ) & 0x3f) | 0x80)
      });
      continue;
    } else {
      GOTO_ERROR_IF(u >= 0x110000);  // outside Unicode range
      result.addAll<std::initializer_list<char>>({
        static_cast<char>(((u >> 18)       ) | 0xf0),
        static_cast<char>(((u >> 12) & 0x3f) | 0x80),
        static_cast<char>(((u >>  6) & 0x3f) | 0x80),
        static_cast<char>(((u      ) & 0x3f) | 0x80)
      });
      continue;
    }

  error:
    result.addAll(StringPtr(u8"\ufffd"));
    hadErrors = true;
  }

  result.add(0);
  return { String(result.releaseAsArray()), hadErrors };
}

namespace {

#if __GNUC__ >= 8 && !__clang__
// GCC 8's new class-memaccess warning rightly dislikes the following hacks, but we're really sure
// we want to allow them so disable the warning.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

template <typename To, typename From>
Array<To> coerceTo(Array<From>&& array) {
  static_assert(sizeof(To) == sizeof(From), "incompatible coercion");
  Array<wchar_t> result;
  memcpy(&result, &array, sizeof(array));
  memset(&array, 0, sizeof(array));
  return result;
}

template <typename To, typename From>
ArrayPtr<To> coerceTo(ArrayPtr<From> array) {
  static_assert(sizeof(To) == sizeof(From), "incompatible coercion");
  return arrayPtr(reinterpret_cast<To*>(array.begin()), array.size());
}

template <typename To, typename From>
EncodingResult<Array<To>> coerceTo(EncodingResult<Array<From>>&& result) {
  return { coerceTo<To>(Array<From>(kj::mv(result))), result.hadErrors };
}

#if __GNUC__ >= 8 && !__clang__
#pragma GCC diagnostic pop
#endif

template <size_t s>
struct WideConverter;

template <>
struct WideConverter<sizeof(char)> {
  typedef char Type;

  static EncodingResult<Array<char>> encode(ArrayPtr<const char> text, bool nulTerminate) {
    auto result = heapArray<char>(text.size() + nulTerminate);
    memcpy(result.begin(), text.begin(), text.size());
    if (nulTerminate) result.back() = 0;
    return { kj::mv(result), false };
  }

  static EncodingResult<kj::String> decode(ArrayPtr<const char> text) {
    return { kj::heapString(text), false };
  }
};

template <>
struct WideConverter<sizeof(char16_t)> {
  typedef char16_t Type;

  static inline EncodingResult<Array<char16_t>> encode(
      ArrayPtr<const char> text, bool nulTerminate) {
    return encodeUtf16(text, nulTerminate);
  }

  static inline EncodingResult<kj::String> decode(ArrayPtr<const char16_t> text) {
    return decodeUtf16(text);
  }
};

template <>
struct WideConverter<sizeof(char32_t)> {
  typedef char32_t Type;

  static inline EncodingResult<Array<char32_t>> encode(
      ArrayPtr<const char> text, bool nulTerminate) {
    return encodeUtf32(text, nulTerminate);
  }

  static inline EncodingResult<kj::String> decode(ArrayPtr<const char32_t> text) {
    return decodeUtf32(text);
  }
};

}  // namespace

EncodingResult<Array<wchar_t>> encodeWideString(ArrayPtr<const char> text, bool nulTerminate) {
  return coerceTo<wchar_t>(WideConverter<sizeof(wchar_t)>::encode(text, nulTerminate));
}
EncodingResult<String> decodeWideString(ArrayPtr<const wchar_t> wide) {
  using Converter = WideConverter<sizeof(wchar_t)>;
  return Converter::decode(coerceTo<const Converter::Type>(wide));
}

// =======================================================================================

namespace {

const char HEX_DIGITS[] = "0123456789abcdef";
// Maps integer in the range [0,16) to a hex digit.

const char HEX_DIGITS_URI[] = "0123456789ABCDEF";
// RFC 3986 section 2.1 says "For consistency, URI producers and normalizers should use uppercase
// hexadecimal digits for all percent-encodings.

static Maybe<uint> tryFromHexDigit(char c) {
  if ('0' <= c && c <= '9') {
    return c - '0';
  } else if ('a' <= c && c <= 'f') {
    return c - ('a' - 10);
  } else if ('A' <= c && c <= 'F') {
    return c - ('A' - 10);
  } else {
    return nullptr;
  }
}

static Maybe<uint> tryFromOctDigit(char c) {
  if ('0' <= c && c <= '7') {
    return c - '0';
  } else {
    return nullptr;
  }
}

}  // namespace

String encodeHex(ArrayPtr<const byte> input) {
  return strArray(KJ_MAP(b, input) {
    return heapArray<char>({HEX_DIGITS[b/16], HEX_DIGITS[b%16]});
  }, "");
}

EncodingResult<Array<byte>> decodeHex(ArrayPtr<const char> text) {
  auto result = heapArray<byte>(text.size() / 2);
  bool hadErrors = text.size() % 2;

  for (auto i: kj::indices(result)) {
    byte b = 0;
    KJ_IF_MAYBE(d1, tryFromHexDigit(text[i*2])) {
      b = *d1 << 4;
    } else {
      hadErrors = true;
    }
    KJ_IF_MAYBE(d2, tryFromHexDigit(text[i*2+1])) {
      b |= *d2;
    } else {
      hadErrors = true;
    }
    result[i] = b;
  }

  return { kj::mv(result), hadErrors };
}

String encodeUriComponent(ArrayPtr<const byte> bytes) {
  Vector<char> result(bytes.size() + 1);
  for (byte b: bytes) {
    if (('A' <= b && b <= 'Z') ||
        ('a' <= b && b <= 'z') ||
        ('0' <= b && b <= '9') ||
        b == '-' || b == '_' || b == '.' || b == '!' || b == '~' || b == '*' || b == '\'' ||
        b == '(' || b == ')') {
      result.add(b);
    } else {
      result.add('%');
      result.add(HEX_DIGITS_URI[b/16]);
      result.add(HEX_DIGITS_URI[b%16]);
    }
  }
  result.add('\0');
  return String(result.releaseAsArray());
}

String encodeUriFragment(ArrayPtr<const byte> bytes) {
  Vector<char> result(bytes.size() + 1);
  for (byte b: bytes) {
    if (('?' <= b && b <= '_') || // covers A-Z
        ('a' <= b && b <= '~') || // covers a-z
        ('&' <= b && b <= ';') || // covers 0-9
        b == '!' || b == '=' || b == '#' || b == '$') {
      result.add(b);
    } else {
      result.add('%');
      result.add(HEX_DIGITS_URI[b/16]);
      result.add(HEX_DIGITS_URI[b%16]);
    }
  }
  result.add('\0');
  return String(result.releaseAsArray());
}

String encodeUriPath(ArrayPtr<const byte> bytes) {
  Vector<char> result(bytes.size() + 1);
  for (byte b: bytes) {
    if (('@' <= b && b <= '[') || // covers A-Z
        ('a' <= b && b <= 'z') ||
        ('0' <= b && b <= ';') || // covers 0-9
        ('&' <= b && b <= '.') ||
        b == '_' || b == '!' || b == '=' || b == ']' ||
        b == '^' || b == '|' || b == '~' || b == '$') {
      result.add(b);
    } else {
      result.add('%');
      result.add(HEX_DIGITS_URI[b/16]);
      result.add(HEX_DIGITS_URI[b%16]);
    }
  }
  result.add('\0');
  return String(result.releaseAsArray());
}

String encodeUriUserInfo(ArrayPtr<const byte> bytes) {
  Vector<char> result(bytes.size() + 1);
  for (byte b: bytes) {
    if (('A' <= b && b <= 'Z') ||
        ('a' <= b && b <= 'z') ||
        ('0' <= b && b <= '9') ||
        ('&' <= b && b <= '.') ||
        b == '_' || b == '!' || b == '~' || b == '$') {
      result.add(b);
    } else {
      result.add('%');
      result.add(HEX_DIGITS_URI[b/16]);
      result.add(HEX_DIGITS_URI[b%16]);
    }
  }
  result.add('\0');
  return String(result.releaseAsArray());
}

String encodeWwwForm(ArrayPtr<const byte> bytes) {
  Vector<char> result(bytes.size() + 1);
  for (byte b: bytes) {
    if (('A' <= b && b <= 'Z') ||
        ('a' <= b && b <= 'z') ||
        ('0' <= b && b <= '9') ||
        b == '-' || b == '_' || b == '.' || b == '*') {
      result.add(b);
    } else if (b == ' ') {
      result.add('+');
    } else {
      result.add('%');
      result.add(HEX_DIGITS_URI[b/16]);
      result.add(HEX_DIGITS_URI[b%16]);
    }
  }
  result.add('\0');
  return String(result.releaseAsArray());
}

EncodingResult<Array<byte>> decodeBinaryUriComponent(
    ArrayPtr<const char> text, DecodeUriOptions options) {
  Vector<byte> result(text.size() + options.nulTerminate);
  bool hadErrors = false;

  const char* ptr = text.begin();
  const char* end = text.end();
  while (ptr < end) {
    if (*ptr == '%') {
      ++ptr;

      if (ptr == end) {
        hadErrors = true;
      } else KJ_IF_MAYBE(d1, tryFromHexDigit(*ptr)) {
        byte b = *d1;
        ++ptr;
        if (ptr == end) {
          hadErrors = true;
        } else KJ_IF_MAYBE(d2, tryFromHexDigit(*ptr)) {
          b = (b << 4) | *d2;
          ++ptr;
        } else {
          hadErrors = true;
        }
        result.add(b);
      } else {
        hadErrors = true;
      }
    } else if (options.plusToSpace && *ptr == '+') {
      ++ptr;
      result.add(' ');
    } else {
      result.add(*ptr++);
    }
  }

  if (options.nulTerminate) result.add(0);
  return { result.releaseAsArray(), hadErrors };
}

// =======================================================================================

namespace _ { // private

String encodeCEscapeImpl(ArrayPtr<const byte> bytes, bool isBinary) {
  Vector<char> escaped(bytes.size());

  for (byte b: bytes) {
    switch (b) {
      case '\a': escaped.addAll(StringPtr("\\a")); break;
      case '\b': escaped.addAll(StringPtr("\\b")); break;
      case '\f': escaped.addAll(StringPtr("\\f")); break;
      case '\n': escaped.addAll(StringPtr("\\n")); break;
      case '\r': escaped.addAll(StringPtr("\\r")); break;
      case '\t': escaped.addAll(StringPtr("\\t")); break;
      case '\v': escaped.addAll(StringPtr("\\v")); break;
      case '\'': escaped.addAll(StringPtr("\\\'")); break;
      case '\"': escaped.addAll(StringPtr("\\\"")); break;
      case '\\': escaped.addAll(StringPtr("\\\\")); break;
      default:
        if (b < 0x20 || b == 0x7f || (isBinary && b > 0x7f)) {
          // Use octal escape, not hex, because hex escapes technically have no length limit and
          // so can create ambiguity with subsequent characters.
          escaped.add('\\');
          escaped.add(HEX_DIGITS[b / 64]);
          escaped.add(HEX_DIGITS[(b / 8) % 8]);
          escaped.add(HEX_DIGITS[b % 8]);
        } else {
          escaped.add(b);
        }
        break;
    }
  }

  escaped.add(0);
  return String(escaped.releaseAsArray());
}

} // namespace

EncodingResult<Array<byte>> decodeBinaryCEscape(ArrayPtr<const char> text, bool nulTerminate) {
  Vector<byte> result(text.size() + nulTerminate);
  bool hadErrors = false;

  size_t i = 0;
  while (i < text.size()) {
    char c = text[i++];
    if (c == '\\') {
      if (i == text.size()) {
        hadErrors = true;
        continue;
      }
      char c2 = text[i++];
      switch (c2) {
        case 'a' : result.add('\a'); break;
        case 'b' : result.add('\b'); break;
        case 'f' : result.add('\f'); break;
        case 'n' : result.add('\n'); break;
        case 'r' : result.add('\r'); break;
        case 't' : result.add('\t'); break;
        case 'v' : result.add('\v'); break;
        case '\'': result.add('\''); break;
        case '\"': result.add('\"'); break;
        case '\\': result.add('\\'); break;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7': {
          uint value = c2 - '0';
          for (uint j = 0; j < 2 && i < text.size(); j++) {
            KJ_IF_MAYBE(d, tryFromOctDigit(text[i])) {
              ++i;
              value = (value << 3) | *d;
            } else {
              break;
            }
          }
          if (value >= 0x100) hadErrors = true;
          result.add(value);
          break;
        }

        case 'x': {
          uint value = 0;
          while (i < text.size()) {
            KJ_IF_MAYBE(d, tryFromHexDigit(text[i])) {
              ++i;
              value = (value << 4) | *d;
            } else {
              break;
            }
          }
          if (value >= 0x100) hadErrors = true;
          result.add(value);
          break;
        }

        case 'u': {
          char16_t value = 0;
          for (uint j = 0; j < 4; j++) {
            if (i == text.size()) {
              hadErrors = true;
              break;
            } else KJ_IF_MAYBE(d, tryFromHexDigit(text[i])) {
              ++i;
              value = (value << 4) | *d;
            } else {
              hadErrors = true;
              break;
            }
          }
          auto utf = decodeUtf16(arrayPtr(&value, 1));
          if (utf.hadErrors) hadErrors = true;
          result.addAll(utf.asBytes());
          break;
        }

        case 'U': {
          char32_t value = 0;
          for (uint j = 0; j < 8; j++) {
            if (i == text.size()) {
              hadErrors = true;
              break;
            } else KJ_IF_MAYBE(d, tryFromHexDigit(text[i])) {
              ++i;
              value = (value << 4) | *d;
            } else {
              hadErrors = true;
              break;
            }
          }
          auto utf = decodeUtf32(arrayPtr(&value, 1));
          if (utf.hadErrors) hadErrors = true;
          result.addAll(utf.asBytes());
          break;
        }

        default:
          result.add(c2);
      }
    } else {
      result.add(c);
    }
  }

  if (nulTerminate) result.add(0);
  return { result.releaseAsArray(), hadErrors };
}

// =======================================================================================
// This code is derived from libb64 which has been placed in the public domain.
// For details, see http://sourceforge.net/projects/libb64

// -------------------------------------------------------------------
// Encoder

namespace {

typedef enum {
  step_A, step_B, step_C
} base64_encodestep;

typedef struct {
  base64_encodestep step;
  char result;
  int stepcount;
} base64_encodestate;

const int CHARS_PER_LINE = 72;

void base64_init_encodestate(base64_encodestate* state_in) {
  state_in->step = step_A;
  state_in->result = 0;
  state_in->stepcount = 0;
}

char base64_encode_value(char value_in) {
  static const char* encoding = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (value_in > 63) return '=';
  return encoding[(int)value_in];
}

int base64_encode_block(const char* plaintext_in, int length_in,
                        char* code_out, base64_encodestate* state_in, bool breakLines) {
  const char* plainchar = plaintext_in;
  const char* const plaintextend = plaintext_in + length_in;
  char* codechar = code_out;
  char result;
  char fragment;

  result = state_in->result;

  switch (state_in->step) {
    while (1) {
      KJ_FALLTHROUGH;
  case step_A:
      if (plainchar == plaintextend) {
        state_in->result = result;
        state_in->step = step_A;
        return codechar - code_out;
      }
      fragment = *plainchar++;
      result = (fragment & 0x0fc) >> 2;
      *codechar++ = base64_encode_value(result);
      result = (fragment & 0x003) << 4;
      KJ_FALLTHROUGH;
  case step_B:
      if (plainchar == plaintextend) {
        state_in->result = result;
        state_in->step = step_B;
        return codechar - code_out;
      }
      fragment = *plainchar++;
      result |= (fragment & 0x0f0) >> 4;
      *codechar++ = base64_encode_value(result);
      result = (fragment & 0x00f) << 2;
      KJ_FALLTHROUGH;
  case step_C:
      if (plainchar == plaintextend) {
        state_in->result = result;
        state_in->step = step_C;
        return codechar - code_out;
      }
      fragment = *plainchar++;
      result |= (fragment & 0x0c0) >> 6;
      *codechar++ = base64_encode_value(result);
      result  = (fragment & 0x03f) >> 0;
      *codechar++ = base64_encode_value(result);

      ++(state_in->stepcount);
      if (breakLines && state_in->stepcount == CHARS_PER_LINE/4) {
        *codechar++ = '\n';
        state_in->stepcount = 0;
      }
    }
  }
  /* control should not reach here */
  return codechar - code_out;
}

int base64_encode_blockend(char* code_out, base64_encodestate* state_in, bool breakLines) {
  char* codechar = code_out;

  switch (state_in->step) {
  case step_B:
    *codechar++ = base64_encode_value(state_in->result);
    *codechar++ = '=';
    *codechar++ = '=';
    ++state_in->stepcount;
    break;
  case step_C:
    *codechar++ = base64_encode_value(state_in->result);
    *codechar++ = '=';
    ++state_in->stepcount;
    break;
  case step_A:
    break;
  }
  if (breakLines && state_in->stepcount > 0) {
    *codechar++ = '\n';
  }

  return codechar - code_out;
}

}  // namespace

String encodeBase64(ArrayPtr<const byte> input, bool breakLines) {
  /* set up a destination buffer large enough to hold the encoded data */
  // equivalent to ceil(input.size() / 3) * 4
  auto numChars = (input.size() + 2) / 3 * 4;
  if (breakLines) {
    // Add space for newline characters.
    uint lineCount = numChars / CHARS_PER_LINE;
    if (numChars % CHARS_PER_LINE > 0) {
      // Partial line.
      ++lineCount;
    }
    numChars = numChars + lineCount;
  }
  auto output = heapString(numChars);
  /* keep track of our encoded position */
  char* c = output.begin();
  /* store the number of bytes encoded by a single call */
  int cnt = 0;
  size_t total = 0;
  /* we need an encoder state */
  base64_encodestate s;

  /*---------- START ENCODING ----------*/
  /* initialise the encoder state */
  base64_init_encodestate(&s);
  /* gather data from the input and send it to the output */
  cnt = base64_encode_block((const char *)input.begin(), input.size(), c, &s, breakLines);
  c += cnt;
  total += cnt;

  /* since we have encoded the entire input string, we know that
     there is no more input data; finalise the encoding */
  cnt = base64_encode_blockend(c, &s, breakLines);
  c += cnt;
  total += cnt;
  /*---------- STOP ENCODING  ----------*/

  KJ_ASSERT(total == output.size(), total, output.size());

  return output;
}

// -------------------------------------------------------------------
// Decoder

namespace {

typedef enum {
  step_a, step_b, step_c, step_d
} base64_decodestep;

struct base64_decodestate {
  bool hadErrors = false;
  size_t nPaddingBytesSeen = 0;
  // Output state. `nPaddingBytesSeen` is not guaranteed to be correct if `hadErrors` is true. It is
  // included in the state purely to preserve the streaming capability of the algorithm while still
  // checking for errors correctly (consider chunk 1 = "abc=", chunk 2 = "d").

  base64_decodestep step = step_a;
  char plainchar = 0;
};

int base64_decode_value(char value_in) {
  // Returns either the fragment value or: -1 on whitespace, -2 on padding, -3 on invalid input.
  //
  // Note that the original libb64 implementation used -1 for invalid input, -2 on padding -- this
  // new scheme allows for some simpler error checks in steps A and B.

  static const signed char decoding[] = {
    -3,-3,-3,-3,-3,-3,-3,-3,  -3,-1,-1,-3,-1,-1,-3,-3,
    -3,-3,-3,-3,-3,-3,-3,-3,  -3,-3,-3,-3,-3,-3,-3,-3,
    -1,-3,-3,-3,-3,-3,-3,-3,  -3,-3,-3,62,-3,-3,-3,63,
    52,53,54,55,56,57,58,59,  60,61,-3,-3,-3,-2,-3,-3,
    -3, 0, 1, 2, 3, 4, 5, 6,   7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,  23,24,25,-3,-3,-3,-3,-3,
    -3,26,27,28,29,30,31,32,  33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,  49,50,51,-3,-3,-3,-3,-3,

    -3,-3,-3,-3,-3,-3,-3,-3,  -3,-3,-3,-3,-3,-3,-3,-3,
    -3,-3,-3,-3,-3,-3,-3,-3,  -3,-3,-3,-3,-3,-3,-3,-3,
    -3,-3,-3,-3,-3,-3,-3,-3,  -3,-3,-3,-3,-3,-3,-3,-3,
    -3,-3,-3,-3,-3,-3,-3,-3,  -3,-3,-3,-3,-3,-3,-3,-3,
    -3,-3,-3,-3,-3,-3,-3,-3,  -3,-3,-3,-3,-3,-3,-3,-3,
    -3,-3,-3,-3,-3,-3,-3,-3,  -3,-3,-3,-3,-3,-3,-3,-3,
    -3,-3,-3,-3,-3,-3,-3,-3,  -3,-3,-3,-3,-3,-3,-3,-3,
    -3,-3,-3,-3,-3,-3,-3,-3,  -3,-3,-3,-3,-3,-3,-3,-3,
  };
  static_assert(sizeof(decoding) == 256, "base64 decoding table size error");
  return decoding[(unsigned char)value_in];
}

int base64_decode_block(const char* code_in, const int length_in,
                        char* plaintext_out, base64_decodestate* state_in) {
  const char* codechar = code_in;
  char* plainchar = plaintext_out;
  signed char fragment;

  if (state_in->step != step_a) {
    *plainchar = state_in->plainchar;
  }

#define ERROR_IF(predicate) state_in->hadErrors = state_in->hadErrors || (predicate)

  switch (state_in->step)
  {
    while (1)
    {
      KJ_FALLTHROUGH;
  case step_a:
      do {
        if (codechar == code_in+length_in) {
          state_in->step = step_a;
          state_in->plainchar = '\0';
          return plainchar - plaintext_out;
        }
        fragment = (signed char)base64_decode_value(*codechar++);
        // It is an error to see invalid or padding bytes in step A.
        ERROR_IF(fragment < -1);
      } while (fragment < 0);
      *plainchar    = (fragment & 0x03f) << 2;
      KJ_FALLTHROUGH;
  case step_b:
      do {
        if (codechar == code_in+length_in) {
          state_in->step = step_b;
          state_in->plainchar = *plainchar;
          // It is always an error to suspend from step B, because we don't have enough bits yet.
          // TODO(someday): This actually breaks the streaming use case, if base64_decode_block() is
          //   to be called multiple times. We'll fix it if we ever care to support streaming.
          state_in->hadErrors = true;
          return plainchar - plaintext_out;
        }
        fragment = (signed char)base64_decode_value(*codechar++);
        // It is an error to see invalid or padding bytes in step B.
        ERROR_IF(fragment < -1);
      } while (fragment < 0);
      *plainchar++ |= (fragment & 0x030) >> 4;
      *plainchar    = (fragment & 0x00f) << 4;
      KJ_FALLTHROUGH;
  case step_c:
      do {
        if (codechar == code_in+length_in) {
          state_in->step = step_c;
          state_in->plainchar = *plainchar;
          // It is an error to complete from step C if we have seen incomplete padding.
          // TODO(someday): This actually breaks the streaming use case, if base64_decode_block() is
          //   to be called multiple times. We'll fix it if we ever care to support streaming.
          ERROR_IF(state_in->nPaddingBytesSeen == 1);
          return plainchar - plaintext_out;
        }
        fragment = (signed char)base64_decode_value(*codechar++);
        // It is an error to see invalid bytes or more than two padding bytes in step C.
        ERROR_IF(fragment < -2 || (fragment == -2 && ++state_in->nPaddingBytesSeen > 2));
      } while (fragment < 0);
      // It is an error to continue from step C after having seen any padding.
      ERROR_IF(state_in->nPaddingBytesSeen > 0);
      *plainchar++ |= (fragment & 0x03c) >> 2;
      *plainchar    = (fragment & 0x003) << 6;
      KJ_FALLTHROUGH;
  case step_d:
      do {
        if (codechar == code_in+length_in) {
          state_in->step = step_d;
          state_in->plainchar = *plainchar;
          return plainchar - plaintext_out;
        }
        fragment = (signed char)base64_decode_value(*codechar++);
        // It is an error to see invalid bytes or more than one padding byte in step D.
        ERROR_IF(fragment < -2 || (fragment == -2 && ++state_in->nPaddingBytesSeen > 1));
      } while (fragment < 0);
      // It is an error to continue from step D after having seen padding bytes.
      ERROR_IF(state_in->nPaddingBytesSeen > 0);
      *plainchar++   |= (fragment & 0x03f);
    }
  }

#undef ERROR_IF

  /* control should not reach here */
  return plainchar - plaintext_out;
}

}  // namespace

EncodingResult<Array<byte>> decodeBase64(ArrayPtr<const char> input) {
  base64_decodestate state;

  auto output = heapArray<byte>((input.size() * 6 + 7) / 8);

  size_t n = base64_decode_block(input.begin(), input.size(),
      reinterpret_cast<char*>(output.begin()), &state);

  if (n < output.size()) {
    auto copy = heapArray<byte>(n);
    memcpy(copy.begin(), output.begin(), n);
    output = kj::mv(copy);
  }

  return EncodingResult<Array<byte>>(kj::mv(output), state.hadErrors);
}

String encodeBase64Url(ArrayPtr<const byte> bytes) {
  // TODO(perf): Rewrite as single pass?
  // TODO(someday): Write decoder?

  auto base64 = kj::encodeBase64(bytes);

  for (char& c: base64) {
    if (c == '+') c = '-';
    if (c == '/') c = '_';
  }

  // Remove trailing '='s.
  kj::ArrayPtr<const char> slice = base64;
  while (slice.size() > 0 && slice.back() == '=') {
    slice = slice.slice(0, slice.size() - 1);
  }

  return kj::str(slice);
}

} // namespace kj
