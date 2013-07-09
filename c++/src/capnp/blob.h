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

#ifndef CAPNP_BLOB_H_
#define CAPNP_BLOB_H_

#include <kj/common.h>
#include <kj/string.h>
#include "common.h"
#include <string.h>

namespace capnp {

struct Data {
  Data() = delete;
  class Reader;
  class Builder;
};

struct Text {
  Text() = delete;
  class Reader;
  class Builder;
};

class Data::Reader: public kj::ArrayPtr<const byte> {
  // Points to a blob of bytes.  The usual Reader rules apply -- Data::Reader behaves like a simple
  // pointer which does not own its target, can be passed by value, etc.

public:
  typedef Data Reads;

  Reader() = default;
  inline Reader(decltype(nullptr)): ArrayPtr<const byte>(nullptr) {}
  inline Reader(const byte* value, size_t size): ArrayPtr<const byte>(value, size) {}
  inline Reader(const kj::Array<const byte>& value): ArrayPtr<const byte>(value) {}
  inline Reader(const ArrayPtr<const byte>& value): ArrayPtr<const byte>(value) {}
  inline Reader(const kj::Array<byte>& value): ArrayPtr<const byte>(value) {}
  inline Reader(const ArrayPtr<byte>& value): ArrayPtr<const byte>(value) {}
};

class Text::Reader: public kj::StringPtr {
  // Like Data::Reader, but points at NUL-terminated UTF-8 text.  The NUL terminator is not counted
  // in the size but must be present immediately after the last byte.
  //
  // Text::Reader's interface contract is that its data MUST be NUL-terminated.  The producer of
  // the Text::Reader must guarantee this, so that the consumer need not check.  The data SHOULD
  // also be valid UTF-8, but this is NOT guaranteed -- the consumer must verify if it cares.

public:
  typedef Text Reads;

  Reader() = default;
  inline Reader(decltype(nullptr)): StringPtr(nullptr) {}
  inline Reader(const char* value): StringPtr(value) {}
  inline Reader(const char* value, size_t size): StringPtr(value, size) {}
  inline Reader(const kj::String& value): StringPtr(value) {}
  inline Reader(const StringPtr& value): StringPtr(value) {}
};

class Data::Builder: public kj::ArrayPtr<byte> {
  // Like Data::Reader except the pointers aren't const.

public:
  typedef Data Builds;

  Builder() = default;
  inline Builder(decltype(nullptr)): ArrayPtr<byte>(nullptr) {}
  inline Builder(byte* value, size_t size): ArrayPtr<byte>(value, size) {}
  inline Builder(kj::Array<byte>& value): ArrayPtr<byte>(value) {}
  inline Builder(ArrayPtr<byte>& value): ArrayPtr<byte>(value) {}

  inline Data::Reader asReader() const { return Data::Reader(*this); }
};

class Text::Builder: public kj::DisallowConstCopy {
  // Basically identical to kj::StringPtr, except that the contents are non-const.

public:
  inline Builder(): content(nulstr, 1) {}
  inline Builder(decltype(nullptr)): content(nulstr, 1) {}
  inline Builder(char* value): content(value, strlen(value) + 1) {}
  inline Builder(char* value, size_t size): content(value, size + 1) {
    KJ_IREQUIRE(value[size] == '\0', "StringPtr must be NUL-terminated.");
  }

  inline Reader asReader() const { return Reader(content.begin(), content.size() - 1); }

  inline operator kj::ArrayPtr<char>();
  inline kj::ArrayPtr<char> asArray();
  inline operator kj::ArrayPtr<const char>() const;
  inline kj::ArrayPtr<const char> asArray() const;
  // Result does not include NUL terminator.

  inline operator kj::StringPtr() const;
  inline kj::StringPtr asString() const;

  inline const char* cStr() const { return content.begin(); }
  // Returns NUL-terminated string.

  inline size_t size() const { return content.size() - 1; }
  // Result does not include NUL terminator.

  inline char operator[](size_t index) const { return content[index]; }
  inline char& operator[](size_t index) { return content[index]; }

  inline char* begin() { return content.begin(); }
  inline char* end() { return content.end() - 1; }
  inline const char* begin() const { return content.begin(); }
  inline const char* end() const { return content.end() - 1; }

  inline bool operator==(decltype(nullptr)) const { return content.size() <= 1; }
  inline bool operator!=(decltype(nullptr)) const { return content.size() > 1; }

  inline bool operator==(Builder other) const { return asString() == other.asString(); }
  inline bool operator!=(Builder other) const { return asString() == other.asString(); }
  inline bool operator< (Builder other) const { return asString() <  other.asString(); }
  inline bool operator> (Builder other) const { return asString() >  other.asString(); }
  inline bool operator<=(Builder other) const { return asString() <= other.asString(); }
  inline bool operator>=(Builder other) const { return asString() >= other.asString(); }

  inline kj::StringPtr slice(size_t start) const;
  inline kj::ArrayPtr<const char> slice(size_t start, size_t end) const;
  inline Builder slice(size_t start);
  inline kj::ArrayPtr<char> slice(size_t start, size_t end);
  // A string slice is only NUL-terminated if it is a suffix, so slice() has a one-parameter
  // version that assumes end = size().

private:
  inline explicit Builder(kj::ArrayPtr<char> content): content(content) {}

  kj::ArrayPtr<char> content;

  static char nulstr[1];
};

inline kj::StringPtr KJ_STRINGIFY(Text::Builder builder) {
  return builder.asString();
}

inline bool operator==(const char* a, const Text::Builder& b) { return a == b.asString(); }
inline bool operator!=(const char* a, const Text::Builder& b) { return a != b.asString(); }

inline Text::Builder::operator kj::StringPtr() const {
  return kj::StringPtr(content.begin(), content.size() - 1);
}

inline kj::StringPtr Text::Builder::asString() const {
  return kj::StringPtr(content.begin(), content.size() - 1);
}

inline Text::Builder::operator kj::ArrayPtr<char>() {
  return content.slice(0, content.size() - 1);
}

inline kj::ArrayPtr<char> Text::Builder::asArray() {
  return content.slice(0, content.size() - 1);
}

inline Text::Builder::operator kj::ArrayPtr<const char>() const {
  return content.slice(0, content.size() - 1);
}

inline kj::ArrayPtr<const char> Text::Builder::asArray() const {
  return content.slice(0, content.size() - 1);
}

inline kj::StringPtr Text::Builder::slice(size_t start) const {
  return asReader().slice(start);
}
inline kj::ArrayPtr<const char> Text::Builder::slice(size_t start, size_t end) const {
  return content.slice(start, end);
}

inline Text::Builder Text::Builder::slice(size_t start) {
  return Text::Builder(content.slice(start, content.size()));
}
inline kj::ArrayPtr<char> Text::Builder::slice(size_t start, size_t end) {
  return content.slice(start, end);
}

}  // namespace capnp

#endif  // CAPNP_BLOB_H_
