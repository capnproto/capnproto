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

#include "wire-format.h"
#include "arena.h"
#include "descriptor.h"

namespace capnproto {

namespace internal {
namespace debug {

bool fieldIsStruct(const StructDescriptor* descriptor, int fieldNumber, int refIndex) {
  return descriptor->fieldCount > fieldNumber &&
         descriptor->fields[fieldNumber].size == FieldSize::REFERENCE &&
         descriptor->fields[fieldNumber].offset == refIndex &&
         descriptor->defaultReferences[refIndex]->kind == Descriptor::Kind::STRUCT;
}

bool fieldIsList(const StructDescriptor* descriptor, int fieldNumber, int refIndex) {
  return descriptor->fieldCount > fieldNumber &&
         descriptor->fields[fieldNumber].size == FieldSize::REFERENCE &&
         descriptor->fields[fieldNumber].offset == refIndex &&
         descriptor->defaultReferences[refIndex]->kind == Descriptor::Kind::LIST;
}

bool fieldIsData(const StructDescriptor* descriptor, int fieldNumber, int dataOffset, int bitSize) {
  return descriptor->fieldCount > fieldNumber &&
         descriptor->fields[fieldNumber].size != FieldSize::REFERENCE &&
         sizeInBits(descriptor->fields[fieldNumber].size) == bitSize &&
         descriptor->fields[fieldNumber].offset == dataOffset;
}

bool dataFieldInRange(const StructDescriptor* descriptor, uint32_t dataOffset, uint32_t size) {
  return descriptor->dataSize * sizeof(uint64_t) >= (dataOffset * size);
}

bool bitFieldInRange(const StructDescriptor* descriptor, uint32_t offset) {
  return descriptor->dataSize * sizeof(uint64_t) > offset / 64;
}

bool refFieldIsStruct(const StructDescriptor* descriptor, int refIndex) {
  return descriptor->referenceCount > refIndex &&
         descriptor->defaultReferences[refIndex]->kind == Descriptor::Kind::STRUCT;
}

bool refFieldIsList(const StructDescriptor* descriptor, int refIndex) {
  return descriptor->referenceCount > refIndex &&
         descriptor->defaultReferences[refIndex]->kind == Descriptor::Kind::LIST;
}

bool elementsAreStructs(const ListDescriptor* descriptor) {
  return descriptor->elementSize == FieldSize::STRUCT;
}
bool elementsAreStructs(const ListDescriptor* descriptor, uint32_t wordSize) {
  return descriptor->elementSize == FieldSize::STRUCT &&
         descriptor->elementDescriptor->asStruct()->wordSize() == wordSize;
}
bool elementsAreLists(const ListDescriptor* descriptor) {
  return descriptor->elementSize == FieldSize::REFERENCE;
}
bool elementsAreData(const ListDescriptor* descriptor, int bitSize) {
  switch (descriptor->elementSize) {
    case FieldSize::REFERENCE:
    case FieldSize::KEY_REFERENCE:
    case FieldSize::STRUCT:
      return false;
    default:
      return true;
  }
}

}  // namespace debug
}  // namespace internal

// =======================================================================================

struct WireReference {
  // A reference, in exactly the format in which it appears on the wire.

  // Copying and moving is not allowed because the offset would become wrong.
  WireReference(const WireReference& other) = delete;
  WireReference(WireReference&& other) = delete;
  WireReference& operator=(const WireReference& other) = delete;
  WireReference& operator=(WireReference&& other) = delete;

  enum Tag {
    STRUCT = 0,
    LIST = 1,
    CAPABILITY = 2,
    FAR = 3
  };

  WireValue<uint32_t> offsetAndTag;

  union {
    struct {
      WireValue<uint8_t> fieldCount;
      WireValue<uint8_t> dataSize;
      WireValue<uint8_t> refCount;
      WireValue<uint8_t> reserved0;
    } structRef;
    // Also covers capabilities.

    struct {
      WireValue<uint32_t> elementSizeAndCount;

      CAPNPROTO_ALWAYS_INLINE(internal::FieldSize elementSize() const) {
        return static_cast<internal::FieldSize>(elementSizeAndCount.get() >> 29);
      }
      CAPNPROTO_ALWAYS_INLINE(uint32_t elementCount() const) {
        return elementSizeAndCount.get() & 0x1fffffffu;
      }
    } listRef;

    struct {
      WireValue<uint32_t> segmentId;
    } farRef;
  };

  CAPNPROTO_ALWAYS_INLINE(bool isNull() const) { return offsetAndTag.get() == 0; }
  CAPNPROTO_ALWAYS_INLINE(uint32_t offset() const) { return offsetAndTag.get() & ~7; }
  CAPNPROTO_ALWAYS_INLINE(int tag() const) { return offsetAndTag.get() & 7; }

  CAPNPROTO_ALWAYS_INLINE(void setTagAndOffset(Tag tag, uint32_t offset)) {
    CAPNPROTO_DEBUG_ASSERT((offset & 7) == 0, "Offsets must be word-aligned.");
    offsetAndTag.set(offset | tag);
  }

  CAPNPROTO_ALWAYS_INLINE(void setStruct(
      const internal::StructDescriptor* descriptor, uint32_t offset)) {
    setTagAndOffset(STRUCT, offset);
    structRef.fieldCount.set(descriptor->fieldCount);
    structRef.dataSize.set(descriptor->dataSize);
    structRef.refCount.set(descriptor->referenceCount);
    structRef.reserved0.set(0);
  }

  CAPNPROTO_ALWAYS_INLINE(void setList(
      const internal::ListDescriptor* descriptor, uint32_t elementCount, uint32_t offset)) {
    setTagAndOffset(LIST, offset);
    CAPNPROTO_DEBUG_ASSERT((elementCount >> 29) == 0, "Lists are limited to 2**29 elements.");
    listRef.elementSizeAndCount.set(
        (static_cast<uint32_t>(descriptor->elementSize) << 29) | elementCount);
  }

  CAPNPROTO_ALWAYS_INLINE(void setFar(uint32_t segmentId, uint32_t offset)) {
    setTagAndOffset(FAR, offset);
    farRef.segmentId.set(segmentId);
  }
};
static_assert(sizeof(WireReference) == 8, "Layout of capnproto::WireReference is wrong.");

// =======================================================================================

template <typename T>
static CAPNPROTO_ALWAYS_INLINE(T divRoundingUp(T a, T b));
template <typename T>
static inline T divRoundingUp(T a, T b) {
  return (a + b - 1) / b;
}

template <typename T>
static inline const void* offsetPtr(const void* ptr, int amount) {
  return reinterpret_cast<const T*>(ptr) + amount;
}

template <typename T>
static inline void* offsetPtr(void* ptr, int amount) {
  return reinterpret_cast<T*>(ptr) + amount;
}

struct WireHelpers {
  static CAPNPROTO_ALWAYS_INLINE(void* allocate(
      WireReference*& ref, Segment*& segment, uint32_t size)) {
    void* ptr = segment->allocate(size);

    if (ptr == nullptr) {
      // Need to allocate in a new segment.

      // Loop here just in case we ever make Segment::allocate() thread-safe -- in this case another
      // thread could have grabbed the space between when we asked the arena for the segment and
      // when we asked the segment to allocate space.
      do {
        segment = segment->getArena()->getSegmentWithAvailable(size + sizeof(WireReference));
        ptr = segment->allocate(size + sizeof(WireReference));
      } while (CAPNPROTO_EXPECT_FALSE(ptr == nullptr));

      ref->setFar(segment->getSegmentId(), segment->getOffset(ptr));
      ref = reinterpret_cast<WireReference*>(ptr);

      // Allocated space follows new reference.
      return ref + 1;
    } else {
      return ptr;
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(void followFars(WireReference*& ref, Segment*& segment)) {
    if (ref->tag() == WireReference::FAR) {
      segment = segment->getArena()->getWritableSegment(ref->farRef.segmentId.get());
      ref = reinterpret_cast<WireReference*>(segment->getPtrUnchecked(ref->offset()));
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(bool followFars(
      const WireReference*& ref, const Segment*& segment)) {
    if (ref->tag() == WireReference::FAR) {
      segment = segment->getArena()->getSegment(ref->farRef.segmentId.get());
      ref = reinterpret_cast<const WireReference*>(
          segment->getPtrChecked(ref->offset(), 0, sizeof(WireReference)));
      return CAPNPROTO_EXPECT_TRUE(ref != nullptr);
    } else {
      return true;
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(bool isStructCompatible(
      const internal::StructDescriptor* descriptor, const WireReference* ref)) {
    if (ref->structRef.fieldCount.get() >= descriptor->fieldCount) {
      // The incoming struct has all of the fields that we know about.
      return ref->structRef.dataSize.get() >= descriptor->dataSize &&
             ref->structRef.refCount.get() >= descriptor->referenceCount;
    } else if (ref->structRef.fieldCount.get() > 0) {
      // We know about more fields than the struct has, and the struct is non-empty.
      const internal::FieldDescriptor* field =
          &descriptor->fields[ref->structRef.fieldCount.get() - 1];
      return ref->structRef.dataSize.get() >= field->requiredDataSize &&
             ref->structRef.refCount.get() >= field->requiredReferenceSize;
    } else {
      // The incoming struct has no fields, so is necessarily compatible.
      return true;
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(StructPtr initStructReference(
      const internal::StructDescriptor* descriptor, WireReference* ref, Segment* segment)) {
    if (ref->isNull()) {
      // Calculate the size of the struct.
      uint32_t size = (descriptor->dataSize + descriptor->referenceCount) * sizeof(uint64_t);

      // Allocate space for the new struct.
      void* ptr = allocate(ref, segment, size);

      // Advance the pointer to point between the data and reference segments.
      ptr = offsetPtr<uint64_t>(ptr, descriptor->dataSize);

      // Initialize the reference.
      ref->setStruct(descriptor, segment->getOffset(ptr));

      // Build the StructPtr.
      return StructPtr(descriptor, segment, ptr);
    } else {
      followFars(ref, segment);

      CAPNPROTO_ASSERT(ref->tag() == WireReference::STRUCT,
          "Called getStruct{Field,Element}() but existing reference is not a struct.");
      CAPNPROTO_ASSERT(ref->structRef.fieldCount == fieldDescriptor->fieldCount,
          "Trying to update struct with incorrect field count.");
      CAPNPROTO_ASSERT(ref->structRef.dataSize == fieldDescriptor->dataSize,
          "Trying to update struct with incorrect data size.");
      CAPNPROTO_ASSERT(ref->structRef.refCount == fieldDescriptor->referenceCount,
          "Trying to update struct with incorrect reference count.");

      return StructPtr(descriptor, segment, segment->getPtrUnchecked(ref->offset()));
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(ListPtr initListReference(
      const internal::ListDescriptor* descriptor, WireReference* ref,
      Segment* segment, uint32_t elementCount)) {
    if (descriptor->elementSize == internal::FieldSize::STRUCT) {
      const internal::StructDescriptor* elementDescriptor =
          descriptor->elementDescriptor->asStruct();

      // Allocate the list, prefixed by a single WireReference.
      WireReference* structRef = reinterpret_cast<WireReference*>(
          allocate(ref, segment, sizeof(WireReference) +
              elementDescriptor->wordSize() * elementCount * sizeof(uint64_t)));
      void* ptr = offsetPtr<uint64_t>(structRef + 1, elementDescriptor->dataSize);

      // Initialize the reference.
      ref->setList(descriptor, elementCount, segment->getOffset(structRef));

      // Initialize the struct reference.
      structRef->setStruct(elementDescriptor, segment->getOffset(ptr));

      // Build the ListPtr.
      return ListPtr(descriptor, segment, ptr, elementCount);
    } else {
      // Calculate size of the list.
      uint32_t size = divRoundingUp<uint32_t>(
          static_cast<uint32_t>(sizeInBits(descriptor->elementSize)) * elementCount, 8);

      // Allocate the list.
      void* ptr = allocate(ref, segment, size);

      // Initialize the reference.
      ref->setList(descriptor, elementCount, segment->getOffset(ptr));

      // Build the ListPtr.
      return ListPtr(descriptor, segment, ptr, elementCount);
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(ListPtr getWritableListReference(
      const internal::ListDescriptor* descriptor, WireReference* ref,
      Segment* segment)) {
    if (ref->isNull()) {
      return ListPtr(descriptor, segment, nullptr, 0);
    }

    followFars(ref, segment);

    CAPNPROTO_ASSERT(ref->tag() == WireReference::LIST,
        "Called getList{Field,Element}() but existing reference is not a list.");

    if (descriptor->elementSize == internal::FieldSize::STRUCT) {
      WireReference* structRef = reinterpret_cast<WireReference*>(
          segment->getPtrUnchecked(ref->offset()));
      return ListPtr(descriptor, segment,
          segment->getPtrUnchecked(structRef->offset()), ref->listRef.elementCount());
    } else {
      return ListPtr(descriptor, segment,
          segment->getPtrUnchecked(ref->offset()), ref->listRef.elementCount());
    }
  }

  static CAPNPROTO_ALWAYS_INLINE(StructReadPtr readStructReference(
      const internal::StructDescriptor* descriptor, const WireReference* ref,
      const Segment* segment, int recursionLimit)) {
    do {
      if (ref == nullptr || ref->isNull()) {
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(recursionLimit == 0)) {
        segment->getArena()->parseError(
            "Message is too deeply-nested or contains cycles.");
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(!followFars(ref, segment))) {
        segment->getArena()->parseError(
            "Message contains out-of-bounds far reference.");
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(ref->tag() != WireReference::STRUCT)) {
        segment->getArena()->parseError(
            "Message contains non-struct reference where struct reference was expected.");
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(!isStructCompatible(descriptor, ref))) {
        segment->getArena()->parseError(
            "Message contains struct that is too small for its field count.");
        break;
      }

      const void* ptr = segment->getPtrChecked(ref->offset(),
          ref->structRef.dataSize.get() * sizeof(uint8_t),
          ref->structRef.refCount.get() * sizeof(WireReference));

      if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
        segment->getArena()->parseError("Message contained out-of-bounds struct reference.");
        break;
      }

      return StructReadPtr(descriptor, segment, ptr, descriptor->defaultData,
                           ref->structRef.fieldCount.get(), 0, recursionLimit - 1);
    } while (false);

    return StructReadPtr(descriptor, segment, nullptr, descriptor->defaultData, 0, 0, 0);
  }

  static CAPNPROTO_ALWAYS_INLINE(ListReadPtr readListReference(
      const internal::ListDescriptor* descriptor, const WireReference* ref, const Segment* segment,
      int recursionLimit)) {
    do {
      if (ref == nullptr || ref->isNull()) {
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(recursionLimit == 0)) {
        segment->getArena()->parseError(
            "Message is too deeply-nested or contains cycles.");
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(!followFars(ref, segment))) {
        segment->getArena()->parseError(
            "Message contains out-of-bounds far reference.");
        break;
      }

      if (CAPNPROTO_EXPECT_FALSE(ref->tag() != WireReference::LIST)) {
        segment->getArena()->parseError(
            "Message contains non-list reference where list reference was expected.");
        break;
      }

      if (ref->listRef.elementSize() == internal::FieldSize::STRUCT) {
        // A struct list reference actually points to a struct reference which in turn points to the
        // first struct in the list.
        const void* ptrPtr =
            segment->getPtrChecked(ref->offset(), 0, sizeof(WireReference));
        if (CAPNPROTO_EXPECT_FALSE(ptrPtr == nullptr)) {
          segment->getArena()->parseError(
              "Message contains out-of-bounds list reference.");
          break;
        }

        uint32_t size = ref->listRef.elementCount();
        ref = reinterpret_cast<const WireReference*>(ptrPtr);

        if (CAPNPROTO_EXPECT_FALSE(ref->tag() != WireReference::STRUCT)) {
          segment->getArena()->parseError(
              "Message contains struct list reference that does not point to a struct reference.");
          break;
        }

        int step = (ref->structRef.dataSize.get() + ref->structRef.refCount.get()) *
            sizeof(uint8_t);
        const void* ptr = segment->getPtrChecked(ref->offset(),
            ref->structRef.dataSize.get() * sizeof(uint8_t),
            ref->structRef.refCount.get() * sizeof(WireReference) +
            step * (size - 1));
        if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
          segment->getArena()->parseError(
              "Message contains out-of-bounds struct list reference.");
          break;
        }

        // If a struct list was not expected, then presumably a non-struct list was upgraded to a
        // struct list.  We need to manipulate the pointer to point at the first field of the
        // struct.  Together with the "stepBits", this will allow the struct list to be accessed as
        // if it were a primitive list without branching.
        ptr = offsetPtr<uint8_t>(ptr, -byteOffsetForFieldZero(descriptor->elementSize));

        // Check whether the size is compatible.
        bool compatible = true;
        switch (descriptor->elementSize) {
          case internal::FieldSize::BIT:
          case internal::FieldSize::BYTE:
          case internal::FieldSize::TWO_BYTES:
          case internal::FieldSize::FOUR_BYTES:
          case internal::FieldSize::EIGHT_BYTES:
            compatible = ref->structRef.dataSize.get() > 0;
            break;

          case internal::FieldSize::REFERENCE:
            compatible = ref->structRef.refCount.get() > 0;
            break;

          case internal::FieldSize::KEY_REFERENCE:
            compatible = ref->structRef.dataSize.get() > 0 &&
                         ref->structRef.refCount.get() > 0;
            break;

          case internal::FieldSize::STRUCT: {
            compatible = isStructCompatible(descriptor->elementDescriptor->asStruct(), ref);
            break;
          }
        }

        if (CAPNPROTO_EXPECT_FALSE(!compatible)) {
          segment->getArena()->parseError("A list had incompatible element type.");
          break;
        }

        return ListReadPtr(descriptor, segment, ptr, size, step * 8,
            ref->structRef.fieldCount.get(), recursionLimit - 1);

      } else {
        // The elements of the list are NOT structs.
        int step = sizeInBits(ref->listRef.elementSize());

        const void* ptr = segment->getPtrChecked(ref->offset(), 0,
            divRoundingUp<uint64_t>(
                implicit_cast<uint64_t>(ref->listRef.elementCount()) * step, 8));

        if (CAPNPROTO_EXPECT_FALSE(ptr == nullptr)) {
          segment->getArena()->parseError("Message contained out-of-bounds list reference.");
          break;
        }

        if (descriptor->elementSize == ref->listRef.elementSize()) {
          return ListReadPtr(descriptor, segment, ptr, ref->listRef.elementCount(),
                             sizeInBits(ref->listRef.elementSize()), 0, recursionLimit);
        } else if (descriptor->elementSize == internal::FieldSize::STRUCT) {
          // We were expecting a struct, but we received a list of some other type.  Perhaps a
          // non-struct list was recently upgraded to a struct list, but the sender is using the
          // old version of the protocol.  We need to verify that the struct's first field matches
          // what the sender sent us.
          const internal::StructDescriptor* elementDescriptor =
              descriptor->elementDescriptor->asStruct();
          if (CAPNPROTO_EXPECT_FALSE(
              elementDescriptor->fieldCount == 0 ||
              elementDescriptor->fields[0].size != ref->listRef.elementSize())) {
            segment->getArena()->parseError("A list had incompatible element type.");
            break;
          }

          // Adjust the pointer to point where we expect it for a struct.
          ptr = offsetPtr<uint8_t>(ptr, byteOffsetForFieldZero(descriptor->elementSize));

          return ListReadPtr(descriptor, segment, ptr, ref->listRef.elementCount(),
                             sizeInBits(ref->listRef.elementSize()), 1, recursionLimit);
        } else {
          segment->getArena()->parseError("A list had incompatible element type.");
          break;
        }
      }
    } while (false);

    switch (descriptor->elementSize) {
      case internal::FieldSize::REFERENCE:
      case internal::FieldSize::KEY_REFERENCE:
      case internal::FieldSize::STRUCT:
        return ListReadPtr(descriptor, segment, nullptr, descriptor->defaultCount, 0, 0,
            recursionLimit - 1);
      default:
        return ListReadPtr(descriptor, segment, descriptor->defaultData, descriptor->defaultCount,
            internal::sizeInBits(descriptor->elementSize), 0, recursionLimit - 1);
    }
  }
};

// =======================================================================================

StructPtr StructPtr::getStructFieldInternal(int refIndex) const {
  return WireHelpers::initStructReference(
      descriptor->defaultReferences[refIndex]->asStruct(),
      reinterpret_cast<WireReference*>(ptr) + refIndex, segment);
}

ListPtr StructPtr::initListFieldInternal(int refIndex, uint32_t elementCount) const {
  return WireHelpers::initListReference(
      descriptor->defaultReferences[refIndex]->asList(),
      reinterpret_cast<WireReference*>(ptr) + refIndex, segment, elementCount);
}

ListPtr StructPtr::getListFieldInternal(int refIndex) const {
  return WireHelpers::getWritableListReference(
      descriptor->defaultReferences[refIndex]->asList(),
      reinterpret_cast<WireReference*>(ptr) + refIndex, segment);
}

StructReadPtr StructPtr::asReadPtr() const {
  return StructReadPtr(descriptor, segment, ptr, descriptor->defaultData,
                       descriptor->fieldCount, 0, 1 << 30);
}

StructReadPtr StructReadPtr::getStructFieldInternal(int fieldNumber, unsigned int refIndex) const {
  return WireHelpers::readStructReference(
      descriptor->defaultReferences[refIndex]->asStruct(),
      fieldNumber < fieldCount
          ? reinterpret_cast<const WireReference*>(ptr) + refIndex
          : nullptr,
      segment, recursionLimit);
}

ListReadPtr StructReadPtr::getListFieldInternal(int fieldNumber, unsigned int refIndex) const {
  return WireHelpers::readListReference(
      descriptor->defaultReferences[refIndex]->asList(),
      fieldNumber < fieldCount
          ? reinterpret_cast<const WireReference*>(ptr) + refIndex
          : nullptr,
      segment, recursionLimit);
}

StructPtr ListPtr::getStructElementInternal(unsigned int index, uint32_t elementWordSize) const {
  return StructPtr(
      descriptor->elementDescriptor->asStruct(), segment,
      offsetPtr<uint64_t>(ptr, elementWordSize * index));
}

ListPtr ListPtr::initListElementInternal(unsigned int index, uint32_t size) const {
  return WireHelpers::initListReference(
      descriptor->elementDescriptor->asList(),
      reinterpret_cast<WireReference*>(ptr) + index,
      segment, size);
}

ListPtr ListPtr::getListElementInternal(unsigned int index) const {
  return WireHelpers::getWritableListReference(
      descriptor->elementDescriptor->asList(),
      reinterpret_cast<WireReference*>(ptr) + index,
      segment);
}

ListReadPtr ListPtr::asReadPtr() const {
  return ListReadPtr(descriptor, segment, ptr, elementCount,
      internal::sizeInBits(descriptor->elementSize),
      descriptor->elementSize == internal::FieldSize::STRUCT
          ? descriptor->elementDescriptor->asStruct()->fieldCount : 0,
      1 << 30);
}

StructReadPtr ListReadPtr::getStructElementInternal(unsigned int index) const {
  const internal::StructDescriptor* elementDescriptor;
  if (ptr == nullptr) {
    elementDescriptor = descriptor->defaultReferences()[index]->asStruct();
  } else {
    elementDescriptor = descriptor->elementDescriptor->asStruct();

    if (CAPNPROTO_EXPECT_FALSE(recursionLimit == 0)) {
      segment->getArena()->parseError(
          "Message is too deeply-nested or contains cycles.");
    } else {
      uint64_t indexBit = static_cast<uint64_t>(index) * stepBits;
      return StructReadPtr(
          elementDescriptor, segment, offsetPtr<uint8_t>(ptr, indexBit / 8),
          descriptor->defaultData, structFieldCount, indexBit % 8, recursionLimit - 1);
    }
  }

  return StructReadPtr(elementDescriptor, segment, nullptr, descriptor->defaultData, 0, 0, 0);
}

ListReadPtr ListReadPtr::getListElementInternal(unsigned int index, uint32_t size) const {
  if (ptr == nullptr) {
    return WireHelpers::readListReference(
        descriptor->defaultReferences()[index]->asList(),
        nullptr, segment, recursionLimit);
  } else {
    return WireHelpers::readListReference(
        descriptor->elementDescriptor->asList(),
        reinterpret_cast<const WireReference*>(offsetPtr<uint64_t>(ptr, index * (stepBits / 64))),
        segment, recursionLimit);
  }
}

}  // namespace capnproto
