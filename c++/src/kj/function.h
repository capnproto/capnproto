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

#ifndef KJ_FUNCTION_H_
#define KJ_FUNCTION_H_

#include "memory.h"

namespace kj {

template <typename Signature>
class Function;
// Function wrapper using virtual-based polymorphism.  Use this when template polymorphism is
// not possible.  You can, for example, accept a Function as a parameter:
//
//     void setFilter(Function<bool(const Widget&)> filter);
//
// The caller of `setFilter()` may then pass any callable object as the parameter.  The callable
// object does not have to have the exact signature specified, just one that is "compatible" --
// i.e. the return type is covariant and the parameters are contravariant.
//
// Unlike `std::function`, `kj::Function`s are movable but not copyable, just like `kj::Own`.  This
// is to avoid unexpected heap allocation or slow atomic reference counting.
//
// When a `Function` is constructed from an lvalue, it captures only a reference to the value.
// When constructed from an rvalue, it invokes the value's move constructor.  So, for example:
//
//     struct AddN {
//       int n;
//       int operator(int i) { return i + n; }
//     }
//
//     Function<int(int, int)> f1 = AddN{2};
//     // f1 owns an instance of AddN.  It may safely be moved out
//     // of the local scope.
//
//     AddN adder(2);
//     Function<int(int, int)> f2 = adder;
//     // f2 contains a reference to `adder`.  Thus, it becomes invalid
//     // when `adder` goes out-of-scope.
//
//     AddN adder2(2);
//     Function<int(int, int)> f3 = kj::mv(adder2);
//     // f3 owns an insatnce of AddN moved from `adder2`.  f3 may safely
//     // be moved out of the local scope.
//
// Additionally, a Function may be bound to a class method using KJ_BIND_METHOD(object, methodName).
// For example:
//
//     class Printer {
//     public:
//       void print(int i);
//       void print(kj::StringPtr s);
//     };
//
//     Printer p;
//
//     Function<void(uint)> intPrinter = KJ_BIND_METHOD(p, print);
//     // Will call Printer::print(int).
//
//     Function<void(const char*)> strPrinter = KJ_BIND_METHOD(p, print);
//     // Will call Printer::print(kj::StringPtr).
//
// Notice how KJ_BIND_METHOD is able to figure out which overload to use depending on the kind of
// Function it is binding to.

template <typename Return, typename... Params>
class Function<Return(Params...)> {
public:
  template <typename F>
  inline Function(F&& f): impl(heap<Impl<F>>(kj::fwd<F>(f))) {}
  Function() = default;

  inline Return operator()(Params... params) {
    return (*impl)(kj::fwd<Params>(params)...);
  }

private:
  class Iface {
  public:
    virtual Return operator()(Params... params) = 0;
  };

  template <typename F>
  class Impl final: public Iface {
  public:
    explicit Impl(F&& f): f(kj::fwd<F>(f)) {}

    Return operator()(Params... params) override {
      return f(kj::fwd<Params>(params)...);
    }

  private:
    F f;
  };

  Own<Iface> impl;
};

namespace _ {  // private

template <typename T>
T rvalueOrRef(T&&);
// Hack to help detect if an expression is an lvalue or an rvalue.
//
//     int i;
//     decltype(i) i1(i);                              // i1 has type int.
//     decltype(rvalueOrRef(i)) i2(i);                 // i2 has type int&.
//     decltype(rvalueOrRef(kj::mv(i)) i3(kj::mv(i));  // i3 has type int.

}  // namespace _ (private)

#if 1

namespace _ {  // private

template <typename T, typename Signature, Signature method>
class BoundMethod;

template <typename T, typename Return, typename... Params, Return (Decay<T>::*method)(Params...)>
class BoundMethod<T, Return (Decay<T>::*)(Params...), method> {
public:
  BoundMethod(T&& t): t(kj::fwd<T>(t)) {}

  Return operator()(Params&&... params) {
    return (t.*method)(kj::fwd<Params>(params)...);
  }

private:
  T t;
};

}  // namespace _ (private)

#define KJ_BIND_METHOD(obj, method) \
  ::kj::_::BoundMethod<decltype(::kj::_::rvalueOrRef(obj)), \
                       decltype(&::kj::Decay<decltype(obj)>::method), \
                       &::kj::Decay<decltype(obj)>::method>(obj)
// Macro that produces a functor object which forwards to the method `obj.name`.  If `obj` is an
// lvalue, the functor will hold a reference to it.  If `obj` is an rvalue, the functor will
// contain a copy (by move) of it.
//
// The current implementation requires that the method is not overloaded.
//
// TODO(someday):  C++14's generic lambdas may be able to simplify this code considerably, and
//   probably make it work with overloaded methods.

#else
// Here's a better implementation of the above that doesn't work with GCC (but does with Clang)
// because it uses a local class with a template method.  Sigh.  This implementation supports
// overloaded methods.

#define KJ_BIND_METHOD(obj, method) \
  ({ \
    typedef decltype(::kj::_::rvalueOrRef(obj)) T; \
    class F { \
    public: \
      inline F(T&& t): t(::kj::fwd<T>(t)) {} \
      template <typename... Params> \
      auto operator()(Params&&... params) \
          -> decltype(::kj::instance<T>().method(::kj::fwd<Params>(params)...)) { \
        return t.method(::kj::fwd<Params>(params)...); \
      } \
    private: \
      T t; \
    }; \
    (F(obj)); \
  })
// Macro that produces a functor object which forwards to the method `obj.name`.  If `obj` is an
// lvalue, the functor will hold a reference to it.  If `obj` is an rvalue, the functor will
// contain a copy (by move) of it.

#endif

}  // namespace kj

#endif  // KJ_FUNCTION_H_
