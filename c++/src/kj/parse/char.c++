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
