#include <capnp/compat/websocket-rpc.h>
#include <kj/io.h>
#include <capnp/serialize.h>

namespace capnp {

WebSocketMessageStream::WebSocketMessageStream(kj::WebSocket& socket, size_t maxMessageReceiveSize)
  : socket(socket),
    maxMessageReceiveSize(maxMessageReceiveSize)
  {};

kj::Promise<kj::Maybe<MessageReaderAndFds>> WebSocketMessageStream::tryReadMessage(
    kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
    ReaderOptions options, kj::ArrayPtr<word> scratchSpace) {
  return socket.receive(maxMessageReceiveSize)
      .then([options, scratchSpace](auto msg) -> kj::Promise<kj::Maybe<MessageReaderAndFds>> {
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
          kj::ArrayPtr<byte> ptr(bytes);
          if (reinterpret_cast<uintptr_t>(&bytes[0]) % alignof(word) == 0
              && bytes.size() % sizeof(word) == 0) {
            return kj::Maybe(MessageReaderAndFds {
              .reader = kj::heap<FlatArrayMessageReader>(
                  kj::arrayPtr(
                    reinterpret_cast<word *>(&bytes[0]),
                    bytes.size() / sizeof(word)
                  ),
                  options
                ).attach(kj::mv(bytes)),
              .fds = nullptr
            });
          } else {
            // The array is misaligned or not a whole number of words, so we
            // need to copy it.
            auto stream = kj::heap<kj::ArrayInputStream>(bytes);
            auto reader = kj::heap<InputStreamMessageReader>(*stream, options, scratchSpace);
            return kj::Maybe(MessageReaderAndFds {
              .reader = kj::mv(reader).attach(kj::mv(stream)),
              .fds = nullptr
            });
          }
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
      .then([this, messages = messages.slice(1, messages.size())]() -> kj::Promise<void> {
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
