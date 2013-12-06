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

  auto proto = method.getProto();
  auto paramType = methodInterface.getDependency(proto.getParamStructType()).asStruct();
  auto resultType = methodInterface.getDependency(proto.getResultStructType()).asStruct();

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
      auto proto = method.getProto();
      return call(method, CallContext<DynamicStruct, DynamicStruct>(*context.hook,
          interface->getDependency(proto.getParamStructType()).asStruct(),
          interface->getDependency(proto.getResultStructType()).asStruct()));
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
