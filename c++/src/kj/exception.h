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

#pragma once

#include <inttypes.h>
#include <type_traits>
#include "memory.h"
#include "array.h"
#include "string.h"
#include "map.h"
#include "one-of.h"
#include "refcount.h"
#include "windows-sanity.h"  // work-around macro conflict with `ERROR`

KJ_BEGIN_HEADER

namespace kj {

class ExceptionImpl;
template <typename T> class Function;

namespace _ {
template <typename T, typename F = decltype(&T::tryDeserializeForKjException)>
constexpr bool hasDeserializeForKjException(T*) {
  static_assert(!std::is_member_function_pointer_v<F>);
  return true;
}
constexpr bool hasDeserializeForKjException(...) { return false; }

template <typename T, typename F = decltype(&T::trySerializeForKjException)>
constexpr bool hasSerializeForKjException(T*) {
  static_assert(std::is_member_function_pointer_v<F>);
  return true;
}
constexpr bool hasSerializeForKjException(...) { return false; }

template <typename T, typename F = decltype(&T::EXCEPTION_DETAIL_TYPE_ID)>
constexpr bool hasExceptionDetailTypeId(T*) {
  static_assert(std::is_same_v<decltype(T::EXCEPTION_DETAIL_TYPE_ID), const uint64_t>);
  return true;
}

constexpr bool hasExceptionDetailTypeId(...) { return false; }

class DetailMapImpl;
}  //namespace _

class Exception {
  // Exception thrown in case of fatal errors.
  //
  // Actually, a subclass of this which also implements std::exception will be thrown, but we hide
  // that fact from the interface to avoid #including <exception>.

public:
  enum class Type {
    // What kind of failure?

    FAILED = 0,
    // Something went wrong. This is the usual error type. KJ_ASSERT and KJ_REQUIRE throw this
    // error type.

    OVERLOADED = 1,
    // The call failed because of a temporary lack of resources. This could be space resources
    // (out of memory, out of disk space) or time resources (request queue overflow, operation
    // timed out).
    //
    // The operation might work if tried again, but it should NOT be repeated immediately as this
    // may simply exacerbate the problem.

    DISCONNECTED = 2,
    // The call required communication over a connection that has been lost. The callee will need
    // to re-establish connections and try again.

    UNIMPLEMENTED = 3
    // The requested method is not implemented. The caller may wish to revert to a fallback
    // approach based on other methods.

    // IF YOU ADD A NEW VALUE:
    // - Update the stringifier.
    // - Update Cap'n Proto's RPC protocol's Exception.Type enum.
  };

  Exception(Type type, const char* file, int line, String description = nullptr) noexcept;
  Exception(Type type, String file, int line, String description = nullptr) noexcept;
  Exception(const Exception& other) noexcept;
  Exception(Exception&& other) = default;
  ~Exception() noexcept;

  const char* getFile() const { return file; }
  int getLine() const { return line; }
  Type getType() const { return type; }
  StringPtr getDescription() const { return description; }
  ArrayPtr<void* const> getStackTrace() const { return arrayPtr(trace, traceCount); }

  void setDescription(kj::String&& desc) { description = kj::mv(desc); }

  StringPtr getRemoteTrace() const { return remoteTrace; }
  void setRemoteTrace(kj::String&& value) { remoteTrace = kj::mv(value); }
  // Additional stack trace data originating from a remote server. If present, then
  // `getStackTrace()` only traces up until entry into the RPC system, and the remote trace
  // contains any trace information returned over the wire. This string is human-readable but the
  // format is otherwise unspecified.

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
    KJ_IF_SOME(c, context) {
      return *c;
    } else {
      return kj::none;
    }
  }

  void wrapContext(const char* file, int line, String&& description);
  // Wraps the context in a new node.  This becomes the head node returned by getContext() -- it
  // is expected that contexts will be added in reverse order as the exception passes up the
  // callback stack.

  KJ_NOINLINE void extendTrace(uint ignoreCount, uint limit = kj::maxValue);
  // Append the current stack trace to the exception's trace, ignoring the first `ignoreCount`
  // frames (see `getStackTrace()` for discussion of `ignoreCount`).
  //
  // If `limit` is set, limit the number of frames added to the given number.

  KJ_NOINLINE void truncateCommonTrace();
  // Remove the part of the stack trace which the exception shares with the caller of this method.
  // This is used by the async library to remove the async infrastructure from the stack trace
  // before replacing it with the async trace.

  void addTrace(void* ptr);
  // Append the given pointer to the backtrace, if it is not already full. This is used by the
  // async library to trace through the promise chain that led to the exception.

  KJ_NOINLINE void addTraceHere();
  // Adds the location that called this method to the stack trace.

  template <typename T>
  void setDetail(T&& detail) {
    static_assert(_::hasExceptionDetailTypeId((T*)nullptr));
    Own<Detail> entry = kj::heap<DetailImpl<T>>(kj::mv(detail));
    detailMap->set(T::EXCEPTION_DETAIL_TYPE_ID, kj::mv(entry));
  }

  template <typename T>
  Maybe<const T&> getDetail() const {
    static_assert(_::hasExceptionDetailTypeId((T*)nullptr));
    KJ_IF_SOME(found, detailMap->find(T::EXCEPTION_DETAIL_TYPE_ID)) {
      KJ_SWITCH_ONEOF(found) {
        KJ_CASE_ONEOF(raw, kj::Array<kj::byte>) {
          if constexpr (_::hasDeserializeForKjException((T*)nullptr)) {
            // In this case, we want to try deserializing the bytes into a T.
            // If successful, we'll replace the value in the detailMap to avoid
            // having to deserialize again. If unsuccessful, we'll return none
            // and leave the stored bytes as is.
            KJ_IF_SOME(instance, T::tryDeserializeForKjException(raw.asPtr())) {
              Own<Detail> holder = kj::heap<DetailImpl<T>>(kj::mv(instance));
              auto& value = static_cast<const DetailImpl<T>&>(*holder).getValue();
              detailMap->set(T::EXCEPTION_DETAIL_TYPE_ID, kj::mv(holder));
              return value;
            }
          } else {
            return kj::none;
          }
        }
        KJ_CASE_ONEOF(detail, Own<Detail>) {
          KJ_IF_SOME(holder, kj::dynamicDowncastIfAvailable<const DetailImpl<T>>(*detail)) {
            return holder.getValue();
          }
        }
      }
    }
    return kj::none;
  }

  template <typename T>
  bool hasDetail() const {
    static_assert(_::hasExceptionDetailTypeId((T*)nullptr));
    return detailMap->find(T::EXCEPTION_DETAIL_TYPE_ID) != kj::none;
  }

  // The set/getDetail methods are used to attach arbitrary additional structured data to
  // the Exception. The detail data must be a struct or class that defines a static uint64_t
  // identifier field named `EXCEPTION_DETAIL_TYPE_ID`.
  // To include the detail data in a capnp RPC serialization, the detail type must implement
  // a regular member method `kj::Maybe<kj::Array<kj::byte>> trySerializeForKjException()`.
  // To deserialize, the detail type must implement a static
  // `kj::Maybe<T> tryDeserializeForException(kj::ArrayPtr<const kj::byte>)`.
  // The trySerialize... and tryDeserialize... methods are optional, however.
  // The detail struct must be movable.
  // The exception will take ownership of the detail struct.
  // Calling setDetail<T>(...) multiple times will overwrite the previous value.
  // The hasDetail<T>() method can be used to check if a detail of a given type is present without
  // deserializing it.
  // The first time getDetail<T>() is called, if the value is deserializable, it will be and
  // the internal storage will be replaced with the deserialized value. Subsequent calls will
  // return the deserialized value directly.
  // The `trySerializeForKjException()` method may be called multiple times if the exception is
  // serialized multiple times.
  //
  // Note that it is important that the EXCEPTION_DETAIL_TYPE_ID be relatively unique within
  // a scope. It is ok for different structs to use the same id value (particular if one struct
  // is used for serialization and another for deserialization), but given that the ID is used
  // as the key, duplicating the ID can result in type confusion.

  template <typename Func>
  void serializeDetail(Func callback) const {
    detailMap->forEach([&](uint64_t id, const DetailMap::Value& value, size_t size) {
      KJ_SWITCH_ONEOF(value) {
        KJ_CASE_ONEOF(raw, kj::Array<kj::byte>) {
          callback(id, raw.asPtr(), size);
        }
        KJ_CASE_ONEOF(detail, Own<Detail>) {
          // If the detail entry does not return a serialized value, then we skip it.
          KJ_IF_SOME(raw, detail->trySerializeForException()) {
            callback(id, raw.asPtr(), size);
          }
        }
      }
    });
  }

  void setDetail(uint64_t id, Array<byte>&& value) {
    detailMap->set(id, kj::mv(value));
  }

  // The serializeDetail and non-templated setDetail are used by rpc.c++ when serializing and
  // deserializing the Exception.

private:
  String ownFile;
  const char* file;
  int line;
  Type type;
  String description;
  Maybe<Own<Context>> context;
  String remoteTrace;
  void* trace[32];
  uint traceCount;

  class Detail {
    // Type-erased holder for exception detail data.
  public:
    virtual ~Detail() noexcept(false) {}
    virtual kj::Maybe<kj::Array<kj::byte>> trySerializeForException() const = 0;
  };

  template <typename T>
  class DetailImpl: public Detail {
  public:
    DetailImpl(T&& value): value(kj::mv(value)) {}
    kj::Maybe<kj::Array<kj::byte>> trySerializeForException() const override {
      if constexpr (_::hasSerializeForKjException((T*)nullptr)) {
        return value.trySerializeForKjException();
      } else {
        return kj::none;
      }
    }
    const T& getValue() const { return value; }
  private:
    T value;
  };

  class DetailMap : public AtomicRefcounted {
    // We don't use a HashMap directly here because we need it to be thread-safe
    // using a MutexGuarded under the covers. We can't use a MutexGuarded directly
    // in this header, however, because it would require including mutex.h, which
    // would introduce a circular dependency. So, we use a virtual interface instead.
  public:
    using Value = OneOf<Own<Detail>, Array<byte>>;
    virtual Maybe<const Value&> find(uint64_t id) const = 0;
    virtual void set(uint64_t id, Value value) const = 0;
    virtual size_t size() const = 0;
    virtual void forEach(
        Function<void(uint64_t, const Value&, size_t)> callback) const = 0;
  };

  Arc<DetailMap> detailMap;

  bool isFullTrace = false;
  // Is `trace` a full trace to the top of the stack (or as close as we could get before we ran
  // out of space)? If this is false, then `trace` is instead a partial trace covering just the
  // frames between where the exception was thrown and where it was caught.
  //
  // extendTrace() transitions this to true, and truncateCommonTrace() changes it back to false.
  //
  // In theory, an exception should only hold a full trace when it is in the process of being
  // thrown via the C++ exception handling mechanism -- extendTrace() is called before the throw
  // and truncateCommonTrace() after it is caught. Note that when exceptions propagate through
  // async promises, the trace is extended one frame at a time instead, so isFullTrace should
  // remain false.

  friend class ExceptionImpl;
  friend class _::DetailMapImpl;
};

struct CanceledException { };
// This exception is thrown to force-unwind a stack in order to immediately cancel whatever that
// stack was doing. It is used in the implementation of fibers in particular. Application code
// should almost never catch this exception, unless you need to modify stack unwinding for some
// reason. kj::runCatchingExceptions() does not catch it.

StringPtr KJ_STRINGIFY(Exception::Type type);
String KJ_STRINGIFY(const Exception& e);

// =======================================================================================

enum class LogSeverity {
  INFO,      // Information describing what the code is up to, which users may request to see
             // with a flag like `--verbose`.  Does not indicate a problem.  Not printed by
             // default; you must call setLogLevel(INFO) to enable.
  WARNING,   // A problem was detected but execution can continue with correct output.
  ERROR,     // Something is wrong, but execution can continue with garbage output.
  FATAL,     // Something went wrong, and execution cannot continue.
  DBG        // Temporary debug logging.  See KJ_DBG.

  // Make sure to update the stringifier if you add a new severity level.
};

StringPtr KJ_STRINGIFY(LogSeverity severity);

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
  KJ_DISALLOW_COPY_AND_MOVE(ExceptionCallback);
  virtual ~ExceptionCallback() noexcept(false);

  virtual void onRecoverableException(Exception&& exception);
  // Called when an exception has been raised, but the calling code has the ability to continue by
  // producing garbage output.  This method _should_ throw the exception, but is allowed to simply
  // return if garbage output is acceptable.
  //
  // The global default implementation throws an exception, unless we're currently in a destructor
  // unwinding due to another exception being thrown, in which case it logs an error and returns.

  virtual void onFatalException(Exception&& exception);
  // Called when an exception has been raised and the calling code cannot continue.  If this method
  // returns normally, abort() will be called.  The method must throw the exception to avoid
  // aborting.
  //
  // The global default implementation throws an exception.

  virtual void logMessage(LogSeverity severity, const char* file, int line, int contextDepth,
                          String&& text);
  // Called when something wants to log some debug text.  `contextDepth` indicates how many levels
  // of context the message passed through; it may make sense to indent the message accordingly.
  //
  // The global default implementation writes the text to stderr.

  enum class StackTraceMode {
    FULL,
    // Stringifying a stack trace will attempt to determine source file and line numbers. This may
    // be expensive. For example, on Linux, this shells out to `addr2line`.
    //
    // This is the default in debug builds.

    ADDRESS_ONLY,
    // Stringifying a stack trace will only generate a list of code addresses.
    //
    // This is the default in release builds.

    NONE
    // Generating a stack trace will always return an empty array.
    //
    // This avoids ever unwinding the stack. On Windows in particular, the stack unwinding library
    // has been observed to be pretty slow, so exception-heavy code might benefit significantly
    // from this setting. (But exceptions should be rare...)
  };

  virtual StackTraceMode stackTraceMode();
  // Returns the current preferred stack trace mode.

  virtual Function<void(Function<void()>)> getThreadInitializer();
  // Called just before a new thread is spawned using kj::Thread. Returns a function which should
  // be invoked inside the new thread to initialize the thread's ExceptionCallback. The initializer
  // function itself receives, as its parameter, the thread's main function, which it must call.

protected:
  ExceptionCallback& next;

private:
  ExceptionCallback(ExceptionCallback& next);

  class RootExceptionCallback;
  friend ExceptionCallback& getExceptionCallback();

  friend class Thread;
};

ExceptionCallback& getExceptionCallback();
// Returns the current exception callback.

KJ_NOINLINE KJ_NORETURN(void throwFatalException(kj::Exception&& exception, uint ignoreCount = 0));
// Invoke the exception callback to throw the given fatal exception.  If the exception callback
// returns, abort.
//
// TODO(2.0): Rename this to `throwException()`.

KJ_NOINLINE void throwRecoverableException(kj::Exception&& exception, uint ignoreCount = 0);
// Invoke the exception callback to throw the given recoverable exception.  If the exception
// callback returns, return normally.
//
// TODO(2.0): Rename this to `throwExceptionUnlessUnwinding()`. (Or, can we fix the unwind problem
//   and be able to remove this entirely?)

// =======================================================================================

namespace _ { class Runnable; }

template <typename Func>
Maybe<Exception> runCatchingExceptions(Func&& func);
// Executes the given function (usually, a lambda returning nothing) catching any exceptions that
// are thrown.  Returns the Exception if there was one, or null if the operation completed normally.
// Non-KJ exceptions will be wrapped.
//
// TODO(2.0): Remove this. Introduce KJ_CATCH() macro which uses getCaughtExceptionAsKj() to handle
//   exception coercion and stack trace management. Then use try/KJ_CATCH everywhere.

kj::Exception getCaughtExceptionAsKj();
// Call from the catch block of a try/catch to get a `kj::Exception` representing the exception
// that was caught, the same way that `kj::runCatchingExceptions` would when catching an exception.
// This is sometimes useful if `runCatchingExceptions()` doesn't quite fit your use case. You can
// call this from any catch block, including `catch (...)`.
//
// Some exception types will actually be rethrown by this function, rather than returned. The most
// common example is `CanceledException`, whose purpose is to unwind the stack and is not meant to
// be caught.

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

  void catchThrownExceptionAsSecondaryFault() const;
};

template <typename Func>
Maybe<Exception> runCatchingExceptions(Func&& func) {
  try {
    func();
    return kj::none;
  } catch (...) {
    return getCaughtExceptionAsKj();
  }
}

template <typename Func>
void UnwindDetector::catchExceptionsIfUnwinding(Func&& func) const {
  if (isUnwinding()) {
    try {
      func();
    } catch (...) {
      catchThrownExceptionAsSecondaryFault();
    }
  } else {
    func();
  }
}

#define KJ_ON_SCOPE_SUCCESS(code) \
  ::kj::UnwindDetector KJ_UNIQUE_NAME(_kjUnwindDetector); \
  KJ_DEFER(if (!KJ_UNIQUE_NAME(_kjUnwindDetector).isUnwinding()) { code; })
// Runs `code` if the current scope is exited normally (not due to an exception).

#define KJ_ON_SCOPE_FAILURE(code) \
  ::kj::UnwindDetector KJ_UNIQUE_NAME(_kjUnwindDetector); \
  KJ_DEFER(if (KJ_UNIQUE_NAME(_kjUnwindDetector).isUnwinding()) { code; })
// Runs `code` if the current scope is exited due to an exception.

// =======================================================================================

KJ_NOINLINE ArrayPtr<void* const> getStackTrace(ArrayPtr<void*> space, uint ignoreCount);
// Attempt to get the current stack trace, returning a list of pointers to instructions. The
// returned array is a slice of `space`. Provide a larger `space` to get a deeper stack trace.
// If the platform doesn't support stack traces, returns an empty array.
//
// `ignoreCount` items will be truncated from the front of the trace. This is useful for chopping
// off a prefix of the trace that is uninteresting to the developer because it's just locations
// inside the debug infrastructure that is requesting the trace. Be careful to mark functions as
// KJ_NOINLINE if you intend to count them in `ignoreCount`. Note that, unfortunately, the
// ignored entries will still waste space in the `space` array (and the returned array's `begin()`
// is never exactly equal to `space.begin()` due to this effect, even if `ignoreCount` is zero
// since `getStackTrace()` needs to ignore its own internal frames).

String stringifyStackTrace(ArrayPtr<void* const>);
// Convert the stack trace to a string with file names and line numbers. This may involve executing
// suprocesses.

String stringifyStackTraceAddresses(ArrayPtr<void* const> trace);
StringPtr stringifyStackTraceAddresses(ArrayPtr<void* const> trace, ArrayPtr<char> scratch);
// Construct a string containing just enough information about a stack trace to be able to convert
// it to file and line numbers later using offline tools. This produces a sequence of
// space-separated code location identifiers. Each identifier may be an absolute address
// (hex number starting with 0x) or may be a module-relative address "<module>@0x<hex>". The
// latter case is preferred when ASLR is in effect and has loaded different modules at different
// addresses.

String getStackTrace();
// Get a stack trace right now and stringify it. Useful for debugging.

void printStackTraceOnCrash();
// Registers signal handlers on common "crash" signals like SIGSEGV that will (attempt to) print
// a stack trace. You should call this as early as possible on program startup. Programs using
// KJ_MAIN get this automatically.

void resetCrashHandlers();
// Resets all signal handlers set by printStackTraceOnCrash().

kj::StringPtr trimSourceFilename(kj::StringPtr filename);
// Given a source code file name, trim off noisy prefixes like "src/" or
// "/ekam-provider/canonical/".

kj::String getCaughtExceptionType();
// Utility function which attempts to return the human-readable type name of the exception
// currently being thrown. This can be called inside a catch block, including a catch (...) block,
// for the purpose of error logging. This function is best-effort; on some platforms it may simply
// return "(unknown)".

class InFlightExceptionIterator {
  // A class that can be used to iterate over exceptions that are in-flight in the current thread,
  // meaning they are either uncaught, or caught by a catch block that is current executing.
  //
  // This is meant for debugging purposes, and the results are best-effort. The C++ standard
  // library does not provide any way to inspect uncaught exceptions, so this class can only
  // discover KJ exceptions thrown using throwFatalException() or throwRecoverableException().
  // All KJ code uses those two functions to throw exceptions, but if your own code uses a bare
  // `throw`, or if the standard library throws an exception, these cannot be inspected.
  //
  // This class is safe to use in a signal handler.

public:
  InFlightExceptionIterator();

  Maybe<const Exception&> next();

private:
  const Exception* ptr;
};

kj::Exception getDestructionReason(void* traceSeparator,
    kj::Exception::Type defaultType, const char* defaultFile, int defaultLine,
    kj::StringPtr defaultDescription);
// Returns an exception that attempts to capture why a destructor has been invoked. If a KJ
// exception is currently in-flight (see InFlightExceptionIterator), then that exception is
// returned. Otherwise, an exception is constructed using the current stack trace and the type,
// file, line, and description provided. In the latter case, `traceSeparator` is appended to the
// stack trace; this should be a pointer to some dummy symbol which acts as a separator between the
// original stack trace and any new trace frames added later.

kj::ArrayPtr<void* const> computeRelativeTrace(
    kj::ArrayPtr<void* const> trace, kj::ArrayPtr<void* const> relativeTo);
// Given two traces expected to have started from the same root, try to find the part of `trace`
// that is different from `relativeTo`, considering that either or both traces might be truncated.
//
// This is useful for debugging, when reporting several related traces at once.

void requireOnStack(void* ptr, kj::StringPtr description);
// Throw an exception if `ptr` does not appear to point to something near the top of the stack.
// Used as a safety check for types that must be stack-allocated, like ExceptionCallback.

}  // namespace kj

KJ_END_HEADER
