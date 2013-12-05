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

#include "ez-rpc.h"
#include "test-util.h"
#include <gtest/gtest.h>

namespace capnp {
namespace _ {
namespace {

TEST(EzRpc, Basic) {
  EzRpcServer server("localhost");
  int callCount = 0;
  server.exportCap("cap1", kj::heap<TestInterfaceImpl>(callCount));
  server.exportCap("cap2", kj::heap<TestCallOrderImpl>());

  EzRpcClient client("localhost", server.getPort().wait(server.getWaitScope()));

  auto cap = client.importCap<test::TestInterface>("cap1");
  auto request = cap.fooRequest();
  request.setI(123);
  request.setJ(true);

  EXPECT_EQ(0, callCount);
  auto response = request.send().wait(server.getWaitScope());
  EXPECT_EQ("foo", response.getX());
  EXPECT_EQ(1, callCount);

  EXPECT_EQ(0, client.importCap("cap2").castAs<test::TestCallOrder>()
      .getCallSequenceRequest().send().wait(server.getWaitScope()).getN());
  EXPECT_EQ(1, client.importCap("cap2").castAs<test::TestCallOrder>()
      .getCallSequenceRequest().send().wait(server.getWaitScope()).getN());
}

}  // namespace
}  // namespace _
}  // namespace capnp
