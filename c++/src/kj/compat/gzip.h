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

#pragma once

#include "comp-stream-tpl.h"
#include <kj/io.h>
#include <kj/async-io.h>
#include <zlib.h>

KJ_BEGIN_HEADER

namespace kj {

namespace _ {  // private

constexpr size_t KJ_GZ_BUF_SIZE = 4096;

class GzipOutputContext final {
public:
  // We always use the maximum window size of 32KB for gzip, the parameter is only provided here
  // for compatibility purposes.
  GzipOutputContext(kj::Maybe<int> compressionLevel, kj::Maybe<int> KJ_UNUSED windowBits = nullptr);
  ~GzipOutputContext() noexcept(false);
  KJ_DISALLOW_COPY_AND_MOVE(GzipOutputContext);

  void setInput(const void* in, size_t size);
  kj::Tuple<bool, kj::ArrayPtr<const byte>> pumpOnce(int flush);

private:
  bool compressing;
  z_stream ctx = {};
  byte buffer[_::KJ_GZ_BUF_SIZE];

  [[noreturn]] void fail(int result);
};

}  // namespace _ (private)

class GzipInputStream final: public InputStream {
public:
  GzipInputStream(InputStream& inner);
  ~GzipInputStream() noexcept(false);
  KJ_DISALLOW_COPY_AND_MOVE(GzipInputStream);

  size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) override;

private:
  InputStream& inner;
  z_stream ctx = {};
  bool atValidEndpoint = false;

  byte buffer[_::KJ_GZ_BUF_SIZE];

  size_t readImpl(byte* buffer, size_t minBytes, size_t maxBytes, size_t alreadyRead);
};

class GzipAsyncInputStream final: public AsyncInputStream {
public:
  GzipAsyncInputStream(AsyncInputStream& inner);
  ~GzipAsyncInputStream() noexcept(false);
  KJ_DISALLOW_COPY_AND_MOVE(GzipAsyncInputStream);

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override;

private:
  AsyncInputStream& inner;
  z_stream ctx = {};
  bool atValidEndpoint = false;

  byte buffer[_::KJ_GZ_BUF_SIZE];

  Promise<size_t> readImpl(byte* buffer, size_t minBytes, size_t maxBytes, size_t alreadyRead);
};

// Declare GzipOutputStream and GzipAsyncOutputStream based on the compressed output stream
// templates.
typedef CompOutputStream<_::GzipOutputContext, int, Z_SYNC_FLUSH, Z_NO_FLUSH,
                         Z_FINISH> GzipOutputStream;
typedef CompAsyncOutputStream<_::GzipOutputContext, int, Z_SYNC_FLUSH, Z_NO_FLUSH,
                         Z_FINISH> GzipAsyncOutputStream;

}  // namespace kj

KJ_END_HEADER
