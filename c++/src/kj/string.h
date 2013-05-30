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

inline ArrayPtr<const char> stringPtr(const char* text) {
  return arrayPtr(text, strlen(text));
}

// =======================================================================================
// String -- Just a NUL-terminated Array<char>.

class String {
public:
  String() = default;
  String(const char* value);
  String(const char* value, size_t length);

  inline ArrayPtr<char> asArray();
  inline ArrayPtr<const char> asArray() const;
  inline const char* cStr() const { return content == nullptr ? "" : content.begin(); }

  inline size_t size() const { return content == nullptr ? 0 : content.size() - 1; }

  inline char* begin() { return content == nullptr ? nullptr : content.begin(); }
  inline char* end() { return content == nullptr ? nullptr : content.end() - 1; }
  inline const char* begin() const { return content == nullptr ? nullptr : content.begin(); }
  inline const char* end() const { return content == nullptr ? nullptr : content.end() - 1; }

private:
  Array<char> content;
};

inline ArrayPtr<char> String::asArray() {
  return content == nullptr ? ArrayPtr<char>(nullptr) : content.slice(0, content.size() - 1);
}
inline ArrayPtr<const char> String::asArray() const {
  return content == nullptr ? ArrayPtr<char>(nullptr) : content.slice(0, content.size() - 1);
}

}  // namespace kj

#endif  // KJ_STRING_H_
