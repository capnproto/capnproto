// Copyright (c) 2016 Sandstorm Development Group, Inc. and contributors
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

#include "readiness-io.h"
#include <kj/test.h>
#include <stdlib.h>

namespace kj {
namespace {

KJ_TEST("readiness IO: write small") {
  auto io = setupAsyncIo();
  auto pipe = io.provider->newOneWayPipe();

  byte buf[3]{};
  auto readPromise = pipe.in->read(buf);

  ReadyOutputStreamWrapper out(*pipe.out);
  KJ_ASSERT(KJ_ASSERT_NONNULL(out.write("foo"_kjb)) == 3);
  readPromise.wait(io.waitScope);
  KJ_ASSERT(buf == "foo"_kjb);
}

KJ_TEST("readiness IO: write many odd") {
  auto io = setupAsyncIo();
  auto pipe = io.provider->newOneWayPipe();

  ReadyOutputStreamWrapper out(*pipe.out);

  size_t totalWritten = 0;
  for (;;) {
    KJ_IF_SOME(n, out.write("bar"_kjb)) {
      totalWritten += n;
      if (n < 3) {
        break;
      }
    } else {
      KJ_FAIL_ASSERT("pipe buffer is divisible by 3? really?");
    }
  }

  auto buf = kj::heapArray<byte>(totalWritten + 1);
  size_t n = pipe.in->read(buf, totalWritten).wait(io.waitScope);
  KJ_ASSERT(n == totalWritten);
  for (size_t i = 0; i < totalWritten; i++) {
    KJ_ASSERT(buf[i] == "bar"_kjb[i%3]);
  }
}

KJ_TEST("readiness IO: write even") {
  auto io = setupAsyncIo();
  auto pipe = io.provider->newOneWayPipe();

  ReadyOutputStreamWrapper out(*pipe.out);

  size_t totalWritten = 0;
  for (;;) {
    KJ_IF_SOME(n, out.write("ba"_kjb)) {
      totalWritten += n;
      if (n < 2) {
        KJ_FAIL_ASSERT("pipe buffer is not divisible by 2? really?");
      }
    } else {
      break;
    }
  }

  auto buf = kj::heapArray<char>(totalWritten + 1);
  size_t n = pipe.in->read(buf.asBytes(), totalWritten).wait(io.waitScope);
  KJ_ASSERT(n == totalWritten);
  for (size_t i = 0; i < totalWritten; i++) {
    KJ_ASSERT(buf[i] == "ba"[i%2]);
  }
}

KJ_TEST("readiness IO: write while corked") {
  auto io = setupAsyncIo();
  auto pipe = io.provider->newOneWayPipe();

  byte buf[6]{};
  auto readPromise = pipe.in->read(buf, 3);

  ReadyOutputStreamWrapper out(*pipe.out);
  auto cork = out.cork();
  KJ_ASSERT(KJ_ASSERT_NONNULL(out.write("foo"_kjb)) == 3);

  // Data hasn't been written yet.
  KJ_ASSERT(!readPromise.poll(io.waitScope));

  // Write some more, and observe it still isn't flushed out yet.
  KJ_ASSERT(KJ_ASSERT_NONNULL(out.write("bar"_kjb)) == 3);
  KJ_ASSERT(!readPromise.poll(io.waitScope));

  // After reenabling pumping, the full read should succeed.
  // We start this block with `if (true) {` instead of just `{` to avoid g++-8 compiler warnings
  // telling us that this block isn't treated as part of KJ_ASSERT's internal `for` loop.
  if (true) {
    auto tmp = kj::mv(cork);
  }
  KJ_ASSERT(readPromise.wait(io.waitScope) == 6);
  KJ_ASSERT(buf == "foobar"_kjb);
}

KJ_TEST("readiness IO: write many odd while corked") {
  auto io = setupAsyncIo();
  auto pipe = io.provider->newOneWayPipe();

  // The even/odd tests should work just as before even with automatic pumping
  // corked, since we should still pump when the buffer fills up.
  ReadyOutputStreamWrapper out(*pipe.out);
  auto cork = out.cork();

  size_t totalWritten = 0;
  for (;;) {
    KJ_IF_SOME(n, out.write("bar"_kjb)) {
      totalWritten += n;
      if (n < 3) {
        break;
      }
    } else {
      KJ_FAIL_ASSERT("pipe buffer is divisible by 3? really?");
    }
  }

  auto buf = kj::heapArray<byte>(totalWritten + 1);
  size_t n = pipe.in->read(buf, totalWritten).wait(io.waitScope);
  KJ_ASSERT(n == totalWritten);
  for (size_t i = 0; i < totalWritten; i++) {
    KJ_ASSERT(buf[i] == "bar"_kjb[i%3]);
  }

  // Eager pumping should still be corked.
  KJ_ASSERT(KJ_ASSERT_NONNULL(out.write("bar"_kjb)) == 3);
  auto readPromise = pipe.in->read(buf, 3);
  KJ_ASSERT(!readPromise.poll(io.waitScope));
}

KJ_TEST("readiness IO: write many even while corked") {
  auto io = setupAsyncIo();
  auto pipe = io.provider->newOneWayPipe();

  ReadyOutputStreamWrapper out(*pipe.out);
  auto cork = out.cork();

  size_t totalWritten = 0;
  for (;;) {
    KJ_IF_SOME(n, out.write("ba"_kjb)) {
      totalWritten += n;
      if (n < 2) {
        KJ_FAIL_ASSERT("pipe buffer is not divisible by 2? really?");
      }
    } else {
      break;
    }
  }

  auto buf = kj::heapArray<byte>(totalWritten + 1);
  size_t n = pipe.in->read(buf, totalWritten).wait(io.waitScope);
  KJ_ASSERT(n == totalWritten);
  for (size_t i = 0; i < totalWritten; i++) {
    KJ_ASSERT(buf[i] == "ba"_kjb[i%2]);
  }

  // Eager pumping should still be corked.
  KJ_ASSERT(KJ_ASSERT_NONNULL(out.write("ba"_kjb)) == 2);
  auto readPromise = pipe.in->read(buf, 2);
  KJ_ASSERT(!readPromise.poll(io.waitScope));
}

KJ_TEST("readiness IO: read small") {
  auto io = setupAsyncIo();
  auto pipe = io.provider->newOneWayPipe();

  ReadyInputStreamWrapper in(*pipe.in);
  char buf[4]{};
  KJ_ASSERT(in.read(kj::ArrayPtr<char>(buf).asBytes()) == kj::none);

  pipe.out->write("foo"_kjb).wait(io.waitScope);

  in.whenReady().wait(io.waitScope);
  KJ_ASSERT(KJ_ASSERT_NONNULL(in.read(kj::ArrayPtr<char>(buf).asBytes())) == 3);
  buf[3] = '\0';
  KJ_ASSERT(kj::StringPtr(buf) == "foo");

  pipe.out = nullptr;

  kj::Maybe<size_t> finalRead;
  for (;;) {
    finalRead = in.read(kj::ArrayPtr<char>(buf).asBytes());
    KJ_IF_SOME(n, finalRead) {
      KJ_ASSERT(n == 0);
      break;
    } else {
      in.whenReady().wait(io.waitScope);
    }
  }
}

KJ_TEST("readiness IO: read many odd") {
  auto io = setupAsyncIo();
  auto pipe = io.provider->newOneWayPipe();

  byte dummy[8192]{};
  for (auto i: kj::indices(dummy)) {
    dummy[i] = "bar"[i%3];
  }
  auto writeTask = pipe.out->write(dummy).then([&]() {
    // shutdown
    pipe.out = nullptr;
  }).eagerlyEvaluate(nullptr);

  ReadyInputStreamWrapper in(*pipe.in);
  char buf[3]{};

  for (;;) {
    auto result = in.read(kj::ArrayPtr<char>(buf).asBytes());
    KJ_IF_SOME(n, result) {
      for (size_t i = 0; i < n; i++) {
        KJ_ASSERT(buf[i] == "bar"[i]);
      }
      KJ_ASSERT(n != 0, "ended at wrong spot");
      if (n < 3) {
        break;
      }
    } else {
      in.whenReady().wait(io.waitScope);
    }
  }

  kj::Maybe<size_t> finalRead;
  for (;;) {
    finalRead = in.read(kj::ArrayPtr<char>(buf).asBytes());
    KJ_IF_SOME(n, finalRead) {
      KJ_ASSERT(n == 0);
      break;
    } else {
      in.whenReady().wait(io.waitScope);
    }
  }
}

KJ_TEST("readiness IO: read many even") {
  auto io = setupAsyncIo();
  auto pipe = io.provider->newOneWayPipe();

  byte dummy[8192]{};
  for (auto i: kj::indices(dummy)) {
    dummy[i] = "ba"[i%2];
  }
  auto writeTask = pipe.out->write(dummy).then([&]() {
    // shutdown
    pipe.out = nullptr;
  }).eagerlyEvaluate(nullptr);

  ReadyInputStreamWrapper in(*pipe.in);
  char buf[2]{};

  for (;;) {
    auto result = in.read(kj::ArrayPtr<char>(buf).asBytes());
    KJ_IF_SOME(n, result) {
      for (size_t i = 0; i < n; i++) {
        KJ_ASSERT(buf[i] == "ba"[i]);
      }
      if (n == 0) {
        break;
      }
      KJ_ASSERT(n == 2, "ended at wrong spot");
    } else {
      in.whenReady().wait(io.waitScope);
    }
  }

  kj::Maybe<size_t> finalRead;
  for (;;) {
    finalRead = in.read(kj::ArrayPtr<char>(buf).asBytes());
    KJ_IF_SOME(n, finalRead) {
      KJ_ASSERT(n == 0);
      break;
    } else {
      in.whenReady().wait(io.waitScope);
    }
  }
}

}  // namespace
}  // namespace kj
