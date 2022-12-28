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

KJ_BEGIN_HEADER

namespace kj {

class ReadyInputStreamWrapper {
  // Provides readiness-based Async I/O as a wrapper around KJ's standard completion-based API, for
  // compatibility with libraries that use readiness-based abstractions (e.g. OpenSSL).
  //
  // Unfortunately this requires buffering, so is not very efficient.

public:
  ReadyInputStreamWrapper(AsyncInputStream& input);
  ~ReadyInputStreamWrapper() noexcept(false);
  KJ_DISALLOW_COPY_AND_MOVE(ReadyInputStreamWrapper);

  kj::Maybe<size_t> read(kj::ArrayPtr<byte> dst);
  // Reads bytes into `dst`, returning the number of bytes read. Returns zero only at EOF. Returns
  // nullptr if not ready.

  kj::Promise<void> whenReady();
  // Returns a promise that resolves when read() will return non-null.

  bool isAtEnd() { return eof; }
  // Returns true if read() would return zero.

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
  KJ_DISALLOW_COPY_AND_MOVE(ReadyOutputStreamWrapper);

  kj::Maybe<size_t> write(kj::ArrayPtr<const byte> src);
  // Writes bytes from `src`, returning the number of bytes written. Never returns zero for
  // a non-empty `src`. Returns nullptr if not ready.

  kj::Promise<void> whenReady();
  // Returns a promise that resolves when write() will return non-null.

  class Cork;
  // An object that, when destructed, will uncork its parent stream.

  Cork cork();
  // After calling, data won't be pumped until either the internal buffer fills up or the returned
  // object is destructed. Use this if you know multiple small write() calls will be happening in
  // the near future and want to flush them all at once.
  // Once the returned object is destructed, behavior goes back to normal. The returned object
  // must be destructed before the ReadyOutputStreamWrapper.
  // TODO(perf): This is an ugly hack to avoid sending lots of tiny packets when using TLS, which
  // has to work around OpenSSL's readiness-based I/O layer. We could certainly do better here.

private:
  AsyncOutputStream& output;
  ArrayPtr<const byte> segments[2];
  kj::ForkedPromise<void> pumpTask = nullptr;
  bool isPumping = false;
  bool corked = false;

  uint start = 0;   // index of first byte
  uint filled = 0;  // number of bytes currently in buffer

  byte buffer[8192];

  void uncork();

  kj::Promise<void> pump();
  // Asynchronously push the buffer out to the underlying stream.
};

class ReadyOutputStreamWrapper::Cork {
  // An object that, when destructed, will uncork its parent stream.
public:
  ~Cork() {
    KJ_IF_MAYBE(p, parent) {
      p->uncork();
    }
  }
  Cork(Cork&& other) : parent(kj::mv(other.parent)) {
    other.parent = nullptr;
  }
  KJ_DISALLOW_COPY(Cork);

private:
  Cork(ReadyOutputStreamWrapper& parent) : parent(parent) {}

  kj::Maybe<ReadyOutputStreamWrapper&> parent;
  friend class ReadyOutputStreamWrapper;
};

} // namespace kj

KJ_END_HEADER
