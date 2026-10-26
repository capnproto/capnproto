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

#include <kj/refcount.h>

struct Foo: public kj::Refcounted {
  int value = 0;
};

struct AtomicFoo: public kj::AtomicRefcounted {
  int value = 0;
};

void deref(kj::Rc<Foo>& rc, const kj::Rc<Foo>& constRc, kj::Arc<AtomicFoo>& arc,
           kj::Rc<int>& rcInt) {
  *rc.get();  // CHECK-MATCH
  *(rc.get());  // CHECK-MATCH
  *constRc.get();  // CHECK-MATCH
  *arc.get();  // CHECK-MATCH
  *rcInt.get();  // CHECK-MATCH
  int i = *kj::rc<int>(1).get();  // CHECK-MATCH
  Foo& foo = *rc.get();  // CHECK-MATCH
  (*rc.get()).value = 1;  // CHECK-MATCH

  // Non-matching calls in a positive test must not be reported.
  *rc;
}
