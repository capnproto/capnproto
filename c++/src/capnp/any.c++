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
        pointer = pointer.getStruct(nullptr).getPointerField(bounded(op.pointerIndex) * POINTERS);
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

Equality AnyStruct::Reader::equals(AnyStruct::Reader right) {
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
    return Equality::NOT_EQUAL;
  }

  if(0 != memcmp(dataL.begin(), dataR.begin(), dataSizeL)) {
    return Equality::NOT_EQUAL;
  }

  auto ptrsL = getPointerSection();
  size_t ptrsSizeL = ptrsL.size();
  while (ptrsSizeL > 0 && ptrsL[ptrsSizeL - 1].isNull()) {
    -- ptrsSizeL;
  }

  auto ptrsR = right.getPointerSection();
  size_t ptrsSizeR = ptrsR.size();
  while (ptrsSizeR > 0 && ptrsR[ptrsSizeR - 1].isNull()) {
    -- ptrsSizeR;
  }

  if(ptrsSizeL != ptrsSizeR) {
    return Equality::NOT_EQUAL;
  }

  size_t i = 0;

  auto eqResult = Equality::EQUAL;
  for (; i < ptrsSizeL; i++) {
    auto l = ptrsL[i];
    auto r = ptrsR[i];
    switch(l.equals(r)) {
      case Equality::EQUAL:
        break;
      case Equality::NOT_EQUAL:
        return Equality::NOT_EQUAL;
      case Equality::UNKNOWN_CONTAINS_CAPS:
        eqResult = Equality::UNKNOWN_CONTAINS_CAPS;
        break;
      default:
        KJ_UNREACHABLE;
    }
  }

  return eqResult;
}

kj::StringPtr KJ_STRINGIFY(Equality res) {
  switch(res) {
    case Equality::NOT_EQUAL:
      return "NOT_EQUAL";
    case Equality::EQUAL:
      return "EQUAL";
    case Equality::UNKNOWN_CONTAINS_CAPS:
      return "UNKNOWN_CONTAINS_CAPS";
  }
  KJ_UNREACHABLE;
}

Equality AnyList::Reader::equals(AnyList::Reader right) {
  if(size() != right.size()) {
    return Equality::NOT_EQUAL;
  }

  if (getElementSize() != right.getElementSize()) {
    return Equality::NOT_EQUAL;
  }

  auto eqResult = Equality::EQUAL;
  switch(getElementSize()) {
    case ElementSize::VOID:
    case ElementSize::BIT:
    case ElementSize::BYTE:
    case ElementSize::TWO_BYTES:
    case ElementSize::FOUR_BYTES:
    case ElementSize::EIGHT_BYTES: {
      size_t cmpSize = getRawBytes().size();

      if (getElementSize() == ElementSize::BIT && size() % 8 != 0) {
        // The list does not end on a byte boundary. We need special handling for the final
        // byte because we only care about the bits that are actually elements of the list.

        uint8_t mask = (1 << (size() % 8)) - 1; // lowest size() bits set
        if ((getRawBytes()[cmpSize - 1] & mask) != (right.getRawBytes()[cmpSize - 1] & mask)) {
          return Equality::NOT_EQUAL;
        }
        cmpSize -= 1;
      }

      if (memcmp(getRawBytes().begin(), right.getRawBytes().begin(), cmpSize) == 0) {
        return Equality::EQUAL;
      } else {
        return Equality::NOT_EQUAL;
      }
    }
    case ElementSize::POINTER:
    case ElementSize::INLINE_COMPOSITE: {
      auto llist = as<List<AnyStruct>>();
      auto rlist = right.as<List<AnyStruct>>();
      for(size_t i = 0; i < size(); i++) {
        switch(llist[i].equals(rlist[i])) {
          case Equality::EQUAL:
            break;
          case Equality::NOT_EQUAL:
            return Equality::NOT_EQUAL;
          case Equality::UNKNOWN_CONTAINS_CAPS:
            eqResult = Equality::UNKNOWN_CONTAINS_CAPS;
            break;
          default:
            KJ_UNREACHABLE;
        }
      }
      return eqResult;
    }
  }
  KJ_UNREACHABLE;
}

Equality AnyPointer::Reader::equals(AnyPointer::Reader right) {
  if(getPointerType() != right.getPointerType()) {
    return Equality::NOT_EQUAL;
  }
  switch(getPointerType()) {
    case PointerType::NULL_:
      return Equality::EQUAL;
    case PointerType::STRUCT:
      return getAs<AnyStruct>().equals(right.getAs<AnyStruct>());
    case PointerType::LIST:
      return getAs<AnyList>().equals(right.getAs<AnyList>());
    case PointerType::CAPABILITY:
      return Equality::UNKNOWN_CONTAINS_CAPS;
  }
  // There aren't currently any other types of pointers
  KJ_UNREACHABLE;
}

bool AnyPointer::Reader::operator==(AnyPointer::Reader right) {
  switch(equals(right)) {
    case Equality::EQUAL:
      return true;
    case Equality::NOT_EQUAL:
      return false;
    case Equality::UNKNOWN_CONTAINS_CAPS:
      KJ_FAIL_REQUIRE(
        "operator== cannot determine equality of capabilities; use equals() instead if you need to handle this case");
  }
  KJ_UNREACHABLE;
}

bool AnyStruct::Reader::operator==(AnyStruct::Reader right) {
  switch(equals(right)) {
    case Equality::EQUAL:
      return true;
    case Equality::NOT_EQUAL:
      return false;
    case Equality::UNKNOWN_CONTAINS_CAPS:
      KJ_FAIL_REQUIRE(
        "operator== cannot determine equality of capabilities; use equals() instead if you need to handle this case");
  }
  KJ_UNREACHABLE;
}

bool AnyList::Reader::operator==(AnyList::Reader right) {
  switch(equals(right)) {
    case Equality::EQUAL:
      return true;
    case Equality::NOT_EQUAL:
      return false;
    case Equality::UNKNOWN_CONTAINS_CAPS:
      KJ_FAIL_REQUIRE(
        "operator== cannot determine equality of capabilities; use equals() instead if you need to handle this case");
  }
  KJ_UNREACHABLE;
}

}  // namespace capnp
