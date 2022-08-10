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

#include "test.h"

#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#else
#include <process.h>
#endif

namespace kj {
namespace _ {  // private

bool hasSubstring(StringPtr haystack, StringPtr needle) {
  if (needle.size() <= haystack.size()) {
    // Boyer Moore Horspool wins https://quick-bench.com/q/RiKdKduhdLb6x_DfS1fHaksqwdQ
    // https://quick-bench.com/q/KV8irwXrkvsNMbNpP8ENR_tBEPY but libc++ only has default_searcher
    // which performs *drastically worse* than the naiive algorithm (seriously - why even bother?).
    // Hell, doing a query for an embedded null & dispatching to strstr is still cheaper & only
    // marginally slower than the purely naiive implementation.

#if !defined(_WIN32)
    return memmem(haystack.begin(), haystack.size(), needle.begin(), needle.size()) != nullptr;
#else
    // TODO(perf): This is not the best algorithm for substring matching. strstr can't be used
    //   because this is supposed to be safe to call on strings with embedded nulls.
    // Amusingly this naiive algorithm some times outperforms std::default_searcher, even if we need
    // to double-check first if the needle has an embedded null (indicating std::search ).
    for (size_t i = 0; i <= haystack.size() - needle.size(); i++) {
      if (haystack.slice(i).startsWith(needle)) {
        return true;
      }
    }
#endif
  }
  return false;
}

LogExpectation::LogExpectation(LogSeverity severity, StringPtr substring)
    : severity(severity), substring(substring), seen(false) {}
LogExpectation::~LogExpectation() {
  if (!unwindDetector.isUnwinding()) {
    KJ_ASSERT(seen, "expected log message not seen", severity, substring);
  }
}

void LogExpectation::logMessage(
    LogSeverity severity, const char* file, int line, int contextDepth,
    String&& text) {
  if (!seen && severity == this->severity) {
    if (hasSubstring(text, substring)) {
      // Match. Ignore it.
      seen = true;
      return;
    }
  }

  // Pass up the chain.
  ExceptionCallback::logMessage(severity, file, line, contextDepth, kj::mv(text));
}

// =======================================================================================

namespace {

class FatalThrowExpectation: public ExceptionCallback {
public:
  FatalThrowExpectation(Maybe<Exception::Type> type,
                        Maybe<StringPtr> message)
      : type(type), message(message) {}

  virtual void onFatalException(Exception&& exception) {
    KJ_IF_MAYBE(expectedType, type) {
      if (exception.getType() != *expectedType) {
        KJ_LOG(ERROR, "threw exception of wrong type", exception, *expectedType);
        _exit(1);
      }
    }
    KJ_IF_MAYBE(expectedSubstring, message) {
      if (!hasSubstring(exception.getDescription(), *expectedSubstring)) {
        KJ_LOG(ERROR, "threw exception with wrong message", exception, *expectedSubstring);
        _exit(1);
      }
    }
    _exit(0);
  }

private:
  Maybe<Exception::Type> type;
  Maybe<StringPtr> message;
};

}  // namespace

bool expectFatalThrow(kj::Maybe<Exception::Type> type, kj::Maybe<StringPtr> message,
                      Function<void()> code) {
#if _WIN32
  // We don't support death tests on Windows due to lack of efficient fork.
  return true;
#else
  pid_t child;
  KJ_SYSCALL(child = fork());
  if (child == 0) {
    KJ_DEFER(_exit(1));
    FatalThrowExpectation expectation(type, message);
    KJ_IF_MAYBE(e, kj::runCatchingExceptions([&]() {
      code();
    })) {
      KJ_LOG(ERROR, "a non-fatal exception was thrown, but we expected fatal", *e);
    } else {
      KJ_LOG(ERROR, "no fatal exception was thrown");
    }
  }

  int status;
  KJ_SYSCALL(waitpid(child, &status, 0));

  if (WIFEXITED(status)) {
    return WEXITSTATUS(status) == 0;
  } else if (WIFSIGNALED(status)) {
    KJ_FAIL_EXPECT("subprocess crashed without throwing exception", WTERMSIG(status));
    return false;
  } else {
    KJ_FAIL_EXPECT("subprocess neither excited nor crashed?", status);
    return false;
  }
#endif
}

bool expectExit(Maybe<int> statusCode, FunctionParam<void()> code)  noexcept {
#if _WIN32
  // We don't support death tests on Windows due to lack of efficient fork.
  return true;
#else
  pid_t child;
  KJ_SYSCALL(child = fork());
  if (child == 0) {
    code();
    _exit(0);
  }

  int status;
  KJ_SYSCALL(waitpid(child, &status, 0));

  if (WIFEXITED(status)) {
    KJ_IF_MAYBE(s, statusCode) {
      KJ_EXPECT(WEXITSTATUS(status) == *s);
      return WEXITSTATUS(status) == *s;
    } else {
      KJ_EXPECT(WEXITSTATUS(status) != 0);
      return WEXITSTATUS(status) != 0;
    }
  } else {
    if (WIFSIGNALED(status)) {
      KJ_FAIL_EXPECT("subprocess didn't exit but triggered a signal", strsignal(WTERMSIG(status)));
    } else {
      KJ_FAIL_EXPECT("subprocess didn't exit and didn't trigger a signal", status);
    }
    return false;
  }
#endif
}


bool expectSignal(Maybe<int> signal, FunctionParam<void()> code) noexcept {
#if _WIN32
  // We don't support death tests on Windows due to lack of efficient fork.
  return true;
#else
  pid_t child;
  KJ_SYSCALL(child = fork());
  if (child == 0) {
    resetCrashHandlers();
    code();
    _exit(0);
  }

  int status;
  KJ_SYSCALL(waitpid(child, &status, 0));

  if (WIFSIGNALED(status)) {
    KJ_IF_MAYBE(s, signal) {
      KJ_EXPECT(WTERMSIG(status) == *s);
      return WTERMSIG(status) == *s;
    }
    return true;
  } else {
    if (WIFEXITED(status)) {
      KJ_FAIL_EXPECT("subprocess didn't trigger a signal but exited", WEXITSTATUS(status));
    } else {
      KJ_FAIL_EXPECT("subprocess didn't exit and didn't trigger a signal", status);
    }
    return false;
  }
#endif
}

}  // namespace _ (private)
}  // namespace kj
