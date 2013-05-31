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

#ifndef KJ_STRING_H_
#define KJ_STRING_H_

#include "array.h"
#include <string.h>

namespace kj {

class StringPtr;
class String;

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
  inline StringPtr(const char* value): content(value, strlen(value) + 1) {}
  inline StringPtr(const String& value);

  inline operator ArrayPtr<const char>() const;
  inline ArrayPtr<const char> asArray() const;
  // Result does not include NUL terminator.

  inline const char* cStr() const { return content.begin(); }
  // Returns NUL-terminated string.

  inline size_t size() const { return content.size() - 1; }
  // Result does not include NUL terminator.

  inline char operator[](size_t index) const { return content[index]; }

  inline const char* begin() const { return content.begin(); }
  inline const char* end() const { return content.end() - 1; }

  inline bool operator==(decltype(nullptr)) const { return content.size() <= 1; }
  inline bool operator!=(decltype(nullptr)) const { return content.size() > 1; }

  inline bool operator==(StringPtr other) const;
  inline bool operator!=(StringPtr other) const { return !(*this == other); }

  inline StringPtr slice(size_t start) const;
  inline ArrayPtr<const char> slice(size_t start, size_t end) const;
  // A string slice is only NUL-terminated if it is a suffix, so slice() has a one-parameter
  // version that assumes end = size().

private:
  inline StringPtr(ArrayPtr<const char> content): content(content) {}

  ArrayPtr<const char> content;
};

inline bool operator==(const char* a, const StringPtr& b) { return b == a; }
inline bool operator!=(const char* a, const StringPtr& b) { return b != a; }

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

  inline operator ArrayPtr<char>();
  inline operator ArrayPtr<const char>() const;
  inline ArrayPtr<char> asArray();
  inline ArrayPtr<const char> asArray() const;
  // Result does not include NUL terminator.

  inline const char* cStr() const;

  inline size_t size() const;
  // Result does not include NUL terminator.

  inline char operator[](size_t index) const;
  inline char& operator[](size_t index);

  inline char* begin();
  inline char* end();
  inline const char* begin() const;
  inline const char* end() const;

  inline bool operator==(decltype(nullptr)) const { return content.size() <= 1; }
  inline bool operator!=(decltype(nullptr)) const { return content.size() > 1; }

  inline bool operator==(StringPtr other) const { return StringPtr(*this) == other; }
  inline bool operator!=(StringPtr other) const { return !(*this == other); }

private:
  Array<char> content;
};

inline bool operator==(const char* a, const String& b) { return b == a; }
inline bool operator!=(const char* a, const String& b) { return b != a; }

String heapString(size_t size);
// Allocate a String of the given size on the heap, not including NUL terminator.  The NUL
// terminator will be initialized automatically but the rest of the content is not initialized.

String heapString(const char* value);
String heapString(const char* value, size_t size);
String heapString(StringPtr value);
String heapString(ArrayPtr<const char> value);
// Allocates a copy of the given value on the heap.

// =======================================================================================
// Inline implementation details.

inline StringPtr::StringPtr(const String& value): content(value.begin(), value.size() + 1) {}

inline StringPtr::operator ArrayPtr<const char>() const {
  return content.slice(0, content.size() - 1);
}

inline ArrayPtr<const char> StringPtr::asArray() const {
  return content.slice(0, content.size() - 1);
}

inline bool StringPtr::operator==(StringPtr other) const {
  return content.size() == other.content.size() &&
      memcmp(content.begin(), other.content.begin(), content.size() - 1) == 0;
}

inline StringPtr StringPtr::slice(size_t start) const {
  return StringPtr(content.slice(start, content.size()));
}
inline ArrayPtr<const char> StringPtr::slice(size_t start, size_t end) const {
  return content.slice(start, end);
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
  KJ_INLINE_DPRECOND(value[size] == '\0', "String must be NUL-terminated.");
}

inline String heapString(const char* value) {
  return heapString(value, strlen(value));
}
inline String heapString(StringPtr value) {
  return heapString(value.begin(), value.size());
}
inline String heapString(ArrayPtr<const char> value) {
  return heapString(value.begin(), value.size());
}

}  // namespace kj

#endif  // KJ_STRING_H_
