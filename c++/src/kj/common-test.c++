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

#include "common.h"
#include <gtest/gtest.h>

namespace kj {
namespace {

TEST(Common, Maybe) {
  {
    Maybe<int> m = 123;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(123, *v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(123, *v);
    } else {
      ADD_FAILURE();
    }
  }

  {
    Maybe<int> m = nullptr;
    EXPECT_TRUE(m == nullptr);
    EXPECT_FALSE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    KJ_IF_MAYBE(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
  }

  int i = 234;
  {
    Maybe<int&> m = i;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
  }

  {
    Maybe<int&> m = nullptr;
    EXPECT_TRUE(m == nullptr);
    EXPECT_FALSE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    KJ_IF_MAYBE(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
  }

  {
    Maybe<int&> m = &i;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_EQ(&i, v);
    } else {
      ADD_FAILURE();
    }
  }

  {
    Maybe<int&> m = upcast<int*>(nullptr);
    EXPECT_TRUE(m == nullptr);
    EXPECT_FALSE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    KJ_IF_MAYBE(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
  }

  {
    Maybe<int> m = &i;
    EXPECT_FALSE(m == nullptr);
    EXPECT_TRUE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      EXPECT_NE(v, &i);
      EXPECT_EQ(234, *v);
    } else {
      ADD_FAILURE();
    }
    KJ_IF_MAYBE(v, mv(m)) {
      EXPECT_NE(v, &i);
      EXPECT_EQ(234, *v);
    } else {
      ADD_FAILURE();
    }
  }

  {
    Maybe<int> m = upcast<int*>(nullptr);
    EXPECT_TRUE(m == nullptr);
    EXPECT_FALSE(m != nullptr);
    KJ_IF_MAYBE(v, m) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
    KJ_IF_MAYBE(v, mv(m)) {
      ADD_FAILURE();
      EXPECT_EQ(0, *v);  // avoid unused warning
    }
  }
}

class Foo {
public:
  virtual ~Foo() {}
};

class Bar: public Foo {
public:
  virtual ~Bar() {}
};

class Baz: public Foo {
public:
  virtual ~Baz() {}
};

TEST(Common, Downcast) {
  Bar bar;
  Foo& foo = bar;

  EXPECT_EQ(&bar, &downcast<Bar&>(foo));
  EXPECT_EQ(&bar, downcast<Bar*>(&foo));
  EXPECT_ANY_THROW(downcast<Baz&>(foo));
  EXPECT_ANY_THROW(downcast<Baz*>(&foo));

#if KJ_NO_RTTI
  EXPECT_TRUE(dynamicDowncastIfAvailable<Bar&>(foo) == nullptr);
  EXPECT_TRUE(dynamicDowncastIfAvailable<Baz&>(foo) == nullptr);
#else
  KJ_IF_MAYBE(m, dynamicDowncastIfAvailable<Bar&>(foo)) {
    EXPECT_EQ(&bar, m);
  } else {
    ADD_FAILURE() << "Dynamic downcast returned null.";
  }
  EXPECT_TRUE(dynamicDowncastIfAvailable<Baz&>(foo) == nullptr);
#endif
}

}  // namespace
}  // namespace kj
