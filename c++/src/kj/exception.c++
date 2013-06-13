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

#include "exception.h"
#include "string.h"
#include "debug.h"
#include <unistd.h>
#include <execinfo.h>
#include <stdlib.h>
#include <exception>

#if __GNUC__
#include <cxxabi.h>
#endif

#if defined(__linux__) && !defined(NDEBUG)
#include <stdio.h>
#include <pthread.h>
#endif

namespace kj {

namespace {

String getStackSymbols(ArrayPtr<void* const> trace) {
#if defined(__linux__) && !defined(NDEBUG)
  // We want to generate a human-readable stack trace.

  // TODO(someday):  It would be really great if we could avoid farming out to addr2line and do
  //   this all in-process, but that may involve onerous requirements like large library
  //   dependencies or using -rdynamic.

  // The environment manipulation is not thread-safe, so lock a mutex.  This could still be
  // problematic if another thread is manipulating the environment in unrelated code, but there's
  // not much we can do about that.  This is debug-only anyway and only an issue when LD_PRELOAD
  // is in use.
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&mutex);

  // Don't heapcheck / intercept syscalls for addr2line.
  const char* preload = getenv("LD_PRELOAD");
  String oldPreload;
  if (preload != nullptr) {
    oldPreload = heapString(preload);
    unsetenv("LD_PRELOAD");
  }

  // Get executable name from /proc/self/exe, then pass it and the stack trace to addr2line to
  // get file/line pairs.
  char exe[512];
  ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe));
  if (n < 0 || n >= static_cast<ssize_t>(sizeof(exe))) {
    return nullptr;
  }
  exe[n] = '\0';

  String lines[8];

  FILE* p = popen(str("addr2line -e ", exe, ' ', strArray(trace, " ")).cStr(), "r");
  if (p == nullptr) {
    return nullptr;
  }

  char line[512];
  size_t i = 0;
  while (i < KJ_ARRAY_SIZE(lines) && fgets(line, sizeof(line), p) != nullptr) {
    // Don't include exception-handling infrastructure in stack trace.
    if (i == 0 &&
        (strstr(line, "kj/common.c++") != nullptr ||
         strstr(line, "kj/exception.") != nullptr ||
         strstr(line, "kj/debug.") != nullptr)) {
      continue;
    }

    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
    lines[i++] = str("\n", line, ": called here");
  }

  // Skip remaining input.
  while (fgets(line, sizeof(line), p) != nullptr) {}

  pclose(p);

  if (oldPreload != nullptr) {
    setenv("LD_PRELOAD", oldPreload.cStr(), true);
  }

  pthread_mutex_unlock(&mutex);

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
    "temporary",
    "permanent"
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
             "\nstack: ", strArray(e.getStackTrace(), " "), getStackSymbols(e.getStackTrace()));
}

Exception::Exception(Nature nature, Durability durability, const char* file, int line,
                     String description) noexcept
    : file(file), line(line), nature(nature), durability(durability),
      description(mv(description)) {
  traceCount = backtrace(trace, 16);
}

Exception::Exception(const Exception& other) noexcept
    : file(other.file), line(other.line), nature(other.nature), durability(other.durability),
      description(str(other.description)), traceCount(other.traceCount) {
  memcpy(trace, other.trace, sizeof(trace[0]) * traceCount);

  KJ_IF_MAYBE(c, other.context) {
    context = heap(*c);
  }
}

Exception::~Exception() noexcept {}

Exception::Context::Context(const Context& other) noexcept
    : file(other.file), line(other.line), description(str(other.description)) {
  KJ_IF_MAYBE(n, other.next) {
    next = heap(*n);
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

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 8)
#define thread_local __thread
#endif

thread_local ExceptionCallback* threadLocalCallback = nullptr;

class RepeatChar {
  // A pseudo-sequence of characters that is actually just one character repeated.
  //
  // TODO(cleanup):  Put this somewhere reusable.  Maybe templatize it too.

public:
  inline RepeatChar(char c, uint size): c(c), size_(size) {}

  class Iterator {
  public:
    Iterator() = default;
    inline Iterator(char c, uint index): c(c), index(index) {}

    inline Iterator& operator++() { ++index; return *this; }
    inline Iterator operator++(int) { ++index; return Iterator(c, index - 1); }

    inline char operator*() const { return c; }

    inline bool operator==(const Iterator& other) const { return index == other.index; }
    inline bool operator!=(const Iterator& other) const { return index != other.index; }

  private:
    char c;
    uint index;
  };

  inline uint size() const { return size_; }
  inline Iterator begin() const { return Iterator(c, 0); }
  inline Iterator end() const { return Iterator(c, size_); }

private:
  char c;
  uint size_;
};
inline RepeatChar KJ_STRINGIFY(RepeatChar value) { return value; }

}  // namespace

ExceptionCallback::ExceptionCallback(): next(getExceptionCallback()) {
  char stackVar;
  ptrdiff_t offset = reinterpret_cast<char*>(this) - &stackVar;
  KJ_ASSERT(offset < 4096 && offset > -4096,
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
    throw ExceptionImpl(mv(exception));
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
    text = str(RepeatChar('_', contextDepth), file, ":", line, ": ", mv(text));

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
        "\nstack: ", strArray(e.getStackTrace(), " "), "\n"));
  }
};

ExceptionCallback& getExceptionCallback() {
  static ExceptionCallback::RootExceptionCallback defaultCallback;
  ExceptionCallback* scoped = threadLocalCallback;
  return scoped != nullptr ? *scoped : defaultCallback;
}

// =======================================================================================

namespace {

#if __GNUC__

// __cxa_eh_globals does not appear to be defined in a visible header.  This is what it looks like.
struct DummyEhGlobals {
  abi::__cxa_exception* caughtExceptions;
  uint uncaughtExceptions;
};

uint uncaughtExceptionCount() {
  // TODO(perf):  Use __cxa_get_globals_fast()?  Requires that __cxa_get_globals() has been called
  //   from somewhere.
  return reinterpret_cast<DummyEhGlobals*>(abi::__cxa_get_globals())->uncaughtExceptions;
}

#else
#error "This needs to be ported to your compiler / C++ ABI."
#endif

}  // namespace

UnwindDetector::UnwindDetector(): uncaughtCount(uncaughtExceptionCount()) {}

bool UnwindDetector::isUnwinding() const {
  return uncaughtExceptionCount() > uncaughtCount;
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

Maybe<Exception> runCatchingExceptions(Runnable& runnable) {
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
