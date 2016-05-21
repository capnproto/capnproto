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

// This file contains types which are intended to help detect incorrect usage at compile
// time, but should then be optimized down to basic primitives (usually, integers) by the
// compiler.

#ifndef CAPNP_COMMON_H_
#define CAPNP_COMMON_H_

#if defined(__GNUC__) && !defined(CAPNP_HEADER_WARNINGS)
#pragma GCC system_header
#endif

#include <kj/units.h>
#include <inttypes.h>
#include <kj/string.h>
#include <kj/memory.h>

namespace capnp {

#define CAPNP_VERSION_MAJOR 0
#define CAPNP_VERSION_MINOR 6
#define CAPNP_VERSION_MICRO 0

#define CAPNP_VERSION \
  (CAPNP_VERSION_MAJOR * 1000000 + CAPNP_VERSION_MINOR * 1000 + CAPNP_VERSION_MICRO)

#ifdef _MSC_VER
#define CAPNP_LITE 1
// MSVC only supports "lite" mode for now, due to missing C++11 features.
#endif

#ifndef CAPNP_LITE
#define CAPNP_LITE 0
#endif

typedef unsigned int uint;

struct Void {
  // Type used for Void fields.  Using C++'s "void" type creates a bunch of issues since it behaves
  // differently from other types.

  inline constexpr bool operator==(Void other) const { return true; }
  inline constexpr bool operator!=(Void other) const { return false; }
};

static constexpr Void VOID = Void();
// Constant value for `Void`,  which is an empty struct.

inline kj::StringPtr KJ_STRINGIFY(Void) { return "void"; }

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

  OTHER
  // Some other type which is often a type parameter to Cap'n Proto templates, but which needs
  // special handling. This includes types like AnyPointer, Dynamic*, etc.
};

enum class Style: uint8_t {
  PRIMITIVE,
  POINTER,      // other than struct
  STRUCT,
  CAPABILITY
};

enum class ElementSize: uint8_t {
  // Size of a list element.

  VOID = 0,
  BIT = 1,
  BYTE = 2,
  TWO_BYTES = 3,
  FOUR_BYTES = 4,
  EIGHT_BYTES = 5,

  POINTER = 6,

  INLINE_COMPOSITE = 7
};

enum class PointerType {
  // Various wire types a pointer field can take

  NULL_,
  // Should be NULL, but that's #defined in stddef.h

  STRUCT,
  LIST,
  CAPABILITY
};

namespace schemas {

template <typename T>
struct EnumInfo;

}  // namespace schemas

namespace _ {  // private

template <typename T, typename = void> struct Kind_;

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

template <typename T> struct Kind_<T, kj::VoidSfinae<typename T::_capnpPrivate::IsStruct>> {
  static constexpr Kind kind = Kind::STRUCT;
};
template <typename T> struct Kind_<T, kj::VoidSfinae<typename T::_capnpPrivate::IsInterface>> {
  static constexpr Kind kind = Kind::INTERFACE;
};
template <typename T> struct Kind_<T, kj::VoidSfinae<typename schemas::EnumInfo<T>::IsEnum>> {
  static constexpr Kind kind = Kind::ENUM;
};

}  // namespace _ (private)

template <typename T, Kind k = _::Kind_<T>::kind>
inline constexpr Kind kind() {
  // This overload of kind() matches types which have a Kind_ specialization.

  return k;
}

#if CAPNP_LITE

#define CAPNP_KIND(T) ::capnp::_::Kind_<T>::kind
// Avoid constexpr methods in lite mode (MSVC is bad at constexpr).

#else  // CAPNP_LITE

#define CAPNP_KIND(T) ::capnp::kind<T>()
// Use this macro rather than kind<T>() in any code which must work in lite mode.

template <typename T, Kind k = kind<T>()>
inline constexpr Style style() {
  return k == Kind::PRIMITIVE || k == Kind::ENUM ? Style::PRIMITIVE
       : k == Kind::STRUCT ? Style::STRUCT
       : k == Kind::INTERFACE ? Style::CAPABILITY : Style::POINTER;
}

#endif  // CAPNP_LITE, else

template <typename T, Kind k = CAPNP_KIND(T)>
struct List;

#if _MSC_VER

template <typename T, Kind k>
struct List {};
// For some reason, without this declaration, MSVC will error out on some uses of List
// claiming that "T" -- as used in the default initializer for the second template param, "k" --
// is not defined. I do not understand this error, but adding this empty default declaration fixes
// it.

#endif

template <typename T> struct ListElementType_;
template <typename T> struct ListElementType_<List<T>> { typedef T Type; };
template <typename T> using ListElementType = typename ListElementType_<T>::Type;

namespace _ {  // private
template <typename T, Kind k> struct Kind_<List<T, k>> {
  static constexpr Kind kind = Kind::LIST;
};
}  // namespace _ (private)

template <typename T, Kind k = CAPNP_KIND(T)> struct ReaderFor_ { typedef typename T::Reader Type; };
template <typename T> struct ReaderFor_<T, Kind::PRIMITIVE> { typedef T Type; };
template <typename T> struct ReaderFor_<T, Kind::ENUM> { typedef T Type; };
template <typename T> struct ReaderFor_<T, Kind::INTERFACE> { typedef typename T::Client Type; };
template <typename T> using ReaderFor = typename ReaderFor_<T>::Type;
// The type returned by List<T>::Reader::operator[].

template <typename T, Kind k = CAPNP_KIND(T)> struct BuilderFor_ { typedef typename T::Builder Type; };
template <typename T> struct BuilderFor_<T, Kind::PRIMITIVE> { typedef T Type; };
template <typename T> struct BuilderFor_<T, Kind::ENUM> { typedef T Type; };
template <typename T> struct BuilderFor_<T, Kind::INTERFACE> { typedef typename T::Client Type; };
template <typename T> using BuilderFor = typename BuilderFor_<T>::Type;
// The type returned by List<T>::Builder::operator[].

template <typename T, Kind k = CAPNP_KIND(T)> struct PipelineFor_ { typedef typename T::Pipeline Type;};
template <typename T> struct PipelineFor_<T, Kind::INTERFACE> { typedef typename T::Client Type; };
template <typename T> using PipelineFor = typename PipelineFor_<T>::Type;

template <typename T, Kind k = CAPNP_KIND(T)> struct TypeIfEnum_;
template <typename T> struct TypeIfEnum_<T, Kind::ENUM> { typedef T Type; };

template <typename T>
using TypeIfEnum = typename TypeIfEnum_<kj::Decay<T>>::Type;

template <typename T>
using FromReader = typename kj::Decay<T>::Reads;
// FromReader<MyType::Reader> = MyType (for any Cap'n Proto type).

template <typename T>
using FromBuilder = typename kj::Decay<T>::Builds;
// FromBuilder<MyType::Builder> = MyType (for any Cap'n Proto type).

template <typename T>
using FromPipeline = typename kj::Decay<T>::Pipelines;
// FromBuilder<MyType::Pipeline> = MyType (for any Cap'n Proto type).

template <typename T>
using FromClient = typename kj::Decay<T>::Calls;
// FromReader<MyType::Client> = MyType (for any Cap'n Proto interface type).

template <typename T>
using FromServer = typename kj::Decay<T>::Serves;
// FromBuilder<MyType::Server> = MyType (for any Cap'n Proto interface type).

template <typename T, typename = void>
struct FromAny_;

template <typename T>
struct FromAny_<T, kj::VoidSfinae<FromReader<T>>> {
  using Type = FromReader<T>;
};

template <typename T>
struct FromAny_<T, kj::VoidSfinae<FromBuilder<T>>> {
  using Type = FromBuilder<T>;
};

template <typename T>
struct FromAny_<T, kj::VoidSfinae<FromPipeline<T>>> {
  using Type = FromPipeline<T>;
};

// Note that T::Client is covered by FromReader

template <typename T>
struct FromAny_<kj::Own<T>, kj::VoidSfinae<FromServer<T>>> {
  using Type = FromServer<T>;
};

template <typename T>
struct FromAny_<T,
    kj::EnableIf<_::Kind_<T>::kind == Kind::PRIMITIVE || _::Kind_<T>::kind == Kind::ENUM>> {
  // TODO(msvc): Ideally the EnableIf condition would be `style<T>() == Style::PRIMITIVE`, but MSVC
  // cannot yet use style<T>() in this constexpr context.

  using Type = kj::Decay<T>;
};

template <typename T>
using FromAny = typename FromAny_<T>::Type;
// Given any Cap'n Proto value type as an input, return the Cap'n Proto base type. That is:
//
//     Foo::Reader -> Foo
//     Foo::Builder -> Foo
//     Foo::Pipeline -> Foo
//     Foo::Client -> Foo
//     Own<Foo::Server> -> Foo
//     uint32_t -> uint32_t

namespace _ {  // private

template <typename T, Kind k = CAPNP_KIND(T)>
struct PointerHelpers;

#if _MSC_VER

template <typename T, Kind k>
struct PointerHelpers {};
// For some reason, without this declaration, MSVC will error out on some uses of PointerHelpers
// claiming that "T" -- as used in the default initializer for the second template param, "k" --
// is not defined. I do not understand this error, but adding this empty default declaration fixes
// it.

#endif

}  // namespace _ (private)

struct MessageSize {
  // Size of a message.  Every struct type has a method `.totalSize()` that returns this.
  uint64_t wordCount;
  uint capCount;
};

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

// GCC 4.7 actually gives unused warnings on these constants in opt mode...
constexpr auto BITS_PER_BYTE KJ_UNUSED = 8 * BITS / BYTES;
constexpr auto BITS_PER_WORD KJ_UNUSED = 64 * BITS / WORDS;
constexpr auto BYTES_PER_WORD KJ_UNUSED = 8 * BYTES / WORDS;

constexpr auto BITS_PER_POINTER KJ_UNUSED = 64 * BITS / POINTERS;
constexpr auto BYTES_PER_POINTER KJ_UNUSED = 8 * BYTES / POINTERS;
constexpr auto WORDS_PER_POINTER KJ_UNUSED = 1 * WORDS / POINTERS;

constexpr WordCount POINTER_SIZE_IN_WORDS = 1 * POINTERS * WORDS_PER_POINTER;

template <typename T>
inline KJ_CONSTEXPR() decltype(BYTES / ELEMENTS) bytesPerElement() {
  return sizeof(T) * BYTES / ELEMENTS;
}

template <typename T>
inline KJ_CONSTEXPR() decltype(BITS / ELEMENTS) bitsPerElement() {
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
