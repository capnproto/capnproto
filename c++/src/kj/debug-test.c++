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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "debug.h"
#include "exception.h"
#include "source-location.h"
#include "vector.h"
#include <kj/compat/gtest.h>
#include <string>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <exception>
#include <stdlib.h>

namespace kj {
namespace _ {  // private
namespace {

class MockException {};

struct ThrowOnStringify {};
kj::String KJ_STRINGIFY(ThrowOnStringify) {
  // Simulates a parameter whose stringification throws an exception (e.g. reading a corrupt
  // Cap'n Proto field). Used to test that KJ_CONTEXT handles this gracefully.
  KJ_FAIL_ASSERT("stringification intentionally failed");
}

class MockExceptionCallback: public ExceptionCallback {
public:
  ~MockExceptionCallback() {}

  kj::String text;

  void onException(Exception&& exception) override {
    auto what = str(exception);
    // Discard the stack trace.
    KJ_IF_SOME(end, what.find("\nstack: ")) {
      text = kj::str(text, "exception: ", what.first(end), '\n');
    } else {
      text = kj::str(text, "exception: ", what, '\n');
    }
    throw MockException();
  }

  void logMessage(LogSeverity severity, const char* file, int line, int contextDepth,
                  String&& text) override {
    if (text.contains("To symbolize stack traces, install it in your $PATH")) {
      // Ignore warning about LLVM_SYMBOLIZER not being available.
      return;
    }

    this->text = kj::str(this->text, "log message: ", file, ":", line, ":+", contextDepth, ": ", severity, ": ", mv(text), "\n");
  }
};

#define EXPECT_LOG_EQ(f, expText) do { \
  kj::String text; \
  { \
    MockExceptionCallback mockCallback; \
    f(); \
    text = kj::mv(mockCallback.text); \
  } \
  EXPECT_EQ(expText, text); \
} while(0)

#define EXPECT_FATAL(code) \
  try { code; KJ_FAIL_EXPECT("expected exception"); } \
  catch (MockException e) {} \
  catch (...) { KJ_FAIL_EXPECT("wrong exception"); }

kj::String fileLine(kj::StringPtr file, int line) {
  auto trimmed = trimSourceFilename(file);
  
  char buffer[32]{};
  snprintf(buffer, sizeof(buffer), "%d", line);
  return kj::str(trimmed, ':', buffer);
}

TEST(Debug, Log) {
  int line;

  EXPECT_LOG_EQ([&](){
    KJ_LOG(WARNING, "Hello world!"); line = __LINE__;
  }, kj::str("log message: ", fileLine(__FILE__, line), ":+0: warning: Hello world!\n"));

  int i = 123;
  const char* str = "foo";

  EXPECT_LOG_EQ([&](){
    KJ_LOG(ERROR, i, str); line = __LINE__;
  }, kj::str("log message: ", fileLine(__FILE__, line), ":+0: error: i = 123; str = foo\n"));

  // kj::str() expressions are included literally.
  EXPECT_LOG_EQ([&](){
    KJ_LOG(ERROR, kj::str(i, str), "x"); line = __LINE__;
  }, kj::str("log message: ", fileLine(__FILE__, line), ":+0: error: 123foo; x\n"));

  EXPECT_LOG_EQ([&](){
    KJ_DBG("Some debug text."); line = __LINE__;
  }, kj::str("log message: ", fileLine(__FILE__, line), ":+0: debug: Some debug text.\n"));

  // INFO logging is disabled by default.
  EXPECT_LOG_EQ([&](){
    KJ_LOG(INFO, "Info."); line = __LINE__;
  }, "");

  // Enable it.
  Debug::setLogLevel(Debug::Severity::INFO);
  EXPECT_LOG_EQ([&](){
    KJ_LOG(INFO, "Some text."); line = __LINE__;
  }, kj::str("log message: ", fileLine(__FILE__, line), ":+0: info: Some text.\n"));

  // Back to default.
  Debug::setLogLevel(Debug::Severity::WARNING);

  EXPECT_LOG_EQ([&](){
    KJ_ASSERT(1 == 1);
  }, "");

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_ASSERT(1 == 2)); line = __LINE__;
  }, kj::str("exception: ", fileLine(__FILE__, line), ": failed: expected 1 == 2 [1 == 2]\n"));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_ASSERT(1 == 2, i, "hi", str)); line = __LINE__;
  }, kj::str(
    "exception: ", fileLine(__FILE__, line), ": "
        "failed: expected 1 == 2 [1 == 2]; i = 123; hi; str = foo\n"
  ));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_REQUIRE(1 == 2, i, "hi", str)); line = __LINE__;
  }, kj::str(
    "exception: ", fileLine(__FILE__, line), ": "
        "failed: expected 1 == 2 [1 == 2]; i = 123; hi; str = foo\n"
  ));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_FAIL_ASSERT("foo")); line = __LINE__;
  }, kj::str("exception: ", fileLine(__FILE__, line), ": failed: foo\n"));
}

TEST(Debug, EmptyVariadicArgs) {
  // Macros taking only `...` must accept being invoked with no arguments.
  int line;

  EXPECT_LOG_EQ([&](){
    KJ_DBG(); line = __LINE__;
  }, kj::str("log message: ", fileLine(__FILE__, line), ":+0: debug: \n"));

  EXPECT_LOG_EQ([&](){
    KJ_FAIL_EXPECT(); line = __LINE__;
  }, kj::str("log message: ", fileLine(__FILE__, line), ":+0: error: \n"));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_FAIL_ASSERT()); line = __LINE__;
  }, kj::str("exception: ", fileLine(__FILE__, line), ": failed\n"));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_FAIL_REQUIRE()); line = __LINE__;
  }, kj::str("exception: ", fileLine(__FILE__, line), ": failed\n"));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_UNIMPLEMENTED()); line = __LINE__;
  }, kj::str("exception: ", fileLine(__FILE__, line), ": unimplemented\n"));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_DTOR_FAIL_ASSERT()); line = __LINE__;
  }, kj::str("exception: ", fileLine(__FILE__, line), ": failed\n"));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_DTOR_FAIL_REQUIRE()); line = __LINE__;
  }, kj::str("exception: ", fileLine(__FILE__, line), ": failed\n"));

  int cline;
  EXPECT_LOG_EQ([&](){
    KJ_CONTEXT(); cline = __LINE__;
    EXPECT_FATAL(KJ_FAIL_ASSERT("bar")); line = __LINE__;
  }, kj::str("exception: ", fileLine(__FILE__, cline), ": context: \n",
             fileLine(__FILE__, line), ": failed: bar\n"));
}

// Testing KJ_LOG_AT, etc. macros:
void myRequire(bool cond, kj::SourceLocation location = {}) {
  KJ_REQUIRE_AT(cond, location, "a_message");
}
void myFailRequire(kj::SourceLocation location = {}) {
  KJ_FAIL_REQUIRE_AT(location, "a_message");
}
void myRequireNonnull(kj::Maybe<int> value, kj::SourceLocation location = {}) {
  (void)KJ_REQUIRE_NONNULL_AT(value, location, "a_message");
}
void myAssert(bool cond, kj::SourceLocation location = {}) {
  KJ_ASSERT_AT(cond, location, "a_message");
}
void myFailAssert(kj::SourceLocation location = {}) {
  KJ_FAIL_ASSERT_AT(location, "a_message");
}
void myAssertNonnull(kj::Maybe<int> value, kj::SourceLocation location = {}) {
  (void)KJ_ASSERT_NONNULL_AT(value, location, "a_message");
}
void myLog(kj::SourceLocation location = {}) {
  KJ_LOG_AT(WARNING, location, "a_message");
}
// Using functional versions of the utility macros used elsewhere, because the __builtin_LINE()
// primitive used by SourceLocation is inaccurate in macros:
void expectLogEq(auto&& f, auto&& expTextFunc) {
  kj::String text;
  {
    MockExceptionCallback mockCallback;
    f();
    text = kj::mv(mockCallback.text);
  }
  kj::String expText = expTextFunc();
  EXPECT_EQ(expText, text);
};
void expectFatal(auto&& f) {
  try {
    f();
    KJ_FAIL_EXPECT("expected exception");
  } catch (MockException e) {
  } catch (...) {
    KJ_FAIL_EXPECT("wrong exception");
  }
}
TEST(Debug, LogAt) {
  int line;

  expectLogEq([&](){
    myLog(); line = __LINE__;
  }, [&](){
    return kj::str("log message: ", fileLine(__FILE__, line), ":+0: warning: a_message\n");
  });

  expectLogEq([&](){
    expectFatal([&](){ myRequire(1 == 2); }); line = __LINE__;
  }, [&](){
    return kj::str("exception: ", fileLine(__FILE__, line), ": "
        "failed: expected cond; a_message\n");
  });

  expectLogEq([&](){
    expectFatal([&](){ myFailRequire(); }); line = __LINE__;
  }, [&](){
    return kj::str("exception: ", fileLine(__FILE__, line), ": failed: a_message\n");
  });

  expectLogEq([&](){
    expectFatal([&](){ myRequireNonnull(kj::none); }); line = __LINE__;
  }, [&](){
    return kj::str("exception: ", fileLine(__FILE__, line), ": "
        "failed: expected value != nullptr; a_message\n");
  });

  expectLogEq([&](){
    expectFatal([&](){ myAssert(1 == 2); }); line = __LINE__;
  }, [&](){
    return kj::str("exception: ", fileLine(__FILE__, line), ": "
        "failed: expected cond; a_message\n");
  });

  expectLogEq([&](){
    expectFatal([&](){ myFailAssert(); }); line = __LINE__;
  }, [&](){
    return kj::str("exception: ", fileLine(__FILE__, line), ": failed: a_message\n");
  });

  expectLogEq([&](){
    expectFatal([&](){ myAssertNonnull(kj::none); }); line = __LINE__;
  }, [&](){
    return kj::str("exception: ", fileLine(__FILE__, line), ": "
        "failed: expected value != nullptr; a_message\n");
  });
}

TEST(Debug, Exception) {
  int i = 123;

  int line = __LINE__; Exception exception = KJ_EXCEPTION(DISCONNECTED, "foo", i);

  EXPECT_EQ(Exception::Type::DISCONNECTED, exception.getType());
  EXPECT_TRUE(kj::StringPtr(__FILE__).endsWith(exception.getFile()));
  EXPECT_EQ(line, exception.getLine());
  EXPECT_EQ("foo; i = 123", exception.getDescription());
}

TEST(Debug, Catch) {
  int line;

  {
    // Catch as kj::Exception.
    Maybe<Exception> exception = kj::runCatchingExceptions([&](){
      line = __LINE__; KJ_FAIL_ASSERT("foo");
    });

    KJ_IF_SOME(e, exception) {
      String what = str(e);
      KJ_IF_SOME(eol, what.findFirst('\n')) {
        auto firstLine = kj::str(what.first(eol));
        what = kj::mv(firstLine);
      }
      kj::StringPtr text = what;
      EXPECT_EQ(kj::str(fileLine(__FILE__, line), ": failed: foo"), text);
    } else {
      ADD_FAILURE() << "Expected exception.";
    }
  }

  {
    // Catch as std::exception.
    try {
      line = __LINE__; KJ_FAIL_ASSERT("foo");
      KJ_KNOWN_UNREACHABLE(ADD_FAILURE() << "Expected exception.");
    } catch (const std::exception& e) {
      kj::StringPtr what = e.what();
      kj::String text;
      KJ_IF_SOME(eol, what.findFirst('\n')) {
        text = kj::str(what.first(eol));
      } else {
        text = kj::str(what);
      }
      EXPECT_EQ(kj::str(fileLine(__FILE__, line), ": failed: foo"), text);
    }
  }
}

int mockSyscall(int i, int error = 0) {
  errno = error;
  return i;
}

TEST(Debug, Syscall) {
  int line;

  int i = 123;
  const char* str = "foo";

  EXPECT_LOG_EQ([&](){
    KJ_SYSCALL(mockSyscall(0));
    KJ_SYSCALL(mockSyscall(1));
  }, "");

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_SYSCALL(mockSyscall(-1, EBADF), i, "bar", str)); line = __LINE__;
  }, kj::str(
    "exception: ", fileLine(__FILE__, line),
        ": failed: mockSyscall(-1, EBADF): ", strerror(EBADF),
        "; i = 123; bar; str = foo\n"
  ));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_SYSCALL(mockSyscall(-1, ECONNRESET), i, "bar", str)); line = __LINE__;
  }, kj::str(
    "exception: ", fileLine(__FILE__, line),
        ": disconnected: mockSyscall(-1, ECONNRESET): ", strerror(ECONNRESET),
        "; i = 123; bar; str = foo\n"
  ));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_SYSCALL(mockSyscall(-1, ENOMEM), i, "bar", str)); line = __LINE__;
  }, kj::str(
    "exception: ", fileLine(__FILE__, line),
        ": overloaded: mockSyscall(-1, ENOMEM): ", strerror(ENOMEM),
        "; i = 123; bar; str = foo\n"
  ));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_SYSCALL(mockSyscall(-1, ENOSYS), i, "bar", str)); line = __LINE__;
  }, kj::str(
    "exception: ", fileLine(__FILE__, line),
        ": unimplemented: mockSyscall(-1, ENOSYS): ", strerror(ENOSYS),
        "; i = 123; bar; str = foo\n"
  ));

  int result = 0;
  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_SYSCALL(result = mockSyscall(-2, EBADF), i, "bar", str)); line = __LINE__;
  }, kj::str(
    "exception: ", fileLine(__FILE__, line),
        ": failed: mockSyscall(-2, EBADF): ", strerror(EBADF),
        "; i = 123; bar; str = foo\n"
  ));
  EXPECT_EQ(-2, result);
}

KJ_TEST("check macros are safe to use in unbraced if/else") {
  // The macros expand to `if (...) {} else ...`, so a following `else` must bind to the outer
  // `if`, not to the macro's.
  int elseCount = 0;
  bool no = false;
  // NOLINTBEGIN(readability-braces-around-statements)
  if (no) KJ_ASSERT(1 == 2); else ++elseCount;
  if (no) KJ_FAIL_ASSERT("unreachable"); else ++elseCount;
  if (no) KJ_SYSCALL(mockSyscall(-1, EBADF)); else ++elseCount;
  if (no) KJ_LOG(ERROR, "unreachable"); else ++elseCount;
  // NOLINTEND(readability-braces-around-statements)
  KJ_EXPECT(elseCount == 4);
}

TEST(Debug, Context) {
  int line;
  int line2;
  int cline;
  int cline2;

  EXPECT_LOG_EQ([&](){
    KJ_CONTEXT("foo"); cline = __LINE__;

    KJ_LOG(WARNING, "blah"); line = __LINE__;
    EXPECT_FATAL(KJ_FAIL_ASSERT("bar")); line2 = __LINE__;
  }, kj::str(
    "log message: ", fileLine(__FILE__, cline), ":+0: info: context: foo\n\n"
        "log message: ", fileLine(__FILE__, line), ":+1: warning: blah\n"
        "exception: ", fileLine(__FILE__, cline), ": context: foo\n",
        fileLine(__FILE__, line2), ": failed: bar\n"
  ));

  EXPECT_LOG_EQ([&](){
    KJ_CONTEXT("foo"); cline = __LINE__;
    {
      int i = 123;
      const char* str = "qux";
      KJ_CONTEXT("baz", i, "corge", str); cline2 = __LINE__;

      EXPECT_FATAL(KJ_FAIL_ASSERT("bar")); line = __LINE__;
    }
  }, kj::str(
    "exception: ", fileLine(__FILE__, cline), ": context: foo\n",
    fileLine(__FILE__, cline2), ": context: baz; i = 123; corge; str = qux\n",
    fileLine(__FILE__, line), ": failed: bar\n"
  ));

  EXPECT_LOG_EQ([&](){
    KJ_CONTEXT("foo"); cline = __LINE__;
    {
      int i = 123;
      const char* str = "qux";
      KJ_CONTEXT("baz", i, "corge", str); cline2 = __LINE__;
    }
    {
      KJ_CONTEXT("grault"); cline2 = __LINE__;
      EXPECT_FATAL(KJ_FAIL_ASSERT("bar")); line = __LINE__;
    }
  }, kj::str(
    "exception: ", fileLine(__FILE__, cline), ": context: foo\n",
    fileLine(__FILE__, cline2), ": context: grault\n",
    fileLine(__FILE__, line), ": failed: bar\n"
  ));
}

KJ_TEST("KJ_CONTEXT parameter that throws during evaluation is dropped") {
  // Regression test: previously, if evaluating a KJ_CONTEXT parameter threw an exception (e.g.
  // stringifying a corrupt value), that exception would re-enter the same Context callback, which
  // would re-evaluate the parameter, throwing again, ... leading to infinite recursion and a
  // stack overflow. Now, if evaluating the context throws, the context is simply dropped and the
  // original exception propagates normally.
  auto exception = kj::runCatchingExceptions([&]() {
    KJ_CONTEXT("bad context", ThrowOnStringify());
    KJ_FAIL_ASSERT("the real error");
  });

  auto& e = KJ_ASSERT_NONNULL(exception);
  auto text = kj::str(e);
  // The real error still propagates...
  KJ_EXPECT(text.contains("the real error"), text);
  // ...but the context that failed to evaluate was dropped.
  KJ_EXPECT(!text.contains("bad context"), text);
}

KJ_TEST("magic assert stringification") {
  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      int foo = 123;
      int bar = 456;
      KJ_ASSERT(foo == bar);
    }));

    KJ_EXPECT(exception.getDescription() == "expected foo == bar [123 == 456]");
  }

  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      auto foo = kj::str("hello");
      auto bar = kj::str("world!");
      KJ_ASSERT(foo == bar, foo.size(), bar.size());
    }));

    KJ_EXPECT(exception.getDescription() ==
        "expected foo == bar [hello == world!]; foo.size() = 5; bar.size() = 6");
  }

  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_ASSERT(kj::str("hello") == kj::str("world!"));
    }));

    KJ_EXPECT(exception.getDescription() ==
        "expected kj::str(\"hello\") == kj::str(\"world!\") [hello == world!]");
  }

  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      int foo = 123;
      int bar = 456;
      KJ_ASSERT((foo == bar));
    }));

    KJ_EXPECT(exception.getDescription() == "expected (foo == bar)");
  }

  // Test use of << on left side, which could create confusion.
  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      int foo = 123;
      int bar = 456;
      KJ_ASSERT(foo << 2 == bar);
    }));

    KJ_EXPECT(exception.getDescription() == "expected foo << 2 == bar [492 == 456]");
  }

  // Test use of & on left side.
  {
    int foo = 4;
    KJ_ASSERT(foo & 4);

    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_ASSERT(foo & 2);
    }));

    KJ_EXPECT(exception.getDescription() == "expected foo & 2");
  }
}

KJ_TEST("assertion diagnostics are captured before argument temporaries are destroyed") {
  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_FAIL_REQUIRE(kj::str("temporary pointer").cStr());
    }));
    KJ_EXPECT(exception.getDescription() == "temporary pointer");
  }

  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_REQUIRE(false, kj::str("temporary view").asPtr());
    }));
    KJ_EXPECT(exception.getDescription() == "expected false; temporary view");
  }

  {
    // The argument is a reference to a subobject, not an owning temporary that can be moved.
    struct Temporary {
      kj::String text = kj::str("temporary subobject");
      kj::String& getText() { return text; }
    };
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_FAIL_ASSERT(Temporary().getText());
    }));
    KJ_EXPECT(exception.getDescription() == "Temporary().getText() = temporary subobject");
  }
}

struct NonmovableDiagnostic {
  NonmovableDiagnostic() = default;
  KJ_DISALLOW_COPY_AND_MOVE(NonmovableDiagnostic);
};

kj::StringPtr KJ_STRINGIFY(const NonmovableDiagnostic&) {
  return "nonmovable";
}

KJ_TEST("assertion diagnostics do not require moving their arguments") {
  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_UNIMPLEMENTED(kj::str("owning temporary"));
    }));
    KJ_EXPECT(exception.getType() == kj::Exception::Type::UNIMPLEMENTED);
    KJ_EXPECT(exception.getDescription() == "owning temporary");
  }

  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_FAIL_REQUIRE(NonmovableDiagnostic());
    }));
    KJ_EXPECT(exception.getDescription() == "NonmovableDiagnostic() = nonmovable");
  }

  {
    auto value = kj::str("not moved");
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_FAIL_REQUIRE(kj::mv(value));
    }));
    KJ_EXPECT(exception.getDescription() == "kj::mv(value) = not moved");
    KJ_EXPECT(value == "not moved");
  }
}

struct SmallDiagnostic {
  // Trivially copyable and as big as two pointers, like an iterator.
  const int* ptr;
  size_t size;

  bool operator==(const SmallDiagnostic& other) const { return size == other.size; }
};

kj::String KJ_STRINGIFY(const SmallDiagnostic& value) {
  return kj::str("small ", value.size);
}

struct PointerDiagnostic {
  // Trivially copyable and as big as a pointer, like most iterators.
  const int* ptr;

  bool operator==(const PointerDiagnostic& other) const { return ptr == other.ptr; }
};

kj::String KJ_STRINGIFY(const PointerDiagnostic& value) {
  return kj::str("pointer to ", *value.ptr);
}

struct Unstringifiable {
  int value;
  bool operator==(const Unstringifiable& other) const { return value == other.value; }
};

KJ_TEST("assertion diagnostics stringify arguments of any type") {
  {
    int i = 0;
    SmallDiagnostic small { &i, 7 };
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_FAIL_REQUIRE("message", small);
    }));
    KJ_EXPECT(exception.getDescription() == "message; small = small 7");
  }

  {
    int i = 0;
    SmallDiagnostic a { &i, 7 };
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_ASSERT(a == SmallDiagnostic({ &i, 8 }));
    }));
    KJ_EXPECT(exception.getDescription() ==
        "expected a == SmallDiagnostic({ &i, 8 }) [small 7 == small 8]");
  }

  {
    int i = 1;
    int j = 2;
    PointerDiagnostic a { &i };
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_ASSERT(a == PointerDiagnostic { &j });
    }));
    KJ_EXPECT(exception.getDescription() ==
        "expected a == PointerDiagnostic { &j } [pointer to 1 == pointer to 2]");
  }

  {
    Unstringifiable a { 1 };
    Unstringifiable b { 2 };
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_ASSERT(a == b);
    }));
    KJ_EXPECT(exception.getDescription() ==
        "expected a == b [(can't stringify) == (can't stringify)]");
  }

  {
    // A char array is stringified up to the first NUL, whatever its size.
    char buffer[8] = { 'a', 'b', '\0', 'c' };
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_FAIL_REQUIRE(buffer);
    }));
    KJ_EXPECT(exception.getDescription() == "buffer = ab");
    KJ_EXPECT(KJ_EXCEPTION(FAILED, buffer).getDescription() == "buffer = ab");
  }

  {
    // More arguments than are stringified on the stack.
    int a = 1, b = 2, c = 3, d = 4, e = 5;
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_ASSERT(a == b, "message", a, b, c, d, e);
    }));
    KJ_EXPECT(exception.getDescription() ==
        "expected a == b [1 == 2]; message; a = 1; b = 2; c = 3; d = 4; e = 5");
  }
}

KJ_NOINLINE void failFailAssert(void*& callerFrame) {
  callerFrame = KJ_CALLING_ADDRESS();
  KJ_FAIL_ASSERT();
}
KJ_NOINLINE void failAssertWithArgs(void*& callerFrame, int i) {
  callerFrame = KJ_CALLING_ADDRESS();
  KJ_ASSERT(i == 0, i, "message");
}
KJ_NOINLINE void failSyscall(void*& callerFrame) {
  callerFrame = KJ_CALLING_ADDRESS();
  KJ_SYSCALL(mockSyscall(-1, EBADF));
}
#if __clang__
KJ_NOINLINE void failDtorAssert(void*& callerFrame, int i) {
  callerFrame = KJ_CALLING_ADDRESS();
  KJ_DTOR_ASSERT(i == 0, i, "message");
}
KJ_NOINLINE void failDtorSyscall(void*& callerFrame) {
  callerFrame = KJ_CALLING_ADDRESS();
  KJ_DTOR_SYSCALL(mockSyscall(-1, EBADF));
}
#endif

KJ_TEST("failed check's stack trace starts in the function containing the check") {
  // The function that throws must skip exactly its own frame: the trace should start with the
  // failing function, followed by its caller.
  auto check = [](auto&& func) {
    void* callerFrame = nullptr;
    auto e = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() { func(callerFrame); }));
    if (isStackTraceSupported()) {
      auto trace = e.getStackTrace();
      KJ_ASSERT(trace.size() >= 2);
      KJ_EXPECT(trace[0] != callerFrame);
      KJ_EXPECT(trace[1] == callerFrame);
    }
  };

  check([](void*& callerFrame) { failFailAssert(callerFrame); });
  check([](void*& callerFrame) { failAssertWithArgs(callerFrame, 123); });
  check([](void*& callerFrame) { failSyscall(callerFrame); });
#if __clang__
  // Debug::failUnlessUnwinding() returns, so it could be tail-called, losing the frame of the
  // function containing the check. KJ_NOT_TAIL_CALLED prevents this, but only works with Clang.
  check([](void*& callerFrame) { failDtorAssert(callerFrame, 123); });
  check([](void*& callerFrame) { failDtorSyscall(callerFrame); });
#endif
}

// =======================================================================================
// KJ_DTOR_* macros

class LogCapture: public ExceptionCallback {
public:
  kj::Vector<kj::String> messages;

  void logMessage(LogSeverity severity, const char* file, int line, int contextDepth,
                  String&& text) override {
    messages.add(kj::str(severity, ": ", text));
  }
};

KJ_TEST("KJ_DTOR_* macros do nothing if the check passes") {
  LogCapture capture;
  bool reachedEnd = false;
  {
    KJ_DEFER({
      KJ_DTOR_ASSERT(1 + 1 == 2);
      KJ_DTOR_REQUIRE(true, "message");
      KJ_DTOR_SYSCALL(mockSyscall(0));
      reachedEnd = true;
    });
  }
  KJ_EXPECT(reachedEnd);

  // Also while unwinding.
  reachedEnd = false;
  KJ_EXPECT_THROW_MESSAGE("primary", {
    KJ_DEFER({
      KJ_DTOR_ASSERT(1 + 1 == 2);
      KJ_DTOR_SYSCALL(mockSyscall(0));
      reachedEnd = true;
    });
    KJ_FAIL_ASSERT("primary");
  });
  KJ_EXPECT(reachedEnd);

  KJ_EXPECT(capture.messages.size() == 0, capture.messages);
}

KJ_TEST("KJ_DTOR_* macros throw like the plain macros if not unwinding") {
  bool reachedEnd = false;
  int i = 123;

  {
    auto e = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_DEFER({
        KJ_DTOR_ASSERT(i == 0, "message");
        reachedEnd = true;
      });
    }));
    KJ_EXPECT(e.getType() == Exception::Type::FAILED);
    KJ_EXPECT(e.getDescription() == "expected i == 0 [123 == 0]; message", e.getDescription());
    KJ_EXPECT(!reachedEnd);
  }

  {
    auto e = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_DTOR_FAIL_REQUIRE("message", i);
    }));
    KJ_EXPECT(e.getDescription() == "message; i = 123", e.getDescription());
  }

  {
    auto e = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_DTOR_SYSCALL(mockSyscall(-1, EBADF), i);
    }));
    KJ_EXPECT(e.getType() == Exception::Type::FAILED);
    KJ_EXPECT(e.getDescription() ==
        kj::str("mockSyscall(-1, EBADF): ", strerror(EBADF), "; i = 123"), e.getDescription());
  }

  {
    auto e = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_DTOR_FAIL_SYSCALL("close", ECONNRESET, i);
    }));
    KJ_EXPECT(e.getType() == Exception::Type::DISCONNECTED);
    KJ_EXPECT(e.getDescription() == kj::str("close: ", strerror(ECONNRESET), "; i = 123"),
              e.getDescription());
  }
}

KJ_TEST("KJ_DTOR_* macros log and continue if unwinding") {
  LogCapture capture;
  bool reachedEnd = false;
  int i = 123;

  auto e = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
    KJ_DEFER({
      KJ_DTOR_ASSERT(i == 0, "message");
      KJ_DTOR_SYSCALL(mockSyscall(-1, EBADF), i);
      KJ_DTOR_FAIL_SYSCALL("close", ECONNRESET);
      KJ_DTOR_FAIL_ASSERT("last");
      reachedEnd = true;
    });
    KJ_FAIL_ASSERT("primary");
  }));

  // The primary exception propagates, and the rest of the destructor ran.
  KJ_EXPECT(e.getDescription() == "primary", e.getDescription());
  KJ_EXPECT(reachedEnd);

  KJ_ASSERT(capture.messages.size() == 4, capture.messages);
  KJ_EXPECT(capture.messages[0].startsWith(
      "error: failed: expected i == 0 [123 == 0]; message\n"), capture.messages[0]);
  KJ_EXPECT(capture.messages[1].startsWith(kj::str(
      "error: failed: mockSyscall(-1, EBADF): ", strerror(EBADF), "; i = 123\n")),
      capture.messages[1]);
  KJ_EXPECT(capture.messages[2].startsWith(kj::str(
      "error: disconnected: close: ", strerror(ECONNRESET), "\n")), capture.messages[2]);
  KJ_EXPECT(capture.messages[3].startsWith("error: failed: last\n"), capture.messages[3]);
}

KJ_TEST("KJ_DTOR_* failure logged while unwinding includes KJ_CONTEXT once") {
  LogCapture capture;

  KJ_EXPECT_THROW_MESSAGE("primary", {
    KJ_DEFER({
      KJ_CONTEXT("context marker");
      KJ_DTOR_FAIL_ASSERT("secondary");
    });
    KJ_FAIL_ASSERT("primary");
  });

  KJ_ASSERT(capture.messages.size() == 2, capture.messages);
  KJ_EXPECT(capture.messages[0] == "info: context: context marker\n", capture.messages[0]);
  KJ_EXPECT(capture.messages[1].startsWith("error: failed: secondary\n"), capture.messages[1]);
  KJ_EXPECT(!capture.messages[1].contains("context marker"), capture.messages[1]);
}

// =======================================================================================
// Stack frame overhead of assertion macros
//
// Measures the exact number of stack bytes that kj/debug.h assertion macros add to the frame of
// the function that contains them.
//
// Each check sits in a KJ_NOINLINE function of the form:
//
//     void check() {
//       <inputs> = opaque(...);
//       KJ_ASSERT(...);
//       probe();
//     }
//
// measureFrame() calls the check through a function pointer and computes the distance between
// its own frame and the frame of probe(). That distance is the frame size of the check plus a
// constant, which is cancelled out by subtracting the distance measured for a check without
// any assertion. None of the checks ever fail, so this is the stack cost of the fast path.
//
// Ideally an assertion costs no stack at all: the failure path only needs the operands in
// registers. Any stack slot it does need is reserved, and often written to, every time the
// function runs.

#if defined(__GNUC__)

template <typename T>
KJ_ALWAYS_INLINE(T opaque(T value));
template <typename T>
inline T opaque(T value) {
  // Returns `value`, but hides it from the optimizer, so the check can't be constant-folded.
  asm volatile("" : "+r"(value));
  return value;
}

template <typename T>
KJ_ALWAYS_INLINE(void use(T value));
template <typename T>
inline void use(T value) {
  // Forces `value` to be computed, without storing it anywhere.
  asm volatile("" :: "r"(value));
}

const char* probeFrame = nullptr;

KJ_NOINLINE void probe() {
  probeFrame = static_cast<const char*>(__builtin_frame_address(0));
}

#define KJ_FRAME_PROBE() \
  probe(); \
  asm volatile("" ::: "memory")
// Records the stack depth. The trailing asm keeps the call from being turned into a tail call,
// which would pop the check's frame before probe() runs.

KJ_NOINLINE ptrdiff_t measureFrame(void (*check)()) {
  const char* top = static_cast<const char*>(__builtin_frame_address(0));
  check = opaque(check);  // Don't let the compiler specialize this function for each check.
  probeFrame = nullptr;
  check();
  asm volatile("" ::: "memory");
  KJ_ASSERT(probeFrame != nullptr);
  return top - probeFrame;
}

int value = 1;
int* const SENTINEL = reinterpret_cast<int*>(1);
const kj::StringPtr stringPtr = "string"_kj;
const kj::String string = kj::str("string");

// ---------------------------------------------------------------------------------------
// Checks. Each one is measured relative to `baseline()`.

KJ_NOINLINE void baseline() {
  use(opaque(&value));
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertTruthy() {
  int* ptr = opaque(&value);
  KJ_ASSERT(ptr);
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertTruthyMessage() {
  int* ptr = opaque(&value);
  KJ_ASSERT(ptr, "ptr must be set");
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertEq() {
  int a = opaque(1);
  int b = opaque(1);
  KJ_ASSERT(a == b);
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertNeMessage() {
  int* ptr = opaque(&value);
  KJ_ASSERT(ptr != SENTINEL, "should only be called once");
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertCheapArgs() {
  size_t a = opaque(size_t(1));
  size_t b = opaque(size_t(2));
  KJ_ASSERT(a < b, "out of range", a, b);
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertStringPtrArg() {
  // kj::StringPtr fits in two registers, but isn't trivially copyable, as it has a user-provided
  // copy constructor.
  int* ptr = opaque(&value);
  kj::StringPtr name = *opaque(&stringPtr);
  KJ_ASSERT(ptr, "ptr must be set", name);
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertSmallClassArg() {
  // SmallDiagnostic is trivially copyable and fits in two registers.
  int* ptr = opaque(&value);
  SmallDiagnostic small { opaque(&value), opaque(size_t(1)) };
  KJ_ASSERT(ptr, "ptr must be set", small);
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertSmallClassEq() {
  // The operands are compared by reference, since together they don't fit in two registers.
  SmallDiagnostic a { opaque(&value), opaque(size_t(1)) };
  SmallDiagnostic b { opaque(&value), opaque(size_t(1)) };
  KJ_ASSERT(a == b);
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertPointerClassEq() {
  PointerDiagnostic a { opaque(&value) };
  PointerDiagnostic b { opaque(&value) };
  KJ_ASSERT(a == b);
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertExpensiveArg() {
  // kj::String isn't trivially copyable. Here it's a reference to an object outside the frame.
  int* ptr = opaque(&value);
  const kj::String& name = *opaque(&string);
  KJ_ASSERT(ptr, "ptr must be set", name);
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void failAssert() {
  int* ptr = opaque(&value);
  if (KJ_UNLIKELY(ptr == nullptr)) {
    KJ_FAIL_ASSERT("ptr must be set");
  }
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void failAssertCheapArg() {
  int* ptr = opaque(&value);
  if (KJ_UNLIKELY(ptr == SENTINEL)) {
    KJ_FAIL_ASSERT("ptr must not be the sentinel", ptr);
  }
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertNonNull() {
  kj::Maybe<int&> maybe = opaque(&value);
  use(&KJ_ASSERT_NONNULL(maybe));
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertNonNullMessage() {
  kj::Maybe<int&> maybe = opaque(&value);
  use(&KJ_ASSERT_NONNULL(maybe, "value must be set"));
  KJ_FRAME_PROBE();
}

KJ_NOINLINE void assertTwice() {
  size_t a = opaque(size_t(1));
  size_t b = opaque(size_t(2));
  KJ_ASSERT(a < b, "out of range", a, b);
  KJ_ASSERT(a != 0, "zero", a);
  KJ_FRAME_PROBE();
}

ptrdiff_t frameOverhead(void (*check)()) {
  return measureFrame(check) - measureFrame(baseline);
}

KJ_TEST("stack frame overhead of assertion macros") {
  // Frame sizes vary between compilers, architectures and optimization levels. Like
  // async-coroutine-alloc-test, only check them in the clang opt build, but always run the
  // checks. We intentionally check exact values, so that any change (in either direction) is
  // noticed.
#if defined(__clang__) && defined(__x86_64__) && defined(KJ_DEBUG_TEST_ASSERT_FRAME_SIZE)
  #define KJ_EXPECT_FRAME(check, size) KJ_EXPECT(frameOverhead(check) == size)
#else
  #define KJ_EXPECT_FRAME(check, size) frameOverhead(check)
#endif

  KJ_EXPECT(frameOverhead(baseline) == 0);

  KJ_EXPECT_FRAME(assertTruthy, 0);
  KJ_EXPECT_FRAME(assertTruthyMessage, 0);
  KJ_EXPECT_FRAME(assertEq, 0);
  KJ_EXPECT_FRAME(assertNeMessage, 0);
  KJ_EXPECT_FRAME(assertCheapArgs, 0);
  KJ_EXPECT_FRAME(assertStringPtrArg, 16);
  KJ_EXPECT_FRAME(assertSmallClassArg, 0);
  KJ_EXPECT_FRAME(assertSmallClassEq, 32);
  KJ_EXPECT_FRAME(assertPointerClassEq, 0);
  KJ_EXPECT_FRAME(assertExpensiveArg, 0);
  KJ_EXPECT_FRAME(failAssert, 0);
  KJ_EXPECT_FRAME(failAssertCheapArg, 0);
  KJ_EXPECT_FRAME(assertNonNull, 0);
  KJ_EXPECT_FRAME(assertNonNullMessage, 0);
  KJ_EXPECT_FRAME(assertTwice, 0);

  #undef KJ_EXPECT_FRAME
}

#endif  // defined(__GNUC__)

}  // namespace
}  // namespace _ (private)
}  // namespace kj
