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

#include "common.h"
#include "../string.h"
#include <kj/compat/gtest.h>

namespace kj {
namespace parse {
namespace {

typedef IteratorInput<char, const char*> Input;

TEST(CommonParsers, AnyParser) {
  StringPtr text = "foo";
  Input input(text.begin(), text.end());
  constexpr auto parser = any;

  Maybe<char> result = parser(input);
  KJ_IF_MAYBE(c, result) {
    EXPECT_EQ('f', *c);
  } else {
    ADD_FAILURE() << "Expected 'c', got null.";
  }
  EXPECT_FALSE(input.atEnd());

  result = parser(input);
  KJ_IF_MAYBE(c, result) {
    EXPECT_EQ('o', *c);
  } else {
    ADD_FAILURE() << "Expected 'o', got null.";
  }
  EXPECT_FALSE(input.atEnd());

  result = parser(input);
  KJ_IF_MAYBE(c, result) {
    EXPECT_EQ('o', *c);
  } else {
    ADD_FAILURE() << "Expected 'o', got null.";
  }
  EXPECT_TRUE(input.atEnd());

  result = parser(input);
  EXPECT_TRUE(result == nullptr);
  EXPECT_TRUE(input.atEnd());
}

TEST(CommonParsers, ExactElementParser) {
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

TEST(CommonParsers, ExactlyConstParser) {
  StringPtr text = "foo";
  Input input(text.begin(), text.end());

  Maybe<Tuple<>> result = exactlyConst<char, 'f'>()(input);
  EXPECT_TRUE(result != nullptr);
  EXPECT_FALSE(input.atEnd());

  result = exactlyConst<char, 'o'>()(input);
  EXPECT_TRUE(result != nullptr);
  EXPECT_FALSE(input.atEnd());

  result = exactlyConst<char, 'x'>()(input);
  EXPECT_TRUE(result == nullptr);
  EXPECT_FALSE(input.atEnd());

  auto parser = exactlyConst<char, 'o'>();
  ParserRef<Input, Tuple<>> wrapped = ref<Input>(parser);
  result = wrapped(input);
  EXPECT_TRUE(result != nullptr);
  EXPECT_TRUE(input.atEnd());
}

TEST(CommonParsers, ConstResultParser) {
  auto parser = constResult(exactly('o'), 123);

  StringPtr text = "o";
  Input input(text.begin(), text.end());
  Maybe<int> result = parser(input);
  KJ_IF_MAYBE(i, result) {
    EXPECT_EQ(123, *i);
  } else {
    ADD_FAILURE() << "Expected 123, got null.";
  }
  EXPECT_TRUE(input.atEnd());
}

TEST(CommonParsers, DiscardParser) {
  auto parser = discard(any);

  StringPtr text = "o";
  Input input(text.begin(), text.end());
  Maybe<Tuple<>> result = parser(input);
  EXPECT_TRUE(result != nullptr);
  EXPECT_TRUE(input.atEnd());
}

TEST(CommonParsers, SequenceParser) {
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
    Maybe<int> result = sequence(transform(exactly('f'), [](){return 123;}),
                                 exactly('o'), exactly('o'))(input);
    KJ_IF_MAYBE(i, result) {
      EXPECT_EQ(123, *i);
    } else {
      ADD_FAILURE() << "Expected 123, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }
}

TEST(CommonParsers, ManyParserCountOnly) {
  StringPtr text = "foooob";

  auto parser = sequence(exactly('f'), many(exactly('o')));

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

TEST(CommonParsers, TimesParser) {
  StringPtr text = "foobar";

  auto parser = sequence(exactly('f'), times(any, 4));

  {
    Input input(text.begin(), text.begin() + 4);
    Maybe<Array<char>> result = parser(input);
    EXPECT_TRUE(result == nullptr);
    EXPECT_TRUE(input.atEnd());
  }

  {
    Input input(text.begin(), text.begin() + 5);
    Maybe<Array<char>> result = parser(input);
    KJ_IF_MAYBE(s, result) {
      EXPECT_EQ("ooba", heapString(*s));
    } else {
      ADD_FAILURE() << "Expected string, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<Array<char>> result = parser(input);
    KJ_IF_MAYBE(s, result) {
      EXPECT_EQ("ooba", heapString(*s));
    } else {
      ADD_FAILURE() << "Expected string, got null.";
    }
    EXPECT_FALSE(input.atEnd());
  }
}

TEST(CommonParsers, TimesParserCountOnly) {
  StringPtr text = "foooob";

  auto parser = sequence(exactly('f'), times(exactly('o'), 4));

  {
    Input input(text.begin(), text.begin() + 4);
    Maybe<Tuple<>> result = parser(input);
    EXPECT_TRUE(result == nullptr);
    EXPECT_TRUE(input.atEnd());
  }

  {
    Input input(text.begin(), text.begin() + 5);
    Maybe<Tuple<>> result = parser(input);
    EXPECT_TRUE(result != nullptr);
    EXPECT_TRUE(input.atEnd());
  }

  {
    Input input(text.begin(), text.end());
    Maybe<Tuple<>> result = parser(input);
    EXPECT_TRUE(result != nullptr);
    EXPECT_FALSE(input.atEnd());
  }

  text = "fooob";

  {
    Input input(text.begin(), text.end());
    Maybe<Tuple<>> result = parser(input);
    EXPECT_TRUE(result == nullptr);
    EXPECT_FALSE(input.atEnd());
  }
}

TEST(CommonParsers, ManyParserSubResult) {
  StringPtr text = "foooob";

  auto parser = many(any);

  {
    Input input(text.begin(), text.end());
    Maybe<Array<char>> result = parser(input);
    KJ_IF_MAYBE(chars, result) {
      EXPECT_EQ(text, heapString(*chars));
    } else {
      ADD_FAILURE() << "Expected char array, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }
}

TEST(CommonParsers, OptionalParser) {
  auto parser = sequence(
      transform(exactly('b'), []() -> uint { return 123; }),
      optional(transform(exactly('a'), []() -> uint { return 456; })),
      transform(exactly('r'), []() -> uint { return 789; }));

  {
    StringPtr text = "bar";
    Input input(text.begin(), text.end());
    Maybe<Tuple<uint, Maybe<uint>, uint>> result = parser(input);
    KJ_IF_MAYBE(value, result) {
      EXPECT_EQ(123u, get<0>(*value));
      KJ_IF_MAYBE(value2, get<1>(*value)) {
        EXPECT_EQ(456u, *value2);
      } else {
        ADD_FAILURE() << "Expected 456, got null.";
      }
      EXPECT_EQ(789u, get<2>(*value));
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
      EXPECT_EQ(123u, get<0>(*value));
      EXPECT_TRUE(get<1>(*value) == nullptr);
      EXPECT_EQ(789u, get<2>(*value));
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

TEST(CommonParsers, OneOfParser) {
  auto parser = oneOf(
      transform(sequence(exactly('f'), exactly('o'), exactly('o')),
                []() -> StringPtr { return "foo"; }),
      transform(sequence(exactly('b'), exactly('a'), exactly('r')),
                []() -> StringPtr { return "bar"; }));

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

TEST(CommonParsers, TransformParser) {
  StringPtr text = "foo";

  auto parser = transformWithLocation(
      sequence(exactly('f'), exactly('o'), exactly('o')),
      [](Span<const char*> location) -> int {
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

TEST(CommonParsers, TransformOrRejectParser) {
  auto parser = transformOrReject(many(any),
      [](Array<char> chars) -> Maybe<int> {
        if (heapString(chars) == "foo") {
          return 123;
        } else {
          return nullptr;
        }
      });

  {
    StringPtr text = "foo";
    Input input(text.begin(), text.end());
    Maybe<int> result = parser(input);
    KJ_IF_MAYBE(i, result) {
      EXPECT_EQ(123, *i);
    } else {
      ADD_FAILURE() << "Expected 123, got null.";
    }
    EXPECT_TRUE(input.atEnd());
  }

  {
    StringPtr text = "bar";
    Input input(text.begin(), text.end());
    Maybe<int> result = parser(input);
    EXPECT_TRUE(result == nullptr);
    EXPECT_TRUE(input.atEnd());
  }
}

TEST(CommonParsers, References) {
  struct TransformFunc {
    int value;

    TransformFunc(int value): value(value) {}

    int operator()() const { return value; }
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
      [](int i, int j, int k) { return i + j + k; });

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

TEST(CommonParsers, NotLookingAt) {
  auto parser = notLookingAt(exactly('a'));

  {
    StringPtr text = "a";
    Input input(text.begin(), text.end());
    EXPECT_TRUE(parser(input) == nullptr);
    EXPECT_FALSE(input.atEnd());
  }

  {
    StringPtr text = "b";
    Input input(text.begin(), text.end());
    EXPECT_TRUE(parser(input) != nullptr);
    EXPECT_FALSE(input.atEnd());
  }
}

TEST(CommonParsers, EndOfInput) {
  auto parser = endOfInput;

  {
    StringPtr text = "a";
    Input input(text.begin(), text.end());
    EXPECT_TRUE(parser(input) == nullptr);
    EXPECT_TRUE(parser(input) == nullptr);
    input.next();
    EXPECT_FALSE(parser(input) == nullptr);
  }
}

}  // namespace
}  // namespace parse
}  // namespace kj
