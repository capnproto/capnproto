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
#include <kj/compat/gtest.h>
#include <string>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <exception>
#include <stdlib.h>

#include "miniposix.h"

#if !_WIN32
#include <sys/wait.h>
#endif

namespace kj {
namespace _ {  // private
namespace {

class MockException {};

class MockExceptionCallback: public ExceptionCallback {
public:
  ~MockExceptionCallback() {}

  kj::String text;

  int outputPipe = -1;

  bool forkForDeathTest() {
    // This is called when exceptions are disabled.  We fork the process instead and then expect
    // the child to die.

#if _WIN32
    // Windows doesn't support fork() or anything like it. Just skip the test.
    return false;

#else
    int pipeFds[2]{};
    KJ_SYSCALL(pipe(pipeFds));
    pid_t child = fork();
    if (child == 0) {
      // This is the child!
      close(pipeFds[0]);
      outputPipe = pipeFds[1];
      text = kj::String();
      return true;
    } else {
      close(pipeFds[1]);

      // Read child error messages into our local buffer.
      char buf[1024]{};
      for (;;) {
        ssize_t n = read(pipeFds[0], buf, sizeof(buf));
        if (n < 0) {
          if (errno == EINTR) {
            continue;
          } else {
            break;
          }
        } else if (n == 0) {
          break;
        } else {
          text = kj::str(text, kj::StringPtr(buf, n));
        }
      }

      close(pipeFds[0]);

      // Get exit status.
      int status;
      KJ_SYSCALL(waitpid(child, &status, 0));

      EXPECT_TRUE(WIFEXITED(status));
      EXPECT_EQ(74, WEXITSTATUS(status));

      return false;
    }
#endif  // _WIN32, else
  }

  void flush() {
    if (outputPipe != -1) {
      const char* pos = &*text.begin();
      const char* end = pos + text.size();

      while (pos < end) {
        miniposix::ssize_t n = miniposix::write(outputPipe, pos, end - pos);
        if (n < 0) {
          if (errno == EINTR) {
            continue;
          } else {
            break;  // Give up on error.
          }
        }
        pos += n;
      }

      text = kj::String();
    }
  }

  void onRecoverableException(Exception&& exception) override {
    auto what = str(exception);
    // Discard the stack trace.
    KJ_IF_SOME(end, what.find("\nstack: ")) {
      text = kj::str(text, "recoverable exception: ", what.first(end), '\n');
    } else {
      text = kj::str(text, "recoverable exception: ", what, '\n');
    }
    flush();
  }

  void onFatalException(Exception&& exception) override {
    auto what = str(exception);
    // Discard the stack trace.
    KJ_IF_SOME(end, what.find("\nstack: ")) {
      text = kj::str(text, "fatal exception: ", what.first(end), '\n');
    } else {
      text = kj::str(text, "fatal exception: ", what, '\n');
    }
    flush();
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
  }, kj::str("fatal exception: ", fileLine(__FILE__, line), ": failed: expected 1 == 2 [1 == 2]\n"));

  KJ_ASSERT(1 == 1) {
    ADD_FAILURE() << "Shouldn't call recovery code when check passes.";
    break;
  };

  bool recovered = false;
  EXPECT_LOG_EQ([&](){
    KJ_ASSERT(1 == 2, "1 is not 2") { recovered = true; break; } line = __LINE__;
  }, kj::str(
    "recoverable exception: ", fileLine(__FILE__, line), ": "
    "failed: expected 1 == 2 [1 == 2]; 1 is not 2\n"
  ));
  EXPECT_TRUE(recovered);

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_ASSERT(1 == 2, i, "hi", str)); line = __LINE__;
  }, kj::str(
    "fatal exception: ", fileLine(__FILE__, line), ": "
        "failed: expected 1 == 2 [1 == 2]; i = 123; hi; str = foo\n"
  ));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_REQUIRE(1 == 2, i, "hi", str)); line = __LINE__;
  }, kj::str(
    "fatal exception: ", fileLine(__FILE__, line), ": "
        "failed: expected 1 == 2 [1 == 2]; i = 123; hi; str = foo\n"
  ));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_FAIL_ASSERT("foo")); line = __LINE__;
  }, kj::str("fatal exception: ", fileLine(__FILE__, line), ": failed: foo\n"));
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
    return kj::str("fatal exception: ", fileLine(__FILE__, line), ": "
        "failed: expected cond; a_message\n");
  });

  expectLogEq([&](){
    expectFatal([&](){ myFailRequire(); }); line = __LINE__;
  }, [&](){
    return kj::str("fatal exception: ", fileLine(__FILE__, line), ": failed: a_message\n");
  });

  expectLogEq([&](){
    expectFatal([&](){ myRequireNonnull(kj::none); }); line = __LINE__;
  }, [&](){
    return kj::str("fatal exception: ", fileLine(__FILE__, line), ": "
        "failed: expected value != nullptr; a_message\n");
  });

  expectLogEq([&](){
    expectFatal([&](){ myAssert(1 == 2); }); line = __LINE__;
  }, [&](){
    return kj::str("fatal exception: ", fileLine(__FILE__, line), ": "
        "failed: expected cond; a_message\n");
  });

  expectLogEq([&](){
    expectFatal([&](){ myFailAssert(); }); line = __LINE__;
  }, [&](){
    return kj::str("fatal exception: ", fileLine(__FILE__, line), ": failed: a_message\n");
  });

  expectLogEq([&](){
    expectFatal([&](){ myAssertNonnull(kj::none); }); line = __LINE__;
  }, [&](){
    return kj::str("fatal exception: ", fileLine(__FILE__, line), ": "
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
    // Catch recoverable as kj::Exception.
    Maybe<Exception> exception = kj::runCatchingExceptions([&](){
      line = __LINE__; KJ_FAIL_ASSERT("foo") { break; }
    });

    KJ_IF_SOME(e, exception) {
      String what = str(e);
      KJ_IF_SOME(eol, what.findFirst('\n')) {
        what = kj::str(what.first(eol));
      }
      kj::StringPtr text = what;
      EXPECT_EQ(kj::str(fileLine(__FILE__, line), ": failed: foo"), text);
    } else {
      ADD_FAILURE() << "Expected exception.";
    }
  }

  {
    // Catch fatal as kj::Exception.
    Maybe<Exception> exception = kj::runCatchingExceptions([&](){
      line = __LINE__; KJ_FAIL_ASSERT("foo");
    });

    KJ_IF_SOME(e, exception) {
      String what = str(e);
      KJ_IF_SOME(eol, what.findFirst('\n')) {
        what = kj::str(what.first(eol));
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
    "fatal exception: ", fileLine(__FILE__, line),
        ": failed: mockSyscall(-1, EBADF): ", strerror(EBADF),
        "; i = 123; bar; str = foo\n"
  ));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_SYSCALL(mockSyscall(-1, ECONNRESET), i, "bar", str)); line = __LINE__;
  }, kj::str(
    "fatal exception: ", fileLine(__FILE__, line),
        ": disconnected: mockSyscall(-1, ECONNRESET): ", strerror(ECONNRESET),
        "; i = 123; bar; str = foo\n"
  ));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_SYSCALL(mockSyscall(-1, ENOMEM), i, "bar", str)); line = __LINE__;
  }, kj::str(
    "fatal exception: ", fileLine(__FILE__, line),
        ": overloaded: mockSyscall(-1, ENOMEM): ", strerror(ENOMEM),
        "; i = 123; bar; str = foo\n"
  ));

  EXPECT_LOG_EQ([&](){
    EXPECT_FATAL(KJ_SYSCALL(mockSyscall(-1, ENOSYS), i, "bar", str)); line = __LINE__;
  }, kj::str(
    "fatal exception: ", fileLine(__FILE__, line),
        ": unimplemented: mockSyscall(-1, ENOSYS): ", strerror(ENOSYS),
        "; i = 123; bar; str = foo\n"
  ));

  int result = 0;
  bool recovered = false;
  EXPECT_LOG_EQ([&](){
    KJ_SYSCALL(result = mockSyscall(-2, EBADF), i, "bar", str) { recovered = true; break; } line = __LINE__;
  }, kj::str(
    "recoverable exception: ", fileLine(__FILE__, line),
        ": failed: mockSyscall(-2, EBADF): ", strerror(EBADF),
        "; i = 123; bar; str = foo\n"
  ));
  EXPECT_EQ(-2, result);
  EXPECT_TRUE(recovered);
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
        "fatal exception: ", fileLine(__FILE__, cline), ": context: foo\n",
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
    "fatal exception: ", fileLine(__FILE__, cline), ": context: foo\n",
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
    "fatal exception: ", fileLine(__FILE__, cline), ": context: foo\n",
    fileLine(__FILE__, cline2), ": context: grault\n",
    fileLine(__FILE__, line), ": failed: bar\n"
  ));
}

KJ_TEST("magic assert stringification") {
  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      int foo = 123;
      int bar = 456;
      KJ_ASSERT(foo == bar) { break; }
    }));

    KJ_EXPECT(exception.getDescription() == "expected foo == bar [123 == 456]");
  }

  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      auto foo = kj::str("hello");
      auto bar = kj::str("world!");
      KJ_ASSERT(foo == bar, foo.size(), bar.size()) { break; }
    }));

    KJ_EXPECT(exception.getDescription() ==
        "expected foo == bar [hello == world!]; foo.size() = 5; bar.size() = 6");
  }

  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_ASSERT(kj::str("hello") == kj::str("world!")) { break; }
    }));

    KJ_EXPECT(exception.getDescription() ==
        "expected kj::str(\"hello\") == kj::str(\"world!\") [hello == world!]");
  }

  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      int foo = 123;
      int bar = 456;
      KJ_ASSERT((foo == bar)) { break; }
    }));

    KJ_EXPECT(exception.getDescription() == "expected (foo == bar)");
  }

  // Test use of << on left side, which could create confusion.
  {
    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      int foo = 123;
      int bar = 456;
      KJ_ASSERT(foo << 2 == bar) { break; }
    }));

    KJ_EXPECT(exception.getDescription() == "expected foo << 2 == bar [492 == 456]");
  }

  // Test use of & on left side.
  {
    int foo = 4;
    KJ_ASSERT(foo & 4);

    auto exception = KJ_ASSERT_NONNULL(kj::runCatchingExceptions([&]() {
      KJ_ASSERT(foo & 2) { break; }
    }));

    KJ_EXPECT(exception.getDescription() == "expected foo & 2");
  }
}

}  // namespace
}  // namespace _ (private)
}  // namespace kj
