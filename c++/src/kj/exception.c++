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

#if _WIN32 || __CYGWIN__
#include "win32-api-version.h"
#endif

#if (_WIN32 && _M_X64) || (__CYGWIN__ && __x86_64__)
// Currently the Win32 stack-trace code only supports x86_64. We could easily extend it to support
// i386 as well but it requires some code changes around how we read the context to start the
// trace.
#define KJ_USE_WIN32_DBGHELP 1
#endif

#include "exception.h"
#include "string.h"
#include "debug.h"
#include "threadlocal.h"
#include "miniposix.h"
#include "function.h"
#include "main.h"
#include <stdlib.h>
#include <exception>
#include <new>
#include <signal.h>
#include <stdint.h>
#ifndef _WIN32
#include <sys/mman.h>
#endif
#include "io.h"

#if !KJ_NO_RTTI
#include <typeinfo>
#endif
#if __GNUC__
#include <cxxabi.h>
#endif

#if (__linux__ && __GLIBC__ && !__UCLIBC__) || __APPLE__
#define KJ_HAS_BACKTRACE 1
#include <execinfo.h>
#endif

#if _WIN32 || __CYGWIN__
#include <windows.h>
#include "windows-sanity.h"
#include <dbghelp.h>
#endif

#if (__linux__ || __APPLE__ || __CYGWIN__)
#include <stdio.h>
#include <pthread.h>
#endif

#if __CYGWIN__
#include <sys/cygwin.h>
#include <ucontext.h>
#endif

#if KJ_HAS_LIBDL
#include "dlfcn.h"
#endif

#if _MSC_VER
#include <intrin.h>
#endif

#if KJ_HAS_COMPILER_FEATURE(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#else
static void __lsan_ignore_object(const void* p) {}
#endif
// TODO(cleanup): Remove the LSAN stuff per https://github.com/capnproto/capnproto/pull/1255
// feedback.

namespace {
template <typename T>
inline T* lsanIgnoreObjectAndReturn(T* ptr) {
  // Defensively lsan_ignore_object since the documentation doesn't explicitly specify what happens
  // if you call this multiple times on the same object.
  // TODO(cleanup): Remove this per https://github.com/capnproto/capnproto/pull/1255.
  __lsan_ignore_object(ptr);
  return ptr;
}
}

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

#if KJ_USE_WIN32_DBGHELP

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

#if __GNUC__ && !__clang__ && __GNUC__ >= 8
// GCC 8 warns that our reinterpret_casts of function pointers below are casting between
// incompatible types. Yes, GCC, we know that. This is the nature of GetProcAddress(); it returns
// everything as `long long int (*)()` and we have to cast to the actual type.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
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
#if __GNUC__ && !__clang__ && __GNUC__ >= 9
#pragma GCC diagnostic pop
#endif
};

const Dbghelp& getDbghelp() {
  static Dbghelp dbghelp;
  return dbghelp;
}

ArrayPtr<void* const> getStackTrace(ArrayPtr<void*> space, uint ignoreCount,
                                    HANDLE thread, CONTEXT& context) {
  // NOTE: Apparently there is a function CaptureStackBackTrace() that is equivalent to glibc's
  //   backtrace(). Somehow I missed that when I originally wrote this. However,
  //   CaptureStackBackTrace() does not accept a CONTEXT parameter; it can only trace the caller.
  //   That's more problematic on Windows where breakHandler(), sehHandler(), and Cygwin signal
  //   handlers all depend on the ability to pass a CONTEXT. So we'll keep this code, which works
  //   after all.

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

    // Subtract 1 from each address so that we identify the calling instructions, rather than the
    // return addresses (which are typically the instruction after the call).
    space[count] = reinterpret_cast<void*>(frame.AddrPC.Offset - 1);
  }

  return space.slice(kj::min(ignoreCount, count), count);
}

}  // namespace
#endif

ArrayPtr<void* const> getStackTrace(ArrayPtr<void*> space, uint ignoreCount) {
  if (getExceptionCallback().stackTraceMode() == ExceptionCallback::StackTraceMode::NONE) {
    return nullptr;
  }

#if KJ_USE_WIN32_DBGHELP
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

#if (__GNUC__ && !_WIN32) || __clang__
// Allow dependents to override the implementation of stack symbolication by making it a weak
// symbol. We prefer weak symbols over some sort of callback registration mechanism becasue this
// allows an alternate symbolication library to be easily linked into tests without changing the
// code of the test.
__attribute__((weak))
#endif
String stringifyStackTrace(ArrayPtr<void* const> trace) {
  if (trace.size() == 0) return nullptr;
  if (getExceptionCallback().stackTraceMode() != ExceptionCallback::StackTraceMode::FULL) {
    return nullptr;
  }

#if KJ_USE_WIN32_DBGHELP && _MSC_VER

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
    DWORD displacement;
    if (dbghelp.symGetLineFromAddr64(process, reinterpret_cast<DWORD64>(trace[i]), &displacement, &lineInfo)) {
      lines[i] = kj::str('\n', lineInfo.FileName, ':', lineInfo.LineNumber);
    }
  }

  return strArray(lines, "");

#elif (__linux__ || __APPLE__ || __CYGWIN__) && !__ANDROID__
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
#elif __CYGWIN__
  wchar_t exeWinPath[MAX_PATH];
  if (GetModuleFileNameW(nullptr, exeWinPath, sizeof(exeWinPath)) == 0) {
    return nullptr;
  }
  char exePosixPath[MAX_PATH * 2];
  if (cygwin_conv_path(CCP_WIN_W_TO_POSIX, exeWinPath, exePosixPath, sizeof(exePosixPath)) < 0) {
    return nullptr;
  }
  p = popen(str("addr2line -e '", exePosixPath, "' ", strTrace).cStr(), "r");
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

StringPtr stringifyStackTraceAddresses(ArrayPtr<void* const> trace, ArrayPtr<char> scratch) {
  // Version which writes into a pre-allocated buffer. This is safe for signal handlers to the
  // extent that dladdr() is safe.
  //
  // TODO(cleanup): We should improve the KJ stringification framework so that there's a way to
  //   write this string directly into a larger message buffer with strPreallocated().

#if KJ_HAS_LIBDL
  char* ptr = scratch.begin();
  char* limit = scratch.end() - 1;

  for (auto addr: trace) {
    Dl_info info;
    // Shared libraries are mapped near the end of the address space while the executable is mapped
    // near the beginning. We want to print addresses in the executable as raw addresses, not
    // offsets, since that's what addr2line expects for executables. For shared libraries it
    // expects offsets. In any case, most frames are likely to be in the main executable so it
    // makes the output cleaner if we don't repeatedly write its name.
    if (reinterpret_cast<uintptr_t>(addr) >= 0x400000000000ull && dladdr(addr, &info)) {
      uintptr_t offset = reinterpret_cast<uintptr_t>(addr) -
                         reinterpret_cast<uintptr_t>(info.dli_fbase);
      ptr = _::fillLimited(ptr, limit, kj::StringPtr(info.dli_fname), "@0x"_kj, hex(offset));
    } else {
      ptr = _::fillLimited(ptr, limit, toCharSequence(addr));
    }

    ptr = _::fillLimited(ptr, limit, " "_kj);
  }
  *ptr = '\0';
  return StringPtr(scratch.begin(), ptr);
#else
  // TODO(someday): Support other platforms.
  return kj::strPreallocated(scratch, kj::delimited(trace, " "));
#endif
}

String getStackTrace() {
  void* space[32];
  auto trace = getStackTrace(space, 2);
  return kj::str(stringifyStackTraceAddresses(trace), stringifyStackTrace(trace));
}

namespace {

#if !KJ_NO_EXCEPTIONS

[[noreturn]] void terminateHandler() {
  void* traceSpace[32];

  // ignoreCount = 3 to ignore std::terminate entry.
  auto trace = kj::getStackTrace(traceSpace, 3);

  kj::String message;

  auto eptr = std::current_exception();
  if (eptr != nullptr) {
    try {
      std::rethrow_exception(eptr);
    } catch (const kj::Exception& exception) {
      message = kj::str("*** Fatal uncaught kj::Exception: ", exception, '\n');
    } catch (const std::exception& exception) {
      message = kj::str("*** Fatal uncaught std::exception: ", exception.what(),
                        "\nstack: ", stringifyStackTraceAddresses(trace),
                                     stringifyStackTrace(trace), '\n');
    } catch (...) {
      message = kj::str("*** Fatal uncaught exception of type: ", kj::getCaughtExceptionType(),
                        "\nstack: ", stringifyStackTraceAddresses(trace),
                                     stringifyStackTrace(trace), '\n');
    }
  } else {
    message = kj::str("*** std::terminate() called with no exception"
                      "\nstack: ", stringifyStackTraceAddresses(trace),
                                   stringifyStackTrace(trace), '\n');
  }

  kj::FdOutputStream(STDERR_FILENO).write(message.begin(), message.size());
  _exit(1);
}

#endif

}  // namespace

#if KJ_USE_WIN32_DBGHELP && !__CYGWIN__
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
            auto trace = getStackTrace(traceSpace, 0, thread, context);
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

kj::StringPtr exceptionDescription(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_BREAKPOINT: return "breakpoint";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "datatype misalignment";
    case EXCEPTION_FLT_DENORMAL_OPERAND: return "denormal floating point operand";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "floating point division by zero";
    case EXCEPTION_FLT_INEXACT_RESULT: return "inexact floating point result";
    case EXCEPTION_FLT_INVALID_OPERATION: return "invalid floating point operation";
    case EXCEPTION_FLT_OVERFLOW: return "floating point overflow";
    case EXCEPTION_FLT_STACK_CHECK: return "floating point stack overflow";
    case EXCEPTION_FLT_UNDERFLOW: return "floating point underflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_IN_PAGE_ERROR: return "page error";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divided by zero";
    case EXCEPTION_INT_OVERFLOW: return "integer overflow";
    case EXCEPTION_INVALID_DISPOSITION: return "invalid disposition";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "noncontinuable exception";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_SINGLE_STEP: return "single step";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    default: return "(unknown exception code)";
  }
}

LONG WINAPI sehHandler(EXCEPTION_POINTERS* info) {
  void* traceSpace[32];
  auto trace = getStackTrace(traceSpace, 0, GetCurrentThread(), *info->ContextRecord);
  auto message = kj::str("*** Received structured exception #0x",
                         hex(info->ExceptionRecord->ExceptionCode), ": ",
                         exceptionDescription(info->ExceptionRecord->ExceptionCode),
                         "; stack: ",
                         stringifyStackTraceAddresses(trace),
                         stringifyStackTrace(trace), '\n');
  FdOutputStream(STDERR_FILENO).write(message.begin(), message.size());
  return EXCEPTION_EXECUTE_HANDLER;  // still crash
}

}  // namespace

void printStackTraceOnCrash() {
  mainThreadId = GetCurrentThreadId();
  KJ_WIN32(SetConsoleCtrlHandler(breakHandler, TRUE));
  SetUnhandledExceptionFilter(&sehHandler);

#if !KJ_NO_EXCEPTIONS
  // Also override std::terminate() handler with something nicer for KJ.
  std::set_terminate(&terminateHandler);
#endif
}

#elif _WIN32
// Windows, but KJ_USE_WIN32_DBGHELP is not enabled. We can't print useful stack traces, so don't
// try to catch SEH nor ctrl+C.

void printStackTraceOnCrash() {
#if !KJ_NO_EXCEPTIONS
  std::set_terminate(&terminateHandler);
#endif
}

#else
namespace {

[[noreturn]] void crashHandler(int signo, siginfo_t* info, void* context) {
  void* traceSpace[32];

#if KJ_USE_WIN32_DBGHELP
  // Win32 backtracing can't trace its way out of a Cygwin signal handler. However, Cygwin gives
  // us direct access to the CONTEXT, which we can pass to the Win32 tracing functions.
  ucontext_t* ucontext = reinterpret_cast<ucontext_t*>(context);
  // Cygwin's mcontext_t has the same layout as CONTEXT.
  // TODO(someday): Figure out why this produces garbage for SIGINT from ctrl+C. It seems to work
  //   correctly for SIGSEGV.
  CONTEXT win32Context;
  static_assert(sizeof(ucontext->uc_mcontext) >= sizeof(win32Context),
      "mcontext_t should be an extension of CONTEXT");
  memcpy(&win32Context, &ucontext->uc_mcontext, sizeof(win32Context));
  auto trace = getStackTrace(traceSpace, 0, GetCurrentThread(), win32Context);
#else
  // ignoreCount = 2 to ignore crashHandler() and signal trampoline.
  auto trace = getStackTrace(traceSpace, 2);
#endif

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

#if !KJ_NO_EXCEPTIONS
  // Also override std::terminate() handler with something nicer for KJ.
  std::set_terminate(&terminateHandler);
#endif
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

void resetCrashHandlers() {
#ifndef _WIN32
  struct sigaction action;
  memset(&action, 0, sizeof(action));

  action.sa_handler = SIG_DFL;
  KJ_SYSCALL(sigaction(SIGSEGV, &action, nullptr));
  KJ_SYSCALL(sigaction(SIGBUS, &action, nullptr));
  KJ_SYSCALL(sigaction(SIGFPE, &action, nullptr));
  KJ_SYSCALL(sigaction(SIGABRT, &action, nullptr));
  KJ_SYSCALL(sigaction(SIGILL, &action, nullptr));
  KJ_SYSCALL(sigaction(SIGSYS, &action, nullptr));

#ifdef KJ_DEBUG
  KJ_SYSCALL(sigaction(SIGINT, &action, nullptr));
#endif
#endif

#if !KJ_NO_EXCEPTIONS
  std::set_terminate(nullptr);
#endif
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
          str(trimSourceFilename(c->file), ":", c->line, ": context: ", c->description, "\n");
      contextPtr = c->next;
    } else {
      break;
    }
  }

  // Note that we put "remote" before "stack" because trace frames are ordered callee before
  // caller, so this is the most natural presentation ordering.
  return str(strArray(contextText, ""),
             e.getFile(), ":", e.getLine(), ": ", e.getType(),
             e.getDescription() == nullptr ? "" : ": ", e.getDescription(),
             e.getRemoteTrace().size() > 0 ? "\nremote: " : "",
             e.getRemoteTrace(),
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

  if (other.remoteTrace != nullptr) {
    remoteTrace = kj::str(other.remoteTrace);
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

void Exception::extendTrace(uint ignoreCount, uint limit) {
  if (isFullTrace) {
    // Awkward: extendTrace() was called twice without truncating in between. This should probably
    // be an error, but historically we didn't check for this so I'm hesitant to make it an error
    // now. We shouldn't actually extend the trace, though, as our current trace is presumably
    // rooted in main() and it'd be weird to append frames "above" that.
    // TODO(cleanup): Abort here and see what breaks?
    return;
  }

  KJ_STACK_ARRAY(void*, newTraceSpace, kj::min(kj::size(trace), limit) + ignoreCount + 1,
      sizeof(trace)/sizeof(trace[0]) + 8, 128);

  auto newTrace = kj::getStackTrace(newTraceSpace, ignoreCount + 1);
  if (newTrace.size() > ignoreCount + 2) {
    // Remove suffix that won't fit into our static-sized trace.
    newTrace = newTrace.slice(0, kj::min(kj::size(trace) - traceCount, newTrace.size()));

    // Copy the rest into our trace.
    memcpy(trace + traceCount, newTrace.begin(), newTrace.asBytes().size());
    traceCount += newTrace.size();
    isFullTrace = true;
  }
}

void Exception::truncateCommonTrace() {
  if (isFullTrace) {
    // We're truncating the common portion of the full trace, turning it back into a limited
    // trace.
    isFullTrace = false;
  } else {
    // If the trace was never extended in the first place, trying to truncate it is at best a waste
    // of time and at worst might remove information for no reason. So, don't.
    //
    // This comes up in particular in coroutines, when the exception originated from a co_awaited
    // promise. In that case we manually add the one relevant frame to the trace, rather than
    // call extendTrace() just to have to truncate most of it again a moment later in the
    // unhandled_exception() callback.
    return;
  }

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
  // TODO(cleanup): Abort here if isFullTrace is true, and see what breaks. This method only makes
  // sense to call on partial traces.

  if (traceCount < kj::size(trace)) {
    trace[traceCount++] = ptr;
  }
}

void Exception::addTraceHere() {
#if __GNUC__
  addTrace(__builtin_return_address(0));
#elif _MSC_VER
  addTrace(_ReturnAddress());
#else
  #error "please implement for your compiler"
#endif
}

#if !KJ_NO_EXCEPTIONS

namespace {

KJ_THREADLOCAL_PTR(ExceptionImpl) currentException = nullptr;

}  // namespace

class ExceptionImpl: public Exception, public std::exception {
public:
  inline ExceptionImpl(Exception&& other): Exception(mv(other)) {
    insertIntoCurrentExceptions();
  }
  ExceptionImpl(const ExceptionImpl& other): Exception(other) {
    // No need to copy whatBuffer since it's just to hold the return value of what().
    insertIntoCurrentExceptions();
  }
  ~ExceptionImpl() {
    // Look for ourselves in the list.
    for (auto* ptr = &currentException; *ptr != nullptr; ptr = &(*ptr)->nextCurrentException) {
      if (*ptr == this) {
        *ptr = nextCurrentException;
        return;
      }
    }

    // Possibly the ExceptionImpl was destroyed on a different thread than created it? That's
    // pretty bad, we'd better abort.
    abort();
  }

  const char* what() const noexcept override;

private:
  mutable String whatBuffer;
  ExceptionImpl* nextCurrentException = nullptr;

  void insertIntoCurrentExceptions() {
    nextCurrentException = currentException;
    currentException = this;
  }

  friend class InFlightExceptionIterator;
};

const char* ExceptionImpl::what() const noexcept {
  whatBuffer = str(*this);
  return whatBuffer.begin();
}

InFlightExceptionIterator::InFlightExceptionIterator()
    : ptr(currentException) {}

Maybe<const Exception&> InFlightExceptionIterator::next() {
  if (ptr == nullptr) return nullptr;

  const ExceptionImpl& result = *static_cast<const ExceptionImpl*>(ptr);
  ptr = result.nextCurrentException;
  return result;
}

#endif  // !KJ_NO_EXCEPTIONS

kj::Exception getDestructionReason(void* traceSeparator, kj::Exception::Type defaultType,
    const char* defaultFile, int defaultLine, kj::StringPtr defaultDescription) {
#if !KJ_NO_EXCEPTIONS
  InFlightExceptionIterator iter;
  KJ_IF_MAYBE(e, iter.next()) {
    auto copy = kj::cp(*e);
    copy.truncateCommonTrace();
    return copy;
  } else {
#endif
    // Darn, use a generic exception.
    kj::Exception exception(defaultType, defaultFile, defaultLine,
        kj::heapString(defaultDescription));

    // Let's give some context on where the PromiseFulfiller was destroyed.
    exception.extendTrace(2, 16);

    // Add a separator that hopefully makes this understandable...
    exception.addTrace(traceSeparator);

    return exception;
#if !KJ_NO_EXCEPTIONS
  }
#endif
}

// =======================================================================================

namespace {

KJ_THREADLOCAL_PTR(ExceptionCallback) threadLocalCallback = nullptr;

}  // namespace

void requireOnStack(void* ptr, kj::StringPtr description) {
#if defined(FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION) || \
    KJ_HAS_COMPILER_FEATURE(address_sanitizer) || \
    defined(__SANITIZE_ADDRESS__)
  // When using libfuzzer or ASAN, this sanity check may spurriously fail, so skip it.
#else
  char stackVar;
  ptrdiff_t offset = reinterpret_cast<char*>(ptr) - &stackVar;
  KJ_REQUIRE(offset < 65536 && offset > -65536,
            kj::str(description));
#endif
}

ExceptionCallback::ExceptionCallback(): next(getExceptionCallback()) {
  requireOnStack(this, "ExceptionCallback must be allocated on the stack.");
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

namespace _ {  // private
  uint uncaughtExceptionCount();  // defined later in this file
}

class ExceptionCallback::RootExceptionCallback: public ExceptionCallback {
public:
  RootExceptionCallback(): ExceptionCallback(*this) {}

  void onRecoverableException(Exception&& exception) override {
#if KJ_NO_EXCEPTIONS
    logException(LogSeverity::ERROR, mv(exception));
#else
    if (_::uncaughtExceptionCount() > 0) {
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
        e.getRemoteTrace().size() > 0 ? "\nremote: " : "",
        e.getRemoteTrace(),
        e.getStackTrace().size() > 0 ? "\nstack: " : "",
        stringifyStackTraceAddresses(e.getStackTrace()),
        stringifyStackTrace(e.getStackTrace()), "\n"));
  }
};

ExceptionCallback& getExceptionCallback() {
  static auto defaultCallback = lsanIgnoreObjectAndReturn(
      new ExceptionCallback::RootExceptionCallback());
  // We allocate on the heap because some objects may throw in their destructors. If those objects
  // had static storage, they might get fully constructed before the root callback. If they however
  // then throw an exception during destruction, there would be a lifetime issue because their
  // destructor would end up getting registered after the root callback's destructor. One solution
  // is to just leak this pointer & allocate on first-use. The cost is that the initialization is
  // mildly more expensive (+ we need to annotate sanitizers to ignore the problem). A great
  // compiler annotation that would simply things would be one that allowed static variables to have
  // their destruction omitted wholesale. That would allow us to avoid the heap but still have the
  // same robust safety semantics leaking would give us. A practical alternative that could be
  // implemented without new compilers would be to define another static root callback in
  // RootExceptionCallback's destructor (+ a separate pointer to share its value with this
  // function). Since this would end up getting constructed during exit unwind, it would have the
  // nice property of effectively being guaranteed to be evicted last.
  //
  // All this being said, I came back to leaking the object is the easiest tweak here:
  //  * Can't go wrong
  //  * Easy to maintain
  //  * Throwing exceptions is bound to do be expensive and malloc-happy anyway, so the incremental
  //    cost of 1 heap allocation is minimal.
  //
  // TODO(cleanup): Harris has an excellent suggestion in
  //  https://github.com/capnproto/capnproto/pull/1255 that should ensure we initialize the root
  //  callback once on first use as a global & never destroy it.

  ExceptionCallback* scoped = threadLocalCallback;
  return scoped != nullptr ? *scoped : *defaultCallback;
}

void throwFatalException(kj::Exception&& exception, uint ignoreCount) {
  if (ignoreCount != (uint)kj::maxValue) exception.extendTrace(ignoreCount + 1);
  getExceptionCallback().onFatalException(kj::mv(exception));
  abort();
}

void throwRecoverableException(kj::Exception&& exception, uint ignoreCount) {
  if (ignoreCount != (uint)kj::maxValue) exception.extendTrace(ignoreCount + 1);
  getExceptionCallback().onRecoverableException(kj::mv(exception));
}

// =======================================================================================

namespace _ {  // private

#if __cplusplus >= 201703L

uint uncaughtExceptionCount() {
  return std::uncaught_exceptions();
}

#elif __GNUC__

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

// LLVM's libstdc++ doesn't declare __cxa_get_globals in its cxxabi.h. GNU does. Because it is
// extern "C", the compiler wills get upset if we re-declare it even in a different namespace.
#if _LIBCPPABI_VERSION
extern "C" void* __cxa_get_globals();
#else
using abi::__cxa_get_globals;
#endif

uint uncaughtExceptionCount() {
  return reinterpret_cast<FakeEhGlobals*>(__cxa_get_globals())->uncaughtExceptions;
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

#if !KJ_NO_EXCEPTIONS
void UnwindDetector::catchThrownExceptionAsSecondaryFault() const {
  // TODO(someday):  Attach the secondary exception to whatever primary exception is causing
  //   the unwind.  For now we just drop it on the floor as this is probably fine most of the
  //   time.
  getCaughtExceptionAsKj();
}
#endif

#if __GNUC__ && !KJ_NO_RTTI
static kj::String demangleTypeName(const char* name) {
  if (name == nullptr) return kj::heapString("(nil)");

  int status;
  char* buf = abi::__cxa_demangle(name, nullptr, nullptr, &status);
  kj::String result = kj::heapString(buf == nullptr ? name : buf);
  free(buf);
  return kj::mv(result);
}

kj::String getCaughtExceptionType() {
  return demangleTypeName(abi::__cxa_current_exception_type()->name());
}
#else
kj::String getCaughtExceptionType() {
  return kj::heapString("(unknown)");
}
#endif

namespace {

size_t sharedSuffixLength(kj::ArrayPtr<void* const> a, kj::ArrayPtr<void* const> b) {
  size_t result = 0;
  while (a.size() > 0 && b.size() > 0 && a.back() == b.back())  {
    ++result;
    a = a.slice(0, a.size() - 1);
    b = b.slice(0, b.size() - 1);
  }
  return result;
}

}  // namespace

kj::ArrayPtr<void* const> computeRelativeTrace(
    kj::ArrayPtr<void* const> trace, kj::ArrayPtr<void* const> relativeTo) {
  using miniposix::ssize_t;

  static constexpr size_t MIN_MATCH_LEN = 4;
  if (trace.size() < MIN_MATCH_LEN || relativeTo.size() < MIN_MATCH_LEN) {
    return trace;
  }

  kj::ArrayPtr<void* const> bestMatch = trace;
  uint bestMatchLen = MIN_MATCH_LEN - 1;  // must beat this to choose something else

  // `trace` and `relativeTrace` may have been truncated at different points. We iterate through
  // truncating various suffixes from one of the two and then seeing if the remaining suffixes
  // match.
  for (ssize_t i = -(ssize_t)(trace.size() - MIN_MATCH_LEN);
       i <= (ssize_t)(relativeTo.size() - MIN_MATCH_LEN);
       i++) {
    // Negative values truncate `trace`, positive values truncate `relativeTo`.
    kj::ArrayPtr<void* const> subtrace = trace.slice(0, trace.size() - kj::max<ssize_t>(0, -i));
    kj::ArrayPtr<void* const> subrt = relativeTo
        .slice(0, relativeTo.size() - kj::max<ssize_t>(0, i));

    uint matchLen = sharedSuffixLength(subtrace, subrt);
    if (matchLen > bestMatchLen) {
      bestMatchLen = matchLen;
      bestMatch = subtrace.slice(0, subtrace.size() - matchLen + 1);
    }
  }

  return bestMatch;
}

#if KJ_NO_EXCEPTIONS

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
  RecoverableExceptionCatcher catcher;
  runnable.run();
  KJ_IF_MAYBE(e, catcher.caught) {
    e->truncateCommonTrace();
  }
  return mv(catcher.caught);
}

}  // namespace _ (private)

#else  // KJ_NO_EXCEPTIONS

kj::Exception getCaughtExceptionAsKj() {
  try {
    throw;
  } catch (Exception& e) {
    e.truncateCommonTrace();
    return kj::mv(e);
  } catch (CanceledException) {
    throw;
  } catch (std::bad_alloc& e) {
    return Exception(Exception::Type::OVERLOADED,
                     "(unknown)", -1, str("std::bad_alloc: ", e.what()));
  } catch (std::exception& e) {
    return Exception(Exception::Type::FAILED,
                     "(unknown)", -1, str("std::exception: ", e.what()));
  } catch (TopLevelProcessContext::CleanShutdownException) {
    throw;
  } catch (...) {
#if __GNUC__ && !KJ_NO_RTTI
    return Exception(Exception::Type::FAILED, "(unknown)", -1, str(
        "unknown non-KJ exception of type: ", getCaughtExceptionType()));
#else
    return Exception(Exception::Type::FAILED, "(unknown)", -1, str("unknown non-KJ exception"));
#endif
  }
}
#endif  // !KJ_NO_EXCEPTIONS

}  // namespace kj
