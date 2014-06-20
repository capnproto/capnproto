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

#include "char.h"
#include "../debug.h"
#include <stdlib.h>

namespace kj {
namespace parse {

namespace _ {  // private

double ParseFloat::operator()(const Array<char>& digits,
                              const Maybe<Array<char>>& fraction,
                              const Maybe<Tuple<Maybe<char>, Array<char>>>& exponent) const {
  size_t bufSize = digits.size();
  KJ_IF_MAYBE(f, fraction) {
    bufSize += 1 + f->size();
  }
  KJ_IF_MAYBE(e, exponent) {
    bufSize += 1 + (get<0>(*e) != nullptr) + get<1>(*e).size();
  }

  KJ_STACK_ARRAY(char, buf, bufSize + 1, 128, 128);

  char* pos = buf.begin();
  memcpy(pos, digits.begin(), digits.size());
  pos += digits.size();
  KJ_IF_MAYBE(f, fraction) {
    *pos++ = '.';
    memcpy(pos, f->begin(), f->size());
    pos += f->size();
  }
  KJ_IF_MAYBE(e, exponent) {
    *pos++ = 'e';
    KJ_IF_MAYBE(sign, get<0>(*e)) {
      *pos++ = *sign;
    }
    memcpy(pos, get<1>(*e).begin(), get<1>(*e).size());
    pos += get<1>(*e).size();
  }

  *pos++ = '\0';
  KJ_DASSERT(pos == buf.end());

  return strtod(buf.begin(), nullptr);
}

}  // namespace _ (private)

}  // namespace parse
}  // namespace kj
