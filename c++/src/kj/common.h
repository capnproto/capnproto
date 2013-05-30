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

// Header that should be #included by everyone.
//
// This defines very simple utilities that are widely applicable.

#ifndef KJ_COMMON_H_
#define KJ_COMMON_H_

#if __cplusplus < 201103L
  #error "This code requires C++11. Either your compiler does not support it or it is not enabled."
  #ifdef __GNUC__
    // Compiler claims compatibility with GCC, so presumably supports -std.
    #error "Pass -std=c++11 on the compiler command line to enable C++11."
  #endif
#endif

#ifdef __GNUC__
  #if __clang__
    #if __clang_major__ < 3 || (__clang_major__ == 3 && __clang_minor__ < 2)
      #warning "This library requires at least Clang 3.2."
    #endif
  #else
    #if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 7)
      #warning "This library requires at least GCC 4.7."
    #endif
  #endif
#endif

// =======================================================================================

namespace kj {

typedef unsigned int uint;
typedef unsigned char byte;

// =======================================================================================
// Common macros, especially for common yet compiler-specific features.

#define KJ_DISALLOW_COPY(classname) \
  classname(const classname&) = delete; \
  classname& operator=(const classname&) = delete

#define KJ_OFFSETOF(type, member) __builtin_offsetof(type, member)

#define KJ_EXPECT_TRUE(condition) __builtin_expect(condition, true)
#define KJ_EXPECT_FALSE(condition) __builtin_expect(condition, false)
// Branch prediction macros.  Evaluates to the condition given, but also tells the compiler that we
// expect the condition to be true/false enough of the time that it's worth hard-coding branch
// prediction.

#ifdef NDEBUG
#define KJ_ALWAYS_INLINE(prototype) inline prototype __attribute__((always_inline))
// Force a function to always be inlined.  Apply only to the prototype, not to the definition.
#else
#define KJ_ALWAYS_INLINE(prototype) inline prototype
// Don't force inline in debug mode.
#endif

#define KJ_NORETURN __attribute__((noreturn));
#define KJ_UNUSED __attribute__((unused));

#if __clang__
#define KJ_UNUSED_FOR_CLANG __attribute__((unused));
// Clang reports "unused" warnings in some places where GCC does not even allow the "unused"
// attribute.
#else
#define KJ_UNUSED_FOR_CLANG
#endif

namespace internal {

void inlinePreconditionFailure(
    const char* file, int line, const char* expectation, const char* macroArgs,
    const char* message = nullptr) KJ_NORETURN;

}  // namespace internal

#define KJ_INLINE_PRECOND(condition, ...) \
    if (KJ_EXPECT_TRUE(condition)); else ::kj::internal::inlinePreconditionFailure( \
        __FILE__, __LINE__, #condition, #__VA_ARGS__, ##__VA_ARGS__)
// Version of PRECOND() which is safe to use in headers that are #included by users.  Used to check
// preconditions inside inline methods.  KJ_INLINE_DPRECOND is particularly useful in that
// it will be enabled depending on whether the application is compiled in debug mode rather than
// whether libkj is.

#ifdef NDEBUG
#define KJ_INLINE_DPRECOND(...)
#else
#define KJ_INLINE_DPRECOND KJ_INLINE_PRECOND
#endif

// #define KJ_STACK_ARRAY(type, name, size, minStack, maxStack)
//
// Allocate an array, preferably on the stack, unless it is too big.  On GCC this will use
// variable-sized arrays.  For other compilers we could just use a fixed-size array.  `minStack`
// is the stack array size to use if variable-width arrays are not supported.  `maxStack` is the
// maximum stack array size if variable-width arrays *are* supported.
#if __clang__
#define KJ_STACK_ARRAY(type, name, size, minStack, maxStack) \
  size_t name##_size = (size); \
  bool name##_isOnStack = name##_size <= (minStack); \
  type name##_stack[minStack]; \
  ::kj::Array<type> name##_heap = name##_isOnStack ? \
      nullptr : kj::newArray<type>(name##_size); \
  ::kj::ArrayPtr<type> name = name##_isOnStack ? \
      kj::arrayPtr(name##_stack, name##_size) : name##_heap
#else
#define KJ_STACK_ARRAY(type, name, size, minStack, maxStack) \
  size_t name##_size = (size); \
  bool name##_isOnStack = name##_size <= (maxStack); \
  type name##_stack[name##_isOnStack ? size : 0]; \
  ::kj::Array<type> name##_heap = name##_isOnStack ? \
      nullptr : kj::newArray<type>(name##_size); \
  ::kj::ArrayPtr<type> name = name##_isOnStack ? \
      kj::arrayPtr(name##_stack, name##_size) : name##_heap
#endif

// =======================================================================================
// Template metaprogramming helpers.

template <typename T>
struct NoInfer_ {
  // Use NoInfer<T>::Type in place of T for a template function parameter to prevent inference of
  // the type based on the parameter value.  There's something in the standard library for this but
  // I didn't want to #include type_traits or whatever.
  typedef T Type;
};
template <typename T>
using NoInfer = typename NoInfer_<T>::Type;

template <typename T> struct RemoveReference_ { typedef T Type; };
template <typename T> struct RemoveReference_<T&> { typedef T Type; };
template <typename T> using RemoveReference = typename RemoveReference_<T>::Type;

template <typename> struct IsLvalueReference_ { static constexpr bool value = false; };
template <typename T> struct IsLvalueReference_<T&> { static constexpr bool value = true; };
template <typename T>
inline constexpr bool isLvalueReference() { return IsLvalueReference_<T>::value; }

template <typename T>
T instance() noexcept;
// Like std::declval, but doesn't transform T into an rvalue reference.  If you want that, specify
// instance<T&&>().

// =======================================================================================
// Equivalents to std::move() and std::forward(), since these are very commonly needed and the
// std header <utility> pulls in lots of other stuff.
//
// We use abbreviated names mv and fwd because these helpers (especially mv) are so commonly used
// that the cost of typing more letters outweighs the cost of being slightly harder to understand
// when first encountered.

template<typename T> constexpr T&& mv(T& t) noexcept { return static_cast<T&&>(t); }

template<typename T>
constexpr T&& fwd(RemoveReference<T>& t) noexcept {
  return static_cast<T&&>(t);
}
template<typename T> constexpr T&& fwd(RemoveReference<T>&& t) noexcept {
  static_assert(!isLvalueReference<T>(), "Attempting to forward rvalue as lvalue reference.");
  return static_cast<T&&>(t);
}

// =======================================================================================
// Upcast/downcast

template <typename To, typename From>
To upcast(From&& from) {
  // `upcast<T>(value)` casts `value` to type `T` only if the conversion is implicit.  Useful for
  // e.g. resolving ambiguous overloads without sacrificing type-safety.
  return kj::fwd<From>(from);
}

template <typename To, typename From>
To dynamicDowncastIfAvailable(From* from) {
  // If RTTI is disabled, always returns nullptr.  Otherwise, works like dynamic_cast.  Useful
  // in situations where dynamic_cast could allow an optimization, but isn't strictly necessary
  // for correctness.  It is highly recommended that you try to arrange all your dynamic_casts
  // this way, as a dynamic_cast that is necessary for correctness implies a flaw in the interface
  // design.

#if KJ_NO_RTTI
  return nullptr;
#else
  return dynamic_cast<To>(from);
#endif
}

template <typename To, typename From>
To downcast(From* from) {
  // Down-cast a value to a sub-type, asserting that the cast is valid.  In opt mode this is a
  // static_cast, but in debug mode (when RTTI is enabled) a dynamic_cast will be used to verify
  // that the value really has the requested type.

  // Force a compile error if To is not a subtype of From.
  if (false) {
    kj::upcast<From*>(To());
  }

#if !KJ_NO_RTTI
  KJ_INLINE_DPRECOND(
      from == nullptr || dynamic_cast<To>(from) != nullptr,
      "Value cannot be downcast() to requested type.");
#endif

  return static_cast<To>(from);
}

template <typename To, typename From>
To downcast(From&& from) {
  // Reference version of downcast().
  return *kj::downcast<To*>(&from);
}

}  // namespace kj

#endif  // KJ_COMMON_H_
