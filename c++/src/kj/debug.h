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
// * `KJ_ASSERT(condition, ...)`:  Throws an exception if `condition` is false, or aborts if
//   exceptions are disabled.  This macro should be used to check for bugs in the surrounding code
//   and its dependencies, but NOT to check for invalid input.  The macro may be followed by a
//   brace-delimited code block; if so, the block will be executed in the case where the assertion
//   fails, before throwing the exception.  If control jumps out of the block (e.g. with "break",
//   "return", or "goto"), then the error is considered "recoverable" -- in this case, if
//   exceptions are disabled, execution will continue normally rather than aborting (but if
//   exceptions are enabled, an exception will still be thrown on exiting the block). A "break"
//   statement in particular will jump to the code immediately after the block (it does not break
//   any surrounding loop or switch).  Example:
//
//       KJ_ASSERT(value >= 0, "Value cannot be negative.", value) {
//         // Assertion failed.  Set value to zero to "recover".
//         value = 0;
//         // Don't abort if exceptions are disabled.  Continue normally.
//         // (Still throw an exception if they are enabled, though.)
//         break;
//       }
//       // When exceptions are disabled, we'll get here even if the assertion fails.
//       // Otherwise, we get here only if the assertion passes.
//
// * `KJ_REQUIRE(condition, ...)`:  Like `KJ_ASSERT` but used to check preconditions -- e.g. to
//   validate parameters passed from a caller.  A failure indicates that the caller is buggy.
//
// * `KJ_SYSCALL(code, ...)`:  Executes `code` assuming it makes a system call.  A negative result
//   is considered an error, with error code reported via `errno`.  EINTR is handled by retrying.
//   Other errors are handled by throwing an exception.  If you need to examine the return code,
//   assign it to a variable like so:
//
//       int fd;
//       KJ_SYSCALL(fd = open(filename, O_RDONLY), filename);
//
//   `KJ_SYSCALL` can be followed by a recovery block, just like `KJ_ASSERT`.
//
// * `KJ_NONBLOCKING_SYSCALL(code, ...)`:  Like KJ_SYSCALL, but will not throw an exception on
//   EAGAIN/EWOULDBLOCK.  The calling code should check the syscall's return value to see if it
//   indicates an error; in this case, it can assume the error was EAGAIN because any other error
//   would have caused an exception to be thrown.
//
// * `KJ_CONTEXT(...)`:  Notes additional contextual information relevant to any exceptions thrown
//   from within the current scope.  That is, until control exits the block in which KJ_CONTEXT()
//   is used, if any exception is generated, it will contain the given information in its context
//   chain.  This is helpful because it can otherwise be very difficult to come up with error
//   messages that make sense within low-level helper code.  Note that the parameters to
//   KJ_CONTEXT() are only evaluated if an exception is thrown.  This implies that any variables
//   used must remain valid until the end of the scope.
//
// Notes:
// * Do not write expressions with side-effects in the message content part of the macro, as the
//   message will not necessarily be evaluated.
// * For every macro `FOO` above except `LOG`, there is also a `FAIL_FOO` macro used to report
//   failures that already happened.  For the macros that check a boolean condition, `FAIL_FOO`
//   omits the first parameter and behaves like it was `false`.  `FAIL_SYSCALL` and
//   `FAIL_RECOVERABLE_SYSCALL` take a string and an OS error number as the first two parameters.
//   The string should be the name of the failed system call.
// * For every macro `FOO` above, there is a `DFOO` version (or `RECOVERABLE_DFOO`) which is only
//   executed in debug mode, i.e. when KJ_DEBUG is defined.  KJ_DEBUG is defined automatically
//   by common.h when compiling without optimization (unless NDEBUG is defined), but you can also
//   define it explicitly (e.g. -DKJ_DEBUG).  Generally, production builds should NOT use KJ_DEBUG
//   as it may enable expensive checks that are unlikely to fail.

#ifndef KJ_DEBUG_H_
#define KJ_DEBUG_H_

#include "string.h"
#include "exception.h"

namespace kj {

#define KJ_LOG(severity, ...) \
  if (!::kj::_::Debug::shouldLog(::kj::_::Debug::Severity::severity)) {} else \
    ::kj::_::Debug::log(__FILE__, __LINE__, ::kj::_::Debug::Severity::severity, \
                        #__VA_ARGS__, __VA_ARGS__)

#define KJ_DBG(...) KJ_LOG(DBG, ##__VA_ARGS__)

#define _kJ_FAULT(nature, cond, ...) \
  if (KJ_LIKELY(cond)) {} else \
    for (::kj::_::Debug::Fault f(__FILE__, __LINE__, ::kj::Exception::Nature::nature, 0, \
                                 #cond, #__VA_ARGS__, ##__VA_ARGS__);; f.fatal())

#define _kJ_FAIL_FAULT(nature, ...) \
  for (::kj::_::Debug::Fault f(__FILE__, __LINE__, ::kj::Exception::Nature::nature, 0, \
                               nullptr, #__VA_ARGS__, ##__VA_ARGS__);; f.fatal())

#define KJ_ASSERT(...) _kJ_FAULT(LOCAL_BUG, ##__VA_ARGS__)
#define KJ_REQUIRE(...) _kJ_FAULT(PRECONDITION, ##__VA_ARGS__)

#define KJ_FAIL_ASSERT(...) _kJ_FAIL_FAULT(LOCAL_BUG, ##__VA_ARGS__)
#define KJ_FAIL_REQUIRE(...) _kJ_FAIL_FAULT(PRECONDITION, ##__VA_ARGS__)

#define KJ_SYSCALL(call, ...) \
  if (auto _kjSyscallResult = ::kj::_::Debug::syscall([&](){return (call);}, false)) {} else \
    for (::kj::_::Debug::Fault f( \
             __FILE__, __LINE__, ::kj::Exception::Nature::OS_ERROR, \
             _kjSyscallResult.getErrorNumber(), #call, #__VA_ARGS__, ##__VA_ARGS__);; f.fatal())

#define KJ_NONBLOCKING_SYSCALL(call, ...) \
  if (auto _kjSyscallResult = ::kj::_::Debug::syscall([&](){return (call);}, true)) {} else \
    for (::kj::_::Debug::Fault f( \
             __FILE__, __LINE__, ::kj::Exception::Nature::OS_ERROR, \
             _kjSyscallResult.getErrorNumber(), #call, #__VA_ARGS__, ##__VA_ARGS__);; f.fatal())

#define KJ_FAIL_SYSCALL(code, errorNumber, ...) \
  for (::kj::_::Debug::Fault f( \
           __FILE__, __LINE__, ::kj::Exception::Nature::OS_ERROR, \
           errorNumber, code, #__VA_ARGS__, ##__VA_ARGS__);; f.fatal())

#define KJ_CONTEXT(...) \
  auto KJ_UNIQUE_NAME(_kjContextFunc) = [&]() -> ::kj::_::Debug::Context::Value { \
        return ::kj::_::Debug::Context::Value(__FILE__, __LINE__, \
            ::kj::_::Debug::makeContextDescription(#__VA_ARGS__, ##__VA_ARGS__)); \
      }; \
  ::kj::_::Debug::ContextImpl<decltype(KJ_UNIQUE_NAME(_kjContextFunc))> \
      KJ_UNIQUE_NAME(_kjContext)(KJ_UNIQUE_NAME(_kjContextFunc))

#define _kJ_NONNULL(nature, value, ...) \
  (*({ \
    auto _kj_result = ::kj::_::readMaybe(value); \
    if (KJ_UNLIKELY(!_kj_result)) { \
      ::kj::_::Debug::Fault(__FILE__, __LINE__, ::kj::Exception::Nature::nature, 0, \
                            #value " != nullptr", #__VA_ARGS__, ##__VA_ARGS__).fatal(); \
    } \
    _kj_result; \
  }))
#define KJ_ASSERT_NONNULL(value, ...) _kJ_NONNULL(LOCAL_BUG, value, ##__VA_ARGS__)
#define KJ_REQUIRE_NONNULL(value, ...) _kJ_NONNULL(PRECONDITION, value, ##__VA_ARGS__)

#ifdef KJ_DEBUG
#define KJ_DLOG LOG
#define KJ_DASSERT KJ_ASSERT
#define KJ_DREQUIRE KJ_REQUIRE
#else
#define KJ_DLOG(...) do {} while (false)
#define KJ_DASSERT(...) do {} while (false)
#define KJ_DREQUIRE(...) do {} while (false)
#endif

namespace _ {  // private

class Debug {
public:
  Debug() = delete;

  enum class Severity {
    INFO,      // Information describing what the code is up to, which users may request to see
               // with a flag like `--verbose`.  Does not indicate a problem.  Not printed by
               // default; you must call setLogLevel(INFO) to enable.
    WARNING,   // A problem was detected but execution can continue with correct output.
    ERROR,     // Something is wrong, but execution can continue with garbage output.
    FATAL,     // Something went wrong, and execution cannot continue.
    DBG        // Temporary debug logging.  See KJ_DBG.

    // Make sure to update the stringifier if you add a new severity level.
  };

  static inline bool shouldLog(Severity severity) { return severity >= minSeverity; }
  // Returns whether messages of the given severity should be logged.

  static inline void setLogLevel(Severity severity) { minSeverity = severity; }
  // Set the minimum message severity which will be logged.
  //
  // TODO(someday):  Expose publicly.

  template <typename... Params>
  static void log(const char* file, int line, Severity severity, const char* macroArgs,
                  Params&&... params);

  class Fault {
  public:
    template <typename... Params>
    Fault(const char* file, int line, Exception::Nature nature, int errorNumber,
          const char* condition, const char* macroArgs, Params&&... params);
    ~Fault() noexcept(false);

    void fatal() KJ_NORETURN;
    // Throw the exception.

  private:
    void init(const char* file, int line, Exception::Nature nature, int errorNumber,
              const char* condition, const char* macroArgs, ArrayPtr<String> argValues);

    Exception* exception;
  };

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

  class Context: public ExceptionCallback {
  public:
    Context();
    KJ_DISALLOW_COPY(Context);
    virtual ~Context() noexcept(false);

    struct Value {
      const char* file;
      int line;
      String description;

      inline Value(const char* file, int line, String&& description)
          : file(file), line(line), description(mv(description)) {}
    };

    virtual Value evaluate() = 0;

    virtual void onRecoverableException(Exception&& exception) override;
    virtual void onFatalException(Exception&& exception) override;
    virtual void logMessage(const char* file, int line, int contextDepth, String&& text) override;

  private:
    bool logged;
    Maybe<Value> value;

    Value ensureInitialized();
  };

  template <typename Func>
  class ContextImpl: public Context {
  public:
    inline ContextImpl(Func& func): func(func) {}
    KJ_DISALLOW_COPY(ContextImpl);

    Value evaluate() override {
      return func();
    }
  private:
    Func& func;
  };

  template <typename... Params>
  static String makeContextDescription(const char* macroArgs, Params&&... params);

private:
  static Severity minSeverity;

  static void logInternal(const char* file, int line, Severity severity, const char* macroArgs,
                          ArrayPtr<String> argValues);
  static String makeContextDescriptionInternal(const char* macroArgs, ArrayPtr<String> argValues);

  static int getOsErrorNumber(bool nonblocking);
  // Get the error code of the last error (e.g. from errno).  Returns -1 on EINTR.
};

ArrayPtr<const char> KJ_STRINGIFY(Debug::Severity severity);

template <typename... Params>
void Debug::log(const char* file, int line, Severity severity, const char* macroArgs,
                Params&&... params) {
  String argValues[sizeof...(Params)] = {str(params)...};
  logInternal(file, line, severity, macroArgs, arrayPtr(argValues, sizeof...(Params)));
}

template <typename... Params>
Debug::Fault::Fault(const char* file, int line, Exception::Nature nature, int errorNumber,
                    const char* condition, const char* macroArgs, Params&&... params)
    : exception(nullptr) {
  String argValues[sizeof...(Params)] = {str(params)...};
  init(file, line, nature, errorNumber, condition, macroArgs,
       arrayPtr(argValues, sizeof...(Params)));
}

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

template <typename... Params>
String Debug::makeContextDescription(const char* macroArgs, Params&&... params) {
  String argValues[sizeof...(Params)] = {str(params)...};
  return makeContextDescriptionInternal(macroArgs, arrayPtr(argValues, sizeof...(Params)));
}

}  // namespace _ (private)
}  // namespace kj

#endif  // KJ_DEBUG_H_
