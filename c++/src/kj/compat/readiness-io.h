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

#pragma once

#include <kj/async-io.h>

namespace kj {

class ReadyInputStreamWrapper {
  // Provides readiness-based Async I/O as a wrapper around KJ's standard completion-based API, for
  // compatibility with libraries that use readiness-based abstractions (e.g. OpenSSL).
  //
  // Unfortunately this requires buffering, so is not very efficient.

public:
  ReadyInputStreamWrapper(AsyncInputStream& input);
  ~ReadyInputStreamWrapper() noexcept(false);
  KJ_DISALLOW_COPY(ReadyInputStreamWrapper);

  kj::Maybe<size_t> read(kj::ArrayPtr<byte> dst);
  // Reads bytes into `dst`, returning the number of bytes read. Returns zero only at EOF. Returns
  // nullptr if not ready.

  kj::Promise<void> whenReady();
  // Returns a promise that resolves when read() will return non-null.

private:
  AsyncInputStream& input;
  kj::ForkedPromise<void> pumpTask = nullptr;
  bool isPumping = false;
  bool eof = false;

  kj::ArrayPtr<const byte> content = nullptr;  // Points to currently-valid part of `buffer`.
  byte buffer[8192];
};

class ReadyOutputStreamWrapper {
  // Provides readiness-based Async I/O as a wrapper around KJ's standard completion-based API, for
  // compatibility with libraries that use readiness-based abstractions (e.g. OpenSSL).
  //
  // Unfortunately this requires buffering, so is not very efficient.

public:
  ReadyOutputStreamWrapper(AsyncOutputStream& output);
  ~ReadyOutputStreamWrapper() noexcept(false);
  KJ_DISALLOW_COPY(ReadyOutputStreamWrapper);

  kj::Maybe<size_t> write(kj::ArrayPtr<const byte> src);
  // Writes bytes from `src`, returning the number of bytes written. Never returns zero. Returns
  // nullptr if not ready.

  kj::Promise<void> whenReady();
  // Returns a promise that resolves when write() will return non-null.

private:
  AsyncOutputStream& output;
  ArrayPtr<const byte> segments[2];
  kj::ForkedPromise<void> pumpTask = nullptr;
  bool isPumping = false;

  uint start = 0;   // index of first byte
  uint filled = 0;  // number of bytes currently in buffer

  byte buffer[8192];

  kj::Promise<void> pump();
  // Asyncronously push the buffer out to the underlying stream.
};

} // namespace kj
