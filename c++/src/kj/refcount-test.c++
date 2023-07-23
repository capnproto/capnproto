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

#include "refcount.h"
#include <kj/compat/gtest.h>

namespace kj {

struct SetTrueInDestructor: public Refcounted {
  SetTrueInDestructor(bool* ptr): ptr(ptr) {}
  ~SetTrueInDestructor() { *ptr = true; }

  bool* ptr;
};

TEST(Refcount, Basic) {
  bool b = false;
  Own<SetTrueInDestructor> ref1 = kj::refcounted<SetTrueInDestructor>(&b);
  EXPECT_FALSE(ref1->isShared());
  Own<SetTrueInDestructor> ref2 = kj::addRef(*ref1);
  EXPECT_TRUE(ref1->isShared());
  Own<SetTrueInDestructor> ref3 = kj::addRef(*ref2);
  EXPECT_TRUE(ref1->isShared());

  EXPECT_FALSE(b);
  ref1 = Own<SetTrueInDestructor>();
  EXPECT_TRUE(ref2->isShared());
  EXPECT_FALSE(b);
  ref3 = Own<SetTrueInDestructor>();
  EXPECT_FALSE(ref2->isShared());
  EXPECT_FALSE(b);
  ref2 = Own<SetTrueInDestructor>();
  EXPECT_TRUE(b);

#if defined(KJ_DEBUG) && !KJ_NO_EXCEPTIONS
  b = false;
  SetTrueInDestructor obj(&b);
  EXPECT_ANY_THROW(addRef(obj));
#endif
}

struct SetTrueInDestructor2 {
  // Like above but doesn't inherit Refcounted.

  SetTrueInDestructor2(bool* ptr): ptr(ptr) {}
  ~SetTrueInDestructor2() { *ptr = true; }

  bool* ptr;
};

KJ_TEST("RefcountedWrapper") {
  {
    bool b = false;
    Own<RefcountedWrapper<SetTrueInDestructor2>> w = refcountedWrapper<SetTrueInDestructor2>(&b);
    KJ_EXPECT(!b);

    Own<SetTrueInDestructor2> ref1 = w->addWrappedRef();
    Own<SetTrueInDestructor2> ref2 = w->addWrappedRef();

    KJ_EXPECT(ref1.get() == &w->getWrapped());
    KJ_EXPECT(ref1.get() == ref2.get());

    KJ_EXPECT(!b);

    w = nullptr;
    ref1 = nullptr;

    KJ_EXPECT(!b);

    ref2 = nullptr;

    KJ_EXPECT(b);
  }

  // Wrap Own<T>.
  {
    bool b = false;
    Own<RefcountedWrapper<Own<SetTrueInDestructor2>>> w =
        refcountedWrapper<SetTrueInDestructor2>(kj::heap<SetTrueInDestructor2>(&b));
    KJ_EXPECT(!b);

    Own<SetTrueInDestructor2> ref1 = w->addWrappedRef();
    Own<SetTrueInDestructor2> ref2 = w->addWrappedRef();

    KJ_EXPECT(ref1.get() == &w->getWrapped());
    KJ_EXPECT(ref1.get() == ref2.get());

    KJ_EXPECT(!b);

    w = nullptr;
    ref1 = nullptr;

    KJ_EXPECT(!b);

    ref2 = nullptr;

    KJ_EXPECT(b);
  }

  // Try wrapping an `int` to really demonstrate the wrapped type can be anything.
  {
    Own<RefcountedWrapper<int>> w = refcountedWrapper<int>(123);
    int* ptr = &w->getWrapped();
    KJ_EXPECT(*ptr == 123);

    Own<int> ref1 = w->addWrappedRef();
    Own<int> ref2 = w->addWrappedRef();

    KJ_EXPECT(ref1.get() == ptr);
    KJ_EXPECT(ref2.get() == ptr);

    w = nullptr;
    ref1 = nullptr;

    KJ_EXPECT(*ref2 == 123);
  }
}

}  // namespace kj
