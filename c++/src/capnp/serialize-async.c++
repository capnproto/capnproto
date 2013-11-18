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

#include "serialize-async.h"
#include <kj/debug.h>

namespace capnp {

namespace {

class AsyncMessageReader: public MessageReader {
public:
  inline AsyncMessageReader(ReaderOptions options): MessageReader(options) {
    memset(firstWord, 0, sizeof(firstWord));
  }
  ~AsyncMessageReader() noexcept(false) {}

  kj::Promise<bool> read(kj::AsyncInputStream& inputStream, kj::ArrayPtr<word> scratchSpace);

  // implements MessageReader ----------------------------------------

  kj::ArrayPtr<const word> getSegment(uint id) override {
    if (id >= segmentCount()) {
      return nullptr;
    } else {
      uint32_t size = id == 0 ? segment0Size() : moreSizes[id - 1].get();
      return kj::arrayPtr(segmentStarts[id], size);
    }
  }

private:
  _::WireValue<uint32_t> firstWord[2];
  kj::Array<_::WireValue<uint32_t>> moreSizes;
  kj::Array<const word*> segmentStarts;

  kj::Array<word> ownedSpace;
  // Only if scratchSpace wasn't big enough.

  inline uint segmentCount() { return firstWord[0].get() + 1; }
  inline uint segment0Size() { return firstWord[1].get(); }

  kj::Promise<void> readAfterFirstWord(
      kj::AsyncInputStream& inputStream, kj::ArrayPtr<word> scratchSpace);
  kj::Promise<void> readSegments(
      kj::AsyncInputStream& inputStream, kj::ArrayPtr<word> scratchSpace);
};

kj::Promise<bool> AsyncMessageReader::read(kj::AsyncInputStream& inputStream,
                                           kj::ArrayPtr<word> scratchSpace) {
  return inputStream.tryRead(firstWord, sizeof(firstWord), sizeof(firstWord))
      .then([this,&inputStream,scratchSpace](size_t n) mutable -> kj::Promise<bool> {
    if (n == 0) {
      return false;
    } else if (n < sizeof(firstWord)) {
      // EOF in first word.
      KJ_FAIL_REQUIRE("Premature EOF.") {
        return false;
      }
    }

    return readAfterFirstWord(inputStream, scratchSpace).then([]() { return true; });
  });
}

kj::Promise<void> AsyncMessageReader::readAfterFirstWord(kj::AsyncInputStream& inputStream,
                                                         kj::ArrayPtr<word> scratchSpace) {
  if (segmentCount() == 0) {
    firstWord[1].set(0);
  }

  // Reject messages with too many segments for security reasons.
  KJ_REQUIRE(segmentCount() < 512, "Message has too many segments.") {
    return kj::READY_NOW;  // exception will be propagated
  }

  if (segmentCount() > 1) {
    // Read sizes for all segments except the first.  Include padding if necessary.
    moreSizes = kj::heapArray<_::WireValue<uint32_t>>(segmentCount() & ~1);
    return inputStream.read(moreSizes.begin(), moreSizes.size() * sizeof(moreSizes[0]))
        .then([this,&inputStream,scratchSpace]() mutable {
          return readSegments(inputStream, scratchSpace);
        });
  } else {
    return readSegments(inputStream, scratchSpace);
  }
}

kj::Promise<void> AsyncMessageReader::readSegments(kj::AsyncInputStream& inputStream,
                                                   kj::ArrayPtr<word> scratchSpace) {
  size_t totalWords = segment0Size();

  if (segmentCount() > 1) {
    for (uint i = 0; i < segmentCount() - 1; i++) {
      totalWords += moreSizes[i].get();
    }
  }

  // Don't accept a message which the receiver couldn't possibly traverse without hitting the
  // traversal limit.  Without this check, a malicious client could transmit a very large segment
  // size to make the receiver allocate excessive space and possibly crash.
  KJ_REQUIRE(totalWords <= getOptions().traversalLimitInWords,
             "Message is too large.  To increase the limit on the receiving end, see "
             "capnp::ReaderOptions.") {
    return kj::READY_NOW;  // exception will be propagated
  }

  if (scratchSpace.size() < totalWords) {
    // TODO(perf):  Consider allocating each segment as a separate chunk to reduce memory
    //   fragmentation.
    ownedSpace = kj::heapArray<word>(totalWords);
    scratchSpace = ownedSpace;
  }

  segmentStarts = kj::heapArray<const word*>(segmentCount());

  segmentStarts[0] = scratchSpace.begin();

  if (segmentCount() > 1) {
    size_t offset = segment0Size();

    for (uint i = 1; i < segmentCount(); i++) {
      segmentStarts[i] = scratchSpace.begin() + offset;
      offset += moreSizes[i-1].get();
    }
  }

  return inputStream.read(scratchSpace.begin(), totalWords * sizeof(word));
}


}  // namespace

kj::Promise<kj::Own<MessageReader>> readMessage(
    kj::AsyncInputStream& input, ReaderOptions options, kj::ArrayPtr<word> scratchSpace) {
  auto reader = kj::heap<AsyncMessageReader>(options);
  auto promise = reader->read(input, scratchSpace);
  return promise.then(kj::mvCapture(reader, [](kj::Own<MessageReader>&& reader, bool success) {
    KJ_REQUIRE(success, "Premature EOF.") { break; }
    return kj::mv(reader);
  }));
}

kj::Promise<kj::Maybe<kj::Own<MessageReader>>> tryReadMessage(
    kj::AsyncInputStream& input, ReaderOptions options, kj::ArrayPtr<word> scratchSpace) {
  auto reader = kj::heap<AsyncMessageReader>(options);
  auto promise = reader->read(input, scratchSpace);
  return promise.then(kj::mvCapture(reader,
        [](kj::Own<MessageReader>&& reader, bool success) -> kj::Maybe<kj::Own<MessageReader>> {
    if (success) {
      return kj::mv(reader);
    } else {
      return nullptr;
    }
  }));
}

// =======================================================================================

namespace {

struct WriteArrays {
  // Holds arrays that must remain valid until a write completes.

  kj::Array<_::WireValue<uint32_t>> table;
  kj::Array<kj::ArrayPtr<const byte>> pieces;
};

}  // namespace

kj::Promise<void> writeMessage(kj::AsyncOutputStream& output,
                               kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  KJ_REQUIRE(segments.size() > 0, "Tried to serialize uninitialized message.");

  WriteArrays arrays;
  arrays.table = kj::heapArray<_::WireValue<uint32_t>>((segments.size() + 2) & ~size_t(1));

  // We write the segment count - 1 because this makes the first word zero for single-segment
  // messages, improving compression.  We don't bother doing this with segment sizes because
  // one-word segments are rare anyway.
  arrays.table[0].set(segments.size() - 1);
  for (uint i = 0; i < segments.size(); i++) {
    arrays.table[i + 1].set(segments[i].size());
  }
  if (segments.size() % 2 == 0) {
    // Set padding byte.
    arrays.table[segments.size() + 1].set(0);
  }

  arrays.pieces = kj::heapArray<kj::ArrayPtr<const byte>>(segments.size() + 1);
  arrays.pieces[0] = kj::arrayPtr(reinterpret_cast<byte*>(arrays.table.begin()),
                                  arrays.table.size() * sizeof(arrays.table[0]));

  for (uint i = 0; i < segments.size(); i++) {
    arrays.pieces[i + 1] = kj::arrayPtr(reinterpret_cast<const byte*>(segments[i].begin()),
                                        reinterpret_cast<const byte*>(segments[i].end()));
  }

  auto promise = output.write(arrays.pieces);

  // Make sure the arrays aren't freed until the write completes.
  return promise.then(kj::mvCapture(arrays, [](WriteArrays&&) {}));
}

}  // namespace capnp
