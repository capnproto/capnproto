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

#include "test.h"
#include "main.h"
#include "io.h"
#include "miniposix.h"
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>

namespace kj {

namespace {

TestCase* testCasesHead = nullptr;
TestCase** testCasesTail = &testCasesHead;

}  // namespace

TestCase::TestCase(const char* file, uint line, const char* description)
    : file(file), line(line), description(description), next(nullptr), prev(testCasesTail),
      shouldRun(true) {
  *prev = this;
  testCasesTail = &next;
}

TestCase::~TestCase() {
  *prev = next;
  if (next == nullptr) {
    testCasesTail = prev;
  } else {
    next->prev = prev;
  }
}

// =======================================================================================

namespace {

void crashHandler(int signo, siginfo_t* info, void* context) {
  void* traceSpace[32];
  auto trace = getStackTrace(traceSpace);

  if (trace.size() >= 3) {
    // Remove getStackTrace(), crashHandler() and signal trampoline from trace.
    trace = trace.slice(3, trace.size());
  }

  auto message = kj::str("*** Received signal #", signo, ": ", strsignal(signo),
                         "\nstack: ", strArray(trace, " "),
                         stringifyStackTrace(trace), '\n');

  FdOutputStream(STDERR_FILENO).write(message.begin(), message.size());
  _exit(1);
}

void registerCrashHandler() {
  // Set up alternate signal stack so that stack overflows can be handled.
  stack_t stack;
  memset(&stack, 0, sizeof(stack));

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifndef MAP_GROWSDOWN
#define MAP_GROWSDOWN 0
#endif

  stack.ss_size = 65536;
  stack.ss_sp = mmap(nullptr, stack.ss_size, PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE | MAP_GROWSDOWN, -1, 0);
  KJ_SYSCALL(sigaltstack(&stack, nullptr));

  // Catch all relevant signals.
  struct sigaction action;
  memset(&action, 0, sizeof(action));

  action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER | SA_RESETHAND;
  action.sa_sigaction = &crashHandler;

  // Dump stack on common "crash" signals.
  KJ_SYSCALL(sigaction(SIGSEGV, &action, nullptr));
  KJ_SYSCALL(sigaction(SIGBUS, &action, nullptr));
  KJ_SYSCALL(sigaction(SIGFPE, &action, nullptr));
  KJ_SYSCALL(sigaction(SIGABRT, &action, nullptr));

  // Dump stack on unimplemented syscalls -- useful in seccomp sandboxes.
  KJ_SYSCALL(sigaction(SIGSYS, &action, nullptr));

  // Dump stack on keyboard interrupt -- useful for infinite loops.
  KJ_SYSCALL(sigaction(SIGINT, &action, nullptr));
}

}  // namespace

// =======================================================================================

namespace {

class TestExceptionCallback: public ExceptionCallback {
public:
  TestExceptionCallback(ProcessContext& context): context(context) {}

  bool failed() { return sawError; }

  void logMessage(LogSeverity severity, const char* file, int line, int contextDepth,
                  String&& text) override {
    void* traceSpace[32];
    auto trace = getStackTrace(traceSpace);

    if (text.size() == 0) {
      text = kj::heapString("expectation failed");
    }

    text = kj::str(kj::repeat('_', contextDepth), file, ':', line, ": ", kj::mv(text),
                   "\nstack: ", strArray(trace, " "), stringifyStackTrace(trace));

    if (severity == LogSeverity::ERROR || severity == LogSeverity::FATAL) {
      sawError = true;
      context.error(text);
    } else {
      context.warning(text);
    }
  }

private:
  ProcessContext& context;
  bool sawError = false;
};

}  // namespace

class TestRunner {
public:
  explicit TestRunner(ProcessContext& context)
      : context(context), useColor(isatty(STDOUT_FILENO)) {
    registerCrashHandler();
  }

  MainFunc getMain() {
    // TODO(now): Include summary of tests.
    return MainBuilder(context, "(no version)", "Runs some tests.")
        .addOptionWithArg({'t', "test-case"}, KJ_BIND_METHOD(*this, setTestCase), "<file>[:<line>]",
            "Run only the specified test case(s). You may use a '*' wildcard in <file>. You may "
            "also omit any prefix of <file>'s path; test from all matching files will run.")
        .callAfterParsing(KJ_BIND_METHOD(*this, run))
        .build();
  }

  MainBuilder::Validity setTestCase(StringPtr pattern) {
    ArrayPtr<const char> filePattern = pattern;
    kj::Maybe<uint> lineNumber = nullptr;

    KJ_IF_MAYBE(colonPos, pattern.findLast(':')) {
      char* end;
      StringPtr lineStr = pattern.slice(*colonPos + 1);
      lineNumber = strtoul(lineStr.cStr(), &end, 0);
      if (lineStr.size() > 0 && *end == '\0') {
        // We have an exact line number.
        filePattern = pattern.slice(0, *colonPos);
      } else {
        // Can't parse as a number. Maybe the colon is part of a Windows path name or something.
        // Let's just keep it as part of the file pattern.
        lineNumber = nullptr;
      }
    }

    // TODO(now): do the filter

    return true;
  }

  MainBuilder::Validity run() {
    if (testCasesHead == nullptr) {
      return "no tests were declared";
    }

    // Find the common path prefix of all filenames, so we can strip it off.
    ArrayPtr<const char> commonPrefix = StringPtr(testCasesHead->file);
    for (TestCase* testCase = testCasesHead; testCase != nullptr; testCase = testCase->next) {
      for (size_t i: kj::indices(commonPrefix)) {
        if (testCase->file[i] != commonPrefix[i]) {
          commonPrefix = commonPrefix.slice(0, i);
          break;
        }
      }
    }

    // Back off the prefix to the last '/'.
    while (commonPrefix.size() > 0 && commonPrefix.back() != '/') {
      commonPrefix = commonPrefix.slice(0, commonPrefix.size() - 1);
    }

    // Run the testts.
    uint passCount = 0;
    uint failCount = 0;
    for (TestCase* testCase = testCasesHead; testCase != nullptr; testCase = testCase->next) {
      if (testCase->shouldRun) {
        auto name = kj::str(testCase->file + commonPrefix.size(), ':', testCase->line,
                            ": ", testCase->description);

        write(BLUE, "[ TEST ]", name);

        bool currentFailed = true;
        KJ_IF_MAYBE(exception, runCatchingExceptions([&]() {
          TestExceptionCallback exceptionCallback(context);
          testCase->run();
          currentFailed = exceptionCallback.failed();
        })) {
          context.error(kj::str(*exception));
        }

        if (currentFailed) {
          write(RED, "[ FAIL ]", name);
          ++failCount;
        } else {
          write(GREEN, "[ PASS ]", name);
          ++passCount;
        }
      }
    }

    if (passCount > 0) write(GREEN, kj::str(passCount, " test(s) passed"), "");
    if (failCount > 0) write(RED, kj::str(failCount, " test(s) failed"), "");
    context.exit();
  }

private:
  ProcessContext& context;
  bool useColor;

  enum Color {
    RED,
    GREEN,
    BLUE
  };

  void write(StringPtr text) {
    FdOutputStream(STDOUT_FILENO).write(text.begin(), text.size());
  }

  void write(Color color, StringPtr prefix, StringPtr message) {
    StringPtr startColor, endColor;
    if (useColor) {
      switch (color) {
        case RED:   startColor = "\033[0;1;31m"; break;
        case GREEN: startColor = "\033[0;1;32m"; break;
        case BLUE:  startColor = "\033[0;1;34m"; break;
      }
      endColor = "\033[0m";
    }

    String text = kj::str(startColor, prefix, endColor, ' ', message, '\n');
    write(text);
  }
};

}  // namespace kj

KJ_MAIN(kj::TestRunner);
