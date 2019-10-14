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

#pragma once

#include "common.h"

KJ_BEGIN_HEADER

namespace kj {

namespace _ {  // private

template <uint i, typename Key, typename First, typename... Rest>
struct TypeIndex_ { static constexpr uint value = TypeIndex_<i + 1, Key, Rest...>::value; };
template <uint i, typename Key, typename... Rest>
struct TypeIndex_<i, Key, Key, Rest...> { static constexpr uint value = i; };

enum class Variants0 {};
enum class Variants1 { _variant0 };
enum class Variants2 { _variant0, _variant1 };
enum class Variants3 { _variant0, _variant1, _variant2 };
enum class Variants4 { _variant0, _variant1, _variant2, _variant3 };
enum class Variants5 { _variant0, _variant1, _variant2, _variant3, _variant4 };
enum class Variants6 { _variant0, _variant1, _variant2, _variant3, _variant4, _variant5 };
enum class Variants7 { _variant0, _variant1, _variant2, _variant3, _variant4, _variant5, _variant6 };
enum class Variants8 { _variant0, _variant1, _variant2, _variant3, _variant4, _variant5, _variant6,
                       _variant7 };

template <uint i> struct Variants_;
template <> struct Variants_<0> { typedef Variants0 Type; };
template <> struct Variants_<1> { typedef Variants1 Type; };
template <> struct Variants_<2> { typedef Variants2 Type; };
template <> struct Variants_<3> { typedef Variants3 Type; };
template <> struct Variants_<4> { typedef Variants4 Type; };
template <> struct Variants_<5> { typedef Variants5 Type; };
template <> struct Variants_<6> { typedef Variants6 Type; };
template <> struct Variants_<7> { typedef Variants7 Type; };
template <> struct Variants_<8> { typedef Variants8 Type; };

template <uint i>
using Variants = typename Variants_<i>::Type;

}  // namespace _ (private)

template <typename... Variants>
class OneOf {
  template <typename Key>
  static inline constexpr uint typeIndex() { return _::TypeIndex_<1, Key, Variants...>::value; }
  // Get the 1-based index of Key within the type list Types.

public:
  inline OneOf(): tag(0) {}
  OneOf(const OneOf& other) { copyFrom(other); }
  OneOf(OneOf& other) { copyFrom(other); }
  OneOf(OneOf&& other) { moveFrom(other); }
  template <typename T>
  OneOf(T&& other): tag(typeIndex<Decay<T>>()) {
    ctor(*reinterpret_cast<Decay<T>*>(space), kj::fwd<T>(other));
  }
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
  T& init(Params&&... params) {
    if (tag != 0) destroy();
    ctor(*reinterpret_cast<T*>(space), kj::fwd<Params>(params)...);
    tag = typeIndex<T>();
    return *reinterpret_cast<T*>(space);
  }

  template <typename T>
  Maybe<T&> tryGet() {
    if (is<T>()) {
      return *reinterpret_cast<T*>(space);
    } else {
      return nullptr;
    }
  }
  template <typename T>
  Maybe<const T&> tryGet() const {
    if (is<T>()) {
      return *reinterpret_cast<const T*>(space);
    } else {
      return nullptr;
    }
  }

  template <uint i>
  KJ_NORETURN(void allHandled());
  // After a series of if/else blocks handling each variant of the OneOf, have the final else
  // block call allHandled<n>() where n is the number of variants. This will fail to compile
  // if new variants are added in the future.

  typedef _::Variants<sizeof...(Variants)> Tag;

  Tag which() const {
    KJ_IREQUIRE(tag != 0, "Can't KJ_SWITCH_ONEOF() on uninitialized value.");
    return static_cast<Tag>(tag - 1);
  }

  template <typename T>
  static constexpr Tag tagFor() {
    return static_cast<Tag>(typeIndex<T>() - 1);
  }

  OneOf* _switchSubject() & { return this; }
  const OneOf* _switchSubject() const& { return this; }
  _::NullableValue<OneOf> _switchSubject() && { return kj::mv(*this); }

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

  static constexpr auto spaceSize = maxSize(sizeof(Variants)...);
  // TODO(msvc):  This constant could just as well go directly inside space's bracket's, where it's
  // used, but MSVC suffers a parse error on `...`.

  union {
    byte space[spaceSize];

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
  inline bool copyVariantFrom(OneOf& other) {
    if (other.is<T>()) {
      ctor(*reinterpret_cast<T*>(space), other.get<T>());
    }
    return false;
  }
  void copyFrom(OneOf& other) {
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

template <typename... Variants>
template <uint i>
void OneOf<Variants...>::allHandled() {
  // After a series of if/else blocks handling each variant of the OneOf, have the final else
  // block call allHandled<n>() where n is the number of variants. This will fail to compile
  // if new variants are added in the future.

  static_assert(i == sizeof...(Variants), "new OneOf variants need to be handled here");
  KJ_UNREACHABLE;
}

#if __cplusplus > 201402L
#define KJ_SWITCH_ONEOF(value) \
  switch (auto _kj_switch_subject = (value)._switchSubject(); _kj_switch_subject->which())
#else
#define KJ_SWITCH_ONEOF(value) \
  /* Without C++17, we can only support one switch per containing block. Deal with it. */ \
  auto _kj_switch_subject = (value)._switchSubject(); \
  switch (_kj_switch_subject->which())
#endif
#define KJ_CASE_ONEOF(name, ...) \
    break; \
  case ::kj::Decay<decltype(*_kj_switch_subject)>::template tagFor<__VA_ARGS__>(): \
    for (auto& name = _kj_switch_subject->template get<__VA_ARGS__>(), *_kj_switch_done = &name; \
         _kj_switch_done; _kj_switch_done = nullptr)
#define KJ_CASE_ONEOF_DEFAULT break; default:
// Allows switching over a OneOf.
//
// Example:
//
//     kj::OneOf<int, float, const char*> variant;
//     KJ_SWITCH_ONEOF(variant) {
//       KJ_CASE_ONEOF(i, int) {
//         doSomethingWithInt(i);
//       }
//       KJ_CASE_ONEOF(s, const char*) {
//         doSomethingWithString(s);
//       }
//       KJ_CASE_ONEOF_DEFAULT {
//         doSomethingElse();
//       }
//     }
//
// Notes:
// - If you don't handle all possible types and don't include a default branch, you'll get a
//   compiler warning, just like a regular switch() over an enum where one of the enum values is
//   missing.
// - There's no need for a `break` statement in a KJ_CASE_ONEOF; it is implied.
// - Under C++11 and C++14, only one KJ_SWITCH_ONEOF() can appear in a block. Wrap the switch in
//   a pair of braces if you need a second switch in the same block. If C++17 is enabled, this is
//   not an issue.
//
// Implementation notes:
// - The use of __VA_ARGS__ is to account for template types that have commas separating type
//   parameters, since macros don't recognize <> as grouping.
// - _kj_switch_done is really used as a boolean flag to prevent the for() loop from actually
//   looping, but it's defined as a pointer since that's all we can define in this context.

}  // namespace kj

KJ_END_HEADER
