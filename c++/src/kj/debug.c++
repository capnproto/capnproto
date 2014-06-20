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

#include "debug.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

namespace kj {
namespace _ {  // private

Debug::Severity Debug::minSeverity = Debug::Severity::WARNING;

ArrayPtr<const char> KJ_STRINGIFY(Debug::Severity severity) {
  static const char* SEVERITY_STRINGS[] = {
    "info",
    "warning",
    "error",
    "fatal",
    "debug"
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

static String makeDescription(DescriptionStyle style, const char* code, int errorNumber,
                              const char* macroArgs, ArrayPtr<String> argValues) {
  KJ_STACK_ARRAY(ArrayPtr<const char>, argNames, argValues.size(), 8, 64);

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
      getExceptionCallback().logMessage(__FILE__, __LINE__, 0,
          str("Failed to parse logging macro args into ",
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

  if (style == ASSERTION && code == nullptr) {
    style = LOG;
  }

  {
    StringPtr expected = "expected ";
    StringPtr codeArray = style == LOG ? nullptr : StringPtr(code);
    StringPtr sep = " = ";
    StringPtr delim = "; ";
    StringPtr colon = ": ";

    StringPtr sysErrorArray;
#if __USE_GNU
    char buffer[256];
    if (style == SYSCALL) {
      sysErrorArray = strerror_r(errorNumber, buffer, sizeof(buffer));
    }
#else
    char buffer[256];
    if (style == SYSCALL) {
      strerror_r(errorNumber, buffer, sizeof(buffer));
      sysErrorArray = buffer;
    }
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

    String result = heapString(totalSize);
    char* pos = result.begin();

    switch (style) {
      case LOG:
        break;
      case ASSERTION:
        pos = _::fill(pos, expected, codeArray);
        break;
      case SYSCALL:
        pos = _::fill(pos, codeArray, colon, sysErrorArray);
        break;
    }
    for (size_t i = 0; i < argValues.size(); i++) {
      if (i > 0 || style != LOG) {
        pos = _::fill(pos, delim);
      }
      if (argNames[i].size() > 0 && argNames[i][0] != '\"') {
        pos = _::fill(pos, argNames[i], sep);
      }
      pos = _::fill(pos, argValues[i]);
    }

    return result;
  }
}

}  // namespace

void Debug::logInternal(const char* file, int line, Severity severity, const char* macroArgs,
                        ArrayPtr<String> argValues) {
  getExceptionCallback().logMessage(file, line, 0,
      str(severity, ": ", makeDescription(LOG, nullptr, 0, macroArgs, argValues), '\n'));
}

Debug::Fault::~Fault() noexcept(false) {
  if (exception != nullptr) {
    Exception copy = mv(*exception);
    delete exception;
    throwRecoverableException(mv(copy));
  }
}

void Debug::Fault::fatal() {
  Exception copy = mv(*exception);
  delete exception;
  exception = nullptr;
  throwFatalException(mv(copy));
  abort();
}

void Debug::Fault::init(
    const char* file, int line, Exception::Nature nature, int errorNumber,
    const char* condition, const char* macroArgs, ArrayPtr<String> argValues) {
  exception = new Exception(nature, Exception::Durability::PERMANENT, file, line,
      makeDescription(nature == Exception::Nature::OS_ERROR ? SYSCALL : ASSERTION,
                      condition, errorNumber, macroArgs, argValues));
}

String Debug::makeContextDescriptionInternal(const char* macroArgs, ArrayPtr<String> argValues) {
  return makeDescription(LOG, nullptr, 0, macroArgs, argValues);
}

int Debug::getOsErrorNumber(bool nonblocking) {
  int result = errno;

  // On many systems, EAGAIN and EWOULDBLOCK have the same value, but this is not strictly required
  // by POSIX, so we need to check both.
  return result == EINTR ? -1
       : nonblocking && (result == EAGAIN || result == EWOULDBLOCK) ? 0
       : result;
}

Debug::Context::Context(): logged(false) {}
Debug::Context::~Context() noexcept(false) {}

Debug::Context::Value Debug::Context::ensureInitialized() {
  KJ_IF_MAYBE(v, value) {
    return Value(v->file, v->line, heapString(v->description));
  } else {
    Value result = evaluate();
    value = Value(result.file, result.line, heapString(result.description));
    return result;
  }
}

void Debug::Context::onRecoverableException(Exception&& exception) {
  Value v = ensureInitialized();
  exception.wrapContext(v.file, v.line, mv(v.description));
  next.onRecoverableException(kj::mv(exception));
}
void Debug::Context::onFatalException(Exception&& exception) {
  Value v = ensureInitialized();
  exception.wrapContext(v.file, v.line, mv(v.description));
  next.onFatalException(kj::mv(exception));
}
void Debug::Context::logMessage(const char* file, int line, int contextDepth, String&& text) {
  if (!logged) {
    Value v = ensureInitialized();
    next.logMessage(v.file, v.line, 0, str("context: ", mv(v.description), '\n'));
    logged = true;
  }

  next.logMessage(file, line, contextDepth + 1, mv(text));
}

}  // namespace _ (private)
}  // namespace kj
