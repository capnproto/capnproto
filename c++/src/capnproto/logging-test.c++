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

#define CAPNPROTO_PRIVATE
#include "logging.h"
#include "exception.h"
#include <gtest/gtest.h>
#include <string>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

namespace capnproto {
namespace internal {
namespace {

class MockException {};

class MockExceptionCallback: public ExceptionCallback {
public:
  ~MockExceptionCallback() {}

  std::string text;

  void onRecoverableException(Exception&& exception) override {
    text += "recoverable exception: ";
    // Discard the last line of "what" because it is a stack trace.
    const char* what = exception.what();
    const char* end = strrchr(what, '\n');
    if (end == nullptr) {
      text += exception.what();
    } else {
      text.append(what, end);
    }
    text += '\n';
  }

  void onFatalException(Exception&& exception) override {
    text += "fatal exception: ";
    // Discard the last line of "what" because it is a stack trace.
    const char* what = exception.what();
    const char* end = strrchr(what, '\n');
    if (end == nullptr) {
      text += exception.what();
    } else {
      text.append(what, end);
    }
    text += '\n';
    throw MockException();
  }

  void logMessage(ArrayPtr<const char> text) override {
    this->text += "log message: ";
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
  MockExceptionCallback::ScopedRegistration reg(mockCallback);
  int line;

  LOG(WARNING, "Hello world!"); line = __LINE__;
  EXPECT_EQ("log message: warning: " + fileLine(__FILE__, line) + ": Hello world!\n",
            mockCallback.text);
  mockCallback.text.clear();

  int i = 123;
  const char* str = "foo";

  LOG(ERROR, i, str); line = __LINE__;
  EXPECT_EQ("log message: error: " + fileLine(__FILE__, line) + ": i = 123; str = foo\n",
            mockCallback.text);
  mockCallback.text.clear();

  CHECK(1 == 1);
  EXPECT_THROW(CHECK(1 == 2), MockException); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": bug in code: expected "
            "1 == 2\n", mockCallback.text);
  mockCallback.text.clear();

  RECOVERABLE_CHECK(1 == 1) {
    ADD_FAILURE() << "Shouldn't call recovery code when check passes.";
  };

  bool recovered = false;
  RECOVERABLE_CHECK(1 == 2, "1 is not 2") { recovered = true; } line = __LINE__;
  EXPECT_EQ("recoverable exception: " + fileLine(__FILE__, line) + ": bug in code: expected "
            "1 == 2; 1 is not 2\n", mockCallback.text);
  EXPECT_TRUE(recovered);
  mockCallback.text.clear();

  EXPECT_THROW(CHECK(1 == 2, i, "hi", str), MockException); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": bug in code: expected "
            "1 == 2; i = 123; hi; str = foo\n", mockCallback.text);
  mockCallback.text.clear();

  EXPECT_THROW(PRECOND(1 == 2, i, "hi", str), MockException); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": precondition not met: expected "
            "1 == 2; i = 123; hi; str = foo\n", mockCallback.text);
  mockCallback.text.clear();

  EXPECT_THROW(CHECK(false, "foo"), MockException); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": bug in code: foo\n",
            mockCallback.text);
  mockCallback.text.clear();
}

TEST(Logging, Syscall) {
  MockExceptionCallback mockCallback;
  MockExceptionCallback::ScopedRegistration reg(mockCallback);
  int line;

  int i = 123;
  const char* str = "foo";

  int fd = SYSCALL(dup(STDIN_FILENO));
  SYSCALL(close(fd));
  EXPECT_THROW(SYSCALL(close(fd), i, "bar", str), MockException); line = __LINE__;
  EXPECT_EQ("fatal exception: " + fileLine(__FILE__, line) + ": error from OS: close(fd): "
            + strerror(EBADF) + "; i = 123; bar; str = foo\n", mockCallback.text);
  mockCallback.text.clear();

  int result = 0;
  bool recovered = false;
  RECOVERABLE_SYSCALL(result = close(fd), i, "bar", str) { recovered = true; } line = __LINE__;
  EXPECT_EQ("recoverable exception: " + fileLine(__FILE__, line) + ": error from OS: close(fd): "
            + strerror(EBADF) + "; i = 123; bar; str = foo\n", mockCallback.text);
  EXPECT_LT(result, 0);
  EXPECT_TRUE(recovered);
}

TEST(Logging, Context) {
  MockExceptionCallback mockCallback;
  MockExceptionCallback::ScopedRegistration reg(mockCallback);

  {
    CONTEXT("foo"); int cline = __LINE__;
    EXPECT_THROW(FAIL_CHECK("bar"), MockException); int line = __LINE__;

    EXPECT_EQ("fatal exception: " + fileLine(__FILE__, cline) + ": context: foo\n"
              + fileLine(__FILE__, line) + ": bug in code: bar\n",
              mockCallback.text);
    mockCallback.text.clear();

    {
      int i = 123;
      const char* str = "qux";
      CONTEXT("baz", i, "corge", str); int cline2 = __LINE__;
      EXPECT_THROW(FAIL_CHECK("bar"), MockException); line = __LINE__;

      EXPECT_EQ("fatal exception: " + fileLine(__FILE__, cline) + ": context: foo\n"
                + fileLine(__FILE__, cline2) + ": context: baz; i = 123; corge; str = qux\n"
                + fileLine(__FILE__, line) + ": bug in code: bar\n",
                mockCallback.text);
      mockCallback.text.clear();
    }

    {
      CONTEXT("grault"); int cline2 = __LINE__;
      EXPECT_THROW(FAIL_CHECK("bar"), MockException); line = __LINE__;

      EXPECT_EQ("fatal exception: " + fileLine(__FILE__, cline) + ": context: foo\n"
                + fileLine(__FILE__, cline2) + ": context: grault\n"
                + fileLine(__FILE__, line) + ": bug in code: bar\n",
                mockCallback.text);
      mockCallback.text.clear();
    }
  }
}

}  // namespace
}  // namespace internal
}  // namespace capnproto
