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

#ifndef KJ_ONE_OF_H_
#define KJ_ONE_OF_H_

#include "common.h"

namespace kj {

namespace _ {  // private

template <uint i, typename Key, typename First, typename... Rest>
struct TypeIndex_ { static constexpr uint value = TypeIndex_<i + 1, Key, Rest...>::value; };
template <uint i, typename Key, typename... Rest>
struct TypeIndex_<i, Key, Key, Rest...> { static constexpr uint value = i; };

}  // namespace _ (private)

template <typename... Variants>
class OneOf {
  template <typename Key>
  static inline constexpr uint typeIndex() { return _::TypeIndex_<1, Key, Variants...>::value; }
  // Get the 1-based index of Key within the type list Types.

public:
  inline OneOf(): tag(0) {}
  OneOf(const OneOf& other) { copyFrom(other); }
  OneOf(OneOf&& other) { moveFrom(other); }
  ~OneOf() { destroy(); }

  OneOf& operator=(const OneOf& other) { if (tag != 0) destroy(); copyFrom(other); return *this; }
  OneOf& operator=(OneOf&& other) { if (tag != 0) destroy(); moveFrom(other); return *this; }

  inline bool operator==(decltype(nullptr)) const { return tag == 0; }
  inline bool operator!=(decltype(nullptr)) const { return tag != 0; }

  template <typename T>
  bool is() const {
    return tag == typeIndex<T>();
  }

  template <typename T>
  T& get() {
    KJ_IREQUIRE(is<T>(), "Must check OneOf::is<T>() before calling get<T>().");
    return *reinterpret_cast<T*>(space);
  }
  template <typename T>
  const T& get() const {
    KJ_IREQUIRE(is<T>(), "Must check OneOf::is<T>() before calling get<T>().");
    return *reinterpret_cast<const T*>(space);
  }

  template <typename T, typename... Params>
  void init(Params&&... params) {
    if (tag != 0) destroy();
    ctor(*reinterpret_cast<T*>(space), kj::fwd<Params>(params)...);
    tag = typeIndex<T>();
  }

private:
  uint tag;

  static inline constexpr size_t maxSize(size_t a) {
    return a;
  }
  template <typename... Rest>
  static inline constexpr size_t maxSize(size_t a, size_t b, Rest... rest) {
    return maxSize(kj::max(a, b), rest...);
  }
  // Returns the maximum of all the parameters.
  // TODO(someday):  Generalize the above template and make it common.  I tried, but C++ decided to
  //   be difficult so I cut my losses.

  union {
    byte space[maxSize(sizeof(Variants)...)];

    void* forceAligned;
    // TODO(someday):  Use C++11 alignas() once we require GCC 4.8 / Clang 3.3.
  };

  template <typename... T>
  inline void doAll(T... t) {}

  template <typename T>
  inline bool destroyVariant() {
    if (tag == typeIndex<T>()) {
      tag = 0;
      dtor(*reinterpret_cast<T*>(space));
    }
    return false;
  }
  void destroy() {
    doAll(destroyVariant<Variants>()...);
  }

  template <typename T>
  inline bool copyVariantFrom(const OneOf& other) {
    if (other.is<T>()) {
      ctor(*reinterpret_cast<T*>(space), other.get<T>());
    }
    return false;
  }
  void copyFrom(const OneOf& other) {
    // Initialize as a copy of `other`.  Expects that `this` starts out uninitialized, so the tag
    // is invalid.
    tag = other.tag;
    doAll(copyVariantFrom<Variants>(other)...);
  }

  template <typename T>
  inline bool moveVariantFrom(OneOf& other) {
    if (other.is<T>()) {
      ctor(*reinterpret_cast<T*>(space), kj::mv(other.get<T>()));
    }
    return false;
  }
  void moveFrom(OneOf& other) {
    // Initialize as a copy of `other`.  Expects that `this` starts out uninitialized, so the tag
    // is invalid.
    tag = other.tag;
    doAll(moveVariantFrom<Variants>(other)...);
  }
};

}  // namespace kj

#endif  // KJ_ONE_OF_H_
