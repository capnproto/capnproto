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
InputFile::~InputFile() {}
OutputStream::~OutputStream() {}

// -------------------------------------------------------------------

InputStreamMessageReader::InputStreamMessageReader(
    InputStream* inputStream, ReaderOptions options, InputStrategy inputStrategy)
    : MessageReader(options), inputStream(inputStream), inputStrategy(inputStrategy),
      segmentsReadSoFar(0) {
  switch (inputStrategy) {
    case InputStrategy::EAGER:
    case InputStrategy::LAZY:
      readNextInternal();
      break;
    case InputStrategy::EAGER_WAIT_FOR_READ_NEXT:
    case InputStrategy::LAZY_WAIT_FOR_READ_NEXT:
      break;
  }
}

void InputStreamMessageReader::readNext() {
  bool needReset = false;

  switch (inputStrategy) {
    case InputStrategy::LAZY:
      if (moreSegments != nullptr || segment0.size != 0) {
        // Make sure we've finished reading the previous message.
        // Note that this sort of defeats the purpose of lazy parsing.  In theory we could be a
        // little more efficient by reading into a stack-allocated scratch buffer rather than
        // allocating space for the remaining segments, but people really shouldn't be using
        // readNext() when lazy-parsing anyway.
        getSegment(moreSegments.size());
      }

      // no break

    case InputStrategy::EAGER:
      needReset = true;

      // TODO:  Save moreSegments for reuse?
      moreSegments = nullptr;

      segmentsReadSoFar = 0;
      segment0.size = 0;
      break;

    case InputStrategy::EAGER_WAIT_FOR_READ_NEXT:
      this->inputStrategy = InputStrategy::EAGER;
      break;
    case InputStrategy::LAZY_WAIT_FOR_READ_NEXT:
      this->inputStrategy = InputStrategy::LAZY;
      break;
  }

  if (inputStream != nullptr) {
    readNextInternal();
  }
  if (needReset) reset();
}

void InputStreamMessageReader::readNextInternal() {
  internal::WireValue<uint32_t> firstWord[2];

  if (!inputStream->read(firstWord, sizeof(firstWord))) return;

  uint segmentCount = firstWord[0].get();
  segment0.size = segmentCount == 0 ? 0 : firstWord[1].get();

  if (segmentCount > 1) {
    internal::WireValue<uint32_t> sizes[segmentCount - 1];
    if (!inputStream->read(sizes, sizeof(sizes))) return;

    moreSegments = newArray<LazySegment>(segmentCount - 1);
    for (uint i = 1; i < segmentCount; i++) {
      moreSegments[i - 1].size = sizes[i - 1].get();
    }
  }

  if (segmentCount % 2 == 0) {
    // Read the padding.
    uint32_t pad;
    if (!inputStream->read(&pad, sizeof(pad))) return;
  }

  if (inputStrategy == InputStrategy::EAGER) {
    getSegment(segmentCount - 1);
  }
}

ArrayPtr<const word> InputStreamMessageReader::getSegment(uint id) {
  if (id > moreSegments.size()) {
    return nullptr;
  }

  while (segmentsReadSoFar <= id && inputStream != nullptr) {
    LazySegment& segment = segmentsReadSoFar == 0 ? segment0 : moreSegments[segmentsReadSoFar - 1];
    if (segment.words.size() < segment.size) {
      segment.words = newArray<word>(segment.size);
    }

    if (!inputStream->read(segment.words.begin(), segment.size * sizeof(word))) {
      // There was an error but no exception was thrown, so we're supposed to plod along with
      // default values.  Discard the broken stream.
      inputStream = nullptr;
      break;
    }

    ++segmentsReadSoFar;
  }

  LazySegment& segment = id == 0 ? segment0 : moreSegments[id - 1];
  return segment.words.slice(0, segment.size);
}

// -------------------------------------------------------------------

InputFileMessageReader::InputFileMessageReader(
    InputFile* inputFile, ReaderOptions options, InputStrategy inputStrategy)
    : MessageReader(options), inputFile(inputFile) {
  internal::WireValue<uint32_t> firstWord[2];

  if (!inputFile->read(0, firstWord, sizeof(firstWord))) return;

  uint segmentCount = firstWord[0].get();
  segment0.size = segmentCount == 0 ? 0 : firstWord[1].get();

  if (segmentCount > 1) {
    internal::WireValue<uint32_t> sizes[segmentCount - 1];
    if (!inputFile->read(sizeof(firstWord), sizes, sizeof(sizes))) return;

    uint64_t offset = (segmentCount / 2 + 1) * sizeof(word);
    segment0.offset = offset;
    offset += segment0.size * sizeof(word);

    moreSegments = newArray<LazySegment>(segmentCount - 1);
    for (uint i = 1; i < segmentCount; i++) {
      uint segmentSize = sizes[i - 1].get();
      moreSegments[i - 1].size = segmentSize;
      moreSegments[i - 1].offset = offset;
      offset += segmentSize * sizeof(word);
    }
  } else {
    segment0.offset = sizeof(firstWord);
  }

  if (inputStrategy == InputStrategy::EAGER) {
    for (uint i = 0; i < segmentCount; i++) {
      getSegment(segmentCount);
    }
    inputFile = nullptr;
  }
}

ArrayPtr<const word> InputFileMessageReader::getSegment(uint id) {
  if (id > moreSegments.size()) {
    return nullptr;
  }

  LazySegment& segment = id == 0 ? segment0 : moreSegments[id - 1];

  if (segment.words == nullptr && segment.size > 0 && inputFile != nullptr) {
    Array<word> words = newArray<word>(segment.size);

    if (!inputFile->read(segment.offset, words.begin(), words.size() * sizeof(word))) {
      // There was an error but no exception was thrown, so we're supposed to plod along with
      // default values.  Discard the broken stream.
      inputFile = nullptr;
    } else {
      segment.words = move(words);
    }
  }

  return segment.words.asPtr();
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

  output.write(table, sizeof(table));

  for (auto& segment: segments) {
    output.write(segment.begin(), segment.size() * sizeof(word));
  }
}

// =======================================================================================

class OsException: public std::exception {
public:
  OsException(int error): error(error) {}
  ~OsException() noexcept {}

  const char* what() const noexcept override {
    // TODO:  Use strerror_r or whatever for thread-safety.  Ugh.
    return strerror(error);
  }

private:
  int error;
};

AutoCloseFd::~AutoCloseFd() {
  if (fd >= 0 && close(fd) < 0) {
    if (std::uncaught_exception()) {
      // TODO:  Devise some way to report secondary errors during unwind.
    } else {
      throw OsException(errno);
    }
  }
}

FdInputStream::~FdInputStream() {}

bool FdInputStream::read(void* buffer, size_t size) {
  char* pos = reinterpret_cast<char*>(buffer);

  while (size > 0) {
    ssize_t n = ::read(fd, pos, size);
    if (n <= 0) {
      if (n < 0) {
        // TODO:  Use strerror_r or whatever for thread-safety.  Ugh.
        errorReporter->reportError(strerror(errno));
      } else if (n == 0) {
        errorReporter->reportError("Stream ended prematurely.");
      }
      return false;
    }

    pos += n;
    size -= n;
  }

  return true;
}

FdInputFile::~FdInputFile() {}

bool FdInputFile::read(size_t offset, void* buffer, size_t size) {
  char* pos = reinterpret_cast<char*>(buffer);
  offset += this->offset;

  while (size > 0) {
    ssize_t n = ::pread(fd, pos, size, offset);
    if (n <= 0) {
      if (n < 0) {
        // TODO:  Use strerror_r or whatever for thread-safety.  Ugh.
        errorReporter->reportError(strerror(errno));
      } else if (n == 0) {
        errorReporter->reportError("Stream ended prematurely.");
      }
      return false;
    }

    pos += n;
    offset += n;
    size -= n;
  }

  return true;
}

FdOutputStream::~FdOutputStream() {}

void FdOutputStream::write(const void* buffer, size_t size) {
  const char* pos = reinterpret_cast<const char*>(buffer);

  while (size > 0) {
    ssize_t n = ::write(fd, pos, size);
    if (n <= 0) {
      throw OsException(n == 0 ? EIO : errno);
    }
    pos += n;
    size -= n;
  }
}

StreamFdMessageReader::~StreamFdMessageReader() {}
FileFdMessageReader::~FileFdMessageReader() {}

void writeMessageToFd(int fd, ArrayPtr<const ArrayPtr<const word>> segments) {
  FdOutputStream stream(fd);
  writeMessage(stream, segments);
}

}  // namespace capnproto
