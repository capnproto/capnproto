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

#include "parse.h"
#include "string.h"
#include <gtest/gtest.h>

namespace kj {
namespace parse {
namespace {

typedef IteratorInput<char, const char*> Input;
ExactElementParser<Input> exactChar(char c) {
  return exactElement<Input>(mv(c));
}

typedef Span<const char*> TestLocation;

TEST(Parsers, ExactElementParser) {
  StringPtr text = "foo";
  Input input(text.begin(), text.end());

  Maybe<Tuple<>> result = exactChar('f')(input);
  EXPECT_TRUE(result != nullptr);
  EXPECT_FALSE(input.atEnd());

  result = exactChar('o')(input);
  EXPECT_TRUE(result != nullptr);
  EXPECT_FALSE(input.atEnd());

  result = exactChar('x')(input);
  EXPECT_TRUE(result == nullptr);
  EXPECT_FALSE(input.atEnd());

  Parser<Input, Tuple<>> wrapped = exactChar('o');
  result = wrapped(input);
  EXPECT_TRUE(result != nullptr);
  EXPECT_TRUE(input.atEnd());
}

TEST(Parsers, SequenceParser) {
  StringPtr text = "foo";

  {
    Input input(text.begin(), text.end());
    Maybe<Tuple<>> result = sequence(exactChar('f'), exactChar('o'), exactChar('o'))(input);
    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<Tuple<>> result = sequence(exactChar('f'), exactChar('o'))(input);
    EXPECT_TRUE(result != nullptr);
    EXPECT_FALSE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<Tuple<>> result = sequence(exactChar('x'), exactChar('o'), exactChar('o'))(input);
    EXPECT_TRUE(result == nullptr);
    EXPECT_FALSE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<Tuple<>> result =
        sequence(sequence(exactChar('f'), exactChar('o')), exactChar('o'))(input);
    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<Tuple<>> result =
        sequence(sequence(exactChar('f')), exactChar('o'), exactChar('o'))(input);
    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<int> result = sequence(transform(exactChar('f'), [](TestLocation){return 123;}),
                                 exactChar('o'), exactChar('o'))(input);
    KJ_IF_MAYBE(i, result) {
      EXPECT_EQ(123, *i);
    } else {
      ADD_FAILURE() << "Expected 123, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }
}

TEST(Parsers, TransformParser) {
  StringPtr text = "foo";

  auto parser = transform(
      sequence(exactChar('f'), exactChar('o'), exactChar('o')),
      [](TestLocation location) -> int {
        EXPECT_EQ("foo", StringPtr(location.begin(), location.end()));
        return 123;
      });

  {
    Input input(text.begin(), text.end());
    Maybe<int> result = parser(input);
    KJ_IF_MAYBE(i, result) {
      EXPECT_EQ(123, *i);
    } else {
      ADD_FAILURE() << "Expected 123, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }
}

TEST(Parsers, TransformParser_MaybeRef) {
  struct Transform {
    int value;

    Transform(int value): value(value) {}

    int operator()(TestLocation) const { return value; }
  };

  // Don't use auto for the TransformParsers here because we're trying to make sure that MaybeRef
  // is working correctly.  When transform() is given an lvalue, it should wrap the type in
  // ParserRef.

  TransformParser<ExactElementParser<Input>, Transform> parser1 =
      transform(exactChar('f'), Transform(12));

  auto otherParser = exactChar('o');
  TransformParser<ParserRef<ExactElementParser<Input>>, Transform> parser2 =
      transform(otherParser, Transform(34));

  auto otherParser2 = exactChar('b');
  TransformParser<ExactElementParser<Input>, Transform> parser3 =
      transform(mv(otherParser2), Transform(56));

  StringPtr text = "foob";
  auto parser = transform(
      sequence(parser1, parser2, exactChar('o'), parser3),
      [](TestLocation, int i, int j, int k) { return i + j + k; });

  {
    Input input(text.begin(), text.end());
    Maybe<int> result = parser(input);
    KJ_IF_MAYBE(i, result) {
      EXPECT_EQ(12 + 34 + 56, *i);
    } else {
      ADD_FAILURE() << "Expected 12 + 34 + 56, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }
}

TEST(Parsers, RepeatedParser) {
  StringPtr text = "foooob";

  auto parser = transform(
      sequence(exactChar('f'), repeated(exactChar('o'))),
      [](TestLocation, ArrayPtr<Tuple<>> values) -> int { return values.size(); });

  {
    Input input(text.begin(), text.begin() + 3);
    Maybe<int> result = parser(input);
    KJ_IF_MAYBE(i, result) {
      EXPECT_EQ(2, *i);
    } else {
      ADD_FAILURE() << "Expected 2, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    Input input(text.begin(), text.begin() + 5);
    Maybe<int> result = parser(input);
    KJ_IF_MAYBE(i, result) {
      EXPECT_EQ(4, *i);
    } else {
      ADD_FAILURE() << "Expected 4, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<int> result = parser(input);
    KJ_IF_MAYBE(i, result) {
      EXPECT_EQ(4, *i);
    } else {
      ADD_FAILURE() << "Expected 4, got null.";
    }
    EXPECT_FALSE(input.atEnd());
  }
}

TEST(Parsers, OneOfParser) {
  auto parser = oneOf(
      transform(sequence(exactChar('f'), exactChar('o'), exactChar('o')),
                [](TestLocation) -> StringPtr { return "foo"; }),
      transform(sequence(exactChar('b'), exactChar('a'), exactChar('r')),
                [](TestLocation) -> StringPtr { return "bar"; }));

  {
    StringPtr text = "foo";
    Input input(text.begin(), text.end());
    Maybe<StringPtr> result = parser(input);
    KJ_IF_MAYBE(s, result) {
      EXPECT_EQ("foo", *s);
    } else {
      ADD_FAILURE() << "Expected 'foo', got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "bar";
    Input input(text.begin(), text.end());
    Maybe<StringPtr> result = parser(input);
    KJ_IF_MAYBE(s, result) {
      EXPECT_EQ("bar", *s);
    } else {
      ADD_FAILURE() << "Expected 'bar', got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }
}

}  // namespace
}  // namespace parse
}  // namespace kj
