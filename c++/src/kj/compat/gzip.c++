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

#if KJ_HAS_ZLIB

#include "gzip.h"
#include <kj/debug.h>

namespace kj {

GzipAsyncInputStream::GzipAsyncInputStream(AsyncInputStream& inner)
    : inner(inner) {
  memset(&ctx, 0, sizeof(ctx));
  ctx.next_in = nullptr;
  ctx.avail_in = 0;
  ctx.next_out = nullptr;
  ctx.avail_out = 0;

  // windowBits = 15 (maximum) + magic value 16 to ask for gzip.
  KJ_ASSERT(inflateInit2(&ctx, 15 + 16) == Z_OK);
}

GzipAsyncInputStream::~GzipAsyncInputStream() noexcept(false) {
  inflateEnd(&ctx);
}

Promise<size_t> GzipAsyncInputStream::tryRead(void* out, size_t minBytes, size_t maxBytes) {
  if (maxBytes == 0) return size_t(0);

  return readImpl(reinterpret_cast<byte*>(out), minBytes, maxBytes, 0);
}

Promise<size_t> GzipAsyncInputStream::readImpl(
    byte* out, size_t minBytes, size_t maxBytes, size_t alreadyRead) {
  if (ctx.avail_in == 0) {
    return inner.tryRead(buffer, 1, sizeof(buffer))
        .then([this,out,minBytes,maxBytes,alreadyRead](size_t amount) -> Promise<size_t> {
      if (amount == 0) {
        KJ_REQUIRE(atValidEndpoint, "gzip compressed stream ended prematurely");
        return alreadyRead;
      } else {
        ctx.next_in = buffer;
        ctx.avail_in = amount;
        return readImpl(out, minBytes, maxBytes, alreadyRead);
      }
    });
  }

  ctx.next_out = reinterpret_cast<byte*>(out);
  ctx.avail_out = maxBytes;

  auto inflateResult = inflate(&ctx, Z_NO_FLUSH);
  atValidEndpoint = inflateResult == Z_STREAM_END;
  if (inflateResult == Z_OK || inflateResult == Z_STREAM_END) {
    if (atValidEndpoint && ctx.avail_in > 0) {
      // There's more data available. Assume start of new content.
      KJ_ASSERT(inflateReset(&ctx) == Z_OK);
    }

    size_t n = maxBytes - ctx.avail_out;
    if (n >= minBytes) {
      return n + alreadyRead;
    } else {
      return readImpl(out + n, minBytes - n, maxBytes - n, alreadyRead + n);
    }
  } else {
    if (ctx.msg == nullptr) {
      KJ_FAIL_REQUIRE("gzip decompression failed", inflateResult);
    } else {
      KJ_FAIL_REQUIRE("gzip decompression failed", ctx.msg);
    }
  }
}

// =======================================================================================

GzipAsyncOutputStream::GzipAsyncOutputStream(AsyncOutputStream& inner, int compressionLevel)
    : inner(inner) {
  memset(&ctx, 0, sizeof(ctx));
  ctx.next_in = nullptr;
  ctx.avail_in = 0;
  ctx.next_out = nullptr;
  ctx.avail_out = 0;

  int initResult =
      deflateInit2(&ctx, compressionLevel, Z_DEFLATED,
                   15 + 16,  // windowBits = 15 (maximum) + magic value 16 to ask for gzip.
                   8,        // memLevel = 8 (the default)
                   Z_DEFAULT_STRATEGY);
  KJ_ASSERT(initResult == Z_OK, initResult);
}

GzipAsyncOutputStream::~GzipAsyncOutputStream() noexcept(false) {
  deflateEnd(&ctx);
}

Promise<void> GzipAsyncOutputStream::write(const void* in, size_t size) {
  ctx.next_in = const_cast<byte*>(reinterpret_cast<const byte*>(in));
  ctx.avail_in = size;
  return pump();
}

Promise<void> GzipAsyncOutputStream::write(ArrayPtr<const ArrayPtr<const byte>> pieces) {
  KJ_REQUIRE(!ended, "already ended");

  if (pieces.size() == 0) return kj::READY_NOW;
  return write(pieces[0].begin(), pieces[0].size())
      .then([this,pieces]() {
    return write(pieces.slice(1, pieces.size()));
  });
}

Promise<void> GzipAsyncOutputStream::end() {
  KJ_REQUIRE(!ended, "already ended");

  ctx.next_out = buffer;
  ctx.avail_out = sizeof(buffer);

  auto deflateResult = deflate(&ctx, Z_FINISH);
  if (deflateResult == Z_OK || deflateResult == Z_STREAM_END) {
    size_t n = sizeof(buffer) - ctx.avail_out;
    auto promise = inner.write(buffer, n);
    if (deflateResult == Z_OK) {
      return promise.then([this]() { return end(); });
    } else {
      ended = true;
      return promise;
    }
  } else {
    if (ctx.msg == nullptr) {
      KJ_FAIL_REQUIRE("gzip compression failed", deflateResult);
    } else {
      KJ_FAIL_REQUIRE("gzip compression failed", ctx.msg);
    }
  }
}

kj::Promise<void> GzipAsyncOutputStream::pump() {
  if (ctx.avail_in == 0) {
    return kj::READY_NOW;
  }

  ctx.next_out = buffer;
  ctx.avail_out = sizeof(buffer);

  auto deflateResult = deflate(&ctx, Z_NO_FLUSH);
  if (deflateResult == Z_OK) {
    size_t n = sizeof(buffer) - ctx.avail_out;
    return inner.write(buffer, n)
        .then([this]() { return pump(); });
  } else {
    if (ctx.msg == nullptr) {
      KJ_FAIL_REQUIRE("gzip compression failed", deflateResult);
    } else {
      KJ_FAIL_REQUIRE("gzip compression failed", ctx.msg);
    }
  }
}

}  // namespace kj

#endif  // KJ_HAS_ZLIB
