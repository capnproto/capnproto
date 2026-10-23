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

#include <kj/memory.h>
#include <kj/refcount.h>

struct Foo: public kj::Refcounted {
  int value = 0;
};

struct AtomicFoo: public kj::AtomicRefcounted {
  int value = 0;
};

void notDeref(kj::Rc<Foo>& rc, kj::Arc<AtomicFoo>& arc, kj::Own<Foo>& own, Foo* ptr) {
  // The preferred form.
  *rc;
  *arc;
  Foo& foo = *rc;

  // get() without a dereference.
  Foo* raw = rc.get();
  rc.get()->value = 1;
  arc.get()->value;
  if (rc.get() == nullptr) {}

  // Dereferencing get() of other smart pointers.
  *own.get();

  // Dereferencing raw pointers.
  *ptr;
}
