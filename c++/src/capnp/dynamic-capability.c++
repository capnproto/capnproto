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

// This file contains the parts of dynamic.h that live in capnp-rpc.so.

#include "dynamic.h"
#include <kj/debug.h>

namespace capnp {

DynamicCapability::Client DynamicCapability::Client::upcast(InterfaceSchema requestedSchema) {
  KJ_REQUIRE(schema.extends(requestedSchema), "Can't upcast to non-superclass.") {}
  return DynamicCapability::Client(requestedSchema, hook->addRef());
}

Request<DynamicStruct, DynamicStruct> DynamicCapability::Client::newRequest(
    InterfaceSchema::Method method, kj::Maybe<MessageSize> sizeHint) {
  auto methodInterface = method.getContainingInterface();

  KJ_REQUIRE(schema.extends(methodInterface), "Interface does not implement this method.");

  auto paramType = method.getParamType();
  auto resultType = method.getResultType();

  auto typeless = hook->newCall(
      methodInterface.getProto().getId(), method.getIndex(), sizeHint);

  return Request<DynamicStruct, DynamicStruct>(
      typeless.getAs<DynamicStruct>(paramType), kj::mv(typeless.hook), resultType);
}

Request<DynamicStruct, DynamicStruct> DynamicCapability::Client::newRequest(
    kj::StringPtr methodName, kj::Maybe<MessageSize> sizeHint) {
  return newRequest(schema.getMethodByName(methodName), sizeHint);
}

kj::Promise<void> DynamicCapability::Server::dispatchCall(
    uint64_t interfaceId, uint16_t methodId,
    CallContext<AnyPointer, AnyPointer> context) {
  KJ_IF_MAYBE(interface, schema.findSuperclass(interfaceId)) {
    auto methods = interface->getMethods();
    if (methodId < methods.size()) {
      auto method = methods[methodId];
      return call(method, CallContext<DynamicStruct, DynamicStruct>(*context.hook,
          method.getParamType(), method.getResultType()));
    } else {
      return internalUnimplemented(
          interface->getProto().getDisplayName().cStr(), interfaceId, methodId);
    }
  } else {
    return internalUnimplemented(schema.getProto().getDisplayName().cStr(), interfaceId);
  }
}

RemotePromise<DynamicStruct> Request<DynamicStruct, DynamicStruct>::send() {
  auto typelessPromise = hook->send();
  auto resultSchemaCopy = resultSchema;

  // Convert the Promise to return the correct response type.
  // Explicitly upcast to kj::Promise to make clear that calling .then() doesn't invalidate the
  // Pipeline part of the RemotePromise.
  auto typedPromise = kj::implicitCast<kj::Promise<Response<AnyPointer>>&>(typelessPromise)
      .then([=](Response<AnyPointer>&& response) -> Response<DynamicStruct> {
        return Response<DynamicStruct>(response.getAs<DynamicStruct>(resultSchemaCopy),
                                       kj::mv(response.hook));
      });

  // Wrap the typeless pipeline in a typed wrapper.
  DynamicStruct::Pipeline typedPipeline(resultSchema,
      kj::mv(kj::implicitCast<AnyPointer::Pipeline&>(typelessPromise)));

  return RemotePromise<DynamicStruct>(kj::mv(typedPromise), kj::mv(typedPipeline));
}

}  // namespace capnp
