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
#include "function.h"
#include <stdlib.h>
#include <exception>
#include <new>
#include <signal.h>
#include <stdint.h>
#ifndef _WIN32
#include <sys/mman.h>
#endif
#include "io.h"

#if (__linux__ && __GLIBC__ && !__UCLIBC__) || __APPLE__
#define KJ_HAS_BACKTRACE 1
#include <execinfo.h>
#endif

#if _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "windows-sanity.h"
#include <dbghelp.h>
#endif

#if (__linux__ || __APPLE__)
#include <stdio.h>
#include <pthread.h>
#endif

#if KJ_HAS_LIBDL
#include "dlfcn.h"
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

#if _WIN32 && _M_X64
// Currently the Win32 stack-trace code only supports x86_64. We could easily extend it to support
// i386 as well but it requires some code changes around how we read the context to start the
// trace.

namespace {

struct Dbghelp {
  // Load dbghelp.dll dynamically since we don't really need it, it's just for debugging.

  HINSTANCE lib;

  BOOL (WINAPI *symInitialize)(HANDLE hProcess,PCSTR UserSearchPath,BOOL fInvadeProcess);
  BOOL (WINAPI *stackWalk64)(
      DWORD MachineType,HANDLE hProcess,HANDLE hThread,
      LPSTACKFRAME64 StackFrame,PVOID ContextRecord,
      PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine,
      PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
      PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine,
      PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress);
  PVOID (WINAPI *symFunctionTableAccess64)(HANDLE hProcess,DWORD64 AddrBase);
  DWORD64 (WINAPI *symGetModuleBase64)(HANDLE hProcess,DWORD64 qwAddr);
  BOOL (WINAPI *symGetLineFromAddr64)(
      HANDLE hProcess,DWORD64 qwAddr,PDWORD pdwDisplacement,PIMAGEHLP_LINE64 Line64);

  Dbghelp()
      : lib(LoadLibraryA("dbghelp.dll")),
        symInitialize(lib == nullptr ? nullptr :
            reinterpret_cast<decltype(symInitialize)>(
                GetProcAddress(lib, "SymInitialize"))),
        stackWalk64(symInitialize == nullptr ? nullptr :
            reinterpret_cast<decltype(stackWalk64)>(
                GetProcAddress(lib, "StackWalk64"))),
        symFunctionTableAccess64(symInitialize == nullptr ? nullptr :
            reinterpret_cast<decltype(symFunctionTableAccess64)>(
                GetProcAddress(lib, "SymFunctionTableAccess64"))),
        symGetModuleBase64(symInitialize == nullptr ? nullptr :
            reinterpret_cast<decltype(symGetModuleBase64)>(
                GetProcAddress(lib, "SymGetModuleBase64"))),
        symGetLineFromAddr64(symInitialize == nullptr ? nullptr :
            reinterpret_cast<decltype(symGetLineFromAddr64)>(
                GetProcAddress(lib, "SymGetLineFromAddr64"))) {
    if (symInitialize != nullptr) {
      symInitialize(GetCurrentProcess(), NULL, TRUE);
    }
  }
};

const Dbghelp& getDbghelp() {
  static Dbghelp dbghelp;
  return dbghelp;
}

ArrayPtr<void* const> getStackTrace(ArrayPtr<void*> space, uint ignoreCount,
                                    HANDLE thread, CONTEXT& context) {
  const Dbghelp& dbghelp = getDbghelp();
  if (dbghelp.stackWalk64 == nullptr ||
      dbghelp.symFunctionTableAccess64 == nullptr ||
      dbghelp.symGetModuleBase64 == nullptr) {
    return nullptr;
  }

  STACKFRAME64 frame;
  memset(&frame, 0, sizeof(frame));

  frame.AddrPC.Offset = context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;

  HANDLE process = GetCurrentProcess();

  uint count = 0;
  for (; count < space.size(); count++) {
    if (!dbghelp.stackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread,
          &frame, &context, NULL, dbghelp.symFunctionTableAccess64,
          dbghelp.symGetModuleBase64, NULL)){
      break;
    }

    space[count] = reinterpret_cast<void*>(frame.AddrPC.Offset);
  }

  return space.slice(kj::min(ignoreCount, count), count);
}

}  // namespace
#endif

ArrayPtr<void* const> getStackTrace(ArrayPtr<void*> space, uint ignoreCount) {
  if (getExceptionCallback().stackTraceMode() == ExceptionCallback::StackTraceMode::NONE) {
    return nullptr;
  }

#if _WIN32 && _M_X64
  CONTEXT context;
  RtlCaptureContext(&context);
  return getStackTrace(space, ignoreCount, GetCurrentThread(), context);
#elif KJ_HAS_BACKTRACE
  size_t size = backtrace(space.begin(), space.size());
  for (auto& addr: space.slice(0, size)) {
    // The addresses produced by backtrace() are return addresses, which means they point to the
    // instruction immediately after the call. Invoking addr2line on these can be confusing because
    // it often points to the next line. If the next instruction is inlined from another function,
    // the trace can be extra-confusing, since now it claims to be in a function that was not
    // actually on the call stack. If we subtract 1 from each address, though, we get a much more
    // reasonable trace. This may cause the addresses to be invalid instruction pointers if the
    // instructions were multi-byte, but it appears addr2line is able to cope with this.
    addr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) - 1);
  }
  return space.slice(kj::min(ignoreCount + 1, size), size);
#else
  return nullptr;
#endif
}

String stringifyStackTrace(ArrayPtr<void* const> trace) {
  if (trace.size() == 0) return nullptr;
  if (getExceptionCallback().stackTraceMode() != ExceptionCallback::StackTraceMode::FULL) {
    return nullptr;
  }

#if _WIN32 && _M_X64 && _MSC_VER

  // Try to get file/line using SymGetLineFromAddr64(). We don't bother if we aren't on MSVC since
  // this requires MSVC debug info.
  //
  // TODO(someday): We could perhaps shell out to addr2line on MinGW.

  const Dbghelp& dbghelp = getDbghelp();
  if (dbghelp.symGetLineFromAddr64 == nullptr) return nullptr;

  HANDLE process = GetCurrentProcess();

  KJ_STACK_ARRAY(String, lines, trace.size(), 32, 32);

  for (auto i: kj::indices(trace)) {
    IMAGEHLP_LINE64 lineInfo;
    memset(&lineInfo, 0, sizeof(lineInfo));
    lineInfo.SizeOfStruct = sizeof(lineInfo);
    if (dbghelp.symGetLineFromAddr64(process, reinterpret_cast<DWORD64>(trace[i]), NULL, &lineInfo)) {
      lines[i] = kj::str('\n', lineInfo.FileName, ':', lineInfo.LineNumber);
    }
  }

  return strArray(lines, "");

#elif (__linux__ || __APPLE__) && !__ANDROID__
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

String stringifyStackTraceAddresses(ArrayPtr<void* const> trace) {
#if KJ_HAS_LIBDL
  return strArray(KJ_MAP(addr, trace) {
    Dl_info info;
    // Shared libraries are mapped near the end of the address space while the executable is mapped
    // near the beginning. We want to print addresses in the executable as raw addresses, not
    // offsets, since that's what addr2line expects for executables. For shared libraries it
    // expects offsets. In any case, most frames are likely to be in the main executable so it
    // makes the output cleaner if we don't repeatedly write its name.
    if (reinterpret_cast<uintptr_t>(addr) >= 0x400000000000ull && dladdr(addr, &info)) {
      uintptr_t offset = reinterpret_cast<uintptr_t>(addr) -
                         reinterpret_cast<uintptr_t>(info.dli_fbase);
      return kj::str(info.dli_fname, '@', reinterpret_cast<void*>(offset));
    } else {
      return kj::str(addr);
    }
  }, " ");
#else
  // TODO(someday): Support other platforms.
  return kj::strArray(trace, " ");
#endif
}

String getStackTrace() {
  void* space[32];
  auto trace = getStackTrace(space, 2);
  return kj::str(stringifyStackTraceAddresses(trace), stringifyStackTrace(trace));
}

#if _WIN32 && _M_X64
namespace {

DWORD mainThreadId = 0;

BOOL WINAPI breakHandler(DWORD type) {
  switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT: {
      HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, mainThreadId);
      if (thread != NULL) {
        if (SuspendThread(thread) != (DWORD)-1) {
          CONTEXT context;
          memset(&context, 0, sizeof(context));
          context.ContextFlags = CONTEXT_FULL;
          if (GetThreadContext(thread, &context)) {
            void* traceSpace[32];
            auto trace = getStackTrace(traceSpace, 2, thread, context);
            ResumeThread(thread);
            auto message = kj::str("*** Received CTRL+C. stack: ",
                                   stringifyStackTraceAddresses(trace),
                                   stringifyStackTrace(trace), '\n');
            FdOutputStream(STDERR_FILENO).write(message.begin(), message.size());
          } else {
            ResumeThread(thread);
          }
        }
        CloseHandle(thread);
      }
      break;
    }
    default:
      break;
  }

  return FALSE;  // still crash
}

}  // namespace

void printStackTraceOnCrash() {
  mainThreadId = GetCurrentThreadId();
  KJ_WIN32(SetConsoleCtrlHandler(breakHandler, TRUE));
}

#elif KJ_HAS_BACKTRACE
namespace {

void crashHandler(int signo, siginfo_t* info, void* context) {
  void* traceSpace[32];

  // ignoreCount = 2 to ignore crashHandler() and signal trampoline.
  auto trace = getStackTrace(traceSpace, 2);

  auto message = kj::str("*** Received signal #", signo, ": ", strsignal(signo),
                         "\nstack: ", stringifyStackTraceAddresses(trace),
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
  //
  // The goal here is to produce the "canonical" filename given the filename returned by e.g.
  // addr2line. addr2line gives us the full path of the file as passed on the compiler
  // command-line, which in turn is affected by build system and by whether and where we're
  // performing an out-of-tree build.
  //
  // To deal with all this, we look for directory names in the path which we recognize to be
  // locations that represent roots of the source tree. We strip said root and everything before
  // it.
  //
  // On Windows, we often get filenames containing backslashes. Since we aren't allowed to allocate
  // a new string here, we can't do much about this, so our returned "canonical" name will
  // unfortunately end up with backslashes.

  static constexpr const char* ROOTS[] = {
    "ekam-provider/canonical/",  // Ekam source file.
    "ekam-provider/c++header/",  // Ekam include file.
    "src/",                      // Non-Ekam source root.
    "tmp/",                      // Non-Ekam generated code.
#if _WIN32
    "src\\",                     // Win32 source root.
    "tmp\\",                     // Win32 generated code.
#endif
  };

retry:
  for (size_t i: kj::indices(filename)) {
    if (i == 0 || filename[i-1] == '/'
#if _WIN32
               || filename[i-1] == '\\'
#endif
        ) {
      // We're at the start of a directory name. Check for valid prefixes.
      for (kj::StringPtr root: ROOTS) {
        if (filename.slice(i).startsWith(root)) {
          filename = filename.slice(i + root.size());

          // We should keep searching to find the last instance of a root name. `i` is no longer
          // a valid index for `filename` so start the loop over.
          goto retry;
        }
      }
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
             e.getStackTrace().size() > 0 ? "\nstack: " : "",
             stringifyStackTraceAddresses(e.getStackTrace()),
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

ExceptionCallback::StackTraceMode ExceptionCallback::stackTraceMode() {
  return next.stackTraceMode();
}

Function<void(Function<void()>)> ExceptionCallback::getThreadInitializer() {
  return next.getThreadInitializer();
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

    while (textPtr != nullptr) {
      miniposix::ssize_t n = miniposix::write(STDERR_FILENO, textPtr.begin(), textPtr.size());
      if (n <= 0) {
        // stderr is broken.  Give up.
        return;
      }
      textPtr = textPtr.slice(n);
    }
  }

  StackTraceMode stackTraceMode() override {
#ifdef KJ_DEBUG
    return StackTraceMode::FULL;
#else
    return StackTraceMode::ADDRESS_ONLY;
#endif
  }

  Function<void(Function<void()>)> getThreadInitializer() override {
    return [](Function<void()> func) {
      // No initialization needed since RootExceptionCallback is automatically the root callback
      // for new threads.
      func();
    };
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
        e.getStackTrace().size() > 0 ? "\nstack: " : "",
        stringifyStackTraceAddresses(e.getStackTrace()),
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

#if _MSC_VER >= 1900
// MSVC14 has a refactored CRT which now provides a direct accessor for this value.
// See https://svn.boost.org/trac/boost/ticket/10158 for a brief discussion.
extern "C" int *__cdecl __processing_throw();

uint uncaughtExceptionCount() {
  return static_cast<uint>(*__processing_throw());
}

#elif _MSC_VER >= 1400
// The below was copied from:
// https://github.com/panaseleus/stack_unwinding/blob/master/boost/exception/uncaught_exception_count.hpp

extern "C" char *__cdecl _getptd();

uint uncaughtExceptionCount() {
  return *reinterpret_cast<uint*>(_getptd() + (sizeof(void*) == 8 ? 0x100 : 0x90));
}
#else
uint uncaughtExceptionCount() {
  // Since the above doesn't work, fall back to uncaught_exception(). This will produce incorrect
  // results in very obscure cases that Cap'n Proto doesn't really rely on anyway.
  return std::uncaught_exception();
}
#endif

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
