// Copyright (c) 2017 Cloudflare, Inc. and contributors
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

#ifndef KJ_COMPAT_GZIP_H_
#define KJ_COMPAT_GZIP_H_

#include <kj/async-io.h>
#include <zlib.h>

namespace kj {

class GzipAsyncInputStream: public AsyncInputStream {
public:
  GzipAsyncInputStream(AsyncInputStream& inner);
  ~GzipAsyncInputStream() noexcept(false);
  KJ_DISALLOW_COPY(GzipAsyncInputStream);

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override;

private:
  AsyncInputStream& inner;
  z_stream ctx;
  bool atValidEndpoint = false;

  byte buffer[4096];

  Promise<size_t> readImpl(byte* buffer, size_t minBytes, size_t maxBytes, size_t alreadyRead);
};

class GzipAsyncOutputStream: public AsyncOutputStream {
public:
  GzipAsyncOutputStream(AsyncOutputStream& inner, int compressionLevel = Z_DEFAULT_COMPRESSION);
  ~GzipAsyncOutputStream() noexcept(false);
  KJ_DISALLOW_COPY(GzipAsyncOutputStream);

  Promise<void> write(const void* buffer, size_t size) override;
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override;

  Promise<void> end();
  // Must call to flush the stream, since some data may be buffered.
  //
  // TODO(cleanup): This should be a virtual method on AsyncOutputStream.

private:
  AsyncOutputStream& inner;
  bool ended = false;
  z_stream ctx;

  byte buffer[4096];

  kj::Promise<void> pump();
};

}  // namespace kj

#endif // KJ_COMPAT_GZIP_H_
