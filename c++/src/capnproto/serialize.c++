// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "serialize.h"
#include "wire-format.h"
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <string>
#include <sys/uio.h>

namespace capnproto {

FlatArrayMessageReader::FlatArrayMessageReader(ArrayPtr<const word> array, ReaderOptions options)
    : MessageReader(options) {
  if (array.size() < 1) {
    // Assume empty message.
    return;
  }

  const internal::WireValue<uint32_t>* table =
      reinterpret_cast<const internal::WireValue<uint32_t>*>(array.begin());

  uint segmentCount = table[0].get();
  size_t offset = segmentCount / 2u + 1u;

  if (array.size() < offset) {
    options.errorReporter->reportError("Message ends prematurely in segment table.");
    return;
  }

  if (segmentCount == 0) {
    return;
  }

  uint segmentSize = table[1].get();

  if (array.size() < offset + segmentSize) {
    options.errorReporter->reportError("Message ends prematurely in first segment.");
    return;
  }

  segment0 = array.slice(offset, offset + segmentSize);
  offset += segmentSize;

  if (segmentCount > 1) {
    moreSegments = newArray<ArrayPtr<const word>>(segmentCount - 1);

    for (uint i = 1; i < segmentCount; i++) {
      uint segmentSize = table[i + 1].get();

      if (array.size() < offset + segmentSize) {
        moreSegments = nullptr;
        options.errorReporter->reportError("Message ends prematurely.");
        return;
      }

      moreSegments[i - 1] = array.slice(offset, offset + segmentSize);
      offset += segmentSize;
    }
  }
}

ArrayPtr<const word> FlatArrayMessageReader::getSegment(uint id) {
  if (id == 0) {
    return segment0;
  } else if (id <= moreSegments.size()) {
    return moreSegments[id - 1];
  } else {
    return nullptr;
  }
}

Array<word> messageToFlatArray(ArrayPtr<const ArrayPtr<const word>> segments) {
  size_t totalSize = segments.size() / 2 + 1;

  for (auto& segment: segments) {
    totalSize += segment.size();
  }

  Array<word> result = newArray<word>(totalSize);

  internal::WireValue<uint32_t>* table =
      reinterpret_cast<internal::WireValue<uint32_t>*>(result.begin());

  table[0].set(segments.size());

  for (uint i = 0; i < segments.size(); i++) {
    table[i + 1].set(segments[i].size());
  }

  if (segments.size() % 2 == 0) {
    // Set padding byte.
    table[segments.size() + 1].set(0);
  }

  word* dst = result.begin() + segments.size() / 2 + 1;

  for (auto& segment: segments) {
    memcpy(dst, segment.begin(), segment.size() * sizeof(word));
    dst += segment.size();
  }

  CAPNPROTO_DEBUG_ASSERT(dst == result.end(), "Buffer overrun/underrun bug in code above.");

  return move(result);
}

// =======================================================================================

InputStream::~InputStream() {}
OutputStream::~OutputStream() {}

void InputStream::skip(size_t bytes) {
  char scratch[8192];
  while (bytes > 0) {
    size_t amount = std::min(bytes, sizeof(scratch));
    bytes -= read(scratch, amount, amount);
  }
}

void OutputStream::write(ArrayPtr<const ArrayPtr<const byte>> pieces) {
  for (auto piece: pieces) {
    write(piece.begin(), piece.size());
  }
}

// -------------------------------------------------------------------

InputStreamMessageReader::InputStreamMessageReader(
    InputStream& inputStream, ReaderOptions options, ArrayPtr<word> scratchSpace)
    : MessageReader(options), inputStream(inputStream), readPos(nullptr) {
  internal::WireValue<uint32_t> firstWord[2];

  inputStream.read(firstWord, sizeof(firstWord), sizeof(firstWord));

  uint segmentCount = firstWord[0].get();
  uint segment0Size = segmentCount == 0 ? 0 : firstWord[1].get();

  size_t totalWords = segment0Size;

  // Read sizes for all segments except the first.  Include padding if necessary.
  internal::WireValue<uint32_t> moreSizes[segmentCount & ~1];
  if (segmentCount > 1) {
    inputStream.read(moreSizes, sizeof(moreSizes), sizeof(moreSizes));
    for (uint i = 0; i < segmentCount - 1; i++) {
      totalWords += moreSizes[i].get();
    }
  }

  if (scratchSpace.size() < totalWords) {
    // TODO:  Consider allocating each segment as a separate chunk to reduce memory fragmentation.
    ownedSpace = newArray<word>(totalWords);
    scratchSpace = ownedSpace;
  }

  segment0 = scratchSpace.slice(0, segment0Size);

  if (segmentCount > 1) {
    moreSegments = newArray<ArrayPtr<const word>>(segmentCount - 1);
    size_t offset = segment0Size;

    for (uint i = 0; i < segmentCount - 1; i++) {
      uint segmentSize = moreSizes[i].get();
      moreSegments[i] = scratchSpace.slice(offset, offset + segmentSize);
      offset += segmentSize;
    }
  }

  if (segmentCount == 1) {
    inputStream.read(scratchSpace.begin(), totalWords * sizeof(word), totalWords * sizeof(word));
  } else if (segmentCount > 1) {
    readPos = reinterpret_cast<byte*>(scratchSpace.begin());
    readPos += inputStream.read(readPos, segment0Size * sizeof(word), totalWords * sizeof(word));
  }
}

InputStreamMessageReader::~InputStreamMessageReader() {
  if (readPos != nullptr) {
    // Note that lazy reads only happen when we have multiple segments, so moreSegments.back() is
    // valid.
    const byte* allEnd = reinterpret_cast<const byte*>(moreSegments.back().end());
    inputStream.skip(allEnd - readPos);
  }
}

ArrayPtr<const word> InputStreamMessageReader::getSegment(uint id) {
  if (id > moreSegments.size()) {
    return nullptr;
  }

  ArrayPtr<const word> segment = id == 0 ? segment0 : moreSegments[id - 1];

  if (readPos != nullptr) {
    // May need to lazily read more data.
    const byte* segmentEnd = reinterpret_cast<const byte*>(segment.end());
    if (readPos < segmentEnd) {
      // Note that lazy reads only happen when we have multiple segments, so moreSegments.back() is
      // valid.
      const byte* allEnd = reinterpret_cast<const byte*>(moreSegments.back().end());
      readPos += inputStream.read(readPos, segmentEnd - readPos, allEnd - readPos);
    }
  }

  return segment;
}

// -------------------------------------------------------------------

void writeMessage(OutputStream& output, ArrayPtr<const ArrayPtr<const word>> segments) {
  internal::WireValue<uint32_t> table[(segments.size() + 2) & ~size_t(1)];

  table[0].set(segments.size());
  for (uint i = 0; i < segments.size(); i++) {
    table[i + 1].set(segments[i].size());
  }
  if (segments.size() % 2 == 0) {
    // Set padding byte.
    table[segments.size() + 1].set(0);
  }

  ArrayPtr<const byte> pieces[segments.size() + 1];
  pieces[0] = arrayPtr(reinterpret_cast<byte*>(table), sizeof(table));

  for (uint i = 0; i < segments.size(); i++) {
    pieces[i + 1] = arrayPtr(reinterpret_cast<const byte*>(segments[i].begin()),
                             reinterpret_cast<const byte*>(segments[i].end()));
  }

  output.write(arrayPtr(pieces, segments.size() + 1));
}

// =======================================================================================

class OsException: public std::exception {
public:
  OsException(const char* function, int error) {
    char buffer[256];
    message = function;
    message += ": ";
    message.append(strerror_r(error, buffer, sizeof(buffer)));
  }
  ~OsException() noexcept {}

  const char* what() const noexcept override {
    return message.c_str();
  }

private:
  std::string message;
};

class PrematureEofException: public std::exception {
public:
  PrematureEofException() {}
  ~PrematureEofException() noexcept {}

  const char* what() const noexcept override {
    return "Stream ended prematurely.";
  }
};

AutoCloseFd::~AutoCloseFd() {
  if (fd >= 0 && close(fd) < 0) {
    if (std::uncaught_exception()) {
      // TODO:  Devise some way to report secondary errors during unwind.
    } else {
      throw OsException("close", errno);
    }
  }
}

FdInputStream::~FdInputStream() {}

size_t FdInputStream::read(void* buffer, size_t minBytes, size_t maxBytes) {
  byte* pos = reinterpret_cast<byte*>(buffer);
  byte* min = pos + minBytes;
  byte* max = pos + maxBytes;

  while (pos < min) {
    ssize_t n = ::read(fd, pos, max - pos);
    if (n <= 0) {
      if (n < 0) {
        int error = errno;
        if (error == EINTR) {
          continue;
        } else {
          throw OsException("read", error);
        }
      } else if (n == 0) {
        throw PrematureEofException();
      }
      return false;
    }

    pos += n;
  }

  return pos - reinterpret_cast<byte*>(buffer);
}

FdOutputStream::~FdOutputStream() {}

void FdOutputStream::write(const void* buffer, size_t size) {
  const char* pos = reinterpret_cast<const char*>(buffer);

  while (size > 0) {
    ssize_t n = ::write(fd, pos, size);
    if (n <= 0) {
      CAPNPROTO_ASSERT(n < 0, "write() returned zero.");
      throw OsException("write", errno);
    }
    pos += n;
    size -= n;
  }
}

void FdOutputStream::write(ArrayPtr<const ArrayPtr<const byte>> pieces) {
  struct iovec iov[pieces.size()];
  for (uint i = 0; i < pieces.size(); i++) {
    // writev() interface is not const-correct.  :(
    iov[i].iov_base = const_cast<byte*>(pieces[i].begin());
    iov[i].iov_len = pieces[i].size();
  }

  struct iovec* current = iov;
  struct iovec* end = iov + pieces.size();

  // Make sure we don't do anything on an empty write.
  while (current < end && current->iov_len == 0) {
    ++current;
  }

  while (current < end) {
    ssize_t n = ::writev(fd, iov, end - current);

    if (n <= 0) {
      if (n <= 0) {
        CAPNPROTO_ASSERT(n < 0, "write() returned zero.");
        throw OsException("writev", errno);
      }
    }

    while (static_cast<size_t>(n) >= current->iov_len) {
      n -= current->iov_len;
      ++current;
    }

    if (n > 0) {
      current->iov_base = reinterpret_cast<byte*>(current->iov_base) + n;
      current->iov_len -= n;
    }
  }
}

StreamFdMessageReader::~StreamFdMessageReader() {}

void writeMessageToFd(int fd, ArrayPtr<const ArrayPtr<const word>> segments) {
  FdOutputStream stream(fd);
  writeMessage(stream, segments);
}

}  // namespace capnproto
