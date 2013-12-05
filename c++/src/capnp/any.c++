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
