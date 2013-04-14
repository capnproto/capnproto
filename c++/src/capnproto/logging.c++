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

namespace capnproto {

Log::Severity Log::minSeverity = Log::Severity::WARNING;

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

static Array<char> makeDescription(const char* condition, const char* macroArgs,
                                   ArrayPtr<Array<char>> argValues) {
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
      getExceptionCallback()->logMessage(
          str(__FILE__, ":", __LINE__, ": Failed to parse logging macro args into ",
              argValues.size(), " names: ", macroArgs, '\n'));
    }
  }

  {
    ArrayPtr<const char> expected = arrayPtr("expected ");
    ArrayPtr<const char> conditionArray = condition == nullptr ? nullptr : arrayPtr(condition);
    ArrayPtr<const char> sep = arrayPtr(" = ");
    ArrayPtr<const char> delim = arrayPtr("; ");

    size_t totalSize = 0;
    if (condition != nullptr) {
      totalSize += expected.size() + conditionArray.size();
    }
    for (size_t i = 0; i < argValues.size(); i++) {
      if (i > 0 || condition != nullptr) totalSize += delim.size();
      if (argNames[i].size() > 0 && argNames[i][0] != '\"') {
        totalSize += argNames[i].size() + sep.size();
      }
      totalSize += argValues[i].size();
    }

    ArrayBuilder<char> result(totalSize);

    if (condition != nullptr) {
      result.addAll(expected);
      result.addAll(conditionArray);
    }
    for (size_t i = 0; i < argValues.size(); i++) {
      if (i > 0 || condition != nullptr) result.addAll(delim);
      if (argNames[i].size() > 0 && argNames[i][0] != '\"') {
        result.addAll(argNames[i]);
        result.addAll(sep);
      }
      result.addAll(argValues[i]);
    }

    return result.finish();
  }
}

void Log::logInternal(const char* file, int line, Severity severity, const char* macroArgs,
                      ArrayPtr<Array<char>> argValues) {
  getExceptionCallback()->logMessage(
      str(severity, ": ", file, ":", line, ": ",
          makeDescription(nullptr, macroArgs, argValues), '\n'));
}

void Log::recoverableFaultInternal(
    const char* file, int line, Exception::Nature nature,
    const char* condition, const char* macroArgs, ArrayPtr<Array<char>> argValues) {
  getExceptionCallback()->onRecoverableException(
      Exception(nature, Exception::Durability::PERMANENT, file, line,
                makeDescription(condition, macroArgs, argValues)));
}

void Log::fatalFaultInternal(
    const char* file, int line, Exception::Nature nature,
    const char* condition, const char* macroArgs, ArrayPtr<Array<char>> argValues) {
  getExceptionCallback()->onFatalException(
      Exception(nature, Exception::Durability::PERMANENT, file, line,
                makeDescription(condition, macroArgs, argValues)));
  abort();
}

}  // namespace capnproto
