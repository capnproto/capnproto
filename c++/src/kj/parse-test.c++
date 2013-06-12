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
typedef Span<const char*> TestLocation;

TEST(Parsers, ExactElementParser) {
  StringPtr text = "foo";
  Input input(text.begin(), text.end());

  Maybe<Tuple<>> result = exactly('f')(input);
  EXPECT_TRUE(result != nullptr);
  EXPECT_FALSE(input.atEnd());

  result = exactly('o')(input);
  EXPECT_TRUE(result != nullptr);
  EXPECT_FALSE(input.atEnd());

  result = exactly('x')(input);
  EXPECT_TRUE(result == nullptr);
  EXPECT_FALSE(input.atEnd());

  auto parser = exactly('o');
  ParserRef<Input, Tuple<>> wrapped = ref<Input>(parser);
  result = wrapped(input);
  EXPECT_TRUE(result != nullptr);
  EXPECT_TRUE(input.atEnd());
}

TEST(Parsers, SequenceParser) {
  StringPtr text = "foo";

  {
    Input input(text.begin(), text.end());
    Maybe<Tuple<>> result = sequence(exactly('f'), exactly('o'), exactly('o'))(input);
    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<Tuple<>> result = sequence(exactly('f'), exactly('o'))(input);
    EXPECT_TRUE(result != nullptr);
    EXPECT_FALSE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<Tuple<>> result = sequence(exactly('x'), exactly('o'), exactly('o'))(input);
    EXPECT_TRUE(result == nullptr);
    EXPECT_FALSE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<Tuple<>> result =
        sequence(sequence(exactly('f'), exactly('o')), exactly('o'))(input);
    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<Tuple<>> result =
        sequence(sequence(exactly('f')), exactly('o'), exactly('o'))(input);
    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<int> result = sequence(transform(exactly('f'), [](TestLocation){return 123;}),
                                 exactly('o'), exactly('o'))(input);
    KJ_IF_MAYBE(i, result) {
      EXPECT_EQ(123, *i);
    } else {
      ADD_FAILURE() << "Expected 123, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }
}

TEST(Parsers, ManyParser) {
  StringPtr text = "foooob";

  auto parser = transform(
      sequence(exactly('f'), many(exactly('o'))),
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

TEST(Parsers, OptionalParser) {
  auto parser = sequence(
      transform(exactly('b'), [](TestLocation) -> uint { return 123; }),
      optional(transform(exactly('a'), [](TestLocation) -> uint { return 456; })),
      transform(exactly('r'), [](TestLocation) -> uint { return 789; }));

  {
    StringPtr text = "bar";
    Input input(text.begin(), text.end());
    Maybe<Tuple<uint, Maybe<uint>, uint>> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(123, get<0>(*value));
      KJ_IF_MAYBE(value2, get<1>(*value)) {
        EXPECT_EQ(456, *value2);
      } else {
        ADD_FAILURE() << "Expected 456, got null.";
      }
      EXPECT_EQ(789, get<2>(*value));
    } else {
      ADD_FAILURE() << "Expected result tuple, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "br";
    Input input(text.begin(), text.end());
    Maybe<Tuple<uint, Maybe<uint>, uint>> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(123, get<0>(*value));
      EXPECT_TRUE(get<1>(*value) == nullptr);
      EXPECT_EQ(789, get<2>(*value));
    } else {
      ADD_FAILURE() << "Expected result tuple, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "bzr";
    Input input(text.begin(), text.end());
    Maybe<Tuple<uint, Maybe<uint>, uint>> result = parser(input);
    EXPECT_TRUE(result == nullptr);
  }
}

TEST(Parsers, OneOfParser) {
  auto parser = oneOf(
      transform(sequence(exactly('f'), exactly('o'), exactly('o')),
                [](TestLocation) -> StringPtr { return "foo"; }),
      transform(sequence(exactly('b'), exactly('a'), exactly('r')),
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

TEST(Parsers, TransformParser) {
  StringPtr text = "foo";

  auto parser = transform(
      sequence(exactly('f'), exactly('o'), exactly('o')),
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

TEST(Parsers, References) {
  struct TransformFunc {
    int value;

    TransformFunc(int value): value(value) {}

    int operator()(TestLocation) const { return value; }
  };

  // Don't use auto for the parsers here in order to verify that the templates are properly choosing
  // whether to use references or copies.

  Transform_<Exactly_<char>, TransformFunc> parser1 =
      transform(exactly('f'), TransformFunc(12));

  auto otherParser = exactly('o');
  Transform_<Exactly_<char>&, TransformFunc> parser2 =
      transform(otherParser, TransformFunc(34));

  auto otherParser2 = exactly('b');
  Transform_<Exactly_<char>, TransformFunc> parser3 =
      transform(mv(otherParser2), TransformFunc(56));

  StringPtr text = "foob";
  auto parser = transform(
      sequence(parser1, parser2, exactly('o'), parser3),
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

TEST(Parsers, AcceptIfParser) {
  auto parser = acceptIf(
      oneOf(transform(exactly('a'), [](TestLocation) -> uint { return 123; }),
            transform(exactly('b'), [](TestLocation) -> uint { return 456; }),
            transform(exactly('c'), [](TestLocation) -> uint { return 789; })),
      [](uint i) {return i > 200;});

  {
    StringPtr text = "a";
    Input input(text.begin(), text.end());
    Maybe<uint> result = parser(input);
    EXPECT_TRUE(result == nullptr);
  }

  {
    StringPtr text = "b";
    Input input(text.begin(), text.end());
    Maybe<uint> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(456, *value);
    } else {
      ADD_FAILURE() << "Expected parse result, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "c";
    Input input(text.begin(), text.end());
    Maybe<uint> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(789, *value);
    } else {
      ADD_FAILURE() << "Expected parse result, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }
}

}  // namespace
}  // namespace parse
}  // namespace kj
