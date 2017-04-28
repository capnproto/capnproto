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

#include "debug.h"
#include "exception.h"
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

#if _MSC_VER
#pragma warning(disable: 4996)
// Warns that sprintf() is buffer-overrunny. Yeah, I know, it's cool.
#endif

namespace kj {
namespace _ {  // private
namespace {

class MockException {};

class MockExceptionCallback: public ExceptionCallback {
public:
  ~MockExceptionCallback() {}

  std::string text;

  int outputPipe = -1;

  bool forkForDeathTest() {
    // This is called when exceptions are disabled.  We fork the process instead and then expect
    // the child to die.

#if _WIN32
    // Windows doesn't support fork() or anything like it. Just skip the test.
    return false;

#else
    int pipeFds[2];
    KJ_SYSCALL(pipe(pipeFds));
    pid_t child = fork();
    if (child == 0) {
      // This is the child!
      close(pipeFds[0]);
      outputPipe = pipeFds[1];
      return true;
    } else {
      close(pipeFds[1]);

      // Read child error messages into our local buffer.
      char buf[1024];
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
          text.append(buf, n);
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

      text.clear();
    }
  }

  void onRecoverableException(Exception&& exception) override {
    text += "recoverable exception: ";
    auto what = str(exception);
    // Discard the stack trace.
    const char* end = strstr(what.cStr(), "\nstack: ");
    if (end == nullptr) {
      text += what.cStr();
    } else {
      text.append(what.cStr(), end);
    }
    text += '\n';
    flush();
  }

  void onFatalException(Exception&& exception) override {
    text += "fatal exception: ";
    auto what = str(exception);
    // Discard the stack trace.
    const char* end = strstr(what.cStr(), "\nstack: ");
    if (end == nullptr) {
      text += what.cStr();
    } else {
      text.append(what.cStr(), end);
    }
    text += '\n';
    flush();
#if KJ_NO_EXCEPTIONS
    if (outputPipe >= 0) {
      // This is a child process.  We got what we want, now exit quickly without writing any
      // additional messages, with a status code that the parent will interpret as "exited in the
      // way we expected".
      _exit(74);
    }
#else
    throw MockException();
#endif
  }

  void logMessage(LogSeverity severity, const char* file, int line, int contextDepth,
                  String&& text) override {
    this->text += "log message: ";
    text = str(file, ":", line, ":+", contextDepth, ": ", severity, ": ", mv(text));
    this->text.append(text.begin(), text.end());
  }
};

#if KJ_NO_EXCEPTIONS
#define EXPECT_FATAL(code) if (mockCallback.forkForDeathTest()) { code; abort(); }
#else
#define EXPECT_FATAL(code) \
  try { code; KJ_FAIL_EXPECT("expected exception"); } \
  catch (MockException e) {} \
  catch (...) { KJ_FAIL_EXPECT("wrong exception"); }
#endif

std::string fileLine(std::string file, int line) {
  file = trimSourceFilename(file.c_str()).cStr();

  file += ':';
  char buffer[32];
  sprintf(buffer, "%d", line);
  file += buffer;
  return file;
}

TEST(Debug, Log) {
  MockExceptionCallback mockCallback;
  int line;

  KJ_LOG(WARNING, "Hello world!"); line = __LINE__;
  EXPECT_EQ("log message: " + fileLine(__FILE__, line) + ":+0: warning: Hello world!\n",
            mockCallback.text);
  mockCallback.text.clear();

  int i = 123;
  const char* str = "foo";

  KJ_LOG(ERROR, i, str); line = __LINE__;
  EXPECT_EQ("log message: " + fileLine(__FILE__, line) + ":+0: error: i = 123; str = foo\n",
            mockCallback.text);
  mockCallback.text.clear();

  KJ_DBG("Some debug text."); line = __LINE__;
  EXPECT_EQ("log message: " + fileLine(__FILE__, line) + ":+0: debug: Some debug text.\n",
            mockCallback.text);
  mockCallback.text.clear();

  // INFO logging is disabled by default.
  KJ_LOG(INFO, "Info."); line = __LINE__;
  EXPECT_EQ("", mockCallback.text);
  mockCallback.text.clear();

  // Enable it.
  Debug::setLogLevel(Debug::Severity::INFO);
  KJ_LOG(INFO, "Some text."); line = __LINE__;
  EXPECT_EQ("log message: " + fileLine(__FILE__, line) + ":+0: info: Some text.\n",
            mockCallback.text);
  mockCallback.text.clear();

  // Back to default.
  Debug::setLogLevel(Debug::Severity::WARNING);

  KJ_ASSERT(1 == 1);
  EXPECT_FATAL(KJ_ASSERT(1 == 2)); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": failed: expected "
            "1 == 2\n", mockCallback.text);
  mockCallback.text.clear();

  KJ_ASSERT(1 == 1) {
    ADD_FAILURE() << "Shouldn't call recovery code when check passes.";
    break;
  };

  bool recovered = false;
  KJ_ASSERT(1 == 2, "1 is not 2") { recovered = true; break; } line = __LINE__;
  EXPECT_EQ("recoverable exception: " + fileLine(__FILE__, line) + ": failed: expected "
            "1 == 2; 1 is not 2\n", mockCallback.text);
  EXPECT_TRUE(recovered);
  mockCallback.text.clear();

  EXPECT_FATAL(KJ_ASSERT(1 == 2, i, "hi", str)); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": failed: expected "
            "1 == 2; i = 123; hi; str = foo\n", mockCallback.text);
  mockCallback.text.clear();

  EXPECT_FATAL(KJ_REQUIRE(1 == 2, i, "hi", str)); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": failed: expected "
            "1 == 2; i = 123; hi; str = foo\n", mockCallback.text);
  mockCallback.text.clear();

  EXPECT_FATAL(KJ_FAIL_ASSERT("foo")); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": failed: foo\n",
            mockCallback.text);
  mockCallback.text.clear();
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

    KJ_IF_MAYBE(e, exception) {
      String what = str(*e);
      KJ_IF_MAYBE(eol, what.findFirst('\n')) {
        what = kj::str(what.slice(0, *eol));
      }
      std::string text(what.cStr());
      EXPECT_EQ(fileLine(__FILE__, line) + ": failed: foo", text);
    } else {
      ADD_FAILURE() << "Expected exception.";
    }
  }

#if !KJ_NO_EXCEPTIONS
  {
    // Catch fatal as kj::Exception.
    Maybe<Exception> exception = kj::runCatchingExceptions([&](){
      line = __LINE__; KJ_FAIL_ASSERT("foo");
    });

    KJ_IF_MAYBE(e, exception) {
      String what = str(*e);
      KJ_IF_MAYBE(eol, what.findFirst('\n')) {
        what = kj::str(what.slice(0, *eol));
      }
      std::string text(what.cStr());
      EXPECT_EQ(fileLine(__FILE__, line) + ": failed: foo", text);
    } else {
      ADD_FAILURE() << "Expected exception.";
    }
  }

  {
    // Catch as std::exception.
    try {
      line = __LINE__; KJ_FAIL_ASSERT("foo");
      ADD_FAILURE() << "Expected exception.";
    } catch (const std::exception& e) {
      kj::StringPtr what = e.what();
      std::string text;
      KJ_IF_MAYBE(eol, what.findFirst('\n')) {
        text.assign(what.cStr(), *eol);
      } else {
        text.assign(what.cStr());
      }
      EXPECT_EQ(fileLine(__FILE__, line) + ": failed: foo", text);
    }
  }
#endif
}

int mockSyscall(int i, int error = 0) {
  errno = error;
  return i;
}

TEST(Debug, Syscall) {
  MockExceptionCallback mockCallback;
  int line;

  int i = 123;
  const char* str = "foo";

  KJ_SYSCALL(mockSyscall(0));
  KJ_SYSCALL(mockSyscall(1));

  EXPECT_FATAL(KJ_SYSCALL(mockSyscall(-1, EBADF), i, "bar", str)); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) +
            ": failed: mockSyscall(-1, EBADF): " + strerror(EBADF) +
            "; i = 123; bar; str = foo\n", mockCallback.text);
  mockCallback.text.clear();

  EXPECT_FATAL(KJ_SYSCALL(mockSyscall(-1, ECONNRESET), i, "bar", str)); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) +
            ": disconnected: mockSyscall(-1, ECONNRESET): " + strerror(ECONNRESET) +
            "; i = 123; bar; str = foo\n", mockCallback.text);
  mockCallback.text.clear();

  EXPECT_FATAL(KJ_SYSCALL(mockSyscall(-1, ENOMEM), i, "bar", str)); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) +
            ": overloaded: mockSyscall(-1, ENOMEM): " + strerror(ENOMEM) +
            "; i = 123; bar; str = foo\n", mockCallback.text);
  mockCallback.text.clear();

  EXPECT_FATAL(KJ_SYSCALL(mockSyscall(-1, ENOSYS), i, "bar", str)); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) +
            ": unimplemented: mockSyscall(-1, ENOSYS): " + strerror(ENOSYS) +
            "; i = 123; bar; str = foo\n", mockCallback.text);
  mockCallback.text.clear();

  int result = 0;
  bool recovered = false;
  KJ_SYSCALL(result = mockSyscall(-2, EBADF), i, "bar", str) { recovered = true; break; } line = __LINE__;
  EXPECT_EQ("recoverable exception: " + fileLine(__FILE__, line) +
            ": failed: mockSyscall(-2, EBADF): " + strerror(EBADF) +
            "; i = 123; bar; str = foo\n", mockCallback.text);
  EXPECT_EQ(-2, result);
  EXPECT_TRUE(recovered);
}

TEST(Debug, Context) {
  MockExceptionCallback mockCallback;

  {
    KJ_CONTEXT("foo"); int cline = __LINE__;

    KJ_LOG(WARNING, "blah"); int line = __LINE__;
    EXPECT_EQ("log message: " + fileLine(__FILE__, cline) + ":+0: context: foo\n"
              "log message: " + fileLine(__FILE__, line) + ":+1: warning: blah\n",
              mockCallback.text);
    mockCallback.text.clear();

    EXPECT_FATAL(KJ_FAIL_ASSERT("bar")); line = __LINE__;
    EXPECT_EQ("fatal exception: " + fileLine(__FILE__, cline) + ": context: foo\n"
              + fileLine(__FILE__, line) + ": failed: bar\n",
              mockCallback.text);
    mockCallback.text.clear();

    {
      int i = 123;
      const char* str = "qux";
      KJ_CONTEXT("baz", i, "corge", str); int cline2 = __LINE__;
      EXPECT_FATAL(KJ_FAIL_ASSERT("bar")); line = __LINE__;

      EXPECT_EQ("fatal exception: " + fileLine(__FILE__, cline) + ": context: foo\n"
                + fileLine(__FILE__, cline2) + ": context: baz; i = 123; corge; str = qux\n"
                + fileLine(__FILE__, line) + ": failed: bar\n",
                mockCallback.text);
      mockCallback.text.clear();
    }

    {
      KJ_CONTEXT("grault"); int cline2 = __LINE__;
      EXPECT_FATAL(KJ_FAIL_ASSERT("bar")); line = __LINE__;

      EXPECT_EQ("fatal exception: " + fileLine(__FILE__, cline) + ": context: foo\n"
                + fileLine(__FILE__, cline2) + ": context: grault\n"
                + fileLine(__FILE__, line) + ": failed: bar\n",
                mockCallback.text);
      mockCallback.text.clear();
    }
  }
}

}  // namespace
}  // namespace _ (private)
}  // namespace kj
