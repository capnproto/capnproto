
#include "websocket-rpc.h"
#include <kj/test.h>

#include <capnp/test.capnp.h>

KJ_TEST("WebSocketMessageStream") {
  kj::EventLoop loop;
  kj::WaitScope waitScope(loop);

  auto pipe = kj::newWebSocketPipe();

  auto msgStreamA = capnp::WebSocketMessageStream(*pipe.ends[0]);
  auto msgStreamB = capnp::WebSocketMessageStream(*pipe.ends[1]);

  // Make a message, fill it with some stuff
  capnp::MallocMessageBuilder originalMsg;
  auto object = originalMsg.initRoot<capnproto_test::capnp::test::TestAllTypes>().initStructList(10);
  object[0].setTextField("Test");
  object[1].initStructField().setTextField("A string");
  object[2].setTextField("Another field");
  object[3].setInt64Field(42);
  auto originalSegments = originalMsg.getSegmentsForOutput();

  // Send the message across the websocket, make sure it comes out unharmed.
  auto writePromise = msgStreamA.writeMessage(nullptr, originalSegments);
  msgStreamB.tryReadMessage(nullptr)
    .then([&](auto maybeResult) -> kj::Promise<void> {
      KJ_IF_MAYBE(result, maybeResult) {
        KJ_ASSERT(result->fds.size() == 0);
        KJ_ASSERT(result->reader->getSegment(originalSegments.size()) == nullptr);
        for(size_t i = 0; i < originalSegments.size(); i++) {
          auto oldSegment = originalSegments[i];
          auto newSegment = result->reader->getSegment(i);

          KJ_ASSERT(oldSegment.size() == newSegment.size());
          KJ_ASSERT(memcmp(
                &oldSegment[0],
                &newSegment[0],
                oldSegment.size() * sizeof(capnp::word)
                ) == 0);
        }
        return kj::READY_NOW;
      } else {
        KJ_FAIL_ASSERT("Reading first message failed");
      }
  }).wait(waitScope);
  writePromise.wait(waitScope);

  // Close the websocket, and make sure the other end gets nullptr when reading.
  auto endPromise = msgStreamA.end();
  msgStreamB.tryReadMessage(nullptr).then([](auto maybe) -> kj::Promise<void> {
    KJ_IF_MAYBE(segments, maybe) {
      KJ_FAIL_ASSERT("Should have gotten nullptr after websocket was closed");
    }
    return kj::READY_NOW;
  }).wait(waitScope);
  endPromise.wait(waitScope);
}
