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

#ifndef CAPNPROTO_BLOB_H_
#define CAPNPROTO_BLOB_H_

#include "macros.h"
#include "type-safety.h"
#include <string.h>

namespace capnproto {

struct Data {
  class Reader;
  class Builder;
};

struct Text {
  class Reader;
  class Builder;
};

template <size_t size>
struct InlineData: public Data {};
// Alias for Data used specifically for InlineData fields.  This primarily exists so that
// List<InlineData<n>> can be specialized.

class Data::Reader {
  // Points to a blob of bytes.  The usual Reader rules apply -- Data::Reader behaves like a simple
  // pointer which does not own its target, can be passed by value, etc.
  //
  // Data::Reader can be implicitly converted to any type which has a constructor that takes a
  // (const char*, size) pair, and can be implicitly constructed from any type that has data()
  // and size() methods.  Many types follow this pattern, such as std::string.  Data::Reader can
  // also be implicitly constructed from a NUL-terminated char*.

public:
  typedef Data Reads;

  inline Reader(): bytes(nullptr), size_(0) {}
  inline Reader(const char* bytes): bytes(bytes), size_(strlen(bytes)) {}
  inline Reader(char* bytes): bytes(bytes), size_(strlen(bytes)) {}
  inline Reader(const char* bytes, uint size): bytes(bytes), size_(size) {}

  template <typename T>
  inline Reader(const T& other): bytes(other.data()), size_(other.size()) {}
  // Primarily intended for converting from std::string.

  template <typename T>
  inline T as() const { return T(bytes, size_); }
  // Explicitly converts to the desired type, which must have a (const char*, size) constructor.

  inline const char* data() const { return bytes; }
  inline uint size() const { return size_; }
  inline const char operator[](uint index) const { return bytes[index]; }

  inline Reader slice(uint start, uint end) const {
    CAPNPROTO_INLINE_DPRECOND(start <= end && end <= size_, "Out-of-bounds slice.");
    return Reader(bytes + start, end - start);
  }

  inline bool operator==(const Reader& other) const {
    return size_ == other.size_ && memcmp(bytes, other.bytes, size_) == 0;
  }
  inline bool operator<=(const Reader& other) const {
    bool shorter = size_ <= other.size_;
    int cmp = memcmp(bytes, other.bytes, shorter ? size_ : other.size_);
    return cmp < 0 ? true : cmp > 0 ? false : size_ <= other.size_;
  }
  inline bool operator!=(const Reader& other) const { return !(*this == other); }
  inline bool operator>=(const Reader& other) const { return other <= *this; }
  inline bool operator< (const Reader& other) const { return !(other <= *this); }
  inline bool operator> (const Reader& other) const { return !(*this <= other); }

  inline const char* begin() const { return bytes; }
  inline const char* end() const { return bytes + size_; }

private:
  const char* bytes;
  uint size_;
};

class Text::Reader: public Data::Reader {
  // Like Data::Reader, but points at NUL-terminated UTF-8 text.  The NUL terminator is not counted
  // in the size but must be present immediately after the last byte.
  //
  // Text::Reader's interface contract is that its data MUST be NUL-terminated.  The producer of
  // the Text::Reader must guarantee this, so that the consumer need not check.  The data SHOULD
  // also be valid UTF-8, but this is NOT guaranteed -- the consumer must verify if it cares.
  //
  // Text::Reader can be implicitly converted to and from const char*.  Additionally, it can be
  // implicitly converted to any type that can be constructed from a (const char*, size) pair, as
  // well as from any type which has c_str() and size() methods.  Many types follow this pattern,
  // such as std::string.

public:
  typedef Text Reads;

  inline Reader(): Data::Reader("", 0) {}
  inline Reader(const char* text): Data::Reader(text, strlen(text)) {}
  inline Reader(char* text): Data::Reader(text, strlen(text)) {}
  inline Reader(const char* text, uint size): Data::Reader(text, size) {
    CAPNPROTO_INLINE_DPRECOND(text[size] == '\0', "Text must be NUL-terminated.");
  }

  template <typename T>
  inline Reader(const T& other): Data::Reader(other.c_str(), other.size()) {
    // Primarily intended for converting from std::string.
    CAPNPROTO_INLINE_DPRECOND(data()[size()] == '\0', "Text must be NUL-terminated.");
  }

  inline const char* c_str() const { return data(); }
  inline operator const char*() const { return data(); }
};

class Data::Builder {
  // Like Data::Reader except the pointers aren't const, and it can't be implicitly constructed from
  // other types.

public:
  typedef Data Builds;

  inline Builder(): bytes(nullptr), size_(0) {}
  inline Builder(char* bytes, uint size): bytes(bytes), size_(size) {}

  template <typename T>
  inline operator T() const { return T(bytes, size_); }
  // Primarily intended for converting to std::string.

  inline operator Data::Reader() const { return Data::Reader(bytes, size_); }

  template <typename T>
  inline T as() const { return T(bytes, size_); }
  // Explicitly converts to the desired type, which must have a (char*, size) constructor.

  inline char* data() const { return bytes; }
  inline uint size() const { return size_; }
  inline char& operator[](uint index) const { return bytes[index]; }

  inline Builder slice(uint start, uint end) const {
    CAPNPROTO_INLINE_DPRECOND(start <= end && end <= size_, "Out-of-bounds slice.");
    return Builder(bytes + start, end - start);
  }

  inline bool operator==(const Builder& other) const {
    return size_ == other.size_ && memcmp(bytes, other.bytes, size_) == 0;
  }
  inline bool operator<=(const Builder& other) const {
    bool shorter = size_ <= other.size_;
    int cmp = memcmp(bytes, other.bytes, shorter ? size_ : other.size_);
    return cmp < 0 ? true : cmp > 0 ? false : size_ <= other.size_;
  }
  inline bool operator!=(const Builder& other) const { return !(*this == other); }
  inline bool operator>=(const Builder& other) const { return other <= *this; }
  inline bool operator< (const Builder& other) const { return !(other <= *this); }
  inline bool operator> (const Builder& other) const { return !(*this <= other); }

  template <typename T>
  inline void copyFrom(const T& other) const {
    CAPNPROTO_INLINE_DPRECOND(size() == other.size(), "Sizes must match to copy.");
    memcpy(bytes, other.data(), other.size());
  }
  inline void copyFrom(const void* other) const {
    memcpy(bytes, other, size_);
  }

  inline char* begin() const { return bytes; }
  inline char* end() const { return bytes + size_; }

private:
  char* bytes;
  uint size_;
};

class Text::Builder: public Data::Builder {
  // Like Text::Reader except the pointers aren't const, and it can't be implicitly constructed from
  // other types.  The Text::Builder automatically initializes the NUL terminator at construction,
  // so it is never necessary for the caller to do so.

public:
  typedef Text Builds;

  inline Builder(): Data::Builder(nulstr, 0) {}
  inline Builder(char* text, uint size): Data::Builder(text, size) { text[size] = '\0'; }

  inline char* c_str() const { return data(); }
  inline operator char*() const { return data(); }
  inline operator const char*() const { return data(); }

private:
  static char nulstr[1];
};

inline bool operator==(const char* a, Data::Reader  b) { return Data::Reader(a) == b; }
inline bool operator==(const char* a, Data::Builder b) { return Data::Reader(a) == (Data::Reader)b; }
inline bool operator==(Data::Reader a, Data::Builder b) { return a == (Data::Reader)b; }
inline bool operator==(Data::Builder a, Data::Reader b) { return (Data::Reader)a == b; }

template <typename T>
T& operator<<(T& os, Data::Reader value) { return os.write(value.data(), value.size()); }
template <typename T>
T& operator<<(T& os, Data::Builder value) { return os.write(value.data(), value.size()); }
template <typename T>
T& operator<<(T& os, Text::Reader value) { return os.write(value.data(), value.size()); }
template <typename T>
T& operator<<(T& os, Text::Builder value) { return os.write(value.data(), value.size()); }

}  // namespace capnproto

#endif  // CAPNPROTO_BLOB_H_
