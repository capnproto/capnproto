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

#include "exception.h"
#include "debug.h"
#include <kj/compat/gtest.h>

namespace kj {
namespace _ {  // private
namespace {

TEST(Exception, TrimSourceFilename) {
  EXPECT_EQ(trimSourceFilename(__FILE__), "kj/exception-test.c++");
}

TEST(Exception, RunCatchingExceptions) {
  bool recovered = false;
  Maybe<Exception> e = kj::runCatchingExceptions([&]() {
    KJ_FAIL_ASSERT("foo") {
      break;
    }
    recovered = true;
  });

#if KJ_NO_EXCEPTIONS
  EXPECT_TRUE(recovered);
#else
  EXPECT_FALSE(recovered);
#endif

  KJ_IF_MAYBE(ex, e) {
    EXPECT_EQ("foo", ex->getDescription());
  } else {
    ADD_FAILURE() << "Expected exception";
  }
}

#if !KJ_NO_EXCEPTIONS
// We skip this test when exceptions are disabled because making it no-exceptions-safe defeats
// the purpose of the test: recoverable exceptions won't throw inside a destructor in the first
// place.

class ThrowingDestructor: public UnwindDetector {
public:
  ~ThrowingDestructor() noexcept(false) {
    catchExceptionsIfUnwinding([]() {
      KJ_FAIL_ASSERT("this is a test, not a real bug");
    });
  }
};

TEST(Exception, UnwindDetector) {
  // If no other exception is happening, ThrowingDestructor's destructor throws one.
  Maybe<Exception> e = kj::runCatchingExceptions([&]() {
    ThrowingDestructor t;
  });

  KJ_IF_MAYBE(ex, e) {
    EXPECT_EQ("this is a test, not a real bug", ex->getDescription());
  } else {
    ADD_FAILURE() << "Expected exception";
  }

  // If another exception is happening, ThrowingDestructor's destructor's exception is squelched.
  e = kj::runCatchingExceptions([&]() {
    ThrowingDestructor t;
    KJ_FAIL_ASSERT("baz") {
      break;
    }
  });

  KJ_IF_MAYBE(ex, e) {
    EXPECT_EQ("baz", ex->getDescription());
  } else {
    ADD_FAILURE() << "Expected exception";
  }
}
#endif

#if !__MINGW32__  // Inexplicably crashes when exception is thrown from constructor.
TEST(Exception, ExceptionCallbackMustBeOnStack) {
#if KJ_NO_EXCEPTIONS
  EXPECT_DEATH_IF_SUPPORTED(new ExceptionCallback, "must be allocated on the stack");
#else
  EXPECT_ANY_THROW(new ExceptionCallback);
#endif
}
#endif  // !__MINGW32__

#if !KJ_NO_EXCEPTIONS
TEST(Exception, ScopeSuccessFail) {
  bool success = false;
  bool failure = false;

  {
    KJ_ON_SCOPE_SUCCESS(success = true);
    KJ_ON_SCOPE_FAILURE(failure = true);

    EXPECT_FALSE(success);
    EXPECT_FALSE(failure);
  }

  EXPECT_TRUE(success);
  EXPECT_FALSE(failure);

  success = false;
  failure = false;

  try {
    KJ_ON_SCOPE_SUCCESS(success = true);
    KJ_ON_SCOPE_FAILURE(failure = true);

    EXPECT_FALSE(success);
    EXPECT_FALSE(failure);

    throw 1;
  } catch (int) {}

  EXPECT_FALSE(success);
  EXPECT_TRUE(failure);
}
#endif

}  // namespace
}  // namespace _ (private)
}  // namespace kj
