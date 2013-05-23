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

#define CAPNPROTO_PRIVATE
#include "logging.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

namespace capnproto {

Log::Severity Log::minSeverity = Log::Severity::INFO;

ArrayPtr<const char> operator*(const Stringifier&, Log::Severity severity) {
  static const char* SEVERITY_STRINGS[] = {
    "info",
    "warning",
    "error",
    "fatal"
  };

  const char* s = SEVERITY_STRINGS[static_cast<uint>(severity)];
  return arrayPtr(s, strlen(s));
}

namespace {

enum DescriptionStyle {
  LOG,
  ASSERTION,
  SYSCALL
};

static Array<char> makeDescription(DescriptionStyle style, const char* code, int errorNumber,
                                   const char* macroArgs, ArrayPtr<Array<char>> argValues) {
  ArrayPtr<const char> argNames[argValues.size()];

  if (argValues.size() > 0) {
    size_t index = 0;
    const char* start = macroArgs;
    while (isspace(*start)) ++start;
    const char* pos = start;
    uint depth = 0;
    bool quoted = false;
    while (char c = *pos++) {
      if (quoted) {
        if (c == '\\' && *pos != '\0') {
          ++pos;
        } else if (c == '\"') {
          quoted = false;
        }
      } else {
        if (c == '(') {
          ++depth;
        } else if (c == ')') {
          --depth;
        } else if (c == '\"') {
          quoted = true;
        } else if (c == ',' && depth == 0) {
          if (index < argValues.size()) {
            argNames[index] = arrayPtr(start, pos - 1);
          }
          ++index;
          while (isspace(*pos)) ++pos;
          start = pos;
        }
      }
    }
    if (index < argValues.size()) {
      argNames[index] = arrayPtr(start, pos - 1);
    }
    ++index;

    if (index != argValues.size()) {
      getExceptionCallback().logMessage(
          str(__FILE__, ":", __LINE__, ": Failed to parse logging macro args into ",
              argValues.size(), " names: ", macroArgs, '\n'));
    }
  }

  if (style == SYSCALL) {
    // Strip off leading "foo = " from code, since callers will sometimes write things like:
    //   ssize_t n;
    //   RECOVERABLE_SYSCALL(n = read(fd, buffer, sizeof(buffer))) { return ""; }
    //   return std::string(buffer, n);
    const char* equalsPos = strchr(code, '=');
    if (equalsPos != nullptr && equalsPos[1] != '=') {
      code = equalsPos + 1;
      while (isspace(*code)) ++code;
    }
  }

  {
    ArrayPtr<const char> expected = arrayPtr("expected ");
    ArrayPtr<const char> codeArray = style == LOG ? nullptr : arrayPtr(code);
    ArrayPtr<const char> sep = arrayPtr(" = ");
    ArrayPtr<const char> delim = arrayPtr("; ");
    ArrayPtr<const char> colon = arrayPtr(": ");

    if (style == ASSERTION && strcmp(code, "false") == 0) {
      // Don't print "expected false", that's silly.
      style = LOG;
    }

    ArrayPtr<const char> sysErrorArray;
#if __USE_GNU
    char buffer[256];
    if (style == SYSCALL) {
      sysErrorArray = arrayPtr(strerror_r(errorNumber, buffer, sizeof(buffer)));
    }
#else
    // TODO(port):  Other unixes should have strerror_r but it may have a different signature.
    //   Port for thread-safety.
    sysErrorArray = arrayPtr(strerror(errorNumber));
#endif

    size_t totalSize = 0;
    switch (style) {
      case LOG:
        break;
      case ASSERTION:
        totalSize += expected.size() + codeArray.size();
        break;
      case SYSCALL:
        totalSize += codeArray.size() + colon.size() + sysErrorArray.size();
        break;
    }

    for (size_t i = 0; i < argValues.size(); i++) {
      if (i > 0 || style != LOG) {
        totalSize += delim.size();
      }
      if (argNames[i].size() > 0 && argNames[i][0] != '\"') {
        totalSize += argNames[i].size() + sep.size();
      }
      totalSize += argValues[i].size();
    }

    ArrayBuilder<char> result(totalSize);

    switch (style) {
      case LOG:
        break;
      case ASSERTION:
        result.addAll(expected);
        result.addAll(codeArray);
        break;
      case SYSCALL:
        result.addAll(codeArray);
        result.addAll(colon);
        result.addAll(sysErrorArray);
        break;
    }
    for (size_t i = 0; i < argValues.size(); i++) {
      if (i > 0 || style != LOG) {
        result.addAll(delim);
      }
      if (argNames[i].size() > 0 && argNames[i][0] != '\"') {
        result.addAll(argNames[i]);
        result.addAll(sep);
      }
      result.addAll(argValues[i]);
    }

    return result.finish();
  }
}

}  // namespace

void Log::logInternal(const char* file, int line, Severity severity, const char* macroArgs,
                      ArrayPtr<Array<char>> argValues) {
  getExceptionCallback().logMessage(
      str(severity, ": ", file, ":", line, ": ",
          makeDescription(LOG, nullptr, 0, macroArgs, argValues), '\n'));
}

void Log::recoverableFaultInternal(
    const char* file, int line, Exception::Nature nature,
    const char* condition, const char* macroArgs, ArrayPtr<Array<char>> argValues) {
  getExceptionCallback().onRecoverableException(
      Exception(nature, Exception::Durability::PERMANENT, file, line,
                makeDescription(ASSERTION, condition, 0, macroArgs, argValues)));
}

void Log::fatalFaultInternal(
    const char* file, int line, Exception::Nature nature,
    const char* condition, const char* macroArgs, ArrayPtr<Array<char>> argValues) {
  getExceptionCallback().onFatalException(
      Exception(nature, Exception::Durability::PERMANENT, file, line,
                makeDescription(ASSERTION, condition, 0, macroArgs, argValues)));
  abort();
}

void Log::recoverableFailedSyscallInternal(
    const char* file, int line, const char* call,
    int errorNumber, const char* macroArgs, ArrayPtr<Array<char>> argValues) {
  getExceptionCallback().onRecoverableException(
      Exception(Exception::Nature::OS_ERROR, Exception::Durability::PERMANENT, file, line,
                makeDescription(SYSCALL, call, errorNumber, macroArgs, argValues)));
}

void Log::fatalFailedSyscallInternal(
    const char* file, int line, const char* call,
    int errorNumber, const char* macroArgs, ArrayPtr<Array<char>> argValues) {
  getExceptionCallback().onFatalException(
      Exception(Exception::Nature::OS_ERROR, Exception::Durability::PERMANENT, file, line,
                makeDescription(SYSCALL, call, errorNumber, macroArgs, argValues)));
  abort();
}

void Log::addContextToInternal(Exception& exception, const char* file, int line,
                               const char* macroArgs, ArrayPtr<Array<char>> argValues) {
  exception.wrapContext(file, line, makeDescription(LOG, nullptr, 0, macroArgs, argValues));
}

int Log::getOsErrorNumber() {
  int result = errno;
  return result == EINTR ? -1 : result;
}

Log::Context::Context(): next(getExceptionCallback()), registration(*this) {}
Log::Context::~Context() {}

void Log::Context::onRecoverableException(Exception&& exception) {
  addTo(exception);
  next.onRecoverableException(capnproto::move(exception));
}
void Log::Context::onFatalException(Exception&& exception) {
  addTo(exception);
  next.onFatalException(capnproto::move(exception));
}
void Log::Context::logMessage(ArrayPtr<const char> text) {
  // TODO(someday):  We could do something like log the context and then indent all log messages
  //   written until the end of the context.
  next.logMessage(text);
}

}  // namespace capnproto
