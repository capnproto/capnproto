// Copyright (c) 2018 Kenton Varda and contributors
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

#include "json-rpc.h"
#include <kj/test.h>
#include <capnp/test-util.h>

namespace capnp {
namespace _ {  // private
namespace {

KJ_TEST("json-rpc basics") {
  auto io = kj::setupAsyncIo();
  auto pipe = kj::newTwoWayPipe();

  JsonRpc::ContentLengthTransport clientTransport(*pipe.ends[0]);
  JsonRpc::ContentLengthTransport serverTransport(*pipe.ends[1]);

  int callCount = 0;

  JsonRpc client(clientTransport);
  JsonRpc server(serverTransport, toDynamic(kj::heap<TestInterfaceImpl>(callCount)));

  auto cap = client.getPeer<test::TestInterface>();
  auto req = cap.fooRequest();
  req.setI(123);
  req.setJ(true);
  auto resp = req.send().wait(io.waitScope);
  KJ_EXPECT(resp.getX() == "foo");

  KJ_EXPECT(callCount == 1);
}

KJ_TEST("json-rpc error") {
  auto io = kj::setupAsyncIo();
  auto pipe = kj::newTwoWayPipe();

  JsonRpc::ContentLengthTransport clientTransport(*pipe.ends[0]);
  JsonRpc::ContentLengthTransport serverTransport(*pipe.ends[1]);

  int callCount = 0;

  JsonRpc client(clientTransport);
  JsonRpc server(serverTransport, toDynamic(kj::heap<TestInterfaceImpl>(callCount)));

  auto cap = client.getPeer<test::TestInterface>();
  KJ_EXPECT_THROW_MESSAGE("Method not implemented", cap.barRequest().send().wait(io.waitScope));
}

KJ_TEST("json-rpc multiple calls") {
  auto io = kj::setupAsyncIo();
  auto pipe = kj::newTwoWayPipe();

  JsonRpc::ContentLengthTransport clientTransport(*pipe.ends[0]);
  JsonRpc::ContentLengthTransport serverTransport(*pipe.ends[1]);

  int callCount = 0;

  JsonRpc client(clientTransport);
  JsonRpc server(serverTransport, toDynamic(kj::heap<TestInterfaceImpl>(callCount)));

  auto cap = client.getPeer<test::TestInterface>();
  auto req1 = cap.fooRequest();
  req1.setI(123);
  req1.setJ(true);
  auto promise1 = req1.send();

  auto req2 = cap.bazRequest();
  initTestMessage(req2.initS());
  auto promise2 = req2.send();

  auto resp1 = promise1.wait(io.waitScope);
  KJ_EXPECT(resp1.getX() == "foo");

  auto resp2 = promise2.wait(io.waitScope);

  KJ_EXPECT(callCount == 2);
}

}  // namespace
}  // namespace _ (private)
}  // namespace capnp
