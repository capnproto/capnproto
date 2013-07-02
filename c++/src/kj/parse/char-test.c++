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

#include "char.h"
#include "../string.h"
#include <gtest/gtest.h>

namespace kj {
namespace parse {
namespace {

typedef IteratorInput<char, const char*> Input;
typedef Span<const char*> TestLocation;

TEST(CharParsers, CharRange) {
  constexpr auto parser = charRange('a', 'z');

  {
    StringPtr text = "a";
    Input input(text.begin(), text.end());
    Maybe<char> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ('a', *value);
    } else {
      ADD_FAILURE() << "Expected parse result, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "n";
    Input input(text.begin(), text.end());
    Maybe<char> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ('n', *value);
    } else {
      ADD_FAILURE() << "Expected parse result, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "z";
    Input input(text.begin(), text.end());
    Maybe<char> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ('z', *value);
    } else {
      ADD_FAILURE() << "Expected parse result, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "`";
    Input input(text.begin(), text.end());
    Maybe<char> result = parser(input);
    EXPECT_TRUE(result == nullptr);
    EXPECT_FALSE(input.atEnd());
  }

  {
    StringPtr text = "{";
    Input input(text.begin(), text.end());
    Maybe<char> result = parser(input);
    EXPECT_TRUE(result == nullptr);
    EXPECT_FALSE(input.atEnd());
  }

  {
    StringPtr text = "A";
    Input input(text.begin(), text.end());
    Maybe<char> result = parser(input);
    EXPECT_TRUE(result == nullptr);
    EXPECT_FALSE(input.atEnd());
  }
}

TEST(CharParsers, AnyOfChars) {
  constexpr auto parser = anyOfChars("axn2B");

  {
    StringPtr text = "a";
    Input input(text.begin(), text.end());
    Maybe<char> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ('a', *value);
    } else {
      ADD_FAILURE() << "Expected parse result, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "n";
    Input input(text.begin(), text.end());
    Maybe<char> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ('n', *value);
    } else {
      ADD_FAILURE() << "Expected parse result, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "B";
    Input input(text.begin(), text.end());
    Maybe<char> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ('B', *value);
    } else {
      ADD_FAILURE() << "Expected parse result, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "b";
    Input input(text.begin(), text.end());
    Maybe<char> result = parser(input);
    EXPECT_TRUE(result == nullptr);
    EXPECT_FALSE(input.atEnd());
  }

  {
    StringPtr text = "j";
    Input input(text.begin(), text.end());
    Maybe<char> result = parser(input);
    EXPECT_TRUE(result == nullptr);
    EXPECT_FALSE(input.atEnd());
  }

  {
    StringPtr text = "A";
    Input input(text.begin(), text.end());
    Maybe<char> result = parser(input);
    EXPECT_TRUE(result == nullptr);
    EXPECT_FALSE(input.atEnd());
  }
}

TEST(CharParsers, CharGroupCombo) {
  constexpr auto parser =
      many(charRange('0', '9').orRange('a', 'z').orRange('A', 'Z').orAny("-_"));

  {
    StringPtr text = "foo1-bar2_baz3@qux";
    Input input(text.begin(), text.end());
    Maybe<Array<char>> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ("foo1-bar2_baz3", str(*value));
    } else {
      ADD_FAILURE() << "Expected parse result, got null.";
    }
    EXPECT_FALSE(input.atEnd());
  }
}

TEST(CharParsers, DiscardCharRange) {
  constexpr auto parser = many(discardCharRange('a', 'z'));

  {
    StringPtr text = "foo-bar";
    Input input(text.begin(), text.end());
    Maybe<int> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(3, *value);
    } else {
      ADD_FAILURE() << "Expected 3, got null.";
    }
    EXPECT_FALSE(input.atEnd());
  }
}

TEST(CharParsers, DiscardAnyOfChars) {
  constexpr auto parser = many(discardAnyOfChars("abcd"));

  {
    StringPtr text = "cadbfoo";
    Input input(text.begin(), text.end());
    Maybe<int> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(4, *value);
    } else {
      ADD_FAILURE() << "Expected 4, got null.";
    }
    EXPECT_FALSE(input.atEnd());
  }
}

TEST(CommonParsers, ExactChar) {
  constexpr auto parser = exactChar<'a'>();

  {
    StringPtr text = "a";
    Input input(text.begin(), text.end());
    EXPECT_TRUE(parser(input) != nullptr);
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "b";
    Input input(text.begin(), text.end());
    EXPECT_TRUE(parser(input) == nullptr);
    EXPECT_FALSE(input.atEnd());
  }
}

}  // namespace
}  // namespace parse
}  // namespace kj
