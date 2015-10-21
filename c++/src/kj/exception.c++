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
#include "miniposix.h"
#include <stdlib.h>
#include <exception>
#include <new>
#include <signal.h>
#include <sys/mman.h>
#include "io.h"

#if (__linux__ && !__ANDROID__) || __APPLE__
#define KJ_HAS_BACKTRACE 1
#include <execinfo.h>
#endif

#if (__linux__ || __APPLE__) && defined(KJ_DEBUG)
#include <stdio.h>
#include <pthread.h>
#endif

namespace kj {

StringPtr KJ_STRINGIFY(LogSeverity severity) {
  static const char* SEVERITY_STRINGS[] = {
    "info",
    "warning",
    "error",
    "fatal",
    "debug"
  };

  return SEVERITY_STRINGS[static_cast<uint>(severity)];
}

ArrayPtr<void* const> getStackTrace(ArrayPtr<void*> space, uint ignoreCount) {
#ifndef KJ_HAS_BACKTRACE
  return nullptr;
#else
  size_t size = backtrace(space.begin(), space.size());
  return space.slice(kj::min(ignoreCount + 1, size), size);
#endif
}

String stringifyStackTrace(ArrayPtr<void* const> trace) {
  if (trace.size() == 0) return nullptr;

#if (__linux__ || __APPLE__) && !__ANDROID__ && defined(KJ_DEBUG)
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

  String lines[32];
  FILE* p = nullptr;
  auto strTrace = strArray(trace, " ");

#if __linux__
  if (access("/proc/self/exe", R_OK) < 0) {
    // Apparently /proc is not available?
    return nullptr;
  }

  // Obtain symbolic stack trace using addr2line.
  // TODO(cleanup): Use fork() and exec() or maybe our own Subprocess API (once it exists), to
  //   avoid depending on a shell.
  p = popen(str("addr2line -e /proc/", getpid(), "/exe ", strTrace).cStr(), "r");
#elif __APPLE__
  // The Mac OS X equivalent of addr2line is atos.
  // (Internally, it uses the private CoreSymbolication.framework library.)
  p = popen(str("xcrun atos -p ", getpid(), ' ', strTrace).cStr(), "r");
#endif

  if (p == nullptr) {
    return nullptr;
  }

  char line[512];
  size_t i = 0;
  while (i < kj::size(lines) && fgets(line, sizeof(line), p) != nullptr) {
    // Don't include exception-handling infrastructure or promise infrastructure in stack trace.
    // addr2line output matches file names; atos output matches symbol names.
    if (strstr(line, "kj/common.c++") != nullptr ||
        strstr(line, "kj/exception.") != nullptr ||
        strstr(line, "kj/debug.") != nullptr ||
        strstr(line, "kj/async.") != nullptr ||
        strstr(line, "kj/async-prelude.h") != nullptr ||
        strstr(line, "kj/async-inl.h") != nullptr ||
        strstr(line, "kj::Exception") != nullptr ||
        strstr(line, "kj::_::Debug") != nullptr) {
      continue;
    }

    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
    lines[i++] = str("\n    ", trimSourceFilename(line), ": returning here");
  }

  // Skip remaining input.
  while (fgets(line, sizeof(line), p) != nullptr) {}

  pclose(p);

  return strArray(arrayPtr(lines, i), "");
#else
  return nullptr;
#endif
}

#if KJ_HAS_BACKTRACE
namespace {

void crashHandler(int signo, siginfo_t* info, void* context) {
  void* traceSpace[32];

  // ignoreCount = 2 to ignore crashHandler() and signal trampoline.
  auto trace = getStackTrace(traceSpace, 2);

  auto message = kj::str("*** Received signal #", signo, ": ", strsignal(signo),
                         "\nstack: ", strArray(trace, " "),
                         stringifyStackTrace(trace), '\n');

  FdOutputStream(STDERR_FILENO).write(message.begin(), message.size());
  _exit(1);
}

}  // namespace

void printStackTraceOnCrash() {
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
  // Note: ss_sp is char* on FreeBSD, void* on Linux and OSX.
  stack.ss_sp = reinterpret_cast<char*>(mmap(
      nullptr, stack.ss_size, PROT_READ | PROT_WRITE,
      MAP_ANONYMOUS | MAP_PRIVATE | MAP_GROWSDOWN, -1, 0));
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
  KJ_SYSCALL(sigaction(SIGILL, &action, nullptr));

  // Dump stack on unimplemented syscalls -- useful in seccomp sandboxes.
  KJ_SYSCALL(sigaction(SIGSYS, &action, nullptr));

#ifdef KJ_DEBUG
  // Dump stack on keyboard interrupt -- useful for infinite loops. Only in debug mode, though,
  // because stack traces on ctrl+c can be obnoxious for, say, command-line tools.
  KJ_SYSCALL(sigaction(SIGINT, &action, nullptr));
#endif
}
#else
void printStackTraceOnCrash() {
}
#endif

kj::StringPtr trimSourceFilename(kj::StringPtr filename) {
  // Removes noisy prefixes from source code file name.

  static constexpr const char* PREFIXES[] = {
    "../",
    "/ekam-provider/canonical/",
    "/ekam-provider/c++header/",
    "src/",
    "tmp/"
  };

retry:
  for (const char* prefix: PREFIXES) {
    if (filename.startsWith(prefix)) {
      filename = filename.slice(strlen(prefix));
      goto retry;
    }
  }

  return filename;
}

StringPtr KJ_STRINGIFY(Exception::Type type) {
  static const char* TYPE_STRINGS[] = {
    "failed",
    "overloaded",
    "disconnected",
    "unimplemented"
  };

  return TYPE_STRINGS[static_cast<uint>(type)];
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
             e.getFile(), ":", e.getLine(), ": ", e.getType(),
             e.getDescription() == nullptr ? "" : ": ", e.getDescription(),
             e.getStackTrace().size() > 0 ? "\nstack: " : "", strArray(e.getStackTrace(), " "),
             stringifyStackTrace(e.getStackTrace()));
}

Exception::Exception(Type type, const char* file, int line, String description) noexcept
    : file(trimSourceFilename(file).cStr()), line(line), type(type), description(mv(description)),
      traceCount(0) {}

Exception::Exception(Type type, String file, int line, String description) noexcept
    : ownFile(kj::mv(file)), file(trimSourceFilename(ownFile).cStr()), line(line), type(type),
      description(mv(description)), traceCount(0) {}

Exception::Exception(const Exception& other) noexcept
    : file(other.file), line(other.line), type(other.type),
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

void Exception::extendTrace(uint ignoreCount) {
  KJ_STACK_ARRAY(void*, newTraceSpace, kj::size(trace) + ignoreCount + 1,
      sizeof(trace)/sizeof(trace[0]) + 8, 128);

  auto newTrace = kj::getStackTrace(newTraceSpace, ignoreCount + 1);
  if (newTrace.size() > ignoreCount + 2) {
    // Remove suffix that won't fit into our static-sized trace.
    newTrace = newTrace.slice(0, kj::min(kj::size(trace) - traceCount, newTrace.size()));

    // Copy the rest into our trace.
    memcpy(trace + traceCount, newTrace.begin(), newTrace.asBytes().size());
    traceCount += newTrace.size();
  }
}

void Exception::truncateCommonTrace() {
  if (traceCount > 0) {
    // Create a "reference" stack trace that is a little bit deeper than the one in the exception.
    void* refTraceSpace[sizeof(this->trace) / sizeof(this->trace[0]) + 4];
    auto refTrace = kj::getStackTrace(refTraceSpace, 0);

    // We expect that the deepest frame in the exception's stack trace should be somewhere in our
    // own trace, since our own trace has a deeper limit. Search for it.
    for (uint i = refTrace.size(); i > 0; i--) {
      if (refTrace[i-1] == trace[traceCount-1]) {
        // See how many frames match.
        for (uint j = 0; j < i; j++) {
          if (j >= traceCount) {
            // We matched the whole trace, apparently?
            traceCount = 0;
            return;
          } else if (refTrace[i-j-1] != trace[traceCount-j-1]) {
            // Found mismatching entry.

            // If we matched more than half of the reference trace, guess that this is in fact
            // the prefix we're looking for.
            if (j > refTrace.size() / 2) {
              // Delete the matching suffix. Also delete one non-matched entry on the assumption
              // that both traces contain that stack frame but are simply at different points in
              // the function.
              traceCount -= j + 1;
              return;
            }
          }
        }
      }
    }

    // No match. Ignore.
  }
}

void Exception::addTrace(void* ptr) {
  if (traceCount < kj::size(trace)) {
    trace[traceCount++] = ptr;
  }
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

void ExceptionCallback::logMessage(
    LogSeverity severity, const char* file, int line, int contextDepth, String&& text) {
  next.logMessage(severity, file, line, contextDepth, mv(text));
}

class ExceptionCallback::RootExceptionCallback: public ExceptionCallback {
public:
  RootExceptionCallback(): ExceptionCallback(*this) {}

  void onRecoverableException(Exception&& exception) override {
#if KJ_NO_EXCEPTIONS
    logException(LogSeverity::ERROR, mv(exception));
#else
    if (std::uncaught_exception()) {
      // Bad time to throw an exception.  Just log instead.
      //
      // TODO(someday): We should really compare uncaughtExceptionCount() against the count at
      //   the innermost runCatchingExceptions() frame in this thread to tell if exceptions are
      //   being caught correctly.
      logException(LogSeverity::ERROR, mv(exception));
    } else {
      throw ExceptionImpl(mv(exception));
    }
#endif
  }

  void onFatalException(Exception&& exception) override {
#if KJ_NO_EXCEPTIONS
    logException(LogSeverity::FATAL, mv(exception));
#else
    throw ExceptionImpl(mv(exception));
#endif
  }

  void logMessage(LogSeverity severity, const char* file, int line, int contextDepth,
                  String&& text) override {
    text = str(kj::repeat('_', contextDepth), file, ":", line, ": ", severity, ": ",
               mv(text), '\n');

    StringPtr textPtr = text;

    while (text != nullptr) {
      miniposix::ssize_t n = miniposix::write(STDERR_FILENO, textPtr.begin(), textPtr.size());
      if (n <= 0) {
        // stderr is broken.  Give up.
        return;
      }
      textPtr = textPtr.slice(n);
    }
  }

private:
  void logException(LogSeverity severity, Exception&& e) {
    // We intentionally go back to the top exception callback on the stack because we don't want to
    // bypass whatever log processing is in effect.
    //
    // We intentionally don't log the context since it should get re-added by the exception callback
    // anyway.
    getExceptionCallback().logMessage(severity, e.getFile(), e.getLine(), 0, str(
        e.getType(), e.getDescription() == nullptr ? "" : ": ", e.getDescription(),
        e.getStackTrace().size() > 0 ? "\nstack: " : "", strArray(e.getStackTrace(), " "),
        stringifyStackTrace(e.getStackTrace()), "\n"));
  }
};

ExceptionCallback& getExceptionCallback() {
  static ExceptionCallback::RootExceptionCallback defaultCallback;
  ExceptionCallback* scoped = threadLocalCallback;
  return scoped != nullptr ? *scoped : defaultCallback;
}

void throwFatalException(kj::Exception&& exception, uint ignoreCount) {
  exception.extendTrace(ignoreCount + 1);
  getExceptionCallback().onFatalException(kj::mv(exception));
  abort();
}

void throwRecoverableException(kj::Exception&& exception, uint ignoreCount) {
  exception.extendTrace(ignoreCount + 1);
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

#elif _MSC_VER

#if 0
// TODO(msvc): The below was copied from:
//     https://github.com/panaseleus/stack_unwinding/blob/master/boost/exception/uncaught_exception_count.hpp
//   Alas, it doesn not appera to work on MSVC2015. The linker claims _getptd() doesn't exist.

extern "C" char *__cdecl _getptd();

uint uncaughtExceptionCount() {
  return *reinterpret_cast<uint*>(_getptd() + (sizeof(void*) == 8 ? 0x100 : 0x90));
}
#endif

uint uncaughtExceptionCount() {
  // Since the above doesn't work, fall back to uncaught_exception(). This will produce incorrect
  // results in very obscure cases that Cap'n Proto doesn't really rely on anyway.
  return std::uncaught_exception();
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
  KJ_IF_MAYBE(e, catcher.caught) {
    e->truncateCommonTrace();
  }
  return mv(catcher.caught);
#else
  try {
    runnable.run();
    return nullptr;
  } catch (Exception& e) {
    e.truncateCommonTrace();
    return kj::mv(e);
  } catch (std::bad_alloc& e) {
    return Exception(Exception::Type::OVERLOADED,
                     "(unknown)", -1, str("std::bad_alloc: ", e.what()));
  } catch (std::exception& e) {
    return Exception(Exception::Type::FAILED,
                     "(unknown)", -1, str("std::exception: ", e.what()));
  } catch (...) {
    return Exception(Exception::Type::FAILED,
                     "(unknown)", -1, str("Unknown non-KJ exception."));
  }
#endif
}

}  // namespace _ (private)

}  // namespace kj
