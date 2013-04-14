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
  static inline void setLogLevel(Severity severity) { minSeverity = severity; }

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

}  // namespace capnproto

#endif  // CAPNPROTO_LOGGING_H_
