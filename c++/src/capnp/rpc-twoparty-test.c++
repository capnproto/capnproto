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

#include "rpc-twoparty.h"
#include "test-util.h"
#include <kj/async-unix.h>
#include <kj/debug.h>
#include <kj/thread.h>
#include <gtest/gtest.h>

namespace capnp {
namespace _ {
namespace {

class TestRestorer final: public SturdyRefRestorer<test::TestSturdyRefObjectId> {
public:
  TestRestorer(int& callCount): callCount(callCount) {}

  Capability::Client restore(test::TestSturdyRefObjectId::Reader objectId) override {
    switch (objectId.getTag()) {
      case test::TestSturdyRefObjectId::Tag::TEST_INTERFACE:
        return kj::heap<TestInterfaceImpl>(callCount);
      case test::TestSturdyRefObjectId::Tag::TEST_EXTENDS:
        return Capability::Client(newBrokenCap("No TestExtends implemented."));
      case test::TestSturdyRefObjectId::Tag::TEST_PIPELINE:
        return kj::heap<TestPipelineImpl>(callCount);
    }
    KJ_UNREACHABLE;
  }

private:
  int& callCount;
};

void runServer(kj::Promise<void> quit, kj::Own<kj::AsyncIoStream> stream, int& callCount) {
  // Set up the server.
  kj::UnixEventLoop eventLoop;
  TwoPartyVatNetwork network(eventLoop, *stream, rpc::twoparty::Side::SERVER);
  TestRestorer restorer(callCount);
  auto server = makeRpcServer(network, restorer, eventLoop);

  // Wait until quit promise is fulfilled.
  eventLoop.wait(kj::mv(quit));
}

Capability::Client getPersistentCap(RpcSystem<rpc::twoparty::SturdyRefHostId>& client,
                                    rpc::twoparty::Side side,
                                    test::TestSturdyRefObjectId::Tag tag) {
  // Create the SturdyRefHostId.
  MallocMessageBuilder hostIdMessage(8);
  auto hostId = hostIdMessage.initRoot<rpc::twoparty::SturdyRefHostId>();
  hostId.setSide(side);

  // Create the SturdyRefObjectId.
  MallocMessageBuilder objectIdMessage(8);
  objectIdMessage.initRoot<test::TestSturdyRefObjectId>().setTag(tag);

  // Connect to the remote capability.
  return client.connect(hostId, objectIdMessage.getRoot<ObjectPointer>());
}

TEST(TwoPartyNetwork, Basic) {
  int callCount = 0;

  // We'll communicate over this two-way pipe (actually, a socketpair).
  auto pipe = kj::newTwoWayPipe();

  // Start up server in another thread.
  auto quitter = kj::newPromiseAndFulfiller<void>();
  kj::Thread thread([&]() {
    runServer(kj::mv(quitter.promise), kj::mv(pipe.ends[1]), callCount);
  });
  KJ_DEFER(quitter.fulfiller->fulfill());  // Stop the server loop before destroying the thread.

  // Set up the client-side objects.
  kj::UnixEventLoop loop;
  TwoPartyVatNetwork network(loop, *pipe.ends[0], rpc::twoparty::Side::CLIENT);
  auto rpcClient = makeRpcClient(network, loop);

  // Request the particular capability from the server.
  auto client = getPersistentCap(rpcClient, rpc::twoparty::Side::SERVER,
      test::TestSturdyRefObjectId::Tag::TEST_INTERFACE).castAs<test::TestInterface>();

  // Use the capability.
  auto request1 = client.fooRequest();
  request1.setI(123);
  request1.setJ(true);
  auto promise1 = request1.send();

  auto request2 = client.bazRequest();
  initTestMessage(request2.initS());
  auto promise2 = request2.send();

  bool barFailed = false;
  auto request3 = client.barRequest();
  auto promise3 = loop.there(request3.send(),
      [](Response<test::TestInterface::BarResults>&& response) {
        ADD_FAILURE() << "Expected bar() call to fail.";
      }, [&](kj::Exception&& e) {
        barFailed = true;
      });

  EXPECT_EQ(0, callCount);

  auto response1 = loop.wait(kj::mv(promise1));

  EXPECT_EQ("foo", response1.getX());

  auto response2 = loop.wait(kj::mv(promise2));

  loop.wait(kj::mv(promise3));

  EXPECT_EQ(2, callCount);
  EXPECT_TRUE(barFailed);
}

TEST(TwoPartyNetwork, Pipelining) {
  int callCount = 0;
  int reverseCallCount = 0;  // Calls back from server to client.

  // We'll communicate over this two-way pipe (actually, a socketpair).
  auto pipe = kj::newTwoWayPipe();

  // Start up server in another thread.
  auto quitter = kj::newPromiseAndFulfiller<void>();
  kj::Thread thread([&]() {
    runServer(kj::mv(quitter.promise), kj::mv(pipe.ends[1]), callCount);
  });
  KJ_DEFER(quitter.fulfiller->fulfill());  // Stop the server loop before destroying the thread.

  // Set up the client-side objects.
  kj::UnixEventLoop loop;
  TwoPartyVatNetwork network(loop, *pipe.ends[0], rpc::twoparty::Side::CLIENT);
  auto rpcClient = makeRpcClient(network, loop);

  // Request the particular capability from the server.
  auto client = getPersistentCap(rpcClient, rpc::twoparty::Side::SERVER,
      test::TestSturdyRefObjectId::Tag::TEST_PIPELINE).castAs<test::TestPipeline>();

  // Use the capability.
  auto request = client.getCapRequest();
  request.setN(234);
  request.setInCap(test::TestInterface::Client(
      kj::heap<TestInterfaceImpl>(reverseCallCount), loop));

  auto promise = request.send();

  auto pipelineRequest = promise.getOutBox().getCap().fooRequest();
  pipelineRequest.setI(321);
  auto pipelinePromise = pipelineRequest.send();

  auto pipelineRequest2 = promise.getOutBox().getCap().castAs<test::TestExtends>().graultRequest();
  auto pipelinePromise2 = pipelineRequest2.send();

  promise = nullptr;  // Just to be annoying, drop the original promise.

  EXPECT_EQ(0, callCount);
  EXPECT_EQ(0, reverseCallCount);

  auto response = loop.wait(kj::mv(pipelinePromise));
  EXPECT_EQ("bar", response.getX());

  auto response2 = loop.wait(kj::mv(pipelinePromise2));
  checkTestMessage(response2);

  EXPECT_EQ(3, callCount);
  EXPECT_EQ(1, reverseCallCount);
}

}  // namespace
}  // namespace _
}  // namespace capnp
