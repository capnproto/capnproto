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

#ifndef KJ_EXCEPTION_H_
#define KJ_EXCEPTION_H_

#include "memory.h"
#include "array.h"
#include "string.h"

namespace kj {

class ExceptionImpl;

class Exception {
  // Exception thrown in case of fatal errors.
  //
  // Actually, a subclass of this which also implements std::exception will be thrown, but we hide
  // that fact from the interface to avoid #including <exception>.

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
            String description = nullptr) noexcept;
  Exception(const Exception& other) noexcept;
  Exception(Exception&& other) = default;
  ~Exception() noexcept;

  const char* getFile() const { return file; }
  int getLine() const { return line; }
  Nature getNature() const { return nature; }
  Durability getDurability() const { return durability; }
  StringPtr getDescription() const { return description; }
  ArrayPtr<void* const> getStackTrace() const { return arrayPtr(trace, traceCount); }

  struct Context {
    // Describes a bit about what was going on when the exception was thrown.

    const char* file;
    int line;
    String description;
    Maybe<Own<Context>> next;

    Context(const char* file, int line, String&& description, Maybe<Own<Context>>&& next)
        : file(file), line(line), description(mv(description)), next(mv(next)) {}
    Context(const Context& other) noexcept;
  };

  inline Maybe<const Context&> getContext() const {
    KJ_IF_MAYBE(c, context) {
      return *c;
    } else {
      return nullptr;
    }
  }

  void wrapContext(const char* file, int line, String&& description);
  // Wraps the context in a new node.  This becomes the head node returned by getContext() -- it
  // is expected that contexts will be added in reverse order as the exception passes up the
  // callback stack.

private:
  const char* file;
  int line;
  Nature nature;
  Durability durability;
  String description;
  Maybe<Own<Context>> context;
  void* trace[16];
  uint traceCount;

  friend class ExceptionImpl;
};

ArrayPtr<const char> KJ_STRINGIFY(Exception::Nature nature);
ArrayPtr<const char> KJ_STRINGIFY(Exception::Durability durability);
String KJ_STRINGIFY(const Exception& e);

// =======================================================================================

class ExceptionCallback {
  // If you don't like C++ exceptions, you may implement and register an ExceptionCallback in order
  // to perform your own exception handling.  For example, a reasonable thing to do is to have
  // onRecoverableException() set a flag indicating that an error occurred, and then check for that
  // flag just before writing to storage and/or returning results to the user.  If the flag is set,
  // discard whatever you have and return an error instead.
  //
  // ExceptionCallbacks must always be allocated on the stack.  When an exception is thrown, the
  // newest ExceptionCallback on the calling thread's stack is called.  The default implementation
  // of each method calls the next-oldest ExceptionCallback for that thread.  Thus the callbacks
  // behave a lot like try/catch blocks, except that they are called before any stack unwinding
  // occurs.

public:
  ExceptionCallback();
  KJ_DISALLOW_COPY(ExceptionCallback);
  virtual ~ExceptionCallback() noexcept(false);

  virtual void onRecoverableException(Exception&& exception);
  // Called when an exception has been raised, but the calling code has the ability to continue by
  // producing garbage output.  This method _should_ throw the exception, but is allowed to simply
  // return if garbage output is acceptable.
  //
  // The global default implementation throws an exception unless the library was compiled with
  // -fno-exceptions, in which case it logs an error and returns.

  virtual void onFatalException(Exception&& exception);
  // Called when an exception has been raised and the calling code cannot continue.  If this method
  // returns normally, abort() will be called.  The method must throw the exception to avoid
  // aborting.
  //
  // The global default implementation throws an exception unless the library was compiled with
  // -fno-exceptions, in which case it logs an error and returns.

  virtual void logMessage(const char* file, int line, int contextDepth, String&& text);
  // Called when something wants to log some debug text.  The text always ends in a newline if
  // it is non-empty.  `contextDepth` indicates how many levels of context the message passed
  // through; it may make sense to indent the message accordingly.
  //
  // The global default implementation writes the text to stderr.

protected:
  ExceptionCallback& next;

private:
  ExceptionCallback(ExceptionCallback& next);

  class RootExceptionCallback;
  friend ExceptionCallback& getExceptionCallback();
};

ExceptionCallback& getExceptionCallback();
// Returns the current exception callback.

// =======================================================================================

namespace _ { class Runnable; }

template <typename Func>
Maybe<Exception> runCatchingExceptions(Func&& func);
// Executes the given function (usually, a lambda returning nothing) catching any exceptions that
// are thrown.  Returns the Exception if there was one, or null if the operation completed normally.
// Non-KJ exceptions will be wrapped.
//
// If exception are disabled (e.g. with -fno-exceptions), this will still detect whether any
// recoverable exceptions occurred while running the function and will return those.

class UnwindDetector {
  // Utility for detecting when a destructor is called due to unwind.  Useful for:
  // - Avoiding throwing exceptions in this case, which would terminate the program.
  // - Detecting whether to commit or roll back a transaction.
  //
  // To use this class, either inherit privately from it or declare it as a member.  The detector
  // works by comparing the exception state against that when the constructor was called, so for
  // an object that was actually constructed during exception unwind, it will behave as if no
  // unwind is taking place.  This is usually the desired behavior.

public:
  UnwindDetector();

  bool isUnwinding() const;
  // Returns true if the current thread is in a stack unwind that it wasn't in at the time the
  // object was constructed.

  template <typename Func>
  void catchExceptionsIfUnwinding(Func&& func) const;
  // Runs the given function (e.g., a lambda).  If isUnwinding() is true, any exceptions are
  // caught and treated as secondary faults, meaning they are considered to be side-effects of the
  // exception that is unwinding the stack.  Otherwise, exceptions are passed through normally.

private:
  uint uncaughtCount;

  void catchExceptionsAsSecondaryFaults(_::Runnable& runnable) const;
};

namespace _ {  // private

class Runnable {
public:
  virtual void run() = 0;
};

template <typename Func>
class RunnableImpl: public Runnable {
public:
  RunnableImpl(Func&& func): func(kj::mv(func)) {}
  void run() override {
    func();
  }
private:
  Func func;
};

Maybe<Exception> runCatchingExceptions(Runnable& runnable);

}  // namespace _ (private)

template <typename Func>
Maybe<Exception> runCatchingExceptions(Func&& func) {
  _::RunnableImpl<Decay<Func>> runnable(kj::fwd<Func>(func));
  return _::runCatchingExceptions(runnable);
}

template <typename Func>
void UnwindDetector::catchExceptionsIfUnwinding(Func&& func) const {
  if (isUnwinding()) {
    _::RunnableImpl<Decay<Func>> runnable(kj::fwd<Func>(func));
    catchExceptionsAsSecondaryFaults(runnable);
  } else {
    func();
  }
}

}  // namespace kj

#endif  // KJ_EXCEPTION_H_
