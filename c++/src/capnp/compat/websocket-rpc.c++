// Copyright (c) 2021 Ian Denhardt and contributors
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

#include <capnp/compat/websocket-rpc.h>
#include <kj/io.h>
#include <capnp/serialize.h>

namespace capnp {

WebSocketMessageStream::WebSocketMessageStream(kj::WebSocket& socket)
  : socket(socket)
  {};

kj::Promise<kj::Maybe<MessageReaderAndFds>> WebSocketMessageStream::tryReadMessage(
    kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
    ReaderOptions options, kj::ArrayPtr<word> scratchSpace) {
  return socket.receive(options.traversalLimitInWords * sizeof(word))
      .then([options](auto msg) -> kj::Promise<kj::Maybe<MessageReaderAndFds>> {
    KJ_SWITCH_ONEOF(msg) {
        KJ_CASE_ONEOF(closeMsg, kj::WebSocket::Close) {
          return kj::Maybe<MessageReaderAndFds>();
        }
        KJ_CASE_ONEOF(str, kj::String) {
          KJ_FAIL_REQUIRE(
              "Unexpected websocket text message; expected only binary messages.");
          break;
        }
        KJ_CASE_ONEOF(bytes, kj::Array<byte>) {
          kj::Own<capnp::MessageReader> reader;
          size_t sizeInWords = bytes.size() / sizeof(word);
          if (reinterpret_cast<uintptr_t>(bytes.begin()) % alignof(word) == 0) {
            reader = kj::heap<FlatArrayMessageReader>(
                kj::arrayPtr(
                  reinterpret_cast<word *>(bytes.begin()),
                  sizeInWords
                ),
                options).attach(kj::mv(bytes));
          } else {
            // The array is misaligned, so we need to copy it.
            auto words = kj::heapArray<word>(sizeInWords);

            // Note: can't just use bytes.size(), since the the target buffer may
            // be shorter due to integer division.
            memcpy(words.begin(), bytes.begin(), sizeInWords * sizeof(word));
            reader = kj::heap<FlatArrayMessageReader>(
                kj::arrayPtr(words.begin(), sizeInWords),
                options).attach(kj::mv(words));
          }
          return kj::Maybe<MessageReaderAndFds>(MessageReaderAndFds {
            kj::mv(reader),
            nullptr
          });
        }
      }
      KJ_UNREACHABLE;
    });
}

kj::Promise<void> WebSocketMessageStream::writeMessage(
    kj::ArrayPtr<const int> fds,
    kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) {
  // TODO(perf): Right now the WebSocket interface only supports send() for
  // contiguous arrays, so we need to copy the whole message into a new buffer
  // in order to send it, whereas ideally we could just write each segment
  // (and the segment table) in sequence. Perhaps we should extend the WebSocket
  // interface to be able to send an ArrayPtr<ArrayPtr<byte>> as one binary
  // message, and then use that to avoid an extra copy here.

  auto stream = kj::heap<kj::VectorOutputStream>(
      computeSerializedSizeInWords(segments) * sizeof(word));
  capnp::writeMessage(*stream, segments);
  auto arrayPtr = stream->getArray();
  return socket.send(arrayPtr).attach(kj::mv(stream));
}

kj::Promise<void> WebSocketMessageStream::writeMessages(
    kj::ArrayPtr<kj::ArrayPtr<const kj::ArrayPtr<const word>>> messages) {
  // TODO(perf): Extend WebSocket interface with a way to write multiple messages at once.

  if(messages.size() == 0) {
    return kj::READY_NOW;
  }
  return writeMessage(nullptr, messages[0])
      .then([this, messages = messages.slice(1, messages.size())]() mutable -> kj::Promise<void> {
    return writeMessages(messages);
  });
}

kj::Maybe<int> WebSocketMessageStream::getSendBufferSize() {
  return nullptr;
}

kj::Promise<void> WebSocketMessageStream::end() {
  return socket.close(
    1005, // most generic code, indicates "No Status Received."
          // Since the MessageStream API doesn't tell us why
          // we're closing the connection, this is the best
          // we can do. This is consistent with what browser
          // implementations do if no status is provided, see:
          //
          // * https://developer.mozilla.org/en-US/docs/Web/API/WebSocket/close
          // * https://developer.mozilla.org/en-US/docs/Web/API/CloseEvent

    "Capnp connection closed" // Similarly not much information to go on here,
                              // but this at least lets us trace this back to
                              // capnp.
  );
};

};
