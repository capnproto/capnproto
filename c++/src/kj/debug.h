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

// This file declares convenient macros for debug logging and error handling.  The macros make
// it excessively easy to extract useful context information from code.  Example:
//
//     KJ_ASSERT(a == b, a, b, "a and b must be the same.");
//
// On failure, this will throw an exception whose description looks like:
//
//     myfile.c++:43: bug in code: expected a == b; a = 14; b = 72; a and b must be the same.
//
// As you can see, all arguments after the first provide additional context.
//
// The macros available are:
//
// * `KJ_LOG(severity, ...)`:  Just writes a log message, to stderr by default (but you can
//   intercept messages by implementing an ExceptionCallback).  `severity` is `INFO`, `WARNING`,
//   `ERROR`, or `FATAL`.  By default, `INFO` logs are not written, but for command-line apps the
//   user should be able to pass a flag like `--verbose` to enable them.  Other log levels are
//   enabled by default.  Log messages -- like exceptions -- can be intercepted by registering an
//   ExceptionCallback.
//
// * `KJ_DBG(...)`:  Like `KJ_LOG`, but intended specifically for temporary log lines added while
//   debugging a particular problem.  Calls to `KJ_DBG` should always be deleted before committing
//   code.  It is suggested that you set up a pre-commit hook that checks for this.
//
// * `KJ_ASSERT(condition, ...)`:  Throws an exception if `condition` is false.  This macro should
//   be used to check for bugs in the surrounding code and its dependencies, but NOT to check for
//   invalid input.  In destructors (and code called from destructors), use the `KJ_DTOR_*` macros
//   described below instead.
//
// * `KJ_REQUIRE(condition, ...)`:  Like `KJ_ASSERT` but used to check preconditions -- e.g. to
//   validate parameters passed from a caller.  A failure indicates that the caller is buggy.
//
// * `KJ_ASSUME(condition, ...)`: Like `KJ_ASSERT`, but in release mode (if KJ_DEBUG is not
//   defined; see below) instead warrants to the compiler that the condition can be assumed to
//   hold, allowing it to optimize accordingly.  This can result in undefined behavior, so use
//   this macro *only* if you can prove to your satisfaction that the condition is guaranteed by
//   surrounding code, and if the condition failing to hold would in any case result in undefined
//   behavior in its dependencies.
//
// * `KJ_SYSCALL(code, ...)`:  Executes `code` assuming it makes a system call.  A negative result
//   is considered an error, with error code reported via `errno`.  EINTR is handled by retrying.
//   Other errors are handled by throwing an exception.  If you need to examine the return code,
//   assign it to a variable like so:
//
//       int fd;
//       KJ_SYSCALL(fd = open(filename, O_RDONLY), filename);
//
// * `KJ_NONBLOCKING_SYSCALL(code, ...)`:  Like KJ_SYSCALL, but will not throw an exception on
//   EAGAIN/EWOULDBLOCK.  The calling code should check the syscall's return value to see if it
//   indicates an error; in this case, it can assume the error was EAGAIN because any other error
//   would have caused an exception to be thrown.
//
// * `KJ_SYSCALL_FD(code, ...)` provides shorthand for the common case that the syscall returns
//   a file descriptor and you want to wrap that file descirptor in `kj::OwnFd`.
//   `KJ_SYSCALL_FD` is like `KJ_SYSCALL`, but:
//     * The syscall must return a file descriptor.
//     * The whole macro evaluates to a `kj::OwnFd`.
//
//   Example:
//
//       kj::OwnFd fd = KJ_SYSCALL_FD(open(filename, O_RDONLY), filename);
//
// * `KJ_CONTEXT(...)`:  Notes additional contextual information relevant to any exceptions thrown
//   from within the current scope.  That is, until control exits the block in which KJ_CONTEXT()
//   is used, if any exception is generated, it will contain the given information in its context
//   chain.  This is helpful because it can otherwise be very difficult to come up with error
//   messages that make sense within low-level helper code.  Note that the parameters to
//   KJ_CONTEXT() are only evaluated if an exception is thrown.  This implies that any variables
//   used must remain valid until the end of the scope.
//
// * `KJ_DTOR_ASSERT(condition, ...)`, `KJ_DTOR_REQUIRE`, `KJ_DTOR_SYSCALL`, `KJ_DTOR_WIN32`,
//   `KJ_DTOR_WINSOCK` and their `KJ_DTOR_FAIL_*` forms:  Variants for destructors (and code called
//   from destructors).  Throwing from a destructor while another exception is unwinding would
//   terminate the program, so if the check fails in that case, these log the failure and continue
//   with the next statement instead.  Otherwise they throw like the plain macros.  The unwinding
//   check only happens once the check has failed, so they cost the same as the plain macros.
//   Code after them must cope with the check having failed.
//
//       Foo::~Foo() noexcept(false) {
//         KJ_DTOR_ASSERT(buffer.size() == 0, "Buffer was not flushed.");
//         KJ_DTOR_SYSCALL(close(fd));
//       }
//
//   To protect calls to functions that may throw, use `KJ_DTOR_CALL()` (see exception.h).
//
// Notes:
// * Do not write expressions with side-effects in the message content part of the macro, as the
//   message will not necessarily be evaluated.
// * For every macro `FOO` above except `LOG`, there is also a `FAIL_FOO` macro used to report
//   failures that already happened.  For the macros that check a boolean condition, `FAIL_FOO`
//   omits the first parameter and behaves like it was `false`.  `FAIL_SYSCALL` (and `FAIL_WIN32`
//   on Windows) takes a string literal and an OS error number as the first two parameters.  The
//   string should be the name of the failed system call.
// * For every macro `FOO` above except `ASSUME`, there is a `DFOO` version which is only executed
//   in debug mode, i.e. when KJ_DEBUG is defined.
//   KJ_DEBUG is defined automatically by common.h when compiling without optimization (unless
//   NDEBUG is defined), but you can also define it explicitly (e.g. -DKJ_DEBUG).  Generally,
//   production builds should NOT use KJ_DEBUG as it may enable expensive checks that are unlikely
//   to fail.
// * Many of the macros have "_AT" variants -- like KJ_LOG_AT(), KJ_REQUIRE_AT(), etc. -- which
//   accept a kj::SourceLocation value to use when reporting the file and line number of failures.
//   These are useful for writing assertion functions for tests, to report failures at the source
//   location within the test where the assertion function was called, rather than the location
//   where the macro was called.

#pragma once

#include "string.h"
#include "exception.h"
#include "windows-sanity.h"  // work-around macro conflict with `ERROR`

KJ_BEGIN_HEADER

namespace kj {

// The macros below intentionally expand to `if (cond) {} else <statement>` so that they are safe
// to use in unbraced if/else statements. This can't be expressed with consistent braces.
// NOLINTBEGIN(readability-inconsistent-ifelse-braces)

#if _MSC_VER && !defined(__clang__)
// MSVC ignores `__declspec(noreturn)` on the `Debug::fail()` template when checking whether a
// function returns a value (C4715 / C4716), so the unconditional failure macros wrap the call in
// an infinite loop to show that control does not continue past it.
#define KJ_FAIL_NORETURN_ for (;;)
#else
#define KJ_FAIL_NORETURN_
#endif

#define KJ_SITE_AT_(location, condition, macroArgs) \
  (::kj::_::Debug::Site { (location).fileName, static_cast<int>((location).lineNumber), \
                          condition, macroArgs })
// The Debug::Site of a check in an _AT macro. The location is only known at runtime, so the Site
// is a temporary. Like KJ_PACKED_SITE_, `condition` is empty if there is none.
// The _AT macros pass their `location` in parentheses, as it may contain commas, e.g.
// `callerLocation<A, B>()` from a macro.

#if KJ_MSVC_TRADITIONAL_CPP
// MSVC does __VA_ARGS__ differently from GCC:
// - A trailing comma before an empty __VA_ARGS__ is removed automatically, whereas GCC wants
//   you to request this behavior with "##__VA_ARGS__".
// - If __VA_ARGS__ is passed directly as an argument to another macro, it will be treated as a
//   *single* argument rather than an argument list. This can be worked around by wrapping the
//   outer macro call in KJ_EXPAND(), which apparently forces __VA_ARGS__ to be expanded before
//   the macro is evaluated. I don't understand the C preprocessor.
// - Using "#__VA_ARGS__" to stringify __VA_ARGS__ expands to zero tokens when __VA_ARGS__ is
//   empty, rather than expanding to an empty string literal. We can work around by concatenating
//   with an empty string literal.

#define KJ_EXPAND(X) X

#define KJ_LOG(severity, ...) \
  if (!::kj::_::Debug::shouldLog(::kj::LogSeverity::severity)) {} else \
    ::kj::_::Debug::log(__FILE__, __LINE__, ::kj::LogSeverity::severity, \
                        "" #__VA_ARGS__, __VA_ARGS__)

#define KJ_LOG_AT(severity, location, ...) \
  if (!::kj::_::Debug::shouldLog(::kj::LogSeverity::severity)) {} else \
    ::kj::_::Debug::log((location).fileName, (location).lineNumber, ::kj::LogSeverity::severity, \
                        "" #__VA_ARGS__, __VA_ARGS__)

#define KJ_DBG(...) KJ_EXPAND(KJ_LOG(DBG, __VA_ARGS__))

// TODO(cleanup): It may be possible to reduce code duplication between the macros and their _AT
// variants, which only differ in their site: KJ_PACKED_SITE_ vs. KJ_SITE_AT_.

#define KJ_REQUIRE(cond, ...) \
  if (auto _kjCondition = ::kj::_::MAGIC_ASSERT << cond) {} else \
    ::kj::_::Debug::fail(KJ_PACKED_SITE_(#cond, "" #__VA_ARGS__), \
        ::kj::_::Debug::ConditionFailed(), _kjCondition, __VA_ARGS__)

#define KJ_REQUIRE_AT(cond, location, ...) \
  if (auto _kjCondition = ::kj::_::MAGIC_ASSERT << cond) {} else \
    ::kj::_::Debug::fail(KJ_SITE_AT_((location), #cond, "" #__VA_ARGS__), \
        ::kj::_::Debug::ConditionFailed(), _kjCondition, __VA_ARGS__)

#define KJ_FAIL_REQUIRE(...) \
  KJ_FAIL_NORETURN_ ::kj::_::Debug::fail(KJ_PACKED_SITE_("", "" #__VA_ARGS__), \
      ::kj::Exception::Type::FAILED, __VA_ARGS__)

#define KJ_FAIL_REQUIRE_AT(location, ...) \
  KJ_FAIL_NORETURN_ ::kj::_::Debug::fail(KJ_SITE_AT_((location), "", "" #__VA_ARGS__), \
      ::kj::Exception::Type::FAILED, __VA_ARGS__)

#define KJ_SYSCALL(call, ...) \
  if (auto _kjSyscallResult = ::kj::_::Debug::syscall([&](){return (call);}, false)) {} else \
    ::kj::_::Debug::fail(KJ_PACKED_SITE_(#call, "" #__VA_ARGS__), \
        _kjSyscallResult.getErrorNumber(), __VA_ARGS__)

#define KJ_NONBLOCKING_SYSCALL(call, ...) \
  if (auto _kjSyscallResult = ::kj::_::Debug::syscall([&](){return (call);}, true)) {} else \
    ::kj::_::Debug::fail(KJ_PACKED_SITE_(#call, "" #__VA_ARGS__), \
        _kjSyscallResult.getErrorNumber(), __VA_ARGS__)

#define KJ_FAIL_SYSCALL(code, errorNumber, ...) \
  KJ_FAIL_NORETURN_ ::kj::_::Debug::fail(KJ_PACKED_SITE_(code, "" #__VA_ARGS__), \
      errorNumber, __VA_ARGS__)

#if _WIN32 || __CYGWIN__

#define KJ_WIN32(call, ...) \
  if (auto _kjWin32Result = ::kj::_::Debug::win32Call(call)) {} else \
    ::kj::_::Debug::fail(KJ_PACKED_SITE_(#call, "" #__VA_ARGS__), \
        _kjWin32Result, __VA_ARGS__)

#define KJ_WINSOCK(call, ...) \
  if (auto _kjWin32Result = ::kj::_::Debug::winsockCall(call)) {} else \
    ::kj::_::Debug::fail(KJ_PACKED_SITE_(#call, "" #__VA_ARGS__), \
        _kjWin32Result, __VA_ARGS__)

#define KJ_FAIL_WIN32(code, errorNumber, ...) \
  KJ_FAIL_NORETURN_ ::kj::_::Debug::fail(KJ_PACKED_SITE_(code, "" #__VA_ARGS__), \
      ::kj::_::Debug::Win32Result(errorNumber), __VA_ARGS__)

#endif  // _WIN32 || __CYGWIN__

#define KJ_UNIMPLEMENTED(...) \
  KJ_FAIL_NORETURN_ ::kj::_::Debug::fail(KJ_PACKED_SITE_("", "" #__VA_ARGS__), \
      ::kj::Exception::Type::UNIMPLEMENTED, __VA_ARGS__)

#define KJ_DTOR_REQUIRE(cond, ...) \
  if (auto _kjCondition = ::kj::_::MAGIC_ASSERT << cond) {} else \
    ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_(#cond, "" #__VA_ARGS__), \
        ::kj::_::Debug::ConditionFailed(), _kjCondition, __VA_ARGS__)

#define KJ_DTOR_FAIL_REQUIRE(...) \
  ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_("", "" #__VA_ARGS__), \
      ::kj::Exception::Type::FAILED, __VA_ARGS__)

#define KJ_DTOR_SYSCALL(call, ...) \
  if (auto _kjSyscallResult = ::kj::_::Debug::syscall([&](){return (call);}, false)) {} else \
    ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_(#call, "" #__VA_ARGS__), \
        _kjSyscallResult.getErrorNumber(), __VA_ARGS__)

#define KJ_DTOR_FAIL_SYSCALL(code, errorNumber, ...) \
  ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_(code, "" #__VA_ARGS__), \
      errorNumber, __VA_ARGS__)

#if _WIN32 || __CYGWIN__

#define KJ_DTOR_WIN32(call, ...) \
  if (auto _kjWin32Result = ::kj::_::Debug::win32Call(call)) {} else \
    ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_(#call, "" #__VA_ARGS__), \
        _kjWin32Result, __VA_ARGS__)

#define KJ_DTOR_WINSOCK(call, ...) \
  if (auto _kjWin32Result = ::kj::_::Debug::winsockCall(call)) {} else \
    ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_(#call, "" #__VA_ARGS__), \
        _kjWin32Result, __VA_ARGS__)

#define KJ_DTOR_FAIL_WIN32(code, errorNumber, ...) \
  ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_(code, "" #__VA_ARGS__), \
      ::kj::_::Debug::Win32Result(errorNumber), __VA_ARGS__)

#endif  // _WIN32 || __CYGWIN__

// TODO(msvc):  MSVC mis-deduces `ContextImpl<decltype(func)>` as `ContextImpl<int>` in some edge
// cases, such as inside nested lambdas inside member functions. Wrapping the type in
// `decltype(instance<...>())` helps it deduce the context function's type correctly.
#define KJ_CONTEXT(...) \
  auto KJ_UNIQUE_NAME(_kjContextFunc) = [&]() -> ::kj::_::Debug::Context::Value { \
        return ::kj::_::Debug::Context::Value(__FILE__, __LINE__, \
            ::kj::_::Debug::makeDescription("" #__VA_ARGS__, __VA_ARGS__)); \
      }; \
  decltype(::kj::instance<::kj::_::Debug::ContextImpl<decltype(KJ_UNIQUE_NAME(_kjContextFunc))>>()) \
      KJ_UNIQUE_NAME(_kjContext)(KJ_UNIQUE_NAME(_kjContextFunc))

#define KJ_REQUIRE_NONNULL(value, ...) \
  (*[&] { \
    auto _kj_result = ::kj::_::readMaybe(value); \
    if (KJ_UNLIKELY(!_kj_result)) { \
      ::kj::_::Debug::fail(KJ_PACKED_SITE_(#value " != nullptr", "" #__VA_ARGS__), \
          ::kj::Exception::Type::FAILED, __VA_ARGS__); \
    } \
    return _kj_result; \
  }())

#define KJ_REQUIRE_NONNULL_AT(value, location, ...) \
  (*[&] { \
    auto _kj_result = ::kj::_::readMaybe(value); \
    if (KJ_UNLIKELY(!_kj_result)) { \
      ::kj::_::Debug::fail(KJ_SITE_AT_((location), #value " != nullptr", "" #__VA_ARGS__), \
          ::kj::Exception::Type::FAILED, __VA_ARGS__); \
    } \
    return _kj_result; \
  }())

#define KJ_EXCEPTION(type, ...) \
  ::kj::Exception(::kj::Exception::Type::type, __FILE__, __LINE__, \
      ::kj::_::Debug::makeDescription("" #__VA_ARGS__, __VA_ARGS__))

#else  // KJ_MSVC_TRADITIONAL_CPP

#define KJ_LOG(severity, ...) \
  if (!::kj::_::Debug::shouldLog(::kj::LogSeverity::severity)) {} else \
    ::kj::_::Debug::log(__FILE__, __LINE__, ::kj::LogSeverity::severity, \
                        #__VA_ARGS__, ##__VA_ARGS__)

#define KJ_LOG_AT(severity, location, ...) \
  if (!::kj::_::Debug::shouldLog(::kj::LogSeverity::severity)) {} else \
    ::kj::_::Debug::log((location).fileName, (location).lineNumber, ::kj::LogSeverity::severity, \
                        #__VA_ARGS__, ##__VA_ARGS__)

#define KJ_DBG(...) KJ_LOG(DBG __VA_OPT__(,) __VA_ARGS__)

#define KJ_REQUIRE(cond, ...) \
  if (auto _kjCondition = ::kj::_::MAGIC_ASSERT << cond) {} else \
    ::kj::_::Debug::fail(KJ_PACKED_SITE_(#cond, #__VA_ARGS__), \
        ::kj::_::Debug::ConditionFailed(), _kjCondition, ##__VA_ARGS__)

#define KJ_REQUIRE_AT(cond, location, ...) \
  if (auto _kjCondition = ::kj::_::MAGIC_ASSERT << cond) {} else \
    ::kj::_::Debug::fail(KJ_SITE_AT_((location), #cond, #__VA_ARGS__), \
        ::kj::_::Debug::ConditionFailed(), _kjCondition, ##__VA_ARGS__)

#define KJ_FAIL_REQUIRE(...) \
  KJ_FAIL_NORETURN_ ::kj::_::Debug::fail(KJ_PACKED_SITE_("", #__VA_ARGS__), \
      ::kj::Exception::Type::FAILED __VA_OPT__(,) __VA_ARGS__)

#define KJ_FAIL_REQUIRE_AT(location, ...) \
  KJ_FAIL_NORETURN_ ::kj::_::Debug::fail(KJ_SITE_AT_((location), "", #__VA_ARGS__), \
      ::kj::Exception::Type::FAILED, ##__VA_ARGS__)

#define KJ_SYSCALL(call, ...) \
  if (auto _kjSyscallResult = ::kj::_::Debug::syscall([&](){return (call);}, false)) {} else \
    ::kj::_::Debug::fail(KJ_PACKED_SITE_(#call, #__VA_ARGS__), \
        _kjSyscallResult.getErrorNumber(), ##__VA_ARGS__)

#define KJ_NONBLOCKING_SYSCALL(call, ...) \
  if (auto _kjSyscallResult = ::kj::_::Debug::syscall([&](){return (call);}, true)) {} else \
    ::kj::_::Debug::fail(KJ_PACKED_SITE_(#call, #__VA_ARGS__), \
        _kjSyscallResult.getErrorNumber(), ##__VA_ARGS__)

#define KJ_FAIL_SYSCALL(code, errorNumber, ...) \
  KJ_FAIL_NORETURN_ ::kj::_::Debug::fail(KJ_PACKED_SITE_(code, #__VA_ARGS__), \
      errorNumber, ##__VA_ARGS__)

#if _WIN32 || __CYGWIN__

#define KJ_WIN32(call, ...) \
  if (auto _kjWin32Result = ::kj::_::Debug::win32Call(call)) {} else \
    ::kj::_::Debug::fail(KJ_PACKED_SITE_(#call, #__VA_ARGS__), \
        _kjWin32Result, ##__VA_ARGS__)
// Invoke a Win32 syscall that returns either BOOL or HANDLE, and throw an exception if it fails.

#define KJ_WINSOCK(call, ...) \
  if (auto _kjWin32Result = ::kj::_::Debug::winsockCall(call)) {} else \
    ::kj::_::Debug::fail(KJ_PACKED_SITE_(#call, #__VA_ARGS__), \
        _kjWin32Result, ##__VA_ARGS__)
// Like KJ_WIN32 but for winsock calls which return `int` with SOCKET_ERROR indicating failure.
//
// Unfortunately, it's impossible to distinguish these from BOOL-returning Win32 calls by type,
// since BOOL is in fact an alias for `int`. :(

#define KJ_FAIL_WIN32(code, errorNumber, ...) \
  KJ_FAIL_NORETURN_ ::kj::_::Debug::fail(KJ_PACKED_SITE_(code, #__VA_ARGS__), \
      ::kj::_::Debug::Win32Result(errorNumber), ##__VA_ARGS__)

#endif

#define KJ_UNIMPLEMENTED(...) \
  KJ_FAIL_NORETURN_ ::kj::_::Debug::fail(KJ_PACKED_SITE_("", #__VA_ARGS__), \
      ::kj::Exception::Type::UNIMPLEMENTED __VA_OPT__(,) __VA_ARGS__)

#define KJ_DTOR_REQUIRE(cond, ...) \
  if (auto _kjCondition = ::kj::_::MAGIC_ASSERT << cond) {} else \
    ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_(#cond, #__VA_ARGS__), \
        ::kj::_::Debug::ConditionFailed(), _kjCondition, ##__VA_ARGS__)
// Like KJ_REQUIRE, but for destructors: if the check fails while another exception is unwinding
// the stack, the failure is logged and execution continues with the next statement, instead of
// throwing (which would terminate the process). See `kj::throwExceptionUnlessUnwinding()`.

#define KJ_DTOR_FAIL_REQUIRE(...) \
  ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_("", #__VA_ARGS__), \
      ::kj::Exception::Type::FAILED __VA_OPT__(,) __VA_ARGS__)

#define KJ_DTOR_SYSCALL(call, ...) \
  if (auto _kjSyscallResult = ::kj::_::Debug::syscall([&](){return (call);}, false)) {} else \
    ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_(#call, #__VA_ARGS__), \
        _kjSyscallResult.getErrorNumber(), ##__VA_ARGS__)

#define KJ_DTOR_FAIL_SYSCALL(code, errorNumber, ...) \
  ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_(code, #__VA_ARGS__), \
      errorNumber, ##__VA_ARGS__)

#if _WIN32 || __CYGWIN__

#define KJ_DTOR_WIN32(call, ...) \
  if (auto _kjWin32Result = ::kj::_::Debug::win32Call(call)) {} else \
    ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_(#call, #__VA_ARGS__), \
        _kjWin32Result, ##__VA_ARGS__)

#define KJ_DTOR_WINSOCK(call, ...) \
  if (auto _kjWin32Result = ::kj::_::Debug::winsockCall(call)) {} else \
    ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_(#call, #__VA_ARGS__), \
        _kjWin32Result, ##__VA_ARGS__)

#define KJ_DTOR_FAIL_WIN32(code, errorNumber, ...) \
  ::kj::_::Debug::failUnlessUnwinding(KJ_PACKED_SITE_(code, #__VA_ARGS__), \
      ::kj::_::Debug::Win32Result(errorNumber), ##__VA_ARGS__)

#endif  // _WIN32 || __CYGWIN__

#define KJ_CONTEXT(...) \
  auto KJ_UNIQUE_NAME(_kjContextFunc) = [&]() -> ::kj::_::Debug::Context::Value { \
        return ::kj::_::Debug::Context::Value(__FILE__, __LINE__, \
            ::kj::_::Debug::makeDescription(#__VA_ARGS__ __VA_OPT__(,) __VA_ARGS__)); \
      }; \
  ::kj::_::Debug::ContextImpl<decltype(KJ_UNIQUE_NAME(_kjContextFunc))> \
      KJ_UNIQUE_NAME(_kjContext)(KJ_UNIQUE_NAME(_kjContextFunc))

#if _MSC_VER && !defined(__clang__)

#define KJ_REQUIRE_NONNULL(value, ...) \
  (*([&] { \
    auto _kj_result = ::kj::_::readMaybe(value); \
    if (KJ_UNLIKELY(!_kj_result)) { \
      ::kj::_::Debug::fail(KJ_PACKED_SITE_(#value " != nullptr", #__VA_ARGS__), \
          ::kj::Exception::Type::FAILED, ##__VA_ARGS__); \
    } \
    return _kj_result; \
  }()))

#define KJ_REQUIRE_NONNULL_AT(value, location, ...) \
  (*([&] { \
    auto _kj_result = ::kj::_::readMaybe(value); \
    if (KJ_UNLIKELY(!_kj_result)) { \
      ::kj::_::Debug::fail(KJ_SITE_AT_((location), #value " != nullptr", #__VA_ARGS__), \
          ::kj::Exception::Type::FAILED, ##__VA_ARGS__); \
    } \
    return _kj_result; \
  }()))

#else

#define KJ_REQUIRE_NONNULL(value, ...) \
  (*({ \
    auto _kj_result = ::kj::_::readMaybe(value); \
    if (KJ_UNLIKELY(!_kj_result)) { \
      ::kj::_::Debug::fail(KJ_PACKED_SITE_(#value " != nullptr", #__VA_ARGS__), \
          ::kj::Exception::Type::FAILED, ##__VA_ARGS__); \
    } \
    kj::mv(_kj_result); \
  }))

#define KJ_REQUIRE_NONNULL_AT(value, location, ...) \
  (*({ \
    auto _kj_result = ::kj::_::readMaybe(value); \
    if (KJ_UNLIKELY(!_kj_result)) { \
      ::kj::_::Debug::fail(KJ_SITE_AT_((location), #value " != nullptr", #__VA_ARGS__), \
          ::kj::Exception::Type::FAILED, ##__VA_ARGS__); \
    } \
    kj::mv(_kj_result); \
  }))

#endif

#define KJ_EXCEPTION(type, ...) \
  ::kj::Exception(::kj::Exception::Type::type, __FILE__, __LINE__, \
      ::kj::_::Debug::makeDescription(#__VA_ARGS__, ##__VA_ARGS__))

#endif  // KJ_MSVC_TRADITIONAL_CPP, else

#define KJ_SYSCALL_HANDLE_ERRORS(call) \
  if (int _kjSyscallError = ::kj::_::Debug::syscallError([&](){return (call);}, false)) \
    switch (int error KJ_UNUSED = _kjSyscallError)
// Like KJ_SYSCALL, but doesn't throw. Instead, the block after the macro is a switch block on the
// error. Additionally, the int value `error` is defined within the block. So you can do:
//
//     KJ_SYSCALL_HANDLE_ERRORS(foo()) {
//       case ENOENT:
//         handleNoSuchFile();
//         break;
//       case EEXIST:
//         handleExists();
//         break;
//       default:
//         KJ_FAIL_SYSCALL("foo()", error);
//     } else {
//       handleSuccessCase();
//     }

#if _WIN32 || __CYGWIN__

#define KJ_WIN32_HANDLE_ERRORS(call) \
  if (uint _kjWin32Error = ::kj::_::Debug::win32Call(call).number) \
    switch (uint error KJ_UNUSED = _kjWin32Error)
// Like KJ_WIN32, but doesn't throw. Instead, the block after the macro is a switch block on the
// error. Additionally, the int value `error` is defined within the block. So you can do:
//
//     KJ_SYSCALL_HANDLE_ERRORS(foo()) {
//       case ERROR_FILE_NOT_FOUND:
//         handleNoSuchFile();
//         break;
//       case ERROR_FILE_EXISTS:
//         handleExists();
//         break;
//       default:
//         KJ_FAIL_WIN32("foo()", error);
//     } else {
//       handleSuccessCase();
//     }

#endif

// NOLINTEND(readability-inconsistent-ifelse-braces)

#if _MSC_VER && !defined(__clang__)

#define KJ_SYSCALL_FD(...) \
  ([&]{ \
    int _kj_fd; \
    KJ_SYSCALL(_kj_fd = __VA_ARGS__); \
    return kj::OwnFd(_kj_fd); \
  }())

#else

#define KJ_SYSCALL_FD(...) \
  ({ \
    int _kj_fd; \
    KJ_SYSCALL(_kj_fd = __VA_ARGS__); \
    (kj::OwnFd(_kj_fd)); \
  })

#endif

// TODO(someday): Add KJ_WIN32_HANDLE(), similar to KJ_SYSCALL_FD().

#define KJ_ASSERT KJ_REQUIRE
#define KJ_FAIL_ASSERT KJ_FAIL_REQUIRE
#define KJ_ASSERT_NONNULL KJ_REQUIRE_NONNULL
#define KJ_ASSERT_AT KJ_REQUIRE_AT
#define KJ_FAIL_ASSERT_AT KJ_FAIL_REQUIRE_AT
#define KJ_ASSERT_NONNULL_AT KJ_REQUIRE_NONNULL_AT
#define KJ_DTOR_ASSERT KJ_DTOR_REQUIRE
#define KJ_DTOR_FAIL_ASSERT KJ_DTOR_FAIL_REQUIRE
// Use "ASSERT" in place of "REQUIRE" when the problem is local to the immediate surrounding code.
// That is, if the assert ever fails, it indicates that the immediate surrounding code is broken.

#ifdef KJ_DEBUG
#define KJ_DLOG KJ_LOG
#define KJ_DASSERT KJ_ASSERT
#define KJ_DREQUIRE KJ_REQUIRE
#define KJ_ASSUME KJ_ASSERT
#else
#define KJ_DLOG(...) do {} while (false)
#define KJ_DASSERT(...) do {} while (false)
#define KJ_DREQUIRE(...) do {} while (false)
#if defined(__GNUC__)
#define KJ_ASSUME(cond, ...) do { if (cond) {} else { __builtin_unreachable(); } } while (false)
#elif defined(__clang__)
#define KJ_ASSUME(cond, ...) __builtin_assume(cond)
#elif defined(_MSC_VER)
#define KJ_ASSUME(cond, ...) __assume(cond)
#else
#define KJ_ASSUME(...) do {} while (false)
#endif

#endif

namespace _ {  // private

#if defined(_MSC_VER) && !defined(__clang__)
#define KJ_FAIL_BY_VALUE_ 0
// MSVC doesn't inline anything in debug builds, not even `__forceinline` functions, so the wrapper
// that passes arguments by value would add a frame to the stack trace of every failed check.
#define KJ_FAIL_INLINE_ inline
#define KJ_FAIL_COLD_
#else
#define KJ_FAIL_BY_VALUE_ 1
#define KJ_FAIL_INLINE_ inline __attribute__((always_inline))
// Unlike KJ_ALWAYS_INLINE, inlines even in debug builds: Debug::fail() must not add a frame to
// the stack trace.
#define KJ_FAIL_COLD_ __attribute__((cold))
// The failure and logging paths: optimized for size and kept away from the code that calls them.
#endif

#if KJ_FAIL_BY_VALUE_

template <typename T, typename From>
inline constexpr bool debugIsSmallClass = requires {
  requires (sizeof(T) <= 2 * sizeof(void*));
  requires __is_trivially_copyable(T);
  requires __is_trivially_constructible(T, From);
};
// Whether `T` is a complete class that can be copied as cheaply as two pointers, e.g. a StringPtr
// or an iterator, from a `From`, the reference to the value that is copied. Being trivially
// copyable isn't enough: for a non-const lvalue, a constructor template taking `U&&` is a better
// match than the copy constructor, and could make a copy that is stringified differently.

template <typename T, typename From>
inline constexpr bool debugByValue = __is_trivially_copyable(T);

template <typename T, typename From>
  requires (__is_class(T) || __is_union(T))
inline constexpr bool debugByValue<T, From> = debugIsSmallClass<T, From>;

template <typename T, typename From>
inline constexpr bool debugByValue<T&, From> = debugByValue<T, From>;
template <typename T, typename From>
inline constexpr bool debugByValue<T&&, From> = debugByValue<T, From>;
template <typename T, size_t n, typename From>
inline constexpr bool debugByValue<T[n], From> = false;
template <typename T, typename From>
inline constexpr bool debugByValue<T[], From> = false;
// Whether `T`, ignoring references, is cheap to copy from a `From`: a scalar type (a number, enum
// or pointer; non-class, non-array types are trivially copyable if and only if they are scalars),
// or a small class (see debugIsSmallClass).

template <typename T, bool = debugByValue<T, T&&>>
struct DebugParam_ { typedef T&& Type; };
template <typename T>
struct DebugParam_<T, true> { typedef Decay<T> Type; };
template <typename T>
using DebugParam = typename DebugParam_<T>::Type;
// The type in which Debug::fail() passes an argument of type `T&&` to the out-of-line code that
// stringifies it. Scalars (numbers, enums, pointers) and small trivially copyable classes are
// copied, everything else is passed by reference. Copying is cheap on the failure path, and means
// that the function containing the check doesn't take the argument's address: a local variable
// can stay in registers, rather than being written to the stack every time the check passes.
//
// Copies don't change the description: everything is stringified before the call returns, while
// any temporaries in the macro's arguments are still alive.

template <typename T>
struct DebugConditionParam_ { typedef DebugParam<T> Type; };
template <typename T>
using DebugConditionParam = typename DebugConditionParam_<T>::Type;
// The type in which Debug::fail() passes a check's condition that is a comparison. It's
// specialized below to pass a DebugComparison as DebugComparisonOperands, so that a comparison as
// `_kjCondition` doesn't need to be in memory either. Only the condition is passed that way: a
// DebugComparison that is an ordinary argument is stringified like any other.

#else  // KJ_FAIL_BY_VALUE_

template <typename T>
using DebugParam = T&&;

#endif  // KJ_FAIL_BY_VALUE_, else

template <typename T>
inline constexpr bool debugIsComparison = requires(T& condition) {
  debugComparison(condition);
};
// Whether a check's evaluated condition is a comparison, whose operands are shown when the check
// fails: a DebugComparison, or the DebugComparisonOperands that DebugConditionParam passes one as.
// Each has a debugComparison() overload, below.

class Debug {
public:
  Debug() = delete;

  typedef LogSeverity Severity;  // backwards-compatibility

#if _WIN32 || __CYGWIN__
  struct Win32Result {
    uint number;
    inline explicit Win32Result(uint number): number(number) {}
    operator bool() const { return number == 0; }
  };
#endif

  static inline bool shouldLog(LogSeverity severity) { return severity >= minSeverity; }
  // Returns whether messages of the given severity should be logged.

  static inline void setLogLevel(LogSeverity severity) { minSeverity = severity; }
  // Set the minimum message severity which will be logged.
  //
  // TODO(someday):  Expose publicly.

  template <typename... Params>
  static KJ_FAIL_INLINE_ void log(const char* file, int line, LogSeverity severity,
                                  const char* macroArgs, Params&&... params);
  // Used by KJ_LOG when shouldLog(severity) is true. logImpl() builds the argument array out of
  // line, so the caller can keep cheap arguments in registers (see DebugParam). An argument passed
  // by reference may still need stack space in the caller.

  struct ConditionFailed {};
  // The code that the check macros pass to fail() when their condition is false. The exception's
  // type is FAILED, and the first param is the evaluated condition. If it's a comparison (see
  // debugIsComparison), its operands are shown after the condition's text, e.g.
  // "expected a == b [1 == 2]". Anything else, e.g. a DebugExpression or the bool that `&&`
  // returns, isn't shown.

  template <typename S, typename... Params>
  static void logFailedCheck(LogSeverity severity, S site, Params&&... params);
  // Logs a failed check, described like fail(site, ConditionFailed(), params...) would but
  // prefixed with "failed: ". Used by KJ_EXPECT, whose failure doesn't throw.

  struct Site {
    // Where a check is, its condition (empty if none) and the names of the macro's arguments.
    const char* file;
    int line;
    const char* condition;
    const char* macroArgs;
  };

  struct Arg {
    // An argument of a failed check or a log message, with the function that stringifies it.
    // The templates only fill in an array of these: the stringification is shared by all
    // arguments of the same type, and the rest of the work by all checks and messages.
    const void* value;
    String (*stringify)(const void* value);

    template <typename T>
    static KJ_FAIL_COLD_ String stringifyValue(const void* value) {
      // `str(value)` on a non-const lvalue, as the macro's arguments are named parameters.
      return str(*const_cast<T*>(static_cast<const T*>(value)));
    }

    static KJ_FAIL_COLD_ String stringifyChars(const void* value) {
      return str(static_cast<const char*>(value));
    }

    template <typename T>
    static Arg of(T& value) { return { __builtin_addressof(value), &stringifyValue<T> }; }
    // (`__builtin_addressof` rather than `&`, which the argument's type may overload.)
    template <size_t s>
    static Arg of(const char (&value)[s]) { return { value, &stringifyChars }; }
    template <size_t s>
    static Arg of(char (&value)[s]) { return { value, &stringifyChars }; }
    // Char arrays, usually message literals, are stringified like `const char*` (up to the first
    // NUL) by `str()`, so they share one function, whatever their size.
  };

  struct Comparison {
    // A failed comparison in a check, e.g. `a == b`: its operands, each stringified by the
    // function shared by all operands of its type, and the operator between them.
    Arg left;
    StringPtr op;
    Arg right;
  };

#if KJ_FAIL_BY_VALUE_
  template <typename S, typename Code, typename... Params>
  static KJ_FAIL_INLINE_ KJ_NORETURN(void fail(S site, Code code, Params&&... params));
  template <typename S, typename Condition, typename... Params>
  static KJ_FAIL_INLINE_ KJ_NORETURN(void fail(S site, ConditionFailed code,
      Condition&& condition, Params&&... params));
  template <typename S, typename Code, typename... Params>
  static KJ_FAIL_INLINE_ void failUnlessUnwinding(S site, Code code, Params&&... params);
  template <typename S, typename Condition, typename... Params>
  static KJ_FAIL_INLINE_ void failUnlessUnwinding(S site, ConditionFailed code,
      Condition&& condition, Params&&... params);
  // Forward to failImpl() / failUnlessUnwindingImpl(), passing cheap arguments by value (see
  // `DebugParam`), and a condition that is a comparison as its operands (see
  // `DebugConditionParam`). A condition that isn't shown, as it isn't a comparison, isn't passed
  // at all: the failure is described like that of a check without a condition, and shares its
  // instantiation. Always inlined, even in debug builds, so that they don't add a frame to the
  // stack trace.

  template <typename S, typename Code, typename... Params>
  static KJ_NOINLINE KJ_FAIL_COLD_ KJ_NORETURN(void failImpl(S site, Code code,
      Params... params));
  template <typename S, typename Code, typename... Params>
  static KJ_NOINLINE KJ_FAIL_COLD_ KJ_NOT_TAIL_CALLED void failUnlessUnwindingImpl(
      S site, Code code, Params... params);
#else
  template <typename S, typename Code, typename... Params>
  static KJ_NOINLINE KJ_NORETURN(void fail(S site, Code code, Params&&... params));
  template <typename S, typename Code, typename... Params>
  static KJ_NOINLINE KJ_NOT_TAIL_CALLED void failUnlessUnwinding(S site, Code code,
      Params&&... params);
#endif
  // fail() throws an exception describing a failed check. Used by the check macros. Kept out of
  // line so that the call site is just a branch and a call. `site` is a PackedSite or a Site.
  //
  // failUnlessUnwinding() throws an exception describing a failed check, unless another exception
  // is unwinding the stack, in which case it logs the failure and returns; see
  // `kj::throwExceptionUnlessUnwinding()`. Used by the `KJ_DTOR_*` macros. The unwinding check
  // only happens out of line, on the failure path, so the call site is a branch and a call, with
  // no try/catch. Not tail-called, so that the trace (which skips the out-of-line frame) still
  // starts with the function containing the check.

  class SyscallResult {
  public:
    inline SyscallResult(int errorNumber): errorNumber(errorNumber) {}
    inline operator void*() { return errorNumber == 0 ? this : nullptr; }
    inline int getErrorNumber() { return errorNumber; }

  private:
    int errorNumber;
  };

  template <typename Call>
  static SyscallResult syscall(Call&& call, bool nonblocking);
  template <typename Call>
  static int syscallError(Call&& call, bool nonblocking);

#if _WIN32 || __CYGWIN__
  static Win32Result win32Call(int boolean);
  static Win32Result win32Call(void* handle);
  static Win32Result winsockCall(int result);
  static uint getWin32ErrorCode();
#endif

  class Context: public ExceptionCallback {
  public:
    Context();
    KJ_DISALLOW_COPY_AND_MOVE(Context);
    virtual ~Context() noexcept(false);

    struct Value {
      const char* file;
      int line;
      String description;

      inline Value(const char* file, int line, String&& description)
          : file(file), line(line), description(mv(description)) {}
    };

    virtual Value evaluate() = 0;

    virtual void onException(Exception&& exception) override;
    virtual void logMessage(LogSeverity severity, const char* file, int line, int contextDepth,
                            String&& text) override;

  private:
    bool logged;
    bool evaluationFailed = false;
    // Set true if an attempt to evaluate() the context threw an exception. Once this happens we
    // permanently drop the context. This prevents re-evaluation, which could throw again, leading
    // to infinite recursion: evaluate() throwing an exception re-enters the exception callback
    // chain, which includes this very Context, so onException() would be invoked
    // recursively.
    bool evaluating = false;
    // Set true while evaluate() is running, so that we can detect the re-entrant case described
    // above and bail out.
    Maybe<Value> value;

    Maybe<Value> ensureInitialized();
    // Evaluates the context (if not already evaluated) and returns its value. Returns kj::none if
    // evaluating the context threw an exception, in which case the context should be dropped.
  };

  template <typename Func>
  class ContextImpl: public Context {
  public:
    inline ContextImpl(Func& func): func(func) {}
    KJ_DISALLOW_COPY_AND_MOVE(ContextImpl);

    Value evaluate() override {
      return func();
    }
  private:
    Func& func;
  };

  template <typename... Params>
  static String makeDescription(const char* macroArgs, Params&&... params);

private:
  static LogSeverity minSeverity;

  template <typename... Params>
  static KJ_NOINLINE KJ_FAIL_COLD_ void logImpl(const char* file, int line, LogSeverity severity,
                                                const char* macroArgs, Params... params);
  static void logInternal(const char* file, int line, LogSeverity severity, const char* macroArgs,
                          ArrayPtr<const Arg> args);
  static void logFailedCheckInternal(LogSeverity severity, const Exception& failure);
  // Logs `failure`, built by makeFailureWithParams() like the exception that fail() would throw,
  // so that KJ_EXPECT describes a failure exactly like KJ_ASSERT. Not on a hot path: it only runs
  // when a test fails.
  static String makeDescriptionInternal(const char* macroArgs, ArrayPtr<const Arg> args);

  static int getOsErrorNumber(bool nonblocking);
  // Get the error code of the last error (e.g. from errno).  Returns -1 on EINTR.

  template <typename S, typename Code, typename... Params>
  static Exception makeFailureWithParams(S site, Code code, Params&&... params);
  template <typename S, typename Condition, typename... Params>
  static Exception makeFailureWithParams(S site, ConditionFailed code, Condition&& condition,
                                         Params&&... params);
  // Builds the exception describing a failed check, stringifying `params`.

  static Exception makeFailure(const Site& site, Exception::Type type, ArrayPtr<const Arg> args,
                               const Comparison* comparison = nullptr);
  static Exception makeFailure(const Site& site, int osErrorNumber, ArrayPtr<const Arg> args);
#if _WIN32 || __CYGWIN__
  static Exception makeFailure(const Site& site, Win32Result osErrorNumber,
                               ArrayPtr<const Arg> args);
#endif
  // Stringifies `args` and builds the exception describing a failed check. `comparison` is the
  // check's condition, if it's a comparison (see ConditionFailed).

  static Exception makeFailure(PackedSite site, Exception::Type type, ArrayPtr<const Arg> args,
                               const Comparison* comparison = nullptr);
  static Exception makeFailure(PackedSite site, int osErrorNumber, ArrayPtr<const Arg> args);
#if _WIN32 || __CYGWIN__
  static Exception makeFailure(PackedSite site, Win32Result osErrorNumber,
                               ArrayPtr<const Arg> args);
#endif
  // Unpack the site and call the overloads above. Converting the PackedSite to a Site in the
  // templates instead would double the code of the failImpl() instantiations.
};

template <typename... Params>
inline void Debug::log(const char* file, int line, LogSeverity severity, const char* macroArgs,
                       Params&&... params) {
  if constexpr (sizeof...(Params) == 0) {
    logInternal(file, line, severity, macroArgs, nullptr);
  } else {
    logImpl<DebugParam<Params>...>(file, line, severity, macroArgs, kj::fwd<Params>(params)...);
  }
}

template <typename... Params>
void Debug::logImpl(const char* file, int line, LogSeverity severity, const char* macroArgs,
                    Params... params) {
  const Arg args[sizeof...(Params)] = { Arg::of(params)... };
  logInternal(file, line, severity, macroArgs, arrayPtr(args, sizeof...(Params)));
}

template <typename S, typename... Params>
void Debug::logFailedCheck(LogSeverity severity, S site, Params&&... params) {
  if (shouldLog(severity)) {
    logFailedCheckInternal(severity, makeFailureWithParams(site, ConditionFailed(), params...));
  }
}

template <typename S, typename Code, typename... Params>
Exception Debug::makeFailureWithParams(S site, Code code, Params&&... params) {
  if constexpr (sizeof...(Params) == 0) {
    return makeFailure(site, code, nullptr);
  } else {
    const Arg args[sizeof...(Params)] = { Arg::of(params)... };
    return makeFailure(site, code, arrayPtr(args, sizeof...(Params)));
  }
}

template <typename S, typename Condition, typename... Params>
Exception Debug::makeFailureWithParams(S site, ConditionFailed code, Condition&& condition,
                                       Params&&... params) {
  if constexpr (debugIsComparison<Decay<Condition>>) {
    const Comparison comparison = debugComparison(condition);
    if constexpr (sizeof...(Params) == 0) {
      return makeFailure(site, Exception::Type::FAILED, nullptr, &comparison);
    } else {
      const Arg args[sizeof...(Params)] = { Arg::of(params)... };
      return makeFailure(site, Exception::Type::FAILED, arrayPtr(args, sizeof...(Params)),
                         &comparison);
    }
  } else {
    return makeFailureWithParams(site, Exception::Type::FAILED, params...);
  }
}

#if KJ_FAIL_BY_VALUE_

template <typename S, typename Code, typename... Params>
inline void Debug::fail(S site, Code code, Params&&... params) {
  failImpl<S, Code, DebugParam<Params>...>(site, code, kj::fwd<Params>(params)...);
}

template <typename S, typename Condition, typename... Params>
inline void Debug::fail(S site, ConditionFailed code, Condition&& condition, Params&&... params) {
  if constexpr (debugIsComparison<Decay<Condition>>) {
    failImpl<S, ConditionFailed, DebugConditionParam<Condition>, DebugParam<Params>...>(
        site, code, kj::fwd<Condition>(condition), kj::fwd<Params>(params)...);
  } else {
    fail(site, Exception::Type::FAILED, kj::fwd<Params>(params)...);
  }
}

template <typename S, typename Code, typename... Params>
inline void Debug::failUnlessUnwinding(S site, Code code, Params&&... params) {
  failUnlessUnwindingImpl<S, Code, DebugParam<Params>...>(site, code, kj::fwd<Params>(params)...);
}

template <typename S, typename Condition, typename... Params>
inline void Debug::failUnlessUnwinding(S site, ConditionFailed code, Condition&& condition,
                                       Params&&... params) {
  if constexpr (debugIsComparison<Decay<Condition>>) {
    failUnlessUnwindingImpl<S, ConditionFailed, DebugConditionParam<Condition>,
                            DebugParam<Params>...>(
        site, code, kj::fwd<Condition>(condition), kj::fwd<Params>(params)...);
  } else {
    failUnlessUnwinding(site, Exception::Type::FAILED, kj::fwd<Params>(params)...);
  }
}

template <typename S, typename Code, typename... Params>
void Debug::failImpl(S site, Code code, Params... params) {
  // ignoreCount = 1 to skip this frame, so the trace starts at the failed check.
  throwException(makeFailureWithParams(site, code, params...), 1);
}

template <typename S, typename Code, typename... Params>
void Debug::failUnlessUnwindingImpl(S site, Code code, Params... params) {
  // ignoreCount = 1 to skip this frame, so the trace starts at the failed check.
  throwExceptionUnlessUnwinding(makeFailureWithParams(site, code, params...), 1);
}

#else  // KJ_FAIL_BY_VALUE_

// The same as failImpl() and failUnlessUnwindingImpl(), but called by the macros directly, taking
// the arguments by reference: on MSVC, a wrapper would add a frame to the stack trace.

template <typename S, typename Code, typename... Params>
void Debug::fail(S site, Code code, Params&&... params) {
  // ignoreCount = 1 to skip this frame, so the trace starts at the failed check.
  throwException(makeFailureWithParams(site, code, params...), 1);
}

template <typename S, typename Code, typename... Params>
void Debug::failUnlessUnwinding(S site, Code code, Params&&... params) {
  // ignoreCount = 1 to skip this frame, so the trace starts at the failed check.
  throwExceptionUnlessUnwinding(makeFailureWithParams(site, code, params...), 1);
}

#endif  // KJ_FAIL_BY_VALUE_, else

#if _WIN32 || __CYGWIN__
inline Debug::Win32Result Debug::win32Call(int boolean) {
  return boolean ? Win32Result(0) : Win32Result(getWin32ErrorCode());
}
inline Debug::Win32Result Debug::win32Call(void* handle) {
  // Assume null and INVALID_HANDLE_VALUE mean failure.
  return win32Call(handle != nullptr && handle != (void*)-1);
}
inline Debug::Win32Result Debug::winsockCall(int result) {
  // Expect a return value of SOCKET_ERROR means failure.
  return win32Call(result != -1);
}
#endif

template <typename Call>
Debug::SyscallResult Debug::syscall(Call&& call, bool nonblocking) {
  while (call() < 0) {
    int errorNum = getOsErrorNumber(nonblocking);
    // getOsErrorNumber() returns -1 to indicate EINTR.
    // Also, if nonblocking is true, then it returns 0 on EAGAIN, which will then be treated as a
    // non-error.
    if (errorNum != -1) {
      return SyscallResult(errorNum);
    }
  }
  return SyscallResult(0);
}

template <typename Call>
int Debug::syscallError(Call&& call, bool nonblocking) {
  while (call() < 0) {
    int errorNum = getOsErrorNumber(nonblocking);
    // getOsErrorNumber() returns -1 to indicate EINTR.
    // Also, if nonblocking is true, then it returns 0 on EAGAIN, which will then be treated as a
    // non-error.
    if (errorNum != -1) {
      return errorNum;
    }
  }
  return 0;
}

template <typename... Params>
String Debug::makeDescription(const char* macroArgs, Params&&... params) {
  const Arg args[sizeof...(Params)] = { Arg::of(params)... };
  return makeDescriptionInternal(macroArgs, arrayPtr(args, sizeof...(Params)));
}

template <>
inline String Debug::makeDescription<>(const char* macroArgs) {
  return makeDescriptionInternal(macroArgs, nullptr);
}

// =======================================================================================
// Magic Asserts!
//
// When KJ_ASSERT(foo == bar) fails, `foo` and `bar`'s actual values will be stringified in the
// error message. How does it work? We use template magic and operator precedence. The assertion
// actually evaluates something like this:
//
//     if (auto _kjCondition = kj::_::MAGIC_ASSERT << foo == bar)
//
// `<<` has operator precedence slightly above `==`, so `kj::_::MAGIC_ASSERT << foo` gets evaluated
// first. This wraps `foo` in a little wrapper that captures the comparison operators and keeps
// enough information around to be able to stringify the left and right sides of the comparison
// independently. As always, the stringification only actually occurs if the assert fails.
//
// You might ask why we use operator `<<` and not e.g. operator `<=`, since operators of the same
// precedence are evaluated left-to-right. The answer is that some compilers trigger all sorts of
// warnings when you seem to be using a comparison as the input to another comparison. The
// particular warning GCC produces is its general "-Wparentheses" warning which is broadly useful,
// so we don't want to disable it. `<<` also produces some warnings, but only on Clang and the
// specific warning is one we're comfortable disabling (see below). This does mean that we have to
// explicitly overload `operator<<` ourselves to make sure using it in an assert still works.
//
// You might also ask, if we're using operator `<<` anyway, why not start it from the right, in
// which case it would bind after computing any `<<` operators that were actually in the user's
// code? I tried this, but it resulted in a somewhat broader warning from clang that I felt worse
// about disabling (a warning about `<<` precedence not applying specifically to overloads) and
// also created ambiguous overload errors in the KJ units code.

#if __clang__
// We intentionally overload operator << for the specific purpose of evaluating it before
// evaluating comparison expressions, so stop Clang from warning about it. Unfortunately this means
// eliminating a warning that would otherwise be useful for people using iostreams... sorry.
#pragma GCC diagnostic ignored "-Woverloaded-shift-op-parentheses"
#endif

template <typename T>
struct DebugExpression;

template <typename T, typename = decltype(toCharSequence(instance<T&>()))>
inline auto tryToCharSequence(T* value) { return kj::toCharSequence(*value); }
inline StringPtr tryToCharSequence(...) { return "(can't stringify)"_kj; }
// SFINAE to stringify a value if and only if it can be stringified.

template <typename T>
KJ_FAIL_COLD_ String stringifyDebugOperand(const void* value) {
  return _::concat(tryToCharSequence(const_cast<T*>(static_cast<const T*>(value))));
}

template <typename T>
Debug::Arg debugOperand(T* value) { return { value, &stringifyDebugOperand<T> }; }
// An operand of a comparison in a check, stringified by the function shared by all operands of
// type `T`.

String stringifyDebugComparison(const Debug::Comparison& comparison);
// Stringifies a comparison in a failed check, e.g. "1 == 2".

#define KJ_DEBUG_COMPARISONS_(X) X(Eq, ==) X(Ne, !=) X(Le, <=) X(Ge, >=) X(Lt, <) X(Gt, >)
// The comparison operators that DebugExpression captures, and the names of their DebugOp types.

#define KJ_DEBUG_OPERATOR_(NAME, OP) \
  struct DebugOp##NAME { static StringPtr op() { return " " #OP " "_kj; } };
KJ_DEBUG_COMPARISONS_(KJ_DEBUG_OPERATOR_)
#undef KJ_DEBUG_OPERATOR_

template <typename Left, typename Right, typename Op>
struct DebugComparison {
  Left left;
  Right right;
  bool result;

  inline operator bool() const { return KJ_LIKELY(result); }

  template <typename T> inline void operator&(T&& other) = delete;
  template <typename T> inline void operator^(T&& other) = delete;
  template <typename T> inline void operator|(T&& other) = delete;
};

template <typename Left, typename Right, typename Op>
Debug::Comparison debugComparison(DebugComparison<Left, Right, Op>& cmp) {
  return { debugOperand(__builtin_addressof(cmp.left)), Op::op(),
           debugOperand(__builtin_addressof(cmp.right)) };
}

template <typename Left, typename Right, typename Op>
String KJ_STRINGIFY(DebugComparison<Left, Right, Op>& cmp) {
  // For a comparison that is an ordinary argument rather than a check's condition, e.g. one that
  // a macro defined outside of KJ passes to KJ_LOG.
  return stringifyDebugComparison(debugComparison(cmp));
}

#if KJ_FAIL_BY_VALUE_

template <typename T, bool = debugByValue<T, T&>>
struct DebugOperandByValue_ { static constexpr bool value = false; };
template <typename T>
struct DebugOperandByValue_<T, true> {
  static constexpr bool value = sizeof(Decay<T>) <= sizeof(void*);
};
template <typename T, bool = DebugOperandByValue_<T>::value>
struct DebugOperand_ { typedef T& Type; };
template <typename T>
struct DebugOperand_<T, true> { typedef Decay<T> Type; };
template <typename T>
using DebugOperand = typename DebugOperand_<T>::Type;
// How DebugComparisonOperands holds an operand of type `T`: cheap ones (see debugByValue; they
// are copied from the DebugComparison's member, a `T&`) that fit in a register by value,
// everything else by reference to the operand in the DebugComparison. So
// DebugComparisonOperands always fits in two registers: a bigger one would be passed on the
// stack, which costs stack space in the function containing the check.

template <typename Left, typename Right, typename Op>
struct DebugComparisonOperands {
  // The operands of a failed DebugComparison, as passed to Debug::failImpl(). Doesn't refer to
  // the DebugComparison itself, so that it can stay in registers when the check passes.

  Left left;
  Right right;

  template <typename L, typename R>
  DebugComparisonOperands(DebugComparison<L, R, Op>& cmp): left(cmp.left), right(cmp.right) {}
};

template <typename Left, typename Right, typename Op>
Debug::Comparison debugComparison(DebugComparisonOperands<Left, Right, Op>& cmp) {
  return { debugOperand(__builtin_addressof(cmp.left)), Op::op(),
           debugOperand(__builtin_addressof(cmp.right)) };
}

template <typename Left, typename Right, typename Op>
struct DebugConditionParam_<DebugComparison<Left, Right, Op>&> {
  typedef DebugComparisonOperands<DebugOperand<Left>, DebugOperand<Right>, Op> Type;
};

#endif  // KJ_FAIL_BY_VALUE_

template <typename T>
struct DebugExpression {
  DebugExpression(T&& value): value(kj::fwd<T>(value)) {}
  T value;

  // Handle comparison operations by constructing a DebugComparison value.
#define DEFINE_OPERATOR(NAME, OP) \
  template <typename U> \
  DebugComparison<T, U, DebugOp##NAME> operator OP(U&& other) { \
    bool result = value OP other; \
    return { kj::fwd<T>(value), kj::fwd<U>(other), result }; \
  }
  KJ_DEBUG_COMPARISONS_(DEFINE_OPERATOR)
#undef DEFINE_OPERATOR

  // Handle binary operators that have equal or lower precedence than comparisons by performing
  // the operation and wrapping the result.
#define DEFINE_OPERATOR(OP) \
  template <typename U> inline auto operator OP(U&& other) { \
    return DebugExpression<decltype(kj::fwd<T>(value) OP kj::fwd<U>(other))>(\
        kj::fwd<T>(value) OP kj::fwd<U>(other)); \
  }
  DEFINE_OPERATOR(<<);
  DEFINE_OPERATOR(>>);
  DEFINE_OPERATOR(&);
  DEFINE_OPERATOR(^);
  DEFINE_OPERATOR(|);
#undef DEFINE_OPERATOR

  inline operator bool() {
    // No comparison performed, we're just asserting the expression is truthy. This also covers
    // the case of the logic operators && and || -- we cannot overload those because doing so would
    // break short-circuiting behavior.
    return value;
  }
};

struct DebugExpressionStart {
  template <typename T>
  DebugExpression<T> operator<<(T&& value) const {
    return DebugExpression<T>(kj::fwd<T>(value));
  }
};
static constexpr DebugExpressionStart MAGIC_ASSERT;

#undef KJ_DEBUG_COMPARISONS_

}  // namespace _ (private)
}  // namespace kj

#undef KJ_FAIL_BY_VALUE_
#undef KJ_FAIL_INLINE_
#undef KJ_FAIL_COLD_

KJ_END_HEADER
