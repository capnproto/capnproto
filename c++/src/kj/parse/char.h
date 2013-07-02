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

namespace kj {
namespace parse {

// =======================================================================================

template <typename ReturnType>
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

  constexpr CharGroup_ invert() const {
    return CharGroup_(~bits[0], ~bits[1], ~bits[2], ~bits[3]);
  }

  template <typename Input>
  Maybe<ReturnType> operator()(Input& input) const;

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

template <>
template <typename Input>
Maybe<char> CharGroup_<char>::operator()(Input& input) const {
  unsigned char c = input.current();
  if ((bits[c / 64] & (1ll << (c % 64))) != 0) {
    input.next();
    return c;
  } else {
    return nullptr;
  }
}

template <>
template <typename Input>
Maybe<Tuple<>> CharGroup_<Tuple<>>::operator()(Input& input) const {
  unsigned char c = input.current();
  if ((bits[c / 64] & (1ll << (c % 64))) != 0) {
    input.next();
    return tuple();
  } else {
    return nullptr;
  }
}

constexpr CharGroup_<char> charRange(char first, char last) {
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

  return CharGroup_<char>().orRange(first, last);
}

constexpr CharGroup_<char> anyOfChars(const char* chars) {
  // Returns a parser that accepts any of the characters in the given string (which should usually
  // be a literal).  The returned parser is of the same type as returned by `charRange()` -- see
  // that function for more info.

  return CharGroup_<char>().orAny(chars);
}

constexpr CharGroup_<Tuple<>> discardCharRange(char first, char last) {
  // Like `charRange()` except that the parser returns an empty tuple.

  return CharGroup_<Tuple<>>().orRange(first, last);
}

constexpr CharGroup_<Tuple<>> discardAnyOfChars(const char* chars) {
  // Like `anyChar()` except that the parser returns an empty tuple.

  return CharGroup_<Tuple<>>().orAny(chars);
}

template <char c>
constexpr ExactlyConst_<char, c> exactChar() {
  // Returns a parser that matches exactly the character given by the template argument (returning
  // no result).
  return ExactlyConst_<char, c>();
}

}  // namespace parse
}  // namespace kj

#endif  // KJ_PARSE_CHAR_H_
