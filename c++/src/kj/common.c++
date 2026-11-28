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

#include "common.h"
#include "debug.h"
#include <stdlib.h>

namespace kj {
namespace _ {  // private

void inlineRequireFailure(PackedSite site, const char* message) {
  if (message == nullptr) {
    Debug::fail(site, kj::Exception::Type::FAILED);
  } else {
    Debug::fail(site, kj::Exception::Type::FAILED, message);
  }
}

void inlineRequireFailure(PackedSite site, const char* message, size_t arg1) {
  Debug::fail(site, kj::Exception::Type::FAILED, message ? message : "", arg1);
}

void inlineRequireFailure(PackedSite site, const char* message, size_t arg1, size_t arg2) {
  Debug::fail(site, kj::Exception::Type::FAILED, message ? message : "", arg1, arg2);
}

void inlineRequireFailure(PackedSite site, const char* message, size_t arg1, size_t arg2,
                          size_t arg3) {
  Debug::fail(site, kj::Exception::Type::FAILED, message ? message : "", arg1, arg2, arg3);
}

void unreachable() {
  KJ_FAIL_ASSERT("Supposedly-unreachable branch executed.");

  // Really make sure we abort.
  KJ_KNOWN_UNREACHABLE(abort());
}


}  // namespace _ (private)

namespace { static thread_local bool currentThreadId; }

ThreadId ThreadId::current() { return ThreadId(&currentThreadId); }

void ThreadId::assertCurrentThread() const { KJ_ASSERT(id == &currentThreadId); }

}  // namespace kj
