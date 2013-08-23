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

TEST(CharParsers, ExactChar) {
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

TEST(CharParsers, ExactString) {
  constexpr auto parser = exactString("foo");

  {
    StringPtr text = "foobar";
    Input input(text.begin(), text.end());
    EXPECT_TRUE(parser(input) != nullptr);
    ASSERT_FALSE(input.atEnd());
    EXPECT_EQ('b', input.current());
  }

  {
    StringPtr text = "bar";
    Input input(text.begin(), text.end());
    EXPECT_TRUE(parser(input) == nullptr);
    EXPECT_FALSE(input.atEnd());
    EXPECT_EQ('b', input.current());
  }
}

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

TEST(CharParsers, Identifier) {
  constexpr auto parser = identifier;

  {
    StringPtr text = "helloWorld123 ";
    Input input(text.begin(), text.end());
    Maybe<String> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ("helloWorld123", *value);
    } else {
      ADD_FAILURE() << "Expected string, got null.";
    }
    EXPECT_FALSE(input.atEnd());
  }
}

TEST(CharParsers, Integer) {
  constexpr auto parser = integer;

  {
    StringPtr text = "12349";
    Input input(text.begin(), text.end());
    Maybe<uint64_t> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(12349u, *value);
    } else {
      ADD_FAILURE() << "Expected integer, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "0x1aF0";
    Input input(text.begin(), text.end());
    Maybe<uint64_t> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(0x1aF0u, *value);
    } else {
      ADD_FAILURE() << "Expected integer, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "064270";
    Input input(text.begin(), text.end());
    Maybe<uint64_t> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(064270u, *value);
    } else {
      ADD_FAILURE() << "Expected integer, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }
}

TEST(CharParsers, Number) {
  constexpr auto parser = number;

  {
    StringPtr text = "12345";
    Input input(text.begin(), text.end());
    Maybe<double> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(12345, *value);
    } else {
      ADD_FAILURE() << "Expected number, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "123.25";
    Input input(text.begin(), text.end());
    Maybe<double> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(123.25, *value);
    } else {
      ADD_FAILURE() << "Expected number, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "123e10";
    Input input(text.begin(), text.end());
    Maybe<double> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(123e10, *value);
    } else {
      ADD_FAILURE() << "Expected number, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "123.25E+10";
    Input input(text.begin(), text.end());
    Maybe<double> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(123.25E+10, *value);
    } else {
      ADD_FAILURE() << "Expected number, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "25e-2";
    Input input(text.begin(), text.end());
    Maybe<double> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(25e-2, *value);
    } else {
      ADD_FAILURE() << "Expected number, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }
}

TEST(CharParsers, DoubleQuotedString) {
  constexpr auto parser = doubleQuotedString;

  {
    StringPtr text = "\"hello\"";
    Input input(text.begin(), text.end());
    Maybe<String> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ("hello", *value);
    } else {
      ADD_FAILURE() << "Expected \"hello\", got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "\"test\\a\\b\\f\\n\\r\\t\\v\\\'\\\"\\\?\\x01\\x20\\2\\34\\156\"";
    Input input(text.begin(), text.end());
    Maybe<String> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ("test\a\b\f\n\r\t\v\'\"\?\x01\x20\2\34\156", *value);
    } else {
      ADD_FAILURE() << "Expected string, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "\"foo'bar\"";
    Input input(text.begin(), text.end());
    Maybe<String> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ("foo'bar", *value);
    } else {
      ADD_FAILURE() << "Expected string, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }
}

TEST(CharParsers, SingleQuotedString) {
  constexpr auto parser = singleQuotedString;

  {
    StringPtr text = "\'hello\'";
    Input input(text.begin(), text.end());
    Maybe<String> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ("hello", *value);
    } else {
      ADD_FAILURE() << "Expected \"hello\", got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "\'test\\a\\b\\f\\n\\r\\t\\v\\\'\\\"\\\?\x01\2\34\156\'";
    Input input(text.begin(), text.end());
    Maybe<String> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ("test\a\b\f\n\r\t\v\'\"\?\x01\2\34\156", *value);
    } else {
      ADD_FAILURE() << "Expected string, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "\'foo\"bar\'";
    Input input(text.begin(), text.end());
    Maybe<String> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ("foo\"bar", *value);
    } else {
      ADD_FAILURE() << "Expected string, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }
}

}  // namespace
}  // namespace parse
}  // namespace kj
