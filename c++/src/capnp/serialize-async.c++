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

// Includes just for need SOL_SOCKET and SO_SNDBUF
#if _WIN32
#include <kj/win32-api-version.h>

#include <winsock2.h>
#include <mswsock.h>
#include <kj/windows-sanity.h>
#else
#include <sys/socket.h>
#endif

#include "serialize-async.h"
#include "serialize.h"
#include <kj/debug.h>
#include <kj/io.h>
#include <kj/encoding.h>

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

inline size_t tableSizeForSegments(size_t segmentsSize) {
  return (segmentsSize + 2) & ~size_t(1);
}

// Helper function that allocates and fills the pointed-to table with info about the segments and
// populates the pieces array with pointers to the segments.
void fillWriteArraysWithMessage(kj::ArrayPtr<const kj::ArrayPtr<const word>> segments,
                                kj::ArrayPtr<_::WireValue<uint32_t>> table,
                                kj::ArrayPtr<kj::ArrayPtr<const byte>> pieces) {
  KJ_REQUIRE(segments.size() > 0, "Tried to serialize uninitialized message.");

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

  KJ_ASSERT(pieces.size() == segments.size() + 1, "incorrectly sized pieces array during write");
  pieces[0] = table.asBytes();
  for (uint i = 0; i < segments.size(); i++) {
    pieces[i + 1] = segments[i].asBytes();
  }
}

template <typename WriteFunc>
kj::Promise<void> writeMessageImpl(kj::ArrayPtr<const kj::ArrayPtr<const word>> segments,
                                   WriteFunc&& writeFunc) {
  KJ_REQUIRE(segments.size() > 0, "Tried to serialize uninitialized message.");

  WriteArrays arrays;
  arrays.table = kj::heapArray<_::WireValue<uint32_t>>(tableSizeForSegments(segments.size()));
  arrays.pieces = kj::heapArray<kj::ArrayPtr<const byte>>(segments.size() + 1);
  fillWriteArraysWithMessage(segments, arrays.table, arrays.pieces);

  auto promise = writeFunc(arrays.pieces);

  // Make sure the arrays aren't freed until the write completes.
  return promise.then(kj::mvCapture(arrays, [](WriteArrays&&) {}));
}

template <typename WriteFunc>
kj::Promise<void> writeMessagesImpl(
    kj::ArrayPtr<kj::ArrayPtr<const kj::ArrayPtr<const word>>> messages, WriteFunc&& writeFunc) {
  KJ_REQUIRE(messages.size() > 0, "Tried to serialize zero messages.");

  // Determine how large the shared table and pieces arrays needs to be.
  size_t tableSize = 0;
  size_t piecesSize = 0;
  for (auto& segments : messages) {
    tableSize += tableSizeForSegments(segments.size());
    piecesSize += segments.size() + 1;
  }
  auto table = kj::heapArray<_::WireValue<uint32_t>>(tableSize);
  auto pieces = kj::heapArray<kj::ArrayPtr<const byte>>(piecesSize);

  size_t tableValsWritten = 0;
  size_t piecesWritten = 0;
  for (auto i : kj::indices(messages)) {
    const size_t tableValsToWrite = tableSizeForSegments(messages[i].size());
    const size_t piecesToWrite = messages[i].size() + 1;
    fillWriteArraysWithMessage(
        messages[i],
        table.slice(tableValsWritten, tableValsWritten + tableValsToWrite),
        pieces.slice(piecesWritten, piecesWritten + piecesToWrite));
    tableValsWritten += tableValsToWrite;
    piecesWritten += piecesToWrite;
  }

  auto promise = writeFunc(pieces);
  return promise.attach(kj::mv(table), kj::mv(pieces));
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

kj::Promise<void> writeMessages(
    kj::AsyncOutputStream& output,
    kj::ArrayPtr<kj::ArrayPtr<const kj::ArrayPtr<const word>>> messages) {
  return writeMessagesImpl(messages,
      [&](kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) {
    return output.write(pieces);
  });
}

kj::Promise<void> writeMessages(
    kj::AsyncOutputStream& output, kj::ArrayPtr<MessageBuilder*> builders) {
  auto messages = kj::heapArray<kj::ArrayPtr<const kj::ArrayPtr<const word>>>(builders.size());
  for (auto i : kj::indices(builders)) {
    messages[i] = builders[i]->getSegmentsForOutput();
  }
  return writeMessages(output, messages);
}

kj::Promise<void> MessageStream::writeMessages(kj::ArrayPtr<MessageBuilder*> builders) {
  auto messages = kj::heapArray<kj::ArrayPtr<const kj::ArrayPtr<const word>>>(builders.size());
  for (auto i : kj::indices(builders)) {
    messages[i] = builders[i]->getSegmentsForOutput();
  }
  return writeMessages(messages);
}

AsyncIoMessageStream::AsyncIoMessageStream(kj::AsyncIoStream& stream)
  : stream(stream) {};

kj::Promise<kj::Maybe<MessageReaderAndFds>> AsyncIoMessageStream::tryReadMessage(
    kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
    ReaderOptions options,
    kj::ArrayPtr<word> scratchSpace) {
  return capnp::tryReadMessage(stream, options, scratchSpace)
    .then([](kj::Maybe<kj::Own<MessageReader>> maybeReader) -> kj::Maybe<MessageReaderAndFds> {
      KJ_IF_MAYBE(reader, maybeReader) {
        return MessageReaderAndFds { kj::mv(*reader), nullptr };
      } else {
        return nullptr;
      }
    });
}

kj::Promise<void> AsyncIoMessageStream::writeMessage(
    kj::ArrayPtr<const int> fds,
    kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  return capnp::writeMessage(stream, segments);
}

kj::Promise<void> AsyncIoMessageStream::writeMessages(
    kj::ArrayPtr<kj::ArrayPtr<const kj::ArrayPtr<const word>>> messages) {
  return capnp::writeMessages(stream, messages);
}

kj::Maybe<int> getSendBufferSize(kj::AsyncIoStream& stream) {
  // TODO(perf): It might be nice to have a tryGetsockopt() that doesn't require catching
  //   exceptions?
  int bufSize = 0;
  KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
    uint len = sizeof(int);
    stream.getsockopt(SOL_SOCKET, SO_SNDBUF, &bufSize, &len);
    KJ_ASSERT(len == sizeof(bufSize)) { break; }
  })) {
    if (exception->getType() != kj::Exception::Type::UNIMPLEMENTED) {
      // TODO(someday): Figure out why getting SO_SNDBUF sometimes throws EINVAL. I suspect it
      //   happens when the remote side has closed their read end, meaning we no longer have
      //   a send buffer, but I don't know what is the best way to verify that that was actually
      //   the reason. I'd prefer not to ignore EINVAL errors in general.

      // kj::throwRecoverableException(kj::mv(*exception));
    }
    return nullptr;
  }
  return bufSize;
}

kj::Promise<void> AsyncIoMessageStream::end() {
  stream.shutdownWrite();
  return kj::READY_NOW;
}

kj::Maybe<int> AsyncIoMessageStream::getSendBufferSize() {
  return capnp::getSendBufferSize(stream);
}

AsyncCapabilityMessageStream::AsyncCapabilityMessageStream(kj::AsyncCapabilityStream& stream)
  : stream(stream) {};

kj::Promise<kj::Maybe<MessageReaderAndFds>> AsyncCapabilityMessageStream::tryReadMessage(
    kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
    ReaderOptions options,
    kj::ArrayPtr<word> scratchSpace) {
  return capnp::tryReadMessage(stream, fdSpace, options, scratchSpace);
}

kj::Promise<void> AsyncCapabilityMessageStream::writeMessage(
    kj::ArrayPtr<const int> fds,
    kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  return capnp::writeMessage(stream, fds, segments);
}

kj::Promise<void> AsyncCapabilityMessageStream::writeMessages(
    kj::ArrayPtr<kj::ArrayPtr<const kj::ArrayPtr<const word>>> messages) {
  return capnp::writeMessages(stream, messages);
}

kj::Maybe<int> AsyncCapabilityMessageStream::getSendBufferSize() {
  return capnp::getSendBufferSize(stream);
}

kj::Promise<void> AsyncCapabilityMessageStream::end() {
  stream.shutdownWrite();
  return kj::READY_NOW;
}

kj::Promise<kj::Own<MessageReader>> MessageStream::readMessage(
    ReaderOptions options,
    kj::ArrayPtr<word> scratchSpace) {
  return tryReadMessage(options, scratchSpace).then([](kj::Maybe<kj::Own<MessageReader>> maybeResult) {
    KJ_IF_MAYBE(result, maybeResult) {
        return kj::mv(*result);
    } else {
        kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "Premature EOF."));
        KJ_UNREACHABLE;
    }
  });
}

kj::Promise<kj::Maybe<kj::Own<MessageReader>>> MessageStream::tryReadMessage(
    ReaderOptions options,
    kj::ArrayPtr<word> scratchSpace) {
  return tryReadMessage(nullptr, options, scratchSpace)
    .then([](auto maybeReaderAndFds) -> kj::Maybe<kj::Own<MessageReader>> {
      KJ_IF_MAYBE(readerAndFds, maybeReaderAndFds) {
        return kj::mv(readerAndFds->reader);
      } else {
        return nullptr;
      }
  });
}

kj::Promise<MessageReaderAndFds> MessageStream::readMessage(
    kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
    ReaderOptions options, kj::ArrayPtr<word> scratchSpace) {
  return tryReadMessage(fdSpace, options, scratchSpace).then([](auto maybeResult) {
      KJ_IF_MAYBE(result, maybeResult) {
        return kj::mv(*result);
      } else {
        kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "Premature EOF."));
        KJ_UNREACHABLE;
      }
  });
}

template<typename Stream>
kj::Promise<kj::Maybe<MessageReaderAndFds>> _::tryReadMessageInternal(Stream& stream,
    uint64_t defaultReadSize, kj::ArrayPtr<kj::AutoCloseFd> passedInFdSpace,
    ReaderOptions readerOptions, kj::ArrayPtr<word> scratchSpace) {
  KJ_IF_MAYBE(m, stream.tryGetBufferedMessage(passedInFdSpace)) {
    return kj::Maybe<MessageReaderAndFds>(kj::mv(*m));
  }

  auto prefixSize = 0;
  kj::Array<byte> buffer;
  void* start;
  size_t minBytes;
  size_t maxBytes;

  auto incomplete = stream.tryGetIncompleteRead();
  KJ_IF_MAYBE(i, incomplete) {
    // We take the incomplete read and put it as a prefix in the readBuffer.

    KJ_ASSERT(i->bytes.size() < i->expectedSize);
    prefixSize = i->bytes.size();

    // The buffer needs to be at least as big as the expected message size, so if the message is
    // larger than defaultReadSize, we need to use that
    auto bufferSize = kj::max(i->expectedSize, defaultReadSize);

    buffer = kj::heapArray<byte>(bufferSize);
    memcpy(static_cast<void*>(buffer.begin()), static_cast<void*>(i->bytes.begin()), prefixSize);

    start = buffer.begin() + prefixSize;
    minBytes = i->expectedSize - prefixSize;
    maxBytes = bufferSize - prefixSize;
  } else {
    buffer = kj::heapArray<byte>(defaultReadSize);
    start = buffer.begin();
    minBytes = 1;
    maxBytes = defaultReadSize;
  }

  return stream.tryRead(kj::mv(buffer), start, minBytes, maxBytes).then(
      [&stream, defaultReadSize, readerOptions, prefixSize, passedInFdSpace]
      (typename Stream::ReadResult result) mutable
      -> kj::Promise<kj::Maybe<MessageReaderAndFds>> {
    auto n = result.byteCount;

    if (n == 0) {
      if (prefixSize) {
        kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "Premature EOF."));
      }

      // EOF, return a buffered message if we have one.
      KJ_IF_MAYBE(m, stream.tryGetBufferedMessage(passedInFdSpace)) {
        return kj::Maybe<MessageReaderAndFds>(kj::mv(*m));
      }
      return kj::Maybe<MessageReaderAndFds>();
    }

    n += prefixSize;

    kj::ArrayPtr<word> readHead(reinterpret_cast<word*>(result.buffer.begin()), n / sizeof(word));

    // Any bytes not on a word boundary at the end of the read are part of an incomplete message.
    kj::Maybe<kj::ArrayPtr<byte>> leftoverBytes;
    auto numLeftoverBytes = n % sizeof(word);
    if (numLeftoverBytes > 0) {
      leftoverBytes = kj::ArrayPtr<byte>(result.buffer.begin() + (n - numLeftoverBytes), numLeftoverBytes);
    }

    while(readHead.size() > 0) {
      auto totalWords = capnp::expectedSizeInWordsFromPrefix(readHead);
      KJ_REQUIRE(totalWords < readerOptions.traversalLimitInWords);

      if (totalWords > readHead.size()) {
        // The last message isn't complete

        leftoverBytes = kj::ArrayPtr<byte>(reinterpret_cast<byte*>(readHead.begin()),
            (readHead.size() * sizeof(word)) + numLeftoverBytes);
        break;
      }

      // We have the complete message
      auto messageCopy = kj::heapArray<word>(totalWords);
      memcpy(messageCopy.begin(), readHead.begin(), totalWords * sizeof(word));

      bool isLast = totalWords == readHead.size();

      stream.bufferCompleteMessage({
        kj::attachVal(FlatArrayMessageReader(messageCopy, readerOptions),
            kj::mv(messageCopy)),
        result.getFds(isLast)
      });
      readHead = readHead.slice(totalWords, readHead.size());
    }

    KJ_IF_MAYBE(leftover, leftoverBytes) {
      kj::ArrayPtr<word> leftoverWords(reinterpret_cast<word*>(leftover->begin()),
          leftover->size() / sizeof(word));

      auto expectedSize = capnp::expectedSizeInWordsFromPrefix(leftoverWords) * sizeof(word);

      stream.bufferIncompleteMessage(*leftover, result.getFds(true), expectedSize);
    }

    return tryReadMessageInternal(stream, defaultReadSize, passedInFdSpace, readerOptions, nullptr);
  });
}

template kj::Promise<kj::Maybe<MessageReaderAndFds>>
      _::tryReadMessageInternal<BufferedAsyncIoMessageStream>(
      BufferedAsyncIoMessageStream& stream,
      uint64_t defaultReadSize, kj::ArrayPtr<kj::AutoCloseFd> passedInFdSpace,
      ReaderOptions readerOptions, kj::ArrayPtr<word> scratchSpace);

template kj::Promise<kj::Maybe<MessageReaderAndFds>>
      _::tryReadMessageInternal<BufferedAsyncCapabilityMessageStream>(
      BufferedAsyncCapabilityMessageStream& stream,
      uint64_t defaultReadSize, kj::ArrayPtr<kj::AutoCloseFd> passedInFdSpace,
      ReaderOptions readerOptions, kj::ArrayPtr<word> scratchSpace);

kj::Promise<BufferedAsyncIoMessageStream::ReadResult>
    BufferedAsyncIoMessageStream::tryRead(kj::Array<byte> buffer, void* start, size_t minBytes,
      size_t maxBytes) {
  return stream.tryRead(start, minBytes, maxBytes).then(
      [buffer = kj::mv(buffer)](size_t n) mutable {
    return ReadResult { kj::mv(buffer), n };
  });
}

kj::Maybe<MessageReaderAndFds> BufferedAsyncIoMessageStream::tryGetBufferedMessage(
    kj::ArrayPtr<kj::AutoCloseFd>) {
  if (bufferedReaders.size() > 0) {
    auto next = kj::mv(bufferedReaders.front());
    bufferedReaders.pop_front();
    return MessageReaderAndFds { kj::mv(next), nullptr };
  }
  return nullptr;
}

kj::Maybe<BufferedAsyncIoMessageStream::IncompleteRead>
    BufferedAsyncIoMessageStream::tryGetIncompleteRead() {
  return kj::mv(incompleteRead);
}

void BufferedAsyncIoMessageStream::bufferCompleteMessage(MessageReaderAndOwnedFds message) {
  bufferedReaders.push_back(kj::mv(message.reader));
}

void BufferedAsyncIoMessageStream::bufferIncompleteMessage(
    kj::ArrayPtr<kj::byte> leftover, kj::Array<kj::AutoCloseFd> fds, size_t size) {
  kj::ArrayPtr<word> leftoverWords(reinterpret_cast<word*>(leftover.begin()),
      leftover.size() / sizeof(word));

  auto& incomplete = incompleteRead.emplace(IncompleteRead {
    kj::heapArray<byte>(leftover.size()),
    capnp::expectedSizeInWordsFromPrefix(leftoverWords) * sizeof(word)
  });
  memcpy(incomplete.bytes.begin(), leftover.begin(), leftover.size());
}

kj::Promise<BufferedAsyncCapabilityMessageStream::ReadResult>
    BufferedAsyncCapabilityMessageStream::tryRead(kj::Array<byte> buffer, void* start,
      size_t minBytes, size_t maxBytes) {
  auto fdSpace = kj::heapArray<kj::AutoCloseFd>(options.maxFdsPerMessage);
  return stream.tryReadWithFds(start, minBytes, maxBytes, fdSpace.begin(),
      options.maxFdsPerMessage).then(
      [this, buffer = kj::mv(buffer), fdSpace = kj::mv(fdSpace)]
      (kj::AsyncCapabilityStream::ReadResult result) mutable {
    return ReadResult(kj::mv(buffer), kj::mv(fdSpace), kj::mv(incompleteReadFds),
        result.byteCount, result.capCount);
  });
}

kj::Maybe<MessageReaderAndFds> BufferedAsyncCapabilityMessageStream::tryGetBufferedMessage(
    kj::ArrayPtr<kj::AutoCloseFd> fdSpace) {
  if (bufferedReaders.size() > 0) {
    auto next = kj::mv(bufferedReaders.front());
    bufferedReaders.pop_front();

    for(auto i : kj::zeroTo(next.fds.size())) {
      if (i >= fdSpace.size()) {
        break;
      }
      fdSpace[i] = kj::mv(next.fds[i]);
    }


    return MessageReaderAndFds {
      kj::mv(next.reader),
      fdSpace.slice(0, kj::min(fdSpace.size(), next.fds.size()))
    };
  } else {
    return nullptr;
  }
}

kj::Maybe<BufferedAsyncCapabilityMessageStream::IncompleteRead>
    BufferedAsyncCapabilityMessageStream::tryGetIncompleteRead() {
  return kj::mv(incompleteRead);
}

void BufferedAsyncCapabilityMessageStream::bufferCompleteMessage(MessageReaderAndOwnedFds message) {
  bufferedReaders.push_back(kj::mv(message));
}

void BufferedAsyncCapabilityMessageStream::bufferIncompleteMessage(
    kj::ArrayPtr<kj::byte> leftover, kj::Array<kj::AutoCloseFd> fds, size_t size) {
  kj::ArrayPtr<word> leftoverWords(reinterpret_cast<word*>(leftover.begin()),
      leftover.size() / sizeof(word));

  auto& incomplete = incompleteRead.emplace(IncompleteRead {
    kj::heapArray<byte>(leftover.size()),
    capnp::expectedSizeInWordsFromPrefix(leftoverWords) * sizeof(word)
  });
  memcpy(incomplete.bytes.begin(), leftover.begin(), leftover.size());
  incompleteReadFds = kj::mv(fds);
}

kj::Array<kj::AutoCloseFd> BufferedAsyncCapabilityMessageStream::ReadResult::getFds(bool isLast) {
  if (prefixFds.size() > 0) {
    KJ_REQUIRE(capCount == 0, "Additional FDs were sent prematurely.");
    return kj::mv(prefixFds);
  } else if (capCount > 0 && isLast) {
    auto builder = kj::heapArrayBuilder<kj::AutoCloseFd>(capCount);
    builder.addAll<kj::AutoCloseFd*, true>(fdSpace.begin(), fdSpace.begin() + capCount);
    capCount = 0;
    return builder.finish();
  } else {
    return kj::Array<kj::AutoCloseFd>();
  }
}

kj::Promise<kj::Maybe<MessageReaderAndFds>> BufferedAsyncIoMessageStream::tryReadMessage(
    kj::ArrayPtr<kj::AutoCloseFd> passedInFdSpace,
    ReaderOptions readerOptions,
    kj::ArrayPtr<word> scratchSpace) {
  return _::tryReadMessageInternal(*this, options.defaultReadSize, passedInFdSpace, readerOptions, scratchSpace);
}

kj::Promise<void> BufferedAsyncIoMessageStream::writeMessage(
    kj::ArrayPtr<const int> fds,
    kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  return capnp::writeMessage(stream, segments);
}

kj::Promise<void> BufferedAsyncIoMessageStream::writeMessages(
    kj::ArrayPtr<kj::ArrayPtr<const kj::ArrayPtr<const word>>> messages) {
  return capnp::writeMessages(stream, messages);
}

kj::Maybe<int> BufferedAsyncIoMessageStream::getSendBufferSize() {
  return capnp::getSendBufferSize(stream);
}

kj::Promise<void> BufferedAsyncIoMessageStream::end() {
  stream.shutdownWrite();
  return kj::READY_NOW;
}

kj::Promise<kj::Maybe<MessageReaderAndFds>> BufferedAsyncCapabilityMessageStream::tryReadMessage(
    kj::ArrayPtr<kj::AutoCloseFd> passedInFdSpace,
    ReaderOptions readerOptions,
    kj::ArrayPtr<word> scratchSpace) {
  return _::tryReadMessageInternal(*this, options.defaultReadSize, passedInFdSpace, readerOptions, scratchSpace);
}

kj::Promise<void> BufferedAsyncCapabilityMessageStream::writeMessage(
    kj::ArrayPtr<const int> fds,
    kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  return capnp::writeMessage(stream, segments);
}

kj::Promise<void> BufferedAsyncCapabilityMessageStream::writeMessages(
    kj::ArrayPtr<kj::ArrayPtr<const kj::ArrayPtr<const word>>> messages) {
  return capnp::writeMessages(stream, messages);
}

kj::Maybe<int> BufferedAsyncCapabilityMessageStream::getSendBufferSize() {
  return capnp::getSendBufferSize(stream);
}

kj::Promise<void> BufferedAsyncCapabilityMessageStream::end() {
  stream.shutdownWrite();
  return kj::READY_NOW;
}

}  // namespace capnp
