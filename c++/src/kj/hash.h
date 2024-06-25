// Copyright (c) 2018 Kenton Varda and contributors
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

#pragma once

#include "string.h"
#include <stdint.h>

KJ_BEGIN_HEADER

namespace kj {
namespace _ {  // private

inline uint intHash32(uint32_t i);
inline uint intHash64(uint64_t i);

struct HashCoder {
  // This is a dummy type with only one instance: HASHCODER (below).  To make an arbitrary type
  // hashable, define `operator*(HashCoder, T)` to return any other type that is already hashable.
  // Be sure to declare the operator in the same namespace as `T` **or** in the global scope.
  // You can use the KJ_HASHCODE() macro as syntax sugar for this.
  //
  // A more usual way to accomplish what we're doing here would be to require that you define
  // a function like `hashCode(T)` and then rely on argument-dependent lookup.  However, this has
  // the problem that it pollutes other people's namespaces and even the global namespace.  For
  // example, some other project may already have functions called `hashCode` which do something
  // different.  Declaring `operator*` with `HashCoder` as the left operand cannot conflict with
  // anything.

  uint operator*(ArrayPtr<const byte> s) const;
  inline uint operator*(ArrayPtr<byte> s) const { return operator*(s.asConst()); }

  inline uint operator*(ArrayPtr<const char> s) const { return operator*(s.asBytes()); }
  inline uint operator*(ArrayPtr<char> s) const { return operator*(s.asBytes()); }
  inline uint operator*(const Array<const char>& s) const { return operator*(s.asBytes()); }
  inline uint operator*(const Array<char>& s) const { return operator*(s.asBytes()); }
  inline uint operator*(const String& s) const { return operator*(s.asBytes()); }
  inline uint operator*(const StringPtr& s) const { return operator*(s.asBytes()); }
  inline uint operator*(const ConstString& s) const { return operator*(s.asBytes()); }

  inline uint operator*(decltype(nullptr)) const { return 0; }
  inline uint operator*(bool b) const { return b; }
  inline uint operator*(char i) const { return intHash32(i); }
  inline uint operator*(signed char i) const { return intHash32(i); }
  inline uint operator*(unsigned char i) const { return intHash32(i); }
  inline uint operator*(signed short i) const { return intHash32(i); }
  inline uint operator*(unsigned short i) const { return intHash32(i); }
  inline uint operator*(signed int i) const { return intHash32(i); }
  inline uint operator*(unsigned int i) const { return intHash32(i); }

  inline uint operator*(signed long i) const {
    if constexpr (sizeof(i) == sizeof(uint32_t)) {
      return intHash32(i);
    } else {
      return intHash64(i);
    }
  }
  inline uint operator*(unsigned long i) const {
    if constexpr (sizeof(i) == sizeof(uint32_t)) {
      return intHash32(i);
    } else {
      return intHash64(i);
    }
  }
  inline uint operator*(signed long long i) const {
    return intHash64(i);
  }
  inline uint operator*(unsigned long long i) const {
    return intHash64(i);
  }

  template <typename T>
  uint operator*(T* ptr) const {
    static_assert(!isSameType<Decay<T>, char>(), "Wrap in StringPtr if you want to hash string "
        "contents. If you want to hash the pointer, cast to void*");
    if constexpr (sizeof(ptr) == sizeof(uint32_t)) {
      return intHash32(reinterpret_cast<uint32_t>(ptr));
    } else {
      return intHash64(reinterpret_cast<uint64_t>(ptr));
    }
  }

  template <typename T>
  uint operator*(const Own<T>& ptr) const {
    return operator*(ptr.get());
  }

  template <typename T, typename = decltype(instance<const HashCoder&>() * instance<const T&>())>
  uint operator*(ArrayPtr<T> arr) const;
  template <typename T, typename = decltype(instance<const HashCoder&>() * instance<const T&>())>
  uint operator*(const Array<T>& arr) const;
  template <typename T, typename = EnableIf<__is_enum(T)>>
  inline uint operator*(T e) const;

  template <typename T, typename Result = decltype(instance<T>().hashCode())>
  inline Result operator*(T&& value) const { return kj::fwd<T>(value).hashCode(); }
};
static KJ_CONSTEXPR(const) HashCoder HASHCODER = HashCoder();

}  // namespace _ (private)

#define KJ_HASHCODE(...) operator*(::kj::_::HashCoder, __VA_ARGS__)
// Defines a hash function for a custom type.  Example:
//
//    class Foo {...};
//    inline uint KJ_HASHCODE(const Foo& foo) { return kj::hashCode(foo.x, foo.y); }
//
// This allows Foo to be passed to hashCode().
//
// The function should be declared either in the same namespace as the target type or in the global
// namespace. It can return any type which itself is hashable -- that value will be hashed in turn
// until a `uint` comes out.
//
// Hash code functions MUST produce hashes that are uniform even in the low-order bits. That is,
// if the caller keeps only the lowest-order N bits of each hash, they will still find hash outputs
// are uniform within those bits. This is important because it allows kj::HashMap to use
// power-of-two hashtable sizes, using only the lower bits of the hash to choose the appropriate
// bucket. In particular this means that, for example, to hash a pointer value, it is not
// sufficient to simply reinterpret it as an integer, becaues pointers are typically aligned and
// so the bottom 2-3 bits are always zero, and also allocation patterns could cause other bits
// to be highly correlated.
//
// The easiest way to satisfy the above is to always write your hash function in terms of calling
// `kj::hashCode()` on some other value, e.g.:
//
//     KJ_HASHCODE(MyType x) { return kj::hashCode(x.key); }
//
// TODO(someday): We should define a type `kj::HashCode` which wraps `uint`, then require all
//   `hashCode()` implementations to return this type. Hash implementations which just forward
//   back to `kj::hashCode()` on an inner value trivially satisfy this. Anything else will need
//   to call a specific API to construct a `HashCode` value, like
//   `kj::HashCode::fromUniformInteger(hash)`, which promises that the value is already uniform.

template <typename T>
inline uint hashCode(T&& value) { return _::HASHCODER * kj::fwd<T>(value); }
template <typename T, size_t N>
inline uint hashCode(T (&arr)[N]) {
  static_assert(!isSameType<Decay<T>, char>(), "Wrap in StringPtr if you want to hash string "
      "contents. If you want to hash the pointer, cast to void*");
  static_assert(isSameType<Decay<T>, char>(), "Wrap in ArrayPtr if you want to hash a C array. "
      "If you want to hash the pointer, cast to void*");
  // At least one of the two static_asserts above always fails, so this won't be used.
  return 0;
}
template <typename... T>
inline uint hashCode(T&&... values) {
  uint hashes[] = { hashCode(kj::fwd<T>(values))... };
  return hashCode(kj::ArrayPtr<uint>(hashes).asBytes());
}
// kj::hashCode() is a universal hashing function, like kj::str() is a universal stringification
// function. Throw stuff in, get a hash code.
//
// Hash codes may differ between different processes, even running exactly the same code.
//
// NOT SUITABLE FOR CRYPTOGRAPHY. This is for hash tables, not crypto.

// =======================================================================================
// inline implementation details

namespace _ {  // private

template <typename T, typename>
inline uint HashCoder::operator*(ArrayPtr<T> arr) const {
  // Hash each array element to create a string of hashes, then murmur2 over those.
  //
  // TODO(perf): Choose a more-modern hash. (See hash.c++.)

  constexpr uint m = 0x5bd1e995;
  constexpr uint r = 24;
  uint h = arr.size() * sizeof(uint);

  for (auto& e: arr) {
    uint k = kj::hashCode(e);
    k *= m;
    k ^= k >> r;
    k *= m;
    h *= m;
    h ^= k;
  }

  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;
  return h;
}
template <typename T, typename>
inline uint HashCoder::operator*(const Array<T>& arr) const {
  return operator*(arr.asPtr());
}

template <typename T, typename>
inline uint HashCoder::operator*(T e) const {
  return operator*(static_cast<__underlying_type(T)>(e));
}

inline uint intHash32(uint32_t i) {
  // Basic 32-bit integer hash function.
  //
  // This hash function needs to have a uniform distribution, such that changing any bits in the
  // input tends to randomly change ~half the bits in the output. In particular, it is important
  // that a change to the high bits of the input will affect the low bits of the output, so that
  // even if we truncate the high bits of the output, the lower bits still have a uniform
  // distribution.
  //
  // The point of all this is that kj::HashMap uses power-of-two-sized tables, and we want to make
  // sure maps with integer keys hash well into those tables.

  // On architectures with a hardware CRC32 instruction, use it. Otherwise fall back to a
  // reasonable shifty hash.
#if __CRC32__
  return __builtin_ia32_crc32si(0, i);
#elif __ARM_FEATURE_CRC32
  return __builtin_arm_crc32w(0, i);
#else
  // Thomas Wang 32 bit integer hash function from https://gist.github.com/badboy/6267743
  // This page says it's public domain: http://burtleburtle.net/bob/hash/integer.html
  i = ~i + (i << 15); // i = (i << 15) - i - 1;
  i = i ^ (i >> 12);
  i = i + (i << 2);
  i = i ^ (i >> 4);
  i = i * 2057; // i = (i + (i << 3)) + (i << 11);
  i = i ^ (i >> 16);
  return i;
#endif
}

inline uint intHash64(uint64_t i) {
  // Basic 64-bit integer hash function.
  //
  // Like intHash32(), but where the input is 64 bits.

  // It's important that if the actual value is in the 32-bit range, the hash code is consistent
  // with a 32-bit integer. Otherwise, if you have, say, a `HashMap<uint64_t, T>` and you write
  // `map.find(1)` it won't work, because `1` is type `int` and will be hashed as a 32-bit integer.
  if (i <= UINT32_MAX) {
    return intHash32(i);
  }

#if __CRC32__
  return __builtin_ia32_crc32di(0, i);
#elif __ARM_FEATURE_CRC32
  return __builtin_arm_crc32d(0, i);
#else
  // Thomas Wang hash6432shift() from https://gist.github.com/badboy/6267743
  // This page says it's public domain (inthash.c):
  //     https://github.com/markokr/pghashlib/blob/master/COPYRIGHT
  i = (~i) + (i << 18);
  i = i ^ (i >> 31);
  i = i * 21;
  i = i ^ (i >> 11);
  i = i + (i << 6);
  i = i ^ (i >> 22);
  return i;
#endif
}

}  // namespace _ (private)
} // namespace kj

KJ_END_HEADER
