// Copyright (c) 2026 Cloudflare, Inc. and contributors
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


#include <kj/debug.h>
#include <kj/exception.h>

#include <exception>
#include <stdlib.h>
#include <unistd.h>

bool check();
void mayThrow();

// KJ_DTOR_* macros log instead of throwing if the stack is unwinding.
struct DtorMacros {
  ~DtorMacros() noexcept(false) {
    KJ_DTOR_ASSERT(check());
    KJ_DTOR_REQUIRE(check(), "message", fd);
    KJ_DTOR_FAIL_ASSERT("message");
    KJ_DTOR_FAIL_REQUIRE("message");
    KJ_DTOR_SYSCALL(close(fd));
    KJ_DTOR_FAIL_SYSCALL("close", 1, fd);
  }

  int fd = 0;
};

// Code whose exceptions are handled.
struct Handled {
  ~Handled() noexcept(false) {
    KJ_DTOR_CALL(KJ_ASSERT(check()));
    KJ_DTOR_CALL(kj::throwException(KJ_EXCEPTION(FAILED, "message")));
    kj::logExceptionsIfUnwinding([&]() {
      KJ_ASSERT(check());
      if (check()) throw 1;
    });
    unwindDetector.catchExceptionsIfUnwinding([&]() {
      KJ_ASSERT(check());
    });
    KJ_IF_SOME(e, kj::runCatchingExceptions([&]() {
      KJ_ASSERT(check());
    })) {
      KJ_LOG(ERROR, e);
    }
    KJ_DEFER(KJ_DTOR_CALL(KJ_ASSERT(check())));
  }

  kj::UnwindDetector unwindDetector;
};

// The body of a `try` with a `catch (...)` handler.
struct TryCatchAll {
  ~TryCatchAll() noexcept(false) {
    try {
      KJ_ASSERT(check());
      throw 1;
    } catch (...) {
    }

    try {
      KJ_ASSERT(check());
    } catch (const kj::Exception&) {
    } catch (...) {
    }

    KJ_TRY {
      KJ_ASSERT(check());
    } KJ_CATCH(e) {
      KJ_LOG(ERROR, e);
    }
  }
};

// Debug-only checks (the test is compiled with KJ_DEBUG defined, so they expand to KJ_ASSERT and
// KJ_REQUIRE).
struct DebugChecks {
  ~DebugChecks() noexcept(false) {
    KJ_DASSERT(check());
    KJ_DREQUIRE(check(), "message");
    KJ_ASSUME(check());
  }
};

// [[noreturn]] functions that end the process instead of throwing.
struct Terminates {
  ~Terminates() noexcept(false) {
    if (check()) std::terminate();
    if (check()) abort();
    if (check()) exit(1);
    if (check()) _exit(1);
  }
};

// Code that can't throw.
struct NoThrow {
  ~NoThrow() noexcept(false) {
    KJ_LOG(ERROR, "message");
    if (check()) return;
  }
};

// The check is lexical and does not follow calls out of the destructor.
struct Calls {
  ~Calls() noexcept(false) {
    mayThrow();
    helper();
  }

  void helper() {
    KJ_ASSERT(check());
  }
};

// In `noexcept` destructors (the default) any exception terminates, unwinding or not, so the
// KJ_DTOR_* macros would not help.
struct NoexceptDtors {
  ~NoexceptDtors() {
    KJ_ASSERT(check());
  }
};

struct ExplicitNoexceptDtors {
  ~ExplicitNoexceptDtors() noexcept {
    KJ_ASSERT(check());
  }
};

// Other functions may throw.
struct OtherFunctions {
  OtherFunctions() {
    KJ_ASSERT(check());
    if (check()) throw 1;
  }

  void method() {
    KJ_ASSERT(check());
    kj::throwException(KJ_EXCEPTION(FAILED, "message"));
  }

  void operator()() {
    KJ_ASSERT(check());
  }
};

void function() {
  KJ_ASSERT(check());
  if (check()) throw 1;
}

// In templates a call with a dependent argument is unresolved until instantiation.
template <typename T>
struct Template {
  ~Template() noexcept(false) {
    kj::logExceptionsIfUnwinding([&]() {
      KJ_ASSERT(value.check());
    });
    KJ_DTOR_CALL(KJ_ASSERT(value.check()));
    KJ_DTOR_ASSERT(value.check());
  }

  T value;
};

struct Checkable {
  bool check();
};

template struct Template<Checkable>;
