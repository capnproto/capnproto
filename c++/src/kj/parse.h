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

// Parser combinator framework!
//
// This file declares several functions which construct parsers, usually taking other parsers as
// input, thus making them parser combinators.
//
// A valid parser is any functor which takes a reference to an input cursor (defined below) as its
// input and returns a Maybe.  The parser returns null on parse failure, or returns the parsed
// result on success.
//
// An "input cursor" is any type which implements the same interface as IteratorInput, below.  Such
// a type acts as a pointer to the current input location.  When a parser returns successfully, it
// will have updated the input cursor to point to the position just past the end of what was parsed.
// On failure, the cursor position is unspecified.

#ifndef KJ_PARSER_H_
#define KJ_PARSER_H_

#include "common.h"
#include "memory.h"
#include "array.h"
#include "tuple.h"
#include "vector.h"

namespace kj {
namespace parse {

template <typename Element, typename Iterator>
class IteratorInput {
  // A parser input implementation based on an iterator range.

public:
  typedef Element ElementType;

  IteratorInput(Iterator begin, Iterator end)
      : parent(nullptr), pos(begin), end(end), best(begin) {}
  IteratorInput(IteratorInput& parent)
      : parent(&parent), pos(parent.pos), end(parent.end), best(parent.pos) {}
  ~IteratorInput() {
    if (parent != nullptr) {
      parent->best = kj::max(kj::max(pos, best), parent->best);
    }
  }

  void advanceParent() {
    parent->pos = pos;
  }

  bool atEnd() { return pos == end; }
  const Element& current() {
    KJ_IREQUIRE(!atEnd());
    return *pos;
  }
  const Element& consume() {
    assert(!atEnd());
    return *pos++;
  }
  void next() {
    KJ_IREQUIRE(!atEnd());
    ++pos;
  }

  Iterator getBest() { return kj::max(pos, best); }

  Iterator getPosition() { return pos; }

private:
  IteratorInput* parent;
  Iterator pos;
  Iterator end;
  Iterator best;  // furthest we got with any sub-input

  IteratorInput(IteratorInput&&) = delete;
  IteratorInput& operator=(const IteratorInput&) = delete;
  IteratorInput& operator=(IteratorInput&&) = delete;
};

template <typename T> struct OutputType_;
template <typename T> struct OutputType_<Maybe<T>> { typedef T Type; };
template <typename Parser, typename Input>
using OutputType = typename OutputType_<decltype(instance<Parser&>()(instance<Input&>()))>::Type;
// Synonym for the output type of a parser, given the parser type and the input type.

// =======================================================================================

template <typename Input, typename Output>
class ParserRef {
  // Acts as a reference to some other parser, with simplified type.  The referenced parser
  // is polymorphic by virtual call rather than templates.  For grammars of non-trivial size,
  // it is important to inject refs into the grammar here and there to prevent the parser types
  // from becoming ridiculous.  Using too many of them can hurt performance, though.

public:
  template <typename Other>
  constexpr ParserRef(Other& other)
      : parser(&other), wrapper(WrapperImplInstance<Other>::instance) {}

  KJ_ALWAYS_INLINE(Maybe<Output> operator()(Input& input) const) {
    // Always inline in the hopes that this allows branch prediction to kick in so the virtual call
    // doesn't hurt so much.
    return wrapper.parse(parser, input);
  }

private:
  struct Wrapper {
    virtual Maybe<Output> parse(const void* parser, Input& input) const = 0;
  };
  template <typename ParserImpl>
  struct WrapperImpl: public Wrapper {
    Maybe<Output> parse(const void* parser, Input& input) const override {
      return (*reinterpret_cast<const ParserImpl*>(parser))(input);
    }
  };
  template <typename ParserImpl>
  struct WrapperImplInstance {
    static constexpr WrapperImpl<ParserImpl> instance = WrapperImpl<ParserImpl>();
  };

  const void* parser;
  const Wrapper& wrapper;
};

template <typename Input, typename Output>
template <typename ParserImpl>
constexpr ParserRef<Input, Output>::WrapperImpl<ParserImpl>
ParserRef<Input, Output>::WrapperImplInstance<ParserImpl>::instance;

template <typename Input, typename ParserImpl>
constexpr ParserRef<Input, OutputType<ParserImpl, Input>> ref(ParserImpl& impl) {
  // Constructs a ParserRef.  You must specify the input type explicitly, e.g.
  // `ref<MyInput>(myParser)`.

  return ParserRef<Input, OutputType<ParserImpl, Input>>(impl);
}

// -------------------------------------------------------------------
// exactly()
// Output = Tuple<>

template <typename T>
class Exactly_ {
public:
  explicit constexpr Exactly_(T&& expected): expected(expected) {}

  template <typename Input>
  Maybe<Tuple<>> operator()(Input& input) const {
    if (input.atEnd() || input.current() != expected) {
      return nullptr;
    } else {
      input.next();
      return Tuple<>();
    }
  }

private:
  T expected;
};

template <typename T>
constexpr Exactly_<T> exactly(T&& expected) {
  // Constructs a parser which succeeds when the input is exactly the token specified.  The
  // result is always the empty tuple.

  return Exactly_<T>(kj::fwd<T>(expected));
}

// -------------------------------------------------------------------
// exactlyConst()
// Output = Tuple<>

template <typename T, T expected>
class ExactlyConst_ {
public:
  explicit constexpr ExactlyConst_() {}

  template <typename Input>
  Maybe<Tuple<>> operator()(Input& input) const {
    if (input.atEnd() || input.current() != expected) {
      return nullptr;
    } else {
      input.next();
      return Tuple<>();
    }
  }
};

template <typename T, T expected>
constexpr ExactlyConst_<T, expected> exactlyConst() {
  // Constructs a parser which succeeds when the input is exactly the token specified.  The
  // result is always the empty tuple.  This parser is templated on the token value which may cause
  // it to perform better -- or worse.  Be sure to measure.

  return ExactlyConst_<T, expected>();
}

template <char c>
constexpr ExactlyConst_<char, c> exactChar() {
  return ExactlyConst_<char, c>();
}

// -------------------------------------------------------------------
// constResult()

template <typename SubParser, typename Result>
class ConstResult_ {
public:
  explicit constexpr ConstResult_(SubParser&& subParser, Result&& result)
      : subParser(kj::fwd<SubParser>(subParser)), result(kj::fwd<Result>(result)) {}

  template <typename Input>
  Maybe<Result> operator()(Input& input) const {
    if (subParser(input) == nullptr) {
      return nullptr;
    } else {
      return result;
    }
  }

private:
  SubParser subParser;
  Result result;
};

template <typename SubParser, typename Result>
constexpr ConstResult_<SubParser, Result> constResult(SubParser&& subParser, Result&& result) {
  // Constructs a parser which returns exactly `result` if `subParser` is successful.

  return ConstResult_<SubParser, Result>(kj::fwd<SubParser>(subParser), kj::fwd<Result>(result));
}

// -------------------------------------------------------------------
// sequence()
// Output = Flattened Tuple of outputs of sub-parsers.

template <typename... SubParsers> class Sequence_;

template <typename FirstSubParser, typename... SubParsers>
class Sequence_<FirstSubParser, SubParsers...> {
public:
  template <typename T, typename... U>
  explicit constexpr Sequence_(T&& firstSubParser, U&&... rest)
      : first(kj::fwd<T>(firstSubParser)), rest(kj::fwd<U>(rest)...) {}

  template <typename Input>
  auto operator()(Input& input) const ->
      Maybe<decltype(tuple(
          instance<OutputType<FirstSubParser, Input>>(),
          instance<OutputType<SubParsers, Input>>()...))> {
    return parseNext(input);
  }

  template <typename Input, typename... InitialParams>
  auto parseNext(Input& input, InitialParams&&... initialParams) const ->
      Maybe<decltype(tuple(
          kj::fwd<InitialParams>(initialParams)...,
          instance<OutputType<FirstSubParser, Input>>(),
          instance<OutputType<SubParsers, Input>>()...))> {
    KJ_IF_MAYBE(firstResult, first(input)) {
      return rest.parseNext(input, kj::fwd<InitialParams>(initialParams)...,
                            kj::mv(*firstResult));
    } else {
      return nullptr;
    }
  }

private:
  FirstSubParser first;
  Sequence_<SubParsers...> rest;
};

template <>
class Sequence_<> {
public:
  template <typename Input>
  Maybe<Tuple<>> operator()(Input& input) const {
    return parseNext(input);
  }

  template <typename Input, typename... Params>
  auto parseNext(Input& input, Params&&... params) const ->
      Maybe<decltype(tuple(kj::fwd<Params>(params)...))> {
    return tuple(kj::fwd<Params>(params)...);
  }
};

template <typename... SubParsers>
constexpr Sequence_<SubParsers...> sequence(SubParsers&&... subParsers) {
  // Constructs a parser that executes each of the parameter parsers in sequence and returns a
  // tuple of their results.

  return Sequence_<SubParsers...>(kj::fwd<SubParsers>(subParsers)...);
}

// -------------------------------------------------------------------
// many()
// Output = Array of output of sub-parser.

template <typename SubParser, bool atLeastOne>
class Many_ {
public:
  explicit constexpr Many_(SubParser&& subParser)
      : subParser(kj::mv(subParser)) {}

  template <typename Input>
  Maybe<Array<OutputType<SubParser, Input>>> operator()(Input& input) const {
    typedef Vector<OutputType<SubParser, Input>> Results;
    Results results;

    while (!input.atEnd()) {
      Input subInput(input);

      KJ_IF_MAYBE(subResult, subParser(subInput)) {
        subInput.advanceParent();
        results.add(kj::mv(*subResult));
      } else {
        break;
      }
    }

    if (atLeastOne && results.empty()) {
      return nullptr;
    }

    return results.releaseAsArray();
  }

private:
  SubParser subParser;
};

template <typename SubParser>
constexpr Many_<SubParser, false> many(SubParser&& subParser) {
  // Constructs a parser that repeatedly executes the given parser until it fails, returning an
  // Array of the results.
  return Many_<SubParser, false>(kj::fwd<SubParser>(subParser));
}

template <typename SubParser>
constexpr Many_<SubParser, true> oneOrMore(SubParser&& subParser) {
  // Like `many()` but the parser must parse at least one item to be successful.
  return Many_<SubParser, true>(kj::fwd<SubParser>(subParser));
}

// -------------------------------------------------------------------
// optional()
// Output = Maybe<output of sub-parser>

template <typename SubParser>
class Optional_ {
public:
  explicit constexpr Optional_(SubParser&& subParser)
      : subParser(kj::mv(subParser)) {}

  template <typename Input>
  Maybe<Maybe<OutputType<SubParser, Input>>> operator()(Input& input) const {
    typedef Maybe<OutputType<SubParser, Input>> Result;

    Input subInput(input);
    KJ_IF_MAYBE(subResult, subParser(subInput)) {
      subInput.advanceParent();
      return Result(kj::mv(*subResult));
    } else {
      return Result(nullptr);
    }
  }

private:
  SubParser subParser;
};

template <typename SubParser>
constexpr Optional_<SubParser> optional(SubParser&& subParser) {
  // Constructs a parser that accepts zero or one of the given sub-parser, returning a Maybe
  // of the sub-parser's result.
  return Optional_<SubParser>(kj::fwd<SubParser>(subParser));
}

// -------------------------------------------------------------------
// oneOf()
// All SubParsers must have same output type, which becomes the output type of the
// OneOfParser.

template <typename... SubParsers>
class OneOf_;

template <typename FirstSubParser, typename... SubParsers>
class OneOf_<FirstSubParser, SubParsers...> {
public:
  template <typename T, typename... U>
  explicit constexpr OneOf_(T&& firstSubParser, U&&... rest)
      : first(kj::fwd<T>(firstSubParser)), rest(kj::fwd<U>(rest)...) {}

  template <typename Input>
  Maybe<OutputType<FirstSubParser, Input>> operator()(Input& input) const {
    {
      Input subInput(input);
      Maybe<OutputType<FirstSubParser, Input>> firstResult = first(subInput);

      if (firstResult != nullptr) {
        subInput.advanceParent();
        return kj::mv(firstResult);
      }
    }

    // Hoping for some tail recursion here...
    return rest(input);
  }

private:
  FirstSubParser first;
  OneOf_<SubParsers...> rest;
};

template <>
class OneOf_<> {
public:
  template <typename Input>
  decltype(nullptr) operator()(Input& input) const {
    return nullptr;
  }
};

template <typename... SubParsers>
constexpr OneOf_<SubParsers...> oneOf(SubParsers&&... parsers) {
  // Constructs a parser that accepts one of a set of options.  The parser behaves as the first
  // sub-parser in the list which returns successfully.  All of the sub-parsers must return the
  // same type.
  return OneOf_<SubParsers...>(kj::fwd<SubParsers>(parsers)...);
}

// -------------------------------------------------------------------
// transform()
// Output = Result of applying transform functor to input value.  If input is a tuple, it is
// unpacked to form the transformation parameters.

template <typename Position>
struct Span {
public:
  inline const Position& begin() { return begin_; }
  inline const Position& end() { return end_; }

  Span() = default;
  inline constexpr Span(Position&& begin, Position&& end): begin_(mv(begin)), end_(mv(end)) {}

private:
  Position begin_;
  Position end_;
};

template <typename Position>
constexpr Span<Decay<Position>> span(Position&& start, Position&& end) {
  return Span<Decay<Position>>(kj::fwd<Position>(start), kj::fwd<Position>(end));
}

template <typename SubParser, typename TransformFunc>
class Transform_ {
public:
  explicit constexpr Transform_(SubParser&& subParser, TransformFunc&& transform)
      : subParser(kj::fwd<SubParser>(subParser)), transform(kj::fwd<TransformFunc>(transform)) {}

  template <typename Input>
  Maybe<decltype(kj::apply(instance<TransformFunc&>(),
                           instance<Span<Decay<decltype(instance<Input&>().getPosition())>>>(),
                           instance<OutputType<SubParser, Input>&&>()))>
      operator()(Input& input) const {
    auto start = input.getPosition();
    KJ_IF_MAYBE(subResult, subParser(input)) {
      return kj::apply(transform, Span<decltype(start)>(kj::mv(start), input.getPosition()),
                       kj::mv(*subResult));
    } else {
      return nullptr;
    }
  }

private:
  SubParser subParser;
  TransformFunc transform;
};

template <typename SubParser, typename TransformFunc>
constexpr Transform_<SubParser, TransformFunc> transform(
    SubParser&& subParser, TransformFunc&& functor) {
  // Constructs a parser which executes some other parser and then transforms the result by invoking
  // `functor` on it.  Typically `functor` is a lambda.  It is invoked using `kj::apply`,
  // meaning tuples will be unpacked as arguments.
  return Transform_<SubParser, TransformFunc>(
      kj::fwd<SubParser>(subParser), kj::fwd<TransformFunc>(functor));
}

// -------------------------------------------------------------------
// acceptIf()
// Output = Same as SubParser

template <typename SubParser, typename Condition>
class AcceptIf_ {
public:
  explicit constexpr AcceptIf_(SubParser&& subParser, Condition&& condition)
      : subParser(kj::mv(subParser)), condition(kj::mv(condition)) {}

  template <typename Input>
  Maybe<OutputType<SubParser, Input>> operator()(Input& input) const {
    KJ_IF_MAYBE(subResult, subParser(input)) {
      if (condition(*subResult)) {
        return kj::mv(*subResult);
      } else {
        return nullptr;
      }
    } else {
      return nullptr;
    }
  }

private:
  SubParser subParser;
  Condition condition;
};

template <typename SubParser, typename Condition>
constexpr AcceptIf_<SubParser, Condition> acceptIf(SubParser&& subParser, Condition&& condition) {
  // Constructs a parser which executes some other parser and then invokes the functor
  // `condition` on the result to check if it is valid.  Typically, `condition` is a lambda
  // returning true or false.  Like with `transform()`, `condition` is invoked using `kj::apply`
  // to unpack tuples.
  return AcceptIf_<SubParser, Condition>(
      kj::fwd<SubParser>(subParser), kj::fwd<Condition>(condition));
}

// -------------------------------------------------------------------
// endOfInput()
// Output = Tuple<>, only succeeds if at end-of-input

class EndOfInput_ {
public:
  template <typename Input>
  Maybe<Tuple<>> operator()(Input& input) const {
    if (input.atEnd()) {
      return Tuple<>();
    } else {
      return nullptr;
    }
  }
};

constexpr EndOfInput_ endOfInput() {
  // Constructs a parser that succeeds only if it is called with no input.
  return EndOfInput_();
}

// -------------------------------------------------------------------
// CharGroup

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
  Maybe<char> operator()(Input& input) const {
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
  return CharGroup_().orRange(first, last);
}
constexpr CharGroup_ anyChar(const char* chars) {
  return CharGroup_().orAny(chars);
}

}  // namespace parse
}  // namespace kj

#endif  // KJ_PARSER_H_
