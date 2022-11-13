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
  return promise.then([arrays=kj::mv(arrays)]() {});
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

kj::Promise<void> MessageStream::writeMessages(kj::ArrayPtr<MessageAndFds> messages) {
  if (messages.size() == 0) return kj::READY_NOW;
  kj::ArrayPtr<MessageAndFds> remainingMessages;

  auto writeProm = [&]() {
    if (messages[0].fds.size() > 0) {
      // We have a message with FDs attached. We need to write any bare messages we've accumulated,
      // if any, then write the message with FDs, then continue on with any remaining messages.

      if (messages.size() > 1) {
        remainingMessages = messages.slice(1, messages.size());
      }

      return writeMessage(messages[0].fds, messages[0].segments);
    } else {
      kj::Vector<kj::ArrayPtr<const kj::ArrayPtr<const word>>> bareMessages(messages.size());
      for(auto i : kj::zeroTo(messages.size())) {
        if (messages[i].fds.size() > 0) {
          break;
        }
        bareMessages.add(messages[i].segments);
      }

      if (messages.size() > bareMessages.size()) {
        remainingMessages = messages.slice(bareMessages.size(), messages.size());
      }
      return writeMessages(bareMessages.asPtr()).attach(kj::mv(bareMessages));
    }
  }();

  if (remainingMessages.size() > 0) {
    return writeProm.then([this, remainingMessages]() mutable {
      return writeMessages(remainingMessages);
    });
  } else {
    return writeProm;
  }
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

// =======================================================================================

class BufferedMessageStream::MessageReaderImpl: public FlatArrayMessageReader {
public:
  MessageReaderImpl(BufferedMessageStream& parent, kj::ArrayPtr<const word> data,
                    ReaderOptions options)
      : FlatArrayMessageReader(data, options), state(&parent) {
    KJ_DASSERT(!parent.hasOutstandingShortLivedMessage);
    parent.hasOutstandingShortLivedMessage = true;
  }
  MessageReaderImpl(kj::Array<word>&& ownBuffer, ReaderOptions options)
      : FlatArrayMessageReader(ownBuffer, options), state(kj::mv(ownBuffer)) {}
  MessageReaderImpl(kj::ArrayPtr<word> scratchBuffer, ReaderOptions options)
      : FlatArrayMessageReader(scratchBuffer, options) {}

  ~MessageReaderImpl() noexcept(false) {
    KJ_IF_MAYBE(parent, state.tryGet<BufferedMessageStream*>()) {
      (*parent)->hasOutstandingShortLivedMessage = false;
    }
  }

private:
  kj::OneOf<BufferedMessageStream*, kj::Array<word>> state;
  // * BufferedMessageStream* if this reader aliases the original buffer.
  // * kj::Array<word> if this reader owns its own backing buffer.
};

BufferedMessageStream::BufferedMessageStream(
    kj::AsyncIoStream& stream, IsShortLivedCallback isShortLivedCallback,
    size_t bufferSizeInWords)
    : stream(stream), isShortLivedCallback(kj::mv(isShortLivedCallback)),
      buffer(kj::heapArray<word>(bufferSizeInWords)),
      beginData(buffer.begin()), beginAvailable(buffer.asBytes().begin()) {}

BufferedMessageStream::BufferedMessageStream(
    kj::AsyncCapabilityStream& stream, IsShortLivedCallback isShortLivedCallback,
    size_t bufferSizeInWords)
    : stream(stream), capStream(stream), isShortLivedCallback(kj::mv(isShortLivedCallback)),
      buffer(kj::heapArray<word>(bufferSizeInWords)),
      beginData(buffer.begin()), beginAvailable(buffer.asBytes().begin()) {}

kj::Promise<kj::Maybe<MessageReaderAndFds>> BufferedMessageStream::tryReadMessage(
    kj::ArrayPtr<kj::AutoCloseFd> fdSpace, ReaderOptions options, kj::ArrayPtr<word> scratchSpace) {
  return tryReadMessageImpl(fdSpace, 0, options, scratchSpace);
}

kj::Promise<void> BufferedMessageStream::writeMessage(
    kj::ArrayPtr<const int> fds,
    kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  KJ_IF_MAYBE(cs, capStream) {
    return capnp::writeMessage(*cs, fds, segments);
  } else {
    return capnp::writeMessage(stream, segments);
  }
}

kj::Promise<void> BufferedMessageStream::writeMessages(
    kj::ArrayPtr<kj::ArrayPtr<const kj::ArrayPtr<const word>>> messages) {
  return capnp::writeMessages(stream, messages);
}

kj::Maybe<int> BufferedMessageStream::getSendBufferSize() {
  return capnp::getSendBufferSize(stream);
}

kj::Promise<void> BufferedMessageStream::end() {
  stream.shutdownWrite();
  return kj::READY_NOW;
}

kj::Promise<kj::Maybe<MessageReaderAndFds>> BufferedMessageStream::tryReadMessageImpl(
    kj::ArrayPtr<kj::AutoCloseFd> fdSpace, size_t fdsSoFar,
    ReaderOptions options, kj::ArrayPtr<word> scratchSpace) {
  KJ_REQUIRE(!hasOutstandingShortLivedMessage,
      "can't read another message while the previous short-lived message still exists");

  kj::byte* beginDataBytes = reinterpret_cast<kj::byte*>(beginData);
  size_t dataByteSize = beginAvailable - beginDataBytes;
  kj::ArrayPtr<word> data = kj::arrayPtr(beginData, dataByteSize / sizeof(word));

  size_t expected = expectedSizeInWordsFromPrefix(data);

  if (!leftoverFds.empty() && expected * sizeof(word) == dataByteSize) {
    // We're about to return a message that consumes the rest of the data in the buffer, and
    // `leftoverFds` is non-empty. Those FDs are considered attached to whatever message contains
    // the last byte in the buffer. That's us! Let's consume them.

    // `fdsSoFar` must be empty here because we shouldn't have performed any reads while
    // `leftoverFds` was non-empty, so there shouldn't have been any other chance to add FDs to
    // `fdSpace`.
    KJ_ASSERT(fdsSoFar == 0);

    fdsSoFar = kj::min(leftoverFds.size(), fdSpace.size());
    for (auto i: kj::zeroTo(fdsSoFar)) {
      fdSpace[i] = kj::mv(leftoverFds[i]);
    }
    leftoverFds.clear();
  }

  if (expected <= data.size()) {
    // The buffer contains at least one whole message, which we can just return without reading
    // any more data.

    auto msgData = kj::arrayPtr(beginData, expected);
    auto reader = kj::heap<MessageReaderImpl>(*this, msgData, options);
    if (!isShortLivedCallback(*reader)) {
      // This message is long-lived, so we must make a copy to get it out of our buffer.
      if (msgData.size() <= scratchSpace.size()) {
        // Oh hey, we can use the provided scratch space.
        memcpy(scratchSpace.begin(), msgData.begin(), msgData.asBytes().size());
        reader = kj::heap<MessageReaderImpl>(scratchSpace, options);
      } else {
        auto ownMsgData = kj::heapArray<word>(msgData.size());
        memcpy(ownMsgData.begin(), msgData.begin(), msgData.asBytes().size());
        reader = kj::heap<MessageReaderImpl>(kj::mv(ownMsgData), options);
      }
    }

    beginData += expected;
    if (reinterpret_cast<byte*>(beginData) == beginAvailable) {
      // The buffer is empty. Let's opportunistically reset the pointers.
      beginData = buffer.begin();
      beginAvailable = buffer.asBytes().begin();
    } else if (fdsSoFar > 0) {
      // The buffer is NOT empty, and we received FDs when we were filling it. These FDs must
      // actually belong to the last message in the buffer, because when the OS returns FDs
      // attached to a read, it will make sure the read does not extend past the last byte to
      // which those FDs were attached.
      //
      // So, we must set these FDs aside for the moment.
      for (auto i: kj::zeroTo(fdsSoFar)) {
        leftoverFds.add(kj::mv(fdSpace[i]));
      }
      fdsSoFar = 0;
    }

    return kj::Maybe<MessageReaderAndFds>(MessageReaderAndFds {
      kj::mv(reader),
      fdSpace.slice(0, fdsSoFar)
    });
  }

  // At this point, the buffer doesn't contain a complete message. We are going to need to perform
  // a read.

  if (expected > buffer.size() / 2 || fdsSoFar > 0) {
    // Read this message into its own separately-allocated buffer. We do this for:
    // - Big messages, because they might not fit in the buffer and because big messages are
    //   almost certainly going to be long-lived and so would require a copy later anyway.
    // - Messages where we've already received some FDs, because these are also almost certainly
    //   long-lived, and we want to avoid accidentally reading into the next message since we
    //   could end up receiving FDs that were intended for that one.
    //
    // Optimization note: You might argue that if the expected size is more than half the buffer,
    // but still less than the *whole* buffer, then we should still try to read into the buffer
    // first. However, keep in mind that in the RPC system, all short-lived messages are
    // relatively small, and hence we can assume that since this is a large message, it will
    // end up being long-lived. Long-lived messages need to be copied out into their own buffer
    // at some point anyway. So we might as well go ahead and allocate that separate buffer
    // now, and read directly into it, rather than try to use the shared buffer. We choose to
    // use buffer.size() / 2 as the cutoff because that ensures that we won't try to move the
    // bytes of a known-large message to the beginning of the buffer (see next if() after this
    // one).

    auto prefix = kj::arrayPtr(beginDataBytes, dataByteSize);

    // We are consuming everything in the buffer here, so we can reset the pointers so the
    // buffer appears empty on the next message read after this.
    beginData = buffer.begin();
    beginAvailable = buffer.asBytes().begin();

    return readEntireMessage(prefix, expected, fdSpace, fdsSoFar, options);
  }

  // Set minBytes to at least complete the current message.
  size_t minBytes = expected * sizeof(word) - dataByteSize;

  // minBytes must be less than half the buffer otherwise we would have taken the
  // readEntireMessage() branch above.
  KJ_DASSERT(minBytes <= buffer.asBytes().size() / 2);

  // Set maxBytes to the space we have available in the buffer.
  size_t maxBytes = buffer.asBytes().end() - beginAvailable;

  if (maxBytes < buffer.asBytes().size() / 2) {
    // We have less than half the buffer remaining to read into. Move the buffered data to the
    // beginning of the buffer to make more space.
    memmove(buffer.begin(), beginData, dataByteSize);
    beginData = buffer.begin();
    beginDataBytes = buffer.asBytes().begin();
    beginAvailable = beginDataBytes + dataByteSize;

    maxBytes = buffer.asBytes().end() - beginAvailable;
  }

  // maxBytes must now be more than half the buffer, because if it weren't we would have moved
  // the existing data above, and the existing data cannot be more than half the buffer because
  // if it were we would have taken the readEntireMesage() path earlier.
  KJ_DASSERT(maxBytes >= buffer.asBytes().size() / 2);

  // Since minBytes is less that half the buffer and maxBytes is more then half, minBytes is
  // definitely less than maxBytes.
  KJ_DASSERT(minBytes <= maxBytes);

  // Read from underlying stream.
  return tryReadWithFds(beginAvailable, minBytes, maxBytes,
                        fdSpace.begin() + fdsSoFar, fdSpace.size() - fdsSoFar)
      .then([this,minBytes,fdSpace,fdsSoFar,options,scratchSpace]
            (kj::AsyncCapabilityStream::ReadResult result) mutable
            -> kj::Promise<kj::Maybe<MessageReaderAndFds>> {
    // Account for new data received in the buffer.
    beginAvailable += result.byteCount;

    if (result.byteCount < minBytes) {
      // Didn't reach minBytes, so we must have hit EOF. That's legal as long as it happened on
      // a clean message boundray.
      if (beginAvailable > reinterpret_cast<kj::byte*>(beginData)) {
        // We had received a partial message before EOF, so this should be considered an error.
        kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED,
            "stream disconnected prematurely"));
      }
      return kj::Maybe<MessageReaderAndFds>(nullptr);
    }

    // Loop!
    return tryReadMessageImpl(fdSpace, fdsSoFar + result.capCount, options, scratchSpace);
  });
}

kj::Promise<kj::Maybe<MessageReaderAndFds>> BufferedMessageStream::readEntireMessage(
    kj::ArrayPtr<const byte> prefix, size_t expectedSizeInWords,
    kj::ArrayPtr<kj::AutoCloseFd> fdSpace, size_t fdsSoFar,
    ReaderOptions options) {
  KJ_REQUIRE(expectedSizeInWords <= options.traversalLimitInWords,
      "incoming RPC message exceeds size limit");

  auto msgBuffer = kj::heapArray<word>(expectedSizeInWords);

  memcpy(msgBuffer.asBytes().begin(), prefix.begin(), prefix.size());

  size_t bytesRemaining = msgBuffer.asBytes().size() - prefix.size();

  // TODO(perf): If we had scatter-read API support, we could optimistically try to read additional
  //   bytes into the shared buffer, to save syscalls when a big message is immediately followed
  //   by small messages.
  auto promise = tryReadWithFds(
      msgBuffer.asBytes().begin() + prefix.size(), bytesRemaining, bytesRemaining,
      fdSpace.begin() + fdsSoFar, fdSpace.size() - fdsSoFar);
  return promise
      .then([this, msgBuffer = kj::mv(msgBuffer), fdSpace, fdsSoFar, options, bytesRemaining]
            (kj::AsyncCapabilityStream::ReadResult result) mutable
            -> kj::Promise<kj::Maybe<MessageReaderAndFds>> {
    fdsSoFar += result.capCount;

    if (result.byteCount < bytesRemaining) {
      // Received EOF during message.
      kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "stream disconnected prematurely"));
      return kj::Maybe<MessageReaderAndFds>(nullptr);
    }

    size_t newExpectedSize = expectedSizeInWordsFromPrefix(msgBuffer);
    if (newExpectedSize > msgBuffer.size()) {
      // Unfortunately, the predicted size increased. This can happen if the segment table had
      // not been fully received when we generated the first prediction. This should be rare
      // (most segment tables are small and should be received all at once), but in this case we
      // will need to make a whole new copy of the message.
      //
      // We recurse here, but this should never recurse more than once, since we should always
      // have the entire segment table by this point and therefore the expected size is now final.
      //
      // TODO(perf): Technically it's guaranteed that the original expectation should have stopped
      //   at the boundary between two segments, so with a clever MesnsageReader implementation
      //   we could actually read the rest of the message into a second buffer, avoiding the copy.
      //   Unclear if it's worth the effort to implement this.
      return readEntireMessage(msgBuffer.asBytes(), newExpectedSize, fdSpace, fdsSoFar, options);
    }

    return kj::Maybe<MessageReaderAndFds>(MessageReaderAndFds {
      kj::heap<MessageReaderImpl>(kj::mv(msgBuffer), options),
      fdSpace.slice(0, fdsSoFar)
    });
  });
}

kj::Promise<kj::AsyncCapabilityStream::ReadResult> BufferedMessageStream::tryReadWithFds(
    void* buffer, size_t minBytes, size_t maxBytes, kj::AutoCloseFd* fdBuffer, size_t maxFds) {
  KJ_IF_MAYBE(cs, capStream) {
    return cs->tryReadWithFds(buffer, minBytes, maxBytes, fdBuffer, maxFds);
  } else {
    // Regular byte stream, no FDs.
    return stream.tryRead(buffer, minBytes, maxBytes)
        .then([](size_t amount) mutable -> kj::AsyncCapabilityStream::ReadResult {
      return { amount, 0 };
    });
  }
}

}  // namespace capnp
