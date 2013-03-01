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
#include "unit.h"

namespace capnproto {

// TODO:  Rename file "common.h" since it's no longer just macros.

#define CAPNPROTO_DISALLOW_COPY(classname) \
  classname(const classname&) = delete; \
  classname& operator=(const classname&) = delete

typedef unsigned int uint;

class byte { uint8_t  content; CAPNPROTO_DISALLOW_COPY(byte); };
class word { uint64_t content; CAPNPROTO_DISALLOW_COPY(word); };
// byte and word are opaque types with sizes of 8 and 64 bits, respectively.  These types are useful
// only to make pointer arithmetic clearer.  Since the contents are private, the only way to access
// them is to first reinterpret_cast to some other pointer type.
//
// Coping is disallowed because you should always use memcpy().  Otherwise, you may run afoul of
// aliasing rules (particularly when copying words).
//
// A pointer of type word* should always be word-aligned even if won't actually be dereferenced as
// that type.

static_assert(sizeof(byte) == 1, "uint8_t is not one byte?");
static_assert(sizeof(word) == 8, "uint64_t is not 8 bytes?");

namespace internal { class bit; }

#ifdef __CDT_PARSER__
// Eclipse gets confused by decltypes, so we'll feed it these simplified-yet-compatible definitions.
//
// We could also consider using these definitions in opt builds.  Trouble is, the mangled symbol
// names of any functions that take these types as inputs would be affected, so it would be
// important to compile the Cap'n Proto library and the client app with the same flags.

typedef uint BitCount;
typedef uint8_t BitCount8;
typedef uint16_t BitCount16;
typedef uint32_t BitCount32;
typedef uint64_t BitCount64;

typedef uint ByteCount;
typedef uint8_t ByteCount8;
typedef uint16_t ByteCount16;
typedef uint32_t ByteCount32;
typedef uint64_t ByteCount64;

typedef uint WordCount;
typedef uint8_t WordCount8;
typedef uint16_t WordCount16;
typedef uint32_t WordCount32;
typedef uint64_t WordCount64;

constexpr BitCount BITS = 1;
constexpr ByteCount BYTES = 1;
constexpr WordCount WORDS = 1;

constexpr uint BITS_PER_BYTE = 8;
constexpr uint BITS_PER_WORD = 64;
constexpr uint BYTES_PER_WORD = 8;

#else

typedef UnitMeasure<uint, internal::bit> BitCount;
typedef UnitMeasure<uint8_t, internal::bit> BitCount8;
typedef UnitMeasure<uint16_t, internal::bit> BitCount16;
typedef UnitMeasure<uint32_t, internal::bit> BitCount32;
typedef UnitMeasure<uint64_t, internal::bit> BitCount64;

typedef UnitMeasure<uint, byte> ByteCount;
typedef UnitMeasure<uint8_t, byte> ByteCount8;
typedef UnitMeasure<uint16_t, byte> ByteCount16;
typedef UnitMeasure<uint32_t, byte> ByteCount32;
typedef UnitMeasure<uint64_t, byte> ByteCount64;

typedef UnitMeasure<uint, word> WordCount;
typedef UnitMeasure<uint8_t, word> WordCount8;
typedef UnitMeasure<uint16_t, word> WordCount16;
typedef UnitMeasure<uint32_t, word> WordCount32;
typedef UnitMeasure<uint64_t, word> WordCount64;

constexpr BitCount BITS = BitCount::ONE;
constexpr ByteCount BYTES = ByteCount::ONE;
constexpr WordCount WORDS = WordCount::ONE;

constexpr UnitRatio<uint, internal::bit, byte> BITS_PER_BYTE(8);
constexpr UnitRatio<uint, internal::bit, word> BITS_PER_WORD(64);
constexpr UnitRatio<uint, byte, word> BYTES_PER_WORD(8);

template <typename T>
inline constexpr byte* operator+(byte* ptr, UnitMeasure<T, byte> offset) {
  return ptr + offset / BYTES;
}
template <typename T>
inline constexpr const byte* operator+(const byte* ptr, UnitMeasure<T, byte> offset) {
  return ptr + offset / BYTES;
}
template <typename T>
inline constexpr byte* operator+=(byte*& ptr, UnitMeasure<T, byte> offset) {
  return ptr = ptr + offset / BYTES;
}
template <typename T>
inline constexpr const byte* operator+=(const byte* ptr, UnitMeasure<T, byte> offset) {
  return ptr = ptr + offset / BYTES;
}

template <typename T>
inline constexpr word* operator+(word* ptr, UnitMeasure<T, word> offset) {
  return ptr + offset / WORDS;
}
template <typename T>
inline constexpr const word* operator+(const word* ptr, UnitMeasure<T, word> offset) {
  return ptr + offset / WORDS;
}
template <typename T>
inline constexpr word* operator+=(word*& ptr, UnitMeasure<T, word> offset) {
  return ptr = ptr + offset / WORDS;
}
template <typename T>
inline constexpr const word* operator+=(const word*& ptr, UnitMeasure<T, word> offset) {
  return ptr = ptr + offset / WORDS;
}

template <typename T>
inline constexpr byte* operator-(byte* ptr, UnitMeasure<T, byte> offset) {
  return ptr - offset / BYTES;
}
template <typename T>
inline constexpr const byte* operator-(const byte* ptr, UnitMeasure<T, byte> offset) {
  return ptr - offset / BYTES;
}
template <typename T>
inline constexpr byte* operator-=(byte*& ptr, UnitMeasure<T, byte> offset) {
  return ptr = ptr - offset / BYTES;
}
template <typename T>
inline constexpr const byte* operator-=(const byte* ptr, UnitMeasure<T, byte> offset) {
  return ptr = ptr - offset / BYTES;
}

template <typename T>
inline constexpr word* operator-(word* ptr, UnitMeasure<T, word> offset) {
  return ptr - offset / WORDS;
}
template <typename T>
inline constexpr const word* operator-(const word* ptr, UnitMeasure<T, word> offset) {
  return ptr - offset / WORDS;
}
template <typename T>
inline constexpr word* operator-=(word*& ptr, UnitMeasure<T, word> offset) {
  return ptr = ptr - offset / WORDS;
}
template <typename T>
inline constexpr const word* operator-=(const word*& ptr, UnitMeasure<T, word> offset) {
  return ptr = ptr - offset / WORDS;
}

#endif

inline constexpr ByteCount intervalLength(const byte* a, const byte* b) {
  return uint(b - a) * BYTES;
}
inline constexpr WordCount intervalLength(const word* a, const word* b) {
  return uint(b - a) * WORDS;
}

namespace internal {

#define CAPNPROTO_EXPECT_TRUE(condition) __builtin_expect(condition, true)
#define CAPNPROTO_EXPECT_FALSE(condition) __builtin_expect(condition, false)
// Branch prediction macros.  Evaluates to the condition given, but also tells the compiler that we
// expect the condition to be true/false enough of the time that it's worth hard-coding branch
// prediction.

#define CAPNPROTO_ALWAYS_INLINE(prototype) inline prototype __attribute__((always_inline))
// Force a function to always be inlined.  Apply only to the prototype, not to the definition.

void assertionFailure(const char* file, int line, const char* expectation, const char* message)
    __attribute__((noreturn));

// CAPNPROTO_ASSERT is just like assert() except it avoids polluting the global namespace with an
// unqualified macro name and it throws an exception (derived from std::exception).
#ifdef NDEBUG
#define CAPNPROTO_DEBUG_ASSERT(condition, message)
#else
#define CAPNPROTO_DEBUG_ASSERT(condition, message) \
  if (CAPNPROTO_EXPECT_TRUE(condition)); else ::capnproto::internal::assertionFailure(\
      __FILE__, __LINE__, #condition, message)
#endif

#define CAPNPROTO_ASSERT(condition, message) \
    ::capnproto::internal::assertionFailure(__FILE__, __LINE__, #condition, message)

}  // namespace internal

template <typename T, typename U>
T implicit_cast(U u) {
  return u;
}

}  // namespace capnproto

#endif  // CAPNPROTO_MACROS_H_
