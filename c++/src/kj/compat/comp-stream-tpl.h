// Copyright (c) 2023 Cloudflare, Inc. and contributors
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
#include <kj/io.h>
#include <kj/async-io.h>

KJ_BEGIN_HEADER

namespace kj {

// Templated classes to support implementing compression streams for different algorithms.
// Currently this includes classes based on OutputStream and AsyncOutputStream as there are limited
// synergies between the input stream implementations for zlib and brotli.
template <class T, class param_type, param_type FLUSH, param_type NO_FLUSH,
          param_type FINISH> class CompOutputStream final : public OutputStream {
public:
  enum { DECOMPRESS };

  CompOutputStream(OutputStream& inner, int compressionLevel = -1)
                  : inner(inner), ctx(compressionLevel) {}
  CompOutputStream(OutputStream& inner, decltype(DECOMPRESS)) : inner(inner), ctx(nullptr) {}
  ~CompOutputStream() noexcept(false) {
    pump(FINISH);
  }
  KJ_DISALLOW_COPY_AND_MOVE(CompOutputStream);

  void write(const void* buffer, size_t size) override {
    ctx.setInput(buffer, size);
    pump(NO_FLUSH);
  };
  using OutputStream::write;

  inline void flush() {
    pump(FLUSH);
  }

private:
  OutputStream& inner;
  T ctx;

  void pump(param_type flush) {
    bool ok;
    do {
      auto result = ctx.pumpOnce(flush);
      ok = get<0>(result);
      auto chunk = get<1>(result);
      if (chunk.size() > 0) {
        inner.write(chunk.begin(), chunk.size());
      }
    } while (ok);
  }
};

// =======================================================================================

template <class T, class param_type, param_type FLUSH, param_type NO_FLUSH,
          param_type FINISH> class CompAsyncOutputStream final : public AsyncOutputStream {
public:
  enum { DECOMPRESS };

  CompAsyncOutputStream(AsyncOutputStream& inner, int compressionLevel = -1)
                        : inner(inner), ctx(compressionLevel) {}
  CompAsyncOutputStream(AsyncOutputStream& inner, decltype(DECOMPRESS))
                        : inner(inner), ctx(nullptr) {}
  KJ_DISALLOW_COPY_AND_MOVE(CompAsyncOutputStream);

  Promise<void> write(const void* buffer, size_t size) override {
    ctx.setInput(buffer, size);
    return pump(NO_FLUSH);
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    if (pieces.size() == 0) return kj::READY_NOW;
    return write(pieces[0].begin(), pieces[0].size())
        .then([this,pieces]() {
      return write(pieces.slice(1, pieces.size()));
    });
  }

  Promise<void> whenWriteDisconnected() override { return inner.whenWriteDisconnected(); }

  inline Promise<void> flush() {
    return pump(FLUSH);
  }
  // Call if you need to flush a stream at an arbitrary data point.

  Promise<void> end() {
    return pump(FINISH);
  }
  // Must call to flush and finish the stream, since some data may be buffered.
  //
  // TODO(cleanup): This should be a virtual method on AsyncOutputStream.

private:
  AsyncOutputStream& inner;
  T ctx;

  kj::Promise<void> pump(param_type flush) {
    auto result = ctx.pumpOnce(flush);
    auto ok = get<0>(result);
    auto chunk = get<1>(result);

    if (chunk.size() == 0) {
      if (ok) {
        return pump(flush);
      } else {
        return kj::READY_NOW;
      }
    } else {
      auto promise = inner.write(chunk.begin(), chunk.size());
      if (ok) {
        promise = promise.then([this, flush]() { return pump(flush); });
      }
      return promise;
    }
  }
};

}  // namespace kj

KJ_END_HEADER
