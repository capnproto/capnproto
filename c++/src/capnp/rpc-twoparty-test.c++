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
  kj::UnixEventLoop eventLoop;
  TwoPartyVatNetwork network(eventLoop, *stream, rpc::twoparty::Side::SERVER);
  TestRestorer restorer(callCount);
  auto server = makeRpcServer(network, restorer, eventLoop);
  eventLoop.wait(kj::mv(quit));
}

Capability::Client connect(RpcSystem<rpc::twoparty::SturdyRefHostId>& client,
                           rpc::twoparty::Side side,
                           test::TestSturdyRefObjectId::Tag tag) {
  MallocMessageBuilder hostIdMessage(8);
  MallocMessageBuilder objectIdMessage(8);
  auto hostId = hostIdMessage.initRoot<rpc::twoparty::SturdyRefHostId>();
  hostId.setSide(side);
  objectIdMessage.initRoot<test::TestSturdyRefObjectId>().setTag(tag);
  return client.connect(hostId, objectIdMessage.getRoot<ObjectPointer>());
}

TEST(TwoPartyNetwork, Basic) {
  auto quitter = kj::newPromiseAndFulfiller<void>();
  auto pipe = kj::newTwoWayPipe();
  int callCount = 0;

  kj::Thread thread([&]() {
    runServer(kj::mv(quitter.promise), kj::mv(pipe.ends[1]), callCount);
  });
  KJ_DEFER(quitter.fulfiller->fulfill());

  kj::UnixEventLoop loop;
  TwoPartyVatNetwork network(loop, *pipe.ends[0], rpc::twoparty::Side::CLIENT);
  auto rpcClient = makeRpcClient(network, loop);

  auto client = connect(rpcClient, rpc::twoparty::Side::SERVER,
      test::TestSturdyRefObjectId::Tag::TEST_INTERFACE).castAs<test::TestInterface>();

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

}  // namespace
}  // namespace _
}  // namespace capnp
