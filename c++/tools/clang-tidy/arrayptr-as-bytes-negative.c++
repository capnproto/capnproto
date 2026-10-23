// Copyright (c) 2026 Cloudflare, Inc. and contributors
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

#include <kj/array.h>

namespace other {
inline kj::ArrayPtr<int> arrayPtr(int* ptr, size_t n) { return kj::arrayPtr(ptr, n); }
}

kj::ArrayPtr<int> getPtr(int* ptr, size_t n) { return kj::arrayPtr(ptr, n); }

void notAsBytes(kj::ArrayPtr<int> existing, kj::Array<int>& array, int value, int* ptr,
                size_t n) {
  // The preferred form.
  kj::asBytes(ptr, n);
  kj::asBytes(value);

  // asBytes() on an existing ArrayPtr.
  existing.asBytes();

  // asBytes() on an ArrayPtr not constructed in place.
  getPtr(ptr, n).asBytes();
  array.asPtr().asBytes();
  other::arrayPtr(ptr, n).asBytes();

  // kj::Array::asBytes().
  array.asBytes();

  // Other methods on a freshly constructed ArrayPtr.
  kj::arrayPtr(ptr, n).first(1);
}
