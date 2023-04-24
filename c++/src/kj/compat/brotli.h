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

#include "comp-stream-tpl.h"
#include <kj/io.h>
#include <kj/async-io.h>
#include <brotli/decode.h>
#include <brotli/encode.h>

KJ_BEGIN_HEADER

namespace kj {

// level 5 should offer a good default tradeoff based on concerns about being slower than gzip at
// e.g. level 6 and about compressing worse than gzip at lower levels. Note that
// BROTLI_DEFAULT_QUALITY is set to the maximum level of 11 – way too slow for on-the-fly
// compression.
constexpr size_t KJ_BROTLI_DEFAULT_QTY = 5;

namespace _ {  // private
// Use a window size of (1 << 19) = 512K by default. Higher values improve compression on longer
// streams but increase memory usage.
constexpr size_t KJ_BROTLI_DEFAULT_WBITS = 19;

// Maximum window size for streams to be decompressed, streams with larger windows are rejected.
// The default of (1 << BROTLI_DEFAULT_WINDOW) = 4MB should be useful to limit memory usage, but
// also means that some valid streams will be rejected.
constexpr size_t KJ_BROTLI_MAX_DEC_WBITS = BROTLI_DEFAULT_WINDOW;

// Use an output buffer size of 8K, larger sizes did not seem to significantly improve performance,
// perhaps due to brotli's internal output buffer.
constexpr size_t KJ_BROTLI_BUF_SIZE = 8192;

class BrotliOutputContext final {
public:
  BrotliOutputContext(kj::Maybe<int> compressionLevel, kj::Maybe<int> windowBits = nullptr);
  ~BrotliOutputContext() noexcept(false);
  KJ_DISALLOW_COPY_AND_MOVE(BrotliOutputContext);

  void setInput(const void* in, size_t size);
  // Flush the stream. Parameter is ignored for decoding as brotli only uses an operation parameter
  // during encoding.
  kj::Tuple<bool, kj::ArrayPtr<const byte>> pumpOnce(BrotliEncoderOperation flush);

private:
  bool compressing;
  int windowBits;
  const byte* next_in;
  size_t available_in;
  bool firstInput = true;

  BrotliEncoderState* cctx;
  BrotliDecoderState* dctx;
  byte buffer[_::KJ_BROTLI_BUF_SIZE];
};

}  // namespace _ (private)

class BrotliInputStream final: public InputStream {
public:
  BrotliInputStream(InputStream& inner);
  ~BrotliInputStream() noexcept(false);
  KJ_DISALLOW_COPY_AND_MOVE(BrotliInputStream);

  size_t tryRead(void* buffer, size_t minBytes, size_t maxBytes) override;

private:
  InputStream& inner;
  BrotliDecoderState* ctx;
  bool atValidEndpoint = false;

  byte buffer[_::KJ_BROTLI_BUF_SIZE];

  const byte* next_in;
  size_t available_in;
  bool firstInput = true;

  size_t readImpl(byte* buffer, size_t minBytes, size_t maxBytes, size_t alreadyRead);
};

class BrotliAsyncInputStream final: public AsyncInputStream {
public:
  BrotliAsyncInputStream(AsyncInputStream& inner);
  ~BrotliAsyncInputStream() noexcept(false);
  KJ_DISALLOW_COPY_AND_MOVE(BrotliAsyncInputStream);

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override;

private:
  AsyncInputStream& inner;
  BrotliDecoderState* ctx;
  bool atValidEndpoint = false;

  byte buffer[_::KJ_BROTLI_BUF_SIZE];
  const byte* next_in;
  size_t available_in;
  bool firstInput = true;

  Promise<size_t> readImpl(byte* buffer, size_t minBytes, size_t maxBytes, size_t alreadyRead);
};

// Declare BrotliOutputStream and BrotliAsyncOutputStream based on the compressed output stream
// templates. The template parameters allow us to specify the right flush operation for brotli,
// which is different than in the zlib API.
typedef CompOutputStream<_::BrotliOutputContext, BrotliEncoderOperation, BROTLI_OPERATION_FLUSH,
                         BROTLI_OPERATION_PROCESS, BROTLI_OPERATION_FINISH> BrotliOutputStream;
typedef CompAsyncOutputStream<_::BrotliOutputContext, BrotliEncoderOperation,
                              BROTLI_OPERATION_FLUSH, BROTLI_OPERATION_PROCESS,
                              BROTLI_OPERATION_FINISH> BrotliAsyncOutputStream;

}  // namespace kj

KJ_END_HEADER
