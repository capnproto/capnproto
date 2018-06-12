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

namespace _ {  // private

GzipOutputContext::GzipOutputContext(kj::Maybe<int> compressionLevel) {
  int initResult;

  KJ_IF_MAYBE(level, compressionLevel) {
    compressing = true;
    initResult =
      deflateInit2(&ctx, *level, Z_DEFLATED,
                   15 + 16,  // windowBits = 15 (maximum) + magic value 16 to ask for gzip.
                   8,        // memLevel = 8 (the default)
                   Z_DEFAULT_STRATEGY);
  } else {
    compressing = false;
    initResult = inflateInit2(&ctx, 15 + 16);
  }

  if (initResult != Z_OK) {
    fail(initResult);
  }
}

GzipOutputContext::~GzipOutputContext() noexcept(false) {
  compressing ? deflateEnd(&ctx) : inflateEnd(&ctx);
}

void GzipOutputContext::setInput(const void* in, size_t size) {
  ctx.next_in = const_cast<byte*>(reinterpret_cast<const byte*>(in));
  ctx.avail_in = size;
}

kj::Tuple<bool, kj::ArrayPtr<const byte>> GzipOutputContext::pumpOnce(int flush) {
  ctx.next_out = buffer;
  ctx.avail_out = sizeof(buffer);

  auto result = compressing ? deflate(&ctx, flush) : inflate(&ctx, flush);
  if (result != Z_OK && result != Z_BUF_ERROR && result != Z_STREAM_END) {
    fail(result);
  }

  // - Z_STREAM_END means we have finished the stream successfully.
  // - Z_BUF_ERROR means we didn't have any more input to process
  //   (but still have to make a call to write to potentially flush data).
  return kj::tuple(result == Z_OK, kj::arrayPtr(buffer, sizeof(buffer) - ctx.avail_out));
}

void GzipOutputContext::fail(int result) {
  auto header = compressing ? "gzip compression failed" : "gzip decompression failed";
  if (ctx.msg == nullptr) {
    KJ_FAIL_REQUIRE(header, result);
  } else {
    KJ_FAIL_REQUIRE(header, ctx.msg);
  }
}

}  // namespace _ (private)

GzipInputStream::GzipInputStream(InputStream& inner)
    : inner(inner) {
  // windowBits = 15 (maximum) + magic value 16 to ask for gzip.
  KJ_ASSERT(inflateInit2(&ctx, 15 + 16) == Z_OK);
}

GzipInputStream::~GzipInputStream() noexcept(false) {
  inflateEnd(&ctx);
}

size_t GzipInputStream::tryRead(void* out, size_t minBytes, size_t maxBytes) {
  if (maxBytes == 0) return size_t(0);

  return readImpl(reinterpret_cast<byte*>(out), minBytes, maxBytes, 0);
}

size_t GzipInputStream::readImpl(
    byte* out, size_t minBytes, size_t maxBytes, size_t alreadyRead) {
  if (ctx.avail_in == 0) {
    size_t amount = inner.tryRead(buffer, 1, sizeof(buffer));
    if (amount == 0) {
      if (!atValidEndpoint) {
        KJ_FAIL_REQUIRE("gzip compressed stream ended prematurely");
      }
      return alreadyRead;
    } else {
      ctx.next_in = buffer;
      ctx.avail_in = amount;
    }
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

GzipOutputStream::GzipOutputStream(OutputStream& inner, int compressionLevel)
    : inner(inner), ctx(compressionLevel) {}

GzipOutputStream::GzipOutputStream(OutputStream& inner, decltype(DECOMPRESS))
    : inner(inner), ctx(nullptr) {}

GzipOutputStream::~GzipOutputStream() noexcept(false) {
  pump(Z_FINISH);
}

void GzipOutputStream::write(const void* in, size_t size) {
  ctx.setInput(in, size);
  pump(Z_NO_FLUSH);
}

void GzipOutputStream::pump(int flush) {
  bool ok;
  do {
    auto result = ctx.pumpOnce(flush);
    ok = get<0>(result);
    auto chunk = get<1>(result);
    inner.write(chunk.begin(), chunk.size());
  } while (ok);
}

// =======================================================================================

GzipAsyncInputStream::GzipAsyncInputStream(AsyncInputStream& inner)
    : inner(inner) {
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
        if (!atValidEndpoint) {
          return KJ_EXCEPTION(DISCONNECTED, "gzip compressed stream ended prematurely");
        }
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
    : inner(inner), ctx(compressionLevel) {}

GzipAsyncOutputStream::GzipAsyncOutputStream(AsyncOutputStream& inner, decltype(DECOMPRESS))
    : inner(inner), ctx(nullptr) {}

Promise<void> GzipAsyncOutputStream::write(const void* in, size_t size) {
  ctx.setInput(in, size);
  return pump(Z_NO_FLUSH);
}

Promise<void> GzipAsyncOutputStream::write(ArrayPtr<const ArrayPtr<const byte>> pieces) {
  if (pieces.size() == 0) return kj::READY_NOW;
  return write(pieces[0].begin(), pieces[0].size())
      .then([this,pieces]() {
    return write(pieces.slice(1, pieces.size()));
  });
}

kj::Promise<void> GzipAsyncOutputStream::pump(int flush) {
  auto result = ctx.pumpOnce(flush);
  auto ok = get<0>(result);
  auto chunk = get<1>(result);
  auto promise = inner.write(chunk.begin(), chunk.size());
  if (ok) {
    promise = promise.then([this, flush]() { return pump(flush); });
  }
  return promise;
}

}  // namespace kj

#endif  // KJ_HAS_ZLIB
