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

#if _WIN32 || __CYGWIN__
#include "win32-api-version.h"
#endif

#include "debug.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#if _WIN32 || __CYGWIN__
#if !__CYGWIN__
#define strerror_r(errno,buf,len) strerror_s(buf,len,errno)
#endif
#include <windows.h>
#include "windows-sanity.h"
#include "encoding.h"
#include <wchar.h>
#endif

namespace kj {
namespace _ {  // private

LogSeverity Debug::minSeverity = LogSeverity::WARNING;

namespace {

Exception::Type typeOfErrno(int error) {
  switch (error) {
#ifdef EDQUOT
    case EDQUOT:
#endif
#ifdef EMFILE
    case EMFILE:
#endif
#ifdef ENFILE
    case ENFILE:
#endif
#ifdef ENOBUFS
    case ENOBUFS:
#endif
#ifdef ENOLCK
    case ENOLCK:
#endif
#ifdef ENOMEM
    case ENOMEM:
#endif
#ifdef ENOSPC
    case ENOSPC:
#endif
#ifdef ETIMEDOUT
    case ETIMEDOUT:
#endif
#ifdef EUSERS
    case EUSERS:
#endif
      return Exception::Type::OVERLOADED;

#ifdef ENOTCONN
    case ENOTCONN:
#endif
#ifdef ECONNABORTED
    case ECONNABORTED:
#endif
#ifdef ECONNREFUSED
    case ECONNREFUSED:
#endif
#ifdef ECONNRESET
    case ECONNRESET:
#endif
#ifdef EHOSTDOWN
    case EHOSTDOWN:
#endif
#ifdef EHOSTUNREACH
    case EHOSTUNREACH:
#endif
#ifdef ENETDOWN
    case ENETDOWN:
#endif
#ifdef ENETRESET
    case ENETRESET:
#endif
#ifdef ENETUNREACH
    case ENETUNREACH:
#endif
#ifdef ENONET
    case ENONET:
#endif
#ifdef EPIPE
    case EPIPE:
#endif
      return Exception::Type::DISCONNECTED;

#ifdef ENOSYS
    case ENOSYS:
#endif
#ifdef ENOTSUP
    case ENOTSUP:
#endif
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
    case EOPNOTSUPP:
#endif
#ifdef ENOPROTOOPT
    case ENOPROTOOPT:
#endif
#ifdef ENOTSOCK
    // This is really saying "syscall not implemented for non-sockets".
    case ENOTSOCK:
#endif
      return Exception::Type::UNIMPLEMENTED;

    default:
      return Exception::Type::FAILED;
  }
}

#if _WIN32 || __CYGWIN__

Exception::Type typeOfWin32Error(DWORD error) {
  switch (error) {
    // TODO(someday): This needs more work.

    case WSAETIMEDOUT:
      return Exception::Type::OVERLOADED;

    case WSAENOTCONN:
    case WSAECONNABORTED:
    case WSAECONNREFUSED:
    case WSAECONNRESET:
    case WSAEHOSTDOWN:
    case WSAEHOSTUNREACH:
    case WSAENETDOWN:
    case WSAENETRESET:
    case WSAENETUNREACH:
    case WSAESHUTDOWN:
      return Exception::Type::DISCONNECTED;

    case WSAEOPNOTSUPP:
    case WSAENOPROTOOPT:
    case WSAENOTSOCK:    // This is really saying "syscall not implemented for non-sockets".
      return Exception::Type::UNIMPLEMENTED;

    default:
      return Exception::Type::FAILED;
  }
}

#endif  // _WIN32

enum DescriptionStyle {
  LOG,
  ASSERTION,
  SYSCALL
};

static String makeDescriptionImpl(DescriptionStyle style, const char* code, int errorNumber,
                                  const char* sysErrorString, const char* macroArgs,
                                  ArrayPtr<String> argValues) {
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
            argNames[index++] = arrayPtr(start, pos - 1);
          }
          while (isspace(*pos)) ++pos;
          start = pos;
          if (*pos == '\0') {
            // ignore trailing comma
            break;
          }
        }
      }
    }
    if (index < argValues.size()) {
      argNames[index++] = arrayPtr(start, pos - 1);
    }

    if (index != argValues.size()) {
      getExceptionCallback().logMessage(LogSeverity::ERROR, __FILE__, __LINE__, 0,
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
    StringPtr openBracket = " [";
    StringPtr closeBracket = "]";

    StringPtr sysErrorArray;
// On android before marshmallow only the posix version of stderror_r was
// available, even with __USE_GNU.
#if __USE_GNU && !(defined(__ANDROID_API__) && __ANDROID_API__ < 23)
    char buffer[256];
    if (style == SYSCALL) {
      if (sysErrorString == nullptr) {
        sysErrorArray = strerror_r(errorNumber, buffer, sizeof(buffer));
      } else {
        sysErrorArray = sysErrorString;
      }
    }
#else
    char buffer[256];
    if (style == SYSCALL) {
      if (sysErrorString == nullptr) {
        strerror_r(errorNumber, buffer, sizeof(buffer));
        sysErrorArray = buffer;
      } else {
        sysErrorArray = sysErrorString;
      }
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

    auto needsLabel = [](ArrayPtr<const char> &argName) -> bool {
      return (argName.size() > 0 && argName[0] != '\"' &&
          !(argName.size() >= 8 && memcmp(argName.begin(), "kj::str(", 8) == 0));
    };

    for (size_t i = 0; i < argValues.size(); i++) {
      if (argNames[i] == "_kjCondition"_kj) {
        // Special handling: don't output delimiter, we want to append this to the previous item,
        // in brackets. Also, if it's just "[false]" (meaning we didn't manage to extract a
        // comparison), don't add it at all.
        if (argValues[i] != "false") {
          totalSize += openBracket.size() + argValues[i].size() + closeBracket.size();
        }
        continue;
      }

      if (i > 0 || style != LOG) {
        totalSize += delim.size();
      }
      if (needsLabel(argNames[i])) {
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
      if (argNames[i] == "_kjCondition"_kj) {
        // Special handling: don't output delimiter, we want to append this to the previous item,
        // in brackets. Also, if it's just "[false]" (meaning we didn't manage to extract a
        // comparison), don't add it at all.
        if (argValues[i] != "false") {
          pos = _::fill(pos, openBracket, argValues[i], closeBracket);
        }
        continue;
      }

      if (i > 0 || style != LOG) {
        pos = _::fill(pos, delim);
      }
      if (needsLabel(argNames[i])) {
        pos = _::fill(pos, argNames[i], sep);
      }
      pos = _::fill(pos, argValues[i]);
    }

    return result;
  }
}

}  // namespace

void Debug::logInternal(const char* file, int line, LogSeverity severity, const char* macroArgs,
                        ArrayPtr<String> argValues) {
  getExceptionCallback().logMessage(severity, trimSourceFilename(file).cStr(), line, 0,
      makeDescriptionImpl(LOG, nullptr, 0, nullptr, macroArgs, argValues));
}

Debug::Fault::~Fault() noexcept(false) {
  if (exception != nullptr) {
    Exception copy = mv(*exception);
    delete exception;
    throwRecoverableException(mv(copy), 1);
  }
}

void Debug::Fault::fatal() {
  Exception copy = mv(*exception);
  delete exception;
  exception = nullptr;
  throwFatalException(mv(copy), 1);
  KJ_KNOWN_UNREACHABLE(abort());
}

void Debug::Fault::init(
    const char* file, int line, Exception::Type type,
    const char* condition, const char* macroArgs, ArrayPtr<String> argValues) {
  exception = new Exception(type, file, line,
      makeDescriptionImpl(ASSERTION, condition, 0, nullptr, macroArgs, argValues));
}

void Debug::Fault::init(
    const char* file, int line, int osErrorNumber,
    const char* condition, const char* macroArgs, ArrayPtr<String> argValues) {
  exception = new Exception(typeOfErrno(osErrorNumber), file, line,
      makeDescriptionImpl(SYSCALL, condition, osErrorNumber, nullptr, macroArgs, argValues));
}

#if _WIN32 || __CYGWIN__
void Debug::Fault::init(
    const char* file, int line, Win32Result osErrorNumber,
    const char* condition, const char* macroArgs, ArrayPtr<String> argValues) {
  LPVOID ptr;
  // TODO(someday): Why doesn't this work for winsock errors?
  DWORD result = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                FORMAT_MESSAGE_FROM_SYSTEM |
                                FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, osErrorNumber.number,
                                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                (LPWSTR) &ptr, 0, NULL);

  String message;
  if (result > 0) {
    KJ_DEFER(LocalFree(ptr));
    const wchar_t* desc = reinterpret_cast<wchar_t*>(ptr);
    size_t len = wcslen(desc);
    if (len > 0 && desc[len-1] == '\n') --len;
    if (len > 0 && desc[len-1] == '\r') --len;
    message = kj::str('#', osErrorNumber.number, ' ',
        decodeWideString(arrayPtr(desc, len)));
  } else {
    message = kj::str("win32 error code: ", osErrorNumber.number);
  }

  exception = new Exception(typeOfWin32Error(osErrorNumber.number), file, line,
      makeDescriptionImpl(SYSCALL, condition, 0, message.cStr(),
                          macroArgs, argValues));
}
#endif

String Debug::makeDescriptionInternal(const char* macroArgs, ArrayPtr<String> argValues) {
  return makeDescriptionImpl(LOG, nullptr, 0, nullptr, macroArgs, argValues);
}

int Debug::getOsErrorNumber(bool nonblocking) {
  int result = errno;

  // On many systems, EAGAIN and EWOULDBLOCK have the same value, but this is not strictly required
  // by POSIX, so we need to check both.
  return result == EINTR ? -1
       : nonblocking && (result == EAGAIN || result == EWOULDBLOCK) ? 0
       : result;
}

#if _WIN32 || __CYGWIN__
uint Debug::getWin32ErrorCode() {
  return ::GetLastError();
}
#endif

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
void Debug::Context::logMessage(LogSeverity severity, const char* file, int line, int contextDepth,
                                String&& text) {
  if (!logged) {
    Value v = ensureInitialized();
    next.logMessage(LogSeverity::INFO, trimSourceFilename(v.file).cStr(), v.line, 0,
                    str("context: ", mv(v.description), '\n'));
    logged = true;
  }

  next.logMessage(severity, file, line, contextDepth + 1, mv(text));
}

}  // namespace _ (private)
}  // namespace kj
