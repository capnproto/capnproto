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

#include "serialize-async.h"
#include <kj/debug.h>
#include <kj/io.h>

namespace capnp {

namespace {

class AsyncMessageReader: public MessageReader {
public:
  inline AsyncMessageReader(ReaderOptions options): MessageReader(options) {
    memset(firstWord, 0, sizeof(firstWord));
  }
  ~AsyncMessageReader() noexcept(false) {}

  kj::Promise<bool> read(kj::AsyncInputStream& inputStream, kj::ArrayPtr<word> scratchSpace);

  kj::Promise<kj::Maybe<size_t>> readWithFds(
      kj::AsyncCapabilityStream& inputStream,
      kj::ArrayPtr<kj::AutoCloseFd> fds, kj::ArrayPtr<word> scratchSpace);

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
      .then([this,&inputStream,KJ_CPCAP(scratchSpace)](size_t n) mutable -> kj::Promise<bool> {
    if (n == 0) {
      return false;
    } else if (n < sizeof(firstWord)) {
      // EOF in first word.
      kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "Premature EOF."));
      return false;
    }

    return readAfterFirstWord(inputStream, scratchSpace).then([]() { return true; });
  });
}

kj::Promise<kj::Maybe<size_t>> AsyncMessageReader::readWithFds(
    kj::AsyncCapabilityStream& inputStream, kj::ArrayPtr<kj::AutoCloseFd> fds,
    kj::ArrayPtr<word> scratchSpace) {
  return inputStream.tryReadWithFds(firstWord, sizeof(firstWord), sizeof(firstWord),
                                    fds.begin(), fds.size())
      .then([this,&inputStream,KJ_CPCAP(scratchSpace)]
            (kj::AsyncCapabilityStream::ReadResult result) mutable
            -> kj::Promise<kj::Maybe<size_t>> {
    if (result.byteCount == 0) {
      return kj::Maybe<size_t>(nullptr);
    } else if (result.byteCount < sizeof(firstWord)) {
      // EOF in first word.
      kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "Premature EOF."));
      return kj::Maybe<size_t>(nullptr);
    }

    return readAfterFirstWord(inputStream, scratchSpace)
        .then([result]() -> kj::Maybe<size_t> { return result.capCount; });
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
        .then([this,&inputStream,KJ_CPCAP(scratchSpace)]() mutable {
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
  return promise.then([reader = kj::mv(reader)](bool success) mutable -> kj::Own<MessageReader> {
    if (!success) {
      kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "Premature EOF."));
    }
    return kj::mv(reader);
  });
}

kj::Promise<kj::Maybe<kj::Own<MessageReader>>> tryReadMessage(
    kj::AsyncInputStream& input, ReaderOptions options, kj::ArrayPtr<word> scratchSpace) {
  auto reader = kj::heap<AsyncMessageReader>(options);
  auto promise = reader->read(input, scratchSpace);
  return promise.then([reader = kj::mv(reader)](bool success) mutable
                      -> kj::Maybe<kj::Own<MessageReader>> {
    if (success) {
      return kj::mv(reader);
    } else {
      return nullptr;
    }
  });
}

kj::Promise<MessageReaderAndFds> readMessage(
    kj::AsyncCapabilityStream& input, kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
    ReaderOptions options, kj::ArrayPtr<word> scratchSpace) {
  auto reader = kj::heap<AsyncMessageReader>(options);
  auto promise = reader->readWithFds(input, fdSpace, scratchSpace);
  return promise.then([reader = kj::mv(reader), fdSpace](kj::Maybe<size_t> nfds) mutable
                      -> MessageReaderAndFds {
    KJ_IF_MAYBE(n, nfds) {
      return { kj::mv(reader), fdSpace.slice(0, *n) };
    } else {
      kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "Premature EOF."));
      return { kj::mv(reader), nullptr };
    }
  });
}

kj::Promise<kj::Maybe<MessageReaderAndFds>> tryReadMessage(
    kj::AsyncCapabilityStream& input, kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
    ReaderOptions options, kj::ArrayPtr<word> scratchSpace) {
  auto reader = kj::heap<AsyncMessageReader>(options);
  auto promise = reader->readWithFds(input, fdSpace, scratchSpace);
  return promise.then([reader = kj::mv(reader), fdSpace](kj::Maybe<size_t> nfds) mutable
                      -> kj::Maybe<MessageReaderAndFds> {
    KJ_IF_MAYBE(n, nfds) {
      return MessageReaderAndFds { kj::mv(reader), fdSpace.slice(0, *n) };
    } else {
      return nullptr;
    }
  });
}

// =======================================================================================

namespace {

struct WriteArrays {
  // Holds arrays that must remain valid until a write completes.

  kj::Array<_::WireValue<uint32_t>> table;
  kj::Array<kj::ArrayPtr<const byte>> pieces;
};

template <typename WriteFunc>
kj::Promise<void> writeMessageImpl(kj::ArrayPtr<const kj::ArrayPtr<const word>> segments,
                                   WriteFunc&& writeFunc) {
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
  arrays.pieces[0] = arrays.table.asBytes();

  for (uint i = 0; i < segments.size(); i++) {
    arrays.pieces[i + 1] = segments[i].asBytes();
  }

  auto promise = writeFunc(arrays.pieces);

  // Make sure the arrays aren't freed until the write completes.
  return promise.then(kj::mvCapture(arrays, [](WriteArrays&&) {}));
}

}  // namespace

kj::Promise<void> writeMessage(kj::AsyncOutputStream& output,
                               kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  return writeMessageImpl(segments,
      [&](kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) {
    return output.write(pieces);
  });
}

kj::Promise<void> writeMessage(kj::AsyncCapabilityStream& output, kj::ArrayPtr<const int> fds,
                               kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  return writeMessageImpl(segments,
      [&](kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) {
    return output.writeWithFds(pieces[0], pieces.slice(1, pieces.size()), fds);
  });
}

}  // namespace capnp
