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

template <typename T>
struct ExtractParseFuncType;

template <typename I, typename O, typename Object>
struct ExtractParseFuncType<Maybe<O> (Object::*)(I&) const> {
  typedef I InputType;
  typedef typename I::ElementType ElementType;
  typedef O OutputType;
};

template <typename I, typename O, typename Object>
struct ExtractParseFuncType<Maybe<O> (Object::*)(I&)> {
  typedef I InputType;
  typedef typename I::ElementType ElementType;
  typedef O OutputType;
};

template <typename T>
struct ExtractParserType: public ExtractParseFuncType<decltype(&T::operator())> {};
template <typename T>
struct ExtractParserType<T&>: public ExtractParserType<T> {};
template <typename T>
struct ExtractParserType<T&&>: public ExtractParserType<T> {};
template <typename T>
struct ExtractParserType<const T>: public ExtractParserType<T> {};
template <typename T>
struct ExtractParserType<const T&>: public ExtractParserType<T> {};
template <typename T>
struct ExtractParserType<const T&&>: public ExtractParserType<T> {};

// =======================================================================================

template <typename Input, typename Output>
class ParserWrapper {
public:
  virtual ~ParserWrapper() {}

  typedef Input InputType;
  typedef typename Input::ElementType ElementType;
  typedef Output OutputType;

  virtual Maybe<Output> operator()(Input& input) const = 0;
  virtual Own<ParserWrapper> clone() = 0;
};

template <typename Input, typename Output>
class Parser {
public:
  Parser(const Parser& other): wrapper(other.wrapper->clone()) {}
  Parser(Parser& other): wrapper(other.wrapper->clone()) {}
  Parser(const Parser&& other): wrapper(other.wrapper->clone()) {}
  Parser(Parser&& other): wrapper(kj::mv(other.wrapper)) {}
  Parser(Own<ParserWrapper<Input, Output>> wrapper): wrapper(kj::mv(wrapper)) {}

  template <typename Other>
  Parser(Other&& other): wrapper(heap<WrapperImpl<Other>>(kj::mv(other))) {}

  Parser& operator=(const Parser& other) { wrapper = other.wrapper->clone(); }
  Parser& operator=(Parser&& other) { wrapper = kj::mv(other.wrapper); }

  // Always inline in the hopes that this allows branch prediction to kick in so the virtual call
  // doesn't hurt so much.
  inline Maybe<Output> operator()(Input& input) const __attribute__((always_inline)) {
    return (*wrapper)(input);
  }

private:
  Own<ParserWrapper<Input, Output>> wrapper;

  template <typename Other>
  struct WrapperImpl: public ParserWrapper<Input, Output> {
    WrapperImpl(Other&& impl): impl(kj::mv(impl)) {};
    ~WrapperImpl() {}

    Maybe<Output> operator()(Input& input) const {
      return impl(input);
    }

    Own<ParserWrapper<Input, Output>> clone() {
      return heap<WrapperImpl>(*this);
    }

    Other impl;
  };
};

template <typename ParserImpl>
Parser<typename ExtractParserType<ParserImpl>::InputType,
       typename ExtractParserType<ParserImpl>::OutputType>
wrap(ParserImpl&& impl) {
  typedef typename ExtractParserType<ParserImpl>::InputType Input;
  typedef typename ExtractParserType<ParserImpl>::OutputType Output;

  return Parser<Input, Output>(kj::mv(impl));
}

template <typename SubParser>
class ParserRef {
public:
  explicit ParserRef(const SubParser& parser): parser(&parser) {}

  Maybe<typename ExtractParserType<SubParser>::OutputType> operator()(
      typename ExtractParserType<SubParser>::InputType& input) const {
    return (*parser)(input);
  }

private:
  const SubParser* parser;
};

template <typename SubParser>
ParserRef<Decay<SubParser>> ref(const SubParser& impl) {
  return ParserRef<Decay<SubParser>>(impl);
}

template <typename T>
struct MaybeRef {
  typedef Decay<T> Type;

  template <typename U>
  static Type from(U&& parser) {
    return static_cast<Type&&>(parser);
  }
};

template <typename T>
struct MaybeRef<T&> {
  typedef ParserRef<Decay<T>> Type;

  template <typename U>
  static Type from(U& parser) {
    return parse::ref(parser);
  }
};

template <template <typename SubParser> class WrapperParser>
struct WrapperParserConstructor {
  template <typename SubParser, typename... Args>
  WrapperParser<typename MaybeRef<SubParser>::Type> operator()(
      SubParser&& subParser, Args&&... args) {
    return WrapperParser<typename MaybeRef<SubParser>::Type>(
        MaybeRef<SubParser>::from(subParser),
        kj::fwd(args)...);
  }
};

// -------------------------------------------------------------------
// ExactElementParser
// Output = Tuple<>

template <typename Input>
class ExactElementParser {
public:
  explicit ExactElementParser(typename Input::ElementType&& expected): expected(expected) {}

  virtual Maybe<Tuple<>> operator()(Input& input) const {
    if (input.atEnd() || input.current() != expected) {
      return nullptr;
    } else {
      input.next();
      return Tuple<>();
    }
  }

private:
  typename Input::ElementType expected;
};

template <typename Input>
ExactElementParser<Input> exactElement(typename Input::ElementType&& expected) {
  return ExactElementParser<Decay<Input>>(kj::mv(expected));
}

// -------------------------------------------------------------------
// SequenceParser
// Output = Flattened Tuple of outputs of sub-parsers.

template <typename Input, typename... SubParsers> class SequenceParser;

template <typename Input, typename FirstSubParser, typename... SubParsers>
class SequenceParser<Input, FirstSubParser, SubParsers...> {
public:
  template <typename T, typename... U>
  explicit SequenceParser(T&& firstSubParser, U&&... rest)
      : first(kj::fwd<T>(firstSubParser)), rest(kj::fwd<U>(rest)...) {}

  auto operator()(Input& input) const ->
      Maybe<decltype(tuple(
          instance<typename ExtractParserType<FirstSubParser>::OutputType>(),
          instance<typename ExtractParserType<SubParsers>::OutputType>()...))> {
    return parseNext(input);
  }

  template <typename... InitialParams>
  auto parseNext(Input& input, InitialParams&&... initialParams) const ->
      Maybe<decltype(tuple(
          kj::fwd<InitialParams>(initialParams)...,
          instance<typename ExtractParserType<FirstSubParser>::OutputType>(),
          instance<typename ExtractParserType<SubParsers>::OutputType>()...))> {
    KJ_IF_MAYBE(firstResult, first(input)) {
      return rest.parseNext(input, kj::fwd<InitialParams>(initialParams)...,
                            kj::mv(*firstResult));
    } else {
      return nullptr;
    }
  }

private:
  FirstSubParser first;
  SequenceParser<Input, SubParsers...> rest;
};

template <typename Input>
class SequenceParser<Input> {
public:
  Maybe<Tuple<>> operator()(Input& input) const {
    return parseNext(input);
  }

  template <typename... Params>
  auto parseNext(Input& input, Params&&... params) const ->
      Maybe<decltype(tuple(kj::fwd<Params>(params)...))> {
    return tuple(kj::fwd<Params>(params)...);
  }
};

template <typename FirstSubParser, typename... MoreSubParsers>
SequenceParser<typename ExtractParserType<FirstSubParser>::InputType,
               typename MaybeRef<FirstSubParser>::Type,
               typename MaybeRef<MoreSubParsers>::Type...>
sequence(FirstSubParser&& first, MoreSubParsers&&... rest) {
  return SequenceParser<typename ExtractParserType<FirstSubParser>::InputType,
                        typename MaybeRef<FirstSubParser>::Type,
                        typename MaybeRef<MoreSubParsers>::Type...>(
      MaybeRef<FirstSubParser>::from(first), MaybeRef<MoreSubParsers>::from(rest)...);
}


// -------------------------------------------------------------------
// RepeatedParser
// Output = Array of output of sub-parser.

template <typename SubParser, bool atLeastOne>
class RepeatedParser {
public:
  explicit RepeatedParser(SubParser&& subParser)
      : subParser(kj::mv(subParser)) {}

  Maybe<Vector<typename ExtractParserType<SubParser>::OutputType>> operator()(
      typename ExtractParserType<SubParser>::InputType& input) const {
    typedef Vector<typename ExtractParserType<SubParser>::OutputType> Results;
    Results results;

    while (!input.atEnd()) {
      typename ExtractParserType<SubParser>::InputType subInput(input);

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

    return kj::mv(results);
  }

private:
  SubParser subParser;
};

template <typename SubParser>
RepeatedParser<typename MaybeRef<SubParser>::Type, false>
repeated(SubParser&& subParser) {
  return RepeatedParser<typename MaybeRef<SubParser>::Type, false>(
      MaybeRef<SubParser>::from(subParser));
}

template <typename SubParser>
RepeatedParser<typename MaybeRef<SubParser>::Type, true>
oneOrMore(SubParser&& subParser) {
  return RepeatedParser<typename MaybeRef<SubParser>::Type, true>(
      MaybeRef<SubParser>::from(subParser));
}

// -------------------------------------------------------------------
// OptionalParser
// Output = Maybe<output of sub-parser>

template <typename SubParser>
class OptionalParser {
public:
  explicit OptionalParser(SubParser&& subParser)
      : subParser(kj::mv(subParser)) {}

  Maybe<Maybe<typename ExtractParserType<SubParser>::OutputType>> operator()(
      typename ExtractParserType<SubParser>::InputType& input) const {
    typedef Maybe<typename ExtractParserType<SubParser>::OutputType> Result;

    typename ExtractParserType<SubParser>::InputType subInput(input);
    auto subResult = subParser(subInput);

    if (subResult == nullptr) {
      return Result(nullptr);
    } else {
      subInput.advanceParent();
      return Result(kj::mv(*subResult));
    }
  }

private:
  SubParser subParser;
};

template <typename SubParser>
OptionalParser<typename MaybeRef<SubParser>::Type>
optional(SubParser&& subParser) {
  return OptionalParser<typename MaybeRef<SubParser>::Type>(
      MaybeRef<SubParser>::from(subParser));
}

// -------------------------------------------------------------------
// OneOfParser
// All SubParsers must have same output type, which becomes the output type of the
// OneOfParser.

template <typename Input, typename Output, typename... SubParsers>
class OneOfParser;

template <typename Input, typename Output, typename FirstSubParser, typename... SubParsers>
class OneOfParser<Input, Output, FirstSubParser, SubParsers...> {
public:
  template <typename T, typename... U>
  explicit OneOfParser(T&& firstSubParser, U&&... rest)
      : first(kj::fwd<T>(firstSubParser)), rest(kj::fwd<U>(rest)...) {}

  Maybe<Output> operator()(Input& input) const {
    {
      Input subInput(input);
      Maybe<Output> firstResult = first(subInput);

      if (firstResult != nullptr) {
        // MAYBE: Should we try parsing with "rest" in order to check for ambiguities?
        subInput.advanceParent();
        return kj::mv(firstResult);
      }
    }

    // Hoping for some tail recursion here...
    return rest(input);
  }

private:
  FirstSubParser first;
  OneOfParser<Input, Output, SubParsers...> rest;
};

template <typename Input, typename Output>
class OneOfParser<Input, Output> {
public:
  Maybe<Output> operator()(Input& input) const {
    return nullptr;
  }
};

template <typename FirstSubParser, typename... MoreSubParsers>
OneOfParser<typename ExtractParserType<FirstSubParser>::InputType,
            typename ExtractParserType<FirstSubParser>::OutputType,
            typename MaybeRef<FirstSubParser>::Type,
            typename MaybeRef<MoreSubParsers>::Type...>
oneOf(FirstSubParser&& first, MoreSubParsers&&... rest) {
  return OneOfParser<typename ExtractParserType<FirstSubParser>::InputType,
                     typename ExtractParserType<FirstSubParser>::OutputType,
                     typename MaybeRef<FirstSubParser>::Type,
                     typename MaybeRef<MoreSubParsers>::Type...>(
      MaybeRef<FirstSubParser>::from(first), MaybeRef<MoreSubParsers>::from(rest)...);
}

// -------------------------------------------------------------------
// TransformParser
// Output = Result of applying transform functor to input value.  If input is a tuple, it is
// unpacked to form the transformation parameters.

template <typename Position>
struct Span {
public:
  inline const Position& begin() { return begin_; }
  inline const Position& end() { return end_; }

  Span() = default;
  inline Span(Position&& begin, Position&& end): begin_(mv(begin)), end_(mv(end)) {}

private:
  Position begin_;
  Position end_;
};

template <typename SubParser, typename Transform>
class TransformParser {
public:
  explicit TransformParser(SubParser&& subParser, Transform&& transform)
      : subParser(kj::mv(subParser)), transform(kj::mv(transform)) {}

  typedef typename ExtractParserType<SubParser>::InputType InputType;
  typedef Decay<decltype(instance<InputType>().getPosition())> Position;
  typedef typename ExtractParserType<SubParser>::OutputType SubOutput;
  typedef decltype(kj::apply(instance<Transform&>(), instance<Span<Position>>(),
                             instance<SubOutput&&>())) Output;

  Maybe<Output> operator()(InputType& input) const {
    auto start = input.getPosition();
    KJ_IF_MAYBE(subResult, subParser(input)) {
      return kj::apply(transform, Span<Position>(kj::mv(start), input.getPosition()),
                       kj::mv(*subResult));
    } else {
      return nullptr;
    }
  }

private:
  SubParser subParser;
  Transform transform;
};

template <typename SubParser, typename Transform>
TransformParser<typename MaybeRef<SubParser>::Type, Decay<Transform>>
transform(SubParser&& subParser, Transform&& transform) {
  return TransformParser<typename MaybeRef<SubParser>::Type, Decay<Transform>>(
      MaybeRef<SubParser>::from(subParser), kj::fwd<Transform>(transform));
}

// -------------------------------------------------------------------
// AcceptIfParser
// Output = Same as SubParser

template <typename SubParser, typename Condition>
class AcceptIfParser {
public:
  explicit AcceptIfParser(SubParser&& subParser, Condition&& condition)
      : subParser(kj::mv(subParser)), condition(kj::mv(condition)) {}

  Maybe<typename ExtractParserType<SubParser>::OutputType>
  operator()(typename ExtractParserType<SubParser>::InputType& input) const {
    Maybe<typename ExtractParserType<SubParser>::OutputType> subResult = subParser(input);
    if (subResult && !condition(*subResult)) {
      subResult = nullptr;
    }
    return subResult;
  }

private:
  SubParser subParser;
  Condition condition;
};

template <typename SubParser, typename Condition>
AcceptIfParser<typename MaybeRef<SubParser>::Type, Decay<Condition>>
acceptIf(SubParser&& subParser, Condition&& condition) {
  return AcceptIfParser<typename MaybeRef<SubParser>::Type, Decay<Condition>>(
      MaybeRef<SubParser>::from(subParser), kj::fwd<Condition>(condition));
}

// -------------------------------------------------------------------
// EndOfInputParser
// Output = Tuple<>, only succeeds if at end-of-input

template <typename Input>
class EndOfInputParser {
public:
  Maybe<Tuple<>> operator()(Input& input) const {
    if (input.atEnd()) {
      return Tuple<>();
    } else {
      return nullptr;
    }
  }
};

template <typename T>
EndOfInputParser<T> endOfInput() {
  return EndOfInputParser<T>();
}

}  // namespace parse
}  // namespace kj

#endif  // KJ_PARSER_H_
