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

// This file contains types which are intended to help detect incorrect usage at compile
// time, but should then be optimized down to basic primitives (usually, integers) by the
// compiler.

#ifndef CAPNP_COMMON_H_
#define CAPNP_COMMON_H_

#include <kj/units.h>
#include <inttypes.h>

namespace capnp {

typedef unsigned int uint;

enum class Void {
  // Type used for Void fields.  There is only one value.  Using C++'s "void" type creates a bunch
  // of issues since it behaves differently from other types.
  VOID
};
template <typename T>
inline T& operator<<(T& os, Void) { return os << "void"; }

struct Text;
struct Data;

enum class Kind: uint8_t {
  PRIMITIVE,
  BLOB,
  ENUM,
  STRUCT,
  UNION,
  INTERFACE,
  LIST,
  UNKNOWN
};

namespace _ {  // private

template <typename T> struct Kind_ { static constexpr Kind kind = Kind::UNKNOWN; };

template <> struct Kind_<Void> { static constexpr Kind kind = Kind::PRIMITIVE; };
template <> struct Kind_<bool> { static constexpr Kind kind = Kind::PRIMITIVE; };
template <> struct Kind_<int8_t> { static constexpr Kind kind = Kind::PRIMITIVE; };
template <> struct Kind_<int16_t> { static constexpr Kind kind = Kind::PRIMITIVE; };
template <> struct Kind_<int32_t> { static constexpr Kind kind = Kind::PRIMITIVE; };
template <> struct Kind_<int64_t> { static constexpr Kind kind = Kind::PRIMITIVE; };
template <> struct Kind_<uint8_t> { static constexpr Kind kind = Kind::PRIMITIVE; };
template <> struct Kind_<uint16_t> { static constexpr Kind kind = Kind::PRIMITIVE; };
template <> struct Kind_<uint32_t> { static constexpr Kind kind = Kind::PRIMITIVE; };
template <> struct Kind_<uint64_t> { static constexpr Kind kind = Kind::PRIMITIVE; };
template <> struct Kind_<float> { static constexpr Kind kind = Kind::PRIMITIVE; };
template <> struct Kind_<double> { static constexpr Kind kind = Kind::PRIMITIVE; };
template <> struct Kind_<Text> { static constexpr Kind kind = Kind::BLOB; };
template <> struct Kind_<Data> { static constexpr Kind kind = Kind::BLOB; };

}  // namespace _ (private)

template <typename T>
inline constexpr Kind kind() {
  return _::Kind_<T>::kind;
}

template <typename T, Kind k = kind<T>()>
struct List;

namespace _ {  // private
template <typename T, Kind k> struct Kind_<List<T, k>> { static constexpr Kind kind = Kind::LIST; };
}  // namespace _ (private)

template <typename T, Kind k = kind<T>()> struct ReaderFor_ { typedef typename T::Reader Type; };
template <typename T> struct ReaderFor_<T, Kind::PRIMITIVE> { typedef T Type; };
template <typename T> struct ReaderFor_<T, Kind::ENUM> { typedef T Type; };
template <typename T> using ReaderFor = typename ReaderFor_<T>::Type;
// The type returned by List<T>::Reader::operator[].

template <typename T, Kind k = kind<T>()> struct BuilderFor_ { typedef typename T::Builder Type; };
template <typename T> struct BuilderFor_<T, Kind::PRIMITIVE> { typedef T Type; };
template <typename T> struct BuilderFor_<T, Kind::ENUM> { typedef T Type; };
template <typename T> using BuilderFor = typename BuilderFor_<T>::Type;
// The type returned by List<T>::Builder::operator[].

template <typename T, Kind k = kind<T>()> struct TypeIfEnum_;
template <typename T> struct TypeIfEnum_<T, Kind::ENUM> { typedef T Type; };

template <typename T>
using TypeIfEnum = typename TypeIfEnum_<kj::Decay<T>>::Type;

template <typename T>
using FromReader = typename kj::Decay<T>::Reads;
// FromReader<MyType::Reader> = MyType (for any Cap'n Proto type).

template <typename T>
using FromBuilder = typename kj::Decay<T>::Builds;
// FromBuilder<MyType::Builder> = MyType (for any Cap'n Proto type).

namespace _ {  // private
template <typename T, Kind k = kind<T>()>
struct PointerHelpers;
}  // namespace _ (private)

// =======================================================================================
// Raw memory types and measures

using kj::byte;

class word { uint64_t content KJ_UNUSED_MEMBER; KJ_DISALLOW_COPY(word); public: word() = default; };
// word is an opaque type with size of 64 bits.  This type is useful only to make pointer
// arithmetic clearer.  Since the contents are private, the only way to access them is to first
// reinterpret_cast to some other pointer type.
//
// Copying is disallowed because you should always use memcpy().  Otherwise, you may run afoul of
// aliasing rules.
//
// A pointer of type word* should always be word-aligned even if won't actually be dereferenced as
// that type.

static_assert(sizeof(byte) == 1, "uint8_t is not one byte?");
static_assert(sizeof(word) == 8, "uint64_t is not 8 bytes?");

#if CAPNP_DEBUG_TYPES
// Set CAPNP_DEBUG_TYPES to 1 to use kj::Quantity for "count" types.  Otherwise, plain integers are
// used.  All the code should still operate exactly the same, we just lose compile-time checking.
// Note that this will also change symbol names, so it's important that the library and any clients
// be compiled with the same setting here.
//
// We disable this by default to reduce symbol name size and avoid any possibility of the compiler
// failing to fully-optimize the types, but anyone modifying Cap'n Proto itself should enable this
// during development and testing.

namespace _ { class BitLabel; class ElementLabel; struct WirePointer; }

typedef kj::Quantity<uint, _::BitLabel> BitCount;
typedef kj::Quantity<uint8_t, _::BitLabel> BitCount8;
typedef kj::Quantity<uint16_t, _::BitLabel> BitCount16;
typedef kj::Quantity<uint32_t, _::BitLabel> BitCount32;
typedef kj::Quantity<uint64_t, _::BitLabel> BitCount64;

typedef kj::Quantity<uint, byte> ByteCount;
typedef kj::Quantity<uint8_t, byte> ByteCount8;
typedef kj::Quantity<uint16_t, byte> ByteCount16;
typedef kj::Quantity<uint32_t, byte> ByteCount32;
typedef kj::Quantity<uint64_t, byte> ByteCount64;

typedef kj::Quantity<uint, word> WordCount;
typedef kj::Quantity<uint8_t, word> WordCount8;
typedef kj::Quantity<uint16_t, word> WordCount16;
typedef kj::Quantity<uint32_t, word> WordCount32;
typedef kj::Quantity<uint64_t, word> WordCount64;

typedef kj::Quantity<uint, _::ElementLabel> ElementCount;
typedef kj::Quantity<uint8_t, _::ElementLabel> ElementCount8;
typedef kj::Quantity<uint16_t, _::ElementLabel> ElementCount16;
typedef kj::Quantity<uint32_t, _::ElementLabel> ElementCount32;
typedef kj::Quantity<uint64_t, _::ElementLabel> ElementCount64;

typedef kj::Quantity<uint, _::WirePointer> WirePointerCount;
typedef kj::Quantity<uint8_t, _::WirePointer> WirePointerCount8;
typedef kj::Quantity<uint16_t, _::WirePointer> WirePointerCount16;
typedef kj::Quantity<uint32_t, _::WirePointer> WirePointerCount32;
typedef kj::Quantity<uint64_t, _::WirePointer> WirePointerCount64;

template <typename T, typename U>
inline constexpr U* operator+(U* ptr, kj::Quantity<T, U> offset) {
  return ptr + offset / kj::unit<kj::Quantity<T, U>>();
}
template <typename T, typename U>
inline constexpr const U* operator+(const U* ptr, kj::Quantity<T, U> offset) {
  return ptr + offset / kj::unit<kj::Quantity<T, U>>();
}
template <typename T, typename U>
inline constexpr U* operator+=(U*& ptr, kj::Quantity<T, U> offset) {
  return ptr = ptr + offset / kj::unit<kj::Quantity<T, U>>();
}
template <typename T, typename U>
inline constexpr const U* operator+=(const U*& ptr, kj::Quantity<T, U> offset) {
  return ptr = ptr + offset / kj::unit<kj::Quantity<T, U>>();
}

template <typename T, typename U>
inline constexpr U* operator-(U* ptr, kj::Quantity<T, U> offset) {
  return ptr - offset / kj::unit<kj::Quantity<T, U>>();
}
template <typename T, typename U>
inline constexpr const U* operator-(const U* ptr, kj::Quantity<T, U> offset) {
  return ptr - offset / kj::unit<kj::Quantity<T, U>>();
}
template <typename T, typename U>
inline constexpr U* operator-=(U*& ptr, kj::Quantity<T, U> offset) {
  return ptr = ptr - offset / kj::unit<kj::Quantity<T, U>>();
}
template <typename T, typename U>
inline constexpr const U* operator-=(const U*& ptr, kj::Quantity<T, U> offset) {
  return ptr = ptr - offset / kj::unit<kj::Quantity<T, U>>();
}

#else

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

typedef uint ElementCount;
typedef uint8_t ElementCount8;
typedef uint16_t ElementCount16;
typedef uint32_t ElementCount32;
typedef uint64_t ElementCount64;

typedef uint WirePointerCount;
typedef uint8_t WirePointerCount8;
typedef uint16_t WirePointerCount16;
typedef uint32_t WirePointerCount32;
typedef uint64_t WirePointerCount64;

#endif

constexpr BitCount BITS = kj::unit<BitCount>();
constexpr ByteCount BYTES = kj::unit<ByteCount>();
constexpr WordCount WORDS = kj::unit<WordCount>();
constexpr ElementCount ELEMENTS = kj::unit<ElementCount>();
constexpr WirePointerCount POINTERS = kj::unit<WirePointerCount>();

constexpr auto BITS_PER_BYTE = 8 * BITS / BYTES;
constexpr auto BITS_PER_WORD = 64 * BITS / WORDS;
constexpr auto BYTES_PER_WORD = 8 * BYTES / WORDS;

constexpr auto BITS_PER_POINTER = 64 * BITS / POINTERS;
constexpr auto BYTES_PER_POINTER = 8 * BYTES / POINTERS;
constexpr auto WORDS_PER_POINTER = 1 * WORDS / POINTERS;

constexpr WordCount POINTER_SIZE_IN_WORDS = 1 * POINTERS * WORDS_PER_POINTER;

template <typename T>
inline constexpr decltype(BYTES / ELEMENTS) bytesPerElement() {
  return sizeof(T) * BYTES / ELEMENTS;
}

template <typename T>
inline constexpr decltype(BITS / ELEMENTS) bitsPerElement() {
  return sizeof(T) * 8 * BITS / ELEMENTS;
}

inline constexpr ByteCount intervalLength(const byte* a, const byte* b) {
  return uint(b - a) * BYTES;
}
inline constexpr WordCount intervalLength(const word* a, const word* b) {
  return uint(b - a) * WORDS;
}

}  // namespace capnp

#endif  // CAPNP_COMMON_H_
