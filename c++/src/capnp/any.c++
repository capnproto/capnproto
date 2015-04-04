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

#include <kj/debug.h>

#if !CAPNP_LITE
#include "capability.h"
#endif  // !CAPNP_LITE

namespace capnp {

#if !CAPNP_LITE

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

#endif  // !CAPNP_LITE

StructEqualityResult AnyStruct::Reader::equals(AnyStruct::Reader right) {
  auto dataL = getDataSection();
  size_t dataSizeL = dataL.size();
  while(dataSizeL > 0 && dataL[dataSizeL - 1] == 0) {
    -- dataSizeL;
  }

  auto dataR = right.getDataSection();
  size_t dataSizeR = dataR.size();
  while(dataSizeR > 0 && dataR[dataSizeR - 1] == 0) {
    -- dataSizeR;
  }

  if(dataSizeL != dataSizeR) {
    return StructEqualityResult::NOT_EQUAL;
  }

  if(0 != memcmp(dataL.begin(), dataR.begin(), dataSizeL)) {
    return StructEqualityResult::NOT_EQUAL;
  }

  auto ptrsL = getPointerSection();
  auto ptrsR = right.getPointerSection();

  size_t i = 0;

  for(; i < kj::min(ptrsL.size(), ptrsR.size()); i++) {
    auto l = ptrsL[i];
    auto r = ptrsR[i];
    switch(l.equals(r)) {
      case StructEqualityResult::EQUAL:
        break;
      case StructEqualityResult::NOT_EQUAL:
        return StructEqualityResult::NOT_EQUAL;
      case StructEqualityResult::UNKNOWN_CONTAINS_CAPS:
        return StructEqualityResult::UNKNOWN_CONTAINS_CAPS;
    }
  }

  return StructEqualityResult::EQUAL;

}

kj::StringPtr KJ_STRINGIFY(StructEqualityResult res) {
  switch(res) {
    case StructEqualityResult::NOT_EQUAL:
      return "NOT_EQUAL";
    case StructEqualityResult::EQUAL:
      return "EQUAL";
    case StructEqualityResult::UNKNOWN_CONTAINS_CAPS:
      return "UNKNOWN_CONTAINS_CAPS";
  }
}

StructEqualityResult AnyList::Reader::equals(AnyList::Reader right) {
  if(size() != right.size()) {
    return StructEqualityResult::NOT_EQUAL;
  }
  switch(getElementSize()) {
    case ElementSize::VOID:
    case ElementSize::BIT:
    case ElementSize::BYTE:
    case ElementSize::TWO_BYTES:
    case ElementSize::FOUR_BYTES:
    case ElementSize::EIGHT_BYTES:
      if(getElementSize() == right.getElementSize()) {
        if(memcmp(getRawBytes().begin(), right.getRawBytes().begin(), getRawBytes().size()) == 0) {
          return StructEqualityResult::EQUAL;
        } else {
          return StructEqualityResult::NOT_EQUAL;
        }
      } else {
        return StructEqualityResult::NOT_EQUAL;
      }
    case ElementSize::POINTER:
    case ElementSize::INLINE_COMPOSITE: {
      auto llist = as<List<AnyStruct>>();
      auto rlist = right.as<List<AnyStruct>>();
      for(size_t i = 0; i < size(); i++) {
        switch(llist[i].equals(rlist[i])) {
          case StructEqualityResult::EQUAL:
            break;
          case StructEqualityResult::NOT_EQUAL:
            return StructEqualityResult::NOT_EQUAL;
          case StructEqualityResult::UNKNOWN_CONTAINS_CAPS:
            return StructEqualityResult::UNKNOWN_CONTAINS_CAPS;
        }
      }
      return StructEqualityResult::EQUAL;
    }
  }
}

StructEqualityResult AnyPointer::Reader::equals(AnyPointer::Reader right) {
  if(right.isCapability()) {
    return StructEqualityResult::UNKNOWN_CONTAINS_CAPS;
  }
  if(isNull()) {
    if(right.isNull()) {
      return StructEqualityResult::EQUAL;
    } else {
      return StructEqualityResult::NOT_EQUAL;
    }
  } else if(isStruct()) {
    if(right.isStruct()) {
      return getAs<AnyStruct>().equals(right.getAs<AnyStruct>());
    } else {
      return StructEqualityResult::NOT_EQUAL;
    }
  } else if(isList()) {
    if(right.isList()) {
      return getAs<AnyList>().equals(right.getAs<AnyList>());
    } else {
      return StructEqualityResult::NOT_EQUAL;
    }
  } else if(isCapability()) {
    return StructEqualityResult::UNKNOWN_CONTAINS_CAPS;
  } else {
    // There aren't currently any other types of pointers
    KJ_FAIL_REQUIRE();
  }
}

bool AnyPointer::Reader::operator ==(AnyPointer::Reader right) {
  switch(equals(right)) {
    case StructEqualityResult::EQUAL:
      return true;
    case StructEqualityResult::NOT_EQUAL:
      return false;
    case StructEqualityResult::UNKNOWN_CONTAINS_CAPS:
      KJ_FAIL_REQUIRE();
  }
}

bool AnyStruct::Reader::operator ==(AnyStruct::Reader right) {
  switch(equals(right)) {
    case StructEqualityResult::EQUAL:
      return true;
    case StructEqualityResult::NOT_EQUAL:
      return false;
    case StructEqualityResult::UNKNOWN_CONTAINS_CAPS:
      KJ_FAIL_REQUIRE();
  }
}

bool AnyList::Reader::operator ==(AnyList::Reader right) {
  switch(equals(right)) {
    case StructEqualityResult::EQUAL:
      return true;
    case StructEqualityResult::NOT_EQUAL:
      return false;
    case StructEqualityResult::UNKNOWN_CONTAINS_CAPS:
      KJ_FAIL_REQUIRE();
  }
}

}  // namespace capnp
