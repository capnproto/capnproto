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

#pragma once

#include "json.h"
#include <kj/async-io.h>
#include <capnp/capability.h>
#include <kj/map.h>

namespace kj { class HttpInputStream; }

namespace capnp {

class JsonRpc: private kj::TaskSet::ErrorHandler {
  // An implementation of JSON-RPC 2.0: https://www.jsonrpc.org/specification
  //
  // This allows you to use Cap'n Proto interface declarations to implement JSON-RPC protocols.
  // Of course, JSON-RPC does not support capabilities. So, the client and server each expose
  // exactly one object to the other.

public:
  class Transport;
  class ContentLengthTransport;

  JsonRpc(Transport& transport, DynamicCapability::Client interface = {});
  KJ_DISALLOW_COPY(JsonRpc);

  DynamicCapability::Client getPeer(InterfaceSchema schema);

  template <typename T>
  typename T::Client getPeer() {
    return getPeer(Schema::from<T>()).template castAs<T>();
  }

  kj::Promise<void> onError() { return errorPromise.addBranch(); }

private:
  JsonCodec codec;
  Transport& transport;
  DynamicCapability::Client interface;
  kj::HashMap<kj::StringPtr, InterfaceSchema::Method> methodMap;
  uint callCount = 0;
  kj::Promise<void> writeQueue = kj::READY_NOW;
  kj::ForkedPromise<void> errorPromise;
  kj::Own<kj::PromiseFulfiller<void>> errorFulfiller;
  kj::Promise<void> readTask;

  struct AwaitedResponse {
    CallContext<DynamicStruct, DynamicStruct> context;
    kj::Own<kj::PromiseFulfiller<void>> fulfiller;
  };
  kj::HashMap<uint, AwaitedResponse> awaitedResponses;

  kj::TaskSet tasks;

  class CapabilityImpl;

  kj::Promise<void> queueWrite(kj::String text);
  void queueError(kj::Maybe<json::Value::Reader> id, int code, kj::StringPtr message);

  kj::Promise<void> readLoop();

  void taskFailed(kj::Exception&& exception) override;

  JsonRpc(Transport& transport, DynamicCapability::Client interface,
          kj::PromiseFulfillerPair<void> paf);
};

class JsonRpc::Transport {
public:
  virtual kj::Promise<void> send(kj::StringPtr text) = 0;
  virtual kj::Promise<kj::String> receive() = 0;
};

class JsonRpc::ContentLengthTransport: public Transport {
  // The transport used by Visual Studio Code: Each message is composed like an HTTP message
  // without the first line. That is, a list of headers, followed by a blank line, followed by the
  // content whose length is determined by the content-length header.
public:
  explicit ContentLengthTransport(kj::AsyncIoStream& stream);
  ~ContentLengthTransport() noexcept(false);
  KJ_DISALLOW_COPY(ContentLengthTransport);

  kj::Promise<void> send(kj::StringPtr text) override;
  kj::Promise<kj::String> receive() override;

private:
  kj::AsyncIoStream& stream;
  kj::Own<kj::HttpInputStream> input;
  kj::ArrayPtr<const byte> parts[2];
};

}  // namespace capnp
