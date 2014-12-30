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

#include "ez-rpc.h"
#include "test-util.h"
#include <kj/compat/gtest.h>

namespace capnp {
namespace _ {
namespace {

TEST(EzRpc, Basic) {
  int callCount = 0;
  EzRpcServer server(kj::heap<TestInterfaceImpl>(callCount), "localhost");

  EzRpcClient client("localhost", server.getPort().wait(server.getWaitScope()));

  auto cap = client.getMain<test::TestInterface>();
  auto request = cap.fooRequest();
  request.setI(123);
  request.setJ(true);

  EXPECT_EQ(0, callCount);
  auto response = request.send().wait(server.getWaitScope());
  EXPECT_EQ("foo", response.getX());
  EXPECT_EQ(1, callCount);
}

TEST(EzRpc, DeprecatedNames) {
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
