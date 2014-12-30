// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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

#include "char.h"
#include "../string.h"
#include <kj/compat/gtest.h>

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
