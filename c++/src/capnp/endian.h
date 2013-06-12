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

#ifndef CAPNP_ENDIAN_H_
#define CAPNP_ENDIAN_H_

#include "common.h"
#include <inttypes.h>
#include <string.h>  // memcpy

namespace capnp {
namespace _ {  // private

// WireValue
//
// Wraps a primitive value as it appears on the wire.  Namely, values are little-endian on the
// wire, because little-endian is the most common endianness in modern CPUs.
//
// Note:  In general, code that depends cares about byte ordering is bad.  See:
//     http://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html
//   Cap'n Proto is special because it is essentially doing compiler-like things, fussing over
//   allocation and layout of memory, in order to squeeze out every last drop of performance.

#if CAPNP_REVERSE_ENDIAN
#define CAPNP_WIRE_BYTE_ORDER __ORDER_BIG_ENDIAN__
#define CAPNP_OPPOSITE_OF_WIRE_BYTE_ORDER __ORDER_LITTLE_ENDIAN__
#else
#define CAPNP_WIRE_BYTE_ORDER __ORDER_LITTLE_ENDIAN__
#define CAPNP_OPPOSITE_OF_WIRE_BYTE_ORDER __ORDER_BIG_ENDIAN__
#endif

#if defined(__BYTE_ORDER__) && \
    __BYTE_ORDER__ == CAPNP_WIRE_BYTE_ORDER && \
    !CAPNP_DISABLE_ENDIAN_DETECTION
// CPU is little-endian.  We can just read/write the memory directly.

template <typename T>
class DirectWireValue {
public:
  KJ_ALWAYS_INLINE(T get() const) { return value; }
  KJ_ALWAYS_INLINE(void set(T newValue)) { value = newValue; }

private:
  T value;
};

template <typename T>
using WireValue = DirectWireValue<T>;
// To prevent ODR problems when endian-test, endian-reverse-test, and endian-fallback-test are
// linked together, we define each implementation with a different name and define an alias to the
// one we want to use.

#elif defined(__BYTE_ORDER__) && \
      __BYTE_ORDER__ == CAPNP_OPPOSITE_OF_WIRE_BYTE_ORDER && \
      defined(__GNUC__) && !CAPNP_DISABLE_ENDIAN_DETECTION
// Big-endian, but GCC's __builtin_bswap() is available.

// TODO(perf):  Use dedicated instructions to read little-endian data on big-endian CPUs that have
//   them.

// TODO(perf):  Verify that this code optimizes reasonably.  In particular, ensure that the
//   compiler optimizes away the memcpy()s and keeps everything in registers.

template <typename T, size_t size = sizeof(T)>
class SwappingWireValue;

template <typename T>
class SwappingWireValue<T, 1> {
public:
  KJ_ALWAYS_INLINE(T get() const) { return value; }
  KJ_ALWAYS_INLINE(void set(T newValue)) { value = newValue; }

private:
  T value;
};

template <typename T>
class SwappingWireValue<T, 2> {
public:
  KJ_ALWAYS_INLINE(T get() const) {
    uint16_t swapped = __builtin_bswap16(value);
    T result;
    memcpy(&result, &swapped, sizeof(T));
    return result;
  }
  KJ_ALWAYS_INLINE(void set(T newValue)) {
    uint16_t raw;
    memcpy(&raw, &newValue, sizeof(T));
    value = __builtin_bswap16(raw);
  }

private:
  uint16_t value;
};

template <typename T>
class SwappingWireValue<T, 4> {
public:
  KJ_ALWAYS_INLINE(T get() const) {
    uint32_t swapped = __builtin_bswap32(value);
    T result;
    memcpy(&result, &swapped, sizeof(T));
    return result;
  }
  KJ_ALWAYS_INLINE(void set(T newValue)) {
    uint32_t raw;
    memcpy(&raw, &newValue, sizeof(T));
    value = __builtin_bswap32(raw);
  }

private:
  uint32_t value;
};

template <typename T>
class SwappingWireValue<T, 8> {
public:
  KJ_ALWAYS_INLINE(T get() const) {
    uint64_t swapped = __builtin_bswap64(value);
    T result;
    memcpy(&result, &swapped, sizeof(T));
    return result;
  }
  KJ_ALWAYS_INLINE(void set(T newValue)) {
    uint64_t raw;
    memcpy(&raw, &newValue, sizeof(T));
    value = __builtin_bswap64(raw);
  }

private:
  uint64_t value;
};

template <typename T>
using WireValue = SwappingWireValue<T>;
// To prevent ODR problems when endian-test, endian-reverse-test, and endian-fallback-test are
// linked together, we define each implementation with a different name and define an alias to the
// one we want to use.

#else
// Unknown endianness.  Fall back to bit shifts.

#if !CAPNP_DISABLE_ENDIAN_DETECTION
#warning "Couldn't detect endianness of your platform.  Using unoptimized fallback implementation."
#warning "Consider changing this code to detect your platform and send us a patch!"
#endif

template <typename T, size_t size = sizeof(T)>
class ShiftingWireValue;

template <typename T>
class ShiftingWireValue<T, 1> {
public:
  KJ_ALWAYS_INLINE(T get() const) { return value; }
  KJ_ALWAYS_INLINE(void set(T newValue)) { value = newValue; }

private:
  T value;
};

template <typename T>
class ShiftingWireValue<T, 2> {
public:
  KJ_ALWAYS_INLINE(T get() const) {
    uint16_t raw = (static_cast<uint16_t>(bytes[0])     ) |
                   (static_cast<uint16_t>(bytes[1]) << 8);
    T result;
    memcpy(&result, &raw, sizeof(T));
    return result;
  }
  KJ_ALWAYS_INLINE(void set(T newValue)) {
    uint16_t raw;
    memcpy(&raw, &newValue, sizeof(T));
    bytes[0] = raw;
    bytes[1] = raw >> 8;
  }

private:
  union {
    byte bytes[2];
    uint16_t align;
  };
};

template <typename T>
class ShiftingWireValue<T, 4> {
public:
  KJ_ALWAYS_INLINE(T get() const) {
    uint32_t raw = (static_cast<uint32_t>(bytes[0])      ) |
                   (static_cast<uint32_t>(bytes[1]) <<  8) |
                   (static_cast<uint32_t>(bytes[2]) << 16) |
                   (static_cast<uint32_t>(bytes[3]) << 24);
    T result;
    memcpy(&result, &raw, sizeof(T));
    return result;
  }
  KJ_ALWAYS_INLINE(void set(T newValue)) {
    uint32_t raw;
    memcpy(&raw, &newValue, sizeof(T));
    bytes[0] = raw;
    bytes[1] = raw >> 8;
    bytes[2] = raw >> 16;
    bytes[3] = raw >> 24;
  }

private:
  union {
    byte bytes[4];
    uint32_t align;
  };
};

template <typename T>
class ShiftingWireValue<T, 8> {
public:
  KJ_ALWAYS_INLINE(T get() const) {
    uint64_t raw = (static_cast<uint64_t>(bytes[0])      ) |
                   (static_cast<uint64_t>(bytes[1]) <<  8) |
                   (static_cast<uint64_t>(bytes[2]) << 16) |
                   (static_cast<uint64_t>(bytes[3]) << 24) |
                   (static_cast<uint64_t>(bytes[4]) << 32) |
                   (static_cast<uint64_t>(bytes[5]) << 40) |
                   (static_cast<uint64_t>(bytes[6]) << 48) |
                   (static_cast<uint64_t>(bytes[7]) << 56);
    T result;
    memcpy(&result, &raw, sizeof(T));
    return result;
  }
  KJ_ALWAYS_INLINE(void set(T newValue)) {
    uint64_t raw;
    memcpy(&raw, &newValue, sizeof(T));
    bytes[0] = raw;
    bytes[1] = raw >> 8;
    bytes[2] = raw >> 16;
    bytes[3] = raw >> 24;
    bytes[4] = raw >> 32;
    bytes[5] = raw >> 40;
    bytes[6] = raw >> 48;
    bytes[7] = raw >> 56;
  }

private:
  union {
    byte bytes[8];
    uint64_t align;
  };
};

template <typename T>
using WireValue = ShiftingWireValue<T>;
// To prevent ODR problems when endian-test, endian-reverse-test, and endian-fallback-test are
// linked together, we define each implementation with a different name and define an alias to the
// one we want to use.

#endif

}  // namespace _ (private)
}  // namespace capnp

#endif  // CAPNP_ENDIAN_H_
