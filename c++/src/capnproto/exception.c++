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
#include "exception.h"
#include "util.h"
#include "logging.h"
#include <unistd.h>
#include <execinfo.h>
#include <stdlib.h>

namespace capnproto {

ArrayPtr<const char> operator*(const Stringifier&, Exception::Nature nature) {
  static const char* NATURE_STRINGS[] = {
    "precondition not met",
    "bug in code",
    "invalid input data",
    "error from OS",
    "network failure",
    "error"
  };

  const char* s = NATURE_STRINGS[static_cast<uint>(nature)];
  return arrayPtr(s, strlen(s));
}

ArrayPtr<const char> operator*(const Stringifier&, Exception::Durability durability) {
  static const char* DURABILITY_STRINGS[] = {
    "temporary",
    "permanent"
  };

  const char* s = DURABILITY_STRINGS[static_cast<uint>(durability)];
  return arrayPtr(s, strlen(s));
}

Exception::Exception(Nature nature, Durability durability, const char* file, int line,
                     Array<char> description) noexcept
    : file(file), line(line), nature(nature), durability(durability),
      description(move(description)) {
  traceCount = backtrace(trace, 16);
}

Exception::Exception(const Exception& other) noexcept
    : file(other.file), line(other.line), nature(other.nature), durability(other.durability),
      description(str(other.description)), traceCount(other.traceCount) {
  memcpy(trace, other.trace, sizeof(trace[0]) * traceCount);

  if (other.context != nullptr) {
    context = heap<Context>(**other.context);
  }
}

Exception::~Exception() noexcept {}

Exception::Context::Context(const Context& other) noexcept
    : file(other.file), line(other.line), description(str(other.description)) {
  if (other.next != nullptr) {
    next = heap<Context>(**other.next);
  }
}

void Exception::wrapContext(const char* file, int line, Array<char>&& description) {
  context = heap<Context>(file, line, move(description), move(context));
}

const char* Exception::what() const noexcept {
  uint contextDepth = 0;

  const Maybe<Own<Context>>* contextPtr = &context;
  while (*contextPtr != nullptr) {
    ++contextDepth;
    contextPtr = &(***contextPtr).next;
  }

  Array<Array<char>> contextText = newArray<Array<char>>(contextDepth);

  contextDepth = 0;
  contextPtr = &context;
  while (*contextPtr != nullptr) {
    const Context& node = ***contextPtr;
    contextText[contextDepth++] =
        str(node.file, ":", node.line, ": context: ", node.description, "\n");
    contextPtr = &node.next;
  }

  // Must be careful to NUL-terminate this.
  whatBuffer = str(strArray(contextText, ""),
                   file, ":", line, ": ", nature,
                   durability == Durability::TEMPORARY ? " (temporary)" : "",
                   this->description == nullptr ? "" : ": ", this->description,
                   "\nstack: ", strArray(arrayPtr(trace, traceCount), " "), '\0');

  return whatBuffer.begin();
}

// =======================================================================================

namespace {

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 8)
#define thread_local __thread
#endif

thread_local ExceptionCallback::ScopedRegistration* threadLocalCallback = nullptr;
ExceptionCallback* globalCallback = nullptr;

}  // namespace

ExceptionCallback::ExceptionCallback() {}
ExceptionCallback::~ExceptionCallback() {
  if (globalCallback == this) {
    globalCallback = nullptr;
  }
}

void ExceptionCallback::onRecoverableException(Exception&& exception) {
#if __GNUC__ && !__EXCEPTIONS
  logMessage(str(exception.what(), '\n'));
#else
  if (std::uncaught_exception()) {
    logMessage(str("unwind: ", exception.what(), '\n'));
  } else {
    throw std::move(exception);
  }
#endif
}

void ExceptionCallback::onFatalException(Exception&& exception) {
#if __GNUC__ && !__EXCEPTIONS
  logMessage(str(exception.what(), '\n'));
#else
  throw std::move(exception);
#endif
}

void ExceptionCallback::logMessage(ArrayPtr<const char> text) {
  while (text != nullptr) {
    ssize_t n = write(STDERR_FILENO, text.begin(), text.size());
    if (n <= 0) {
      // stderr is broken.  Give up.
      return;
    }
    text = text.slice(n, text.size());
  }
}

void ExceptionCallback::useProcessWide() {
  RECOVERABLE_PRECOND(globalCallback == nullptr,
      "Can't register multiple global ExceptionCallbacks at once.") {
    return;
  }
  globalCallback = this;
}

ExceptionCallback::ScopedRegistration::ScopedRegistration(ExceptionCallback& callback)
    : callback(callback) {
  old = threadLocalCallback;
  threadLocalCallback = this;
}

ExceptionCallback::ScopedRegistration::~ScopedRegistration() {
  threadLocalCallback = old;
}

ExceptionCallback& getExceptionCallback() {
  static ExceptionCallback defaultCallback;
  ExceptionCallback::ScopedRegistration* scoped = threadLocalCallback;
  return scoped != nullptr ? scoped->getCallback() :
     globalCallback != nullptr ? *globalCallback : defaultCallback;
}

}  // namespace capnproto
