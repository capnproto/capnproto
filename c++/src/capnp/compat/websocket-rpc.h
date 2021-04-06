#pragma once

#include <kj/compat/http.h>
#include <capnp/serialize-async.h>

namespace capnp {
class WebSocketMessageStream final : public MessageStream {
  // An implementation of MessageStream that sends messages over a websocket.
  //
  // Each capnproto message is sent in a single binary websocket frame.
public:
  WebSocketMessageStream(
      kj::WebSocket& socket,
      size_t maxMessageReceiveSize = kj::WebSocket::SUGGESTED_MAX_MESSAGE_SIZE);

  // Implements MessageStream
  kj::Promise<kj::Maybe<MessageReaderAndFds>> tryReadMessage(
      kj::ArrayPtr<kj::AutoCloseFd> fdSpace,
      ReaderOptions options = ReaderOptions(), kj::ArrayPtr<word> scratchSpace = nullptr) override;
  kj::Promise<void> writeMessage(
      kj::ArrayPtr<const int> fds,
      kj::ArrayPtr<const kj::ArrayPtr<const word>> segments) override
    KJ_WARN_UNUSED_RESULT;
  kj::Promise<void> writeMessages(
      kj::ArrayPtr<kj::ArrayPtr<const kj::ArrayPtr<const word>>> messages) override
    KJ_WARN_UNUSED_RESULT;
  kj::Maybe<int> getSendBufferSize() override;
  kj::Promise<void> end() override;
private:
  kj::WebSocket& socket;
  size_t maxMessageReceiveSize;
};
};
