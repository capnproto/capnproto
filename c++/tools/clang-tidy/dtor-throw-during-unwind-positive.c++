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

#include <unistd.h>

[[noreturn]] void userNoReturn();
bool check();
void mayThrow();

// Release checks in a `noexcept(false)` destructor throw even if the stack is unwinding.
struct Checks {
  ~Checks() noexcept(false) {
    KJ_ASSERT(check());  // CHECK-MATCH
    KJ_REQUIRE(check(), "message", 1);  // CHECK-MATCH
    KJ_FAIL_ASSERT("message");  // CHECK-MATCH
    KJ_FAIL_REQUIRE("message");  // CHECK-MATCH
    KJ_SYSCALL(close(fd));  // CHECK-MATCH
    KJ_NONBLOCKING_SYSCALL(close(fd));  // CHECK-MATCH
    KJ_FAIL_SYSCALL("close", 1, fd);  // CHECK-MATCH
    KJ_UNIMPLEMENTED("message");  // CHECK-MATCH
    KJ_IREQUIRE(check());  // CHECK-MATCH
    KJ_UNREACHABLE;  // CHECK-MATCH
  }

  int fd = 0;
};

// Recovery blocks usually skip the throw, but are deprecated in favor of KJ_DTOR_*.
struct RecoveryBlocks {
  ~RecoveryBlocks() noexcept(false) {
    KJ_ASSERT(check()) { break; }  // CHECK-MATCH
    KJ_REQUIRE(check()) { return; }  // CHECK-MATCH
    KJ_FAIL_ASSERT("message") { break; }  // CHECK-MATCH
  }
};

// Explicit throws and calls to [[noreturn]] functions that are not noexcept.
struct Throws {
  ~Throws() noexcept(false) {
    if (check()) throw 1;  // CHECK-MATCH
    if (check()) throw;  // CHECK-MATCH
    if (check()) kj::throwException(KJ_EXCEPTION(FAILED, "message"));  // CHECK-MATCH
    if (check()) kj::throwFatalException(KJ_EXCEPTION(FAILED, "message"));  // CHECK-MATCH
    if (check()) userNoReturn();  // CHECK-MATCH
  }
};

// Lambdas written in the destructor, such as KJ_DEFER blocks, run as part of it.
struct Lambdas {
  ~Lambdas() noexcept(false) {
    KJ_DEFER(KJ_ASSERT(check()));  // CHECK-MATCH
    auto lambda = []() {
      KJ_ASSERT(check());  // CHECK-MATCH
    };
    lambda();
  }
};

// Only `catch (...)` protects a `try` body, and handlers are not protected at all.
struct TryCatch {
  ~TryCatch() noexcept(false) {
    try {
      KJ_ASSERT(check());  // CHECK-MATCH
    } catch (const kj::Exception&) {
    }

    try {
      mayThrow();
    } catch (...) {
      KJ_ASSERT(check());  // CHECK-MATCH
    }

    KJ_TRY {
      mayThrow();
    } KJ_CATCH(e) {
      kj::throwException(kj::mv(e));  // CHECK-MATCH
    }
  }
};

// Out-of-line destructors.
struct OutOfLine {
  ~OutOfLine() noexcept(false);
};

OutOfLine::~OutOfLine() noexcept(false) {
  KJ_ASSERT(check());  // CHECK-MATCH
}

// Templates are checked as written, as well as when instantiated.
template <typename T>
struct Template {
  ~Template() noexcept(false) {
    KJ_ASSERT(value.check());  // CHECK-MATCH
    KJ_ASSERT(check());  // CHECK-MATCH
  }

  T value;
};

struct Checkable {
  bool check();
};

template struct Template<Checkable>;
