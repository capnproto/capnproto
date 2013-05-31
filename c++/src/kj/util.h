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

#ifndef KJ_UTIL_H_
#define KJ_UTIL_H_

#include <initializer_list>
#include <utility>
#include <type_traits>
#include "array.h"
#include "string.h"
#include <string.h>

namespace kj {

// =======================================================================================
// String stuff

inline size_t sum(std::initializer_list<size_t> nums) {
  size_t result = 0;
  for (auto num: nums) {
    result += num;
  }
  return result;
}

inline char* fill(char* ptr) { return ptr; }

template <typename First, typename... Rest>
char* fill(char* __restrict__ target, const First& first, Rest&&... rest) {
  auto i = first.begin();
  auto end = first.end();
  while (i != end) {
    *target++ = *i++;
  }
  return fill(target, std::forward<Rest>(rest)...);
}

template <typename... Params>
String concat(Params&&... params) {
  // Concatenate a bunch of containers into a single Array.  The containers can be anything that
  // is iterable and whose elements can be converted to `Element`.

  String result = heapString(sum({params.size()...}));
  fill(result.begin(), std::forward<Params>(params)...);
  return result;
}

inline String concat(String&& arr) {
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
  // the problem that it pollutes other people's namespaces and even the global namespace.  For
  // example, some other project may already have functions called `toString` which do something
  // different.  Declaring `operator*` with `Stringifier` as the left operand cannot conflict with
  // anything.

  inline ArrayPtr<const char> operator*(ArrayPtr<const char> s) const { return s; }
  inline ArrayPtr<const char> operator*(const Array<const char>& s) const { return s; }
  inline ArrayPtr<const char> operator*(const Array<char>& s) const { return s; }
  template<size_t n>
  inline ArrayPtr<const char> operator*(const CappedArray<char, n>& s) const { return s; }
  inline ArrayPtr<const char> operator*(const char* s) const { return arrayPtr(s, strlen(s)); }
  inline ArrayPtr<const char> operator*(const String& s) const { return s.asArray(); }
  inline ArrayPtr<const char> operator*(const StringPtr& s) const { return s.asArray(); }

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
  CappedArray<char, sizeof(const void*) * 4> operator*(const void* s) const;

  template <typename T>
  Array<char> operator*(ArrayPtr<T> arr) const;
  template <typename T>
  Array<char> operator*(const Array<T>& arr) const;
};
static constexpr Stringifier STR = Stringifier();

CappedArray<char, sizeof(unsigned short) * 4> hex(unsigned short i);
CappedArray<char, sizeof(unsigned int) * 4> hex(unsigned int i);
CappedArray<char, sizeof(unsigned long) * 4> hex(unsigned long i);
CappedArray<char, sizeof(unsigned long long) * 4> hex(unsigned long long i);

template <typename... Params>
String str(Params&&... params) {
  // Magic function which builds a string from a bunch of arbitrary values.  Example:
  //     str(1, " / ", 2, " = ", 0.5)
  // returns:
  //     "1 / 2 = 0.5"
  // To teach `str` how to stringify a type, see `Stringifier`.

  return concat(STR * std::forward<Params>(params)...);
}

template <typename T>
String strArray(T&& arr, const char* delim) {
  size_t delimLen = strlen(delim);
  KJ_STACK_ARRAY(decltype(STR * arr[0]), pieces, arr.size(), 8, 32);
  size_t size = 0;
  for (size_t i = 0; i < arr.size(); i++) {
    if (i > 0) size += delimLen;
    pieces[i] = STR * arr[i];
    size += pieces[i].size();
  }

  String result = heapString(size);
  char* pos = result.begin();
  for (size_t i = 0; i < arr.size(); i++) {
    if (i > 0) {
      memcpy(pos, delim, delimLen);
      pos += delimLen;
    }
    pos = fill(pos, pieces[i]);
  }
  return result;
}

template <typename T>
inline Array<char> Stringifier::operator*(ArrayPtr<T> arr) const {
  return strArray(arr, ", ");
}

template <typename T>
inline Array<char> Stringifier::operator*(const Array<T>& arr) const {
  return strArray(arr, ", ");
}

template <typename T, typename Func>
auto mapArray(T&& arr, Func&& func) -> Array<decltype(func(arr[0]))> {
  // TODO(cleanup):  Use ArrayBuilder.
  Array<decltype(func(arr[0]))> result = heapArray<decltype(func(arr[0]))>(arr.size());
  size_t pos = 0;
  for (auto& element: arr) {
    result[pos++] = func(element);
  }
  return result;
}

}  // namespace kj

#endif  // KJ_UTIL_H_
