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

// This file contains a bunch of internal declarations that must appear before async.h can start.
// We don't define these directly in async.h because it makes the file hard to read.

#pragma once

#if defined(__GNUC__) && !KJ_HEADER_WARNINGS
#pragma GCC system_header
#endif

#include "exception.h"
#include "tuple.h"

namespace kj {

class EventLoop;
template <typename... T>
class Promise;
class WaitScope;
class TaskSet;

template <typename... T>
Promise<Array<kj::Tuple<T...>>> joinPromises(Array<Promise<T...>>&& promises);
Promise<void> joinPromises(Array<Promise<void>>&& promises);
Promise<> joinPromises(Array<Promise<>>&& promises);

template <typename... T>
Promise<Array<kj::Tuple<T...>>> joinPromisesFailfast(Array<Promise<T...>>&& promises);
Promise<void> joinPromisesFailfast(Array<Promise<void>>&& promises);
Promise<> joinPromisesFailfast(Array<Promise<>>&& promises);

template <typename... T>
Promise<T...> reducePromise(void*, Promise<T...>&& promise);
template <typename... T>
Promise<T...> reducePromise(Promise<T...>*, Promise<Promise<T...>>&& promise);

namespace _ {  // private

template <typename... T> struct PromiseFromTuple_ { typedef Promise<T...> Type; };
template <typename... T>
struct PromiseFromTuple_<kj::_::Tuple<T...>> {
  typedef Promise<T...> Type;
};
template <typename... T> using PromiseFromTuple = typename PromiseFromTuple_<T...>::Type;

template <typename T>
Promise<T> chainPromiseType(T*);
template <typename... T>
Promise<T...> chainPromiseType(_::Tuple<T...>*);
template <typename... T>
Promise<T...> chainPromiseType(Promise<T...>*);

template <typename... T>
using ChainPromises = decltype(chainPromiseType((kj::Tuple<T...>*)nullptr));
// Constructs a promise for T, reducing double-promises. That is, if T is Promise<U>, resolves to
// Promise<U>, otherwise resolves to Promise<T>.
//
// In the case of multiple values, no chaining occurs, even if the values are promises. E.g.
// Promise<Promise<T>, U> does NOT become Promise<T, U> -- because this would delay the
// application's ability to access the U. The application may indeed be planning to split the
// Promise<Promise<T>, U> into a Promise<T> and a Promise<U>, where the Promise<U> would be
// expected to resolve earlier than the Promise<T>. If the application really wants to reduce
// to Promise<T, U>, it can achieve this by splitting and re-joining the promise.

template <typename... T>
using ReducePromises =
    decltype(reducePromise(instance<kj::Tuple<T...>*>(), instance<PromiseFromTuple<T...>&&>()));

class PropagateException {
  // A functor which accepts a kj::Exception as a parameter and returns a broken promise of
  // arbitrary type which simply propagates the exception.
public:
  class Bottom {
  public:
    Bottom(Exception&& exception): exception(kj::mv(exception)) {}

    Exception asException() { return kj::mv(exception); }

  private:
    Exception exception;
  };

  Bottom operator()(Exception&& e) {
    return Bottom(kj::mv(e));
  }
  Bottom operator()(const  Exception& e) {
    return Bottom(kj::cp(e));
  }
};

template <typename Func, typename... T>
struct ReturnType_ { typedef decltype(kj::apply(instance<Func>(), instance<T>()...)) Type; };
template <typename Func>
struct ReturnType_<Func, void> { typedef decltype(instance<Func>()()) Type; };

template <typename Func, typename... T>
using ReturnType = typename ReturnType_<Func, T...>::Type;
// The return type of functor Func given a parameter of type T, with the special exception that if
// T is void, this is the return type of Func called with no arguments.

template <typename T> struct SplitTuplePromise_ { typedef Promise<T> Type; };
template <typename... T>
struct SplitTuplePromise_<kj::_::Tuple<T...>> {
  typedef kj::Tuple<ReducePromises<T>...> Type;
};

template <typename T>
using SplitTuplePromise = typename SplitTuplePromise_<T>::Type;
// T -> Promise<T>
// Tuple<T> -> Tuple<Promise<T>>

typedef Tuple<> Void;
// Application code should NOT refer to this!  See `kj::READY_NOW` instead.

template <typename... T> struct PromiseNodeType_ { typedef kj::Tuple<T...> Type; };
template <> struct PromiseNodeType_<void> { typedef Void Type; };
template <typename... T> using PromiseNodeType = typename PromiseNodeType_<T...>::Type;
// PromiseNodeType<T...> is the type that a PromiseNode deals in to implement Promise<T...>.

template <typename In, typename Out>
struct MaybeVoidCaller {
  // Calls the function converting a Tuple input to a parameter list and a void return
  // value to a Void output. Recall that Void is a typedef for Tuple<>.

  template <typename Func>
  static inline Out apply(Func& func, In&& in) {
    return kj::apply(func, kj::mv(in));
  }
};
template <typename In, typename Out>
struct MaybeVoidCaller<In&, Out> {
  template <typename Func>
  static inline Out apply(Func& func, In& in) {
    return kj::apply(func, in);
  }
};
template <typename In>
struct MaybeVoidCaller<In, Void> {
  template <typename Func>
  static inline Void apply(Func& func, In&& in) {
    kj::apply(func, kj::mv(in));
    return Void();
  }
};
template <typename In>
struct MaybeVoidCaller<In&, Void> {
  template <typename Func>
  static inline Void apply(Func& func, In& in) {
    kj::apply(func, in);
    return Void();
  }
};
template <typename In>
struct MaybeVoidCaller<In, void>: public MaybeVoidCaller<In, Void> {};

template <typename T>
inline T&& returnMaybeVoid(T&& t) {
  return kj::fwd<T>(t);
}
inline void returnMaybeVoid(Void&& v) {}

class ExceptionOrValue;
class PromiseNode;
class ChainPromiseNode;
template <typename T>
class ForkHub;

class Event;

class PromiseBase {
public:
  kj::String trace();
  // Dump debug info about this promise.

private:
  Own<PromiseNode> node;

  PromiseBase() = default;
  PromiseBase(Own<PromiseNode>&& node): node(kj::mv(node)) {}

  friend class kj::EventLoop;
  friend class ChainPromiseNode;
  template <typename...>
  friend class kj::Promise;
  friend class kj::TaskSet;
  template <typename... U>
  friend Promise<Array<kj::Tuple<U...>>> kj::joinPromises(Array<Promise<U...>>&& promises);
  friend Promise<void> kj::joinPromises(Array<Promise<void>>&& promises);
  friend Promise<> kj::joinPromises(Array<Promise<>>&& promises);
  template <typename... U>
  friend Promise<Array<kj::Tuple<U...>>> kj::joinPromisesFailfast(Array<Promise<U...>>&& promises);
  friend Promise<void> kj::joinPromisesFailfast(Array<Promise<void>>&& promises);
  friend Promise<> kj::joinPromisesFailfast(Array<Promise<>>&& promises);
  template <typename... U>
  friend Promise<U...> kj::reducePromise(Promise<U...>*, Promise<Promise<U...>>&& promise);
};

void detach(kj::Promise<void>&& promise);
void waitImpl(Own<_::PromiseNode>&& node, _::ExceptionOrValue& result, WaitScope& waitScope);
bool pollImpl(_::PromiseNode& node, WaitScope& waitScope);
Promise<void> yield();
Own<PromiseNode> neverDone();

class ReadyNow {
public:
  inline operator Promise<>() const;

  KJ_NORETURN(void wait(WaitScope& waitScope) const);
};

class NeverDone {
public:
  template <typename... T>
  operator Promise<T...>() const {
    return Promise<T...>(false, neverDone());
  }

  KJ_NORETURN(void wait(WaitScope& waitScope) const);
};

}  // namespace _ (private)
}  // namespace kj
