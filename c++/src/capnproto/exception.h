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

#ifndef CAPNPROTO_EXCEPTION_H_
#define CAPNPROTO_EXCEPTION_H_

#include <exception>
#include "type-safety.h"

namespace capnproto {

class Exception: public std::exception {
  // Exception thrown in case of fatal errors.

#ifdef __CDT_PARSER__
  // For some reason Eclipse gets confused by the definition of Nature if it's the first thing
  // in the class.
  typedef void WorkAroundCdtBug;
#endif

public:
  enum class Nature {
    // What kind of failure?  This is informational, not intended for programmatic use.
    // Note that the difference between some of these failure types is not always clear.  For
    // example, a precondition failure may be due to a "local bug" in the calling code, or it
    // may be due to invalid input.

    PRECONDITION,
    LOCAL_BUG,
    INPUT,
    OS_ERROR,
    NETWORK_FAILURE,
    OTHER

    // Make sure to update the stringifier if you add a new nature.
  };

  enum class Durability {
    TEMPORARY,  // Retrying the exact same operation might succeed.
    PERMANENT   // Retrying the exact same operation will fail in exactly the same way.

    // Make sure to update the stringifier if you add a new durability.
  };

  Exception(Nature nature, Durability durability, const char* file, int line,
            Array<char> description = nullptr) noexcept;
  Exception(const Exception& other) noexcept;
  Exception(Exception&& other) = default;
  ~Exception() noexcept;

  const char* getFile() const { return file; }
  int getLine() const { return line; }
  Nature getNature() const { return nature; }
  Durability getDurability() const { return durability; }
  ArrayPtr<const char> getDescription() const { return description; }

  struct Context {
    // Describes a bit about what was going on when the exception was thrown.

    const char* file;
    int line;
    Array<char> description;
    Maybe<Own<Context>> next;

    Context(const char* file, int line, Array<char>&& description, Maybe<Own<Context>>&& next)
        : file(file), line(line), description(move(description)), next(move(next)) {}
    Context(const Context& other) noexcept;
  };

  inline Maybe<const Context&> getContext() const {
    if (context == nullptr) {
      return nullptr;
    } else {
      return **context;
    }
  }

  void wrapContext(const char* file, int line, Array<char>&& description);
  // Wraps the context in a new node.  This becomes the head node returned by getContext() -- it
  // is expected that contexts will be added in reverse order as the exception passes up the
  // callback stack.

  const char* what() const noexcept override;

private:
  const char* file;
  int line;
  Nature nature;
  Durability durability;
  Array<char> description;
  Maybe<Own<Context>> context;
  void* trace[16];
  uint traceCount;
  mutable Array<char> whatBuffer;
};

class Stringifier;
ArrayPtr<const char> operator*(const Stringifier&, Exception::Nature nature);
ArrayPtr<const char> operator*(const Stringifier&, Exception::Durability durability);

class ExceptionCallback {
  // If you don't like C++ exceptions, you may implement and register an ExceptionCallback in order
  // to perform your own exception handling.
  //
  // For example, a reasonable thing to do is to have onRecoverableException() set a flag
  // indicating that an error occurred, and then check for that flag further up the stack.

public:
  ExceptionCallback();
  CAPNPROTO_DISALLOW_COPY(ExceptionCallback);
  virtual ~ExceptionCallback();

  virtual void onRecoverableException(Exception&& exception);
  // Called when an exception has been raised, but the calling code has the ability to continue by
  // producing garbage output.  This method _should_ throw the exception, but is allowed to simply
  // return if garbage output is acceptable.  The default implementation throws an exception unless
  // Cap'n Proto was compiled with -fno-exceptions, in which case it logs an error and returns.

  virtual void onFatalException(Exception&& exception);
  // Called when an exception has been raised and the calling code cannot continue.  If this method
  // returns normally, abort() will be called.  The method must throw the exception to avoid
  // aborting.  The default implementation throws an exception unless Cap'n Proto was compiled with
  // -fno-exceptions, in which case it logs an error and returns.

  virtual void logMessage(ArrayPtr<const char> text);
  // Called when Cap'n Proto wants to log some debug text.  The text always ends in a newline if
  // it is non-empty.  The default implementation writes the text to stderr.

  void useProcessWide();
  // Use this ExceptionCallback for all exceptions thrown from any thread in the process which
  // doesn't otherwise have a thread-local callback.  When the ExceptionCallback is destroyed,
  // the default behavior will be restored.  It is an error to set multiple process-wide callbacks
  // at the same time.
  //
  // Note that to delete (and thus unregister) a global ExceptionCallback, you must ensure that
  // no other threads might running code that could throw at that time.  It is probably best to
  // leave the callback registered permanently.

  class ScopedRegistration {
    // Allocate a ScopedRegistration on the stack to register you ExceptionCallback just within
    // the current thread.  When the ScopedRegistration is destroyed, the previous thread-local
    // callback will be restored.

  public:
    ScopedRegistration(ExceptionCallback& callback);
    CAPNPROTO_DISALLOW_COPY(ScopedRegistration);
    ~ScopedRegistration();

    inline ExceptionCallback& getCallback() { return callback; }

  private:
    ExceptionCallback& callback;
    ScopedRegistration* old;
  };
};

ExceptionCallback& getExceptionCallback();
// Returns the current exception callback.

}  // namespace capnproto

#endif  // CAPNPROTO_EXCEPTION_H_
