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

#include <initializer_list>
#include "array.h"
#include "units.h"
#include <string.h>

KJ_BEGIN_HEADER

namespace kj {
  class StringPtr;
  class String;

  class StringTree;   // string-tree.h
}

constexpr kj::StringPtr operator "" _kj(const char* str, size_t n);
// You can append _kj to a string literal to make its type be StringPtr. There are a few cases
// where you must do this for correctness:
// - When you want to declare a constexpr StringPtr. Without _kj, this is a compile error.
// - When you want to initialize a static/global StringPtr from a string literal without forcing
//   global constructor code to run at dynamic initialization time.
// - When you have a string literal that contains NUL characters. Without _kj, the string will
//   be considered to end at the first NUL.
// - When you want to initialize an ArrayPtr<const char> from a string literal, without including
//   the NUL terminator in the data. (Initializing an ArrayPtr from a regular string literal is
//   a compile error specifically due to this ambiguity.)
//
// In other cases, there should be no difference between initializing a StringPtr from a regular
// string literal vs. one with _kj (assuming the compiler is able to optimize away strlen() on a
// string literal).

namespace kj {

// Our STL string SFINAE trick does not work with GCC 4.7, but it works with Clang and GCC 4.8, so
// we'll just preprocess it out if not supported.
#if __clang__ || __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8) || _MSC_VER
#define KJ_COMPILER_SUPPORTS_STL_STRING_INTEROP 1
#endif

// =======================================================================================
// StringPtr -- A NUL-terminated ArrayPtr<const char> containing UTF-8 text.
//
// NUL bytes are allowed to appear before the end of the string.  The only requirement is that
// a NUL byte appear immediately after the last byte of the content.  This terminator byte is not
// counted in the string's size.

class StringPtr {
public:
  inline StringPtr(): content("", 1) {}
  inline StringPtr(decltype(nullptr)): content("", 1) {}
  inline StringPtr(const char* value KJ_LIFETIMEBOUND): content(value, strlen(value) + 1) {}
  inline StringPtr(const char* value KJ_LIFETIMEBOUND, size_t size): content(value, size + 1) {
    KJ_IREQUIRE(value[size] == '\0', "StringPtr must be NUL-terminated.");
  }
  inline StringPtr(const char* begin KJ_LIFETIMEBOUND, const char* end KJ_LIFETIMEBOUND): StringPtr(begin, end - begin) {}
  inline StringPtr(String&& value KJ_LIFETIMEBOUND) : StringPtr(value) {}
  inline StringPtr(const String& value KJ_LIFETIMEBOUND);
  StringPtr& operator=(String&& value) = delete;
  inline StringPtr& operator=(decltype(nullptr)) {
    content = ArrayPtr<const char>("", 1);
    return *this;
  }

#if __cpp_char8_t
  inline StringPtr(const char8_t* value KJ_LIFETIMEBOUND): StringPtr(reinterpret_cast<const char*>(value)) {}
  inline StringPtr(const char8_t* value KJ_LIFETIMEBOUND, size_t size)
      : StringPtr(reinterpret_cast<const char*>(value), size) {}
  inline StringPtr(const char8_t* begin KJ_LIFETIMEBOUND, const char8_t* end KJ_LIFETIMEBOUND)
      : StringPtr(reinterpret_cast<const char*>(begin), reinterpret_cast<const char*>(end)) {}
  // KJ strings are and always have been UTF-8, so screw this C++20 char8_t stuff.
#endif

#if KJ_COMPILER_SUPPORTS_STL_STRING_INTEROP
  template <typename T, typename = decltype(instance<T>().c_str())>
  inline StringPtr(const T& t KJ_LIFETIMEBOUND): StringPtr(t.c_str()) {}
  // Allow implicit conversion from any class that has a c_str() method (namely, std::string).
  // We use a template trick to detect std::string in order to avoid including the header for
  // those who don't want it.

  template <typename T, typename = decltype(instance<T>().c_str())>
  inline operator T() const { return cStr(); }
  // Allow implicit conversion to any class that has a c_str() method (namely, std::string).
  // We use a template trick to detect std::string in order to avoid including the header for
  // those who don't want it.
#endif

  inline constexpr operator ArrayPtr<const char>() const;
  inline constexpr ArrayPtr<const char> asArray() const;
  inline ArrayPtr<const byte> asBytes() const { return asArray().asBytes(); }
  // Result does not include NUL terminator.

  inline const char* cStr() const { return content.begin(); }
  // Returns NUL-terminated string.

  inline size_t size() const { return content.size() - 1; }
  // Result does not include NUL terminator.

  inline char operator[](size_t index) const { return content[index]; }

  inline constexpr const char* begin() const { return content.begin(); }
  inline constexpr const char* end() const { return content.end() - 1; }

  inline constexpr bool operator==(decltype(nullptr)) const { return content.size() <= 1; }
  inline constexpr bool operator!=(decltype(nullptr)) const { return content.size() > 1; }

  inline bool operator==(const StringPtr& other) const;
  inline bool operator!=(const StringPtr& other) const { return !(*this == other); }
  inline bool operator< (const StringPtr& other) const;
  inline bool operator> (const StringPtr& other) const { return other < *this; }
  inline bool operator<=(const StringPtr& other) const { return !(other < *this); }
  inline bool operator>=(const StringPtr& other) const { return !(*this < other); }

  inline StringPtr slice(size_t start) const;
  inline ArrayPtr<const char> slice(size_t start, size_t end) const;
  // A string slice is only NUL-terminated if it is a suffix, so slice() has a one-parameter
  // version that assumes end = size().

  inline bool startsWith(const StringPtr& other) const;
  inline bool endsWith(const StringPtr& other) const;

  inline Maybe<size_t> findFirst(char c) const;
  inline Maybe<size_t> findLast(char c) const;

  template <typename T>
  T parseAs() const;
  // Parse string as template number type.
  // Integer numbers prefixed by "0x" and "0X" are parsed in base 16 (like strtoi with base 0).
  // Integer numbers prefixed by "0" are parsed in base 10 (unlike strtoi with base 0).
  // Overflowed integer numbers throw exception.
  // Overflowed floating numbers return inf.

private:
  inline explicit constexpr StringPtr(ArrayPtr<const char> content): content(content) {}

  ArrayPtr<const char> content;

  friend constexpr kj::StringPtr (::operator "" _kj)(const char* str, size_t n);
  friend class SourceLocation;
};

#if !__cpp_impl_three_way_comparison
inline bool operator==(const char* a, const StringPtr& b) { return b == a; }
inline bool operator!=(const char* a, const StringPtr& b) { return b != a; }
#endif

template <> char StringPtr::parseAs<char>() const;
template <> signed char StringPtr::parseAs<signed char>() const;
template <> unsigned char StringPtr::parseAs<unsigned char>() const;
template <> short StringPtr::parseAs<short>() const;
template <> unsigned short StringPtr::parseAs<unsigned short>() const;
template <> int StringPtr::parseAs<int>() const;
template <> unsigned StringPtr::parseAs<unsigned>() const;
template <> long StringPtr::parseAs<long>() const;
template <> unsigned long StringPtr::parseAs<unsigned long>() const;
template <> long long StringPtr::parseAs<long long>() const;
template <> unsigned long long StringPtr::parseAs<unsigned long long>() const;
template <> float StringPtr::parseAs<float>() const;
template <> double StringPtr::parseAs<double>() const;

// =======================================================================================
// String -- A NUL-terminated Array<char> containing UTF-8 text.
//
// NUL bytes are allowed to appear before the end of the string.  The only requirement is that
// a NUL byte appear immediately after the last byte of the content.  This terminator byte is not
// counted in the string's size.
//
// To allocate a String, you must call kj::heapString().  We do not implement implicit copying to
// the heap because this hides potential inefficiency from the developer.

class String {
public:
  String() = default;
  inline String(decltype(nullptr)): content(nullptr) {}
  inline String(char* value, size_t size, const ArrayDisposer& disposer);
  // Does not copy.  `size` does not include NUL terminator, but `value` must be NUL-terminated.
  inline explicit String(Array<char> buffer);
  // Does not copy.  Requires `buffer` ends with `\0`.

  inline operator ArrayPtr<char>() KJ_LIFETIMEBOUND;
  inline operator ArrayPtr<const char>() const KJ_LIFETIMEBOUND;
  inline ArrayPtr<char> asArray() KJ_LIFETIMEBOUND;
  inline ArrayPtr<const char> asArray() const KJ_LIFETIMEBOUND;
  inline ArrayPtr<byte> asBytes() KJ_LIFETIMEBOUND { return asArray().asBytes(); }
  inline ArrayPtr<const byte> asBytes() const KJ_LIFETIMEBOUND { return asArray().asBytes(); }
  // Result does not include NUL terminator.

  inline StringPtr asPtr() const KJ_LIFETIMEBOUND {
    // Convenience operator to return a StringPtr.
    return StringPtr{*this};
  }

  inline Array<char> releaseArray() { return kj::mv(content); }
  // Disowns the backing array (which includes the NUL terminator) and returns it. The String value
  // is clobbered (as if moved away).

  inline const char* cStr() const KJ_LIFETIMEBOUND;

  inline size_t size() const;
  // Result does not include NUL terminator.

  inline char operator[](size_t index) const;
  inline char& operator[](size_t index) KJ_LIFETIMEBOUND;

  inline char* begin() KJ_LIFETIMEBOUND;
  inline char* end() KJ_LIFETIMEBOUND;
  inline const char* begin() const KJ_LIFETIMEBOUND;
  inline const char* end() const KJ_LIFETIMEBOUND;

  inline bool operator==(decltype(nullptr)) const { return content.size() <= 1; }
  inline bool operator!=(decltype(nullptr)) const { return content.size() > 1; }

  inline bool operator==(const StringPtr& other) const { return StringPtr(*this) == other; }
  inline bool operator!=(const StringPtr& other) const { return StringPtr(*this) != other; }
  inline bool operator< (const StringPtr& other) const { return StringPtr(*this) <  other; }
  inline bool operator> (const StringPtr& other) const { return StringPtr(*this) >  other; }
  inline bool operator<=(const StringPtr& other) const { return StringPtr(*this) <= other; }
  inline bool operator>=(const StringPtr& other) const { return StringPtr(*this) >= other; }

  inline bool operator==(const String& other) const { return StringPtr(*this) == StringPtr(other); }
  inline bool operator!=(const String& other) const { return StringPtr(*this) != StringPtr(other); }
  inline bool operator< (const String& other) const { return StringPtr(*this) <  StringPtr(other); }
  inline bool operator> (const String& other) const { return StringPtr(*this) >  StringPtr(other); }
  inline bool operator<=(const String& other) const { return StringPtr(*this) <= StringPtr(other); }
  inline bool operator>=(const String& other) const { return StringPtr(*this) >= StringPtr(other); }
  // Note that if we don't overload for `const String&` specifically, then C++20 will decide that
  // comparisons between two strings are ambiguous. (Clang turns this into a warning,
  // -Wambiguous-reversed-operator, due to the stupidity...)

  inline bool startsWith(const StringPtr& other) const { return StringPtr(*this).startsWith(other);}
  inline bool endsWith(const StringPtr& other) const { return StringPtr(*this).endsWith(other); }

  inline StringPtr slice(size_t start) const KJ_LIFETIMEBOUND {
    return StringPtr(*this).slice(start);
  }
  inline ArrayPtr<const char> slice(size_t start, size_t end) const KJ_LIFETIMEBOUND {
    return StringPtr(*this).slice(start, end);
  }

  inline Maybe<size_t> findFirst(char c) const { return StringPtr(*this).findFirst(c); }
  inline Maybe<size_t> findLast(char c) const { return StringPtr(*this).findLast(c); }

  template <typename T>
  T parseAs() const { return StringPtr(*this).parseAs<T>(); }
  // Parse as number

private:
  Array<char> content;
};

#if !__cpp_impl_three_way_comparison
inline bool operator==(const char* a, const String& b) { return b == a; }
inline bool operator!=(const char* a, const String& b) { return b != a; }
#endif

String heapString(size_t size);
// Allocate a String of the given size on the heap, not including NUL terminator.  The NUL
// terminator will be initialized automatically but the rest of the content is not initialized.

String heapString(const char* value);
String heapString(const char* value, size_t size);
String heapString(StringPtr value);
String heapString(const String& value);
String heapString(ArrayPtr<const char> value);
// Allocates a copy of the given value on the heap.

// =======================================================================================
// Magic str() function which transforms parameters to text and concatenates them into one big
// String.

template <typename T>
class SignalSafeCharSequence: public T {
  // Annotates that the result of a stringification operation is signal-safe. T is the type being
  // wrapped after stringification (ArrayPtr/CappedArray/FixedArray/StringPtr).
public:
  using T::T;

  SignalSafeCharSequence() = default;
  template <typename ...Args>
  constexpr SignalSafeCharSequence(Args&&... args): T(kj::fwd<Args>(args)...) {}
};

namespace _ {  // private
inline size_t sum(std::initializer_list<size_t> nums) {
  size_t result = 0;
  for (auto num: nums) {
    result += num;
  }
  return result;
}

inline char* fill(char* ptr) { return ptr; }
inline char* fillLimited(char* ptr, char* limit) { return ptr; }

template <typename... Rest>
char* fill(char* __restrict__ target, const StringTree& first, Rest&&... rest);
template <typename... Rest>
char* fillLimited(char* __restrict__ target, char* limit, const StringTree& first, Rest&&... rest);
// Make str() work with stringifiers that return StringTree by patching fill().
//
// Defined in string-tree.h.

template <typename First, typename... Rest>
char* fill(char* __restrict__ target, const First& first, Rest&&... rest) {
  auto i = first.begin();
  auto end = first.end();
  while (i != end) {
    *target++ = *i++;
  }
  return fill(target, kj::fwd<Rest>(rest)...);
}

template <typename... Params>
String concat(Params&&... params) {
  // Concatenate a bunch of containers into a single Array.  The containers can be anything that
  // is iterable and whose elements can be converted to `char`.

  String result = heapString(sum({params.size()...}));
  fill(result.begin(), kj::fwd<Params>(params)...);
  return result;
}

inline String concat(String&& arr) {
  return kj::mv(arr);
}

template <typename First, typename... Rest>
char* fillLimited(char* __restrict__ target, char* limit, const First& first, Rest&&... rest) {
  auto i = first.begin();
  auto end = first.end();
  while (i != end) {
    if (target == limit) return target;
    *target++ = *i++;
  }
  return fillLimited(target, limit, kj::fwd<Rest>(rest)...);
}

template <typename T>
class Delimited;
// Delimits a sequence of type T with a string delimiter. Implements kj::delimited().

template <typename T, typename... Rest>
char* fill(char* __restrict__ target, Delimited<T>&& first, Rest&&... rest);
template <typename T, typename... Rest>
char* fillLimited(char* __restrict__ target, char* limit, Delimited<T>&& first,Rest&&... rest);
template <typename T, typename... Rest>
char* fill(char* __restrict__ target, Delimited<T>& first, Rest&&... rest);
template <typename T, typename... Rest>
char* fillLimited(char* __restrict__ target, char* limit, Delimited<T>& first,Rest&&... rest);
// As with StringTree, we special-case Delimited<T>.

// SignalSafeCharSequence<Delimited<T>> needs some special handling so that the right
// fill/fillLimited implementation is called.
template <typename T, typename... Rest>
char* fill(char* __restrict__ target, SignalSafeCharSequence<Delimited<T>>&& first,
    Rest&&... rest) {
  return fill(target, static_cast<Delimited<T>&&>(kj::mv(first)), kj::fwd<Rest>(rest)...);
}
template <typename T, typename... Rest>
char* fillLimited(char* __restrict__ target, char* limit,
    SignalSafeCharSequence<Delimited<T>>&& first, Rest&&... rest) {
  return fillLimited(target, limit, static_cast<Delimited<T>&&>(kj::mv(first)),
      kj::fwd<Rest>(rest)...);
}
template <typename T, typename... Rest>
char* fill(char* __restrict__ target, SignalSafeCharSequence<Delimited<T>>& first, Rest&&... rest) {
  return fill(target, static_cast<Delimited<T>&>(first), kj::fwd<Rest>(rest)...);
}
template <typename T, typename... Rest>
char* fillLimited(char* __restrict__ target, char* limit,
    SignalSafeCharSequence<Delimited<T>>& first,Rest&&... rest) {
  return fillLimited(target, limit, static_cast<Delimited<T>&>(first), kj::fwd<Rest>(rest)...);
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

  template <typename T>
  using SignalSafe = SignalSafeCharSequence<T>;

  inline SignalSafe<ArrayPtr<const char>>
  operator*(ArrayPtr<const char> s) const { return s; }
  inline SignalSafe<ArrayPtr<const char>>
  operator*(ArrayPtr<char> s) const { return s; }
  inline SignalSafe<ArrayPtr<const char>>
  operator*(const Array<const char>& s) const KJ_LIFETIMEBOUND {
    return s;
  }
  inline SignalSafe<ArrayPtr<const char>>
  operator*(const Array<char>& s) const KJ_LIFETIMEBOUND { return s; }
  template<size_t n>
  inline SignalSafe<ArrayPtr<const char>>
  operator*(const CappedArray<char, n>& s) const KJ_LIFETIMEBOUND {
    return s;
  }
  template <size_t n>
  inline SignalSafe<ArrayPtr<const char>>
  operator*(const FixedArray<char, n> &s) const KJ_LIFETIMEBOUND {
    return s;
  }
  inline SignalSafe<ArrayPtr<const char>>
  operator*(const char *s) const KJ_LIFETIMEBOUND {
    return arrayPtr(s, strlen(s));
  }
#if __cpp_char8_t
  inline SignalSafe<ArrayPtr<const char>> operator*(const char8_t* s) const KJ_LIFETIMEBOUND {
    return operator*(reinterpret_cast<const char*>(s));
  }
#endif
  inline SignalSafe<ArrayPtr<const char>> operator*(const String& s) const KJ_LIFETIMEBOUND {
    return s.asArray();
  }
  inline SignalSafe<ArrayPtr<const char>> operator*(
      const StringPtr& s) const { return s.asArray(); }

  inline Range<char> operator*(const Range<char>& r) const { return r; }
  inline Repeat<char> operator*(const Repeat<char>& r) const { return r; }

  inline SignalSafe<FixedArray<char, 1>> operator*(char c) const {
    FixedArray<char, 1> result;
    result[0] = c;
    return result;
  }

  constexpr SignalSafe<StringPtr> operator*(decltype(nullptr)) const {
    return "nullptr"_kj;
  }
  constexpr SignalSafe<StringPtr> operator*(bool b) const {
    return b ? "true"_kj : "false"_kj;
  }

  SignalSafe<CappedArray<char, 5>> operator*(signed char i) const;
  SignalSafe<CappedArray<char, 5>> operator*(unsigned char i) const;
  SignalSafe<CappedArray<char, sizeof(short) * 3 + 2>> operator*(short i) const;
  SignalSafe<CappedArray<char, sizeof(unsigned short) * 3 + 2>> operator*(unsigned short i) const;
  SignalSafe<CappedArray<char, sizeof(int) * 3 + 2>> operator*(int i) const;
  SignalSafe<CappedArray<char, sizeof(unsigned int) * 3 + 2>> operator*(unsigned int i) const;
  SignalSafe<CappedArray<char, sizeof(long) * 3 + 2>> operator*(long i) const;
  SignalSafe<CappedArray<char, sizeof(unsigned long) * 3 + 2>> operator*(unsigned long i) const;
  SignalSafe<CappedArray<char, sizeof(long long) * 3 + 2>> operator*(long long i) const;
  SignalSafe<CappedArray<char, sizeof(unsigned long long) * 3 + 2>> operator*(
      unsigned long long i) const;
  CappedArray<char, 24> operator*(float f) const;
  CappedArray<char, 32> operator*(double f) const;
  SignalSafe<CappedArray<char, sizeof(const void*) * 2 + 1>> operator*(const void* s) const;

#if KJ_COMPILER_SUPPORTS_STL_STRING_INTEROP  // supports expression SFINAE?
  template <typename T, typename Result = decltype(instance<T>().toString())>
  inline Result operator*(T&& value) const { return kj::fwd<T>(value).toString(); }
#endif
};
static KJ_CONSTEXPR(const) Stringifier STR = Stringifier();

}  // namespace _ (private)

template <typename T>
auto toCharSequence(T&& value) -> decltype(_::STR * kj::fwd<T>(value)) {
  // Returns an iterable of chars that represent a textual representation of the value, suitable
  // for debugging.
  //
  // Most users should use str() instead, but toCharSequence() may occasionally be useful to avoid
  // heap allocation overhead that str() implies.
  //
  // To specialize this function for your type, see KJ_STRINGIFY.

  return _::STR * kj::fwd<T>(value);
}

namespace _ {  // private
template <typename T, typename = void>
struct IsSignalSafeToCharSequence_ {
  static constexpr bool value = false;
};
template <typename T>
struct IsSignalSafeToCharSequence_<T, VoidSfinae<decltype(toCharSequence(instance<T>()))>> {
  static constexpr bool value = isInstanceOfTemplate<
      decltype(toCharSequence(instance<T>())), SignalSafeCharSequence>();
};

template <typename T> struct HasCappedCapacity_ { static constexpr bool value = false; };
template <typename T>
struct HasCappedCapacity_<SignalSafeCharSequence<T>>: public HasCappedCapacity_<T> {};
template <typename T, size_t N>
struct HasCappedCapacity_<FixedArray<T, N>> { static constexpr bool value = true; };
template <typename T, size_t N>
struct HasCappedCapacity_<CappedArray<T, N>> { static constexpr bool value = true; };

template <typename T> static inline constexpr bool hasCappedCapacity() {
  // This returns true if T::capacity() is a method that exists, false otherwise. This is a
  // helper function for isToCharSequenceResultingInCappedCapacity as I couldn't figure out how to
  // make the SFINAE magic less verbose.
  return HasCappedCapacity_<T>::value;
}

template <typename T> struct IsLiteralArray_ { static constexpr bool value = false; };
template <typename T, size_t N>
struct IsLiteralArray_<T(&)[N]> { static constexpr bool value = true; };
template <typename T> static constexpr bool isLiteralArray() { return IsLiteralArray_<T>::value; }
// Detect if a type is a C array.

template <typename T>
struct IsSpecialToCharSequenceCapacity_ { static constexpr bool value = false; };
template <size_t N>
struct IsSpecialToCharSequenceCapacity_<char(&)[N]> { static constexpr bool value = true; };
template <size_t N>
struct IsSpecialToCharSequenceCapacity_<const char(&)[N]> { static constexpr bool value = true; };
// TODO(someday): Support T[N] for types other than char which should get rid of most of these
//   helpers (would instead just imply stringification returning a
//   ConstexprDelimited<T, N, length of delimiter> type that can compute the total capacity
//   required.
// That would get rid of most of IsSpecialToCharSequenceCapacity_ and all IsLiteralArray_.
template <>
struct IsSpecialToCharSequenceCapacity_<decltype(nullptr)> { static constexpr bool value = true; };
template <>
struct IsSpecialToCharSequenceCapacity_<bool> { static constexpr bool value = true; };
// Will always be needed to handle nullptr/bool properly.

template <typename T>
static constexpr bool isSpecialToCharSequenceCapacity() {
  return IsSpecialToCharSequenceCapacity_<T>::value;
}

template <typename T, typename = void>
struct IsToCharSequenceResultingInCappedCapacity_ {
  static constexpr bool value = false;
};
template <typename T>
class IsToCharSequenceResultingInCappedCapacity_<T,
    VoidSfinae<decltype(toCharSequence(instance<T>()))>> {
public:
  static constexpr bool value = isSpecialToCharSequenceCapacity<T>() || (!isLiteralArray<T>() &&
      hasCappedCapacity<decltype(toCharSequence(instance<T>()))>());
  // Currently toCharSequence will think arrays are T* and say it has a capped capacity when it
  // doesn't (treating it like a void*). Thus we need special helpers here to disable.
};
template <typename T>
static constexpr inline bool isToCharSequenceResultingInCappedCapacity() {
  // Returns true if toCharSequence<T> returns a stringifiable type that has an upper-bound capacity
  // requirement known at compile-time (i.e. CappedArray/FixedArray). This is primarily useful for
  // indicating whether composite types are safe to stringify in a signal handler context as
  // otherwise you would need to allocate memory. There's an alternate universe where KJ_STRINGIFY
  // could optionally accept the buffer to append into rather than having to return a type, but
  // that seems overly complex.
  return IsToCharSequenceResultingInCappedCapacity_<T>::value;
};

template <typename T>
struct CappedCapacityOfToCharSequence_ {
  static constexpr size_t capacity =
      CappedCapacityOfToCharSequence_<decltype(toCharSequence(instance<T>()))>::capacity;
};
template <>
struct CappedCapacityOfToCharSequence_<decltype(nullptr)> {
  static constexpr size_t capacity = sizeof("nullptr") - 1;
};
template <>
struct CappedCapacityOfToCharSequence_<bool> {
  static constexpr size_t capacity = max(sizeof("true"), sizeof("false")) - 1;
};
template <typename T, size_t N>
struct CappedCapacityOfToCharSequence_<CappedArray<T, N>> {
  static constexpr size_t capacity = N;
};
template <typename T, size_t N>
struct CappedCapacityOfToCharSequence_<FixedArray<T, N>> {
  static constexpr size_t capacity = N;
};
template <typename T, size_t N>
struct CappedCapacityOfToCharSequence_<T(&)[N]> {
  // TODO(someday): Support arbitrary T[N] arrays by doing
  //   CappedCapacityOfToCharSequence_<T>::capacity * N + (N - 1) * strlen(", ")
  // char[N] should be presumed to be null-terminated & have capacity N - 1 as below.
  // This requires some care as there needs to be a variant of Delimited that takes a fixed string
  // delimiter as a template argument and implementing an override of operator* to distinguish T[N].
  static_assert(isToCharSequenceResultingInCappedCapacity<T(&)[N]>(),
      "This type doesn't have a capped capacity that's currently known.");
  static constexpr size_t capacity = N - 1;
};
template <typename T>
struct CappedCapacityOfToCharSequence_<SignalSafeCharSequence<T>>
    : public CappedCapacityOfToCharSequence_<T> {};

template <typename T>
static inline constexpr size_t cappedCapacityOfToCharSequence() {
  // Returns the maximum capacity required to serialize T to a string. Calling this on a type that
  // doesn't satisfy isToCharSequenceResultingInCappedCapacity will result in a compile error.
  return CappedCapacityOfToCharSequence_<T>::capacity;
}
}  // namespace _ (private)

template <typename T>
static constexpr inline bool isSignalSafeToCharSequence() {
  // Returns true if T is safe to stringify in a signal handler context. This means no memory
  // allocations and no invocations of methods that are not safe not use in a signal handler
  // context. These are manually annotated for each type and this must be done carefully to avoid
  // accidentally allowing types to be passed to strPreallocated. This can be invoked on a type T
  // that isn't itself valid to pass to toCharSequence, in which case this returns false.
  return _::IsSignalSafeToCharSequence_<T>::value;
}

template <typename T, typename U>
using PropagateSignalSafeCharSequence = Conditional<isSignalSafeToCharSequence<U>(),
    SignalSafeCharSequence<T>, T>;

CappedArray<char, sizeof(unsigned char) * 2 + 1> hex(unsigned char i);
CappedArray<char, sizeof(unsigned short) * 2 + 1> hex(unsigned short i);
CappedArray<char, sizeof(unsigned int) * 2 + 1> hex(unsigned int i);
CappedArray<char, sizeof(unsigned long) * 2 + 1> hex(unsigned long i);
CappedArray<char, sizeof(unsigned long long) * 2 + 1> hex(unsigned long long i);

template <typename... Params>
String str(Params&&... params) {
  // Magic function which builds a string from a bunch of arbitrary values.  Example:
  //     str(1, " / ", 2, " = ", 0.5)
  // returns:
  //     "1 / 2 = 0.5"
  // To teach `str` how to stringify a type, see `Stringifier`.

  return _::concat(toCharSequence(kj::fwd<Params>(params))...);
}

inline String str(String&& s) { return mv(s); }
// Overload to prevent redundant allocation.

template <typename T>
_::Delimited<T> delimited(T&& arr, kj::StringPtr delim);
// Use to stringify an array.

template <typename T>
String strArray(T&& arr, const char* delim) {
  size_t delimLen = strlen(delim);
  KJ_STACK_ARRAY(decltype(_::STR * arr[0]), pieces, kj::size(arr), 8, 32);
  size_t size = 0;
  for (size_t i = 0; i < kj::size(arr); i++) {
    if (i > 0) size += delimLen;
    pieces[i] = _::STR * arr[i];
    size += pieces[i].size();
  }

  String result = heapString(size);
  char* pos = result.begin();
  for (size_t i = 0; i < kj::size(arr); i++) {
    if (i > 0) {
      memcpy(pos, delim, delimLen);
      pos += delimLen;
    }
    pos = _::fill(pos, pieces[i]);
  }
  return result;
}

namespace _ {   // private
template <typename ... Args>
struct AssertSignalSafeToCharSequence_;

template <>
struct AssertSignalSafeToCharSequence_<>{};

template <typename T, typename ... Args>
struct AssertSignalSafeToCharSequence_<T, Args...>:
    public AssertSignalSafeToCharSequence_<Args...> {
  static_assert(isSignalSafeToCharSequence<T>(),
      "This type isn't safe to stringify in a signal handler.");
};
}  // namespace _ (private)

template <typename... Params>
StringPtr strPreallocated(ArrayPtr<char> buffer, Params&&... params) {
  // Like str() but writes into a preallocated buffer. If the buffer is not long enough, the result
  // is truncated (but still NUL-terminated).
  //
  // This can be used like:
  //
  //     char buffer[256];
  //     StringPtr text = strPreallocated(buffer, params...);
  //
  // This is useful for optimization. For use in a signal handler use strSignalSafe() instead.

  char* end = _::fillLimited(buffer.begin(), buffer.end() - 1,
      toCharSequence(kj::fwd<Params>(params))...);
  *end = '\0';
  return StringPtr(buffer.begin(), end);
}

template <typename... Params>
StringPtr KJ_ALWAYS_INLINE(strSignalSafe(ArrayPtr<char> buffer, Params&&... params)) {
  // Like strPreallocated this fills into a pre-allocated buffer to avoid calls to malloc. Unlike
  // strPreallocated, all of the stringifiers for the inputs must also be safe to stringify within
  // a signal handler. KJ guarantees signal safety when stringifying any built-in integer type
  // (but NOT floating-points), basic char/byte sequences (ArrayPtr<byte>, String, etc.), as well as
  // Array<T> as long as T can also be stringified safely. To safely stringify a delimited array,
  // you must use kj::delimited(arr, delim) rather than the deprecated kj::strArray(arr, delim).
  // These rules are enforced at compile-time for types that returns kj::SignalSafeCharSequence from
  // KJ_STRINGIFY.

  // Compile-time enforcement that the either we're not in a signal handler or the supplied types
  // follow the rules.
  constexpr _::AssertSignalSafeToCharSequence_<Params...> assertion KJ_UNUSED {};

  return strPreallocated(buffer, kj::fwd<Params>(params)...);
}

template <typename T, typename = decltype(toCharSequence(kj::instance<T&>()))>
inline PropagateSignalSafeCharSequence<_::Delimited<ArrayPtr<T>>, T>
operator*(const _::Stringifier&, ArrayPtr<T> arr) {
  return _::Delimited<ArrayPtr<T>>(arr, ", ");
}

template <typename T, typename = decltype(toCharSequence(kj::instance<const T&>()))>
inline PropagateSignalSafeCharSequence<_::Delimited<ArrayPtr<const T>>, const T>
operator*(const _::Stringifier&, const Array<T>& arr) {
  return _::Delimited<ArrayPtr<const T>>(arr, ", ");
}

#define KJ_STRINGIFY(...) operator*(::kj::_::Stringifier, __VA_ARGS__)
// Defines a stringifier for a custom type.  Example:
//
//    class Foo {...};
//    inline StringPtr KJ_STRINGIFY(const Foo& foo) { return foo.name(); }
//      // or perhaps
//    inline String KJ_STRINGIFY(const Foo& foo) { return kj::str(foo.fld1(), ",", foo.fld2()); }
//
// This allows Foo to be passed to str().
//
// The function should be declared either in the same namespace as the target type or in the global
// namespace.  It can return any type which is an iterable container of chars.

// =======================================================================================
// Inline implementation details.

inline StringPtr::StringPtr(const String& value): content(value.cStr(), value.size() + 1) {}

inline constexpr StringPtr::operator ArrayPtr<const char>() const {
  return ArrayPtr<const char>(content.begin(), content.size() - 1);
}

inline constexpr ArrayPtr<const char> StringPtr::asArray() const {
  return ArrayPtr<const char>(content.begin(), content.size() - 1);
}

inline bool StringPtr::operator==(const StringPtr& other) const {
  return content.size() == other.content.size() &&
      memcmp(content.begin(), other.content.begin(), content.size() - 1) == 0;
}

inline bool StringPtr::operator<(const StringPtr& other) const {
  bool shorter = content.size() < other.content.size();
  int cmp = memcmp(content.begin(), other.content.begin(),
                   shorter ? content.size() : other.content.size());
  return cmp < 0 || (cmp == 0 && shorter);
}

inline StringPtr StringPtr::slice(size_t start) const {
  return StringPtr(content.slice(start, content.size()));
}
inline ArrayPtr<const char> StringPtr::slice(size_t start, size_t end) const {
  return content.slice(start, end);
}

inline bool StringPtr::startsWith(const StringPtr& other) const {
  return other.content.size() <= content.size() &&
      memcmp(content.begin(), other.content.begin(), other.size()) == 0;
}
inline bool StringPtr::endsWith(const StringPtr& other) const {
  return other.content.size() <= content.size() &&
      memcmp(end() - other.size(), other.content.begin(), other.size()) == 0;
}

inline Maybe<size_t> StringPtr::findFirst(char c) const {
  const char* pos = reinterpret_cast<const char*>(memchr(content.begin(), c, size()));
  if (pos == nullptr) {
    return nullptr;
  } else {
    return pos - content.begin();
  }
}

inline Maybe<size_t> StringPtr::findLast(char c) const {
  for (size_t i = size(); i > 0; --i) {
    if (content[i-1] == c) {
      return i-1;
    }
  }
  return nullptr;
}

inline String::operator ArrayPtr<char>() {
  return content == nullptr ? ArrayPtr<char>(nullptr) : content.slice(0, content.size() - 1);
}
inline String::operator ArrayPtr<const char>() const {
  return content == nullptr ? ArrayPtr<const char>(nullptr) : content.slice(0, content.size() - 1);
}

inline ArrayPtr<char> String::asArray() {
  return content == nullptr ? ArrayPtr<char>(nullptr) : content.slice(0, content.size() - 1);
}
inline ArrayPtr<const char> String::asArray() const {
  return content == nullptr ? ArrayPtr<const char>(nullptr) : content.slice(0, content.size() - 1);
}

inline const char* String::cStr() const { return content == nullptr ? "" : content.begin(); }

inline size_t String::size() const { return content == nullptr ? 0 : content.size() - 1; }

inline char String::operator[](size_t index) const { return content[index]; }
inline char& String::operator[](size_t index) { return content[index]; }

inline char* String::begin() { return content == nullptr ? nullptr : content.begin(); }
inline char* String::end() { return content == nullptr ? nullptr : content.end() - 1; }
inline const char* String::begin() const { return content == nullptr ? nullptr : content.begin(); }
inline const char* String::end() const { return content == nullptr ? nullptr : content.end() - 1; }

inline String::String(char* value, size_t size, const ArrayDisposer& disposer)
    : content(value, size + 1, disposer) {
  KJ_IREQUIRE(value[size] == '\0', "String must be NUL-terminated.");
}

inline String::String(Array<char> buffer): content(kj::mv(buffer)) {
  KJ_IREQUIRE(content.size() > 0 && content.back() == '\0', "String must be NUL-terminated.");
}

inline String heapString(const char* value) {
  return heapString(value, strlen(value));
}
inline String heapString(StringPtr value) {
  return heapString(value.begin(), value.size());
}
inline String heapString(const String& value) {
  return heapString(value.begin(), value.size());
}
inline String heapString(ArrayPtr<const char> value) {
  return heapString(value.begin(), value.size());
}

namespace _ {  // private

template <typename T>
class Delimited {
public:
  Delimited(T array, kj::StringPtr delimiter)
      : array(kj::fwd<T>(array)), delimiter(delimiter) {}

  // TODO(someday): In theory we should support iteration as a character sequence, but the iterator
  //   will be pretty complicated.

  size_t size() {
    ensureStringifiedInitialized();

    size_t result = 0;
    bool first = true;
    for (auto& e: stringified) {
      if (first) {
        first = false;
      } else {
        result += delimiter.size();
      }
      result += e.size();
    }
    return result;
  }

  char* flattenTo(char* __restrict__ target) {
    ensureStringifiedInitialized();

    bool first = true;
    for (auto& elem: stringified) {
      if (first) {
        first = false;
      } else {
        target = fill(target, delimiter);
      }
      target = fill(target, elem);
    }
    return target;
  }

  char* flattenTo(char* __restrict__ target, char* limit) {
    // This is called in the strPreallocated(). We want to avoid allocation. size() will not have
    // been called in this case, so hopefully `stringified` is still uninitialized. We will
    // stringify each item and immediately use it.
    bool first = true;
    for (auto&& elem: array) {
      if (target == limit) return target;
      if (first) {
        first = false;
      } else {
        target = fillLimited(target, limit, delimiter);
      }
      target = fillLimited(target, limit, kj::toCharSequence(elem));
    }
    return target;
  }

private:
  typedef decltype(toCharSequence(*instance<T>().begin())) StringifiedItem;
  T array;
  kj::StringPtr delimiter;
  Array<StringifiedItem> stringified;

  void ensureStringifiedInitialized() {
    if (array.size() > 0 && stringified.size() == 0) {
      stringified = KJ_MAP(e, array) { return toCharSequence(e); };
    }
  }
};

template <typename T, typename... Rest>
char* fill(char* __restrict__ target, Delimited<T>&& first, Rest&&... rest) {
  target = first.flattenTo(target);
  return fill(target, kj::fwd<Rest>(rest)...);
}
template <typename T, typename... Rest>
char* fillLimited(char* __restrict__ target, char* limit, Delimited<T>&& first, Rest&&... rest) {
  target = first.flattenTo(target, limit);
  return fillLimited(target, limit, kj::fwd<Rest>(rest)...);
}
template <typename T, typename... Rest>
char* fill(char* __restrict__ target, Delimited<T>& first, Rest&&... rest) {
  target = first.flattenTo(target);
  return fill(target, kj::fwd<Rest>(rest)...);
}
template <typename T, typename... Rest>
char* fillLimited(char* __restrict__ target, char* limit, Delimited<T>& first, Rest&&... rest) {
  target = first.flattenTo(target, limit);
  return fillLimited(target, limit, kj::fwd<Rest>(rest)...);
}

template <typename T>
inline SignalSafeCharSequence<Delimited<T>> KJ_STRINGIFY(Delimited<T>&& delimited) {
  return kj::mv(delimited);
}
template <typename T>
inline const SignalSafeCharSequence<Delimited<T>>& KJ_STRINGIFY(const Delimited<T>& delimited) {
  return delimited;
}

}  // namespace _ (private)

template <typename T>
_::Delimited<T> delimited(T&& arr, kj::StringPtr delim) {
  return _::Delimited<T>(kj::fwd<T>(arr), delim);
}

}  // namespace kj

constexpr kj::StringPtr operator "" _kj(const char* str, size_t n) {
  return kj::StringPtr(kj::ArrayPtr<const char>(str, n + 1));
};

KJ_END_HEADER
