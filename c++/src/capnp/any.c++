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

#include "any.h"
#include "capability.h"

namespace capnp {

kj::Own<ClientHook> PipelineHook::getPipelinedCap(kj::Array<PipelineOp>&& ops) {
  return getPipelinedCap(ops.asPtr());
}

kj::Own<ClientHook> AnyPointer::Reader::getPipelinedCap(
    kj::ArrayPtr<const PipelineOp> ops) const {
  _::PointerReader pointer = reader;

  for (auto& op: ops) {
    switch (op.type) {
      case PipelineOp::Type::NOOP:
        break;

      case PipelineOp::Type::GET_POINTER_FIELD:
        pointer = pointer.getStruct(nullptr).getPointerField(op.pointerIndex * POINTERS);
        break;
    }
  }

  return pointer.getCapability();
}

AnyPointer::Pipeline AnyPointer::Pipeline::noop() {
  auto newOps = kj::heapArray<PipelineOp>(ops.size());
  for (auto i: kj::indices(ops)) {
    newOps[i] = ops[i];
  }
  return Pipeline(hook->addRef(), kj::mv(newOps));
}

AnyPointer::Pipeline AnyPointer::Pipeline::getPointerField(uint16_t pointerIndex) {
  auto newOps = kj::heapArray<PipelineOp>(ops.size() + 1);
  for (auto i: kj::indices(ops)) {
    newOps[i] = ops[i];
  }
  auto& newOp = newOps[ops.size()];
  newOp.type = PipelineOp::GET_POINTER_FIELD;
  newOp.pointerIndex = pointerIndex;

  return Pipeline(hook->addRef(), kj::mv(newOps));
}

kj::Own<ClientHook> AnyPointer::Pipeline::asCap() {
  return hook->getPipelinedCap(ops);
}

}  // namespace capnp
