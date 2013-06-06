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

#include "debug.h"
#include "exception.h"
#include <gtest/gtest.h>
#include <string>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <exception>

namespace kj {
namespace _ {  // private
namespace {

class MockException {};

class MockExceptionCallback: public ExceptionCallback {
public:
  ~MockExceptionCallback() {}

  std::string text;

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
    throw MockException();
  }

  void logMessage(const char* file, int line, int contextDepth, String&& text) override {
    this->text += "log message: ";
    text = str(file, ":", line, ":+", contextDepth, ": ", mv(text));
    this->text.append(text.begin(), text.end());
  }
};

std::string fileLine(std::string file, int line) {
  file += ':';
  char buffer[32];
  sprintf(buffer, "%d", line);
  file += buffer;
  return file;
}

TEST(Logging, Log) {
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
  Log::setLogLevel(Log::Severity::INFO);
  KJ_LOG(INFO, "Some text."); line = __LINE__;
  EXPECT_EQ("log message: " + fileLine(__FILE__, line) + ":+0: info: Some text.\n",
            mockCallback.text);
  mockCallback.text.clear();

  // Back to default.
  Log::setLogLevel(Log::Severity::WARNING);

  KJ_ASSERT(1 == 1);
  EXPECT_THROW(KJ_ASSERT(1 == 2), MockException); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": bug in code: expected "
            "1 == 2\n", mockCallback.text);
  mockCallback.text.clear();

  KJ_ASSERT(1 == 1) {
    ADD_FAILURE() << "Shouldn't call recovery code when check passes.";
    break;
  };

  bool recovered = false;
  KJ_ASSERT(1 == 2, "1 is not 2") { recovered = true; break; } line = __LINE__;
  EXPECT_EQ("recoverable exception: " + fileLine(__FILE__, line) + ": bug in code: expected "
            "1 == 2; 1 is not 2\n", mockCallback.text);
  EXPECT_TRUE(recovered);
  mockCallback.text.clear();

  EXPECT_THROW(KJ_ASSERT(1 == 2, i, "hi", str), MockException); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": bug in code: expected "
            "1 == 2; i = 123; hi; str = foo\n", mockCallback.text);
  mockCallback.text.clear();

  EXPECT_THROW(KJ_REQUIRE(1 == 2, i, "hi", str), MockException); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": requirement not met: expected "
            "1 == 2; i = 123; hi; str = foo\n", mockCallback.text);
  mockCallback.text.clear();

  EXPECT_THROW(KJ_FAIL_ASSERT("foo"), MockException); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": bug in code: foo\n",
            mockCallback.text);
  mockCallback.text.clear();
}

TEST(Logging, Catch) {
  int line;

  // Catch as kj::Exception.
  try {
    line = __LINE__; KJ_FAIL_ASSERT("foo");
    ADD_FAILURE() << "Expected exception.";
  } catch (const Exception& e) {
    String what = str(e);
    std::string text(what.cStr(), strchr(what.cStr(), '\n') - what.cStr());
    EXPECT_EQ(fileLine(__FILE__, line) + ": bug in code: foo", text);
  }

  // Catch as std::exception.
  try {
    line = __LINE__; KJ_FAIL_ASSERT("foo");
    ADD_FAILURE() << "Expected exception.";
  } catch (const std::exception& e) {
    const char* what = e.what();
    std::string text(what, strchr(what, '\n') - what);
    EXPECT_EQ(fileLine(__FILE__, line) + ": bug in code: foo", text);
  }
}

TEST(Logging, Syscall) {
  MockExceptionCallback mockCallback;
  int line;

  int i = 123;
  const char* str = "foo";

  int fd;
  KJ_SYSCALL(fd = dup(STDIN_FILENO));
  KJ_SYSCALL(close(fd));
  EXPECT_THROW(KJ_SYSCALL(close(fd), i, "bar", str), MockException); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": error from OS: close(fd): "
            + strerror(EBADF) + "; i = 123; bar; str = foo\n", mockCallback.text);
  mockCallback.text.clear();

  int result = 0;
  bool recovered = false;
  KJ_SYSCALL(result = close(fd), i, "bar", str) { recovered = true; break; } line = __LINE__;
  EXPECT_EQ("recoverable exception: " + fileLine(__FILE__, line) + ": error from OS: close(fd): "
            + strerror(EBADF) + "; i = 123; bar; str = foo\n", mockCallback.text);
  EXPECT_LT(result, 0);
  EXPECT_TRUE(recovered);
}

TEST(Logging, Context) {
  MockExceptionCallback mockCallback;

  {
    KJ_CONTEXT("foo"); int cline = __LINE__;

    KJ_LOG(WARNING, "blah"); int line = __LINE__;
    EXPECT_EQ("log message: " + fileLine(__FILE__, cline) + ":+0: context: foo\n"
              "log message: " + fileLine(__FILE__, line) + ":+1: warning: blah\n",
              mockCallback.text);
    mockCallback.text.clear();

    EXPECT_THROW(KJ_FAIL_ASSERT("bar"), MockException); line = __LINE__;
    EXPECT_EQ("fatal exception: " + fileLine(__FILE__, cline) + ": context: foo\n"
              + fileLine(__FILE__, line) + ": bug in code: bar\n",
              mockCallback.text);
    mockCallback.text.clear();

    {
      int i = 123;
      const char* str = "qux";
      KJ_CONTEXT("baz", i, "corge", str); int cline2 = __LINE__;
      EXPECT_THROW(KJ_FAIL_ASSERT("bar"), MockException); line = __LINE__;

      EXPECT_EQ("fatal exception: " + fileLine(__FILE__, cline) + ": context: foo\n"
                + fileLine(__FILE__, cline2) + ": context: baz; i = 123; corge; str = qux\n"
                + fileLine(__FILE__, line) + ": bug in code: bar\n",
                mockCallback.text);
      mockCallback.text.clear();
    }

    {
      KJ_CONTEXT("grault"); int cline2 = __LINE__;
      EXPECT_THROW(KJ_FAIL_ASSERT("bar"), MockException); line = __LINE__;

      EXPECT_EQ("fatal exception: " + fileLine(__FILE__, cline) + ": context: foo\n"
                + fileLine(__FILE__, cline2) + ": context: grault\n"
                + fileLine(__FILE__, line) + ": bug in code: bar\n",
                mockCallback.text);
      mockCallback.text.clear();
    }
  }
}

TEST(Logging, ExceptionCallbackMustBeOnStack) {
  // TODO(cleanup):  Put in exception-test.c++, when it exists.
  EXPECT_ANY_THROW(new ExceptionCallback);
}

}  // namespace
}  // namespace _ (private)
}  // namespace kj
