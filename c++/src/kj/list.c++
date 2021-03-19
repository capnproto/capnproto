// Copyright (c) 2021 Cloudflare, Inc. and contributors
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

#include "list.h"
#include "debug.h"

namespace kj {
namespace _ {

void throwDoubleAdd() {
  kj::throwFatalException(KJ_EXCEPTION(FAILED,
      "tried to add element to kj::List but the element is already in a list"));
}
void throwRemovedNotPresent() {
  kj::throwFatalException(KJ_EXCEPTION(FAILED,
      "tried to remove element from kj::List but the element is not in a list"));
}
void throwRemovedWrongList() {
  kj::throwFatalException(KJ_EXCEPTION(FAILED,
      "tried to remove element from kj::List but the element is in a different list"));
}
void throwDestroyedWhileInList() {
  kj::throwFatalException(KJ_EXCEPTION(FAILED,
      "destroyed object that is still in a kj::List"));
}

}  // namespace _
}  // namespace kj
