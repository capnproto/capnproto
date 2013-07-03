// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef KJ_PARSE_CHAR_H_
#define KJ_PARSE_CHAR_H_

#include "common.h"
#include "../string.h"
#include <inttypes.h>

namespace kj {
namespace parse {

// =======================================================================================

class CharGroup_ {
public:
  constexpr CharGroup_(): bits{0, 0, 0, 0} {}

  constexpr CharGroup_ orRange(unsigned char first, unsigned char last) const {
    return CharGroup_(bits[0] | (oneBits(last +   1) & ~oneBits(first      )),
                      bits[1] | (oneBits(last -  63) & ~oneBits(first -  64)),
                      bits[2] | (oneBits(last - 127) & ~oneBits(first - 128)),
                      bits[3] | (oneBits(last - 191) & ~oneBits(first - 192)));
  }

  constexpr CharGroup_ orAny(const char* chars) const {
    return *chars == 0 ? *this : orChar(*chars).orAny(chars + 1);
  }

  constexpr CharGroup_ orChar(unsigned char c) const {
    return CharGroup_(bits[0] | bit(c),
                      bits[1] | bit(c - 64),
                      bits[2] | bit(c - 128),
                      bits[3] | bit(c - 256));
  }

  constexpr CharGroup_ orGroup(CharGroup_ other) const {
    return CharGroup_(bits[0] | other.bits[0],
                      bits[1] | other.bits[1],
                      bits[2] | other.bits[2],
                      bits[3] | other.bits[3]);
  }

  constexpr CharGroup_ invert() const {
    return CharGroup_(~bits[0], ~bits[1], ~bits[2], ~bits[3]);
  }

  template <typename Input>
  Maybe<char> operator()(Input& input) const {
    if (input.atEnd()) return nullptr;
    unsigned char c = input.current();
    if ((bits[c / 64] & (1ll << (c % 64))) != 0) {
      input.next();
      return c;
    } else {
      return nullptr;
    }
  }

private:
  typedef unsigned long long Bits64;

  constexpr CharGroup_(Bits64 a, Bits64 b, Bits64 c, Bits64 d): bits{a, b, c, d} {}
  Bits64 bits[4];

  static constexpr Bits64 oneBits(int count) {
    return count <= 0 ? 0ll : count >= 64 ? -1ll : ((1ll << count) - 1);
  }
  static constexpr Bits64 bit(int index) {
    return index < 0 ? 0 : index >= 64 ? 0 : (1ll << index);
  }
};

constexpr CharGroup_ charRange(char first, char last) {
  // Create a parser which accepts any character in the range from `first` to `last`, inclusive.
  // For example: `charRange('a', 'z')` matches all lower-case letters.  The parser's result is the
  // character matched.
  //
  // The returned object has methods which can be used to match more characters.  The following
  // produces a parser which accepts any letter as well as '_', '+', '-', and '.'.
  //
  //     charRange('a', 'z').orRange('A', 'Z').orChar('_').orAny("+-.")
  //
  // You can also use `.invert()` to match the opposite set of characters.

  return CharGroup_().orRange(first, last);
}

constexpr CharGroup_ anyOfChars(const char* chars) {
  // Returns a parser that accepts any of the characters in the given string (which should usually
  // be a literal).  The returned parser is of the same type as returned by `charRange()` -- see
  // that function for more info.

  return CharGroup_().orAny(chars);
}

template <char c>
constexpr ExactlyConst_<char, c> exactChar() {
  // Returns a parser that matches exactly the character given by the template argument (returning
  // no result).
  return ExactlyConst_<char, c>();
}

// =======================================================================================

namespace _ {  // private

struct ArrayToString {
  inline String operator()(const Array<char>& arr) const {
    return heapString(arr);
  }
};

}  // namespace _ (private)

template <typename SubParser>
constexpr auto charsToString(SubParser&& subParser)
    -> decltype(transform(kj::fwd<SubParser>(subParser), _::ArrayToString())) {
  // Wraps a parser that returns Array<char> such that it returns String instead.
  return parse::transform(kj::fwd<SubParser>(subParser), _::ArrayToString());
}

// =======================================================================================
// Basic character classes.

constexpr auto alpha = charRange('a', 'z').orRange('A', 'Z');
constexpr auto digit = charRange('0', '9');
constexpr auto alphaNumeric = alpha.orGroup(digit);
constexpr auto nameStart = alpha.orChar('_');
constexpr auto nameChar = alphaNumeric.orChar('_');
constexpr auto hexDigit = charRange('0', '9').orRange('a', 'f').orRange('A', 'F');
constexpr auto octDigit = charRange('0', '7');
constexpr auto whitespace = many(anyOfChars(" \f\n\r\t\v"));

constexpr auto discardWhitespace = discard(many(discard(anyOfChars(" \f\n\r\t\v"))));
// Like discard(whitespace) but avoids some memory allocation.

// =======================================================================================
// Identifiers

namespace _ { // private

struct IdentifierToString {
  inline String operator()(char first, const Array<char>& rest) const {
    String result = heapString(rest.size() + 1);
    result[0] = first;
    memcpy(result.begin() + 1, rest.begin(), rest.size());
    return result;
  }
};

}  // namespace _ (private)

constexpr auto identifier = transform(sequence(nameStart, many(nameChar)), _::IdentifierToString());
// Parses an identifier (e.g. a C variable name).

// =======================================================================================
// Integers

namespace _ {  // private

inline char parseDigit(char c) {
  if (c < 'A') return c - '0';
  if (c < 'a') return c - 'A' + 10;
  return c - 'a' + 10;
}

template <uint base>
struct ParseInteger {
  inline uint64_t operator()(const Array<char>& digits) const {
    return operator()('0', digits);
  }
  uint64_t operator()(char first, const Array<char>& digits) const {
    uint64_t result = parseDigit(first);
    for (char digit: digits) {
      result = result * base + parseDigit(digit);
    }
    return result;
  }
};


}  // namespace _ (private)

constexpr auto integer = sequence(
    oneOf(
      transform(sequence(exactChar<'0'>(), exactChar<'x'>(), many(hexDigit)), _::ParseInteger<16>()),
      transform(sequence(exactChar<'0'>(), many(octDigit)), _::ParseInteger<8>()),
      transform(sequence(charRange('1', '9'), many(digit)), _::ParseInteger<10>())),
    notLookingAt(alpha.orAny("_.")));

// =======================================================================================
// Numbers (i.e. floats)

namespace _ {  // private

struct ParseFloat {
  double operator()(const Array<char>& digits,
                    const Maybe<Array<char>>& fraction,
                    const Maybe<Tuple<Maybe<char>, Array<char>>>& exponent) const;
};

}  // namespace _ (private)

constexpr auto number = transform(
    sequence(
        many(digit),
        optional(sequence(exactChar<'.'>(), many(digit))),
        optional(sequence(discard(anyOfChars("eE")), optional(anyOfChars("+-")), many(digit))),
        notLookingAt(alpha.orAny("_."))),
    _::ParseFloat());

// =======================================================================================
// Quoted strings

namespace _ {  // private

struct InterpretEscape {
  char operator()(char c) const {
    switch (c) {
      case 'a': return '\a';
      case 'b': return '\b';
      case 'f': return '\f';
      case 'n': return '\n';
      case 'r': return '\r';
      case 't': return '\t';
      case 'v': return '\v';
      default: return c;
    }
  }
};

struct ParseHexEscape {
  inline char operator()(char first, char second) const {
    return (parseDigit(first) << 4) | second;
  }
};

struct ParseOctEscape {
  inline char operator()(char first, Maybe<char> second, Maybe<char> third) const {
    char result = first - '0';
    KJ_IF_MAYBE(digit1, second) {
      result = (result << 3) | (*digit1 - '0');
      KJ_IF_MAYBE(digit2, third) {
        result = (result << 3) | (*digit2 - '0');
      }
    }
    return result;
  }
};

}  // namespace _ (private)

constexpr auto escapeSequence =
    sequence(exactChar<'\\'>(), oneOf(
        transform(anyOfChars("abfnrtv'\"\\\?"), _::InterpretEscape()),
        transform(sequence(exactChar<'x'>(), hexDigit, hexDigit), _::ParseHexEscape()),
        transform(sequence(octDigit, optional(octDigit), optional(octDigit)),
                  _::ParseOctEscape())));
// A parser that parses a C-string-style escape sequence (starting with a backslash).  Returns
// a char.

constexpr auto doubleQuotedString = charsToString(sequence(
    exactChar<'\"'>(),
    many(oneOf(anyOfChars("\\\n\"").invert(), escapeSequence)),
    exactChar<'\"'>()));
// Parses a C-style double-quoted string.

constexpr auto singleQuotedString = charsToString(sequence(
    exactChar<'\''>(),
    many(oneOf(anyOfChars("\\\n\'").invert(), escapeSequence)),
    exactChar<'\''>()));
// Parses a C-style single-quoted string.

}  // namespace parse
}  // namespace kj

#endif  // KJ_PARSE_CHAR_H_
