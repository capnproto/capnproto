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

#include "serialize.h"
#include "layout.h"
#include <kj/debug.h>
#include <exception>

namespace capnp {

UnalignedFlatArrayMessageReader::UnalignedFlatArrayMessageReader(
    kj::ArrayPtr<const word> array, ReaderOptions options)
    : MessageReader(options), end(array.end()) {
  if (array.size() < 1) {
    // Assume empty message.
    return;
  }

  const _::WireValue<uint32_t>* table =
      reinterpret_cast<const _::WireValue<uint32_t>*>(array.begin());

  uint segmentCount = table[0].get() + 1;
  size_t offset = segmentCount / 2u + 1u;

  KJ_REQUIRE(array.size() >= offset, "Message ends prematurely in segment table.") {
    return;
  }

  {
    uint segmentSize = table[1].get();

    KJ_REQUIRE(array.size() >= offset + segmentSize,
               "Message ends prematurely in first segment.") {
      return;
    }

    segment0 = array.slice(offset, offset + segmentSize);
    offset += segmentSize;
  }

  if (segmentCount > 1) {
    moreSegments = kj::heapArray<kj::ArrayPtr<const word>>(segmentCount - 1);

    for (uint i = 1; i < segmentCount; i++) {
      uint segmentSize = table[i + 1].get();

      KJ_REQUIRE(array.size() >= offset + segmentSize, "Message ends prematurely.") {
        moreSegments = nullptr;
        return;
      }

      moreSegments[i - 1] = array.slice(offset, offset + segmentSize);
      offset += segmentSize;
    }
  }

  end = array.begin() + offset;
}

size_t expectedSizeInWordsFromPrefix(kj::ArrayPtr<const word> array) {
  if (array.size() < 1) {
    // All messages are at least one word.
    return 1;
  }

  const _::WireValue<uint32_t>* table =
      reinterpret_cast<const _::WireValue<uint32_t>*>(array.begin());

  uint segmentCount = table[0].get() + 1;
  size_t offset = segmentCount / 2u + 1u;

  // If the array is too small to contain the full segment table, truncate segmentCount to just
  // what is available.
  segmentCount = kj::min(segmentCount, array.size() * 2 - 1u);

  size_t totalSize = offset;
  for (uint i = 0; i < segmentCount; i++) {
    totalSize += table[i + 1].get();
  }
  return totalSize;
}

kj::ArrayPtr<const word> UnalignedFlatArrayMessageReader::getSegment(uint id) {
  if (id == 0) {
    return segment0;
  } else if (id <= moreSegments.size()) {
    return moreSegments[id - 1];
  } else {
    return nullptr;
  }
}

kj::ArrayPtr<const word> FlatArrayMessageReader::checkAlignment(kj::ArrayPtr<const word> array) {
  KJ_REQUIRE((uintptr_t)array.begin() % sizeof(void*) == 0,
      "Input to FlatArrayMessageReader is not aligned. If your architecture supports unaligned "
      "access (e.g. x86/x64/modern ARM), you may use UnalignedFlatArrayMessageReader instead, "
      "though this may harm performance.");

  return array;
}

kj::ArrayPtr<const word> initMessageBuilderFromFlatArrayCopy(
    kj::ArrayPtr<const word> array, MessageBuilder& target, ReaderOptions options) {
  FlatArrayMessageReader reader(array, options);
  target.setRoot(reader.getRoot<AnyPointer>());
  return kj::arrayPtr(reader.getEnd(), array.end());
}

kj::Array<word> messageToFlatArray(kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  kj::Array<word> result = kj::heapArray<word>(computeSerializedSizeInWords(segments));

  _::WireValue<uint32_t>* table =
      reinterpret_cast<_::WireValue<uint32_t>*>(result.begin());

  // We write the segment count - 1 because this makes the first word zero for single-segment
  // messages, improving compression.  We don't bother doing this with segment sizes because
  // one-word segments are rare anyway.
  table[0].set(segments.size() - 1);

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

  KJ_DASSERT(dst == result.end(), "Buffer overrun/underrun bug in code above.");

  return kj::mv(result);
}

size_t computeSerializedSizeInWords(kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  KJ_REQUIRE(segments.size() > 0, "Tried to serialize uninitialized message.");

  size_t totalSize = segments.size() / 2 + 1;

  for (auto& segment: segments) {
    totalSize += segment.size();
  }

  return totalSize;
}

// =======================================================================================

InputStreamMessageReader::InputStreamMessageReader(
    kj::InputStream& inputStream, ReaderOptions options, kj::ArrayPtr<word> scratchSpace)
    : MessageReader(options), inputStream(inputStream), readPos(nullptr) {
  _::WireValue<uint32_t> firstWord[2];

  inputStream.read(firstWord, sizeof(firstWord));

  uint segmentCount = firstWord[0].get() + 1;
  uint segment0Size = segmentCount == 0 ? 0 : firstWord[1].get();

  size_t totalWords = segment0Size;

  // Reject messages with too many segments for security reasons.
  KJ_REQUIRE(segmentCount < 512, "Message has too many segments.") {
    segmentCount = 1;
    segment0Size = 1;
    break;
  }

  // Read sizes for all segments except the first.  Include padding if necessary.
  KJ_STACK_ARRAY(_::WireValue<uint32_t>, moreSizes, segmentCount & ~1, 16, 64);
  if (segmentCount > 1) {
    inputStream.read(moreSizes.begin(), moreSizes.size() * sizeof(moreSizes[0]));
    for (uint i = 0; i < segmentCount - 1; i++) {
      totalWords += moreSizes[i].get();
    }
  }

  // Don't accept a message which the receiver couldn't possibly traverse without hitting the
  // traversal limit.  Without this check, a malicious client could transmit a very large segment
  // size to make the receiver allocate excessive space and possibly crash.
  KJ_REQUIRE(totalWords <= options.traversalLimitInWords,
             "Message is too large.  To increase the limit on the receiving end, see "
             "capnp::ReaderOptions.") {
    segmentCount = 1;
    segment0Size = kj::min(segment0Size, options.traversalLimitInWords);
    totalWords = segment0Size;
    break;
  }

  if (scratchSpace.size() < totalWords) {
    // TODO(perf):  Consider allocating each segment as a separate chunk to reduce memory
    //   fragmentation.
    ownedSpace = kj::heapArray<word>(totalWords);
    scratchSpace = ownedSpace;
  }

  segment0 = scratchSpace.slice(0, segment0Size);

  if (segmentCount > 1) {
    moreSegments = kj::heapArray<kj::ArrayPtr<const word>>(segmentCount - 1);
    size_t offset = segment0Size;

    for (uint i = 0; i < segmentCount - 1; i++) {
      uint segmentSize = moreSizes[i].get();
      moreSegments[i] = scratchSpace.slice(offset, offset + segmentSize);
      offset += segmentSize;
    }
  }

  if (segmentCount == 1) {
    inputStream.read(scratchSpace.begin(), totalWords * sizeof(word));
  } else if (segmentCount > 1) {
    readPos = scratchSpace.asBytes().begin();
    readPos += inputStream.read(readPos, segment0Size * sizeof(word), totalWords * sizeof(word));
  }
}

InputStreamMessageReader::~InputStreamMessageReader() noexcept(false) {
  if (readPos != nullptr) {
    unwindDetector.catchExceptionsIfUnwinding([&]() {
      // Note that lazy reads only happen when we have multiple segments, so moreSegments.back() is
      // valid.
      const byte* allEnd = reinterpret_cast<const byte*>(moreSegments.back().end());
      inputStream.skip(allEnd - readPos);
    });
  }
}

kj::ArrayPtr<const word> InputStreamMessageReader::getSegment(uint id) {
  if (id > moreSegments.size()) {
    return nullptr;
  }

  kj::ArrayPtr<const word> segment = id == 0 ? segment0 : moreSegments[id - 1];

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

void readMessageCopy(kj::InputStream& input, MessageBuilder& target,
                     ReaderOptions options, kj::ArrayPtr<word> scratchSpace) {
  InputStreamMessageReader message(input, options, scratchSpace);
  target.setRoot(message.getRoot<AnyPointer>());
}

// -------------------------------------------------------------------

void writeMessage(kj::OutputStream& output, kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  KJ_REQUIRE(segments.size() > 0, "Tried to serialize uninitialized message.");

  KJ_STACK_ARRAY(_::WireValue<uint32_t>, table, (segments.size() + 2) & ~size_t(1), 16, 64);

  // We write the segment count - 1 because this makes the first word zero for single-segment
  // messages, improving compression.  We don't bother doing this with segment sizes because
  // one-word segments are rare anyway.
  table[0].set(segments.size() - 1);
  for (uint i = 0; i < segments.size(); i++) {
    table[i + 1].set(segments[i].size());
  }
  if (segments.size() % 2 == 0) {
    // Set padding byte.
    table[segments.size() + 1].set(0);
  }

  KJ_STACK_ARRAY(kj::ArrayPtr<const byte>, pieces, segments.size() + 1, 4, 32);
  pieces[0] = table.asBytes();

  for (uint i = 0; i < segments.size(); i++) {
    pieces[i + 1] = segments[i].asBytes();
  }

  output.write(pieces);
}

// =======================================================================================

StreamFdMessageReader::~StreamFdMessageReader() noexcept(false) {}

void writeMessageToFd(int fd, kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  kj::FdOutputStream stream(fd);
  writeMessage(stream, segments);
}

void readMessageCopyFromFd(int fd, MessageBuilder& target,
                           ReaderOptions options, kj::ArrayPtr<word> scratchSpace) {
  kj::FdInputStream stream(fd);
  readMessageCopy(stream, target, options, scratchSpace);
}

}  // namespace capnp
