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
#include <kj/compat/http.h>
#include <capnp/compat/json-rpc.capnp.h>

namespace capnp {

static constexpr uint64_t JSON_NAME_ANNOTATION_ID = 0xfa5b1fd61c2e7c3dull;
static constexpr uint64_t JSON_NOTIFICATION_ANNOTATION_ID = 0xa0a054dea32fd98cull;

class JsonRpc::CapabilityImpl final: public DynamicCapability::Server {
public:
  CapabilityImpl(JsonRpc& parent, InterfaceSchema schema)
      : DynamicCapability::Server(schema), parent(parent) {}

  kj::Promise<void> call(InterfaceSchema::Method method,
                         CallContext<DynamicStruct, DynamicStruct> context) override {
    auto proto = method.getProto();
    bool isNotification = false;
    kj::StringPtr name = proto.getName();
    for (auto annotation: proto.getAnnotations()) {
      switch (annotation.getId()) {
        case JSON_NAME_ANNOTATION_ID:
          name = annotation.getValue().getText();
          break;
        case JSON_NOTIFICATION_ANNOTATION_ID:
          isNotification = true;
          break;
      }
    }

    capnp::MallocMessageBuilder message;
    auto value = message.getRoot<json::Value>();
    auto list = value.initObject(3 + !isNotification);

    uint index = 0;

    auto jsonrpc = list[index++];
    jsonrpc.setName("jsonrpc");
    jsonrpc.initValue().setString("2.0");

    uint callId = parent.callCount++;

    if (!isNotification) {
      auto id = list[index++];
      id.setName("id");
      id.initValue().setNumber(callId);
    }

    auto methodName = list[index++];
    methodName.setName("method");
    methodName.initValue().setString(name);

    auto params = list[index++];
    params.setName("params");
    parent.codec.encode(context.getParams(), params.initValue());

    auto writePromise = parent.queueWrite(parent.codec.encode(value));

    if (isNotification) {
      auto sproto = context.getResultsType().getProto().getStruct();
      MessageSize size { sproto.getDataWordCount(), sproto.getPointerCount() };
      context.initResults(size);
      return kj::mv(writePromise);
    } else {
      auto paf = kj::newPromiseAndFulfiller<void>();
      parent.awaitedResponses.insert(callId, AwaitedResponse { context, kj::mv(paf.fulfiller) });
      auto promise = writePromise.then([p = kj::mv(paf.promise)]() mutable { return kj::mv(p); });
      auto& parentRef = parent;
      return promise.attach(kj::defer([&parentRef,callId]() {
        parentRef.awaitedResponses.erase(callId);
      }));
    }
  }

private:
  JsonRpc& parent;
};

JsonRpc::JsonRpc(Transport& transport, DynamicCapability::Client interface)
    : JsonRpc(transport, kj::mv(interface), kj::newPromiseAndFulfiller<void>()) {}
JsonRpc::JsonRpc(Transport& transport, DynamicCapability::Client interfaceParam,
                 kj::PromiseFulfillerPair<void> paf)
    : transport(transport),
      interface(kj::mv(interfaceParam)),
      errorPromise(paf.promise.fork()),
      errorFulfiller(kj::mv(paf.fulfiller)),
      readTask(readLoop().eagerlyEvaluate([this](kj::Exception&& e) {
        errorFulfiller->reject(kj::mv(e));
      })),
      tasks(*this) {
  codec.handleByAnnotation(interface.getSchema());
  codec.handleByAnnotation<json::RpcMessage>();

  for (auto method: interface.getSchema().getMethods()) {
    auto proto = method.getProto();
    kj::StringPtr name = proto.getName();
    for (auto annotation: proto.getAnnotations()) {
      switch (annotation.getId()) {
        case JSON_NAME_ANNOTATION_ID:
          name = annotation.getValue().getText();
          break;
      }
    }
    methodMap.insert(name, method);
  }
}

DynamicCapability::Client JsonRpc::getPeer(InterfaceSchema schema) {
  codec.handleByAnnotation(interface.getSchema());
  return kj::heap<CapabilityImpl>(*this, schema);
}

static kj::HttpHeaderTable& staticHeaderTable() {
  static kj::HttpHeaderTable HEADER_TABLE;
  return HEADER_TABLE;
}

kj::Promise<void> JsonRpc::queueWrite(kj::String text) {
  auto fork = writeQueue.then([this, text = kj::mv(text)]() mutable {
    auto promise = transport.send(text);
    return promise.attach(kj::mv(text));
  }).eagerlyEvaluate([this](kj::Exception&& e) {
    errorFulfiller->reject(kj::mv(e));
  }).fork();
  writeQueue = fork.addBranch();
  return fork.addBranch();
}

void JsonRpc::queueError(kj::Maybe<json::Value::Reader> id, int code, kj::StringPtr message) {
  MallocMessageBuilder capnpMessage;
  auto jsonResponse = capnpMessage.getRoot<json::RpcMessage>();
  jsonResponse.setJsonrpc("2.0");
  KJ_IF_MAYBE(i, id) {
    jsonResponse.setId(*i);
  } else {
    jsonResponse.initId().setNull();
  }
  auto error = jsonResponse.initError();
  error.setCode(code);
  error.setMessage(message);

  // OK to discard result of queueWrite() since it's just one branch of a fork.
  queueWrite(codec.encode(jsonResponse));
}

kj::Promise<void> JsonRpc::readLoop() {
  return transport.receive().then([this](kj::String message) -> kj::Promise<void> {
    MallocMessageBuilder capnpMessage;
    auto rpcMessageBuilder = capnpMessage.getRoot<json::RpcMessage>();

    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
      codec.decode(message, rpcMessageBuilder);
    })) {
      queueError(nullptr, -32700, kj::str("Parse error: ", exception->getDescription()));
      return readLoop();
    }

    KJ_CONTEXT("decoding JSON-RPC message", message);

    auto rpcMessage = rpcMessageBuilder.asReader();

    if (!rpcMessage.hasJsonrpc()) {
      queueError(nullptr, -32700, kj::str("Missing 'jsonrpc' field."));
      return readLoop();
    } else if (rpcMessage.getJsonrpc() != "2.0") {
      queueError(nullptr, -32700,
          kj::str("Unknown JSON-RPC version. This peer implements version '2.0'."));
      return readLoop();
    }

    switch (rpcMessage.which()) {
      case json::RpcMessage::NONE:
        queueError(nullptr, -32700, kj::str("message has none of params, result, or error"));
        break;

      case json::RpcMessage::PARAMS: {
        // a call
        KJ_IF_MAYBE(method, methodMap.find(rpcMessage.getMethod())) {
          auto req = interface.newRequest(*method);
          KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
            codec.decode(rpcMessage.getParams(), req);
          })) {
            kj::Maybe<JsonValue::Reader> id;
            if (rpcMessage.hasId()) id = rpcMessage.getId();
            queueError(id, -32602,
                kj::str("Type error in method params: ", exception->getDescription()));
            break;
          }

          if (rpcMessage.hasId()) {
            auto id = rpcMessage.getId();
            auto idCopy = kj::heapArray<word>(id.totalSize().wordCount + 1);
            memset(idCopy.begin(), 0, idCopy.asBytes().size());
            copyToUnchecked(id, idCopy);
            auto idPtr = readMessageUnchecked<json::Value>(idCopy.begin());

            auto promise = req.send()
                .then([this,idPtr](Response<DynamicStruct> response) mutable {
              MallocMessageBuilder capnpMessage;
              auto jsonResponse = capnpMessage.getRoot<json::RpcMessage>();
              jsonResponse.setJsonrpc("2.0");
              jsonResponse.setId(idPtr);
              codec.encode(DynamicStruct::Reader(response), jsonResponse.initResult());
              return queueWrite(codec.encode(jsonResponse));
            }, [this,idPtr](kj::Exception&& e) {
              MallocMessageBuilder capnpMessage;
              auto jsonResponse = capnpMessage.getRoot<json::RpcMessage>();
              jsonResponse.setJsonrpc("2.0");
              jsonResponse.setId(idPtr);
              auto error = jsonResponse.initError();
              switch (e.getType()) {
                case kj::Exception::Type::FAILED:
                  error.setCode(-32000);
                  break;
                case kj::Exception::Type::DISCONNECTED:
                  error.setCode(-32001);
                  break;
                case kj::Exception::Type::OVERLOADED:
                  error.setCode(-32002);
                  break;
                case kj::Exception::Type::UNIMPLEMENTED:
                  error.setCode(-32601);  // method not found
                  break;
              }
              error.setMessage(e.getDescription());
              return queueWrite(codec.encode(jsonResponse));
            });
            tasks.add(promise.attach(kj::mv(idCopy)));
          } else {
            // No 'id', so this is a notification.
            tasks.add(req.send().ignoreResult().catch_([](kj::Exception&& exception) {
              if (exception.getType() != kj::Exception::Type::UNIMPLEMENTED) {
                KJ_LOG(ERROR, "JSON-RPC notification threw exception into the abyss", exception);
              }
            }));
          }
        } else {
          if (rpcMessage.hasId()) {
            queueError(rpcMessage.getId(), -32601, "Method not found");
          } else {
            // Ignore notification for unknown method.
          }
        }
        break;
      }

      case json::RpcMessage::RESULT: {
        auto id = rpcMessage.getId();
        if (!id.isNumber()) {
          // JSON-RPC doesn't define what to do if receiving a response with an invalid id.
          KJ_LOG(ERROR, "JSON-RPC response has invalid ID");
        } else KJ_IF_MAYBE(awaited, awaitedResponses.find((uint)id.getNumber())) {
          KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
            codec.decode(rpcMessage.getResult(), awaited->context.getResults());
            awaited->fulfiller->fulfill();
          })) {
            // Errors always propagate from callee to caller, so we don't want to throw this error
            // back to the server.
            awaited->fulfiller->reject(kj::mv(*exception));
          }
        } else {
          // Probably, this is the response to a call that was canceled.
        }
        break;
      }

      case json::RpcMessage::ERROR: {
        auto id = rpcMessage.getId();
        if (id.isNull()) {
          // Error message will be logged by KJ_CONTEXT, above.
          KJ_LOG(ERROR, "peer reports JSON-RPC protocol error");
        } else if (!id.isNumber()) {
          // JSON-RPC doesn't define what to do if receiving a response with an invalid id.
          KJ_LOG(ERROR, "JSON-RPC response has invalid ID");
        } else KJ_IF_MAYBE(awaited, awaitedResponses.find((uint)id.getNumber())) {
          auto error = rpcMessage.getError();
          auto code = error.getCode();
          kj::Exception::Type type =
              code == -32601 ? kj::Exception::Type::UNIMPLEMENTED
                             : kj::Exception::Type::FAILED;
          awaited->fulfiller->reject(kj::Exception(
              type, __FILE__, __LINE__, kj::str(error.getMessage())));
        } else {
          // Probably, this is the response to a call that was canceled.
        }
        break;
      }
    }

    return readLoop();
  });
}

void JsonRpc::taskFailed(kj::Exception&& exception) {
  errorFulfiller->reject(kj::mv(exception));
}

// =======================================================================================

JsonRpc::ContentLengthTransport::ContentLengthTransport(kj::AsyncIoStream& stream)
    : stream(stream), input(kj::newHttpInputStream(stream, staticHeaderTable())) {}
JsonRpc::ContentLengthTransport::~ContentLengthTransport() noexcept(false) {}

kj::Promise<void> JsonRpc::ContentLengthTransport::send(kj::StringPtr text) {
  auto headers = kj::str("Content-Length: ", text.size(), "\r\n\r\n");
  parts[0] = headers.asBytes();
  parts[1] = text.asBytes();
  return stream.write(parts).attach(kj::mv(headers));
}

kj::Promise<kj::String> JsonRpc::ContentLengthTransport::receive() {
  return input->readMessage()
      .then([](kj::HttpInputStream::Message&& message) {
    auto promise = message.body->readAllText();
    return promise.attach(kj::mv(message.body));
  });
}

}  // namespace capnp
