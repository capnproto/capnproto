// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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

#include "serialize-packed.h"
#include <kj/debug.h>
#include "layout.h"
#include <vector>

namespace capnp {

namespace _ {  // private

PackedInputStream::PackedInputStream(kj::BufferedInputStream& inner): inner(inner) {}
PackedInputStream::~PackedInputStream() noexcept(false) {}

size_t PackedInputStream::tryRead(void* dst, size_t minBytes, size_t maxBytes) {
  if (maxBytes == 0) {
    return 0;
  }

  KJ_DREQUIRE(minBytes % sizeof(word) == 0, "PackedInputStream reads must be word-aligned.");
  KJ_DREQUIRE(maxBytes % sizeof(word) == 0, "PackedInputStream reads must be word-aligned.");

  uint8_t* __restrict__ out = reinterpret_cast<uint8_t*>(dst);
  uint8_t* const outEnd = reinterpret_cast<uint8_t*>(dst) + maxBytes;
  uint8_t* const outMin = reinterpret_cast<uint8_t*>(dst) + minBytes;

  kj::ArrayPtr<const byte> buffer = inner.tryGetReadBuffer();
  if (buffer.size() == 0) {
    return 0;
  }
  const uint8_t* __restrict__ in = reinterpret_cast<const uint8_t*>(buffer.begin());

#define REFRESH_BUFFER() \
  inner.skip(buffer.size()); \
  buffer = inner.getReadBuffer(); \
  KJ_REQUIRE(buffer.size() > 0, "Premature end of packed input.") { \
    return out - reinterpret_cast<uint8_t*>(dst); \
  } \
  in = reinterpret_cast<const uint8_t*>(buffer.begin())

#define BUFFER_END (reinterpret_cast<const uint8_t*>(buffer.end()))
#define BUFFER_REMAINING ((size_t)(BUFFER_END - in))

  for (;;) {
    uint8_t tag;

    KJ_DASSERT((out - reinterpret_cast<uint8_t*>(dst)) % sizeof(word) == 0,
           "Output pointer should always be aligned here.");

    if (BUFFER_REMAINING < 10) {
      if (out >= outMin) {
        // We read at least the minimum amount, so go ahead and return.
        inner.skip(in - reinterpret_cast<const uint8_t*>(buffer.begin()));
        return out - reinterpret_cast<uint8_t*>(dst);
      }

      if (BUFFER_REMAINING == 0) {
        REFRESH_BUFFER();
        continue;
      }

      // We have at least 1, but not 10, bytes available.  We need to read slowly, doing a bounds
      // check on each byte.

      tag = *in++;

      for (uint i = 0; i < 8; i++) {
        if (tag & (1u << i)) {
          if (BUFFER_REMAINING == 0) {
            REFRESH_BUFFER();
          }
          *out++ = *in++;
        } else {
          *out++ = 0;
        }
      }

      if (BUFFER_REMAINING == 0 && (tag == 0 || tag == 0xffu)) {
        REFRESH_BUFFER();
      }
    } else {
      tag = *in++;

#define HANDLE_BYTE(n) \
      { \
         bool isNonzero = (tag & (1u << n)) != 0; \
         *out++ = *in & (-(int8_t)isNonzero); \
         in += isNonzero; \
      }

      HANDLE_BYTE(0);
      HANDLE_BYTE(1);
      HANDLE_BYTE(2);
      HANDLE_BYTE(3);
      HANDLE_BYTE(4);
      HANDLE_BYTE(5);
      HANDLE_BYTE(6);
      HANDLE_BYTE(7);
#undef HANDLE_BYTE
    }

    if (tag == 0) {
      KJ_DASSERT(BUFFER_REMAINING > 0, "Should always have non-empty buffer here.");

      uint runLength = *in++ * sizeof(word);

      KJ_REQUIRE(runLength <= outEnd - out,
                 "Packed input did not end cleanly on a segment boundary.") {
        return out - reinterpret_cast<uint8_t*>(dst);
      }
      memset(out, 0, runLength);
      out += runLength;

    } else if (tag == 0xffu) {
      KJ_DASSERT(BUFFER_REMAINING > 0, "Should always have non-empty buffer here.");

      uint runLength = *in++ * sizeof(word);

      KJ_REQUIRE(runLength <= outEnd - out,
                 "Packed input did not end cleanly on a segment boundary.") {
        return out - reinterpret_cast<uint8_t*>(dst);
      }

      uint inRemaining = BUFFER_REMAINING;
      if (inRemaining >= runLength) {
        // Fast path.
        memcpy(out, in, runLength);
        out += runLength;
        in += runLength;
      } else {
        // Copy over the first buffer, then do one big read for the rest.
        memcpy(out, in, inRemaining);
        out += inRemaining;
        runLength -= inRemaining;

        inner.skip(buffer.size());
        inner.read(out, runLength);
        out += runLength;

        if (out == outEnd) {
          return maxBytes;
        } else {
          buffer = inner.getReadBuffer();
          in = reinterpret_cast<const uint8_t*>(buffer.begin());

          // Skip the bounds check below since we just did the same check above.
          continue;
        }
      }
    }

    if (out == outEnd) {
      inner.skip(in - reinterpret_cast<const uint8_t*>(buffer.begin()));
      return maxBytes;
    }
  }

  KJ_FAIL_ASSERT("Can't get here.");
  return 0;  // GCC knows KJ_FAIL_ASSERT doesn't return, but Eclipse CDT still warns...

#undef REFRESH_BUFFER
}

void PackedInputStream::skip(size_t bytes) {
  // We can't just read into buffers because buffers must end on block boundaries.

  if (bytes == 0) {
    return;
  }

  KJ_DREQUIRE(bytes % sizeof(word) == 0, "PackedInputStream reads must be word-aligned.");

  kj::ArrayPtr<const byte> buffer = inner.getReadBuffer();
  const uint8_t* __restrict__ in = reinterpret_cast<const uint8_t*>(buffer.begin());

#define REFRESH_BUFFER() \
  inner.skip(buffer.size()); \
  buffer = inner.getReadBuffer(); \
  KJ_REQUIRE(buffer.size() > 0, "Premature end of packed input.") { return; } \
  in = reinterpret_cast<const uint8_t*>(buffer.begin())

  for (;;) {
    uint8_t tag;

    if (BUFFER_REMAINING < 10) {
      if (BUFFER_REMAINING == 0) {
        REFRESH_BUFFER();
        continue;
      }

      // We have at least 1, but not 10, bytes available.  We need to read slowly, doing a bounds
      // check on each byte.

      tag = *in++;

      for (uint i = 0; i < 8; i++) {
        if (tag & (1u << i)) {
          if (BUFFER_REMAINING == 0) {
            REFRESH_BUFFER();
          }
          in++;
        }
      }
      bytes -= 8;

      if (BUFFER_REMAINING == 0 && (tag == 0 || tag == 0xffu)) {
        REFRESH_BUFFER();
      }
    } else {
      tag = *in++;

#define HANDLE_BYTE(n) \
      in += (tag & (1u << n)) != 0

      HANDLE_BYTE(0);
      HANDLE_BYTE(1);
      HANDLE_BYTE(2);
      HANDLE_BYTE(3);
      HANDLE_BYTE(4);
      HANDLE_BYTE(5);
      HANDLE_BYTE(6);
      HANDLE_BYTE(7);
#undef HANDLE_BYTE

      bytes -= 8;
    }

    if (tag == 0) {
      KJ_DASSERT(BUFFER_REMAINING > 0, "Should always have non-empty buffer here.");

      uint runLength = *in++ * sizeof(word);

      KJ_REQUIRE(runLength <= bytes, "Packed input did not end cleanly on a segment boundary.") {
        return;
      }

      bytes -= runLength;

    } else if (tag == 0xffu) {
      KJ_DASSERT(BUFFER_REMAINING > 0, "Should always have non-empty buffer here.");

      uint runLength = *in++ * sizeof(word);

      KJ_REQUIRE(runLength <= bytes, "Packed input did not end cleanly on a segment boundary.") {
        return;
      }

      bytes -= runLength;

      uint inRemaining = BUFFER_REMAINING;
      if (inRemaining > runLength) {
        // Fast path.
        in += runLength;
      } else {
        // Forward skip to the underlying stream.
        runLength -= inRemaining;
        inner.skip(buffer.size() + runLength);

        if (bytes == 0) {
          return;
        } else {
          buffer = inner.getReadBuffer();
          in = reinterpret_cast<const uint8_t*>(buffer.begin());

          // Skip the bounds check below since we just did the same check above.
          continue;
        }
      }
    }

    if (bytes == 0) {
      inner.skip(in - reinterpret_cast<const uint8_t*>(buffer.begin()));
      return;
    }
  }

  KJ_FAIL_ASSERT("Can't get here.");
}

// -------------------------------------------------------------------

PackedOutputStream::PackedOutputStream(kj::BufferedOutputStream& inner)
    : inner(inner) {}
PackedOutputStream::~PackedOutputStream() noexcept(false) {}

void PackedOutputStream::write(const void* src, size_t size) {
  kj::ArrayPtr<byte> buffer = inner.getWriteBuffer();
  byte slowBuffer[20];

  uint8_t* __restrict__ out = reinterpret_cast<uint8_t*>(buffer.begin());

  const uint8_t* __restrict__ in = reinterpret_cast<const uint8_t*>(src);
  const uint8_t* const inEnd = reinterpret_cast<const uint8_t*>(src) + size;

  while (in < inEnd) {
    if (reinterpret_cast<uint8_t*>(buffer.end()) - out < 10) {
      // Oops, we're out of space.  We need at least 10 bytes for the fast path, since we don't
      // bounds-check on every byte.

      // Write what we have so far.
      inner.write(buffer.begin(), out - reinterpret_cast<uint8_t*>(buffer.begin()));

      // Use a slow buffer into which we'll encode 10 to 20 bytes.  This should get us past the
      // output stream's buffer boundary.
      buffer = kj::arrayPtr(slowBuffer, sizeof(slowBuffer));
      out = reinterpret_cast<uint8_t*>(buffer.begin());
    }

    uint8_t* tagPos = out++;

#define HANDLE_BYTE(n) \
    uint8_t bit##n = *in != 0; \
    *out = *in; \
    out += bit##n; /* out only advances if the byte was non-zero */ \
    ++in

    HANDLE_BYTE(0);
    HANDLE_BYTE(1);
    HANDLE_BYTE(2);
    HANDLE_BYTE(3);
    HANDLE_BYTE(4);
    HANDLE_BYTE(5);
    HANDLE_BYTE(6);
    HANDLE_BYTE(7);
#undef HANDLE_BYTE

    uint8_t tag = (bit0 << 0) | (bit1 << 1) | (bit2 << 2) | (bit3 << 3)
                | (bit4 << 4) | (bit5 << 5) | (bit6 << 6) | (bit7 << 7);
    *tagPos = tag;

    if (tag == 0) {
      // An all-zero word is followed by a count of consecutive zero words (not including the
      // first one).

      // We can check a whole word at a time.
      const uint64_t* inWord = reinterpret_cast<const uint64_t*>(in);

      // The count must fit it 1 byte, so limit to 255 words.
      const uint64_t* limit = reinterpret_cast<const uint64_t*>(inEnd);
      if (limit - inWord > 255) {
        limit = inWord + 255;
      }

      while (inWord < limit && *inWord == 0) {
        ++inWord;
      }

      // Write the count.
      *out++ = inWord - reinterpret_cast<const uint64_t*>(in);

      // Advance input.
      in = reinterpret_cast<const uint8_t*>(inWord);

    } else if (tag == 0xffu) {
      // An all-nonzero word is followed by a count of consecutive uncompressed words, followed
      // by the uncompressed words themselves.

      // Count the number of consecutive words in the input which have no more than a single
      // zero-byte.  We look for at least two zeros because that's the point where our compression
      // scheme becomes a net win.
      // TODO(perf):  Maybe look for three zeros?  Compressing a two-zero word is a loss if the
      //   following word has no zeros.
      const uint8_t* runStart = in;

      const uint8_t* limit = inEnd;
      if ((size_t)(limit - in) > 255 * sizeof(word)) {
        limit = in + 255 * sizeof(word);
      }

      while (in < limit) {
        // Check eight input bytes for zeros.
        uint c = *in++ == 0;
        c += *in++ == 0;
        c += *in++ == 0;
        c += *in++ == 0;
        c += *in++ == 0;
        c += *in++ == 0;
        c += *in++ == 0;
        c += *in++ == 0;

        if (c >= 2) {
          // Un-read the word with multiple zeros, since we'll want to compress that one.
          in -= 8;
          break;
        }
      }

      // Write the count.
      uint count = in - runStart;
      *out++ = count / sizeof(word);

      if (count <= reinterpret_cast<uint8_t*>(buffer.end()) - out) {
        // There's enough space to memcpy.
        memcpy(out, runStart, count);
        out += count;
      } else {
        // Input overruns the output buffer.  We'll give it to the output stream in one chunk
        // and let it decide what to do.
        inner.write(buffer.begin(), reinterpret_cast<byte*>(out) - buffer.begin());
        inner.write(runStart, in - runStart);
        buffer = inner.getWriteBuffer();
        out = reinterpret_cast<uint8_t*>(buffer.begin());
      }
    }
  }

  // Write whatever is left.
  inner.write(buffer.begin(), reinterpret_cast<byte*>(out) - buffer.begin());
}

}  // namespace _ (private)

// =======================================================================================

PackedMessageReader::PackedMessageReader(
    kj::BufferedInputStream& inputStream, ReaderOptions options, kj::ArrayPtr<word> scratchSpace)
    : PackedInputStream(inputStream),
      InputStreamMessageReader(static_cast<PackedInputStream&>(*this), options, scratchSpace) {}

PackedMessageReader::~PackedMessageReader() noexcept(false) {}

PackedFdMessageReader::PackedFdMessageReader(
    int fd, ReaderOptions options, kj::ArrayPtr<word> scratchSpace)
    : FdInputStream(fd),
      BufferedInputStreamWrapper(static_cast<FdInputStream&>(*this)),
      PackedMessageReader(static_cast<BufferedInputStreamWrapper&>(*this),
                          options, scratchSpace) {}

PackedFdMessageReader::PackedFdMessageReader(
    kj::AutoCloseFd fd, ReaderOptions options, kj::ArrayPtr<word> scratchSpace)
    : FdInputStream(kj::mv(fd)),
      BufferedInputStreamWrapper(static_cast<FdInputStream&>(*this)),
      PackedMessageReader(static_cast<BufferedInputStreamWrapper&>(*this),
                          options, scratchSpace) {}

PackedFdMessageReader::~PackedFdMessageReader() noexcept(false) {}

void writePackedMessage(kj::BufferedOutputStream& output,
                        kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  _::PackedOutputStream packedOutput(output);
  writeMessage(packedOutput, segments);
}

void writePackedMessage(kj::OutputStream& output,
                        kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  KJ_IF_MAYBE(bufferedOutputPtr, kj::dynamicDowncastIfAvailable<kj::BufferedOutputStream>(output)) {
    writePackedMessage(*bufferedOutputPtr, segments);
  } else {
    byte buffer[8192];
    kj::BufferedOutputStreamWrapper bufferedOutput(output, kj::arrayPtr(buffer, sizeof(buffer)));
    writePackedMessage(bufferedOutput, segments);
  }
}

void writePackedMessageToFd(int fd, kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  kj::FdOutputStream output(fd);
  writePackedMessage(output, segments);
}

size_t computeUnpackedSizeInWords(kj::ArrayPtr<const byte> packedBytes) {
  const byte* ptr = packedBytes.begin();
  const byte* end = packedBytes.end();

  size_t total = 0;
  while (ptr < end) {
    uint tag = *ptr;
    size_t count = kj::popCount(tag);
    total += 1;
    KJ_REQUIRE(end - ptr >= count, "invalid packed data");
    ptr += count + 1;

    if (tag == 0) {
      KJ_REQUIRE(ptr < end, "invalid packed data");
      total += *ptr++;
    } else if (tag == 0xff) {
      KJ_REQUIRE(ptr < end, "invalid packed data");
      size_t words = *ptr++;
      total += words;
      size_t bytes = words * sizeof(word);
      KJ_REQUIRE(end - ptr >= bytes, "invalid packed data");
      ptr += bytes;
    }
  }

  return total;
}

}  // namespace capnp
