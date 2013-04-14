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

#ifndef CAPNPROTO_UTIL_H_
#define CAPNPROTO_UTIL_H_

#include <initializer_list>
#include <utility>
#include <type_traits>
#include "type-safety.h"
#include <string.h>

namespace capnproto {

// =======================================================================================
// Arrays

// TODO:  Move Array here?

template <typename T, size_t fixedSize>
class FixedArray {
public:
  inline size_t size() const { return fixedSize; }
  inline T* begin() { return content; }
  inline T* end() { return content + fixedSize; }
  inline const T* begin() const { return content; }
  inline const T* end() const { return content + fixedSize; }

  inline operator ArrayPtr<T>() {
    return arrayPtr(content, fixedSize);
  }
  inline operator ArrayPtr<const T>() const {
    return arrayPtr(content, fixedSize);
  }

  inline T& operator[](size_t index) { return content[index]; }
  inline const T& operator[](size_t index) const { return content[index]; }

private:
  T content[fixedSize];
};

template <typename T, size_t fixedSize>
class CappedArray {
public:
  inline constexpr CappedArray(): currentSize(fixedSize) {}
  inline explicit constexpr CappedArray(size_t s): currentSize(s) {}

  inline size_t size() const { return currentSize; }
  inline void setSize(size_t s) { currentSize = s; }
  inline T* begin() { return content; }
  inline T* end() { return content + currentSize; }
  inline const T* begin() const { return content; }
  inline const T* end() const { return content + currentSize; }

  inline operator ArrayPtr<T>() const {
    return arrayPtr(content, fixedSize);
  }

private:
  size_t currentSize;
  T content[fixedSize];
};

template <typename T>
Array<T> iterableToArray(T&& a) {
  Array<T> result = newArray<T>(a.size());
  auto i = a.iterator();
  auto end = a.end();
  T* __restrict__ ptr = result.begin();
  while (i != end) {
    *ptr++ = *i++;
  }
  return result;
}

template <typename T>
Array<T> iterableToArray(Array<T>&& a) {
  return std::move(a);
}

// =======================================================================================
// String stuff

inline size_t sum(std::initializer_list<size_t> nums) {
  size_t result = 0;
  for (auto num: nums) {
    result += num;
  }
  return result;
}

template <typename Element>
Element* fill(Element* ptr) { return ptr; }

template <typename Element, typename First, typename... Rest>
Element* fill(Element* __restrict__ target, First& first, Rest&&... rest) {
  auto i = first.begin();
  auto end = first.end();
  while (i != end) {
    *target++ = *i++;
  }
  return fill(target, std::forward<Rest>(rest)...);
}

template <typename Element, typename First, typename... Rest>
Element* fill(Element* __restrict__ target, First&& first, Rest&&... rest) {
  auto i = first.begin();
  auto end = first.end();
  while (i != end) {
    *target++ = std::move(*i++);
  }
  return fill(target, std::forward<Rest>(rest)...);
}

template <typename Element, typename... Params>
Array<Element> concat(Params&&... params) {
  Array<Element> result = newArray<Element>(sum({params.size()...}));
  fill(result.begin(), std::forward<Params>(params)...);
  return result;
}

template <typename Element>
Array<Element> concat(Array<Element>&& arr) {
  return std::move(arr);
}

struct Stringifier {
  // This is a dummy type with only one instance: STR (below).  To make an arbitrary type
  // stringifiable, define `operator*(Stringifier, T)` to return an iterable container of `char`.
  // The container type must have a `size()` method.  Be sure to declare the operator in the same
  // namespace as `T` **or** in the global scope.
  //
  // A more usual way to accomplish what we're doing here would be to require that you define
  // a function like `toString(T)` and then rely on argument-dependent lookup.  However, this has
  // the problem that it pollutes other people's namespaces and even the global namespace.
  // Declaring `operator*` with `Stringifier` as the left operand does not harm any other
  // namespaces.

  inline ArrayPtr<const char> operator*(ArrayPtr<const char> s) const { return s; }
  inline ArrayPtr<const char> operator*(const char* s) const { return arrayPtr(s, strlen(s)); }

  inline FixedArray<char, 1> operator*(char c) const {
    FixedArray<char, 1> result;
    result[0] = c;
    return result;
  }

  CappedArray<char, sizeof(short) * 4> operator*(short i) const;
  CappedArray<char, sizeof(unsigned short) * 4> operator*(unsigned short i) const;
  CappedArray<char, sizeof(int) * 4> operator*(int i) const;
  CappedArray<char, sizeof(unsigned int) * 4> operator*(unsigned int i) const;
  CappedArray<char, sizeof(long) * 4> operator*(long i) const;
  CappedArray<char, sizeof(unsigned long) * 4> operator*(unsigned long i) const;
  CappedArray<char, sizeof(long long) * 4> operator*(long long i) const;
  CappedArray<char, sizeof(unsigned long long) * 4> operator*(unsigned long long i) const;
  CappedArray<char, 24> operator*(float f) const;
  CappedArray<char, 32> operator*(double f) const;
};
static constexpr Stringifier STR;

template <typename... Params>
Array<char> str(Params&&... params) {
  return concat<char>(STR * std::forward<Params>(params)...);
}

}  // namespace capnproto

#endif  // CAPNPROTO_UTIL_H_
