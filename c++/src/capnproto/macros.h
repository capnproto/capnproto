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

#ifndef CAPNPROTO_MACROS_H_
#define CAPNPROTO_MACROS_H_

#include <inttypes.h>

#ifdef __GNUC__
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#warning "Did you forget to enable C++11?  Make sure to pass -std=gnu++11 to GCC."
#endif

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 7)
#warning "Cap'n Proto requires at least GCC 4.7."
#endif
#endif

namespace capnproto {

#define CAPNPROTO_DISALLOW_COPY(classname) \
  classname(const classname&) = delete; \
  classname& operator=(const classname&) = delete

namespace internal {

#define CAPNPROTO_OFFSETOF(type, member) __builtin_offsetof(type, member)

#define CAPNPROTO_EXPECT_TRUE(condition) __builtin_expect(condition, true)
#define CAPNPROTO_EXPECT_FALSE(condition) __builtin_expect(condition, false)
// Branch prediction macros.  Evaluates to the condition given, but also tells the compiler that we
// expect the condition to be true/false enough of the time that it's worth hard-coding branch
// prediction.

#ifdef NDEBUG
#define CAPNPROTO_ALWAYS_INLINE(prototype) inline prototype __attribute__((always_inline))
// Force a function to always be inlined.  Apply only to the prototype, not to the definition.
#else
#define CAPNPROTO_ALWAYS_INLINE(prototype) inline prototype
// Don't force inline in debug mode.
#endif

#define CAPNPROTO_NORETURN __attribute__((noreturn));

void inlinePreconditionFailure(
    const char* file, int line, const char* expectation, const char* macroArgs,
    const char* message = nullptr) CAPNPROTO_NORETURN;

#define CAPNPROTO_INLINE_PRECOND(condition, ...) \
    if (CAPNPROTO_EXPECT_TRUE(condition)); else ::capnproto::internal::inlinePreconditionFailure( \
        __FILE__, __LINE__, #condition, #__VA_ARGS__, ##__VA_ARGS__)
// Version of PRECOND() which is safe to use in headers that are #included by users.  Used to check
// preconditions inside inline methods.  CAPNPROTO_INLINE_DPRECOND is particularly useful in that
// it will be enabled depending on whether the application is compiled in debug mode rather than
// whether libcapnproto is.

#ifdef NDEBUG
#define CAPNPROTO_INLINE_DPRECOND(...)
#else
#define CAPNPROTO_INLINE_DPRECOND CAPNPROTO_INLINE_PRECOND
#endif

// Allocate an array, preferably on the stack, unless it is too big.  On GCC this will use
// variable-sized arrays.  For other compilers we could just use a fixed-size array.
#define CAPNPROTO_STACK_ARRAY(type, name, size, maxStack) \
  size_t name##_size = (size); \
  bool name##_isOnStack = name##_size <= (maxStack); \
  type name##_stack[name##_isOnStack ? size : 0]; \
  ::capnproto::Array<type> name##_heap = name##_isOnStack ? \
      nullptr : newArray<type>(name##_size); \
  ::capnproto::ArrayPtr<type> name = name##_isOnStack ? \
      arrayPtr(name##_stack, name##_size) : name##_heap

}  // namespace internal

template <typename T, typename U>
T implicit_cast(U u) {
  return u;
}

}  // namespace capnproto

#endif  // CAPNPROTO_MACROS_H_
