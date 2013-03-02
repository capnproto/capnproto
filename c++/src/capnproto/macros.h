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

#define CAPNPROTO_ALWAYS_INLINE(prototype) inline prototype __attribute__((always_inline))
// Force a function to always be inlined.  Apply only to the prototype, not to the definition.

void assertionFailure(const char* file, int line, const char* expectation, const char* message)
    __attribute__((noreturn));

#define CAPNPROTO_ASSERT(condition, message) \
    if (CAPNPROTO_EXPECT_TRUE(condition)); else ::capnproto::internal::assertionFailure(\
        __FILE__, __LINE__, #condition, message)

// CAPNPROTO_ASSERT is just like assert() except it avoids polluting the global namespace with an
// unqualified macro name and it throws an exception (derived from std::exception).
#ifdef NDEBUG
#define CAPNPROTO_DEBUG_ASSERT(condition, message)
#else
#define CAPNPROTO_DEBUG_ASSERT(condition, message) CAPNPROTO_ASSERT(condition, message)
#endif

}  // namespace internal

template <typename T, typename U>
T implicit_cast(U u) {
  return u;
}

}  // namespace capnproto

#endif  // CAPNPROTO_MACROS_H_
