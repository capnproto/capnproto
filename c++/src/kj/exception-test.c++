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
#include "main.h"
#include <kj/compat/gtest.h>
#include <stdexcept>
#include <stdint.h>

namespace kj {
namespace _ {  // private
namespace {

TEST(Exception, TrimSourceFilename) {
#if _WIN32
  EXPECT_TRUE(trimSourceFilename(__FILE__) == "kj/exception-test.c++" ||
              trimSourceFilename(__FILE__) == "kj\\exception-test.c++");
#else
  EXPECT_EQ(trimSourceFilename(__FILE__), "kj/exception-test.c++");
#endif
}

TEST(Exception, RunCatchingExceptions) {
  bool recovered = false;
  Maybe<Exception> e = kj::runCatchingExceptions([&]() {
    KJ_FAIL_ASSERT("foo") {
      break;
    }
    recovered = true;
  });

  EXPECT_FALSE(recovered);

  KJ_IF_SOME(ex, e) {
    EXPECT_EQ("foo", ex.getDescription());
  } else {
    ADD_FAILURE() << "Expected exception";
  }
}

TEST(Exception, RunCatchingExceptionsStdException) {
  Maybe<Exception> e = kj::runCatchingExceptions([&]() {
    throw std::logic_error("foo");
  });

  KJ_IF_SOME(ex, e) {
    EXPECT_EQ("std::exception: foo", ex.getDescription());
  } else {
    ADD_FAILURE() << "Expected exception";
  }
}

TEST(Exception, RunCatchingExceptionsOtherException) {
  Maybe<Exception> e = kj::runCatchingExceptions([&]() {
    throw 123;
  });

  KJ_IF_SOME(ex, e) {
#if __GNUC__ && !KJ_NO_RTTI
    EXPECT_EQ("unknown non-KJ exception of type: int", ex.getDescription());
#else
    EXPECT_EQ("unknown non-KJ exception", ex.getDescription());
#endif
  } else {
    ADD_FAILURE() << "Expected exception";
  }
}

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

  KJ_IF_SOME(ex, e) {
    EXPECT_EQ("this is a test, not a real bug", ex.getDescription());
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

  KJ_IF_SOME(ex, e) {
    EXPECT_EQ("baz", ex.getDescription());
  } else {
    ADD_FAILURE() << "Expected exception";
  }
}

#if defined(FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION) || \
    KJ_HAS_COMPILER_FEATURE(address_sanitizer) || \
    defined(__SANITIZE_ADDRESS__)
// The implementation skips this check in these cases.
#else
#if !__MINGW32__  // Inexplicably crashes when exception is thrown from constructor.
TEST(Exception, ExceptionCallbackMustBeOnStack) {
  KJ_EXPECT_THROW_MESSAGE("must be allocated on the stack", new ExceptionCallback);
}
#endif
#endif  // !__MINGW32__

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

#if __GNUG__ || defined(__clang__)
kj::String testStackTrace() __attribute__((noinline));
#elif _MSC_VER
__declspec(noinline) kj::String testStackTrace();
#endif

kj::String testStackTrace() {
  // getStackTrace() normally skips its immediate caller, so we wrap it in another layer.
  return getStackTrace();
}

KJ_TEST("getStackTrace() returns correct line number, not line + 1") {
  // Backtraces normally produce the return address of each stack frame, but that's usually the
  // address immediately after the one that made the call. As a result, it used to be that stack
  // traces often pointed to the line after the one that made a call, which was confusing. This
  // checks that this bug is fixed.
  //
  // This is not a very robust test, because:
  // 1) Since symbolic stack traces are not available in many situations (e.g. release builds
  //    lacking debug symbols, systems where addr2line isn't present, etc.), we only check that
  //    the stack trace does *not* contain the *wrong* value, rather than checking that it does
  //    contain the right one.
  // 2) This test only detects the problem if the call instruction to testStackTrace() is the
  //    *last* instruction attributed to its line of code. Whether or not this is true seems to be
  //    dependent on obscure compiler behavior. For example, below, it could only be the case if
  //    RVO is applied -- but in my testing, RVO does seem to be applied here. I tried several
  //    variations involving passing via an output parameter or a global variable rather than
  //    returning, but found some variations detected the problem and others didn't, essentially
  //    at random.

  auto trace = testStackTrace();
  auto wrong = kj::str("exception-test.c++:", __LINE__);

  KJ_ASSERT(!trace.contains(wrong), trace, wrong);
}

KJ_TEST("InFlightExceptionIterator works") {
  bool caught = false;
  try {
    KJ_DEFER({
      try {
        KJ_FAIL_ASSERT("bar");
      } catch (const kj::Exception& e) {
        InFlightExceptionIterator iter;
        KJ_IF_SOME(e2, iter.next()) {
          KJ_EXPECT(&e2 == &e, e2.getDescription());
        } else {
          KJ_FAIL_EXPECT("missing first exception");
        }

        KJ_IF_SOME(e2, iter.next()) {
          KJ_EXPECT(e2.getDescription() == "foo", e2.getDescription());
        } else {
          KJ_FAIL_EXPECT("missing second exception");
        }

        KJ_EXPECT(iter.next() == kj::none, "more than two exceptions");

        caught = true;
      }
    });
    KJ_FAIL_ASSERT("foo");
  } catch (const kj::Exception& e) {
    // expected
  }

  KJ_EXPECT(caught);
}

KJ_TEST("computeRelativeTrace") {
  auto testCase = [](uint expectedPrefix,
                     ArrayPtr<const uintptr_t> trace, ArrayPtr<const uintptr_t> relativeTo) {
    auto tracePtr = KJ_MAP(x, trace) { return (void*)x; };
    auto relativeToPtr = KJ_MAP(x, relativeTo) { return (void*)x; };

    auto result = computeRelativeTrace(tracePtr, relativeToPtr);
    KJ_EXPECT(result.begin() == tracePtr.begin());

    KJ_EXPECT(result.size() == expectedPrefix, trace, relativeTo, result);
  };

  testCase(8,
      {1, 2, 3, 4, 5, 6, 7, 8},
      {8, 7, 6, 5, 4, 3, 2, 1});

  testCase(5,
      {1, 2, 3, 4, 5, 6, 7, 8},
      {8, 7, 6, 5, 5, 6, 7, 8});

  testCase(5,
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
      {8, 7, 6, 5, 5, 6, 7, 8});

  testCase(5,
      {1, 2, 3, 4, 5, 6, 7, 8, 6, 7, 8},
      {8, 7, 6, 5, 5, 6, 7, 8});

  testCase(9,
      {1, 2, 3, 4, 5, 6, 7, 8, 5, 5, 6, 7, 8},
      {8, 7, 6, 5, 5, 6, 7, 8});

  testCase(5,
      {1, 2, 3, 4, 5, 5, 6, 7, 8, 5, 6, 7, 8},
      {8, 7, 6, 5, 5, 6, 7, 8});

  testCase(5,
      {1, 2, 3, 4, 5, 6, 7, 8},
      {8, 7, 6, 5, 5, 6, 7, 8, 7, 8});

  testCase(5,
      {1, 2, 3, 4, 5, 6, 7, 8},
      {8, 7, 6, 5, 6, 7, 8, 7, 8});
}

KJ_TEST("exception details") {
  kj::Exception e = KJ_EXCEPTION(FAILED, "foo");

  e.setDetail(123, kj::heapArray("foo"_kjb));
  e.setDetail(456, kj::heapArray("bar"_kjb));

  KJ_EXPECT(kj::str(KJ_ASSERT_NONNULL(e.getDetail(123)).asChars()) == "foo");
  KJ_EXPECT(kj::str(KJ_ASSERT_NONNULL(e.getDetail(456)).asChars()) == "bar");
  KJ_EXPECT(e.getDetail(789) == kj::none);

  kj::Exception e2 = kj::cp(e);
  KJ_EXPECT(kj::str(KJ_ASSERT_NONNULL(e2.getDetail(123)).asChars()) == "foo");
  KJ_EXPECT(kj::str(KJ_ASSERT_NONNULL(e2.getDetail(456)).asChars()) == "bar");
  KJ_EXPECT(e2.getDetail(789) == kj::none);

  KJ_EXPECT(kj::str(KJ_ASSERT_NONNULL(e2.releaseDetail(123)).asChars()) == "foo");
  KJ_EXPECT(e2.getDetail(123) == kj::none);
  KJ_EXPECT(kj::str(KJ_ASSERT_NONNULL(e2.getDetail(456)).asChars()) == "bar");
}

KJ_TEST("Maybe<Exception> move-assignment is safe when this owns other") {
  // Test that move-assignment works correctly when `other` is inside `this`'s value.
  // An Exception can own another Exception via a detail array with attach().
  //
  // This scenario is extremely contrived and almost certainly won't happen in practice,
  // but we're testing for good measure.

  // Create an inner exception that we'll attach to the outer one
  Own<Exception> innerOwn = heap<Exception>(Exception::Type::FAILED, __FILE__, __LINE__,
      str("inner exception"));
  Exception& inner = *innerOwn;

  // Create an outer exception and attach the inner one to a detail
  Maybe<Exception> outer = KJ_EXCEPTION(FAILED, "outer exception");
  auto detailArray = heapArray<byte>(0).attach(kj::mv(innerOwn));
  KJ_ASSERT_NONNULL(outer).setDetail(123, kj::mv(detailArray));

  // Now `inner` is owned by outer's detail. Verify we can still access it.
  KJ_EXPECT(inner.getDescription() == "inner exception");

  // Move-assign outer from inner. Without a correctly implemented assignment operator, this
  // would be use-after-free because outer would be destroyed (freeing inner) before inner
  // is accessed.
  outer = kj::mv(inner);

  KJ_EXPECT(outer != kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(outer).getDescription() == "inner exception");
}

KJ_TEST("copy constructor") {
  auto e = new kj::Exception(kj::Exception::Type::FAILED, kj::str("src/bar.cc"),
                             35, kj::str("test_exception"));
  KJ_EXPECT(e->getFile() == "bar.cc"_kj);
  KJ_EXPECT(e->getLine() == 35);
  KJ_EXPECT(e->getDescription() == "test_exception"_kj);

  kj::Exception e1(*e);
  delete e;

  KJ_EXPECT(e1.getFile() == "bar.cc"_kj);
  KJ_EXPECT(e1.getLine() == 35);
  KJ_EXPECT(e1.getDescription() == "test_exception"_kj);
}

KJ_TEST("KJ_TRY/KJ_CATCH basic functionality") {
  bool caughtException = false;

  KJ_TRY {
    KJ_FAIL_ASSERT("test exception");
  } KJ_CATCH(e) {
    caughtException = true;
    KJ_EXPECT(e.getDescription() == "test exception");
    KJ_EXPECT(e.getType() == kj::Exception::Type::FAILED);
  }

  KJ_EXPECT(caughtException);
}

KJ_TEST("KJ_TRY/KJ_CATCH with no exception") {
  bool handlerCalled = false;
  bool tryBlockCompleted = false;

  KJ_TRY {
    tryBlockCompleted = true;
  } KJ_CATCH(_) {
    handlerCalled = true;
  }

  KJ_EXPECT(tryBlockCompleted);
  KJ_EXPECT(!handlerCalled);
}

KJ_TEST("KJ_TRY/KJ_CATCH with std::exception") {
  bool caughtException = false;

  KJ_TRY {
    throw std::runtime_error("std exception test");
  } KJ_CATCH(e) {
    caughtException = true;
    KJ_EXPECT(e.getDescription().contains("std::exception: std exception test"));
  }

  KJ_EXPECT(caughtException);
}

KJ_TEST("KJ_TRY/KJ_CATCH with multiple statements") {
  bool caughtException = false;
  int value = 0;

  KJ_TRY {
    value = 42;
    KJ_FAIL_ASSERT("delayed exception");
  } KJ_CATCH(e) {
    caughtException = true;
    KJ_EXPECT(e.getDescription() == "delayed exception");
    KJ_EXPECT(value == 42);
  }

  KJ_EXPECT(caughtException);
  KJ_EXPECT(value == 42);
}

KJ_TEST("KJ_TRY/KJ_CATCH handler can access variables") {
  int handlerValue = 0;
  bool caughtException = false;

  KJ_TRY {
    KJ_FAIL_ASSERT("handler test");
  } KJ_CATCH(ex) {
    caughtException = true;
    handlerValue = 123;
    KJ_EXPECT(ex.getDescription() == "handler test");
  }

  KJ_EXPECT(caughtException);
  KJ_EXPECT(handlerValue == 123);
}

KJ_TEST("KJ_TRY/KJ_CATCH nested usage") {
  bool outerCaught = false;
  bool innerCaught = false;

  KJ_TRY {
    KJ_TRY {
      KJ_FAIL_ASSERT("inner exception");
    } KJ_CATCH(innerEx) {
      innerCaught = true;
      KJ_EXPECT(innerEx.getDescription() == "inner exception");
      KJ_FAIL_ASSERT("outer exception");
    }
  } KJ_CATCH(outerEx) {
    outerCaught = true;
    KJ_EXPECT(outerEx.getDescription() == "outer exception");
  }

  KJ_EXPECT(innerCaught);
  KJ_EXPECT(outerCaught);
}

KJ_TEST("KJ_TRY/KJ_CATCH with different exception types") {
  bool disconnectedCaught = false;
  bool overloadedCaught = false;

  KJ_TRY {
    throw KJ_EXCEPTION(DISCONNECTED, "test disconnection");
  } KJ_CATCH(e1) {
    disconnectedCaught = true;
    KJ_EXPECT(e1.getType() == kj::Exception::Type::DISCONNECTED);
    KJ_EXPECT(e1.getDescription() == "test disconnection");
  }

  KJ_TRY {
    throw KJ_EXCEPTION(OVERLOADED, "test overloaded");
  } KJ_CATCH(e2) {
    overloadedCaught = true;
    KJ_EXPECT(e2.getType() == kj::Exception::Type::OVERLOADED);
    KJ_EXPECT(e2.getDescription() == "test overloaded");
  }

  KJ_EXPECT(disconnectedCaught);
  KJ_EXPECT(overloadedCaught);
}

KJ_TEST("KJ_TRY/KJ_CATCH inside try/catch") {
  bool kjCaught = false;
  bool stdCaught = false;

  try {
    KJ_TRY {
      KJ_FAIL_ASSERT("inner kj exception");
    } KJ_CATCH(e) {
      kjCaught = true;
      KJ_EXPECT(e.getDescription() == "inner kj exception");
    }
  } catch (const kj::Exception& e) {
    stdCaught = true;
    KJ_FAIL_EXPECT("should not reach outer catch");
  }

  KJ_EXPECT(kjCaught);
  KJ_EXPECT(!stdCaught);
}

KJ_TEST("KJ_TRY/KJ_CATCH inside try/catch with uncaught exception") {
  bool kjCaught = false;
  bool stdCaught = false;

  try {
    KJ_TRY {
      // This should not throw
      int x = 42;
      (void)x;
    } KJ_CATCH(_) {
      kjCaught = true;
      KJ_FAIL_EXPECT("handler should not be called");
    }
    // This throws after KJ_TRY/KJ_CATCH completes normally
    KJ_FAIL_ASSERT("outer exception");
  } catch (const kj::Exception& e) {
    stdCaught = true;
    KJ_EXPECT(e.getDescription() == "outer exception");
  }

  KJ_EXPECT(!kjCaught);
  KJ_EXPECT(stdCaught);
}

KJ_TEST("KJ_TRY/KJ_CATCH inside try/catch with std::exception") {
  bool kjCaught = false;
  bool stdCaught = false;

  try {
    KJ_TRY {
      throw std::logic_error("std exception in KJ_TRY/KJ_CATCH");
    } KJ_CATCH(e) {
      kjCaught = true;
      KJ_EXPECT(e.getDescription().contains("std::exception: std exception in KJ_TRY/KJ_CATCH"));
    }
  } catch (const std::exception& e) {
    stdCaught = true;
    KJ_FAIL_EXPECT("should not reach outer catch");
  }

  KJ_EXPECT(kjCaught);
  KJ_EXPECT(!stdCaught);
}

KJ_TEST("KJ_TRY/KJ_CATCH does not catch CanceledException") {
  bool kjCatchCalled = false;
  bool outerCatchCalled = false;

  try {
    KJ_TRY {
      throw kj::CanceledException();
    } KJ_CATCH(_) {
      kjCatchCalled = true;
      KJ_FAIL_EXPECT("KJ_CATCH should not handle CanceledException");
    }
  } catch (const kj::CanceledException&) {
    outerCatchCalled = true;
  }

  KJ_EXPECT(!kjCatchCalled);
  KJ_EXPECT(outerCatchCalled);
}

KJ_TEST("KJ_TRY/KJ_CATCH does not catch CleanShutdownException") {
  bool kjCatchCalled = false;
  bool outerCatchCalled = false;

  try {
    KJ_TRY {
      throw kj::TopLevelProcessContext::CleanShutdownException{42};
    } KJ_CATCH(_) {
      kjCatchCalled = true;
      KJ_FAIL_EXPECT("KJ_CATCH should not handle CleanShutdownException");
    }
  } catch (const kj::TopLevelProcessContext::CleanShutdownException& e) {
    outerCatchCalled = true;
    KJ_EXPECT(e.exitCode == 42);
  }

  KJ_EXPECT(!kjCatchCalled);
  KJ_EXPECT(outerCatchCalled);
}

KJ_TEST("getDestructionReason returns default exception if exception wasn't thrown") {
  auto e =
      kj::getDestructionReason(nullptr, kj::Exception::Type::FAILED, __FILE__,
                               __LINE__, "default description"_kj);
  KJ_EXPECT(e.getType() == kj::Exception::Type::FAILED);
  KJ_EXPECT(e.getDescription() == "default description"_kj);
}

KJ_TEST("getDestructionReason returns thrown exception if it wasn't consumed") {
  try {
    kj::throwFatalException(KJ_EXCEPTION(DISCONNECTED, "test exception"));
  } catch (...) {
    auto e =
        kj::getDestructionReason(nullptr, kj::Exception::Type::FAILED, __FILE__,
                                 __LINE__, "default description"_kj);
    KJ_EXPECT(e.getType() == kj::Exception::Type::DISCONNECTED);
    KJ_EXPECT(e.getDescription() == "test exception"_kj);
  }
}

KJ_TEST("getDestructionReason returns default exception if exception was "
        "consumed") {
  try {
    kj::throwFatalException(KJ_EXCEPTION(DISCONNECTED, "test exception"));
  } catch (...) {
    auto caughtException = kj::getCaughtExceptionAsKj();
    KJ_EXPECT(caughtException.getType() == kj::Exception::Type::DISCONNECTED);
    KJ_EXPECT(caughtException.getDescription() == "test exception"_kj);

    auto e =
        kj::getDestructionReason(nullptr, kj::Exception::Type::FAILED, __FILE__,
                                 __LINE__, "default description"_kj);
    KJ_EXPECT(e.getType() == kj::Exception::Type::FAILED);
    KJ_EXPECT(e.getDescription() == "default description"_kj);
  }
}

// =======================================================================================
// Maybe<Exception> niche optimization tests

KJ_TEST("Maybe<Exception> niche optimization") {
  // Maybe<Exception> should use niche optimization, storing Exception directly without a
  // separate bool flag. This means sizeof(Maybe<Exception>) == sizeof(Exception).
  static_assert(sizeof(Maybe<Exception>) == sizeof(Exception),
      "Maybe<Exception> should be niche-optimized to the same size as Exception");

  // Test basic Maybe<Exception> functionality with niche optimization
  {
    Maybe<Exception> empty;
    KJ_EXPECT(empty == kj::none);
  }

  {
    Maybe<Exception> m = KJ_EXCEPTION(FAILED, "test error");
    KJ_EXPECT(m != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(m).getDescription() == "test error");
  }

  // Test move semantics
  {
    Maybe<Exception> m1 = KJ_EXCEPTION(DISCONNECTED, "disconnect error");
    Maybe<Exception> m2 = kj::mv(m1);
    KJ_EXPECT(m1 == kj::none);  // Source should be empty after move
    KJ_EXPECT(m2 != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(m2).getType() == Exception::Type::DISCONNECTED);
  }

  // Test assignment
  {
    Maybe<Exception> m;
    m = KJ_EXCEPTION(OVERLOADED, "overload error");
    KJ_EXPECT(m != kj::none);
    KJ_EXPECT(KJ_ASSERT_NONNULL(m).getType() == Exception::Type::OVERLOADED);

    m = kj::none;
    KJ_EXPECT(m == kj::none);
  }
}

}  // namespace
}  // namespace _ (private)
}  // namespace kj
