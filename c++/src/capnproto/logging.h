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

// This file declares convenient macros for debug logging and error handling.  The macros make
// it excessively easy to extract useful context information from code.  Example:
//
//     CHECK(a == b, a, b, "a and b must be the same.");
//
// On failure, this will throw an exception whose description looks like:
//
//     myfile.c++:43: bug in code: expected a == b; a = 14; b = 72; a and b must be the same.
//
// As you can see, all arguments after the first provide additional context.
//
// The macros available are:
//
// * `LOG(severity, ...)`:  Just writes a log message, to stderr by default (but you can intercept
//   messages by implementing an ExceptionCallback).  `severity` is `INFO`, `WARNING`, `ERROR`, or
//   `FATAL`.  If the severity is not higher than the global logging threshold, nothing will be
//   written and in fact the log message won't even be evaluated.
//
// * `CHECK(condition, ...)`:  Throws an exception if `condition` is false, or aborts if exceptions
//   are disabled.  This macro should be used to check for bugs in the surrounding code and its
//   dependencies, but NOT to check for invalid input.
//
// * `PRECOND(condition, ...)`:  Like `CHECK` but used to check preconditions -- i.e. to validate
//   parameters passed from a caller.  A failure indicates that the caller is buggy.
//
// * `RECOVERABLE_CHECK(condition, ...) { ... }`:  Like `CHECK` except that if exceptions are
//   disabled, instead of aborting, the following code block will be executed.  This block should
//   do whatever it can to fill in dummy values so that the code can continue executing, even if
//   this means the eventual output will be garbage.
//
// * `RECOVERABLE_PRECOND(condition, ...) { ... }`:  Like `RECOVERABLE_CHECK` and `PRECOND`.
//
// * `VALIDATE_INPUT(condition, ...) { ... }`:  Like `RECOVERABLE_PRECOND` but used to validate
//   input that may have come from the user or some other untrusted source.  Recoverability is
//   required in this case.
//
// * `SYSCALL(code, ...)`:  Executes `code` assuming it makes a system call.  A negative return
//   value is considered an error.  EINTR is handled by retrying.  Other errors are handled by
//   throwing an exception.  The macro also returns the call's result.  For example, the following
//   calls `open()` and includes the file name in any error message:
//
//       int fd = SYSCALL(open(filename, O_RDONLY), filename);
//
// * `RECOVERABLE_SYSCALL(code, ...) { ... }`:  Like `RECOVERABLE_CHECK` and `SYSCALL`.  Note that
//   unfortunately this macro cannot return a value since it implements control flow, but you can
//   assign to a variable *inside* the parameter instead:
//
//       int fd;
//       RECOVERABLE_SYSCALL(fd = open(filename, O_RDONLY), filename) {
//         // Failed.  Open /dev/null instead.
//         fd = SYSCALL(open("/dev/null", O_RDONLY));
//       }
//
// * `CONTEXT(...)`:  Notes additional contextual information relevant to any exceptions thrown
//   from within the current scope.  That is, until control exits the block in which CONTEXT()
//   is used, if any exception is generated, it will contain the given information in its context
//   chain.  This is helpful because it can otherwise be very difficult to come up with error
//   messages that make sense within low-level helper code.  Note that the parameters to CONTEXT()
//   are only evaluated if an exception is thrown.  This means that any variables used must remain
//   valid until the end of the scope.
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
//   executed in debug mode.  When `NDEBUG` is defined, these macros expand to nothing.

#ifndef CAPNPROTO_LOGGING_H_
#define CAPNPROTO_LOGGING_H_

#ifndef CAPNPROTO_PRIVATE
#error "This header is only meant to be included by Cap'n Proto's own source code."
#endif

#include "util.h"
#include "exception.h"

namespace capnproto {

class Log {
public:
  enum class Severity {
    INFO,      // Information useful for debugging.  No problem detected.
    WARNING,   // A problem was detected but execution can continue with correct output.
    ERROR,     // Something is wrong, but execution can continue with garbage output.
    FATAL      // Something went wrong, and execution cannot continue.

    // Make sure to update the stringifier if you add a new severity level.
  };

  static inline bool shouldLog(Severity severity) { return severity >= minSeverity; }
  // Returns whether messages of the given severity should be logged.

  static inline void setLogLevel(Severity severity) { minSeverity = severity; }
  // Set the minimum message severity which will be logged.

  template <typename... Params>
  static void log(const char* file, int line, Severity severity, const char* macroArgs,
                  Params&&... params);

  template <typename... Params>
  static void recoverableFault(const char* file, int line, Exception::Nature nature,
                               const char* condition, const char* macroArgs, Params&&... params);

  template <typename... Params>
  static void fatalFault(const char* file, int line, Exception::Nature nature,
                         const char* condition, const char* macroArgs, Params&&... params)
                         CAPNPROTO_NORETURN;

  template <typename Call, typename... Params>
  static bool recoverableSyscall(Call&& call, const char* file, int line, const char* callText,
                                 const char* macroArgs, Params&&... params);

  template <typename Call, typename... Params>
  static auto syscall(Call&& call, const char* file, int line, const char* callText,
                      const char* macroArgs, Params&&... params) -> decltype(call());

  template <typename... Params>
  static void reportFailedRecoverableSyscall(
      int errorNumber, const char* file, int line, const char* callText,
      const char* macroArgs, Params&&... params);

  template <typename... Params>
  static void reportFailedSyscall(
      int errorNumber, const char* file, int line, const char* callText,
      const char* macroArgs, Params&&... params);

  class Context: public ExceptionCallback {
  public:
    Context();
    CAPNPROTO_DISALLOW_COPY(Context);
    virtual ~Context();
    virtual void addTo(Exception& exception) = 0;

    virtual void onRecoverableException(Exception&& exception) override;
    virtual void onFatalException(Exception&& exception) override;
    virtual void logMessage(ArrayPtr<const char> text) override;

  private:
    ExceptionCallback& next;
    ScopedRegistration registration;
  };

  template <typename Func>
  class ContextImpl: public Context {
  public:
    inline ContextImpl(Func& func): func(func) {}
    CAPNPROTO_DISALLOW_COPY(ContextImpl);

    void addTo(Exception& exception) override {
      func(exception);
    }
  private:
    Func& func;
  };

  template <typename... Params>
  static void addContextTo(Exception& exception, const char* file,
                           int line, const char* macroArgs, Params&&... params);

private:
  static Severity minSeverity;

  static void logInternal(const char* file, int line, Severity severity, const char* macroArgs,
                          ArrayPtr<Array<char>> argValues);
  static void recoverableFaultInternal(
      const char* file, int line, Exception::Nature nature,
      const char* condition, const char* macroArgs, ArrayPtr<Array<char>> argValues);
  static void fatalFaultInternal(
      const char* file, int line, Exception::Nature nature,
      const char* condition, const char* macroArgs, ArrayPtr<Array<char>> argValues)
      CAPNPROTO_NORETURN;
  static void recoverableFailedSyscallInternal(
      const char* file, int line, const char* call,
      int errorNumber, const char* macroArgs, ArrayPtr<Array<char>> argValues);
  static void fatalFailedSyscallInternal(
      const char* file, int line, const char* call,
      int errorNumber, const char* macroArgs, ArrayPtr<Array<char>> argValues)
      CAPNPROTO_NORETURN;
  static void addContextToInternal(Exception& exception, const char* file, int line,
                                   const char* macroArgs, ArrayPtr<Array<char>> argValues);

  static int getOsErrorNumber();
  // Get the error code of the last error (e.g. from errno).  Returns -1 on EINTR.
};

ArrayPtr<const char> operator*(const Stringifier&, Log::Severity severity);

#define LOG(severity, ...) \
  if (!::capnproto::Log::shouldLog(::capnproto::Log::Severity::severity)) {} else \
    ::capnproto::Log::log(__FILE__, __LINE__, ::capnproto::Log::Severity::severity, \
                          #__VA_ARGS__, __VA_ARGS__)

#define FAULT(nature, cond, ...) \
  if (CAPNPROTO_EXPECT_TRUE(cond)) {} else \
    ::capnproto::Log::fatalFault(__FILE__, __LINE__, \
        ::capnproto::Exception::Nature::nature, #cond, #__VA_ARGS__, ##__VA_ARGS__)

#define RECOVERABLE_FAULT(nature, cond, ...) \
  if (CAPNPROTO_EXPECT_TRUE(cond)) {} else \
    if (::capnproto::Log::recoverableFault(__FILE__, __LINE__, \
            ::capnproto::Exception::Nature::nature, #cond, #__VA_ARGS__, ##__VA_ARGS__), false) {} \
    else

#define CHECK(...) FAULT(LOCAL_BUG, __VA_ARGS__)
#define RECOVERABLE_CHECK(...) RECOVERABLE_FAULT(LOCAL_BUG, __VA_ARGS__)
#define PRECOND(...) FAULT(PRECONDITION, __VA_ARGS__)
#define RECOVERABLE_PRECOND(...) RECOVERABLE_FAULT(PRECONDITION, __VA_ARGS__)
#define VALIDATE_INPUT(...) RECOVERABLE_FAULT(INPUT, __VA_ARGS__)

#define FAIL_CHECK(...) CHECK(false, ##__VA_ARGS__)
#define FAIL_RECOVERABLE_CHECK(...) RECOVERABLE_CHECK(false, ##__VA_ARGS__)
#define FAIL_PRECOND(...) PRECOND(false, ##__VA_ARGS__)
#define FAIL_RECOVERABLE_PRECOND(...) RECOVERABLE_PRECOND(false, ##__VA_ARGS__)
#define FAIL_VALIDATE_INPUT(...) VALIDATE_INPUT(false, ##__VA_ARGS__)

#define SYSCALL(call, ...) \
  ::capnproto::Log::syscall( \
      [&](){return (call);}, __FILE__, __LINE__, #call, #__VA_ARGS__, ##__VA_ARGS__)

#define RECOVERABLE_SYSCALL(call, ...) \
  if (::capnproto::Log::recoverableSyscall( \
      [&](){return (call);}, __FILE__, __LINE__, #call, #__VA_ARGS__, ##__VA_ARGS__)) {} \
  else

#define FAIL_SYSCALL(code, errorNumber, ...) \
  do { \
    /* make sure to read error number before doing anything else that could change it */ \
    int _errorNumber = errorNumber; \
    ::capnproto::Log::reportFailedSyscall( \
        _errorNumber, __FILE__, __LINE__, #code, #__VA_ARGS__, ##__VA_ARGS__); \
  } while (false)

#define FAIL_RECOVERABLE_SYSCALL(code, errorNumber, ...) \
  do { \
    /* make sure to read error number before doing anything else that could change it */ \
    int _errorNumber = errorNumber; \
    ::capnproto::Log::reportFailedRecoverableSyscall( \
        _errorNumber, __FILE__, __LINE__, #code, #__VA_ARGS__, ##__VA_ARGS__); \
  } while (false)

#define CONTEXT(...) \
  auto _capnpContextFunc = [&](::capnproto::Exception& exception) { \
        return ::capnproto::Log::addContextTo(exception, \
            __FILE__, __LINE__, #__VA_ARGS__, ##__VA_ARGS__); \
      }; \
  ::capnproto::Log::ContextImpl<decltype(_capnpContextFunc)> _capnpContext(_capnpContextFunc)

#ifdef NDEBUG
#define DLOG(...) do {} while (false)
#define DCHECK(...) do {} while (false)
#define RECOVERABLE_DCHECK(...) do {} while (false)
#define DPRECOND(...) do {} while (false)
#define RECOVERABLE_DPRECOND(...) do {} while (false)
#else
#define DLOG LOG
#define DCHECK CHECK
#define RECOVERABLE_DCHECK RECOVERABLE_CHECK
#define DPRECOND PRECOND
#define RECOVERABLE_DPRECOND RECOVERABLE_PRECOND
#endif

template <typename... Params>
void Log::log(const char* file, int line, Severity severity, const char* macroArgs,
              Params&&... params) {
  Array<char> argValues[sizeof...(Params)] = {str(params)...};
  logInternal(file, line, severity, macroArgs, arrayPtr(argValues, sizeof...(Params)));
}

template <typename... Params>
void Log::recoverableFault(const char* file, int line, Exception::Nature nature,
                           const char* condition, const char* macroArgs, Params&&... params) {
  Array<char> argValues[sizeof...(Params)] = {str(params)...};
  recoverableFaultInternal(file, line, nature, condition, macroArgs,
                           arrayPtr(argValues, sizeof...(Params)));
}

template <typename... Params>
void Log::fatalFault(const char* file, int line, Exception::Nature nature,
                     const char* condition, const char* macroArgs, Params&&... params) {
  Array<char> argValues[sizeof...(Params)] = {str(params)...};
  fatalFaultInternal(file, line, nature, condition, macroArgs,
                     arrayPtr(argValues, sizeof...(Params)));
}

template <typename Call, typename... Params>
bool Log::recoverableSyscall(Call&& call, const char* file, int line, const char* callText,
                             const char* macroArgs, Params&&... params) {
  int result;
  while ((result = call()) < 0) {
    int errorNum = getOsErrorNumber();
    // getOsErrorNumber() returns -1 to indicate EINTR
    if (errorNum != -1) {
      Array<char> argValues[sizeof...(Params)] = {str(params)...};
      recoverableFailedSyscallInternal(file, line, callText, errorNum,
                                       macroArgs, arrayPtr(argValues, sizeof...(Params)));
      return false;
    }
  }
  return true;
}

#ifndef __CDT_PARSER__  // Eclipse dislikes the late return spec.
template <typename Call, typename... Params>
auto Log::syscall(Call&& call, const char* file, int line, const char* callText,
                  const char* macroArgs, Params&&... params) -> decltype(call()) {
  decltype(call()) result;
  while ((result = call()) < 0) {
    int errorNum = getOsErrorNumber();
    // getOsErrorNumber() returns -1 to indicate EINTR
    if (errorNum != -1) {
      Array<char> argValues[sizeof...(Params)] = {str(params)...};
      fatalFailedSyscallInternal(file, line, callText, errorNum,
                                 macroArgs, arrayPtr(argValues, sizeof...(Params)));
    }
  }
  return result;
}
#endif

template <typename... Params>
void Log::reportFailedRecoverableSyscall(
    int errorNumber, const char* file, int line, const char* callText,
    const char* macroArgs, Params&&... params) {
  Array<char> argValues[sizeof...(Params)] = {str(params)...};
  recoverableFailedSyscallInternal(file, line, callText, errorNumber, macroArgs,
                                   arrayPtr(argValues, sizeof...(Params)));
}

template <typename... Params>
void Log::reportFailedSyscall(
    int errorNumber, const char* file, int line, const char* callText,
    const char* macroArgs, Params&&... params) {
  Array<char> argValues[sizeof...(Params)] = {str(params)...};
  fatalFailedSyscallInternal(file, line, callText, errorNumber, macroArgs,
                             arrayPtr(argValues, sizeof...(Params)));
}

template <typename... Params>
void Log::addContextTo(Exception& exception, const char* file, int line,
                       const char* macroArgs, Params&&... params) {
  Array<char> argValues[sizeof...(Params)] = {str(params)...};
  addContextToInternal(exception, file, line, macroArgs, arrayPtr(argValues, sizeof...(Params)));
}

}  // namespace capnproto

#endif  // CAPNPROTO_LOGGING_H_
