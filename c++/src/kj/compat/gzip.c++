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

GzipOutputContext::PumpResult GzipOutputContext::pumpOnce(int flush, bool async) {
  ctx.next_out = buffer;
  ctx.avail_out = sizeof(buffer);

  auto result = compressing ? deflate(&ctx, flush) : inflate(&ctx, flush);

  bool done;
  switch (result) {
    case Z_OK:
    case Z_BUF_ERROR:
      // Z_OK means we made forward progress -- hopefully as much progress as possible given the
      // buffer states. Z_BUF_ERROR means we couldn't make progress either because the input buffer
      // is empty or the output buffer is full. From our point of view, these are really the same
      // state: we now need to attend to the buffers before making further progress.
      if (ctx.avail_in > 0) {
        // We didn't consume all input, so we definitely aren't done yet.
        done = false;
      } else {
        switch (flush) {
          case Z_NO_FLUSH:
            // Caller did not request a flush. The only important thing is that we consumed the
            // whole input buffer. It's possible that zlib could cough up some more data if we gave
            // it more output buffer space, but it's OK if we leave that for a later write.
            done = true;
            break;
          case Z_SYNC_FLUSH:
            // Caller asked for a flush. While all input was consumed, it's possible that zlib ran
            // out of output space before finalizing the flush, in which case we have to keep
            // pumping. If we *didn't* run out of output space... the documentation is ambiguous.
            // Probably in that case we're done, but the docs only really say that Z_OK means some
            // progress was made, not that all possible progress was made. We really need to run
            // deflate again until Z_BUF_ERROR occurs.
            if (ctx.avail_out == 0) {
              // We probably ran out of output space. We don't really know, and we have no way to
              // check without providing more space, so we'll need to pump again later.
              done = false;
            } else {
              // We didn't run out of space. The documentation is ambiguous here. Z_OK means
              // "progress was made", but not "all possible progress was made". But Z_SYNC_FLUSH is
              // documented as "all pending output is flushed to the output buffer". One might
              // argue that, to be correct, you really need to call deflate()/inflate() again and
              // check that it returns Z_BUF_ERROR to indicate that everything has been written.
              // However, it seems completely silly to imagine zlib would not have written
              // everything if it had space to do so. Plus, Z_SYNC_FLUSH is documented to add an
              // empty block in order to align to a byte boundary -- would calling Z_SYNC_FLUSH
              // again not cause another empty block?
              done = true;
            }
            break;
          case Z_FINISH:
            // Caller asked for EOF. If we were truly done, then we would have received
            // Z_STREAM_END.
            if (result == Z_BUF_ERROR && ctx.avail_out > 0) {
              // Hmm this shouldn't happen: We asked to finish, and we have available output buffer
              // space, but zlib says it can't make progress.
              if (compressing) {
                KJ_FAIL_ASSERT("Z_FINISH produced Z_BUF_ERROR despite having buffer space??");
              } else {
                // We're decompressing. zlib is telling us that the compressed data did not
                // actually end cleanly. We want to treat this the same as a read() in which
                // minBytes wasn't reached, so for async streams it is DISCONNECTED, while for
                // sync streams it's a require failure.
                if (async) {
                  kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED,
                      "gzip stream ended prematurely"));
                } else {
                  KJ_FAIL_REQUIRE("gzip stream ended prematurely") { break; }
                }

                // When exceptions are disabled we'll recover by signaling EOF.
                done = true;
              }
            } else {
              // Keep going until Z_STREAM_END.
              done = false;
            }
            break;
          default:
            KJ_FAIL_REQUIRE("unknown flush type", flush);
        }
      }
      break;
    case Z_STREAM_END:
      // EOF was reached and the stream is done.
      done = true;
      break;
    default:
      fail(result);
      KJ_UNREACHABLE;
  }

  return { kj::arrayPtr(buffer, ctx.next_out), done };
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
  for (;;) {
    auto result = ctx.pumpOnce(flush, false);
    inner.write(result.data.begin(), result.data.size());
    if (result.done) {
      break;
    }
  }
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

Promise<void> GzipAsyncOutputStream::write(
    WriteType type, ArrayPtr<const byte> data,
    ArrayPtr<const ArrayPtr<const byte>> moreData) {
  ctx.setInput(data.begin(), data.size());
  if (moreData.size() > 0) {
    return pump(WriteType::PARTIAL)
        .then([this, type, moreData]() {
      return write(type, moreData[0], moreData.slice(1));
    });
  } else {
    return pump(type);
  }
}

kj::Promise<void> GzipAsyncOutputStream::pump(WriteType type) {
  int flush = Z_NO_FLUSH;
  switch (type) {
    case WriteType::PARTIAL:
      flush = Z_NO_FLUSH;
      break;
    case WriteType::FLUSH:
      flush = Z_SYNC_FLUSH;
      break;
    case WriteType::END:
      flush = Z_FINISH;
      break;
  }

  auto result = ctx.pumpOnce(flush, true);
  auto promise = inner.write(result.done ? type : WriteType::PARTIAL, result.data);
  if (!result.done) {
    promise = promise.then([this, type]() { return pump(type); });
  }
  return promise;
}

}  // namespace kj

#endif  // KJ_HAS_ZLIB
