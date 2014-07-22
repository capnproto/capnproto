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
#include "string.h"
#include "debug.h"
#include "threadlocal.h"
#include <unistd.h>
#include <stdlib.h>
#include <exception>

#if (__linux__ && !__ANDROID__) || __APPLE__
#define KJ_HAS_BACKTRACE 1
#include <execinfo.h>
#endif

#if (__linux__ || __APPLE__) && defined(KJ_DEBUG)
#include <stdio.h>
#include <pthread.h>
#endif

namespace kj {

namespace {

String getStackSymbols(ArrayPtr<void* const> trace) {
#if (__linux__ || __APPLE__) && defined(KJ_DEBUG)
  // We want to generate a human-readable stack trace.

  // TODO(someday):  It would be really great if we could avoid farming out to another process
  //   and do this all in-process, but that may involve onerous requirements like large library
  //   dependencies or using -rdynamic.

  // The environment manipulation is not thread-safe, so lock a mutex.  This could still be
  // problematic if another thread is manipulating the environment in unrelated code, but there's
  // not much we can do about that.  This is debug-only anyway and only an issue when LD_PRELOAD
  // is in use.
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&mutex);
  KJ_DEFER(pthread_mutex_unlock(&mutex));

  // Don't heapcheck / intercept syscalls.
  const char* preload = getenv("LD_PRELOAD");
  String oldPreload;
  if (preload != nullptr) {
    oldPreload = heapString(preload);
    unsetenv("LD_PRELOAD");
  }
  KJ_DEFER(if (oldPreload != nullptr) { setenv("LD_PRELOAD", oldPreload.cStr(), true); });

  String lines[8];
  FILE* p = nullptr;

#if __linux__
  // Get executable name from /proc/self/exe, then pass it and the stack trace to addr2line to
  // get file/line pairs.
  char exe[512];
  ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe));
  if (n < 0 || n >= static_cast<ssize_t>(sizeof(exe))) {
    return nullptr;
  }
  exe[n] = '\0';

  p = popen(str("addr2line -e ", exe, ' ', strArray(trace, " ")).cStr(), "r");
#elif __APPLE__
  // The Mac OS X equivalent of addr2line is atos.
  // (Internally, it uses the private CoreSymbolication.framework library.)
  p = popen(str("atos -d -p ", getpid(), ' ', strArray(trace, " ")).cStr(), "r");
#endif

  if (p == nullptr) {
    return nullptr;
  }

  char line[512];
  size_t i = 0;
  while (i < kj::size(lines) && fgets(line, sizeof(line), p) != nullptr) {
    // Don't include exception-handling infrastructure in stack trace.
    // addr2line output matches file names; atos output matches symbol names.
    if (i == 0 &&
        (strstr(line, "kj/common.c++") != nullptr ||
         strstr(line, "kj/exception.") != nullptr ||
         strstr(line, "kj/debug.") != nullptr ||
         strstr(line, "kj::Exception") != nullptr ||
         strstr(line, "kj::_::Debug") != nullptr)) {
      continue;
    }

    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
    lines[i++] = str("\n", line, ": called here");
  }

  // Skip remaining input.
  while (fgets(line, sizeof(line), p) != nullptr) {}

  pclose(p);

  return strArray(arrayPtr(lines, i), "");
#else
  return nullptr;
#endif
}

}  // namespace

ArrayPtr<const char> KJ_STRINGIFY(Exception::Nature nature) {
  static const char* NATURE_STRINGS[] = {
    "requirement not met",
    "bug in code",
    "error from OS",
    "network failure",
    "error"
  };

  const char* s = NATURE_STRINGS[static_cast<uint>(nature)];
  return arrayPtr(s, strlen(s));
}

ArrayPtr<const char> KJ_STRINGIFY(Exception::Durability durability) {
  static const char* DURABILITY_STRINGS[] = {
    "permanent",
    "temporary",
    "overloaded"
  };

  const char* s = DURABILITY_STRINGS[static_cast<uint>(durability)];
  return arrayPtr(s, strlen(s));
}

String KJ_STRINGIFY(const Exception& e) {
  uint contextDepth = 0;

  Maybe<const Exception::Context&> contextPtr = e.getContext();
  for (;;) {
    KJ_IF_MAYBE(c, contextPtr) {
      ++contextDepth;
      contextPtr = c->next;
    } else {
      break;
    }
  }

  Array<String> contextText = heapArray<String>(contextDepth);

  contextDepth = 0;
  contextPtr = e.getContext();
  for (;;) {
    KJ_IF_MAYBE(c, contextPtr) {
      contextText[contextDepth++] =
          str(c->file, ":", c->line, ": context: ", c->description, "\n");
      contextPtr = c->next;
    } else {
      break;
    }
  }

  return str(strArray(contextText, ""),
             e.getFile(), ":", e.getLine(), ": ", e.getNature(),
             e.getDurability() == Exception::Durability::TEMPORARY ? " (temporary)" : "",
             e.getDescription() == nullptr ? "" : ": ", e.getDescription(),
             e.getStackTrace().size() > 0 ? "\nstack: " : "", strArray(e.getStackTrace(), " "),
             getStackSymbols(e.getStackTrace()));
}

Exception::Exception(Nature nature, Durability durability, const char* file, int line,
                     String description) noexcept
    : file(file), line(line), nature(nature), durability(durability),
      description(mv(description)) {
#ifndef KJ_HAS_BACKTRACE
  traceCount = 0;
#else
  traceCount = backtrace(trace, 16);
#endif
}

Exception::Exception(Nature nature, Durability durability, String file, int line,
                     String description) noexcept
    : ownFile(kj::mv(file)), file(ownFile.cStr()), line(line), nature(nature),
      durability(durability), description(mv(description)) {
#ifndef KJ_HAS_BACKTRACE
  traceCount = 0;
#else
  traceCount = backtrace(trace, 16);
#endif
}

Exception::Exception(const Exception& other) noexcept
    : file(other.file), line(other.line), nature(other.nature), durability(other.durability),
      description(heapString(other.description)), traceCount(other.traceCount) {
  if (file == other.ownFile.cStr()) {
    ownFile = heapString(other.ownFile);
    file = ownFile.cStr();
  }

  memcpy(trace, other.trace, sizeof(trace[0]) * traceCount);

  KJ_IF_MAYBE(c, other.context) {
    context = heap(**c);
  }
}

Exception::~Exception() noexcept {}

Exception::Context::Context(const Context& other) noexcept
    : file(other.file), line(other.line), description(str(other.description)) {
  KJ_IF_MAYBE(n, other.next) {
    next = heap(**n);
  }
}

void Exception::wrapContext(const char* file, int line, String&& description) {
  context = heap<Context>(file, line, mv(description), mv(context));
}

class ExceptionImpl: public Exception, public std::exception {
public:
  inline ExceptionImpl(Exception&& other): Exception(mv(other)) {}
  ExceptionImpl(const ExceptionImpl& other): Exception(other) {
    // No need to copy whatBuffer since it's just to hold the return value of what().
  }

  const char* what() const noexcept override;

private:
  mutable String whatBuffer;
};

const char* ExceptionImpl::what() const noexcept {
  whatBuffer = str(*this);
  return whatBuffer.begin();
}

// =======================================================================================

namespace {

KJ_THREADLOCAL_PTR(ExceptionCallback) threadLocalCallback = nullptr;

}  // namespace

ExceptionCallback::ExceptionCallback(): next(getExceptionCallback()) {
  char stackVar;
  ptrdiff_t offset = reinterpret_cast<char*>(this) - &stackVar;
  KJ_ASSERT(offset < 65536 && offset > -65536,
            "ExceptionCallback must be allocated on the stack.");

  threadLocalCallback = this;
}

ExceptionCallback::ExceptionCallback(ExceptionCallback& next): next(next) {}

ExceptionCallback::~ExceptionCallback() noexcept(false) {
  if (&next != this) {
    threadLocalCallback = &next;
  }
}

void ExceptionCallback::onRecoverableException(Exception&& exception) {
  next.onRecoverableException(mv(exception));
}

void ExceptionCallback::onFatalException(Exception&& exception) {
  next.onFatalException(mv(exception));
}

void ExceptionCallback::logMessage(const char* file, int line, int contextDepth, String&& text) {
  next.logMessage(file, line, contextDepth, mv(text));
}

class ExceptionCallback::RootExceptionCallback: public ExceptionCallback {
public:
  RootExceptionCallback(): ExceptionCallback(*this) {}

  void onRecoverableException(Exception&& exception) override {
#if KJ_NO_EXCEPTIONS
    logException(mv(exception));
#else
    if (std::uncaught_exception()) {
      // Bad time to throw an exception.  Just log instead.
      logException(mv(exception));
    } else {
      throw ExceptionImpl(mv(exception));
    }
#endif
  }

  void onFatalException(Exception&& exception) override {
#if KJ_NO_EXCEPTIONS
    logException(mv(exception));
#else
    throw ExceptionImpl(mv(exception));
#endif
  }

  void logMessage(const char* file, int line, int contextDepth, String&& text) override {
    text = str(kj::repeat('_', contextDepth), file, ":", line, ": ", mv(text));

    StringPtr textPtr = text;

    while (text != nullptr) {
      ssize_t n = write(STDERR_FILENO, textPtr.begin(), textPtr.size());
      if (n <= 0) {
        // stderr is broken.  Give up.
        return;
      }
      textPtr = textPtr.slice(n);
    }
  }

private:
  void logException(Exception&& e) {
    // We intentionally go back to the top exception callback on the stack because we don't want to
    // bypass whatever log processing is in effect.
    //
    // We intentionally don't log the context since it should get re-added by the exception callback
    // anyway.
    getExceptionCallback().logMessage(e.getFile(), e.getLine(), 0, str(
        e.getNature(), e.getDurability() == Exception::Durability::TEMPORARY ? " (temporary)" : "",
        e.getDescription() == nullptr ? "" : ": ", e.getDescription(),
        e.getStackTrace().size() > 0 ? "\nstack: " : "", strArray(e.getStackTrace(), " "),
        getStackSymbols(e.getStackTrace()), "\n"));
  }
};

ExceptionCallback& getExceptionCallback() {
  static ExceptionCallback::RootExceptionCallback defaultCallback;
  ExceptionCallback* scoped = threadLocalCallback;
  return scoped != nullptr ? *scoped : defaultCallback;
}

void throwFatalException(kj::Exception&& exception) {
  getExceptionCallback().onFatalException(kj::mv(exception));
  abort();
}

void throwRecoverableException(kj::Exception&& exception) {
  getExceptionCallback().onRecoverableException(kj::mv(exception));
}

// =======================================================================================

namespace _ {  // private

#if __GNUC__

// Horrible -- but working -- hack:  We can dig into __cxa_get_globals() in order to extract the
// count of uncaught exceptions.  This function is part of the C++ ABI implementation used on Linux,
// OSX, and probably other platforms that use GCC.  Unfortunately, __cxa_get_globals() is only
// actually defined in cxxabi.h on some platforms (e.g. Linux, but not OSX), and even where it is
// defined, it returns an incomplete type.  Here we use the same hack used by Evgeny Panasyuk:
//   https://github.com/panaseleus/stack_unwinding/blob/master/boost/exception/uncaught_exception_count.hpp
//
// Notice that a similar hack is possible on MSVC -- if its C++11 support ever gets to the point of
// supporting KJ in the first place.
//
// It appears likely that a future version of the C++ standard may include an
// uncaught_exception_count() function in the standard library, or an equivalent language feature.
// Some discussion:
//   https://groups.google.com/a/isocpp.org/d/msg/std-proposals/HglEslyZFYs/kKdu5jJw5AgJ

struct FakeEhGlobals {
  // Fake

  void* caughtExceptions;
  uint uncaughtExceptions;
};

// Because of the 'extern "C"', the symbol name is not mangled and thus the namespace is effectively
// ignored for linking.  Thus it doesn't matter that we are declaring __cxa_get_globals() in a
// different namespace from the ABI's definition.
extern "C" {
FakeEhGlobals* __cxa_get_globals();
}

uint uncaughtExceptionCount() {
  // TODO(perf):  Use __cxa_get_globals_fast()?  Requires that __cxa_get_globals() has been called
  //   from somewhere.
  return __cxa_get_globals()->uncaughtExceptions;
}

#else
#error "This needs to be ported to your compiler / C++ ABI."
#endif

}  // namespace _ (private)

UnwindDetector::UnwindDetector(): uncaughtCount(_::uncaughtExceptionCount()) {}

bool UnwindDetector::isUnwinding() const {
  return _::uncaughtExceptionCount() > uncaughtCount;
}

void UnwindDetector::catchExceptionsAsSecondaryFaults(_::Runnable& runnable) const {
  // TODO(someday):  Attach the secondary exception to whatever primary exception is causing
  //   the unwind.  For now we just drop it on the floor as this is probably fine most of the
  //   time.
  runCatchingExceptions(runnable);
}

namespace _ {  // private

class RecoverableExceptionCatcher: public ExceptionCallback {
  // Catches a recoverable exception without using try/catch.  Used when compiled with
  // -fno-exceptions.

public:
  virtual ~RecoverableExceptionCatcher() noexcept(false) {}

  void onRecoverableException(Exception&& exception) override {
    if (caught == nullptr) {
      caught = mv(exception);
    } else {
      // TODO(someday):  Consider it a secondary fault?
    }
  }

  Maybe<Exception> caught;
};

Maybe<Exception> runCatchingExceptions(Runnable& runnable) noexcept {
#if KJ_NO_EXCEPTIONS
  RecoverableExceptionCatcher catcher;
  runnable.run();
  return mv(catcher.caught);
#else
  try {
    runnable.run();
    return nullptr;
  } catch (Exception& e) {
    return kj::mv(e);
  } catch (std::exception& e) {
    return Exception(Exception::Nature::OTHER, Exception::Durability::PERMANENT,
                     "(unknown)", -1, str("std::exception: ", e.what()));
  } catch (...) {
    return Exception(Exception::Nature::OTHER, Exception::Durability::PERMANENT,
                     "(unknown)", -1, str("Unknown non-KJ exception."));
  }
#endif
}

}  // namespace _ (private)

}  // namespace kj
